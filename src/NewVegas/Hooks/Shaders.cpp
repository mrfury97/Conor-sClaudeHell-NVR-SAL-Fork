#pragma once

// 0xB89D80
VirtFuncDetour kSkyShaderConstantsDetour;
void __fastcall SkyShader__UpdateConstants(SkyShader* apThis, void*, const NiPropertyState* apProperties) {
    const SkyShaderProperty* pShaderProp = apProperties->GetShadeProperty<SkyShaderProperty const>();
    uint32_t eSkyObjectType = pShaderProp->eSkyObjectType;
    TheShaderManager->ShaderConst.skyObjectID = eSkyObjectType;

    ThisCall(kSkyShaderConstantsDetour.GetOverwrittenAddr(), apThis, apProperties);

    TheRenderManager->device->SetPixelShaderConstantF(20, (const float*)&TheShaderManager->ShaderConst.skyObjectID, 1);
}

// Per-object material data for the PBR object shaders: ObjectMaterial, pixel register c150
// (Shaders/Includes/Object.hlsl), set directly, not through the TESR_ constant table.
//   x = 1 when the mesh has the engine's Specular flag, so the diffuse-only shader variants
//       know not to add a highlight the game draws in its own specular pass.
//   y = the engine's specular distance fade, BSShaderPPLightingProperty::GetSpecularLODFade
//       (0xB66B80): 1 up to fSpecularLODStartFade, 0 from fSpecularLODEnd on, where the game
//       stops drawing the specular pass. Vanilla fades the highlight with it so it does not pop.
// Written by SetShadersHook (once per batch) and by the per-geometry hooks below (every draw):
// within a batch each mesh has its own.
void WriteObjectMaterial(NiGeometry* Geometry) {
    float ObjectMaterial[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    if (Geometry) {
        BSShaderProperty* ShaderProperty = static_cast<BSShaderProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));
        if (ShaderProperty && ShaderProperty->GetFlag(BSSP_SPECULAR)) {
            ObjectMaterial[0] = 1.0f;
            const float StartFade = *(float*)0x011F9454;   // BSShaderManager::fSpecularLODStartFade
            const float End = *(float*)0x011F9458;         // BSShaderManager::fSpecularLODEnd
            const float Distance = ShaderProperty->fLODFade;   // fCameraDistance in the engine's own layout (0x34)
            const bool Stinger = (ShaderProperty->ulFlags[1] & BSShaderProperty::stinger_prop) != 0;
            if (End > 0.0f && !Stinger && Distance > StartFade)
                ObjectMaterial[1] = Distance >= End ? 0.0f : 1.0f - (Distance - StartFade) / max(End - StartFade, 1e-3f);
        }
    }
    TheRenderManager->device->SetPixelShaderConstantF(150, ObjectMaterial, 1);
}

// --- Authored material maps (_rmaos) ---------------------------------------------------------
// The texture slot for Community Shaders style PBR materials (docs/pbr-rework-design.md): a
// material whose diffuse texture `foo.dds` has a companion `foo_rmaos.dds` (loose or in a BSA) is
// lit with true PBR (Shaders/Includes/Object.hlsl). Channels: R roughness, G metalness, B ambient
// occlusion, A specular (F0). No NIF editing: the companion is found by name.
//
// The diffuse texture is the shader property's first diffuse slot
// (BSShaderPPLightingProperty::ppTextures[0][0], an NiSourceTexture) and its path NVR already
// reads (ddsPath1). The companion is looked up once per texture through the engine's own
// FileFinder::GetFile (0xAFDF20), which takes loose files first and then the archives, as the
// game does for every texture; the bytes become a D3D texture with D3DXCreateTextureFromFileInMemory.
// Textures without a companion are remembered as such, so the lookup never repeats.
//
// Per draw (objects and parallax): the map goes to sampler s10 and MaterialMap (pixel c152) x to
// 1; the dynamic environment cube (effects/DynamicCubemaps.h), when there is one, to s11 and
// MaterialMap y to 1. The engine's highlight passes are muted for such materials (they have no
// albedo for a metal's F0); the other passes draw the highlight themselves. So are its env map
// passes (BSSM_ENVMAP .. BSSM_2x_ENVMAP_W, 0x244-0x24A, vanilla shaders): the material reflects
// its environment itself, and the vanilla cubemap on top would reflect it twice. After the draw
// s10/s11 go back to exactly what they held before (textures and sampler states) and MaterialMap to
// 0, so nothing else (tree branches share these shaders; land reads normal maps there) ever sees it.
namespace MaterialMaps {

    struct TextureEntry {
        std::string         Path;       // the diffuse path the entry was made for: a freed texture's address can be reused
        IDirect3DTexture9*  Map;        // nullptr: no companion
    };
    static std::unordered_map<const NiSourceTexture*, TextureEntry> sByTexture;
    static std::unordered_map<std::string, IDirect3DTexture9*> sByPath;   // by companion path, across textures
    static bool  sBound = false;
    static bool  sMuted = false;
    static DWORD sSavedWriteMask = 0xF;

    // s10/s11 are shared with the game: land binds NormalMap[3], [4] there (TerrainTemplate). So the
    // map, the cube and their sampler states go through the engine's render state cache
    // (NiDX9RenderState::SetTexture / SetSamplerState, as MaterialPass does), never straight to the
    // device. The cache then knows what is bound, and land re-sends its own textures and states where
    // they differ: nothing to save or restore around each draw. (Set on the device behind the cache's
    // back, land kept s11's CLAMP and LINEAR and drew that layer's normal map smeared from its edge,
    // and a texture cleared to NULL stayed empty for the next mesh the cache thought still had it.)
    // Unchanged values cost a compare in the cache, so after the first PBR draw this is nearly free.
    static void BindSampler(NiDX9RenderState* State, UInt32 Slot, IDirect3DBaseTexture9* Texture, D3DTEXTUREADDRESS Address) {
        State->SetTexture(Slot, Texture);
        State->SetSamplerState(Slot, D3DSAMP_ADDRESSU, Address, false);
        State->SetSamplerState(Slot, D3DSAMP_ADDRESSV, Address, false);
        State->SetSamplerState(Slot, D3DSAMP_ADDRESSW, Address, false);
        State->SetSamplerState(Slot, D3DSAMP_MINFILTER, D3DTEXF_LINEAR, false);
        State->SetSamplerState(Slot, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR, false);
        State->SetSamplerState(Slot, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR, false);
        State->SetSamplerState(Slot, D3DSAMP_SRGBTEXTURE, FALSE, false);
    }

    // The engine's file, read whole through its stream interface, as Font::Load (0xA15320) does:
    //   FileFinder::GetFile(name, READ_ONLY, 0x4000, ARCHIVE_TYPE_TEXTURES)
    //   NiFile::m_bGood (+0x2C), BSFile::GetSize (vtable slot 10), NiBinaryStream::m_pfnRead (+0x08,
    //   __cdecl), Destroy (vtable slot 0).
    static IDirect3DTexture9* LoadFromGame(const char* Path) {
        typedef void* (__cdecl* GetFileFn)(const char*, UInt32, UInt32, UInt32);
        typedef UInt32 (__cdecl* ReadFn)(void*, void*, UInt32, UInt32*, UInt32);
        void* File = ((GetFileFn)0xAFDF20)(Path, 0, 0x4000, 2);
        if (!File) return nullptr;

        IDirect3DTexture9* Map = nullptr;
        UInt32* VTable = *(UInt32**)File;
        if (*(bool*)((UInt8*)File + 0x2C)) {
            const UInt32 Size = ThisCall(VTable[10], File);
            if (Size > 128 && Size < 0x10000000) {
                std::vector<UInt8> Data(Size);
                UInt32 ComponentSize = 1;
                const UInt32 Read = (*(ReadFn*)((UInt8*)File + 0x08))(File, Data.data(), Size, &ComponentSize, 1);
                if (Read == Size && FAILED(D3DXCreateTextureFromFileInMemory(TheRenderManager->device, Data.data(), Size, &Map)))
                    Map = nullptr;
            }
        }
        ThisCall(VTable[0], File, true);
        return Map;
    }

    // "Data\Textures\Armor\Leather01.dds" -> "textures\armor\leather01_rmaos.dds"
    static bool CompanionPath(const char* Diffuse, std::string& Out) {
        std::string Path(Diffuse);
        for (char& c : Path) c = (c == '/') ? '\\' : (char)tolower((unsigned char)c);
        const size_t Data = Path.find("data\\");
        if (Data != std::string::npos) Path.erase(0, Data + 5);
        if (Path.compare(0, 9, "textures\\") != 0) Path.insert(0, "textures\\");
        if (Path.size() < 4 || Path.compare(Path.size() - 4, 4, ".dds") != 0) return false;
        if (Path.size() >= 10 && Path.compare(Path.size() - 10, 10, "_rmaos.dds") == 0) return false;
        Path.insert(Path.size() - 4, "_rmaos");
        Out = Path;
        return true;
    }

