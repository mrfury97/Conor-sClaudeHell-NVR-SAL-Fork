#pragma once

// Dynamic cubemaps: an environment cubemap around the camera, built from the screen every frame,
// for the reflections of authored PBR materials. A D3D9 port of Community Shaders' Dynamic
// Cubemaps (GPL-3.0-or-later); see Effects/DynamicCubemaps.fx.hlsl for the passes.
//
// Each frame, on the scene exactly as drawn (RenderEffectsPreTonemapping, before any effect):
//   1 Capture: the scene's colour is projected into a 256 px capture cube along each texel's
//     direction and blended over what the cube held (two cubes, ping-ponged), then box-filtered
//     down its mips. It accumulates as the camera looks around.
//   2 Infer: what was never seen is filled from coarser mips, then the sky or the room's light.
//   3 Prefilter: GGX-convolved into the environment cube, one roughness per mip.
// The objects drawn next frame sample the environment cube (bound per draw to s11 by
// MaterialMaps in NewVegas/Hooks/Shaders.cpp; Shaders/Includes/Object.hlsl).
class DynamicCubemapsEffect : public EffectRecord
{
public:
	DynamicCubemapsEffect() : EffectRecord("DynamicCubemaps") {};

	static const UINT Size = 256;  // DynamicCubemaps.fx.hlsl CUBE_SIZE
	static const UINT Mips = 9;    // 256 .. 1; prefiltered roughness = mip / (Mips - 1); CUBE_MIPS, and ENVIRONMENT_CUBE_MIPS (Mips - 1) in Object.hlsl

	struct DynamicCubemapsStruct {
		D3DXVECTOR4		Face;       // x face, y 1 / face size, z roughness, w coverage kept per frame where nothing is seen
		D3DXVECTOR4		Fallback;   // rgb the room's ambient light (linear), w 1 outdoors
		D3DXVECTOR4		Capture;    // y 1 to restart the capture
		D3DXVECTOR4		Debug;      // x DebugView (0 off, 1 panorama, 2 mirror, 3 coverage), y mip shown
	};
	DynamicCubemapsStruct	Constants = {};

	// The environment cube the objects reflect, or null while there is none (effect off, not yet
	// built, or creation failed).
	IDirect3DCubeTexture9*	GetEnvironment() { return Valid ? Env : nullptr; }

	void	RenderCubemaps(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
	// [Shaders.DynamicCubemaps.Main] DebugView over the finished frame (end of ShaderManager::RenderEffects).
	void	RenderDebug(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);

	void	UpdateConstants();
	void	UpdateSettings();
	bool	ShouldRender();

private:
	IDirect3DCubeTexture9*	CaptureCube[2] = {};
	IDirect3DCubeTexture9*	Inferred = nullptr;
	IDirect3DCubeTexture9*	Env = nullptr;
	IDirect3DSurface9*		CaptureSurfaces[2][6][Mips] = {};
	IDirect3DSurface9*		InferredSurfaces[6][Mips] = {};
	IDirect3DSurface9*		EnvSurfaces[6][Mips] = {};
	bool		Created = false;
	bool		Failed = false;
	bool		Valid = false;
	bool		Reset = true;
	int			Current = 0;            // the capture cube holding last frame's result
	bool		WasExterior = false;
	D3DXVECTOR3	LastCameraPosition = { 0.0f, 0.0f, 0.0f };

	D3DXHANDLE	FaceHandle = NULL;
	D3DXHANDLE	FallbackHandle = NULL;
	D3DXHANDLE	CaptureHandle = NULL;
	D3DXHANDLE	DebugHandle = NULL;

	bool	EnsureTextures(IDirect3DDevice9* Device);
	void	ReleaseTextures();
	void	DrawFace(IDirect3DDevice9* Device, IDirect3DSurface9* Target, UINT Face, UINT Mip, float Roughness);
	void	DownsampleMips(IDirect3DDevice9* Device, IDirect3DSurface9* Surfaces[6][Mips]);
};
