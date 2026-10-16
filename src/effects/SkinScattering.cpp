#include <algorithm>

#include "SkinScattering.h"

static const float SkinScatterMMPerUnit = 14.2857f;   // one game unit is 1/70 m

void SkinScatteringEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkinScatterData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SkinScatterKernel", &Constants.Kernel[0]);
	TheShaderManager->RegisterConstant("TESR_SkinScatterDebug", &Constants.Debug);
}

void SkinScatteringEffect::RegisterTextures() {
	// Created on the first skin draw, to match the scene target exactly (size, multisampling).
	TheTextureManager->RegisterTexture("TESR_SkinScatterBuffer", (IDirect3DBaseTexture9**)&Texture);
	TheTextureManager->RegisterTexture("TESR_SkinAlbedoBuffer", (IDirect3DBaseTexture9**)&AlbedoTexture);

	D3DCAPS9 Caps;
	TheRenderManager->device->GetDeviceCaps(&Caps);
	Supported = Caps.NumSimultaneousRTs >= 3
		&& (Caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS)
		&& (Caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
	if (!Supported) Logger::Log("[WARNING] SkinScattering: the device cannot bind three render targets of their own formats; skin scattering is off");
}

// The settings live on the skin shader's own menu pages: [Shaders.Skin.Scattering] outdoors,
// [Shaders.Skin.Interiors] indoors. There is no [Shaders.SkinScattering] section, so the effect
// record itself is always on and ScreenSpace is the switch (with [Shaders.Skin.Status] Enabled
// above it).
static void ReadProfile(SkinScatteringEffect::ProfileStruct* Profile, const char* Section) {
	Profile->ScreenSpace = TheSettingManager->GetSettingI(Section, "ScreenSpace") != 0;
	Profile->Width = max(0.0f, TheSettingManager->GetSettingF(Section, "Width"));
	Profile->DepthFollow = max(0.0f, TheSettingManager->GetSettingF(Section, "DepthFollow"));
	Profile->AlbedoDetail = std::clamp(TheSettingManager->GetSettingF(Section, "AlbedoDetail"), 0.0f, 1.0f);
	Profile->Strength = D3DXVECTOR3(
		TheSettingManager->GetSettingF(Section, "StrengthRed"),
		TheSettingManager->GetSettingF(Section, "StrengthGreen"),
		TheSettingManager->GetSettingF(Section, "StrengthBlue"));
	// Falloff narrows each channel's profile inside the kernel's fixed sample range (a colour,
	// 0-1, in Community Shaders). Above 1 the profile outgrows the samples: its weight lands on the
	// few widely spaced outer taps and every bright spot is copied out as a row of dots. Width is
	// what widens the scattering, samples and all.
	Profile->Falloff = D3DXVECTOR3(
		std::clamp(TheSettingManager->GetSettingF(Section, "FalloffRed"), 0.0f, 1.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "FalloffGreen"), 0.0f, 1.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "FalloffBlue"), 0.0f, 1.0f));
}

void SkinScatteringEffect::UpdateSettings() {
	ReadProfile(&ExteriorProfile, "Shaders.Skin.Scattering");
	ReadProfile(&InteriorProfile, "Shaders.Skin.Interiors");
	// [Shaders.Skin.Debug] DebugView 8-10 are this effect's (its views 1-3); see SkinShaders.
	const int DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Skin.Debug", "DebugView"), 0, SkinShaders::DebugViewCount);
	Constants.Debug.x = DebugView >= SkinShaders::FirstScatteringDebugView ? (float)(DebugView - SkinShaders::FirstScatteringDebugView + 1) : 0.0f;
	SkinShaderDebug = DebugView > 0 && DebugView < SkinShaders::FirstScatteringDebugView;
	KernelDirty = true;
}

void SkinScatteringEffect::UpdateConstants() {
	// Indoors or out: the kernel is rebuilt when the profile in use changes.
	const bool isInterior = !TheShaderManager->GameState.isExterior;
	if (isInterior != ProfileIsInterior) {
		ProfileIsInterior = isInterior;
		KernelDirty = true;
	}
	Profile = isInterior ? InteriorProfile : ExteriorProfile;
	ScreenSpace = Profile.ScreenSpace;

	if (KernelDirty) {
		CalculateKernel();
		KernelDirty = false;
	}
	Constants.Data.x = Profile.Width / SkinScatterMMPerUnit;   // game units per kernel unit (mm)
	Constants.Data.y = Profile.DepthFollow;
	Constants.Data.z = 0.002f;                                 // relative distance tolerance of the skin test
	Constants.Data.w = Profile.AlbedoDetail;
	// As Community Shaders: the kernel turned by a random angle per pixel, the two passes staying at
	// right angles, which breaks the blur's axis-aligned rows of taps up into noise. Only with TAA,
	// which averages the noise away (CS always has its own temporal AA); the angle moves each frame.
	static UInt32 Frame = 0;
	Frame++;
	const bool TAAOn = TheShaderManager->Effects.TAA && TheShaderManager->Effects.TAA->Enabled;
	Constants.Debug.y = TAAOn ? 1.0f : 0.0f;
	Constants.Debug.z = (float)(Frame % 64);
}