    // Every loaded map is released at each loading screen (a door, a load, fast travel: see
    // UpdateFaceGenInteriorFlag), and found again on first use. Without this they stayed in video
    // memory for the whole session. Run between frames, so none is in use; slot 10 is unbound
    // through the render state cache first, so neither the device nor the cache keeps a freed map.
    static void ReleaseAll() {
        if (sByPath.empty() && sByTexture.empty()) return;
        TheRenderManager->renderState->SetTexture(10, nullptr);
        size_t Released = 0;
        for (auto& Entry : sByPath)
            if (Entry.second) { Entry.second->Release(); Released++; }
        sByPath.clear();
        sByTexture.clear();
        sBound = false;
        if (Released) Logger::Log("MaterialMaps: released %u maps", (UInt32)Released);
    }

    static IDirect3DTexture9* Find(const NiSourceTexture* Diffuse) {
        const char* Name = Diffuse->ddsPath1;
        if (!Name || !*Name) return nullptr;

        auto Known = sByTexture.find(Diffuse);
        if (Known != sByTexture.end() && Known->second.Path == Name) return Known->second.Map;

        IDirect3DTexture9* Map = nullptr;
        std::string Companion;
        if (CompanionPath(Name, Companion)) {
            auto ByPath = sByPath.find(Companion);
            if (ByPath != sByPath.end())
                Map = ByPath->second;
            else {
                Map = LoadFromGame(Companion.c_str());
                if (!Map) Map = LoadFromGame(("data\\" + Companion).c_str());
                sByPath[Companion] = Map;
                if (Map) Logger::Log("MaterialMaps: %s", Companion.c_str());
            }
        }
        sByTexture[Diffuse] = { std::string(Name), Map };
        return Map;
    }

    static bool Active() {
        PBRShaders* PBR = TheShaderManager->Shaders.PBR;
        return TheSettingManager->SettingsMain.Main.RenderEffects && PBR && PBR->Enabled;
    }

    // The engine's highlight-only passes: objects SLS2047/2049/2051/2053/2055 (not the hair
    // ones), parallax PAR2024-PAR2028.
    static bool IsHighlightPass(const NiD3DPixelShaderEx* PixelShader) {
        if (!PixelShader || !PixelShader->Name) return false;
        int n = -1;
        if (!strncmp(PixelShader->Name, "SLS", 3)) {
            n = atoi(PixelShader->Name + 3);
            return n >= 2047 && n <= 2056 && (n % 2) == 1;
        }
        if (!strncmp(PixelShader->Name, "PAR", 3)) {
            n = atoi(PixelShader->Name + 3);
            return n >= 2024 && n <= 2028;
        }
        return false;
    }

    // Before each object or parallax draw. Run before MergedLights::OnDraw.
    void EndDraw();

    void OnDraw(NiGeometry* Geometry, const NiD3DPixelShaderEx* PixelShader) {
        if (sBound || sMuted) EndDraw();   // a previous draw's PostGeometry did not run (see MergedLights::OnDraw)
        if (!Geometry || !Active()) return;
        const UInt16 PassType = *(UInt16*)0x011F91E4;   // BSShaderManager::eCurrentPass
        const bool EnvPass = PassType >= 0x244 && PassType <= 0x24A;
        if (!EnvPass && (!PixelShader || PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup)) return;
        BSShaderPPLightingProperty* Property = static_cast<BSShaderPPLightingProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));
        if (!Property || !Property->ppTextures[0] || !Property->ppTextures[0][0]) return;
        IDirect3DTexture9* Map = Find(Property->ppTextures[0][0]);
        if (!Map) return;
        TheShaderManager->Effects.DynamicCubemaps->PBRDrawn = true;   // the cube is worth updating this frame

        IDirect3DDevice9* Device = TheRenderManager->device;
        // The game's env map passes come after the mesh's light, texture and highlight passes, so
        // kept they lay the vanilla cubemap over the PBR result ([Shaders.PBR.Main] VanillaEnvMapOnPBR).
        // They draw with the game's own shaders and need nothing bound here.
        if (EnvPass && TheShaderManager->Shaders.PBR->MaterialSettings.VanillaEnvMapOnPBR) return;
        if (EnvPass || IsHighlightPass(PixelShader)) {
            Device->GetRenderState(D3DRS_COLORWRITEENABLE, &sSavedWriteMask);
            Device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            sMuted = true;
            return;
        }
        NiDX9RenderState* State = TheRenderManager->renderState;
        BindSampler(State, 10, Map, D3DTADDRESS_WRAP);
        IDirect3DCubeTexture9* Environment = TheShaderManager->Effects.DynamicCubemaps->GetEnvironment();
        if (Environment) BindSampler(State, 11, Environment, D3DTADDRESS_CLAMP);
        const float Flag[4] = { 1.0f, Environment ? 1.0f : 0.0f, 0.0f, 0.0f };
        Device->SetPixelShaderConstantF(152, Flag, 1);
        sBound = true;
    }

    // After the draw. Run after MergedLights::EndDraw.
    void EndDraw() {
        IDirect3DDevice9* Device = TheRenderManager->device;
        if (sBound) {
            // The map and cube stay bound (and known to the cache): MaterialMap at 0 is what keeps
            // every other draw from sampling them.
            const float Zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            Device->SetPixelShaderConstantF(152, Zero, 1);
            sBound = false;
        }
        if (sMuted) {
            Device->SetRenderState(D3DRS_COLORWRITEENABLE, sSavedWriteMask);
            sMuted = false;
        }
    }
}

// --- Merged light passes ---------------------------------------------------------------------
// With more lamps than its first pass takes, a mesh is lit in several: a light-only pass with the
// sun and up to two lamps (AD2/AD3), then additive passes of two or three more each
// (BSSM_DIFFUSEPT2/3, blended ONE/ONE), the texture pass that multiplies the frame by the
// texture, and, for meshes with the engine's Specular flag, highlight passes for the sun and the
// lamps (BSSM_2x_SPECULARDIR/PT/PT2/PT3). Each pass encodes its own light, so the frame sums
// sqrt(a) + sqrt(b) where linear lighting needs sqrt(a + b): a mesh lit in several passes came
// out brighter than its neighbour lit in one, with a hard edge between them. The highlight passes
// also fade their layer by its own brightness, so the same mesh shone differently lit in one pass.
//
// Here the light-only pass takes all of it: the hook reads the lamps off the mesh's own pass list
// (BSShaderProperty::pRenderPassArray), uploads them to pixel registers c154-c186
// (Shaders/Includes/MergedLights.hlsl) and the shader sums them with the rest, highlights
// included (divided by the texture the texture pass multiplies back in). The additive passes and
// highlight passes, drawn after every light-only pass (the batch renderer goes by pass type),
// are then muted: colour writes off, as the skin highlight passes are. A pass is muted only if
// every one of its lights went into its mesh's first pass; anything else keeps the engine's
// passes.
//
// Covered: objects (ShadowLightShader), hair (HairShader, which uses ShadowLightShader's passes;
// its own hair highlight passes stay), parallax (ParallaxShader) and skin (SkinShader, whose
// highlights the skin shader always draws itself), the projected-shadow forms of the lamp passes
// (NVR's lamp shaders never applied that shadow to lamps), and first-person meshes. Not tree
// branches: their light-only passes run the SpeedTree vertex shaders, which NVR does not replace,
// so there is no world frame to light the lamps with.
//
// Light constants as vanilla's (BSShaderLightingProperty::SetLight1x2x, 0xB70820, and
// ShadowLightShader::SetupGeometryConstants_Lights, 0xB78A90): colour = diffuse x dimmer (capped
// at 1 without HDR) x the property's forced darkness x the light's LOD dimmer, black when the
// forced darkness is under 1; radius = the light's specular red; position = its world position
// plus ShadowSceneNode::kLightingOffset, here made camera-relative (minus NiRenderer::kPosAdjust).
// --- Material debug views on multi-pass meshes ----------------------------------------------------
// [Shaders.PBR.Main] DebugView writes its picture in a mesh's first lit pass. A mesh the game draws in
// several passes (more lamps than one pass takes: GetRenderPasses_2x, 0xBDF790) then has the texture
// pass multiply the frame by its texture and the lamp and highlight passes add to it, so whole meshes
// showed their textures instead of the view. While a view is on those later passes are muted: the
// lamp passes SLS2045/2046 and PAR2021/2022, the highlight passes SLS2047-2056 and PAR2024-2028, and
// the game's own 1.x texture and ambient pass shaders (SLS1xxx, PAR1xxx).
namespace DebugViewPasses {

    static bool  sMuted = false;
    static DWORD sSavedWriteMask = 0xF;

    static bool IsLaterPass(const NiD3DPixelShaderEx* PixelShader) {
        if (!PixelShader || !PixelShader->Name) return false;
        const char* Name = PixelShader->Name;
        if (!strncmp(Name, "SLS1", 4) || !strncmp(Name, "PAR1", 4)) return true;
        if (!strncmp(Name, "SLS", 3)) { const int n = atoi(Name + 3); return n >= 2045 && n <= 2056; }
        if (!strncmp(Name, "PAR", 3)) { const int n = atoi(Name + 3); return (n >= 2021 && n <= 2022) || (n >= 2024 && n <= 2028); }
        return false;
    }

