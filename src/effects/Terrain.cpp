#include <cfloat>
#include <algorithm>

#include "Terrain.h"

void TerrainShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_TerrainData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_TerrainSkyData", &Constants.SkyData);
	TheShaderManager->RegisterConstant("TESR_TerrainPBRData", &Constants.PBRData);
	TheShaderManager->RegisterConstant("TESR_TerrainParallaxData", &ParallaxConstants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainParallaxExtraData", &ParallaxConstants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_TerrainBlendData", &BlendingConstants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainBlendFrame", &BlendingConstants.Frame);
}

// Runs as a land shader binds (ShaderRecord::SetCT), before its constants upload. In the world scene the first
// bind copies the depth drawn so far -- the objects the land will cover -- and every land draw of that scene
// compares against it. Anywhere else (water reflections, cube maps: their own depth buffer) blending is off.
void TerrainShaders::PrepareBlending() {
	BlendingConstants.Data.x = 0.0f;
	if (!BlendingSettings.Enabled || !TheShaderManager->GameState.isExterior) return;
	if (TheTextureManager->TerrainBlendDepthState == 0 || !TheTextureManager->TerrainBlendWorldDepth) {
		if (TheTextureManager->TerrainBlendLogScene) TheTextureManager->TerrainBlendLog += "[no-depth-surface] ";
		return;
	}

	IDirect3DSurface9* Current = nullptr;
	TheRenderManager->device->GetDepthStencilSurface(&Current);
	bool mainScene = Current && Current == TheTextureManager->TerrainBlendWorldDepth;
	if (Current) Current->Release();
	if (TheTextureManager->TerrainBlendLogScene)
		TheTextureManager->TerrainBlendLog += mainScene ? (TheTextureManager->TerrainBlendDepthState == 1 ? "[COPY] " : "[on] ") : "[other-depth] ";
	if (!mainScene) return;

	if (TheTextureManager->TerrainBlendDepthState == 1) {
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->TerrainBlendDepthTexture);
		TheTextureManager->TerrainBlendDepthState = 2;
	}
	BlendingConstants.Data.x = 1.0f;
}

// The states the land's blending changes, as set on the device and in the game's cache together: the cache
// skips a state it believes already set, so changing the device alone desyncs them (as NVR's shadow code once did
// with the depth test).
static const D3DRENDERSTATETYPE LandBlendStates[7] = {
	D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
	D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA,
};

static void SetLandRenderState(D3DRENDERSTATETYPE State, DWORD Value) {
	TheRenderManager->renderState->SetRenderState(State, Value, RenderStateArgs);
	TheRenderManager->device->SetRenderState(State, Value);
}

// The land's pixel shaders: SLS2092-SLS2146 (TerrainShaders::Templates, TerrainTemplate.hlsl). The shader writes
// alpha 1 wherever it does not fade, so blending changes nothing there. The colour blends over the objects drawn
// before the land; the target's alpha is kept as it was.
void TerrainShaders::BeginLandDraw(const char* PixelShaderName) {
	if (!BlendingSettings.Enabled || BlendingSettings.Dither || !TheShaderManager->GameState.isExterior || !PixelShaderName) return;
	if (memcmp(PixelShaderName, "SLS2", 4)) return;
	const int Number = atoi(PixelShaderName + 3);
	if (Number < 2092 || Number > 2146) return;
	for (int i = 0; i < 7; i++) LandBlendSaved[i] = TheRenderManager->renderState->GetRenderState(LandBlendStates[i]);
	SetLandRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	SetLandRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	SetLandRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	SetLandRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	SetLandRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
	SetLandRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
	SetLandRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
	LandBlendSet = true;
}

void TerrainShaders::EndLandDraw() {
	if (!LandBlendSet) return;
	for (int i = 0; i < 7; i++) SetLandRenderState(LandBlendStates[i], LandBlendSaved[i]);
	LandBlendSet = false;
}


