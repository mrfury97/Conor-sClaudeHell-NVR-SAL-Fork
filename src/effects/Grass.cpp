#include <algorithm>

#include "Grass.h"

void GrassShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_GrassScale", &Constants.Scale);
	TheShaderManager->RegisterConstant("TESR_GrassLighting", &Constants.Lighting);
	TheShaderManager->RegisterConstant("TESR_GrassLighting2", &Constants.Lighting2);
	TheShaderManager->RegisterConstant("TESR_GrassLighting3", &Constants.Lighting3);
}

// iMinGrassSize, fTexturePctThreshold, fGrass*Distance and fGrassWindMagnitude* are consumed
// at cell load: writing them affects only cells loaded afterwards.
void GrassShaders::UpdateSettings() {
	if (!Enabled) return;

	Constants.Scale.x = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleX");
	Constants.Scale.y = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleY");
	Constants.Scale.z = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleZ");

	// Grass lighting (GRASS23x000TMS.pso). Each of the four is off at 0, which is also what a missing
	// key reads as, so a TOML without them renders vanilla grass. The two shape exponents fall back to
	// their defaults instead: at 0 they would be meaningless rather than off.
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
}

void GrassShaders::UpdateConstants() {}
