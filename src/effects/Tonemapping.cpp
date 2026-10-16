#include "Tonemapping.h"


void TonemappingShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_HDRBloomData", &Constants.BloomData);
	TheShaderManager->RegisterConstant("TESR_HDRData", &Constants.HDRData);
	TheShaderManager->RegisterConstant("TESR_LotteData", &Constants.LotteData);
	TheShaderManager->RegisterConstant("TESR_ToneMapping", &Constants.ToneMapping);
	TheShaderManager->RegisterConstant("TESR_GradingBalanceR", &Constants.GradingBalance[0]);
	TheShaderManager->RegisterConstant("TESR_GradingBalanceG", &Constants.GradingBalance[1]);
	TheShaderManager->RegisterConstant("TESR_GradingBalanceB", &Constants.GradingBalance[2]);
	TheShaderManager->RegisterConstant("TESR_GradingScene", &Constants.GradingScene);
	TheShaderManager->RegisterConstant("TESR_GradingLift", &Constants.GradingLift);
	TheShaderManager->RegisterConstant("TESR_GradingGamma", &Constants.GradingGamma);
	TheShaderManager->RegisterConstant("TESR_GradingGain", &Constants.GradingGain);
	TheShaderManager->RegisterConstant("TESR_GradingControl", &Constants.GradingControl);
}

// White balance as Unity's (ColorUtils.ColorBalanceToLMSCoeffs, after von Kries): the white point moved
// along the daylight locus (temperature) and across it (tint), corrected in LMS cone space. Returned as
// one linear RGB matrix, scaled so that white keeps its luminance (a warmer picture, not a darker one).
static void WhiteBalanceMatrix(float Temperature, float Tint, D3DXVECTOR4* Rows) {
	static const float LinToLMS[3][3] = {
		{ 3.90405e-1f, 5.49941e-1f, 8.92632e-3f },
		{ 7.08416e-2f, 9.63172e-1f, 1.35775e-3f },
		{ 2.31082e-2f, 1.28021e-1f, 9.36245e-1f } };
	static const float LMSToLin[3][3] = {
		{ 2.85847e+0f, -1.62879e+0f, -2.48910e-2f },
		{ -2.10182e-1f, 1.15820e+0f, 3.24281e-4f },
		{ -4.18120e-2f, -1.18169e-1f, 1.06867e+0f } };
	auto xyToLMS = [](float x, float y, float* LMS) {
		float X = x / y, Y = 1.0f, Z = (1.0f - x - y) / y;
		LMS[0] = 0.7328f * X + 0.4296f * Y - 0.1624f * Z;
		LMS[1] = -0.7036f * X + 1.6975f * Y + 0.0061f * Z;
		LMS[2] = 0.0030f * X + 0.0136f * Y + 0.9834f * Z;
	};
	float t1 = Temperature / 65.0f, t2 = Tint / 65.0f;
	float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
	float y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;
	float D65[3], Target[3];
	xyToLMS(0.31271f, 0.32902f, D65);
	xyToLMS(x, y, Target);

	float M[3][3];
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++) {
			float v = 0.0f;
			for (int k = 0; k < 3; k++) v += LMSToLin[r][k] * (D65[k] / Target[k]) * LinToLMS[k][c];
			M[r][c] = v;
		}
	const float Luma[3] = { 0.2126f, 0.7152f, 0.0722f };
	float White = 0.0f;
	for (int r = 0; r < 3; r++) White += Luma[r] * (M[r][0] + M[r][1] + M[r][2]);
	float Scale = White > 1e-4f ? 1.0f / White : 1.0f;
	for (int r = 0; r < 3; r++) Rows[r] = D3DXVECTOR4(M[r][0] * Scale, M[r][1] * Scale, M[r][2] * Scale, 0.0f);
}

