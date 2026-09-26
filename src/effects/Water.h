#pragma once

class WaterShaders : public ShaderCollection
{
public:
	WaterShaders() : ShaderCollection("Water") {};

	// Complex Water: every water surface pixel shader is compiled from one source,
	// ComplexWater.pso.hlsl, with the defines that pick the kind of water it draws. DebugView is
	// compiled in too (WATER_DEBUG_VIEW), so it costs the water nothing when off: the shaders are
	// compiled when the game loads them, so changing it takes a restart.
	std::map<std::string_view, ShaderTemplate> Templates() {
		static char DebugView[8];
		int View = TheSettingManager->GetSettingI("Shaders.Water.ComplexWater", "DebugView");
		sprintf(DebugView, "%d", View < 0 ? 0 : (View > 15 ? 15 : View));
		return std::map<std::string_view, ShaderTemplate>{
			{ "WATER000.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER017.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_WADING", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER001.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_PLACED", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER018.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_PLACED", "1"}, {"WATER_WADING", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER008.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_INTERIOR", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER025.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_INTERIOR", "1"}, {"WATER_WADING", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER033.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_LOD", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
			{ "WATER016.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_BELOW", "1"}, {"WATER_DEBUG_VIEW", DebugView}} } },
		};
	};

	struct WaterStruct {
		D3DXVECTOR4		waterCoefficients;
		D3DXVECTOR4		waveParams;
		D3DXVECTOR4		waterVolume;
		D3DXVECTOR4		waterSettings;
		D3DXVECTOR4		shorelineParams;
	};

	// Complex Water's settings for one kind of water (layout in Includes/ComplexWater.hlsl). Each
	// kind has its own set, under its own constant names: outdoor water (and the distant water, and
	// the surface seen from below) TESR_Water*, interior water TESR_InteriorWater*, placed water
	// TESR_PlacedWater*.
	struct ComplexWaterStruct {
		D3DXVECTOR4		Lighting;		// Lighting: x sun shadows, y absorption depth, z water colour brightness, w wave scattering
		D3DXVECTOR4		Lighting2;		// Lighting2: x specular AA, y point lights, z sun glitter, w crest sharpness
		D3DXVECTOR4		Lighting3;		// Lighting3: x foam, y foam width, z shore fade width, w reflection blur
		D3DXVECTOR4		Lighting4;		// Lighting4: x caustics, y caustics scale, z 1 outdoors (set per frame), w ripple size
		D3DXVECTOR4		ScatterColor;	// ScatterColor: rgb water body colour, w 1 when set
		D3DXVECTOR4		Absorption;		// Absorption: rgb absorption rates
		D3DXVECTOR4		Waves;			// Waves: x height, y length, z direction (radians, set per frame), w steepness
		D3DXVECTOR4		Waves2;			// Waves2: x whitecaps, y parallax, z refraction blur, w refraction dispersion
		D3DXVECTOR4		Lighting5;		// Lighting5: x 1 when the game's reflection map is rendered, y foam scale, z sky tint, w ripples
	};

	struct WaterConstants {
		WaterStruct		Default;
		WaterStruct		Placed;
		D3DXVECTOR4		Fog;
		D3DXVECTOR4		deepColor;
		D3DXVECTOR4		shallowColor;
		D3DXVECTOR4		LODColor;
		// Complex Water: [Shaders.Water.ComplexWater] (outdoors), [Shaders.Water.Interiors], [Shaders.Water.Placed].
		ComplexWaterStruct	OutdoorWater;
		ComplexWaterStruct	InteriorWater;
		ComplexWaterStruct	PlacedWater;
		D3DXVECTOR4		WaveOrigin;		// TESR_WaterWaveOrigin: xy the first wave layer's origin in the world, zw the second's (every kind)
	};
	WaterConstants		Constants;

	// The Complex Water set of the water in the player's cell: interior water in an interior, else
	// outdoor water (for the WaterReflections effect, which bends its reflection with the same waves).
	const ComplexWaterStruct& CellWater() const;

	// SkipReflectionPass: RenderReflectionsHook skips the game's reflection pass while Complex Water runs.
	bool	SkipReflectionPass = false;
	// Whether the player's cell has water at TESR_WaterSettings.x at all (every exterior does; an
	// interior only when flagged), for the WaterReflections effect, which finds the water by it.
	bool	HasWater = false;

	// Wave direction: WaveDirection, or with WaveDirectionFromWind the weather's wind (plus
	// WindDirectionOffset). Turned toward at a steady rate, the wave pattern rotating about the
	// player (WaveOrigin) so the waves turn in place instead of sliding.
	float	waveDirectionSetting = 0.0f;
	bool	waveDirectionFromWind = false;
	float	windDirectionOffset = 0.0f;
	float	waveAngle = 0.0f;
	bool	waveAngleSet = false;
	float	lastWindLogged = -1000.0f;
	void	UpdateWaveDirection();

	float	causticsStrength;

	void	RegisterComplexWater(const char* Prefix, ComplexWaterStruct* Water);
	void	ReadComplexWater(const char* Section, ComplexWaterStruct* Water);

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};