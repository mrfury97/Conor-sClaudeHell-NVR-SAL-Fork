#pragma once

class TonemappingShaders : public ShaderCollection
{
public:
	TonemappingShaders() : ShaderCollection("Tonemapping") {};

	struct ValuesStruct {
		float HighlightSaturation;
		float WeatherContrast;
		float ToneMappingColor;
		float Linearization;
		float BloomBlend;
		float BloomStrength;
		float WeatherModifier;
		float TonemapWhitePoint;
		float TonemappingMode;
		float Exposure;
		float Saturation;
		float Gamma;

		float TonemapContrast;
		float TonemapBrightness;
		float TonemapMidpoint;
		float TonemapShoulder;
		float ExposureEV;           // exposure compensation in stops, on top of Exposure
	};

	// [Shaders.ColorGrading.*]: applied in the tonemapping shaders (Includes/Tonemapping.hlsl GradeScene,
	// GradeDisplay), blended by time of day and interior like the rest.
	struct GradingValuesStruct {
		float Temperature;          // white balance, -100 (cooler) to 100 (warmer)
		float Tint;                 // -100 (green) to 100 (magenta)
		float Contrast;             // in stops around ContrastPivot, 1 neutral
		float ContrastPivot;        // linear, 0.18 middle grey
		float Saturation;
		float Vibrance;
		float HighlightSaturation;
		float Lift[3];
		float Gamma[3];
		float Gain[3];
	};

	struct TonemappingSettingsStruct {
		ValuesStruct Main;
		ValuesStruct Night;
		ValuesStruct Interiors;
		GradingValuesStruct GradingMain;
		GradingValuesStruct GradingNight;
		GradingValuesStruct GradingInteriors;
		bool GradingEnabled;
		int DebugView;
	};
	TonemappingSettingsStruct Settings;

	struct TonemappingStruct {
		D3DXVECTOR4		ToneMapping;
		D3DXVECTOR4		BloomData;
		D3DXVECTOR4		SkyData;
		D3DXVECTOR4		HDRData;
		D3DXVECTOR4		LotteData;
		float			PointLightMult;
		D3DXVECTOR4		GradingBalance[3];   // white balance matrix rows
		D3DXVECTOR4		GradingScene;        // contrast, pivot, saturation, vibrance
		D3DXVECTOR4		GradingLift;         // rgb lift, w highlight saturation
		D3DXVECTOR4		GradingGamma;        // rgb 1 / gamma
		D3DXVECTOR4		GradingGain;         // rgb gain
		D3DXVECTOR4		GradingControl;      // x grading on, y white balance on, z DebugView
	};
	TonemappingStruct Constants;
	
	void	UpdateConstants();
	void	UpdateGrading();
	void	RegisterConstants();
	void	UpdateSettings();
};