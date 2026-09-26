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
	TheShaderManager->RegisterConstant("TESR_WaterWaveOrigin", &Constants.WaveOrigin);
}


// The wave direction for this frame (Constants.Waves.z, radians anticlockwise from +x). The wave
// pattern is tiled from each layer's origin (Constants.WaveOrigin); turning it about the world origin
// would slide the waves near the player, who is tens of thousands of units from it, so each turn
// instead rotates the origins about the player: the waves around them turn in place.
void WaterShaders::UpdateWaveDirection() {
	const float TwoPi = 6.2831853f;
	float target = waveDirectionSetting;
	Sky* sky = Tes ? Tes->sky : NULL;
	if (waveDirectionFromWind && sky) {
		float wind = sky->windDirection;
		if (fabsf(wind - lastWindLogged) > 0.01f) {
			Logger::Log("Complex Water: weather wind direction %f, speed %f", wind, sky->windSpeed);
			lastWindLogged = wind;
		}
		if (fabsf(wind) > TwoPi + 0.01f) wind *= 0.0174532925f; // degrees
		// A compass heading (0 north, clockwise) to an angle from +x (east), anticlockwise.
		target = 1.5707963f - wind + windDirectionOffset;
	}

	if (!waveAngleSet) {
		waveAngle = target;
		waveAngleSet = true;
		Constants.WaveOrigin = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	else {
		float dt = (float)TheFrameRateManager->ElapsedTime;
		if (!(dt > 0.0f) || dt > 0.5f) dt = 0.0f; // paused, or a hitch: hold
		float diff = fmodf(target - waveAngle, TwoPi);
		if (diff > TwoPi * 0.5f) diff -= TwoPi;
		if (diff < -TwoPi * 0.5f) diff += TwoPi;
		float maxStep = 0.0349066f * dt; // 2 degrees a second
		float step = std::clamp(diff, -maxStep, maxStep);
		if (step != 0.0f) {
			waveAngle = fmodf(waveAngle + step, TwoPi);
			// Rotate each layer's origin about the player by the same step, so the pattern turns
			// about them.
			float c = cosf(step), s = sinf(step);
			float px = Player ? Player->pos.x : 0.0f;
			float py = Player ? Player->pos.y : 0.0f;
			for (int layer = 0; layer < 2; layer++) {
				float& ox = layer ? Constants.WaveOrigin.z : Constants.WaveOrigin.x;
				float& oy = layer ? Constants.WaveOrigin.w : Constants.WaveOrigin.y;
				float dx = ox - px, dy = oy - py;
				ox = px + c * dx - s * dy;
				oy = py + s * dx + c * dy;
			}
		}
	}
	Constants.Waves.z = waveAngle;
}

void WaterShaders::UpdateConstants() {

	UpdateWaveDirection();

	TESWaterForm* currentWater = NULL;
	float height = Tes->GetWaterHeight(Player, WorldSceneGraph, &currentWater);
	TESObjectCELL* cell = Player->parentCell;
	HasWater = cell && (!cell->IsInterior() || (cell->flags0 & TESObjectCELL::kFlags0_HasWater));

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
	// Every water setting lives in [Shaders.Water.ComplexWater]; the old per-kind sections
	// (Default, Interiors, Placed) are gone.
	const char* Section = "Shaders.Water.ComplexWater";
	auto Read = [Section](const char* Key, float Min, float Max) { return std::clamp(TheSettingManager->GetSettingF(Section, Key), Min, Max); };
	auto ReadOr = [Section](const char* Key, float Min, float Max, float Fallback) {
		float Value = TheSettingManager->GetSettingF(Section, Key);
		return Value > 0.0f ? std::clamp(Value, Min, Max) : Fallback;
	};
	bool interior = !TheShaderManager->GameState.isExterior;

	// Refraction and the shoreline, for every kind of water (placed water laps twice as fast, as it
	// did in its own section).
	Constants.Default.waterSettings.w = Constants.Placed.waterSettings.w = Read("Refraction", 0.0f, 2.0f);
	Constants.Default.shorelineParams.x = Read("ShoreMovement", 0.0f, 3.0f) * (interior ? 2.0f : 1.0f);
	Constants.Placed.shorelineParams.x = Read("ShoreMovement", 0.0f, 3.0f) * 2.0f;

	// The underwater view (the Underwater effect, with the head under the surface): its own section,
	// [Shaders.Underwater.Main]. Interiors keep the clear, dark water their old section had: no
	// caustics, god rays or fog tint.
	const char* UnderwaterSection = "Shaders.Underwater.Main";
	auto ReadUnderwater = [UnderwaterSection](const char* Key, float Min, float Max) { return std::clamp(TheSettingManager->GetSettingF(UnderwaterSection, Key), Min, Max); };
	causticsStrength = interior ? 0.0f : ReadUnderwater("Caustics", 0.0f, 10.0f); // later modified by current sunglare
	Constants.Default.waterVolume.w = interior ? 0.0f : ReadUnderwater("GodRays", 0.0f, 3.0f);
	Constants.Default.waterVolume.z = interior ? 1.0f : ReadUnderwater("Murk", 0.0f, 10.0f);
	Constants.Default.waterSettings.y = interior ? 10.0f : ReadUnderwater("DepthDarkness", 0.0f, 20.0f);
	Constants.Default.waterCoefficients.x = interior ? 0.0f : ReadUnderwater("FogR", 0.0f, 5.0f);
	Constants.Default.waterCoefficients.y = interior ? 0.0f : ReadUnderwater("FogG", 0.0f, 5.0f);
	Constants.Default.waterCoefficients.z = interior ? 0.0f : ReadUnderwater("FogB", 0.0f, 5.0f);
	Constants.Default.waterCoefficients.w = interior ? 0.0f : ReadUnderwater("Scattering", 0.0f, 5.0f);

	// The surface waves the underwater view draws (its own, not Complex Water's): fixed at the old
	// sections' values. x choppiness, y wave width, z wave speed, w reflectivity (unused).
	Constants.Default.waveParams = D3DXVECTOR4(interior ? 0.5f : 0.7f, 0.8f, 0.7f, 1.0f);
	Constants.Placed.waveParams = D3DXVECTOR4(0.5f, 0.8f, 0.7f, 1.0f);

	// Complex Water (ComplexWater.pso.hlsl), for every kind of water. Each term is off at 0, which is also
	// what a missing key reads as; the ones where 0 would be meaningless fall back to their defaults.

	Constants.Lighting.x = Read("SunShadows", 0.0f, 1.0f);
	Constants.Lighting.y = ReadOr("AbsorptionDepth", 0.1f, 5.0f, 1.2f);
	Constants.Lighting.z = ReadOr("WaterColorBrightness", 0.1f, 5.0f, 1.0f);
	Constants.Lighting.w = Read("WaveScattering", 0.0f, 3.0f);
	Constants.Lighting2.x = Read("SpecularAA", 0.0f, 1.0f);
	Constants.Lighting2.y = Read("PointLights", 0.0f, 3.0f);
	Constants.Lighting2.z = Read("SunGlitter", 0.0f, 3.0f);
	Constants.Lighting2.w = Read("CrestSharpness", 0.0f, 1.0f);
	Constants.Lighting3.x = Read("Foam", 0.0f, 1.0f);
	Constants.Lighting3.y = ReadOr("FoamWidth", 2.0f, 400.0f, 20.0f);
	Constants.Lighting3.z = Read("ShoreFadeWidth", 0.0f, 300.0f);
	Constants.Lighting3.w = Read("ReflectionBlur", 0.0f, 3.0f);
	Constants.Lighting4.x = Read("Caustics", 0.0f, 3.0f);
	Constants.Lighting4.y = ReadOr("CausticsScale", 50.0f, 3000.0f, 220.0f);
	// GameReflections off (SkipReflectionPass): the game's reflection pass draws the world a second time, mirrored, for the
	// reflection map -- CPU time on every frame with water in view. Skipped, outdoor water reflects the
	// sky along the reflected ray instead (and the WaterReflections effect, what is on the screen).
	// The toggle lives with the screen-space reflections that stand in for it
	// ([Shaders.WaterReflections.Main] GameReflections).
	SkipReflectionPass = TheSettingManager->GetSettingI("Shaders.WaterReflections.Main", "GameReflections") == 0;
	Constants.Lighting5.x = SkipReflectionPass ? 0.0f : 1.0f;
	Constants.Lighting5.y = ReadOr("FoamScale", 20.0f, 5000.0f, 250.0f);
	Constants.Lighting5.z = Read("SkyTint", 0.0f, 1.0f);
	// The detail ripples, from Complex Water's own settings (not the water forms' choppiness,
	// waveWidth and waveSpeed): their strength, and their size against WaveLength.
	Constants.Lighting5.w = Read("Ripples", 0.0f, 2.0f);
	Constants.Lighting4.w = ReadOr("RippleSize", 0.25f, 4.0f, 1.0f);

	// Wave shape: off at WaveHeight 0 (also missing), which leaves the normal-map waves alone.
	Constants.Waves.x = Read("WaveHeight", 0.0f, 60.0f);
	Constants.Waves.y = ReadOr("WaveLength", 50.0f, 5000.0f, 200.0f);
	waveDirectionSetting = Read("WaveDirection", 0.0f, 360.0f) * 0.0174532925f;
	waveDirectionFromWind = TheSettingManager->GetSettingI(Section, "WaveDirectionFromWind") != 0;
	windDirectionOffset = Read("WindDirectionOffset", -360.0f, 360.0f) * 0.0174532925f;
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