// Ported from Community Shaders' SubsurfaceScattering::CalculateKernel (after Jimenez's
// SeparableSSS): d'Eon's skin profile, its red channel shaped per channel by Falloff, sampled
// at offsets packed toward the centre, normalised, then lerped toward identity by Strength.
static D3DXVECTOR3 SkinScatterGaussian(const D3DXVECTOR3& Falloff, float Variance, float R) {
	float g[3];
	const float falloff[3] = { Falloff.x, Falloff.y, Falloff.z };
	for (int i = 0; i < 3; i++) {
		float rr = R / (0.001f + falloff[i]);
		g[i] = exp(-(rr * rr) / (2.0f * Variance)) / (2.0f * 3.14f * Variance);
	}
	return D3DXVECTOR3(g[0], g[1], g[2]);
}

static D3DXVECTOR3 SkinScatterProfile(const D3DXVECTOR3& Falloff, float R) {
	// The narrowest d'Eon lobe (0.233 at 0.0064 mm^2) is light that barely enters the skin;
	// Strength accounts for it.
	return 0.100f * SkinScatterGaussian(Falloff, 0.0484f, R)
		+ 0.118f * SkinScatterGaussian(Falloff, 0.187f, R)
		+ 0.113f * SkinScatterGaussian(Falloff, 0.567f, R)
		+ 0.358f * SkinScatterGaussian(Falloff, 1.99f, R)
		+ 0.078f * SkinScatterGaussian(Falloff, 7.41f, R);
}

void SkinScatteringEffect::CalculateKernel() {
	const int N = KernelSamples;
	const float Range = N > 20 ? 3.0f : 2.0f;
	const float Exponent = 2.0f;
	D3DXVECTOR4* K = Constants.Kernel;

	float step = 2.0f * Range / (N - 1);
	for (int i = 0; i < N; i++) {
		float o = -Range + float(i) * step;
		float sign = o < 0.0f ? -1.0f : 1.0f;
		K[i].w = Range * sign * fabs(pow(o, Exponent)) / pow(Range, Exponent);
	}

	for (int i = 0; i < N; i++) {
		float w0 = i > 0 ? fabs(K[i].w - K[i - 1].w) : 0.0f;
		float w1 = i < N - 1 ? fabs(K[i].w - K[i + 1].w) : 0.0f;
		float area = (w0 + w1) / 2.0f;
		D3DXVECTOR3 t = area * SkinScatterProfile(Profile.Falloff, K[i].w);
		K[i].x = t.x;
		K[i].y = t.y;
		K[i].z = t.z;
	}

	// The centre sample first.
	D3DXVECTOR4 centre = K[N / 2];
	for (int i = N / 2; i > 0; i--) K[i] = K[i - 1];
	K[0] = centre;

	D3DXVECTOR3 sum(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < N; i++) sum += D3DXVECTOR3(K[i].x, K[i].y, K[i].z);
	for (int i = 0; i < N; i++) {
		K[i].x /= max(sum.x, 1e-6f);
		K[i].y /= max(sum.y, 1e-6f);
		K[i].z /= max(sum.z, 1e-6f);
	}

	const D3DXVECTOR3& s = Profile.Strength;
	K[0].x = (1.0f - s.x) + s.x * K[0].x;
	K[0].y = (1.0f - s.y) + s.y * K[0].y;
	K[0].z = (1.0f - s.z) + s.z * K[0].z;
	for (int i = 1; i < N; i++) {
		K[i].x *= s.x;
		K[i].y *= s.y;
		K[i].z *= s.z;
	}
}

// --- The second render target ----------------------------------------------------------------

