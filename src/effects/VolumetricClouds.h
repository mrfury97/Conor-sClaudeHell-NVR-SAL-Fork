#pragma once

// Volumetric clouds on the sky, as a post-process (Effects/VolumetricClouds.fx.hlsl): a layer of
// raymarched cloud a few kilometres up, lit by the game's sun and sky, as much of it as the weather
// calls for, drifting with the weather's wind.
class VolumetricCloudsEffect : public EffectRecord
{
public:
	VolumetricCloudsEffect() : EffectRecord("VolumetricClouds") {};

	struct VolumetricCloudsStruct {
		D3DXVECTOR4		Shape;	// x: coverage (from the weather)  y: density  z: base height (m)  w: thickness (m)
		D3DXVECTOR4		March;	// x: steps  y: light steps  z: scale (m)  w: strength
		D3DXVECTOR4		Wind;	// xy: drift (m, wrapped to Scale)  z: slow change  w: DebugView
		D3DXVECTOR4		Light;	// x: sun brightness  y: sky brightness  z: silver lining  w: horizon fade (m)
	};
	VolumetricCloudsStruct	Constants;

	// Coverage per kind of weather, eased toward the current weather's (CoverageChange a second).
	float	coverageClear = 0.3f;
	float	coverageCloudy = 0.6f;
	float	coverageRainy = 0.85f;
	float	coverageChange = 0.02f;
	float	coverage = -1.0f;			// below 0: not yet set (set at once, not eased)
	float	driftSpeed = 10.0f;			// m/s at the weather's middle wind speed
	float	evolveSpeed = 0.002f;		// noise tiles a second the clouds change shape by
	float	driftX = 0.0f, driftY = 0.0f, evolve = 0.0f;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
};
