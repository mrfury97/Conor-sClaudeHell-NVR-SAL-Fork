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
		D3DXVECTOR4		Lighting4;		// TESR_WaterLighting4: x caustics, y caustics scale
		D3DXVECTOR4		ScatterColor;	// TESR_WaterScatterColor: rgb water body colour, w 1 when set
		D3DXVECTOR4		Absorption;		// TESR_WaterAbsorption: rgb absorption rates
	};
	WaterConstants		Constants;

	float	causticsStrength;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};