    void OnDraw(const NiD3DPixelShaderEx* PixelShader) {
        PBRShaders* PBR = TheShaderManager->Shaders.PBR;
        if (!TheSettingManager->SettingsMain.Main.RenderEffects || !PBR || !PBR->Enabled || PBR->MaterialSettings.DebugView <= 0) return;
        if (sMuted || !IsLaterPass(PixelShader)) return;
        TheRenderManager->device->GetRenderState(D3DRS_COLORWRITEENABLE, &sSavedWriteMask);
        TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
        sMuted = true;
    }

    void EndDraw() {
        if (!sMuted) return;
        TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, sSavedWriteMask);
        sMuted = false;
    }
}

// --- Lamp shadows in the object shaders -------------------------------------------------------
// Shaders/Includes/PointShadow.hlsl shadows each lamp's own light. Per draw, for each light of the
// game's current pass (PSLightColor[j] is the pass's light j, ShadowLightShader::
// SetupGeometryOpt_Lights*) and each merged lamp (MergedLights): the anchor and radius of the shadow
// slot its cluster holds (ShaderManager::GetNearbyLights, ShadowManager::LampSlot), and slot * 2 +
// fade, -1 for a lamp without one. EndDraw puts -1 back, so no other draw reads them.
namespace ForwardPointShadows {

    static const UINT BaseRegister = 190;       // TESR_PointShadowBase[6]
    static const UINT BaseInfoRegister = 196;   // TESR_PointShadowBaseInfo[2]
    static const UINT MergedRegister = 198;     // TESR_PointShadowMerged[16]
    static const UINT BaseLights = 6;
    static bool sUploaded = false;
    static const UINT FillRegister = 216;       // TESR_InverseSquareFill[2] (Shaders/Includes/InverseSquare.hlsl)
    static bool sFillUploaded = false;

    bool Active() {
        ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
        return TheSettingManager->SettingsMain.Main.RenderEffects && Shadows && Shadows->Constants.PointShadowData.x > 0.0f && TheShadowManager;
    }

    // A lamp's shadow info (slot * 2 + fade, or -1), and its slot's anchor (camera-relative, as the
    // merged lamps' positions) and radius into Anchor.
    // The anchor in the shaders' frame. World draws: less the camera's position, the frame both the
    // cube was drawn in (ShadowManager::RenderShadowCubeMap's Eye) and the shaders rebuild positions
    // in (GetShadowWorldPos: the view has no translation, CameraLocation being zero) -- kPosAdjust
    // plus the lighting offset only matched that while the game had not moved either, and the
    // shadows slid with the view when it had. First-person draws keep that conversion: the game
    // moves the lighting offset for the viewmodel on purpose.
    bool sFirstPersonDraw = false;
    float LampInfo(const ShadowSceneLight* Light, float* Anchor) {
        if (!Light || !Active()) return -1.0f;
        const auto Found = TheShadowManager->LampSlot.find(Light);
        if (Found == TheShadowManager->LampSlot.end()) return -1.0f;
        const int Slot = Found->second;
        if (Slot < 0 || Slot >= ShadowSlotsMax) return -1.0f;
        const D3DXVECTOR4& Position = TheShadowManager->SlotPosition[Slot];   // the anchor, world space; w the radius
        if (Position.w <= 0.0f) return -1.0f;
        const NiPoint3& PosAdjust = *(NiPoint3*)0x011F474C;                   // NiRenderer::kPosAdjust
        void* SceneNode = *(void**)0x011F91C8;                                // BSShaderManager::pShadowSceneNode[0]
        const NiPoint3 Offset = SceneNode ? *(NiPoint3*)((UInt8*)SceneNode + 0x1E4) : NiPoint3(0.0f, 0.0f, 0.0f);   // kLightingOffset
        const D3DXVECTOR4& Camera = TheRenderManager->CameraPosition;
        if (sFirstPersonDraw) {
            Anchor[0] = Position.x + Offset.x - PosAdjust.x;
            Anchor[1] = Position.y + Offset.y - PosAdjust.y;
            Anchor[2] = Position.z + Offset.z - PosAdjust.z;
        }
        else {
            Anchor[0] = Position.x - Camera.x;
            Anchor[1] = Position.y - Camera.y;
            Anchor[2] = Position.z - Camera.z;
        }
        Anchor[3] = Position.w;
        // Develop.DebugMode: how far kPosAdjust and the lighting offset are from what the cube used.
        static UInt32 LogCalls = 0;
        if (TheSettingManager->SettingsMain.Develop.DebugMode && !sFirstPersonDraw && ++LogCalls >= 20000) {
            LogCalls = 0;
            Logger::Log("ForwardPointShadows: world draw: kPosAdjust - camera = %.2f %.2f %.2f, lighting offset = %.2f %.2f %.2f",
                PosAdjust.x - Camera.x, PosAdjust.y - Camera.y, PosAdjust.z - Camera.z, Offset.x, Offset.y, Offset.z);
        }
        const float Fade = std::clamp(TheShadowManager->SlotFadeValue[Slot], 0.0f, 1.0f);
        return (float)Slot * 2.0f + Fade;
    }

    void EndDraw();

    // The current pass's lights, for a draw with one of NVR's object or parallax pixel shaders.
    void OnDraw(NiGeometry* Geometry, const NiD3DPixelShaderEx* PixelShader);
}

namespace MergedLights {

    struct RenderPassData {                  // BSShaderProperty::RenderPass
        NiGeometry*         Geometry;        // 00
        UInt16              PassEnum;        // 04
        UInt8               AccumulationHint;
        bool                FirstPass;
        bool                NoFog;           // 08
        UInt8               NumLights;
        UInt8               MaxNumLights;
        char                LandTexture;
        ShadowSceneLight**  SceneLights;     // 0C
    };
    // The live passes are the first Count. RenderPassArray::Add (0xBA9EE0) reuses the slots past
    // it on the next rebuild, so up to Size they hold an earlier build's passes: same mesh,
    // lights that may be gone.
    struct RenderPassArrayData {             // BSShaderProperty::RenderPassArray
        void*               _vtbl;
        RenderPassData**    Base;            // 04
        UInt16              MaxSize;
        UInt16              Size;            // 0A slots allocated
        UInt16              ESize;
        UInt16              GrowBy;
        UInt32              Count;           // 10 live passes
    };
    static UInt32 LivePasses(const RenderPassArrayData* Passes) {
        return Passes->Count < Passes->Size ? Passes->Count : Passes->Size;
    }

    static const UInt32 MaxLights = 16;      // MergedLights.hlsl
    static const UInt32 MaxBaseLights = 4;   // the light-only pass's own: the sun and up to two lamps

    struct Merged {
        NiGeometry*         Geometry;
        UInt32              Count;
        ShadowSceneLight*   Lights[MaxLights];
        UInt32              BaseCount;
        ShadowSceneLight*   BaseLights[MaxBaseLights];
        bool                Specular;        // the highlight passes went in too
    };
    // This frame's, by mesh. A mesh's light-only pass can run more than once a frame (one draw per
    // skin partition, reflections); the last one stands.
    static std::unordered_map<NiGeometry*, Merged> sMerged;
    static bool  sPassMuted = false;
    static DWORD sSavedWriteMask = 0xF;
    // Set while a draw has lamps uploaded. The count goes back to 0 after that draw: shaders
    // these hooks do not cover (tree branches) use the same light-only shaders and must never
    // read another mesh's lamps.
    static bool  sLightsUploaded = false;
    static bool  sViewOverridden = false;   // first person: the viewmodel camera's inverse matrices are in vertex c240-c247

    // BSSM_DIFFUSEPT2/3 and their FaceGen, parallax, skinned and projected-shadow forms. Not the
    // tree branch ones (Sb).
    static bool IsMergeableLampPass(UInt16 PassEnum) {
        return (PassEnum >= 0x132 && PassEnum <= 0x13C && PassEnum != 0x137)    // DIFFUSEPT2 .. _SFgShp, not _Sb
            || (PassEnum >= 0x13E && PassEnum <= 0x148 && PassEnum != 0x143);   // DIFFUSEPT3 .. _SFgShp, not _Sb
    }

    // The 2x highlight passes, BSSM_SPECULARDIR (0x154) to BSSM_2x_SPECULARPT3_Sb (0x177).
    static bool IsSpecularPass(UInt16 PassEnum) {
        return PassEnum >= 0x154 && PassEnum <= 0x177;
    }

    // The ones the light-only pass can take: plain, parallax, skinned and projected-shadow. Not
    // hair (_H), which has its own highlight model, the tree branch ones (_Sb), nor the 1x ones.
    static bool IsMergeableSpecularPass(UInt16 PassEnum) {
        switch (PassEnum) {
            case 0x15A: case 0x15C: case 0x15D: case 0x160: case 0x162: case 0x163:   // 2x_SPECULARDIR, _Px, _S, _Shp, _PxShp, _SShp
            case 0x166: case 0x168: case 0x169:                                       // 2x_SPECULARPT, _Px, _S
            case 0x16C: case 0x16E: case 0x16F:                                       // 2x_SPECULARPT2, _Px, _S
            case 0x172: case 0x174: case 0x175:                                       // 2x_SPECULARPT3, _Px, _S
                return true;
        }
        return false;
    }