void SkinScatteringEffect::ReleaseTarget() {
	if (MSAASurface) { MSAASurface->Release(); MSAASurface = nullptr; }
	if (TextureSurface) { TextureSurface->Release(); TextureSurface = nullptr; }
	if (Texture) { Texture->Release(); Texture = nullptr; }
	if (AlbedoMSAASurface) { AlbedoMSAASurface->Release(); AlbedoMSAASurface = nullptr; }
	if (AlbedoTextureSurface) { AlbedoTextureSurface->Release(); AlbedoTextureSurface = nullptr; }
	if (AlbedoTexture) { AlbedoTexture->Release(); AlbedoTexture = nullptr; }
	TargetDesc = {};
	// Every shader that sampled the old textures holds their raw pointers.
	ClearSampler("TESR_SkinScatterBuffer", strlen("TESR_SkinScatterBuffer"));
	ClearSampler("TESR_SkinAlbedoBuffer", strlen("TESR_SkinAlbedoBuffer"));
}

// True when the scene target is the main view and the second target matches it.
bool SkinScatteringEffect::EnsureTarget(const D3DSURFACE_DESC& SceneDesc) {
	// Reflections, menus and other render-to-texture views draw skin into smaller targets.
	if (SceneDesc.Width != TheRenderManager->width || SceneDesc.Height != TheRenderManager->height) return false;

	if (Texture && TargetDesc.Width == SceneDesc.Width && TargetDesc.Height == SceneDesc.Height &&
		TargetDesc.MultiSampleType == SceneDesc.MultiSampleType && TargetDesc.MultiSampleQuality == SceneDesc.MultiSampleQuality)
		return true;

	// The world scene's target never changes between frames. If it seems to, some other view is
	// drawing skin into a target of the same size, and recreating every time would thrash.
	if (Texture && ++Recreations > 2) {
		Logger::Log("[WARNING] SkinScattering: skin is drawn into more than one full-screen target; skin scattering is off");
		ReleaseTarget();
		Supported = false;
		return false;
	}

	ReleaseTarget();
	IDirect3DDevice9* Device = TheRenderManager->device;
	if (FAILED(Device->CreateTexture(SceneDesc.Width, SceneDesc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &Texture, NULL))) {
		Logger::Log("[ERROR] SkinScattering: could not create the skin target; skin scattering is off");
		Texture = nullptr;
		Supported = false;
		return false;
	}
	Texture->GetSurfaceLevel(0, &TextureSurface);
	if (FAILED(Device->CreateTexture(SceneDesc.Width, SceneDesc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &AlbedoTexture, NULL))) {
		Logger::Log("[ERROR] SkinScattering: could not create the skin albedo target; skin scattering is off");
		AlbedoTexture = nullptr;
		ReleaseTarget();
		Supported = false;
		return false;
	}
	AlbedoTexture->GetSurfaceLevel(0, &AlbedoTextureSurface);

	// MRT targets must share the scene's multisampling; resolved into the textures before use.
	if (SceneDesc.MultiSampleType != D3DMULTISAMPLE_NONE) {
		if (FAILED(Device->CreateRenderTarget(SceneDesc.Width, SceneDesc.Height, D3DFMT_A16B16G16R16F,
			SceneDesc.MultiSampleType, SceneDesc.MultiSampleQuality, FALSE, &MSAASurface, NULL)) ||
			FAILED(Device->CreateRenderTarget(SceneDesc.Width, SceneDesc.Height, D3DFMT_A16B16G16R16F,
			SceneDesc.MultiSampleType, SceneDesc.MultiSampleQuality, FALSE, &AlbedoMSAASurface, NULL))) {
			Logger::Log("[ERROR] SkinScattering: could not create the multisampled skin targets; skin scattering is off");
			ReleaseTarget();
			Supported = false;
			return false;
		}
	}

	TargetDesc = SceneDesc;
	Logger::Log("SkinScattering: skin target %ux%u, %u samples", SceneDesc.Width, SceneDesc.Height, (UInt32)SceneDesc.MultiSampleType);
	return true;
}

// D3D9's Clear clears every bound target, so the skin target is cleared on its own, bound as
// target 0 for a moment, with everything Clear honours put back.
void SkinScatteringEffect::ClearTarget(IDirect3DSurface9* SceneTarget) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	D3DVIEWPORT9 Viewport;
	RECT ScissorRect;
	DWORD Scissor, WriteMask;
	Device->GetViewport(&Viewport);
	Device->GetScissorRect(&ScissorRect);   // binding target 0 resets both
	Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &Scissor);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE, &WriteMask);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

	Device->SetRenderTarget(0, MSAASurface ? MSAASurface : TextureSurface);
	Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	Device->SetRenderTarget(0, AlbedoMSAASurface ? AlbedoMSAASurface : AlbedoTextureSurface);
	Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
	Device->SetRenderTarget(0, SceneTarget);

	Device->SetRenderState(D3DRS_COLORWRITEENABLE, WriteMask);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, Scissor);
	Device->SetScissorRect(&ScissorRect);
	Device->SetViewport(&Viewport);
}

