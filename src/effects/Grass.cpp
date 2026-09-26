#include <algorithm>

#include "Grass.h"

void GrassShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_GrassScale", &Constants.Scale);
	TheShaderManager->RegisterConstant("TESR_GrassLighting", &Constants.Lighting);
	TheShaderManager->RegisterConstant("TESR_GrassLighting2", &Constants.Lighting2);
	TheShaderManager->RegisterConstant("TESR_GrassLighting3", &Constants.Lighting3);
	TheShaderManager->RegisterConstant("TESR_GrassLighting4", &Constants.Lighting4);
	TheShaderManager->RegisterConstant("TESR_GrassLighting5", &Constants.Lighting5);
	TheShaderManager->RegisterConstant("TESR_GrassVariation", &Constants.Variation);
	TheShaderManager->RegisterConstant("TESR_GrassDryTips", &Constants.DryTips);
	TheShaderManager->RegisterConstant("TESR_GrassDryColor", &Constants.DryColor);
}

// iMinGrassSize, fTexturePctThreshold, fGrass*Distance and fGrassWindMagnitude* are consumed
// at cell load: writing them affects only cells loaded afterwards.
void GrassShaders::UpdateSettings() {
	if (!Enabled) return;

	Constants.Scale.x = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleX");
	Constants.Scale.y = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleY");
	Constants.Scale.z = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleZ");

	// Grass lighting (GRASS23x000TMS.pso and the grass shaders compiled from it). Each term is off at 0,
	// which is also what a missing key reads as, so a TOML without them renders vanilla grass; settings
	// where 0 would be meaningless or harmful (exponents, heights, sizes, colours, brightness) fall back
	// to their defaults instead.
	const char* Section = "Shaders.Grass.Main";
	Constants.Lighting.x = std::clamp(TheSettingManager->GetSettingF(Section, "Translucency"), 0.0f, 3.0f);
	Constants.Lighting.y = std::clamp(TheSettingManager->GetSettingF(Section, "Roundness"), 0.0f, 3.0f);
	Constants.Lighting.z = std::clamp(TheSettingManager->GetSettingF(Section, "RootDarkening"), 0.0f, 1.0f);
	Constants.Lighting.w = std::clamp(TheSettingManager->GetSettingF(Section, "Specular"), 0.0f, 2.0f);

	float focus = TheSettingManager->GetSettingF(Section, "TranslucencyFocus");
	float gloss = TheSettingManager->GetSettingF(Section, "SpecularGlossiness");
	Constants.Lighting2.x = focus > 0.0f ? std::clamp(focus, 1.0f, 32.0f) : 4.0f;
	Constants.Lighting2.y = gloss > 0.0f ? std::clamp(gloss, 1.0f, 128.0f) : 16.0f;
	// 0 is plain Lambert, a real setting, so a missing key is taken at its word.
	Constants.Lighting2.w = std::clamp(TheSettingManager->GetSettingF(Section, "DiffuseWrap"), 0.0f, 1.0f);
	float rootHeight = TheSettingManager->GetSettingF(Section, "RootDarkeningHeight");
	Constants.Lighting3.x = rootHeight > 0.0f ? std::clamp(rootHeight, 1.0f, 200.0f) : 20.0f;
	// Off at 0, which is also what a missing key reads as.
	Constants.Lighting3.y = std::clamp(TheSettingManager->GetSettingF(Section, "PointLights"), 0.0f, 3.0f);

	// Distance falloffs: past DetailDistance the costly grass lighting fades out, past ShadowDistance
	// the forward sun shadow does, each over its Fade. A distance of 0 (also a missing key) is no limit.
	Constants.Lighting3.z = std::clamp(TheSettingManager->GetSettingF(Section, "DetailDistance"), 0.0f, 20000.0f);
	Constants.Lighting3.w = std::clamp(TheSettingManager->GetSettingF(Section, "DetailFade"), 1.0f, 10000.0f);
	Constants.Lighting4.x = std::clamp(TheSettingManager->GetSettingF(Section, "ShadowDistance"), 0.0f, 20000.0f);
	Constants.Lighting4.y = std::clamp(TheSettingManager->GetSettingF(Section, "ShadowFade"), 1.0f, 10000.0f);

	// Grass texture brightness. A missing key reads 0, which would blacken the grass: it means 1.
	float brightness = TheSettingManager->GetSettingF(Section, "Brightness");
	Constants.Lighting4.z = brightness > 0.0f ? std::clamp(brightness, 0.1f, 2.0f) : 1.0f;
	// Sky light from the lit normal instead of the flat card's. Off at 0, which is also a missing key.
	Constants.Lighting4.w = std::clamp(TheSettingManager->GetSettingF(Section, "AmbientNormal"), 0.0f, 1.0f);

	// Translucency colour. All three missing read 0, which would put the glow out: that means white.
	D3DXVECTOR4 tint(std::clamp(TheSettingManager->GetSettingF(Section, "TranslucencyColorR"), 0.0f, 2.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "TranslucencyColorG"), 0.0f, 2.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "TranslucencyColorB"), 0.0f, 2.0f), 0.0f);
	Constants.Lighting5 = (tint.x + tint.y + tint.z > 0.0f) ? tint : D3DXVECTOR4(1.0f, 1.0f, 1.0f, 0.0f);

	// Colour variation across fields. Off at 0 (also a missing key); a missing patch size means 1500.
	Constants.Variation.x = std::clamp(TheSettingManager->GetSettingF(Section, "ColorVariation"), 0.0f, 1.0f);
	float patchSize = TheSettingManager->GetSettingF(Section, "ColorVariationScale");
	Constants.Variation.y = patchSize > 0.0f ? std::clamp(patchSize, 100.0f, 20000.0f) : 1500.0f;
	Constants.Variation.z = std::clamp(TheSettingManager->GetSettingF(Section, "ColorVariationBrightness"), 0.0f, 1.0f);
	// Grazing-angle brightening. Off at 0, also a missing key.
	Constants.Variation.w = std::clamp(TheSettingManager->GetSettingF(Section, "GrazingBrightening"), 0.0f, 1.0f);

	// Dry tips. Off at 0 (also a missing key); missing heights and colour fall back to the defaults.
	Constants.DryTips.x = std::clamp(TheSettingManager->GetSettingF(Section, "DryTips"), 0.0f, 1.0f);
	float dryStart = TheSettingManager->GetSettingF(Section, "DryTipsHeight");
	float dryFade = TheSettingManager->GetSettingF(Section, "DryTipsFade");
	Constants.DryTips.y = dryStart > 0.0f ? std::clamp(dryStart, 0.0f, 300.0f) : 25.0f;
	Constants.DryTips.z = dryFade > 0.0f ? std::clamp(dryFade, 1.0f, 300.0f) : 30.0f;
	D3DXVECTOR4 dry(std::clamp(TheSettingManager->GetSettingF(Section, "DryTipsColorR"), 0.0f, 2.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "DryTipsColorG"), 0.0f, 2.0f),
		std::clamp(TheSettingManager->GetSettingF(Section, "DryTipsColorB"), 0.0f, 2.0f), 0.0f);
	Constants.DryColor = (dry.x + dry.y + dry.z > 0.0f) ? dry : D3DXVECTOR4(1.2f, 1.05f, 0.6f, 0.0f);

	// Normal maps: <grass texture>_n.dds beside each grass texture, loose under Data\Textures. Off at 0.
	NormalMapStrength = std::clamp(TheSettingManager->GetSettingF(Section, "NormalMaps"), 0.0f, 2.0f);
	NormalMapFlipGreen = TheSettingManager->GetSettingI(Section, "NormalMapFlipGreen") != 0;
}

void GrassShaders::UpdateConstants() {}
