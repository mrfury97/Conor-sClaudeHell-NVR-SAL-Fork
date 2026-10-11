#pragma once

// Raindrops on the first-person arms and weapon, after Screen Space Shaders' HUD raindrops (STALKER
// Anomaly, Ascii1457). The drops are drawn by those meshes' own shaders (Shaders/Includes/
// HudRaindrops.hlsl, in SkinTemplate and ObjectTemplate), so they stay on the surface through every
// animation: the game's shader hooks (NewVegas/Hooks/Shaders.cpp) call SetDrawConstants for each draw,
// which turns them on for first-person geometry only, and ClearDrawConstants after it.
//
// This effect keeps the wetness they read: Effects/HudRaindrops.fx eases a 1 x 1 value every frame
// (technique 0, as AutoExposure keeps its exposure) and keeps it (1). It rises while the player stands
// in the rain, not under cover (the ortho map: anything above the camera), and falls under cover,
// indoors or after the rain.
class HudRaindropsEffect : public EffectRecord
{
public:
	HudRaindropsEffect() : EffectRecord("HudRaindrops") {};

	struct HudRaindropsStruct {
		D3DXVECTOR4		Data;    // x rain (0-1)
		D3DXVECTOR4		Time;    // y this frame's seconds, z seconds to wet through, w seconds to dry
		D3DXVECTOR4		Shade;   // z 1 when under cover regardless (indoors), w 1 to wipe (underwater)
		D3DXVECTOR4		Debug;   // x 1 always wet (to try it without rain)
		D3DXVECTOR4		Screen;  // the Pip-Boy screen's drops: x threshold, y clock, z refraction, w drop texture tiles across the screen
	};
	HudRaindropsStruct	Constants = {};

	struct HudRaindropsSettings {
		float DropSize;     // a drop texture tile, in game units
		float Density;
		float WetTime;
		float DryTime;
		float Refraction;
		float Reflection;
		float Gloss;        // the drops'
		float WetGloss;     // the wet surface's around them
		float AnimSpeed;
		int   DebugView;
	};
	HudRaindropsSettings	Settings = {};

	struct HudRaindropsTextures {
		IDirect3DTexture9* StateTexture;   // 1 x 1: r the wetness (0-1), g 1 under cover (TESR_HudRainState)
		IDirect3DSurface9* StateSurface;
		IDirect3DTexture9* NewTexture;     // this frame's, before it is copied into the one above
		IDirect3DSurface9* NewSurface;
		IDirect3DTexture9* ScreenCopy;     // the Pip-Boy screen as the game rendered it (TESR_HudRainScreenCopy)
		IDirect3DSurface9* ScreenCopySurface;
		IDirect3DVertexBuffer9* ScreenQuad;   // a quad over the screen's texture
		UINT ScreenWidth, ScreenHeight;
		D3DFORMAT ScreenFormat;
	};
	HudRaindropsTextures	Textures = {};

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	UpdateWetness(IDirect3DDevice9* Device);

	// The per-draw constants of Shaders/Includes/HudRaindrops.hlsl (c218-c219).
	void	SetDrawConstants(NiGeometry* Geometry);
	void	ClearDrawConstants();
	// The drops on the Pip-Boy's screen, drawn into its texture once the game has rendered the menu
	// into it (FOPipboyManager's RenderPostProcess, Hooks/Shaders.cpp).
	void	DrawOnScreen(BSRenderedTexture* Target);

private:
	bool	Cleared = false;     // the wetness reset to 0 once its texture exists; until then no drops
	float	Clock = 0.0f;        // the drops' clock: runs while it rains
	float	DryingLeft = 0.0f;   // seconds the drops may still be drying (the passes run until then)
	bool	DrawSet = false;     // the hooks' constants are nonzero on the device
};