// Every frame (cheap): blended by time of day and interior as the transitions run.
void TonemappingShaders::UpdateGrading() {
	auto Blend = [](float GradingValuesStruct::* Member, const TonemappingSettingsStruct& S) {
		return TheShaderManager->GetTransitionValue(S.GradingMain.*Member, S.GradingNight.*Member, S.GradingInteriors.*Member);
	};
	auto BlendAt = [](float (GradingValuesStruct::* Member)[3], int i, const TonemappingSettingsStruct& S) {
		return TheShaderManager->GetTransitionValue((S.GradingMain.*Member)[i], (S.GradingNight.*Member)[i], (S.GradingInteriors.*Member)[i]);
	};
	float Temperature = Blend(&GradingValuesStruct::Temperature, Settings);
	float Tint = Blend(&GradingValuesStruct::Tint, Settings);
	float Contrast = Blend(&GradingValuesStruct::Contrast, Settings);
	float Pivot = Blend(&GradingValuesStruct::ContrastPivot, Settings);
	float Saturation = Blend(&GradingValuesStruct::Saturation, Settings);
	float Vibrance = Blend(&GradingValuesStruct::Vibrance, Settings);
	float HighlightSaturation = Blend(&GradingValuesStruct::HighlightSaturation, Settings);
	float Lift[3], Gamma[3], Gain[3];
	for (int i = 0; i < 3; i++) {
		Lift[i] = BlendAt(&GradingValuesStruct::Lift, i, Settings);
		Gamma[i] = (std::max)(BlendAt(&GradingValuesStruct::Gamma, i, Settings), 0.05f);
		Gain[i] = BlendAt(&GradingValuesStruct::Gain, i, Settings);
	}

	const bool Balance = Temperature != 0.0f || Tint != 0.0f;
	bool Neutral = !Balance && Contrast == 1.0f && Saturation == 1.0f && Vibrance == 0.0f && HighlightSaturation == 1.0f;
	for (int i = 0; i < 3; i++) Neutral = Neutral && Lift[i] == 0.0f && Gamma[i] == 1.0f && Gain[i] == 1.0f;

	if (Balance) WhiteBalanceMatrix(Temperature, Tint, Constants.GradingBalance);
	Constants.GradingScene = D3DXVECTOR4(Contrast, Pivot, Saturation, Vibrance);
	Constants.GradingLift = D3DXVECTOR4(Lift[0], Lift[1], Lift[2], HighlightSaturation);
	Constants.GradingGamma = D3DXVECTOR4(1.0f / Gamma[0], 1.0f / Gamma[1], 1.0f / Gamma[2], 0.0f);
	Constants.GradingGain = D3DXVECTOR4(Gain[0], Gain[1], Gain[2], 0.0f);
	Constants.GradingControl = D3DXVECTOR4((Settings.GradingEnabled && !Neutral) ? 1.0f : 0.0f, Balance ? 1.0f : 0.0f, (float)Settings.DebugView, 0.0f);
}


