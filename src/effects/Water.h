#pragma once

class WaterShaders : public ShaderCollection
{
public:
	WaterShaders() : ShaderCollection("Water") {};

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
		D3DXVECTOR4		Lighting;		// TESR_WaterLighting: x sun shadows, y absorption, z absorption depth, w wave scattering
		D3DXVECTOR4		Lighting2;		// TESR_WaterLighting2: x specular AA, y point lights, z physical Fresnel, w debug view
		D3DXVECTOR4		Lighting3;		// TESR_WaterLighting3: x water colour brightness, y wave detail, z foam, w foam width
		D3DXVECTOR4		Lighting4;		// TESR_WaterLighting4: x shore fade width, y reflection blur
	};
	WaterConstants		Constants;

	float	causticsStrength;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};