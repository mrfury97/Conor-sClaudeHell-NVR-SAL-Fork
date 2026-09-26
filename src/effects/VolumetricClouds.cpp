#include <algorithm>
#include <cmath>

#include "VolumetricClouds.h"

// Per frame: the coverage eases toward the current weather's, and the clouds drift with its wind
// (the weather's wind direction, a compass heading; its wind speed, 0-255), accumulated here so a
// change of direction turns the drift rather than jumping the clouds.
void VolumetricCloudsEffect::UpdateConstants() {
	float dt = (float)TheFrameRateManager->ElapsedTime;
	if (!(dt > 0.0f) || dt > 0.5f) dt = 0.0f; // paused, or a hitch: hold

	Sky* sky = Tes ? Tes->sky : NULL;
	TESWeather* weather = sky ? sky->firstWeather : NULL;
	float target = coverageClear;
	float windSpeed = 0.5f;
	if (weather) {
		UInt8 type = weather->GetWeatherType();
		if (type & (TESWeather::WeatherType::kType_Rainy | TESWeather::WeatherType::kType_Snow)) target = coverageRainy;
		else if (type & TESWeather::WeatherType::kType_Cloudy) target = coverageCloudy;
		windSpeed = weather->GetWindSpeed() / 255.0f;
	}
	if (coverage < 0.0f) coverage = target;
	else {
		float step = coverageChange * dt;
		coverage += std::clamp(target - coverage, -step, step);
	}

	const float TwoPi = 6.2831853f;
	float heading = sky ? sky->windDirection : 0.0f;
	if (fabsf(heading) > TwoPi + 0.01f) heading *= 0.0174532925f; // degrees
	float angle = 1.5707963f - heading; // compass (0 north, clockwise) to an angle from +x (east)
	float speed = driftSpeed * (0.5f + windSpeed);
	float scale = max(Constants.March.z, 100.0f);
	driftX = fmodf(driftX + cosf(angle) * speed * dt, scale);
	driftY = fmodf(driftY + sinf(angle) * speed * dt, scale);
	evolve = fmodf(evolve + evolveSpeed * dt, 1.0f);

	Constants.Shape.x = coverage;
	Constants.Wind.x = driftX;
	Constants.Wind.y = driftY;
	Constants.Wind.z = evolve;
}

void VolumetricCloudsEffect::UpdateSettings() {
	const char* Section = "Shaders.VolumetricClouds.Main";
	auto Read = [Section](const char* Key, float Min, float Max) { return std::clamp(TheSettingManager->GetSettingF(Section, Key), Min, Max); };
	coverageClear = Read("CoverageClear", 0.0f, 1.0f);
	coverageCloudy = Read("CoverageCloudy", 0.0f, 1.0f);
	coverageRainy = Read("CoverageRainy", 0.0f, 1.0f);
	coverageChange = Read("CoverageChange", 0.001f, 1.0f);
	driftSpeed = Read("DriftSpeed", 0.0f, 200.0f);
	evolveSpeed = Read("EvolveSpeed", 0.0f, 0.1f);

	Constants.Shape.y = Read("Density", 0.001f, 1.0f);
	Constants.Shape.z = Read("BaseHeight", 0.0f, 10000.0f);
	Constants.Shape.w = Read("Thickness", 50.0f, 5000.0f);
	Constants.March.x = Read("Steps", 4.0f, 32.0f);
	Constants.March.y = Read("LightSteps", 1.0f, 8.0f);
	Constants.March.z = Read("Scale", 500.0f, 100000.0f);
	Constants.March.w = Read("Strength", 0.0f, 1.0f);
	Constants.Wind.w = (float)std::clamp(TheSettingManager->GetSettingI(Section, "DebugView"), 0, 3);
	Constants.Light.x = Read("SunBrightness", 0.0f, 5.0f);
	Constants.Light.y = Read("SkyBrightness", 0.0f, 5.0f);
	Constants.Light.z = Read("SilverLining", 0.0f, 2.0f);
	Constants.Light.w = Read("HorizonFade", 5000.0f, 300000.0f);
}

void VolumetricCloudsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_VolumetricCloudsShape", &Constants.Shape);
	TheShaderManager->RegisterConstant("TESR_VolumetricCloudsMarch", &Constants.March);
	TheShaderManager->RegisterConstant("TESR_VolumetricCloudsWind", &Constants.Wind);
	TheShaderManager->RegisterConstant("TESR_VolumetricCloudsLight", &Constants.Light);
}

// Outdoors only, above the water, and not at Strength 0.
bool VolumetricCloudsEffect::ShouldRender() {
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater && Constants.March.w > 0.0f;
}
