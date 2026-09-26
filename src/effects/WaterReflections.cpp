#include "WaterReflections.h"

// The settings for the player's cell (outdoor or interior), and the waves (and reflection blur) of
// the water in it, interior water's in an interior, so the reflection bends with the waves it shows.
void WaterReflectionsEffect::UpdateConstants() {
	Constants.Data = TheShaderManager->GameState.isExterior ? OutdoorData : InteriorData;
	WaterShaders* water = TheShaderManager->Shaders.Water;
	if (!water) return;
	const WaterShaders::ComplexWaterStruct& cellWater = water->CellWater();
	Constants.Waves = cellWater.Waves;
	Constants.Blur = cellWater.Lighting3;
}

void WaterReflectionsEffect::UpdateSettings() {
	float debugView = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.WaterReflections.Main", "DebugView"), 0, 6);
	auto Read = [debugView](const char* Section) {
		return D3DXVECTOR4(
			TheSettingManager->GetSettingF(Section, "Strength"),
			max(TheSettingManager->GetSettingF(Section, "MaxDistance"), 100.0f),
			TheSettingManager->GetSettingF(Section, "Distortion"),
			debugView);
	};
	OutdoorData = Read("Shaders.WaterReflections.Main");
	InteriorData = Read("Shaders.WaterReflections.Interiors");
	Constants.Data = TheShaderManager->GameState.isExterior ? OutdoorData : InteriorData;
}

void WaterReflectionsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsWaves", &Constants.Waves);
	TheShaderManager->RegisterConstant("TESR_WaterReflectionsBlur", &Constants.Blur);
}

// Part of Complex Water: only while its shaders draw the water (they keep the water height, the
// wave field and the reflectivity the effect reads up to date), where the cell has water, and above
// it (below, the Underwater effect takes over); not at all where this kind of cell's Strength is 0.
bool WaterReflectionsEffect::ShouldRender() {
	WaterShaders* water = TheShaderManager->Shaders.Water;
	float strength = (TheShaderManager->GameState.isExterior ? OutdoorData : InteriorData).x;
	return water && water->Enabled && water->HasWater && !TheShaderManager->GameState.isUnderwater && strength > 0.0f;
}