void SkinScatteringEffect::BeginFrame() {
	Unbind();
	ClearedThisFrame = false;
	SkinDrawn = false;
}

// Called by the SkinShader hook just before a skin geometry is drawn.
void SkinScatteringEffect::BindForSkinDraw(NiD3DPixelShaderEx* PixelShader) {
	if (Bound) return;   // the partitions of one skinned geometry
	if (!Enabled || !ScreenSpace || !Supported || Effect == nullptr) return;

	// The world scene only. The engine draws first-person arms, menus and reflections into other
	// targets, some full-screen without multisampling; the depth test would reject the arms anyway.
	if (!InWorldScene) return;
	if (!TheSettingManager->SettingsMain.Main.RenderEffects || !TheShaderManager->Shaders.Skin->Enabled) return;

	// Only NVR's own skin pixel shaders write the second target.
	if (!PixelShader || PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup) return;
	if (TheRenderManager->renderState->GetPixelShader() != PixelShader->ShaderHandle) return;

	IDirect3DDevice9* Device = TheRenderManager->device;

	// Opaque passes write everything; additive light passes (blend ONE, ONE) add their
	// highlights to the colour channels and leave the distance alone. Anything else is a fading
	// or alpha-blended actor, which keeps its own complete colour and is simply not scattered.
	DWORD Blend = FALSE;
	Device->GetRenderState(D3DRS_ALPHABLENDENABLE, &Blend);
	bool Additive = false;
	if (Blend) {
		DWORD Src, Dst;
		Device->GetRenderState(D3DRS_SRCBLEND, &Src);
		Device->GetRenderState(D3DRS_DESTBLEND, &Dst);
		if (Src != D3DBLEND_ONE || Dst != D3DBLEND_ONE) return;
		Additive = true;
	}

	IDirect3DSurface9* Scene = nullptr;
	if (FAILED(Device->GetRenderTarget(0, &Scene)) || !Scene) return;
	D3DSURFACE_DESC SceneDesc;
	Scene->GetDesc(&SceneDesc);
	if (!EnsureTarget(SceneDesc)) {
		Scene->Release();
		return;
	}

	if (!ClearedThisFrame) {
		ClearTarget(Scene);
		ClearedThisFrame = true;
	}
	Scene->Release();

	D3DVIEWPORT9 Viewport;
	Device->GetViewport(&Viewport);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &SavedWriteMask1);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE2, &SavedWriteMask2);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, Additive ? (D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE) : 0xF);
	// Albedo is the same in every pass: the opaque one writes it, additive ones would sum it.
	Device->SetRenderState(D3DRS_COLORWRITEENABLE2, Additive ? 0 : 0xF);
	Device->SetRenderTarget(1, MSAASurface ? MSAASurface : TextureSurface);
	Device->SetRenderTarget(2, AlbedoMSAASurface ? AlbedoMSAASurface : AlbedoTextureSurface);
	Device->SetViewport(&Viewport);

	Bound = true;
	SkinDrawn = true;
}

// Called by the SkinShader hook after the geometry is drawn, and defensively at the end of the
// world and first-person scenes: nothing else may write the skin target.
void SkinScatteringEffect::Unbind() {
	if (!Bound) return;
	IDirect3DDevice9* Device = TheRenderManager->device;
	D3DVIEWPORT9 Viewport;
	Device->GetViewport(&Viewport);
	Device->SetRenderTarget(1, NULL);
	Device->SetRenderTarget(2, NULL);
	Device->SetViewport(&Viewport);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, SavedWriteMask1);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE2, SavedWriteMask2);
	Bound = false;
}

bool SkinScatteringEffect::ShouldRender() {
	if (!ScreenSpace || !SkinDrawn || !Texture || !TextureSurface || !AlbedoTextureSurface) return false;
	// The skin shaders' debug views: shown as drawn, not blurred. The target stays bound during
	// skin draws, so the views still show which skin the blur would take.
	if (SkinShaderDebug) return false;
	if (MSAASurface) TheRenderManager->device->StretchRect(MSAASurface, NULL, TextureSurface, NULL, D3DTEXF_NONE);
	if (AlbedoMSAASurface) TheRenderManager->device->StretchRect(AlbedoMSAASurface, NULL, AlbedoTextureSurface, NULL, D3DTEXF_NONE);
	return true;
}