    static bool Contains(ShadowSceneLight* const* Lights, UInt32 Count, const ShadowSceneLight* Light) {
        for (UInt32 k = 0; k < Count; k++) if (Lights[k] == Light) return true;
        return false;
    }

    // "SLS2037.pso" -> 2037 for the given prefix, else -1.
    static int ShaderNumber(const NiD3DPixelShaderEx* PixelShader, const char* Prefix) {
        if (!PixelShader || !PixelShader->Name) return -1;
        const size_t n = strlen(Prefix);
        if (strncmp(PixelShader->Name, Prefix, n) != 0) return -1;
        return atoi(PixelShader->Name + n);
    }

    bool Active(bool Skin) {
        PBRShaders* PBR = TheShaderManager->Shaders.PBR;
        if (!TheSettingManager->SettingsMain.Main.RenderEffects || !PBR || !PBR->Enabled) return false;
        if (!PBR->MaterialSettings.LinearLighting || !PBR->MaterialSettings.MergeLightPasses) return false;
        return !Skin || (TheShaderManager->Shaders.Skin && TheShaderManager->Shaders.Skin->Enabled);
    }

    void EndDraw();

    void BeginFrame() {
        EndDraw();   // nothing a draw set may carry into a new frame (see OnDraw)
        sMerged.clear();
    }

    // First-person meshes are drawn with the viewmodel camera (its own FOV and near plane), but
    // the shaders rebuild world positions and directions through TESR_InvProjectionTransform and
    // TESR_InvViewTransform (vertex c240-c247, Includes/Shadow.hlsl), which are the world camera's: the
    // world frame and position the merged lamps need come out skewed, and so do the forward sun
    // shadow lookup and the ambient normal (the weapon read the shadow maps at the wrong place, in
    // patches per triangle, since the lookup's normal comes from the position's derivatives). For
    // every first-person draw (OnDraw) the inverses of the renderer's matrices at that moment, the
    // viewmodel camera's, go there instead (NiDX9Renderer::SetCameraData writes them for every
    // camera), and EndDraw puts NVR's back.
    static void OverrideViewForFirstPerson() {
        D3DXMATRIX InvProj, InvView;
        if (!D3DXMatrixInverse(&InvProj, NULL, &TheRenderManager->projMatrix)) return;
        if (!D3DXMatrixInverse(&InvView, NULL, &TheRenderManager->viewMatrix)) return;
        TheRenderManager->device->SetVertexShaderConstantF(240, (const float*)&InvProj, 4);
        TheRenderManager->device->SetVertexShaderConstantF(244, (const float*)&InvView, 4);
        sViewOverridden = true;
    }

    // A light-only pass able to take the lamps: collect and upload them (or a count of 0).
    //   AllowSpecular: the shader can draw the highlight passes' share (objects and parallax; the
    //   skin shader draws its highlights in every pass anyway)
    void SetupBasePass(NiGeometry* Geometry, bool Skin, bool AllowSpecular) {
        if (!Geometry || !Active(Skin)) return;

        // The merged lamps light the pixel at the world position and with the world normal the vertex
        // shader rebuilds through TESR_InvProjectionTransform/TESR_InvViewTransform (vertex c240-c247,
        // Includes/Shadow.hlsl), uploaded only when the vertex shader changes (ShaderRecord::SetCT). Every
        // merged pass writes the world camera's matrices itself; first-person draws already have the
        // viewmodel camera's (OverrideViewForFirstPerson).
        if (!sViewOverridden) {
            TheRenderManager->device->SetVertexShaderConstantF(240, (const float*)&TheRenderManager->InvProjMatrix, 4);
            TheRenderManager->device->SetVertexShaderConstantF(244, (const float*)&TheRenderManager->InvViewMatrix, 4);
        }
        float Data[4 * (1 + 2 * MaxLights)] = {};
        Merged M = {};
        M.Geometry = Geometry;
        BSShaderPPLightingProperty* Property = Geometry ? static_cast<BSShaderPPLightingProperty*>(Geometry->GetProperty(NiProperty::kType_Shade)) : nullptr;
        const RenderPassArrayData* Passes = Property ? (const RenderPassArrayData*)Property->pRenderPassArray : nullptr;
        const RenderPassData* Current = *(RenderPassData**)0x011F91E0;              // BSShaderManager::pCurrentRenderPass
        bool ok = Passes && Passes->Base && Current && Current->Geometry == Geometry;

        // The light-only pass's own lights (the sun first), which its highlight passes also cover.
        if (ok && Current->SceneLights)
            for (UInt8 j = 0; j < Current->NumLights && M.BaseCount < MaxBaseLights; j++)
                if (Current->SceneLights[j]) M.BaseLights[M.BaseCount++] = Current->SceneLights[j];

        // The lamps of the additive passes.
        const UInt32 Live = ok ? LivePasses(Passes) : 0;
        for (UInt32 i = 0; ok && i < Live; i++) {
            const RenderPassData* Pass = Passes->Base[i];
            if (!Pass || Pass->Geometry != Geometry || !IsMergeableLampPass(Pass->PassEnum) || !Pass->SceneLights) continue;
            for (UInt8 j = 0; j < Pass->NumLights; j++) {
                ShadowSceneLight* Light = Pass->SceneLights[j];
                if (!Light) continue;
                if (!Light->bPointLight || !Light->sourceLight) { ok = false; break; }
                if (Contains(M.Lights, M.Count, Light)) continue;
                if (M.Count == MaxLights) { ok = false; break; }
                M.Lights[M.Count++] = Light;
            }
        }

        // The highlight passes: all of them mergeable and lit by lights the pass now has.
        bool AnySpecular = false;
        M.Specular = ok && AllowSpecular;
        for (UInt32 i = 0; M.Specular && i < Live; i++) {
            const RenderPassData* Pass = Passes->Base[i];
            if (!Pass || Pass->Geometry != Geometry || !IsSpecularPass(Pass->PassEnum)) continue;
            AnySpecular = true;
            if (!IsMergeableSpecularPass(Pass->PassEnum) || !Pass->SceneLights) { M.Specular = false; break; }
            for (UInt8 j = 0; j < Pass->NumLights; j++) {
                const ShadowSceneLight* Light = Pass->SceneLights[j];
                if (Light && !Contains(M.BaseLights, M.BaseCount, Light) && !Contains(M.Lights, M.Count, Light)) { M.Specular = false; break; }
            }
        }
        M.Specular = M.Specular && AnySpecular;

        if (ok && (M.Count || M.Specular)) {
            const NiPoint3& PosAdjust = *(NiPoint3*)0x011F474C;                  // NiRenderer::kPosAdjust
            void* SceneNode = *(void**)0x011F91C8;                               // BSShaderManager::pShadowSceneNode[0]
            const NiPoint3 Offset = SceneNode ? *(NiPoint3*)((UInt8*)SceneNode + 0x1E4) : NiPoint3(0.0f, 0.0f, 0.0f);   // kLightingOffset
            const bool HDR = *(bool*)0x011F941E;                                 // BSShaderManager::bHDR
            const float ForcedDarkness = Property->fUnk06C;                      // BSShaderLightingProperty::fForcedDarkness

            Data[0] = (float)M.Count;
            Data[1] = M.Specular ? 1.0f : 0.0f;
            float Anchors[4 * MaxLights] = {};
            for (UInt32 k = 0; k < M.Count; k++) {
                const ShadowSceneLight* Light = M.Lights[k];
                const NiPointLight* Source = Light->sourceLight;
                const NiPoint3& Position = Source->m_worldTransform.pos;
                float* P = &Data[4 * (1 + k)];
                P[0] = Position.x + Offset.x - PosAdjust.x;
                P[1] = Position.y + Offset.y - PosAdjust.y;
                P[2] = Position.z + Offset.z - PosAdjust.z;
                P[3] = Source->Spec.r;

                float Dimmer = Source->Dimmer;
                if (!HDR && Dimmer > 1.0f) Dimmer = 1.0f;
                const float Scale = ForcedDarkness < 1.0f ? 0.0f : Dimmer * ForcedDarkness * Light->fLODDimmer;
                float* C = &Data[4 * (1 + MaxLights + k)];
                C[0] = Source->Diff.r * Scale;
                C[1] = Source->Diff.g * Scale;
                C[2] = Source->Diff.b * Scale;
                C[3] = ForwardPointShadows::LampInfo(Light, &Anchors[4 * k]);   // its shadow (Includes/PointShadow.hlsl)
            }
            sMerged[Geometry] = M;
            TheRenderManager->device->SetPixelShaderConstantF(154, Data, 1 + 2 * MaxLights);
            if (ForwardPointShadows::Active() && M.Count)
                TheRenderManager->device->SetPixelShaderConstantF(ForwardPointShadows::MergedRegister, Anchors, M.Count);
            sLightsUploaded = true;
        }
        // Otherwise nothing to write: the count is 0 outside a merged draw (EndDraw).
    }

