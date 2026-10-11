#include <algorithm>

#include "Skin.h"

void SkinShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkinData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SkinExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_SkinDebugData", &Constants.DebugData);
}

void SkinShaders::UpdateConstants() {
	const bool isExterior = TheShaderManager->GameState.isExterior;
	const MaterialStruct& Material = isExterior ? Exterior : Interior;
	Constants.Data.x = Material.SpecularStrength;
	Constants.Data.y = Material.Roughness;
	Constants.Data.z = Material.PerPixelWidth;   // per-pixel diffusion, where the screen-space blur does not reach
	Constants.Data.w = Material.Translucency;
	Constants.ExtraData.z = Material.ShadowScatter;
	// Vanilla-matched highlights, as in Object.hlsl: vanilla lit a face's specular mask with its own
	// Blinn-Phong pass, peaking at mask x the light. The skin's two GGX lobes at F0 0.028 peak at
	// F0 / (4 alpha^2) each; the direct highlight is scaled so their mixed peak reaches the mask,
	// keeping the PBR shape. Matched at the Roughness setting, before rain smooths it (wet skin still
	// shines more) and before the shader's specular AA: 1x at the default 0.45, up to 12x for very
	// rough settings. Settings alone decide it, so it is worked out here, not in every pixel. The
	// lobes' constants are SkinLighting.hlsl's SKIN_F0, SKIN_LOBE0/1_ROUGHNESS and SKIN_LOBE_MIX.
	{
		const float F0 = 0.028f, Lobe0 = 0.75f, Lobe1 = 1.3f, Mix = 0.15f;
		const float a0 = std::clamp(Material.Roughness * Lobe0, 0.04f, 1.0f), a1 = std::clamp(Material.Roughness * Lobe1, 0.04f, 1.0f);
		const float alpha0 = a0 * a0, alpha1 = a1 * a1;
		const float peak = F0 / (4.0f * alpha0 * alpha0) * (1.0f - Mix) + F0 / (4.0f * alpha1 * alpha1) * Mix;
		const float matched = std::clamp(1.0f / (2.0f * peak), 1.0f, 12.0f);   // the 2: the shader reads a 50% mask as 1
		const float weight = std::clamp(Material.VanillaMatchedHighlights, 0.0f, 1.0f);
		Constants.ExtraData.w = 1.0f + (matched - 1.0f) * weight;
	}

	// Nothing occludes the sky the reflection samples, so indoors it would light skin from a sky
	// it cannot see (the object shaders do the same).
	Constants.ExtraData.x = isExterior ? SkyReflectionScale : 0.0f;

	// Rain wets skin: smoother and shinier. Same rain/puddle factor the PBR and terrain shaders use.
	float rainFactor = max(TheShaderManager->Effects.WetWorld->Constants.Data.x, TheShaderManager->Effects.WetWorld->Constants.Data.z);
	Constants.ExtraData.y = isExterior ? std::clamp(rainFactor * RainWetness, 0.0f, 1.0f) : 0.0f;

	// The shader's own views only; the scattering effect's views leave the skin drawn normally.
	Constants.DebugData.x = DebugView < FirstScatteringDebugView ? (float)DebugView : 0.0f;
	Constants.DebugData.y = isExterior ? 0.0f : 1.0f;
}

static void ReadMaterial(SkinShaders::MaterialStruct* Material, const char* Section, const char* ScatteringSection) {
	Material->SpecularStrength = max(0.0f, TheSettingManager->GetSettingF(Section, "SpecularStrength"));
	Material->Roughness = std::clamp(TheSettingManager->GetSettingF(Section, "Roughness"), 0.1f, 1.0f);
	Material->VanillaMatchedHighlights = std::clamp(TheSettingManager->GetSettingF(Section, "VanillaMatchedHighlights"), 0.0f, 1.0f);
	Material->ShadowScatter = max(0.0f, TheSettingManager->GetSettingF(Section, "ShadowScatter"));
	Material->Translucency = max(0.0f, TheSettingManager->GetSettingF(Section, "Translucency"));
	Material->PerPixelWidth = max(0.0f, TheSettingManager->GetSettingF(ScatteringSection, "PerPixelWidth"));
}

void SkinShaders::UpdateSettings() {
	ReadMaterial(&Exterior, "Shaders.Skin.Main", "Shaders.Skin.Scattering");
	ReadMaterial(&Interior, "Shaders.Skin.Interiors", "Shaders.Skin.Interiors");
	SkyReflectionScale = max(0.0f, TheSettingManager->GetSettingF("Shaders.Skin.Main", "SkyReflectionScale"));
	RainWetness = std::clamp(TheSettingManager->GetSettingF("Shaders.Skin.Main", "RainWetness"), 0.0f, 1.0f);
	DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Skin.Debug", "DebugView"), 0, DebugViewCount);
}
