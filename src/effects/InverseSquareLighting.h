#pragma once

// Inverse square falloff for lamps (Shaders/Includes/InverseSquare.hlsl), as Community Shaders'
// Inverse Square Lighting: its own entry in the menu ([Shaders.InverseSquareLighting]). It has no
// shaders of its own: the object, skin, parallax, terrain and hair shaders all read its constant.
class InverseSquareLightingShaders : public ShaderCollection
{
public:
	InverseSquareLightingShaders() : ShaderCollection("InverseSquareLighting") {};

	struct InverseSquareStruct {
		D3DXVECTOR4		Data;   // x 0 off, else the radius past which lamps keep vanilla's falloff; y q (size^2), z k (q / (q + 1)), w scale / (1 - k)
	};
	InverseSquareStruct	Constants;

	struct InverseSquareSettings {
		float	Size;             // the bulb's radius, over the lamp's radius
		float	Strength;         // brightness over vanilla's (1: as much light overall)
		float	FillLightRadius;  // lamps reaching farther keep vanilla's falloff (0: none do)
	};
	InverseSquareSettings	Settings;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};