    // An additive lamp pass or a highlight pass: muted when its mesh's first pass took all its
    // lights.
    bool MuteAddPass(NiGeometry* Geometry) {
        const RenderPassData* Pass = *(RenderPassData**)0x011F91E0;            // BSShaderManager::pCurrentRenderPass
        if (!Pass || Pass->Geometry != Geometry || !Pass->SceneLights) return false;
        const bool Lamp = IsMergeableLampPass(Pass->PassEnum);
        if (!Lamp && !IsMergeableSpecularPass(Pass->PassEnum)) return false;
        const auto Found = sMerged.find(Geometry);
        if (Found == sMerged.end()) return false;                             // first pass not merged this frame
        const Merged* M = &Found->second;
        if (!Lamp && !M->Specular) return false;
        for (UInt8 j = 0; j < Pass->NumLights; j++) {
            const ShadowSceneLight* Light = Pass->SceneLights[j];
            if (!Light) continue;
            if (Contains(M->Lights, M->Count, Light)) continue;
            if (!Lamp && Contains(M->BaseLights, M->BaseCount, Light)) continue;
            return false;                                                     // a light missing from the first pass
        }
        if (!sPassMuted) {
            TheRenderManager->device->GetRenderState(D3DRS_COLORWRITEENABLE, &sSavedWriteMask);
            TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            sPassMuted = true;
        }
        return true;
    }

    void EndDraw() {
        if (sLightsUploaded) {
            const float Zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            TheRenderManager->device->SetPixelShaderConstantF(154, Zero, 1);
            sLightsUploaded = false;
        }
        if (sViewOverridden) {
            TheRenderManager->device->SetVertexShaderConstantF(240, (const float*)&TheRenderManager->InvProjMatrix, 4);
            TheRenderManager->device->SetVertexShaderConstantF(244, (const float*)&TheRenderManager->InvViewMatrix, 4);
            sViewOverridden = false;
        }
        if (sPassMuted) {
            TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, sSavedWriteMask);
            sPassMuted = false;
        }
    }

    // Per draw, for the object (and hair), parallax and skin shaders.
    //   objects:  light-only SLS2037-SLS2044, additive SLS2045/SLS2046, highlights SLS2047-SLS2056
    //   parallax: light-only PAR2013-PAR2020, additive PAR2021/PAR2022, highlights PAR2024-PAR2028
    //   skin:     light-only SKIN2004-SKIN2007, additive SKIN2008/SKIN2009
    // Returns true when the draw is muted.
    bool OnDraw(NiGeometry* Geometry, const NiD3DPixelShaderEx* PixelShader) {
        // A previous draw's state still set means its PostGeometry never ran: a muted pass would blank this
        // draw, and uploaded lamps would light this mesh with another's. Clear it before anything else.
        // The game calls PrepareGeometryForRendering more than once for some meshes (seen on first-person
        // weapons) before PostGeometry runs, so a first-person draw's viewmodel camera matrices could stay in
        // c240-c247 into the following draws and the next frame: world meshes then placed their merged lamps
        // through the wrong camera, and lamps switched off depending on the view.
        if (sPassMuted || sLightsUploaded || sViewOverridden) EndDraw();
        if (!PixelShader || PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup) return false;   // vanilla shader
        BSShaderPPLightingProperty* Property = Geometry ? static_cast<BSShaderPPLightingProperty*>(Geometry->GetProperty(NiProperty::kType_Shade)) : nullptr;
        if (Property && (Property->ulFlags[1] & 0x40)) OverrideViewForFirstPerson();   // BSS2_1st_person: every pass, merged or not
        ForwardPointShadows::sFirstPersonDraw = Property && (Property->ulFlags[1] & 0x40);   // the merged lamps' anchors (LampInfo)
        int n = ShaderNumber(PixelShader, "SLS");
        if (n >= 2037 && n <= 2044) { SetupBasePass(Geometry, false, true); return false; }
        if (n >= 2045 && n <= 2056) return MuteAddPass(Geometry);
        n = ShaderNumber(PixelShader, "PAR");
        if (n >= 2013 && n <= 2020) { SetupBasePass(Geometry, false, true); return false; }
        if ((n >= 2021 && n <= 2022) || (n >= 2024 && n <= 2028)) return MuteAddPass(Geometry);
        n = ShaderNumber(PixelShader, "SKIN");
        if (n >= 2004 && n <= 2007) { SetupBasePass(Geometry, true, false); return false; }
        if (n == 2008 || n == 2009) return MuteAddPass(Geometry);
        return false;
    }
}

namespace ForwardPointShadows {

    void OnDraw(NiGeometry* Geometry, const NiD3DPixelShaderEx* PixelShader) {
        if (sUploaded || sFillUploaded) EndDraw();
        if (!Geometry || !PixelShader || PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup) return;   // vanilla shader
        const MergedLights::RenderPassData* Current = *(MergedLights::RenderPassData**)0x011F91E0;   // BSShaderManager::pCurrentRenderPass
        if (!Current || Current->Geometry != Geometry || !Current->SceneLights) return;
        const UInt32 FillFirst = (Current->PassEnum == 0xCA || Current->PassEnum == 0xD1) ? 1 : 0;   // as First below
        // Inverse square lighting: the pass's fill lights keep vanilla's falloff, and the skin
        // shaders cannot tell them by radius (Includes/InverseSquare.hlsl).
        const InverseSquareLightingShaders* InverseSquare = TheShaderManager->Shaders.InverseSquareLighting;
        if (InverseSquare && InverseSquare->Enabled && InverseSquare->Settings.FillLightRadius > 0.0f) {
            float Fill[8] = {};
            bool AnyFill = false;
            for (UInt32 k = 0; k < 8 && FillFirst + k < Current->NumLights; k++) {
                const ShadowSceneLight* Light = Current->SceneLights[FillFirst + k];
                if (Light && Light->sourceLight && Light->sourceLight->Spec.r > InverseSquare->Settings.FillLightRadius) {
                    Fill[k] = 1.0f;
                    AnyFill = true;
                }
            }
            if (AnyFill) {
                TheRenderManager->device->SetPixelShaderConstantF(FillRegister, Fill, 2);
                sFillUploaded = true;
            }
        }
        if (!Active()) return;
        const BSShaderPPLightingProperty* Property = static_cast<BSShaderPPLightingProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));
        sFirstPersonDraw = Property && (Property->ulFlags[1] & 0x40);   // BSS2_1st_person (LampInfo)
        // The pixel shader's light k is the pass's light k, but in BSSM_ADT4_Opt (0xCA) and
        // BSSM_ADTS10_Opt (0xD1): their setups (ShadowLightShader::SetupGeometryOpt_Lights /
        // _LightsSpecular) write pass light k to kShaderLightConstants[k], and "LightColors"
        // (PSLightColor, c3) starts at kShaderLightConstants[1] -- so PSLightColor[k] is light k + 1.
        // (The other passes' SetupGeometryConstants_Lights put light k at kShaderLightConstants[k + 1].)
        const UInt32 First = (Current->PassEnum == 0xCA || Current->PassEnum == 0xD1) ? 1 : 0;
        float Anchors[4 * BaseLights] = {};
        float Info[8] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        bool Any = false;
        for (UInt32 k = 0; k < BaseLights && First + k < Current->NumLights; k++) {
            Info[k] = LampInfo(Current->SceneLights[First + k], &Anchors[4 * k]);
            Any |= Info[k] >= 0.0f;
        }
        if (!Any) return;
        TheRenderManager->device->SetPixelShaderConstantF(BaseRegister, Anchors, BaseLights);
        TheRenderManager->device->SetPixelShaderConstantF(BaseInfoRegister, Info, 2);
        sUploaded = true;
    }

    void EndDraw() {
        const float None[8] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        if (sUploaded) {
            TheRenderManager->device->SetPixelShaderConstantF(BaseInfoRegister, None, 2);
            sUploaded = false;
        }
        if (sFillUploaded) {
            const float NoFill[8] = {};
            TheRenderManager->device->SetPixelShaderConstantF(FillRegister, NoFill, 2);
            sFillUploaded = false;
        }
    }
}

// --- Decals keep the game's shaders ------------------------------------------------------------
// Decals (blood, bullet holes, impact marks and NIF decal meshes) lie on the surface they mark and
// stay in front of it only through the game's depth bias. Drawn with NVR's object shaders, which
// compute positions and lighting differently from the wall underneath, they flickered in interiors.
// Every decal draw therefore runs on the game's own vertex and pixel shader pair, and skips NVR's
// per-draw extras (merged lamps, material maps), so its additive light passes stay as the game
// built them.
//
// The batch hook (SetShadersHook) binds shaders once per batch, and a batch mixes decals with
// other meshes, so the swap is per draw: the game's pair goes on the device for the decal, and
// PostGeometry puts back the pair the render state has cached, which the next draw expects.
namespace VanillaDecals {

    static bool sSwapped = false;

    bool IsDecalProperty(const BSShaderProperty* Property) {
        return Property && (Property->GetFlag(BSSP_DECAL) || Property->GetFlag(BSSP_DYNAMIC_DECAL) || Property->GetFlag(BSSP_ALPHA_DECAL));
    }

    bool IsDecal(NiGeometry* Geometry) {
        return Geometry && IsDecalProperty(static_cast<BSShaderProperty*>(Geometry->GetProperty(NiProperty::kType_Shade)));
    }

    void EndDraw();

