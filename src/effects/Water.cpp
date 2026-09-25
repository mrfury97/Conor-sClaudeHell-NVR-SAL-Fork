#include <algorithm>

#include "Water.h"

void WaterShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WaterDeepColor", &Constants.deepColor);
	TheShaderManager->RegisterConstant("TESR_WaterShallowColor", &Constants.shallowColor);
	TheShaderManager->RegisterConstant("TESR_WaterLODColor", &Constants.LODColor);
	TheShaderManager->RegisterConstant("TESR_WaterFog", &Constants.Fog);
	TheShaderManager->RegisterConstant("TESR_WaterCoefficients", &Constants.Default.waterCoefficients);
	TheShaderManager->RegisterConstant("TESR_WaveParams", &Constants.Default.waveParams);
	TheShaderManager->RegisterConstant("TESR_WaterVolume", &Constants.Default.waterVolume);
	TheShaderManager->RegisterConstant("TESR_WaterSettings", &Constants.Default.waterSettings);
	TheShaderManager->RegisterConstant("TESR_WaterShorelineParams", &Constants.Default.shorelineParams);
	TheShaderManager->RegisterConstant("TESR_PlacedWaterCoefficients", &Constants.Placed.waterCoefficients);
	TheShaderManager->RegisterConstant("TESR_PlacedWaveParams", &Constants.Placed.waveParams);
	TheShaderManager->RegisterConstant("TESR_PlacedWaterVolume", &Constants.Placed.waterVolume);
	TheShaderManager->RegisterConstant("TESR_PlacedWaterSettings", &Constants.Placed.waterSettings);
	TheShaderManager->RegisterConstant("TESR_PlacedWaterShorelineParams", &Constants.Placed.shorelineParams);
	TheShaderManager->RegisterConstant("TESR_WaterLighting", &Constants.Lighting);
	TheShaderManager->RegisterConstant("TESR_WaterLighting2", &Constants.Lighting2);
	TheShaderManager->RegisterConstant("TESR_WaterLighting3", &Constants.Lighting3);
	TheShaderManager->RegisterConstant("TESR_WaterLighting4", &Constants.Lighting4);
	TheShaderManager->RegisterConstant("TESR_WaterScatterColor", &Constants.ScatterColor);
	TheShaderManager->RegisterConstant("TESR_WaterAbsorption", &Constants.Absorption);
	TheShaderManager->RegisterConstant("TESR_WaterWaves", &Constants.Waves);
	TheShaderManager->RegisterConstant("TESR_WaterWaves2", &Constants.Waves2);
	TheShaderManager->RegisterConstant("TESR_WaterLighting5", &Constants.Lighting5);
}


void WaterShaders::UpdateConstants() {

	TESWaterForm* currentWater = NULL;
	float height = Tes->GetWaterHeight(Player, WorldSceneGraph, &currentWater);

	// get water height based on player position
	Constants.Default.waterSettings.x = height;
	Constants.Default.waterSettings.z = TheShaderManager->GameState.isUnderwater;
	Constants.Placed.waterSettings.x = Constants.Default.waterSettings.x;
	Constants.Placed.waterSettings.z = Constants.Default.waterSettings.z;

	TESWorldSpace* worldSpace = Player->GetWorldSpace();
	if (worldSpace) {
		TESWaterForm* LODWater = worldSpace->waterFormLast;
		if (LODWater)
			Constants.LODColor = LODWater->GetShallowColor()->toD3DXVECTOR4();
	}

	if (currentWater) {
		Constants.deepColor = currentWater->GetDeepColor()->toD3DXVECTOR4();
		Constants.shallowColor = currentWater->GetShallowColor()->toD3DXVECTOR4();
		Constants.Fog.x = currentWater->properties.fogNearUW;
		Constants.Fog.y = currentWater->properties.fogFarUW;
		Constants.Fog.z = currentWater->properties.fogAmountUW;
		Constants.Fog.z = 1;
	}

	// Complex Water: is the sun out here at all. Placed water is drawn indoors too, where the game's
	// time of day (and so its sun) still runs.
	Constants.Lighting4.z = TheShaderManager->GameState.isExterior ? 1.0f : 0.0f;

	// caustics strength
	Constants.Default.waterVolume.x = Constants.Placed.waterVolume.x = causticsStrength * TheShaderManager->ShaderConst.sunGlare;
}

