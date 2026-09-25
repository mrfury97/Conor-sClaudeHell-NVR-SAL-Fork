#include "WaterReflections.h"

void WaterReflectionsEffect::UpdateConstants() {
}

void WaterReflectionsEffect::UpdateSettings() {
	Constants.Data.x = TheSettingManager->GetSettingF("Shaders.WaterReflections.Main", "Strength");
	Constants.Data.y = max(TheSettingManager->GetSettingF("Shaders.WaterReflections.Main", "MaxDistance"), 100.0f);
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.WaterReflections.Main", "Distortion");
	Constants.Data.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.WaterReflections.Main", "DebugView"), 0, 4);
}

void WaterReflectionsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsData", &Constants.Data);
}

// Only above the water: below it the Underwater effect takes over.
bool WaterReflectionsEffect::ShouldRender() {
	return !TheShaderManager->GameState.isUnderwater;
}