    // Before a draw: true when the geometry is a decal (it is then drawn with the game's shaders).
    bool OnDraw(NiGeometry* Geometry, NiD3DPass* Pass) {
        if (sSwapped) EndDraw();   // a previous draw's PostGeometry did not run (see MergedLights::OnDraw)
        if (!IsDecal(Geometry)) return false;
        NiD3DVertexShaderEx* VertexShader = Pass ? (NiD3DVertexShaderEx*)Pass->VertexShader : nullptr;
        NiD3DPixelShaderEx* PixelShader = Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr;
        if (!VertexShader || !PixelShader || !VertexShader->ShaderHandleBackup || !PixelShader->ShaderHandleBackup) return true;
        const bool NVRVertex = VertexShader->ShaderHandle != VertexShader->ShaderHandleBackup;
        const bool NVRPixel = PixelShader->ShaderHandle != PixelShader->ShaderHandleBackup;
        if (!NVRVertex && !NVRPixel) return true;   // already the game's pair

        // Both sides together: the game's shaders are a matched pair, and a 3.0 shader cannot pair with a 2.x one.
        IDirect3DDevice9* Device = TheRenderManager->device;
        Device->SetVertexShader((IDirect3DVertexShader9*)VertexShader->ShaderHandleBackup);
        Device->SetPixelShader((IDirect3DPixelShader9*)PixelShader->ShaderHandleBackup);
        sSwapped = true;
        return true;
    }

    // After the draw: the shaders the render state believes are bound go back on the device.
    void EndDraw() {
        if (!sSwapped) return;
        IDirect3DDevice9* Device = TheRenderManager->device;
        Device->SetVertexShader(TheRenderManager->renderState->GetVertexShader());
        Device->SetPixelShader(TheRenderManager->renderState->GetPixelShader());
        sSwapped = false;
    }
}

// ShadowLightShader's per-geometry virtuals (vtable 0x10AF2F8): the same two functions as
// SkinShader's slots 27 and 35, hooked in this shader's own vtable, for the merged light passes.
VirtFuncDetour kLightPrepareGeometryDetour;
VirtFuncDetour kLightPostGeometryDetour;

void* __fastcall ShadowLightShader__PrepareGeometryForRendering(void* apThis, void*, void* apGeometry, void* apPartition, void* apRendererData, void* apState) {
    void* Result = (void*)ThisCall(kLightPrepareGeometryDetour.GetOverwrittenAddr(), apThis, apGeometry, apPartition, apRendererData, apState);
    NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;   // NiD3DShader::m_pCurrentPass
    if (VanillaDecals::OnDraw((NiGeometry*)apGeometry, Pass)) return Result;
    WriteObjectMaterial((NiGeometry*)apGeometry);
    NiD3DPixelShaderEx* PixelShader = Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr;
    MaterialMaps::OnDraw((NiGeometry*)apGeometry, PixelShader);
    MergedLights::OnDraw((NiGeometry*)apGeometry, PixelShader);
    ForwardPointShadows::OnDraw((NiGeometry*)apGeometry, PixelShader);
    DebugViewPasses::OnDraw(PixelShader);
    return Result;
}

void __fastcall ShadowLightShader__PostGeometry(void* apThis, void*, void* apProperties) {
    ThisCall(kLightPostGeometryDetour.GetOverwrittenAddr(), apThis, apProperties);
    DebugViewPasses::EndDraw();   // restored first: it muted after MergedLights and MaterialMaps
    ForwardPointShadows::EndDraw();
    MergedLights::EndDraw();
    MaterialMaps::EndDraw();
    VanillaDecals::EndDraw();
}

// ParallaxShader's (vtable 0x10BB7A8): the same two functions again, in its own vtable.
VirtFuncDetour kParallaxPrepareGeometryDetour;
VirtFuncDetour kParallaxPostGeometryDetour;

void* __fastcall ParallaxShader__PrepareGeometryForRendering(void* apThis, void*, void* apGeometry, void* apPartition, void* apRendererData, void* apState) {
    void* Result = (void*)ThisCall(kParallaxPrepareGeometryDetour.GetOverwrittenAddr(), apThis, apGeometry, apPartition, apRendererData, apState);
    NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;   // NiD3DShader::m_pCurrentPass
    if (VanillaDecals::OnDraw((NiGeometry*)apGeometry, Pass)) return Result;
    WriteObjectMaterial((NiGeometry*)apGeometry);
    NiD3DPixelShaderEx* PixelShader = Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr;
    MaterialMaps::OnDraw((NiGeometry*)apGeometry, PixelShader);
    MergedLights::OnDraw((NiGeometry*)apGeometry, PixelShader);
    ForwardPointShadows::OnDraw((NiGeometry*)apGeometry, PixelShader);
    DebugViewPasses::OnDraw(PixelShader);
    return Result;
}

void __fastcall ParallaxShader__PostGeometry(void* apThis, void*, void* apProperties) {
    ThisCall(kParallaxPostGeometryDetour.GetOverwrittenAddr(), apThis, apProperties);
    DebugViewPasses::EndDraw();   // restored first: it muted after MergedLights and MaterialMaps
    ForwardPointShadows::EndDraw();
    MergedLights::EndDraw();
    MaterialMaps::EndDraw();
    VanillaDecals::EndDraw();
}

// HairShader's (vtable 0x10BBB50): ShadowLightShader's passes and per-geometry functions, in its
// own vtable.
VirtFuncDetour kHairPrepareGeometryDetour;
VirtFuncDetour kHairPostGeometryDetour;

void* __fastcall HairShader__PrepareGeometryForRendering(void* apThis, void*, void* apGeometry, void* apPartition, void* apRendererData, void* apState) {
    void* Result = (void*)ThisCall(kHairPrepareGeometryDetour.GetOverwrittenAddr(), apThis, apGeometry, apPartition, apRendererData, apState);
    WriteObjectMaterial((NiGeometry*)apGeometry);
    NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;   // NiD3DShader::m_pCurrentPass
    MergedLights::OnDraw((NiGeometry*)apGeometry, Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr);
    ForwardPointShadows::OnDraw((NiGeometry*)apGeometry, Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr);
    return Result;
}

void __fastcall HairShader__PostGeometry(void* apThis, void*, void* apProperties) {
    ThisCall(kHairPostGeometryDetour.GetOverwrittenAddr(), apThis, apProperties);
    ForwardPointShadows::EndDraw();
    MergedLights::EndDraw();
}

// Lighting30Shader's (vtable 0x10BA0F8): the 3X lighting passes, hair among them (SM3002/SM3003),
// which take the lamps' shadows too. Slot 27 is NiD3DShader::PrepareGeometryForRendering as in the
// others; slot 35 is its own post-geometry function (0xBBEEF0, stencil and z-write reset).
VirtFuncDetour kLighting30PrepareGeometryDetour;
VirtFuncDetour kLighting30PostGeometryDetour;

void* __fastcall Lighting30Shader__PrepareGeometryForRendering(void* apThis, void*, void* apGeometry, void* apPartition, void* apRendererData, void* apState) {
    void* Result = (void*)ThisCall(kLighting30PrepareGeometryDetour.GetOverwrittenAddr(), apThis, apGeometry, apPartition, apRendererData, apState);
    NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;   // NiD3DShader::m_pCurrentPass
    ForwardPointShadows::OnDraw((NiGeometry*)apGeometry, Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr);
    return Result;
}

void __fastcall Lighting30Shader__PostGeometry(void* apThis, void*, void* apProperties) {
    ThisCall(kLighting30PostGeometryDetour.GetOverwrittenAddr(), apThis, apProperties);
    ForwardPointShadows::EndDraw();
}

// SkinShader's per-geometry virtuals (vtable 0x10BB980), bracketing every skin draw on both of
// the batch renderer's paths (BSBatchRenderer::RenderPassImmediately_Standard and _Skinned):
// slot 27 PrepareGeometryForRendering (NiD3DShader, 0xE812F0) runs just before the draw, slot 35
// PostGeometry (ShadowLightShader, 0xB7C320) just after. The skin scattering effect binds its
// second render target between the two and nowhere else (src/effects/SkinScattering.h). The
// batch-level SetupPassShaders hook (SetShadersHook) runs once per batch, not per draw.
VirtFuncDetour kSkinPrepareGeometryDetour;
VirtFuncDetour kSkinPostGeometryDetour;

// The engine also draws its own highlight passes on skin geometry (traced on FaceGenFace:
// BSSM_2x_SPECULARDIR_S / _SPECULARPT_S with the object shaders), BSSM_SPECULARDIR (0x154) to
// BSSM_2x_SPECULARPT3_Sb (0x177). NVR's skin shader draws physically based highlights itself, so
// while it is on these are muted (colour writes off for the draw): with them, faces had two sets
// of highlights, and the screen-space scattering blurred the second as if it were diffuse light.
static bool  sSkinHighlightMuted = false;
static DWORD sSkinSavedWriteMask = 0xF;