void TonemappingShaders::UpdateConstants() {
	bool isExterior = TheShaderManager->GameState.isExterior;
	float transitionCurve = TheShaderManager->GameState.transitionCurve;

	UpdateGrading();

	if (TheSettingManager->SettingsChanged || TheShaderManager->GameState.isDayTimeChanged) {
		// Point light multiplier disabled for now
		//Constants.PointLightMult = TheSettingManager->GetSettingTransition("Shaders.Tonemapping", "PointLightMultiplier", isExterior, transitionCurve);
		Constants.ToneMapping.x = TheShaderManager->GetTransitionValue(Settings.Main.HighlightSaturation, Settings.Night.HighlightSaturation, Settings.Interiors.HighlightSaturation);
		Constants.ToneMapping.y = TheShaderManager->GetTransitionValue(Settings.Main.WeatherContrast, Settings.Night.WeatherContrast, Settings.Interiors.WeatherContrast);
		Constants.ToneMapping.z = TheShaderManager->GetTransitionValue(Settings.Main.ToneMappingColor, Settings.Night.ToneMappingColor, Settings.Interiors.ToneMappingColor);
		Constants.ToneMapping.w = TheShaderManager->GetTransitionValue(Settings.Main.Linearization, Settings.Night.Linearization, Settings.Interiors.Linearization);
		Constants.BloomData.x = TheShaderManager->GetTransitionValue(Settings.Main.BloomBlend, Settings.Night.BloomBlend, Settings.Interiors.BloomBlend);
		Constants.BloomData.y = TheShaderManager->GetTransitionValue(Settings.Main.BloomStrength, Settings.Night.BloomStrength, Settings.Interiors.BloomStrength);
		Constants.BloomData.z = TheShaderManager->GetTransitionValue(Settings.Main.WeatherModifier, Settings.Night.WeatherModifier, Settings.Interiors.WeatherModifier);
		Constants.BloomData.w = TheShaderManager->GetTransitionValue(Settings.Main.TonemapWhitePoint, Settings.Night.TonemapWhitePoint, Settings.Interiors.TonemapWhitePoint);
		Constants.HDRData.x = TheShaderManager->GameState.isExterior ? Settings.Main.TonemappingMode : Settings.Interiors.TonemappingMode;
		Constants.HDRData.y = TheShaderManager->GetTransitionValue(Settings.Main.Exposure, Settings.Night.Exposure, Settings.Interiors.Exposure) *
			exp2f(TheShaderManager->GetTransitionValue(Settings.Main.ExposureEV, Settings.Night.ExposureEV, Settings.Interiors.ExposureEV));
		Constants.HDRData.z = TheShaderManager->GetTransitionValue(Settings.Main.Saturation, Settings.Night.Saturation, Settings.Interiors.Saturation);
		Constants.HDRData.w = TheShaderManager->GetTransitionValue(Settings.Main.Gamma, Settings.Night.Gamma, Settings.Interiors.Gamma);

		Constants.LotteData.x = TheShaderManager->GetTransitionValue(Settings.Main.TonemapContrast, Settings.Night.TonemapContrast, Settings.Interiors.TonemapContrast);
		Constants.LotteData.y = TheShaderManager->GetTransitionValue(Settings.Main.TonemapBrightness, Settings.Night.TonemapBrightness, Settings.Interiors.TonemapBrightness);
		Constants.LotteData.z = TheShaderManager->GetTransitionValue(Settings.Main.TonemapMidpoint, Settings.Night.TonemapMidpoint, Settings.Interiors.TonemapMidpoint);
		Constants.LotteData.w = TheShaderManager->GetTransitionValue(Settings.Main.TonemapShoulder, Settings.Night.TonemapShoulder, Settings.Interiors.TonemapShoulder);

		// Scale values for VTLottes/AMDLottes
		if (Constants.HDRData.x == 1.0) {
			float hdrMax = max(1.0, Constants.BloomData.w * 100.0);
			float contrast = max(0.01, Constants.LotteData.x * 1.35);
			float shoulder = max(0.0, (min(1.0, Constants.LotteData.w * 0.993))); // Shoulder should not! exceed 1.0
			float midIn = max(0.01, Constants.LotteData.z * 0.18);
			float midOut = max(0.01, (Constants.LotteData.y * 0.18) / shoulder);
			float colToneB = -((-pows(midIn, contrast) + (midOut * (pows(hdrMax, contrast * shoulder) * pows(midIn, contrast) -
				pows(hdrMax, contrast) * pows(midIn, contrast * shoulder) * midOut)) /
				(pows(hdrMax, contrast * shoulder) * midOut - pows(midIn, contrast * shoulder) * midOut)) /
				(pows(midIn, contrast * shoulder) * midOut));

			float colToneC = (pows(hdrMax, contrast * shoulder) * pows(midIn, contrast) - pows(hdrMax, contrast) * pows(midIn, contrast * shoulder) * midOut) /
				(pows(hdrMax, contrast * shoulder) * midOut - pows(midIn, contrast * shoulder) * midOut);

			Constants.LotteData.x = contrast;
			Constants.LotteData.w = shoulder;
			Constants.LotteData.z = colToneB;
			Constants.LotteData.y = colToneC;
		}
	}
}

