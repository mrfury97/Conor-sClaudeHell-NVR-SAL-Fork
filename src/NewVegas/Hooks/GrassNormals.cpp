#include "GrassNormals.h"
#include <unordered_map>
#include <string>
#include <algorithm>

namespace GrassNormals {
	// NVR's sky hook (Hooks.cpp) replaces SkyShader::UpdateConstants at this vtable entry, and it works
	// in game, so it pins UpdateConstants to slot 31 of the BSShader vtable layout TallGrassShader shares.
	static constexpr UInt32 kSkyUpdateConstantsEntry = 0x10AFE94;
	static constexpr UInt32 kUpdateConstantsSlot = 31;
	static constexpr UInt32 kGetRTTISlot = 2;
	static constexpr UInt32 kVtblTallGrassShader = 0x10B8980;	// class_vtbls.h
	static constexpr UInt32 kVtblNiSourceTexture = 0x109B9EC;	// class_vtbls.h

	// Must match GRASS23x000TMS.pso.hlsl: GrassNormalMap : register(s10), GrassNormalParams : register(c199).
	// s10 is free in the grass shader (s0 diffuse, s9 shadow atlas); c199 sits past its TESR_ constants.
	static constexpr UInt32 kSampler = 10;
	static constexpr UInt32 kNoiseSampler = 11;
	static constexpr UInt32 kParamsRegister = 199;
	static constexpr UInt32 kNoiseSize = 64;

	static VirtFuncDetour s_detour;
	static bool s_installed = false;

	// Keyed by the D3D texture bound on stage 0 when grass draws -- its diffuse -- so the memory search
	// below runs once per grass texture. Keyed by grass geometry, as it was, it ran for every geometry
	// the engine creates as grass streams in around the player: hundreds in dense grass, each a walk
	// through memory, and a full re-search of everything on screen whenever the cache filled.
	struct TextureEntry {
		const void* texture;			// the Gamebryo texture owning it; null: not found, retried after kRetryMs
		const char* path;				// its filename pointer when this was built
		IDirect3DTexture9* normal;		// null: no normal map for this texture
		DWORD searched;					// GetTickCount when it was looked for
	};
	static constexpr DWORD kRetryMs = 60000;	// a failed search is rare (not grass-textured), and costs the walk again
	static constexpr size_t kMaxTextures = 1024;	// distinct grass textures; stale ones only pile up over a long session
	static std::unordered_map<IDirect3DBaseTexture9*, TextureEntry> s_textures;
	static std::unordered_map<std::string, IDirect3DTexture9*> s_normalsByPath;	// loaded once, kept for the session
	static bool s_loggedNoTexture = false;
	static bool s_loggedNoBound = false;
	static bool s_loggedFirst = false;

	// Colour-variation noise (GRASS23x000TMS.pso, s11): made once, bound with the normal map.
	static IDirect3DTexture9* s_noise = nullptr;
	static bool s_noiseFailed = false;

	// What this module last put on the device, so a run of grass draws with nothing changing makes no
	// device calls at all. Anything else may have changed s10, s11 or c199 once the engine switches
	// shaders, so SetShadersHook clears it (InvalidateState) and the next grass draw sets all again.
	static bool s_stateValid = false;
	static IDirect3DTexture9* s_boundNormal = nullptr;
	static float s_boundParams[4] = {};

	// ---- Memory reads that may be wrong, and so are guarded -------------------------------------
	// Nothing here trusts a guessed structure layout past what NVR's headers already assert: the grass
	// texture is found by walking the geometry's properties for objects that are provably textures, and
	// the one bound for the draw is preferred. Every read is guarded.

