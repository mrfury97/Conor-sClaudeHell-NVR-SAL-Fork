#pragma once

class GrassShaders : public ShaderCollection
{
public:
	GrassShaders() : ShaderCollection("Grass") {};

	// The game's other grass pixel shaders are compiled from GRASS23x000TMS.pso's source, so all carry
	// the same grass lighting: the non-multisampled one, and the one-point-light pass (whose vanilla
	// shader reads attenuation coordinates the replacement grass vertex shaders no longer write).
	std::map<std::string_view, ShaderTemplate> Templates() {
		return std::map<std::string_view, ShaderTemplate>{
			{ "GRASS23x000.pso", ShaderTemplate{ "GRASS23x000TMS.pso", {{"GRASS_TMS", "0"}} } },
			{ "GRASS23x001.pso", ShaderTemplate{ "GRASS23x000TMS.pso", {{"GRASS_TMS", "0"}} } },
			{ "GRASS23x001TMS.pso", ShaderTemplate{ "GRASS23x000TMS.pso", {{"GRASS_TMS", "1"}} } },
		};
	};

	struct GrassStruct {
		D3DXVECTOR4		Scale;
		// Zeroed until the first UpdateSettings: all-zero grass lighting is vanilla grass.
		D3DXVECTOR4		Lighting = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);	// x: translucency, y: roundness, z: root darkening, w: specular
		D3DXVECTOR4		Lighting2 = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);	// x: translucency focus, y: specular glossiness, z: unused, w: diffuse wrap
		D3DXVECTOR4		Lighting3 = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);	// x: root darkening height (units), y: point light strength, z: detail distance, w: detail fade
		D3DXVECTOR4		DryTips = D3DXVECTOR4(0.0f, 25.0f, 30.0f, 0.0f);	// x: strength, y: start height, z: fade length
		D3DXVECTOR4		DryColor = D3DXVECTOR4(1.2f, 1.05f, 0.6f, 0.0f);	// rgb: dry tip colour
		D3DXVECTOR4		Variation = D3DXVECTOR4(0.0f, 1500.0f, 0.0f, 0.0f);	// x: colour variation, y: patch size, z: brightness variation, w: grazing brightening
		D3DXVECTOR4		Lighting5 = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 0.0f);	// rgb: translucency colour
		D3DXVECTOR4		Lighting4 = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f);	// x: shadow distance, y: shadow fade, z: brightness, w: ambient normal
	};
	GrassStruct	Constants;
	// Per-texture normal maps (NewVegas/Hooks/GrassNormals.cpp reads these per grass geometry).
	float		NormalMapStrength = 0.0f;
	bool		NormalMapFlipGreen = false;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};