void WaterShaders::UpdateSettings() {
	//SettingsWaterStruct* sws = NULL;

	TESWaterForm* currentWater = Player->parentCell->GetWaterForm();
	const char* sectionName = "Shaders.Water.Default";
	if (!TheShaderManager->GameState.isExterior) sectionName = "Shaders.Water.Interiors";
	
	if (currentWater) {
		UInt32 WaterType = currentWater->GetWaterType();
		if (WaterType == TESWaterForm::WaterType::kWaterType_Blood)
			sectionName = "Shaders.Water.Blood";
		else if (WaterType == TESWaterForm::WaterType::kWaterType_Lava)
			sectionName = "Shaders.Water.Lava";

		// world space specific settings. TODO Reimplement with Toml
		//else if (!(sws = TheSettingManager->GetSettingsWater(currentCell->GetEditorName())) && currentWorldSpace)
		//	sws = TheSettingManager->GetSettingsWater(currentWorldSpace->GetEditorName());
	}

	Constants.Default.waterCoefficients.x = TheSettingManager->GetSettingF(sectionName, "inExtCoeff_R");
	Constants.Default.waterCoefficients.y = TheSettingManager->GetSettingF(sectionName, "inExtCoeff_G");
	Constants.Default.waterCoefficients.z = TheSettingManager->GetSettingF(sectionName, "inExtCoeff_B");
	Constants.Default.waterCoefficients.w = TheSettingManager->GetSettingF(sectionName, "inScattCoeff");
	causticsStrength = TheSettingManager->GetSettingF(sectionName, "causticsStrength"); // later modified by current sunglare
	Constants.Default.waveParams.x = TheSettingManager->GetSettingF(sectionName, "choppiness");
	Constants.Default.waveParams.y = TheSettingManager->GetSettingF(sectionName, "waveWidth");
	Constants.Default.waveParams.z = TheSettingManager->GetSettingF(sectionName, "waveSpeed");
	Constants.Default.waveParams.w = TheSettingManager->GetSettingF(sectionName, "reflectivity");
	Constants.Default.waterSettings.y = TheSettingManager->GetSettingF(sectionName, "depthDarkness");
	Constants.Default.waterVolume.z = TheSettingManager->GetSettingF(sectionName, "turbidity");
	Constants.Default.waterVolume.w = TheSettingManager->GetSettingF(sectionName, "causticsStrengthS");
	Constants.Default.shorelineParams.x = TheSettingManager->GetSettingF(sectionName, "shoreMovement");
	Constants.Default.waterSettings.w = TheSettingManager->GetSettingF(sectionName, "refractionPower");


	Constants.Placed.waveParams.x = TheSettingManager->GetSettingF("Shaders.Water.Placed", "choppiness");
	Constants.Placed.waveParams.y = TheSettingManager->GetSettingF("Shaders.Water.Placed", "waveWidth");
	Constants.Placed.waveParams.z = TheSettingManager->GetSettingF("Shaders.Water.Placed", "waveSpeed");
	Constants.Placed.waveParams.w = TheSettingManager->GetSettingF("Shaders.Water.Placed", "reflectivity");
	Constants.Placed.shorelineParams.x = TheSettingManager->GetSettingF("Shaders.Water.Placed", "shoreMovement");
	Constants.Placed.waterSettings.w = TheSettingManager->GetSettingF("Shaders.Water.Placed", "refractionPower");

	// Complex Water (ComplexWater.pso.hlsl), for every kind of water. Each term is off at 0, which is also
	// what a missing key reads as; the ones where 0 would be meaningless fall back to their defaults.
	const char* Section = "Shaders.Water.ComplexWater";
	auto Read = [Section](const char* Key, float Min, float Max) { return std::clamp(TheSettingManager->GetSettingF(Section, Key), Min, Max); };
	auto ReadOr = [Section](const char* Key, float Min, float Max, float Fallback) {
		float Value = TheSettingManager->GetSettingF(Section, Key);
		return Value > 0.0f ? std::clamp(Value, Min, Max) : Fallback;
	};

	Constants.Lighting.x = Read("SunShadows", 0.0f, 1.0f);
	Constants.Lighting.y = ReadOr("AbsorptionDepth", 0.1f, 5.0f, 1.2f);
	Constants.Lighting.z = ReadOr("WaterColorBrightness", 0.1f, 5.0f, 1.0f);
	Constants.Lighting.w = Read("WaveScattering", 0.0f, 3.0f);
	Constants.Lighting2.x = Read("SpecularAA", 0.0f, 1.0f);
	Constants.Lighting2.y = Read("PointLights", 0.0f, 3.0f);
	Constants.Lighting2.z = Read("SunGlitter", 0.0f, 3.0f);
	Constants.Lighting2.w = (float)std::clamp(TheSettingManager->GetSettingI(Section, "DebugView"), 0, 16);
	Constants.Lighting3.x = Read("Foam", 0.0f, 1.0f);
	Constants.Lighting3.y = ReadOr("FoamWidth", 2.0f, 400.0f, 12.0f);
	Constants.Lighting3.z = Read("ShoreFadeWidth", 0.0f, 300.0f);
	Constants.Lighting3.w = Read("ReflectionBlur", 0.0f, 3.0f);
	Constants.Lighting4.x = Read("Caustics", 0.0f, 3.0f);
	Constants.Lighting4.y = ReadOr("CausticsScale", 50.0f, 3000.0f, 220.0f);
	Constants.Lighting4.w = Read("ScreenSpaceReflections", 0.0f, 1.0f);
	// SkipReflectionPass: the game's reflection pass draws the world a second time, mirrored, for the
	// reflection map -- CPU time on every frame with water in view. Skipped, outdoor water reflects the
	// sky along the reflected ray under the screen-space reflections instead.
	SkipReflectionPass = TheSettingManager->GetSettingI(Section, "SkipReflectionPass") != 0;
	Constants.Lighting5.x = SkipReflectionPass ? 0.0f : 1.0f;

	// Wave shape: off at WaveHeight 0 (also missing), which leaves the normal-map waves alone.
	Constants.Waves.x = Read("WaveHeight", 0.0f, 60.0f);
	Constants.Waves.y = ReadOr("WaveLength", 50.0f, 5000.0f, 200.0f);
	Constants.Waves.z = Read("WaveDirection", 0.0f, 360.0f) * 0.0174532925f;
	Constants.Waves.w = Read("WaveSteepness", 0.0f, 1.0f);
	Constants.Waves2.x = Read("Whitecaps", 0.0f, 1.0f);
	Constants.Waves2.y = Read("WaveParallax", 0.0f, 2.0f);
	Constants.Waves2.z = Read("RefractionBlur", 0.0f, 3.0f);
	Constants.Waves2.w = Read("RefractionDispersion", 0.0f, 1.0f);

	// The water body's glow colour. All three 0 (also missing) means the water form's own colours.
	D3DXVECTOR4 scatter(Read("ScatterColorR", 0.0f, 2.0f), Read("ScatterColorG", 0.0f, 2.0f), Read("ScatterColorB", 0.0f, 2.0f), 1.0f);
	Constants.ScatterColor = (scatter.x + scatter.y + scatter.z > 0.0f) ? scatter : D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	// Absorption rate per colour. All three 0 (also missing) means real water's, red fastest.
	D3DXVECTOR4 absorption(Read("AbsorptionColorR", 0.0f, 5.0f), Read("AbsorptionColorG", 0.0f, 5.0f), Read("AbsorptionColorB", 0.0f, 5.0f), 0.0f);
	Constants.Absorption = (absorption.x + absorption.y + absorption.z > 0.0f) ? absorption : D3DXVECTOR4(1.0f, 0.4f, 0.25f, 0.0f);
}