#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

// [Shaders.ShadowsExteriors.TerrainShadows]: the land's shadows on the land, far past the sun's cascades
// (mountains darkening the valleys beyond them at a low sun), after Community Shaders' Terrain Shadows
// (GPL-3), from the same xLODGen height maps (FNVLODGen: Terrain LOD > Height maps), one per worldspace:
//   Data\Textures\Terrain\<Worldspace>\<Worldspace>.HeightMap.<S>.<W>.<N>.<E>.<min z>.<max z>.dds
// 16-bit, 32 points per cell (128 units apart), 32767 = height 0, steps of 8 units.
//
// The file is read on a worker thread when the worldspace changes (shrunk to MaxResolution a side by
// averaging). From the heights, each texel's shadow height -- below it the sun is hidden by land between
// it and the sun -- is swept toward the sun on a worker thread whenever the sun has moved a little: each
// texel takes the higher of its own height and the shadow height of the texel before it on the light's
// path, lowered by how far the light drops over one step. Two channels, the sun's elevation minus and
// plus Softness, give the soft edge; the map's own ground height is kept beside them. The sun-shadow
// composite (Effects/ShadowsExteriors.fx, TerrainShadow) compares each pixel's height with them, never
// taking a pixel as lower than that ground: the LOD land on screen is simplified and sits below the
// map in places, which would otherwise shadow itself (and flicker with TAA's jitter).
class TerrainShadowMap {
public:
	struct SettingsStruct {
		bool	Enabled;
		UINT	MaxResolution;     // texels on the longer side at most
		float	Bias;              // shadow heights lowered by this (hides the land shadowing its own slopes)
		float	Softness;          // degrees either side of the sun's elevation
		float	Strength;
		float	Darkness;          // share of the ambient and sky light the land's shadow also takes
		float	Contrast;          // steepens the shadow's edge (1 as swept)
		int		DebugView;
	};
	SettingsStruct	Settings = {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;     // x west edge, y north edge (world), z 1 / width (units), w strength (0: off or not ready)
		D3DXVECTOR4	Range;    // x lowest encoded height, y encoded range, z bias, w blur (game units)
		D3DXVECTOR4	Params;   // x 1 / height (units), y DebugView, z Darkness, w Contrast
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	Texture = nullptr;   // A16B16G16R16: r upper, g lower shadow height, b ground (TESR_TerrainShadowBuffer)

	void	UpdateSettings();
	void	RegisterConstants();
	void	RegisterTextures();
	// Once a frame outdoors, from ShadowManager::RenderShadowMaps. SunDir points at the sun (or moon).
	void	Update(IDirect3DDevice9* Device, const D3DXVECTOR3& SunDir);

private:
	struct HeightMap {
		std::vector<float>	Heights;   // row 0 north, column 0 west
		UINT	Width = 0, Height = 0;
		float	West = 0.0f, North = 0.0f;   // the centre of texel (0, 0)
		float	Texel = 128.0f;              // game units between texel centres
		void*	WorldSpace = nullptr;
		std::string	Message;                 // logged when done
	};
	struct SweepResult {
		std::vector<UINT64>	Packed;
		float	Low = 0.0f, Range = 1.0f, Blur = 0.0f;
		UINT	Generation = 0;
		std::string	Error;   // set if it failed (out of memory), logged
	};

	// Both run on worker threads and never throw: a failure comes back as an empty result and a message.
	static HeightMap	Load(void* WorldSpace, std::string Name, UINT MaxResolution);
	static SweepResult	Sweep(std::shared_ptr<const HeightMap> Map, D3DXVECTOR3 SunDir, float Softness, float Bias, UINT Generation);
	void	ReleaseTexture();

	void*				RequestedWorldSpace = nullptr;   // the land worldspace a load was started for
	UINT				RequestedResolution = 0;
	std::future<HeightMap>	LoadJob;
	bool				Loading = false;
	std::shared_ptr<const HeightMap>	Map;   // the loaded heights (empty: none for this worldspace), shared with a running sweep
	UINT				Generation = 0;      // bumped by every load

	std::future<SweepResult>	SweepJob;
	bool				Sweeping = false;
	D3DXVECTOR3			SweptSunDir = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	UINT				SweptGeneration = ~0u;
	float				SweptBias = -1.0f, SweptSoftness = -1.0f;
	ULONGLONG			SettingsChangedAt = 0;   // GetTickCount64 of the last change to MaxResolution, Bias or Softness:
	                                             // reloads and sweeps for them wait until the menu has been left alone
	bool				Uploaded = false;    // the texture holds a sweep, of UploadedWorldSpace's map
	UINT				UploadedGeneration = ~0u;
	// A sweep of the same map with the sun moved is copied into the texture a strip of rows a frame
	// (all of it at once took milliseconds on the main thread); the encoding does not depend on the sun,
	// so rows from the two sweeps read the same while it runs.
	SweepResult			PendingUpload;
	UINT				PendingRow = 0;
	bool				Uploading = false;
	void*				UploadedWorldSpace = nullptr;
	UINT				TextureWidth = 0, TextureHeight = 0;
};
