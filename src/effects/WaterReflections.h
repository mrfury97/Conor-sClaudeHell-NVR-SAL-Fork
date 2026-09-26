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
		D3DXVECTOR4		Blur;	// w: that water's ReflectionBlur
	};
	WaterReflectionsStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
};