// FaceGen faces in the light-only passes (AD2/AD3: SKIN2004-SKIN2007). The engine multiplies the
// frame by the face's colour afterwards in BSSM_TEXTURE_SFg (SLS1005: the base texture with both
// FaceGen maps, 2((2(fg0 - 0.5) + base) 2 fg1), the same blend as the full pass), but binds only
// the base texture to these passes (SkinShader::SetupGeometryTextures, 0xBD02E0), so the skin
// shader divided its highlights, and wrote the scattering effect's albedo and brightness, with
// the bare base texture: off by the FaceGen tint, which showed as tinted lips, brows and eyelids
// and threw the scattering's skin test. The FaceGen maps go to stages 2 and 3, as the full pass
// has them (texture 1 of the diffuse and normal sets); stage 3 is the glow map here, which the
// skin shader does not read and the engine sets again for every draw. Stage 2 is otherwise empty
// in these passes and is emptied again after the draw (ApplyTextureStages binds only stages with
// a texture), so nothing later binds a stale texture through it.
static void* sFaceGenStage2 = nullptr;   // the stage whose texture PostGeometry clears

static bool BindFaceGenMaps(void* apThis, NiGeometry* Geometry, NiD3DPass* Pass) {
    if (!Geometry || !Pass || Pass->StageCount < 4) return false;
    BSShaderPPLightingProperty* Property = static_cast<BSShaderPPLightingProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));
    if (!Property || !Property->GetFlag(BSSP_FACEGEN)) return false;
    if (!Property->ppTextures[0] || !Property->ppTextures[0][1]) return false;

    void** Stages = *(void***)((UInt8*)Pass + 0x24);   // NiD3DPass::m_kStages.m_pBase
    if (!Stages || !Stages[2] || !Stages[3]) return false;

    ThisCall(0xB7C0A0, apThis, Property, 2, 1);   // ShadowLightShader::SetDiffuseMap: FaceGenMap0
    ThisCall(0xB7C0E0, apThis, Property, 3, 1);   // ShadowLightShader::SetNormalMap: FaceGenMap1
    ThisCall(0xBE2170, apThis);                   // BSShader::ApplyTextureStages
    sFaceGenStage2 = Stages[2];
    return true;
}

void* __fastcall SkinShader__PrepareGeometryForRendering(void* apThis, void*, void* apGeometry, void* apPartition, void* apRendererData, void* apState) {
    void* Result = (void*)ThisCall(kSkinPrepareGeometryDetour.GetOverwrittenAddr(), apThis, apGeometry, apPartition, apRendererData, apState);

    NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;   // NiD3DShader::m_pCurrentPass
    NiD3DPixelShaderEx* PixelShader = Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr;
    const UInt16 PassType = *(UInt16*)0x011F91E4;  // BSShaderManager::eCurrentPass

    const bool NVRSkin = TheShaderManager->Shaders.Skin->Enabled && TheSettingManager->SettingsMain.Main.RenderEffects;
    const bool SkinPixelShader = PixelShader && PixelShader->Name && !memcmp(PixelShader->Name, "SKIN", 4);
    // The pass type alone is not trusted to be current: a skin pixel shader is never muted.
    if (NVRSkin && !SkinPixelShader && PassType >= 0x154 && PassType <= 0x177 && !sSkinHighlightMuted) {
        TheRenderManager->device->GetRenderState(D3DRS_COLORWRITEENABLE, &sSkinSavedWriteMask);
        TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
        sSkinHighlightMuted = true;
    }

    // The scattering targets only for NVR's own skin pixel shaders, not the highlight or fog
    // passes the engine also draws with this shader.
    // Merged light passes: the lamps of the additive passes go into the light-only pass, and
    // those passes are muted (MergedLights above). A muted pass writes nothing, the scattering
    // target included.
    const bool MergedMuted = SkinPixelShader && MergedLights::OnDraw((NiGeometry*)apGeometry, PixelShader);
    // The pass's lamps' shadows (Includes/PointShadow.hlsl, ForwardPointShadows above).
    if (SkinPixelShader && !MergedMuted) ForwardPointShadows::OnDraw((NiGeometry*)apGeometry, PixelShader);

    SkinScatteringEffect* Scattering = TheShaderManager->Effects.SkinScattering;
    if (Scattering && SkinPixelShader && !MergedMuted)
        Scattering->BindForSkinDraw(PixelShader);

    // The light-only passes: SKIN2004-SKIN2007.
    bool FaceGenBound = false;
    if (NVRSkin && SkinPixelShader && !memcmp(PixelShader->Name, "SKIN200", 7) && PixelShader->Name[7] >= '4' && PixelShader->Name[7] <= '7')
        FaceGenBound = BindFaceGenMaps(apThis, (NiGeometry*)apGeometry, Pass);

    // Develop.DebugMode + the TraceShaders key: what this hook did for every skin draw that frame.
    if (TheSettingManager->SettingsMain.Develop.DebugMode && Global->OnKeyDown(TheSettingManager->SettingsMain.Develop.TraceShaders)) {
        Logger::Log("SkinHook: pass %s (0x%X), pixel shader %s: highlights muted %d, scattering bound %d",
            Pointers::Functions::GetPassDescription(PassType), (UInt32)PassType,
            (PixelShader && PixelShader->Name) ? PixelShader->Name : "(none)",
            sSkinHighlightMuted ? 1 : 0, (Scattering && Scattering->Bound) ? 1 : 0);
    }

    // SkinScreenSpaceScatter (Includes/SkinLighting.hlsl), every skin draw: x 1 while the target is
    // bound, so the shader leaves diffusion to the screen-space blur; y 1 while the FaceGen maps
    // are bound to a light-only pass.
    const float ScreenSpace[4] = { (Scattering && Scattering->Bound) ? 1.0f : 0.0f, FaceGenBound ? 1.0f : 0.0f, 0.0f, 0.0f };
    TheRenderManager->device->SetPixelShaderConstantF(147, ScreenSpace, 1);
    return Result;
}

void __fastcall SkinShader__PostGeometry(void* apThis, void*, void* apProperties) {
    ThisCall(kSkinPostGeometryDetour.GetOverwrittenAddr(), apThis, apProperties);
    if (TheShaderManager->Effects.SkinScattering) TheShaderManager->Effects.SkinScattering->Unbind();
    ForwardPointShadows::EndDraw();
    MergedLights::EndDraw();
    if (sFaceGenStage2) {
        *(void**)((UInt8*)sFaceGenStage2 + 0x08) = nullptr;   // NiD3DTextureStage::m_pkTexture
        sFaceGenStage2 = nullptr;
    }
    if (sSkinHighlightMuted) {
        TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, sSkinSavedWriteMask);
        sSkinHighlightMuted = false;
    }
}

// Interior faces. BSShaderPPLightingProperty::GetRenderPasses_2x (0xBDF790) builds every lit
// mesh's pass list, and for FaceGen geometry in interiors it skips the passes SkinShader draws
// itself (ADT/ADT2 and AD2/AD3: the two `!bIsFaceGen || !BSShaderManager::bInterior` tests). An
// interior face got the generic ones instead, traced in game:
//   BSSM_AMBIENT_S (SLS1000) + BSSM_DIFFUSEDIR_S (SLS1002): ambient and the main light, vanilla
//   BSSM_DIFFUSEPT2/3_SFg (SKIN2008/9): additive point-light passes, the only NVR skin
//   BSSM_TEXTURE_SFg (SLS1005): the FaceGen texture multiplied in
// So indoors the skin shader lit only the lamps, the highlights of the main light were gone (its
// specular pass is muted while NVR skin is on) and the screen-space scattering never ran: only
// an opaque skin pass writes the depth and albedo it needs to find skin.
//
// Both tests read bInterior from one load, `mov al, [BSShaderManager::bInterior]` at 0xBE018B
// (A0 27 94 1F 01; the third way into the second test, from 0xBE01B4, reuses al too). The patch
// points that load at FaceGenInteriorFlag: 0 while NVR's skin shader is on, so interior faces
// take the exterior route (one ADT pass, or AD + point-light + texture passes, all SkinShader's
// own); the real bInterior when it is off, which keeps vanilla's behaviour. Nothing else reads
// it: bInterior's other test in that function (no projected actor shadows indoors) is a separate
// load and untouched.
static UInt8 FaceGenInteriorFlag = 0;

void InstallFaceGenInteriorPatch() {
    static const UInt8 Expected[5] = { 0xA0, 0x27, 0x94, 0x1F, 0x01 };
    if (memcmp((const void*)0xBE018B, Expected, sizeof(Expected)) != 0) {
        Logger::Log("[WARNING] Skin: unexpected code at 0xBE018B; interior faces keep vanilla's passes");
        return;
    }
    SafeWrite32(0xBE018C, (UInt32)&FaceGenInteriorFlag);
}

