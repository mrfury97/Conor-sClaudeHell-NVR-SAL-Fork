#pragma once

// Screen-space reflections on the water, as a post-process on the finished frame
// (Effects/WaterReflections.fx.hlsl).
class WaterReflectionsEffect : public EffectRecord
{
public:
	WaterReflectionsEffect() : EffectRecord("WaterReflections") {};

	struct WaterReflectionsStruct {
		D3DXVECTOR4		Data;	// x: Strength  y: MaxDistance  z: Distortion  w: DebugView
	};
	WaterReflectionsStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
};
