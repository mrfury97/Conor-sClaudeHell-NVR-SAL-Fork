#include "WaterReflections.h"

// The waves (and reflection blur) of the water in the player's cell, interior water's in an
// interior, so the reflection bends with the waves that water shows.
void WaterReflectionsEffect::UpdateConstants() {
	WaterShaders* water = TheShaderManager->Shaders.Water;
	if (!water) return;
	const WaterShaders::ComplexWaterStruct& cellWater = water->CellWater();
	Constants.Waves = cellWater.Waves;
	Constants.Blur = cellWater.Lighting3;
}

void WaterReflectionsEffect::UpdateSettings() {
	Constants.Data.x = TheSettingManager->GetSettingF("Shaders.WaterReflections.Main", "Strength");
	Constants.Data.y = max(TheSettingManager->GetSettingF("Shaders.WaterReflections.Main", "MaxDistance"), 100.0f);
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.WaterReflections.Main", "Distortion");
	Constants.Data.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.WaterReflections.Main", "DebugView"), 0, 6);
}

void WaterReflectionsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsWaves", &Constants.Waves);
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsBlur", &Constants.Blur);
}

// Part of Complex Water: only while its shaders draw the water (they keep the water height, the
// wave field and the reflectivity the effect reads up to date), where the cell has water, and above
// it (below, the Underwater effect takes over).
bool WaterReflectionsEffect::ShouldRender() {
	WaterShaders* water = TheShaderManager->Shaders.Water;
	return water && water->Enabled && water->HasWater && !TheShaderManager->GameState.isUnderwater;
}
