#include "AutoExposure.h"

void AutoExposureEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_AutoExposureData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_AutoExposureTime", &Constants.Time);
}

void AutoExposureEffect::RegisterTextures() {
	// The cells' sums at 16 bits (filtered when summed); the adapted value at 32: at 16 its easing
	// stalled short of the target at high frame rates (Effects/AutoExposure.fx).
	TheTextureManager->InitTexture("TESR_AutoExposureGrid", &Textures.GridTexture, &Textures.GridSurface, 8, 4, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_AutoExposureBuffer", &Textures.AdaptedTexture, &Textures.AdaptedSurface, 1, 1, D3DFMT_G32R32F);
	TheTextureManager->InitTexture("TESR_AutoExposureNew", &Textures.NewTexture, &Textures.NewSurface, 1, 1, D3DFMT_G32R32F);
	Cleared = false;
}

void AutoExposureEffect::UpdateSettings() {
	auto Read = [](ValuesStruct* Values, const char* Section) {
		Values->Target = std::clamp(TheSettingManager->GetSettingF(Section, "Target"), 0.01f, 2.0f);
		Values->MinScale = std::clamp(TheSettingManager->GetSettingF(Section, "MinScale"), 0.01f, 1.0f);
		Values->MaxScale = std::clamp(TheSettingManager->GetSettingF(Section, "MaxScale"), 1.0f, 64.0f);
		Values->AdaptBright = std::clamp(TheSettingManager->GetSettingF(Section, "AdaptBright"), 0.0f, 30.0f);
		Values->AdaptDark = std::clamp(TheSettingManager->GetSettingF(Section, "AdaptDark"), 0.0f, 30.0f);
	};
	Read(&Settings.Main, "Shaders.AutoExposure.Main");
	Read(&Settings.Night, "Shaders.AutoExposure.Night");
	Read(&Settings.Interiors, "Shaders.AutoExposure.Interiors");
	Settings.CenterWeight = std::clamp(TheSettingManager->GetSettingF("Shaders.AutoExposure.Main", "CenterWeight"), 0.0f, 8.0f);
}

// Run every frame, on or off (ShaderManager::UpdateConstants): off, the tonemapping shaders ignore it.
void AutoExposureEffect::UpdateConstants() {
	auto Blend = [](float ValuesStruct::* Member, const AutoExposureSettingsStruct& S) {
		return TheShaderManager->GetTransitionValue(S.Main.*Member, S.Night.*Member, S.Interiors.*Member);
	};
	Constants.Data = D3DXVECTOR4(Enabled ? 1.0f : 0.0f, Blend(&ValuesStruct::Target, Settings),
		Blend(&ValuesStruct::MinScale, Settings), Blend(&ValuesStruct::MaxScale, Settings));
	Constants.Time = D3DXVECTOR4(Blend(&ValuesStruct::AdaptBright, Settings), Blend(&ValuesStruct::AdaptDark, Settings),
		Settings.CenterWeight, std::clamp((float)TheFrameRateManager->ElapsedTime, 0.0f, 0.25f));
}

// The scene's brightness metered (technique 0, into GridTexture), eased toward and turned into a
// scale (1, into NewTexture, reading AdaptedTexture), then kept (2, copied into AdaptedTexture),
// before the bloom and the tonemapping that reads it (ShaderManager::RenderEffectsPreTonemapping).
void AutoExposureEffect::Measure() {
	if (!Enabled || !Effect || !Textures.GridSurface || !Textures.AdaptedSurface || !Textures.NewSurface) return;
	IDirect3DDevice9* Device = TheRenderManager->device;
	if (!Cleared) {   // 0: nothing measured yet, the first measurement is taken as it is
		Device->SetRenderTarget(0, Textures.AdaptedSurface);
		Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L);
		Cleared = true;
	}
	Device->SetRenderTarget(0, Textures.GridSurface);
	Render(Device, Textures.GridSurface, nullptr, 0, false, nullptr);
	Device->SetRenderTarget(0, Textures.NewSurface);
	Render(Device, Textures.NewSurface, nullptr, 1, false, nullptr);
	Device->SetRenderTarget(0, Textures.AdaptedSurface);
	Render(Device, Textures.AdaptedSurface, nullptr, 2, false, nullptr);
}