	static bool ReadVtableRTTIName(UInt32 aVtable, char* apName, UInt32 aSize) {
		// GetRTTI is read, never called: MSVC compiles each class's to `mov eax, imm32 / ret`.
		__try {
			const UInt8* code = *(const UInt8**)(aVtable + kGetRTTISlot * 4);
			if (code[0] != 0xB8 || code[5] != 0xC3) return false;
			const NiRTTI* rtti = *(const NiRTTI**)(code + 1);
			strncpy_s(apName, aSize, rtti->name, _TRUNCATE);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// The game executable's own image: code and vtables live there, never a texture, so the search
	// does not wander into it.
	static UInt32 s_imageStart = 0;
	static UInt32 s_imageEnd = 0;

	static bool LooksLikePointer(UInt32 aValue) {
		// No upper bound below 4 GB: FNV is commonly large-address-aware, with heap above 0x80000000.
		if (aValue < 0x10000 || aValue >= 0xFFFF0000 || (aValue & 3)) return false;
		return !(aValue >= s_imageStart && aValue < s_imageEnd);
	}

	static bool ReadWord(const void* apAddress, UInt32& arValue) {
		__try {
			arValue = *(const UInt32*)apAddress;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Any Gamebryo texture, whatever its class: its renderer data (NiTexture +0x24) points back at it
	// (NiDX9TextureData +0x08). A two-way link no unrelated pair of words forms by chance.
	static bool IsTexture(const void* apObject) {
		UInt32 vtable = 0;
		if (!ReadWord(apObject, vtable)) return false;
		if (vtable == kVtblNiSourceTexture) return true;
		UInt32 rendererData = 0;
		UInt32 parent = 0;
		if (!ReadWord((const UInt8*)apObject + 0x24, rendererData) || !LooksLikePointer(rendererData)) return false;
		return ReadWord((const UInt8*)rendererData + 0x08, parent) && parent == (UInt32)apObject;
	}

	static IDirect3DBaseTexture9* D3DTextureOf(const void* apTexture) {
		UInt32 rendererData = 0;
		UInt32 d3dTexture = 0;
		if (!ReadWord((const UInt8*)apTexture + 0x24, rendererData) || !rendererData) return nullptr;
		if (!ReadWord((const UInt8*)rendererData + 0x64, d3dTexture)) return nullptr;
		return (IDirect3DBaseTexture9*)d3dTexture;
	}

	// NiSourceTexture's filename (+0x30), accepted only if it reads as a .dds path.
	static const char* FilenameOf(const void* apTexture, char* apBuffer, UInt32 aSize) {
		__try {
			const char* name = *(const char* const*)((const UInt8*)apTexture + 0x30);
			if (!name) return nullptr;
			UInt32 length = 0;
			for (; length < aSize - 1 && name[length]; length++) {
				char c = name[length];
				if (c < 0x20 || c > 0x7E) return nullptr;
				apBuffer[length] = c;
			}
			apBuffer[length] = 0;
			if (length < 5 || _stricmp(apBuffer + length - 4, ".dds")) return nullptr;
			return name;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	// Class name of a Gamebryo object, for the diagnostic dump: GetRTTI's code read, never called.
	static bool ObjectClassName(const void* apObject, char* apName, UInt32 aSize) {
		UInt32 vtable = 0;
		if (!ReadWord(apObject, vtable) || !(vtable >= s_imageStart && vtable < s_imageEnd)) return false;
		return ReadVtableRTTIName(vtable, apName, aSize);
	}

	struct SearchState {
		IDirect3DBaseTexture9* bound;
		const void* match;			// the texture bound for the draw
		const void* visited[256];
		UInt32 visitedCount;
	};

	// Depth-limited walk of an object's first words for textures: a property or geometry holds its
	// texture directly or through arrays and map objects.
	static void Scan(const void* apObject, UInt32 aDepth, SearchState& arState) {
		for (UInt32 v = 0; v < arState.visitedCount; v++)
			if (arState.visited[v] == apObject) return;
		if (arState.visitedCount < 256) arState.visited[arState.visitedCount++] = apObject;

		const UInt32 width = aDepth >= 3 ? 32 : 16;
		for (UInt32 i = 0; i < width && !arState.match; i++) {
			UInt32 value = 0;
			if (!ReadWord((const UInt32*)apObject + i, value)) return;
			if (!LooksLikePointer(value)) continue;

			const void* candidate = (const void*)value;
			if (IsTexture(candidate)) {
				if (D3DTextureOf(candidate) == arState.bound) arState.match = candidate;
				continue;
			}
			if (aDepth > 1) Scan(candidate, aDepth - 1, arState);
		}
	}

	// Once: the Gamebryo objects within two steps of each root, by offset and class name, and every
	// texture met on the way. What the log needs to show where grass keeps its texture if the search
	// above ever comes up empty.
	static void DumpObject(const char* apLabel, const void* apObject, UInt32 aDepth, UInt32 aIndent) {
		char name[128] = "?";
		ObjectClassName(apObject, name, sizeof(name));
		Logger::Log("[GrassNormals] %*s%s %08X %s", aIndent * 2, "", apLabel, (UInt32)apObject, name);
		if (!aDepth) return;
		for (UInt32 i = 0; i < 32; i++) {
			UInt32 value = 0;
			if (!ReadWord((const UInt32*)apObject + i, value)) return;
			if (!LooksLikePointer(value)) continue;
			const void* child = (const void*)value;
			char label[32];
			sprintf_s(label, "+%02X", i * 4);
			if (IsTexture(child)) {
				char filename[MAX_PATH] = "(no filename)";
				FilenameOf(child, filename, sizeof(filename));
				Logger::Log("[GrassNormals] %*s%s texture %08X d3d %08X %s", (aIndent + 1) * 2, "", label, value, (UInt32)D3DTextureOf(child), filename);
				continue;
			}
			char childName[128];
			if (ObjectClassName(child, childName, sizeof(childName)) || aDepth > 1)
				DumpObject(label, child, aDepth - 1, aIndent + 1);
		}
	}

	static const void* CurrentGeometry() {
		UInt32 holder = 0;
		UInt32 geometry = 0;
		// The holder may be a static inside the executable, so only non-null is required of it.
		if (!ReadWord((const void*)0x011F91E0, holder) || holder < 0x10000) return nullptr;
		if (!ReadWord((const void*)holder, geometry) || !LooksLikePointer(geometry)) return nullptr;
		return (const void*)geometry;
	}

	// The Gamebryo texture whose D3D texture is apBound, searched for from the draw's properties and
	// geometry. Slow (a guarded walk through memory), so NormalMapFor caches what it finds.
	static const void* FindTexture(const NiPropertyState* apProperties, IDirect3DBaseTexture9* apBound) {
		const void* texturing = apProperties->m_spTextureProperty;
		const void* shade = apProperties->m_spShadeProperty;
		const void* geometry = CurrentGeometry();

		SearchState state = {};
		state.bound = apBound;
		if (texturing) Scan(texturing, 4, state);
		if (!state.match && shade) Scan(shade, 4, state);
		if (!state.match && geometry) Scan(geometry, 3, state);

		if (state.match && !s_loggedFirst) {
			s_loggedFirst = true;
			char filename[MAX_PATH] = "(no filename)";
			FilenameOf(state.match, filename, sizeof(filename));
			Logger::Log("[GrassNormals] first grass texture found: %s", filename);
		}
		if (!state.match && !s_loggedNoTexture) {
			s_loggedNoTexture = true;
			Logger::Log("[GrassNormals] could not find the texture of a grass geometry; its grass draws without a normal map. Dump follows (once):");
			Logger::Log("[GrassNormals] texture bound on stage 0: %08X", (UInt32)apBound);
			if (texturing) DumpObject("texturing property", texturing, 2, 0);
			if (shade) DumpObject("shade property", shade, 2, 0);
			if (geometry) DumpObject("geometry", geometry, 1, 0);
		}
		return state.match;
	}

	// ---- Normal map files ------------------------------------------------------------------------

	// textures\landscape\grass\foo.dds -> Data\Textures\landscape\grass\foo_n.dds
	static bool NormalMapPath(const char* apDiffuse, std::string& arPath) {
		std::string path = apDiffuse;
		std::transform(path.begin(), path.end(), path.begin(), [](char c) { return c == '/' ? '\\' : (char)tolower((unsigned char)c); });
		if (path.size() < 5 || path.compare(path.size() - 4, 4, ".dds")) return false;
		if (path.size() >= 6 && !path.compare(path.size() - 6, 6, "_n.dds")) return false;
		path.erase(path.size() - 4);
		path += "_n.dds";

		if (!path.compare(0, 5, "data\\")) arPath = path;
		else if (!path.compare(0, 9, "textures\\")) arPath = "Data\\" + path;
		else arPath = "Data\\Textures\\" + path;
		return true;
	}

	static IDirect3DTexture9* LoadNormalMap(const char* apDiffuse) {
		std::string path;
		if (!NormalMapPath(apDiffuse, path)) return nullptr;

		auto loaded = s_normalsByPath.find(path);
		if (loaded != s_normalsByPath.end()) return loaded->second;

		IDirect3DTexture9* texture = nullptr;
		if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			Logger::Log("[GrassNormals] no normal map for %s (looked for %s)", apDiffuse, path.c_str());
		}
		else if (FAILED(D3DXCreateTextureFromFileA(TheRenderManager->device, path.c_str(), &texture))) {
			Logger::Log("[GrassNormals] could not load %s", path.c_str());
			texture = nullptr;
		}
		else {
			Logger::Log("[GrassNormals] loaded %s", path.c_str());
		}
		s_normalsByPath[path] = texture;
		return texture;
	}

	// The normal map for the grass texture bound on stage 0, from the cache when it is still valid.
	static IDirect3DTexture9* NormalMapFor(const NiPropertyState* apProperties, IDirect3DBaseTexture9* apBound) {
		if (!apBound) {
			if (!s_loggedNoBound) {
				s_loggedNoBound = true;
				Logger::Log("[GrassNormals] a grass draw had no texture bound on stage 0; it draws without a normal map");
			}
			return nullptr;
		}

		auto cached = s_textures.find(apBound);
		if (cached != s_textures.end()) {
			const TextureEntry& entry = cached->second;
			if (!entry.texture) {
				if (GetTickCount() - entry.searched < kRetryMs) return nullptr;
			}
			else {
				// Revalidated every time, a few guarded reads: a texture freed with its cell can be
				// replaced by another at the same address, which must not keep the old one's map.
				UInt32 namePointer = 0;
				if (IsTexture(entry.texture) && D3DTextureOf(entry.texture) == apBound &&
					ReadWord((const UInt8*)entry.texture + 0x30, namePointer) && (const char*)namePointer == entry.path)
					return entry.normal;
			}
		}

		TextureEntry entry = { FindTexture(apProperties, apBound), nullptr, nullptr, GetTickCount() };
		if (entry.texture) {
			UInt32 namePointer = 0;
			char filename[MAX_PATH];
			if (ReadWord((const UInt8*)entry.texture + 0x30, namePointer)) entry.path = (const char*)namePointer;
			if (FilenameOf(entry.texture, filename, sizeof(filename))) entry.normal = LoadNormalMap(filename);
		}
		if (s_textures.size() >= kMaxTextures) s_textures.clear();
		s_textures[apBound] = entry;
		return entry.normal;
	}

	// 64x64, two independent random channels (R, G), fixed seed so the pattern is the same every run.
	// Bilinear filtering between random texels is value noise: the smooth patches the variation wants,
	// for one texture read. Managed pool, so it survives a device reset.
	static IDirect3DTexture9* NoiseTexture() {
		if (s_noise || s_noiseFailed) return s_noise;
		if (FAILED(TheRenderManager->device->CreateTexture(kNoiseSize, kNoiseSize, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_noise, nullptr))) {
			s_noise = nullptr;
			s_noiseFailed = true;
			Logger::Log("[GrassNormals] could not create the colour-variation noise texture; the shader works it out instead");
			return nullptr;
		}
		D3DLOCKED_RECT locked;
		if (FAILED(s_noise->LockRect(0, &locked, nullptr, 0))) {
			s_noise->Release();
			s_noise = nullptr;
			s_noiseFailed = true;
			return nullptr;
		}
		UInt32 seed = 0x9E3779B9u;
		for (UInt32 y = 0; y < kNoiseSize; y++) {
			UInt32* row = (UInt32*)((UInt8*)locked.pBits + y * locked.Pitch);
			for (UInt32 x = 0; x < kNoiseSize; x++) {
				seed = seed * 1664525u + 1013904223u;
				UInt32 red = seed >> 24;
				seed = seed * 1664525u + 1013904223u;
				UInt32 green = seed >> 24;
				row[x] = 0xFF000000u | (red << 16) | (green << 8);
			}
		}
		s_noise->UnlockRect(0);
		return s_noise;
	}

	static void SetSamplerStates(NiDX9RenderState* apRenderState, UInt32 aSampler, UInt32 aMipFilter) {
		apRenderState->SetSamplerState(aSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP, false);
		apRenderState->SetSamplerState(aSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP, false);
		apRenderState->SetSamplerState(aSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR, false);
		apRenderState->SetSamplerState(aSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR, false);
		apRenderState->SetSamplerState(aSampler, D3DSAMP_MIPFILTER, aMipFilter, false);
		apRenderState->SetSamplerState(aSampler, D3DSAMP_SRGBTEXTURE, FALSE, false);
	}

	// ---- The hook ------------------------------------------------------------------------------------

	void InvalidateState() {
		s_stateValid = false;
	}

	static void Bind(const NiPropertyState* apProperties) {
		GrassShaders* grass = TheShaderManager->Shaders.Grass;
		if (!s_installed || !grass || !grass->Enabled || !apProperties) return;

		NiDX9RenderState* renderState = TheRenderManager->renderState;
		IDirect3DTexture9* normal = nullptr;
		if (grass->NormalMapStrength > 0.0f) {
			normal = NormalMapFor(apProperties, renderState->GetTexture(0));
		}
		IDirect3DTexture9* noise = NoiseTexture();

		// x: strength, 0 when this grass has no map; y: green channel sign; z: noise texture bound.
		float params[4] = { normal ? grass->NormalMapStrength : 0.0f, grass->NormalMapFlipGreen ? -1.0f : 1.0f, noise ? 1.0f : 0.0f, 0.0f };

		// Through the render state, not the device, so NiDX9RenderState's texture cache stays in step.
		// s10 is unbound when this grass has no map, so nothing from another grass type is left on it.
		if (!s_stateValid || normal != s_boundNormal) {
			renderState->SetTexture(kSampler, normal);
			if (normal) SetSamplerStates(renderState, kSampler, D3DTEXF_LINEAR);
			s_boundNormal = normal;
		}
		if (!s_stateValid && noise) {
			renderState->SetTexture(kNoiseSampler, noise);
			SetSamplerStates(renderState, kNoiseSampler, D3DTEXF_NONE);
		}
		if (!s_stateValid || memcmp(params, s_boundParams, sizeof(params))) {
			TheRenderManager->device->SetPixelShaderConstantF(kParamsRegister, params, 1);
			memcpy(s_boundParams, params, sizeof(params));
		}
		s_stateValid = true;
	}

	static void __fastcall TallGrassShader__UpdateConstants(TallGrassShader* apThis, void*, const NiPropertyState* apProperties) {
		ThisCall(s_detour.GetOverwrittenAddr(), apThis, apProperties);
		Bind(apProperties);
	}

	void Install() {
		if (s_installed) return;

		// Both vtables must be what they are taken for before an entry is replaced: SkyShader's at
		// the base the sky hook's slot implies, TallGrassShader's at class_vtbls.h's address.
		const UInt32 skyVtable = kSkyUpdateConstantsEntry - kUpdateConstantsSlot * 4;
		char skyName[64] = {};
		char grassName[64] = {};
		bool skyOk = ReadVtableRTTIName(skyVtable, skyName, sizeof(skyName)) && !strcmp(skyName, "SkyShader");
		bool grassOk = ReadVtableRTTIName(kVtblTallGrassShader, grassName, sizeof(grassName)) && !strcmp(grassName, "TallGrassShader");
		if (!skyOk || !grassOk) {
			Logger::Log("[GrassNormals] vtable check failed (sky '%s', grass '%s'); grass normal maps disabled", skyName, grassName);
			return;
		}

		HMODULE image = GetModuleHandleA(nullptr);
		IMAGE_NT_HEADERS* headers = (IMAGE_NT_HEADERS*)((UInt8*)image + ((IMAGE_DOS_HEADER*)image)->e_lfanew);
		s_imageStart = (UInt32)image;
		s_imageEnd = s_imageStart + headers->OptionalHeader.SizeOfImage;

		s_detour.ReplaceVirtualFunc(kVtblTallGrassShader + kUpdateConstantsSlot * 4, TallGrassShader__UpdateConstants);
		s_installed = true;
		Logger::Log("[GrassNormals] installed");
	}
}
