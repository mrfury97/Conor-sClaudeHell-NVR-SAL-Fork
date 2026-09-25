#pragma once

class WaterShaders : public ShaderCollection
{
public:
	WaterShaders() : ShaderCollection("Water") {};

	// Complex Water: every water surface pixel shader is compiled from one source,
	// ComplexWater.pso.hlsl, with the defines that pick the kind of water it draws.
	std::map<std::string_view, ShaderTemplate> Templates() {
		return std::map<std::string_view, ShaderTemplate>{
			{ "WATER000.pso", ShaderTemplate{ "ComplexWater.pso", {} } },
			{ "WATER017.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_WADING", "1"}} } },
			{ "WATER001.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_PLACED", "1"}} } },
			{ "WATER018.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_PLACED", "1"}, {"WATER_WADING", "1"}} } },
			{ "WATER008.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_INTERIOR", "1"}} } },
			{ "WATER025.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_INTERIOR", "1"}, {"WATER_WADING", "1"}} } },
			{ "WATER033.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_LOD", "1"}} } },
			{ "WATER016.pso", ShaderTemplate{ "ComplexWater.pso", {{"WATER_BELOW", "1"}} } },
		};
	};

	struct WaterStruct {
		D3DXVECTOR4		waterCoefficients;
		D3DXVECTOR4		waveParams;
		D3DXVECTOR4		waterVolume;
		D3DXVECTOR4		waterSettings;
		D3DXVECTOR4		shorelineParams;
	};

	struct WaterConstants {
		WaterStruct		Default;
		WaterStruct		Placed;
		D3DXVECTOR4		Fog;
		D3DXVECTOR4		deepColor;
		D3DXVECTOR4		shallowColor;
		D3DXVECTOR4		LODColor;
		// Complex Water ([Shaders.Water.ComplexWater]; layout in Includes/ComplexWater.hlsl)
		D3DXVECTOR4		Lighting;		// TESR_WaterLighting: x sun shadows, y absorption depth, z water colour brightness, w wave scattering
		D3DXVECTOR4		Lighting2;		// TESR_WaterLighting2: x specular AA, y point lights, z sun glitter, w debug view
		D3DXVECTOR4		Lighting3;		// TESR_WaterLighting3: x foam, y foam width, z shore fade width, w reflection blur
		D3DXVECTOR4		Lighting4;		// TESR_WaterLighting4: x caustics, y caustics scale, z 1 outdoors (set per frame), w screen-space reflections
		D3DXVECTOR4		ScatterColor;	// TESR_WaterScatterColor: rgb water body colour, w 1 when set
		D3DXVECTOR4		Absorption;		// TESR_WaterAbsorption: rgb absorption rates
		D3DXVECTOR4		Waves;			// TESR_WaterWaves: x height, y length, z direction (radians), w steepness
		D3DXVECTOR4		Waves2;			// TESR_WaterWaves2: x whitecaps, y parallax, z refraction blur, w refraction dispersion
		D3DXVECTOR4		Lighting5;		// TESR_WaterLighting5: x 1 when the game's reflection map is rendered
	};
	WaterConstants		Constants;

	// SkipReflectionPass: RenderReflectionsHook skips the game's reflection pass while Complex Water runs.
	bool	SkipReflectionPass = false;

	float	causticsStrength;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};