void TerrainShaders::UpdateSettings() {
	usePBR = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "UsePBR");

	Settings.Default.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "TerrainSaturation");
	Settings.Default.RoughnessScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "RoughnessScale");
	Settings.Default.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "LightingScale");
	Settings.Default.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "AmbientScale");
	Settings.Default.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "SkylightingScale");
	Settings.Default.SpecularStrength = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "SpecularStrength");
	Settings.Default.VanillaMatchedHighlights = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "VanillaMatchedHighlights");
	Settings.Default.SkyReflectionScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "SkyReflectionScale");
	Settings.Default.AmbientNormalDetail = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "AmbientNormalDetail");

	Settings.Rain.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "TerrainSaturation");
	Settings.Rain.RoughnessScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "RoughnessScale");
	Settings.Rain.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "LightingScale");
	Settings.Rain.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "AmbientScale");
	Settings.Rain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "SkylightingScale");
	Settings.Rain.SpecularStrength = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "SpecularStrength");
	Settings.Rain.VanillaMatchedHighlights = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "VanillaMatchedHighlights");
	Settings.Rain.SkyReflectionScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "SkyReflectionScale");
	Settings.Rain.AmbientNormalDetail = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "AmbientNormalDetail");

	Settings.Night.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "TerrainSaturation");
	Settings.Night.RoughnessScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "RoughnessScale");
	Settings.Night.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "LightingScale");
	Settings.Night.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "AmbientScale");
	Settings.Night.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "SkylightingScale");
	Settings.Night.SpecularStrength = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "SpecularStrength");
	Settings.Night.VanillaMatchedHighlights = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "VanillaMatchedHighlights");
	Settings.Night.SkyReflectionScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "SkyReflectionScale");
	Settings.Night.AmbientNormalDetail = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "AmbientNormalDetail");

	Settings.NightRain.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "TerrainSaturation");
	Settings.NightRain.RoughnessScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "RoughnessScale");
	Settings.NightRain.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "LightingScale");
	Settings.NightRain.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "AmbientScale");
	Settings.NightRain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "SkylightingScale");
	Settings.NightRain.SpecularStrength = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "SpecularStrength");
	Settings.NightRain.VanillaMatchedHighlights = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "VanillaMatchedHighlights");
	Settings.NightRain.SkyReflectionScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "SkyReflectionScale");
	Settings.NightRain.AmbientNormalDetail = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "AmbientNormalDetail");

	ParallaxSettings.Enabled = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "Enabled");
	ParallaxSettings.HighQuality = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "HighQuality");
	ParallaxSettings.Shadows = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "Shadows");
	ParallaxSettings.HeightBlend = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "HeightBlend");
	ParallaxSettings.MaxDistance = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "MaxDistance");
	ParallaxSettings.Height = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "Height");
	ParallaxSettings.ShadowsIntensity = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "ShadowsIntensity");

	// [Shaders.PBR.Main] LinearLighting, shared with the object shaders so both light the same way
	// (two lighting spaces would show where terrain meets objects).
	Constants.SkyData.z = TheSettingManager->GetSettingI("Shaders.PBR.Main", "LinearLighting") ? 1.0f : 0.0f;

	// Land's own material settings: FNV's landscape specular data is crude and vanilla-matched land
	// highlights blow out paths seen against a low sun, so terrain gets its own strength, vanilla
	// match, reflections and ambient detail, per weather (blended in UpdateConstants). The
	// specular occlusion switch is [Shaders.Terrain.Main] only.
	Constants.PBRData.z = TheSettingManager->GetSettingI("Shaders.Terrain.Main", "SpecularOcclusion") ? 1.0f : 0.0f;
	// DebugView stays the object one: one switch for the whole material debug view.
	Constants.PBRData.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.PBR.Main", "DebugView"), 0, 4);

	BlendingSettings.Enabled = TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Enabled");
	BlendingSettings.Debug = TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Debug");
	BlendingSettings.Dither = TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Dither");
	BlendingConstants.Frame.y = BlendingSettings.Dither ? 1.0f : 0.0f;
	BlendingSettings.BlendDistance = max(0.1f, TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "BlendDistance"));
	BlendingSettings.MaxDistance = TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "MaxDistance");
	BlendingConstants.Data.y = BlendingSettings.BlendDistance;
	BlendingConstants.Data.z = BlendingSettings.MaxDistance;
	BlendingConstants.Data.w = BlendingSettings.Debug ? 1.0f : 0.0f;

	LODSettings.NoiseScale = TheSettingManager->GetSettingF("Shaders.Terrain.LOD", "LODNoiseScale");
	LODSettings.NoiseTile = TheSettingManager->GetSettingF("Shaders.Terrain.LOD", "LODNoiseTile");
}

