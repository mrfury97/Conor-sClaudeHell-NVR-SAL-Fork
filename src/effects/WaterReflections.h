#pragma once

// Screen-space reflections on the water, as a post-process on the finished frame
// (Effects/WaterReflections.fx.hlsl).
class WaterReflectionsEffect : public EffectRecord
{
public:
	WaterReflectionsEffect() : EffectRecord("WaterReflections") {};

	struct WaterReflectionsStruct {
		D3DXVECTOR4		Data;	// x: Strength  y: MaxDistance  z: Distortion  w: DebugView
		D3DXVECTOR4		Waves;	// the waves of the water in the player's cell (Complex Water's Waves: outdoor or interior)
		D3DXVECTOR4		Blur;	// x: that water's ShallowWaves, w: its ReflectionBlur
	};
	WaterReflectionsStruct	Constants;

	// Strength, MaxDistance and Distortion for outdoor water ([Shaders.WaterReflections.Main]) and for
	// interior water ([Shaders.WaterReflections.Interiors]); the one for the player's cell goes to
	// Constants.Data each frame, with Main's DebugView.
	D3DXVECTOR4		OutdoorData = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	D3DXVECTOR4		InteriorData = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
};