// --- Lighting30 meshes onto NVR's shaders -------------------------------------------------------
// The engine draws some lit meshes with Lighting30Shader, a single-pass SM3 lighting shader NVR does
// not replace, so they keep vanilla shading (no PBR, _rmaos maps, merged lights or NVR lighting):
//   1 parallax occlusion meshes (BSSP_ParallaxOcclusion), whenever bUse30Lighting is on (any GPU
//     with more than 255 instruction slots), instead of ParallaxShader
//   2 dynamic decals with a normal map (BSTempEffectSimpleDecal::FinalizeGeometry, 0x68C0A7: blood,
//     bullet holes), always
//   3 NIFs whose shader block is Lighting30ShaderProperty or NoFaderShaderProperty (both streamed by
//     Lighting30ShaderProperty::CreateObject, 0xBB45D0)
// All three pass through BSShaderPPLightingProperty::ClarifyShader (0xB68880, vtable slot 44 of both
// property classes) from BSShaderManager::PrepareGeometry, which attaches whatever property it
// returns in place of the old one. That is how the engine itself turns a mesh into a Lighting30 one
// (case 1, and alpha/decal meshes in bloom mode: MAKE_LIGHTING30). The hook does the reverse:
//   - a Lighting30ShaderProperty (exact class; Lighting30 adds only its pass building, the class is
//     otherwise BSShaderPPLightingProperty's) is copied into a new BSShaderPPLightingProperty
//     (CreateObject 0xB68D50, then CopyTo3, vtable slot 52, the copy its constructor uses), set to
//     ShadowLightShader and run through ClarifyShader, which sends parallax meshes to ParallaxShader
//   - the parallax occlusion flag is hidden from ClarifyShader for the call, so it takes the
//     parallax branch it takes when bUse30Lighting is off, and put back after
// Meshes keep their flags, textures and alpha; only the shader and its pass lists change. Case 4
// (alpha and decal meshes with HDR off and bloom on) is left alone. [Shaders.PBR.Main]
// RouteLighting30; read once, applies to meshes as they load.
namespace Lighting30Route {

    typedef void* (__thiscall* ClarifyShaderFn)(void*, NiGeometry*, int, int);
    static const ClarifyShaderFn ClarifyShader = (ClarifyShaderFn)0xB68880;
    static const UInt32 Lighting30VTable = 0x10B9910;
    static const UInt32 ParallaxOcclusionBit = 1u << 28;   // BSSP_ParallaxOcclusion (0x1C), ulFlags[0]
    static const UInt32 HairBit = 1u << BSSP_HAIR;          // ulFlags[0]

    // BSShaderProperty members by offset (NVR's headers stop at NiShadeProperty).
    static UInt32& ShaderPropertyType(void* Property) { return *(UInt32*)((UInt8*)Property + 0x1C); }
    static UInt32& Flags0(void* Property) { return *(UInt32*)((UInt8*)Property + 0x20); }
    static UInt32& ShaderIndex(void* Property) { return *(UInt32*)((UInt8*)Property + 0x58); }

    static bool Enabled() {
        static int Setting = -1;
        if (Setting < 0) Setting = TheSettingManager->GetSettingI("Shaders.PBR.Main", "RouteLighting30") ? 1 : 0;
        return Setting != 0;
    }

    // ClarifyShader with the parallax occlusion flag hidden.
    static void* ClarifyWithoutLighting30(void* Property, NiGeometry* Geometry, int a, int b) {
        const bool Occlusion = (Flags0(Property) & ParallaxOcclusionBit) != 0;
        if (Occlusion) Flags0(Property) &= ~ParallaxOcclusionBit;
        void* Result = ClarifyShader(Property, Geometry, a, b);
        if (Occlusion) Flags0(Property) |= ParallaxOcclusionBit;
        if (Result && Occlusion) Flags0(Result) |= ParallaxOcclusionBit;
        return Result;
    }

    void* __fastcall Hook(void* Property, void*, NiGeometry* Geometry, int a, int b) {
        // Decals (case 2, and decal NIFs) stay on the game's own route and shaders: see VanillaDecals.
        // So does hair: the engine itself puts it on Lighting30 (BSSM_3XLIGHTING_H*, drawn by NVR's
        // SM3003), and the copy below lost its kHairTint, which is where the NPC's hair colour lives:
        // every head of hair came out the texture's bare grey.
        if (!Enabled() || VanillaDecals::IsDecalProperty((BSShaderProperty*)Property) || (Flags0(Property) & HairBit))
            return ClarifyShader(Property, Geometry, a, b);
        if (*(UInt32*)Property != Lighting30VTable) return ClarifyWithoutLighting30(Property, Geometry, a, b);

        void* Copy = ((void* (__cdecl*)())0xB68D50)();   // BSShaderPPLightingProperty::CreateObject
        if (!Copy) return ClarifyShader(Property, Geometry, a, b);
        UInt32* VTable = *(UInt32**)Property;
        ThisCall(VTable[52], Property, Copy);           // CopyTo3: textures, texture set, flags, material values
        // Not copied by CopyTo3; the class layouts match, Lighting30 adding only its pass building.
        static_cast<BSShaderPPLightingProperty*>(Copy)->kHairTint = static_cast<BSShaderPPLightingProperty*>(Property)->kHairTint;
        ShaderIndex(Copy) = 1;                          // BSSM_SHADER_SHADOWLIGHT
        ShaderPropertyType(Copy) = NiShadeProperty::kProp_PPLighting;

        void* Replacement = ClarifyWithoutLighting30(Copy, Geometry, a, b);
        if (Replacement) {
            // The engine made yet another property from the copy (bloom mode): use that one.
            ThisCall((*(UInt32**)Copy)[0], Copy, 1);     // scalar deleting destructor: never attached, no references
            return Replacement;
        }
        return Copy;
    }
}

void InstallLighting30Route() {
    SafeWrite32(0x10AE0D0 + 44 * 4, (UInt32)Lighting30Route::Hook);   // BSShaderPPLightingProperty
    SafeWrite32(0x10B9910 + 44 * 4, (UInt32)Lighting30Route::Hook);   // Lighting30ShaderProperty
}

// --- Lamps kept regardless of the view ---------------------------------------------------------
// ShadowSceneLight::TestFrustumCull (0xB9E970) marks a lamp whose sphere lies outside any plane of the
// camera's view usFrustumCull = 255, and BSShaderLightingProperty::GetNumberOfActiveNonShadowLights /
// GetFirstActiveNonShadowLight (0xB707D0, 0xB70600) then leave it out of every mesh it lights.
// GetRenderPasses_2x (0xBDF790) picks a mesh's passes from that count: one combined pass for a single
// lamp (ADT/ADTS), one "Opt" pass for a few (AddPass_Opt: SLS2029-SLS2036), or a light-only pass plus
// a pass per two or three lamps, a texture pass and highlight passes (AD2/AD3, DiffusePoint2/3,
// Textures, Specular) for more. Turning the camera so a lamp's sphere leaves the view therefore moves
// the meshes near it from one of these routes to another. They light identically in vanilla, but not
// NVR's PBR versions of them (linear sums, merged lamps, authored materials, debug views), so lights
// and highlights jumped as the view turned. The view test is the only one that depends on where the
// camera looks; the light LOD fade (fLightLODStartFade/End, by distance) stays, as does the test of
// shadow-casting lamps, whose shadow camera the game also uses for shadows. A lamp entirely off screen
// lights nothing visible, so keeping it costs only the passes it adds to meshes it already touches.
void (__thiscall* TestFrustumCull)(ShadowSceneLight*, NiCullingProcess*) = (void (__thiscall*)(ShadowSceneLight*, NiCullingProcess*))0xB9E970;
void __fastcall TestFrustumCullHook(ShadowSceneLight* This, void*, NiCullingProcess* Culler) {
    PBRShaders* PBR = TheShaderManager->Shaders.PBR;
    const bool Keep = Culler && This && !This->bIsShadowCasting && TheSettingManager->SettingsMain.Main.RenderEffects &&
        PBR && PBR->Enabled && PBR->MaterialSettings.KeepOffscreenLights;
    if (!Keep) {
        TestFrustumCull(This, Culler);
        return;
    }
    // NiCullingProcess::m_kPlanes (0x2C) .m_uiActivePlanes (0x60): TestFrustumCull copies the planes and
    // tests only the active ones, so with none active the view never culls the lamp.
    UInt32& ActivePlanes = *(UInt32*)((UInt8*)Culler + 0x2C + 0x60);
    const UInt32 Saved = ActivePlanes;
    ActivePlanes = 0;
    TestFrustumCull(This, Culler);
    ActivePlanes = Saved;
}

// Every frame, before the scene's pass lists are built (RenderHook).
void UpdateFaceGenInteriorFlag() {
    MergedLights::BeginFrame();

    // A loading screen behind us -- interior/exterior changed, or the camera jumped farther than any
    // frame's movement (a door, a load, fast travel) -- releases the material maps (MaterialMaps).
    static D3DXVECTOR4 LastCamera = TheRenderManager->CameraPosition;
    static bool LastExterior = TheShaderManager->GameState.isExterior;
    const D3DXVECTOR4 Camera = TheRenderManager->CameraPosition;
    const D3DXVECTOR3 Moved(Camera.x - LastCamera.x, Camera.y - LastCamera.y, Camera.z - LastCamera.z);
    if (TheShaderManager->GameState.isExterior != LastExterior || D3DXVec3Length(&Moved) > 1500.0f)
        MaterialMaps::ReleaseAll();
    LastCamera = Camera;
    LastExterior = TheShaderManager->GameState.isExterior;

    const UInt8 Interior = *(UInt8*)0x011F9427;   // BSShaderManager::bInterior
    const bool NVRSkin = TheShaderManager->Shaders.Skin && TheShaderManager->Shaders.Skin->Enabled && TheSettingManager->SettingsMain.Main.RenderEffects;
    FaceGenInteriorFlag = NVRSkin ? 0 : Interior;
}