void TerrainShaders::UpdateConstants() {
	// get max value between rain animator and puddle animator
	float rainFactor = max(TheShaderManager->Effects.WetWorld->Constants.Data.x, TheShaderManager->Effects.WetWorld->Constants.Data.z);

	if (!TheShaderManager->GameState.isExterior) return;

	BlendingConstants.Frame.x = fmodf(BlendingConstants.Frame.x + 1.0f, 64.0f);

	Constants.ExtraData.x = usePBR;
	Constants.ExtraData.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Saturation, Settings.Night.Saturation, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.Saturation, Settings.NightRain.Saturation, 0.0), rainFactor);
	// Hemisphere skylight strength. No separate toggle: 0 disables it. Terrain has no
	// Interiors section -- UpdateConstants returns early indoors -- so the interior operand
	// of GetTransitionValue is 0, matching how the other terrain settings blend.
	Constants.SkyData.x = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.SkylightingScale, Settings.Night.SkylightingScale, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.SkylightingScale, Settings.NightRain.SkylightingScale, 0.0), rainFactor);


	Constants.ExtraData.z = LODSettings.NoiseScale;
	Constants.ExtraData.w = LODSettings.NoiseTile;

	if (usePBR) {
		Constants.Data.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.RoughnessScale, Settings.Night.RoughnessScale, 0.0),
			TheShaderManager->GetTransitionValue(Settings.Rain.RoughnessScale, Settings.NightRain.RoughnessScale, 0.0), rainFactor);
	}

	Constants.Data.z = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.LightScale, Settings.Night.LightScale, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.LightScale, Settings.NightRain.LightScale, 0.0), rainFactor);
	Constants.Data.w = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.AmbientScale, Settings.Night.AmbientScale, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.AmbientScale, Settings.NightRain.AmbientScale, 0.0), rainFactor);

	// Land material settings, per weather like the rest.
	auto blend = [&](float TerrainSettings::* member) {
		return std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.*member, Settings.Night.*member, 0.0),
			TheShaderManager->GetTransitionValue(Settings.Rain.*member, Settings.NightRain.*member, 0.0), rainFactor);
	};
	Constants.Data.x = max(0.0f, blend(&TerrainSettings::SpecularStrength));
	Constants.SkyData.w = std::clamp(blend(&TerrainSettings::VanillaMatchedHighlights), 0.0f, 1.0f);
	Constants.PBRData.x = max(0.0f, blend(&TerrainSettings::SkyReflectionScale));
	Constants.PBRData.y = std::clamp(blend(&TerrainSettings::AmbientNormalDetail), 0.0f, 1.0f);

	ParallaxConstants.Data.x = ParallaxSettings.Enabled;
	ParallaxConstants.Data.y = ParallaxSettings.Shadows;
	ParallaxConstants.Data.z = ParallaxSettings.HeightBlend;
	ParallaxConstants.Data.w = ParallaxSettings.HighQuality;

	ParallaxConstants.ExtraData.x = ParallaxSettings.MaxDistance;
	ParallaxConstants.ExtraData.y = ParallaxSettings.Height;
	ParallaxConstants.ExtraData.z = ParallaxSettings.ShadowsIntensity;
};

