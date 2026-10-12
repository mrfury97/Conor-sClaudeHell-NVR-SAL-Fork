#pragma once

class TerrainShaders : public ShaderCollection
{
public:
	TerrainShaders() : ShaderCollection("Terrain") {};

	std::map<std::string_view, ShaderTemplate> Templates() {
		return std::map<std::string_view, ShaderTemplate>{
			{ "SLS2100.vso", ShaderTemplate{ "TerrainTemplate", {{"VS", ""}} } },
			{ "SLS2092.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "1"}}} },
			{ "SLS2094.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "1"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2096.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "1"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2098.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "1"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2100.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "2"}}} },
			{ "SLS2102.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "2"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2104.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "2"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2106.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "2"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2108.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "3"}}} },
			{ "SLS2110.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "3"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2112.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "3"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2114.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "3"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2116.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "4"}}} },
			{ "SLS2118.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "4"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2120.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "4"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2122.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "4"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2124.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "5"}}} },
			{ "SLS2126.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "5"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2128.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "5"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2130.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "5"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2132.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "6"}}} },
			{ "SLS2134.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "6"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2136.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "6"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2138.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "6"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2140.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "7"}}} },
			{ "SLS2142.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "7"}, {"NUM_PT_LIGHTS", "6"}}} },
			{ "SLS2144.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "7"}, {"NUM_PT_LIGHTS", "12"}}} },
			{ "SLS2146.pso", ShaderTemplate{"TerrainTemplate", {{"PS", ""}, {"TEX_COUNT", "7"}, {"NUM_PT_LIGHTS", "24"}}} },
			{ "SLS2002.vso", ShaderTemplate{"TerrainLODTemplate", {{"VS", ""}}} },
			{ "SLS2003.pso", ShaderTemplate{"TerrainLODTemplate", {{"PS", ""}}} },
			{ "SLS2080.vso", ShaderTemplate{"TerrainFadeTemplate", {{"VS", ""}}} },
			{ "SLS2082.pso", ShaderTemplate{"TerrainFadeTemplate", {{"PS", ""}}} },
		};
	};

	struct TerrainSettings {
		float LightScale;
		float AmbientScale;
		float RoughnessScale;
		float Saturation;
		float SkylightingScale;
		float SpecularStrength;
		float VanillaMatchedHighlights;
		float SkyReflectionScale;
		float AmbientNormalDetail;
	};
	struct ParallaxSettingsStruct {
		bool Enabled;
		bool HighQuality;
		bool Shadows;
		bool HeightBlend;
		float MaxDistance;
		float Height;
		float ShadowsIntensity;
	};
	struct LODSettingsStruct {
		float NoiseScale;
		float NoiseTile;
	};
	struct TerrainSettingsStruct {
		TerrainSettings Default;
		TerrainSettings Rain;
		TerrainSettings Night;
		TerrainSettings NightRain;
	};
	TerrainSettingsStruct Settings;
	ParallaxSettingsStruct ParallaxSettings;
	LODSettingsStruct LODSettings;

	struct TerrainStruct {
		D3DXVECTOR4		Data;
		D3DXVECTOR4		ExtraData;
		// TerrainExtraData is full: x usePBR, y saturation, z NoiseScale, w NoiseTile.
		D3DXVECTOR4		PBRData;      // x: sky reflections, y: ambient normal detail, z: specular occlusion ([Shaders.Terrain.Main]), w: debug view ([Shaders.PBR.Main])
		D3DXVECTOR4		SkyData;      // x: skylight strength, z: linear lighting, w: vanilla-matched highlights
	};
	TerrainStruct	Constants;

	struct ParallaxStruct {
		D3DXVECTOR4     Data;
		D3DXVECTOR4     ExtraData;
	};
	ParallaxStruct  ParallaxConstants;

	// Terrain blending ([Shaders.Terrain.Blending]), after Community Shaders' Terrain Blending: where the land
	// covers an object by less than BlendDistance, the land fades out (dithered, TAA smooths it), so the
	// object sinks softly into the ground instead of ending in a hard line.
	struct BlendingSettingsStruct {
		bool Enabled;
		bool Debug;
		bool Dither;		// fade by a moving dither (needs TAA) instead of alpha blending
		float BlendDistance;
		float MaxDistance;
	};
	BlendingSettingsStruct BlendingSettings;

	struct BlendingStruct {
		D3DXVECTOR4		Data;		// x: on for this draw, y: blend distance, z: max distance, w: debug view
		D3DXVECTOR4		Frame;		// x: frame index (moves the dither every frame), y: 1 to dither instead of blending
	};
	BlendingStruct	BlendingConstants;

	bool			usePBR;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	void	PrepareBlending();
	// Alpha blending for the land's own draws while terrain blending fades it (ShadowLightShader's per-geometry
	// hooks, Hooks/Shaders.cpp): on before a draw with a land pixel shader, the states put back after it.
	void	BeginLandDraw(const char* PixelShaderName);
	void	EndLandDraw();
	bool	LandBlendSet = false;
	DWORD	LandBlendSaved[7] = {};
};