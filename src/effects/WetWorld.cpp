#include "WetWorld.h"

void WetWorldEffect::UpdateConstants() {
	// Rain fall & puddles
	if (TheShaderManager->GameState.isRainy && Constants.PuddlesAnimator.switched == false) {
		// it just started raining
		Constants.PuddlesAnimator.switched = true;
		Constants.PuddlesAnimator.Start(increaseRate, 1);
	}
	else if (!TheShaderManager->GameState.isRainy && Constants.PuddlesAnimator.switched) {
		// it just stopped raining
		Constants.PuddlesAnimator.switched = false;
		Constants.PuddlesAnimator.Start(decreaseRate, 0);
	}
	// How much it rains, from the sky itself, not the Precipitations effect (whose animator only runs while
	// that effect is on): the weathers flagged rainy, across a weather transition the outgoing one fading as
	// the incoming one comes in (Sky::weatherPercent, 0-1 toward firstWeather).
	float Rain = 0.0f;
	Sky* WorldSky = Tes ? Tes->sky : nullptr;
	if (TheShaderManager->GameState.isExterior && WorldSky) {
		auto Rainy = [](TESWeather* Weather) { return (Weather && (Weather->GetWeatherType() & TESWeather::WeatherType::kType_Rainy)) ? 1.0f : 0.0f; };
		const float Progress = std::clamp(WorldSky->weatherPercent, 0.0f, 1.0f);
		Rain = WorldSky->secondWeather ? Rainy(WorldSky->firstWeather) * Progress + Rainy(WorldSky->secondWeather) * (1.0f - Progress) : Rainy(WorldSky->firstWeather);
	}
	Constants.Data.x = Rain;
	Constants.Data.y = TheShaderManager->GameState.isRainy;
	Constants.Data.z = Constants.PuddlesAnimator.GetValue();

	if (Constants.Data.x || Constants.Data.z) TheShaderManager->orthoRequired = true; // mark ortho map calculation as necessary
}

void WetWorldEffect::UpdateSettings() {
	increaseRate = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Increase");
	decreaseRate = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Decrease");

	Constants.Coeffs.x = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_R");
	Constants.Coeffs.y = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_G");
	Constants.Coeffs.z = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_B");
	Constants.Coeffs.w = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleSpecularMultiplier");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Amount");
}

void WetWorldEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WetWorldCoeffs", &Constants.Coeffs);
	TheShaderManager->RegisterConstant("TESR_WetWorldData", &Constants.Data);
}

bool WetWorldEffect::ShouldRender() { 
	return (Constants.Data.x || Constants.Data.z) && TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater;
};