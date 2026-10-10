#pragma once

// Auto exposure (eye adaptation) before the tone curve, as modern engines do it: the scene's average
// brightness is measured every frame (Effects/AutoExposure.fx), eased toward over time (faster when
// it brightens than when it darkens, as eyes do), and the tonemapping shaders
// (Shaders/ISHDRBLENDINSHADERCIN*.pso, Includes/Tonemapping.hlsl) scale the scene so that average
// lands on Target before the curve compresses it. Tonemapping's Exposure stays, as a compensation
// on top. The old Exposure effect scaled the image after the curve, on colours already compressed.
class AutoExposureEffect : public EffectRecord
{
public:
	AutoExposureEffect() : EffectRecord("AutoExposure") {};

	struct ValuesStruct {
		float Target;          // the brightness the scene's average is brought to (linear, 0.18: middle grey)
		float MinScale;        // the least it scales the scene by (how far a bright scene is darkened)
		float MaxScale;        // the most (how far a dark one is brightened)
		float AdaptBright;     // seconds to adapt to a brighter scene
		float AdaptDark;       // seconds to adapt to a darker one
	};
	struct AutoExposureSettingsStruct {
		ValuesStruct Main;
		ValuesStruct Night;
		ValuesStruct Interiors;
		float CenterWeight;    // how much more the centre of the screen counts (0: evenly)
	};
	AutoExposureSettingsStruct	Settings;

	struct AutoExposureStruct {
		D3DXVECTOR4		Data;   // x 1 on, y target, z min scale, w max scale (the tonemapping shaders)
		D3DXVECTOR4		Time;   // x adapt bright (s), y adapt dark (s), z centre weight, w this frame's time (s)
	};
	AutoExposureStruct	Constants;

	struct AutoExposureTextures {
		IDirect3DTexture9* GridTexture;      // the scene metered in 8 x 4 cells (TESR_AutoExposureGrid)
		IDirect3DSurface9* GridSurface;
		IDirect3DTexture9* AdaptedTexture;   // 1 x 1: r the adapted average brightness, g the scale it gives (TESR_AutoExposureBuffer)
		IDirect3DSurface9* AdaptedSurface;
		IDirect3DTexture9* NewTexture;       // this frame's, before it is copied into the one above
		IDirect3DSurface9* NewSurface;
	};
	AutoExposureTextures	Textures;
	bool					Cleared;   // the adapted value reset to 0 (nothing measured yet)

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	void	Measure();
};