void TonemappingShaders::UpdateSettings() {
	Settings.Main.HighlightSaturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "HighlightSaturation");
	Settings.Main.WeatherContrast = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "WeatherContrast");
	Settings.Main.ToneMappingColor = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "ToneMappingColor");
	Settings.Main.Linearization = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "Linearization");
	Settings.Main.BloomBlend = TheSettingManager->GetSettingF("Shaders.Bloom.Main", "Blending");
	Settings.Main.BloomStrength = TheSettingManager->GetSettingF("Shaders.Bloom.Main", "Strength");
	Settings.Main.WeatherModifier = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "WeatherModifier");
	Settings.Main.TonemapWhitePoint = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "TonemapWhitePoint");
	Settings.Main.TonemappingMode = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "TonemappingMode");
	Settings.Main.Exposure = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "Exposure");
	Settings.Main.Saturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "Saturation");
	Settings.Main.Gamma = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "Gamma");

	Settings.Main.TonemapContrast = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "TonemapContrast");
	Settings.Main.TonemapBrightness = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "TonemapBrightness");
	Settings.Main.TonemapMidpoint = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "TonemapMidpoint");
	Settings.Main.TonemapShoulder = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "TonemapShoulder");

	Settings.Night.HighlightSaturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "HighlightSaturation");
	Settings.Night.WeatherContrast = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "WeatherContrast");
	Settings.Night.ToneMappingColor = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "ToneMappingColor");
	Settings.Night.Linearization = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "Linearization");
	Settings.Night.BloomBlend = TheSettingManager->GetSettingF("Shaders.Bloom.Night", "Blending");
	Settings.Night.BloomStrength = TheSettingManager->GetSettingF("Shaders.Bloom.Night", "Strength");
	Settings.Night.WeatherModifier = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "WeatherModifier");
	Settings.Night.TonemapWhitePoint = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "TonemapWhitePoint");
	Settings.Night.TonemappingMode = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "TonemappingMode");
	Settings.Night.Exposure = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "Exposure");
	Settings.Night.Saturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "Saturation");
	Settings.Night.Gamma = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "Gamma");

	Settings.Night.TonemapContrast = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "TonemapContrast");
	Settings.Night.TonemapBrightness = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "TonemapBrightness");
	Settings.Night.TonemapMidpoint = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "TonemapMidpoint");
	Settings.Night.TonemapShoulder = TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "TonemapShoulder");

	Settings.Interiors.HighlightSaturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "HighlightSaturation");
	Settings.Interiors.WeatherContrast = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "WeatherContrast");
	Settings.Interiors.ToneMappingColor = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "ToneMappingColor");
	Settings.Interiors.Linearization = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "Linearization");
	Settings.Interiors.BloomBlend = TheSettingManager->GetSettingF("Shaders.Bloom.Interiors", "Blending");
	Settings.Interiors.BloomStrength = TheSettingManager->GetSettingF("Shaders.Bloom.Interiors", "Strength");
	Settings.Interiors.WeatherModifier = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "WeatherModifier");
	Settings.Interiors.TonemapWhitePoint = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "TonemapWhitePoint");
	Settings.Interiors.TonemappingMode = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "TonemappingMode");
	Settings.Interiors.Exposure = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "Exposure");
	Settings.Interiors.Saturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "Saturation");
	Settings.Interiors.Gamma = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "Gamma");

	Settings.Interiors.TonemapContrast = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "TonemapContrast");
	Settings.Interiors.TonemapBrightness = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "TonemapBrightness");
	Settings.Interiors.TonemapMidpoint = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "TonemapMidpoint");
	Settings.Interiors.TonemapShoulder = TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "TonemapShoulder");

	Settings.Main.ExposureEV = std::clamp(TheSettingManager->GetSettingF("Shaders.Tonemapping.Main", "ExposureEV"), -8.0f, 8.0f);
	Settings.Night.ExposureEV = std::clamp(TheSettingManager->GetSettingF("Shaders.Tonemapping.Night", "ExposureEV"), -8.0f, 8.0f);
	Settings.Interiors.ExposureEV = std::clamp(TheSettingManager->GetSettingF("Shaders.Tonemapping.Interiors", "ExposureEV"), -8.0f, 8.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Tonemapping.Main", "DebugView"), 0, 4);

	auto ReadGrading = [](GradingValuesStruct* Values, const char* Section) {
		Values->Temperature = std::clamp(TheSettingManager->GetSettingF(Section, "Temperature"), -100.0f, 100.0f);
		Values->Tint = std::clamp(TheSettingManager->GetSettingF(Section, "Tint"), -100.0f, 100.0f);
		Values->Contrast = std::clamp(TheSettingManager->GetSettingF(Section, "Contrast"), 0.25f, 4.0f);
		Values->ContrastPivot = std::clamp(TheSettingManager->GetSettingF(Section, "ContrastPivot"), 0.01f, 1.0f);
		Values->Saturation = std::clamp(TheSettingManager->GetSettingF(Section, "Saturation"), 0.0f, 3.0f);
		Values->Vibrance = std::clamp(TheSettingManager->GetSettingF(Section, "Vibrance"), -1.0f, 2.0f);
		Values->HighlightSaturation = std::clamp(TheSettingManager->GetSettingF(Section, "HighlightSaturation"), 0.0f, 3.0f);
		const char* Channels = "RGB";
		for (int i = 0; i < 3; i++) {
			char Key[16];
			sprintf_s(Key, "Lift%c", Channels[i]);
			Values->Lift[i] = std::clamp(TheSettingManager->GetSettingF(Section, Key), -0.5f, 0.5f);
			sprintf_s(Key, "Gamma%c", Channels[i]);
			Values->Gamma[i] = std::clamp(TheSettingManager->GetSettingF(Section, Key), 0.2f, 5.0f);
			sprintf_s(Key, "Gain%c", Channels[i]);
			Values->Gain[i] = std::clamp(TheSettingManager->GetSettingF(Section, Key), 0.0f, 4.0f);
		}
	};
	ReadGrading(&Settings.GradingMain, "Shaders.ColorGrading.Main");
	ReadGrading(&Settings.GradingNight, "Shaders.ColorGrading.Night");
	ReadGrading(&Settings.GradingInteriors, "Shaders.ColorGrading.Interiors");
	Settings.GradingEnabled = TheSettingManager->GetSettingI("Shaders.ColorGrading.Main", "Enabled") != 0;

}