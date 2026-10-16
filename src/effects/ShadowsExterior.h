#pragma once

#include "TerrainShadows.h"

typedef struct frustum {
	enum PLANE {
		PLANENEAR = 0,
		PLANEFAR = 1,
		PLANELEFT = 2,
		PLANERIGHT = 3,
		PLANETOP = 4,
		PLANEBOTTOM = 5,
	};
	PLANE PLANE;
	D3DXPLANE plane[6];
} frustum;

class ShadowsExteriorEffect : public EffectRecord
{
public:
	TerrainShadowMap	TerrainShadows;   // [Shaders.ShadowsExteriors.TerrainShadows], updated in ShadowManager::RenderShadowMaps

	ShadowsExteriorEffect() : EffectRecord("ShadowsExteriors") {};

	static const int Modes = 3;
	static const int FormatBits = 2;

	D3DFORMAT Formats[Modes][FormatBits] = {
		{D3DFMT_G16R16, D3DFMT_G32R32F},
		{D3DFMT_G16R16F, D3DFMT_G32R32F},
		{D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F},
	};
	
	enum ShadowMapTypeEnum {
		MapNear = 0,
		MapMiddle = 1,
		MapFar = 2,
		MapLod = 3,
		MapOrtho = 4,
	};

	//Constants
	struct ShadowStruct {
		D3DXVECTOR4		SmoothedSunDir;
		D3DXVECTOR4		Data;
		D3DXVECTOR4		FormatData;
		D3DXVECTOR4		ScreenSpaceData;
		D3DXVECTOR4		ContactData;		// x: strength, y: ray length, z: thickness, w: max distance
		D3DXVECTOR4		ContactWeapon;		// the first-person weapon's contact shadows: x ray length, y thickness, z hardness, w depth (nearer is the weapon)
		D3DXVECTOR4		ContactExtra;		// x the world's hardness (0: the strongest hit only), y 1 when the weapon casts onto the world
		D3DXVECTOR4		SunLight;			// rgb: the sun colour the object shaders receive
		D3DXVECTOR4		AmbientLight;		// rgb: the ambient colour the object shaders receive
		D3DXVECTOR4		ContactDebug;		// x: debug view of the contact shadows, 0 off
		D3DXVECTOR4		ContactLight;		// Bend's light coordinate: xy the sun on screen (pixels), z its depth (TESR_DepthBuffer.y), w +1 in front of the camera, -1 behind, 0 off
		D3DXVECTOR4		ContactLightDir;	// toward the sun in view space, the direction the coordinate is made from (the cascades' smoothed one); w the fade after a transition
		D3DXVECTOR4		ContactBend;		// x SurfaceThickness, y BilinearThreshold, z ShadowContrast, w samples per ray
		D3DXMATRIX		ContactReprojection;	// camera-relative world now -> clip space last frame (Shaders/Includes/Shadow.hlsl, GetContactShadow)
		D3DXVECTOR4		ContactSkinMask;	// x 1 when the skin target holds this frame's skin (ShaderManager, before the composite)
		D3DXVECTOR4		ContactInject;		// x 1 while the contact shadows are applied in the skin's lighting, y the depth match
		D3DXVECTOR4		OrthoData;
		D3DXVECTOR4		ShadowFade;
		D3DXMATRIXA16	ShadowWorld;
		D3DXMATRIX		ShadowViewProj;
		D3DXMATRIX		ShadowSpotlightCameraToLight[SpotLightsMax];
		D3DXVECTOR4		ShadowCubeMapLightPosition;
		D3DXVECTOR4		ShadowLightPosition[ShadowCubeMapsMax];
		D3DXVECTOR4		ShadowLightFade[ShadowCubeMapsMax / 4];   // slot i's shadow strength in component i % 4 of [i / 4]: 0 none, 1 full (fading as clusters trade slots)
		D3DXVECTOR4		PointShadowData;   // Shaders/Includes/PointShadow.hlsl: x 1 when lamps are shadowed in the object shaders, y 1 / atlas width, z 1 / atlas height, w a face's size
		D3DXVECTOR4		PointShadowParams; // Shaders/Includes/PointShadow.hlsl: x NearFade, y the atlas's tiles per row, z ShadowNormalOffset x 2 / face size, w ShadowSoftness
		D3DXVECTOR4		LampContactData;   // lamps' contact shadows indoors (Effects/SunShadows.fx, technique 1): x strength (0 off)
		D3DXVECTOR4		LampContactBend;   // the lamps' Bend settings, as ContactBend: x SurfaceThickness, y BilinearThreshold, z ShadowContrast, w samples per ray
		D3DXVECTOR4		LampContactWeapon; // x the first-person weapon's depth (nearer is the weapon), y 1 when it casts onto the world, z 1 for the debug view, w 1 when the skin target is this frame's (set by ShaderManager)
		D3DXVECTOR4		PointShadowPCSS;   // Shaders/Includes/PointShadow.hlsl: x 1 PCSS, y PCSSLightSize, z PCSSMaxSpread (texels), w this frame's turn of the sample pattern
		D3DXVECTOR4		ShadowMapRadius;
		D3DXVECTOR4		ShadowBlur;
		// Forward sun shadows, runtime side. x: 1 when the forward path is SUPPRESSED.
		//
		// The polarity is deliberate. A constant that fails to reach a shader reads as zero,
		// and zero here means "not suppressed" -- i.e. the current, working behaviour. The
		// opposite polarity would turn a dropped constant into silently missing shadows,
		// which is exactly the failure that made this a compile-time macro in the first place.
		D3DXVECTOR4		ForwardData;		// x: 1 when the forward path is suppressed, y: receiver offset toward the sun
		D3DXVECTOR4		ReceiverOffset;		// x: the same offset, for the effects (SunCascades.hlsl)
	};

	// Settings
	struct FormsStruct {
		bool				AlphaEnabled;
		bool				Activators;
		bool				Actors;
		bool				Apparatus;
		bool				Books;
		bool				Containers;
		bool				Doors;
		bool				Furniture;
		bool				Misc;
		bool				Statics;
		bool				Terrain;
		bool				Trees;
		bool				Lod;
		float				MinRadius;
		float				OrigMinRadius;
	};

	struct ShadowMapSettings {
		D3DXMATRIX				ShadowCameraToLight;
		D3DVIEWPORT9			ShadowMapViewPort;
		frustum					ShadowMapFrustum;
		NiFrustumPlanes			ShadowMapFrustumPlanes;
		D3DXVECTOR4				ShadowMapCascadeCenterRadius;
		D3DXVECTOR3				CameraTranslation;  // Camera translation at the moment the shadow matrix was calculated.
		D3DXVECTOR4				ClearColor;
		FormsStruct				Forms;
		float					ShadowMapResolution;
		float					ShadowMapInverseResolution;
		float					ShadowMapRadius;
		float					ShadowMapNear;
		bool					CustomClearRequired;
	};

	struct ShadowMapStruct {
		int					Mode;
		int					FormatBits;
		D3DFORMAT			Format;
		int					CascadeResolution;
		bool                Prefilter;
		bool				MSAA;
		bool				Mipmaps;
		bool				LimitFrequency;
		bool				LodSkipLoaded;     // distant LOD inside the loaded cells casts no sun shadow (ShadowManager::AccumChildren)
		bool				CachedDistant;     // CachedDistantShadows: middle/far/LOD cascades redrawn less often (ShadowManager::RenderShadowMaps)
		int					Anisotropy;
		float				Distance;
		float				CascadeLambda;
	};

	struct OrthoStruct {
		int					Resolution;
		float				Distance;
		bool				LimitFrequency;
	};

	struct ExteriorsStruct {
		bool				Enabled;
		bool				ForwardShadows;
		bool				UsePointShadowsDay;
		bool				UsePointShadowsNight;
		int					Quality;
		float				Darkness;
		float				NightMinDarkness;
	};

	struct InteriorsStruct {
		bool				Enabled;
		bool				TorchesCastShadows;
		FormsStruct			Forms;
		int					LightPoints;
		int					Quality;
		int					ShadowCubeMapSize;
		int					DrawDistance;
		float				Darkness;
		float				LightRadiusMult;
		float				LightClusterRadius;   // lamps closer than this share one shadow (ShaderManager::GetNearbyLights)
		bool				ForwardPointShadows;  // shadow each lamp's own light in the object shaders, not the finished frame
		float				FillLightRadius;      // lamps reaching farther are a level's fill lights (ShaderManager::GetNearbyLights)
		float				FillLightShadowStrength;
		float				NearFade;             // occluders this close to a lamp cast less of its shadow (Shaders/Includes/PointShadow.hlsl)
		bool				PlayerInsideLamp;     // a lamp inside the player leaves the player out of its cube (ShadowManager::RenderShadowCubeMap)
		float				ShadowSoftness;       // lamp shadow filter spread, in texels (Shaders/Includes/PointShadow.hlsl)
		float				ShadowNormalOffset;   // lamp shadow lookups pushed off the surface, in cube texels (Shaders/Includes/PointShadow.hlsl)
		bool				PCSS;                 // lamp shadows' edges widen with distance from what casts them (Shaders/Includes/PointShadow.hlsl)
		bool				RedrawActorsOnly;     // lamp shadow faces with something moving in them keep their still part and redraw only what moves (ShadowManager)
		float				MovingLampStep;       // a moving lamp's cube is redrawn only once it has moved this far from where it was drawn (game units)
		float				ActorShadowFullRate;  // lamps nearer the camera than this redraw their moving casters every frame (game units)
		int					ActorShadowSlowFrames; // farther ones every this many frames at most, staggered (1: every frame)
		float				ActorPoseThreshold;   // a skinned mesh's pose counts as changed once a bone has moved this far (game units)
		float				PCSSLightSize;        // the lamps' radius for it, in units
		float				PCSSMaxSpread;        // the widest edge, in shadow map texels
		int					PCSSFilter;           // 0 auto (smooth without TAA, dithered with it), 1 smooth, 2 dithered
		float				ContactStrength;      // lamps' screen-space contact shadows (with ForwardPointShadows), 0 off
		int					ContactLamps;         // lamps per pixel that cast them (1-2)
		float				ContactFade;          // how fast they fade along the shadow (0 not at all, 1 by the ray's end, 2 by its middle)
		float				ContactAmbient;       // how much of the ambient and cell light they also take (grounding)
		D3DXVECTOR4			ContactBend;          // their Bend settings (ContactSampleCount, ContactSurfaceThickness, ContactBilinearThreshold, ContactShadowContrast)
		float				ContactWeaponDepth;   // nearer is the first-person weapon
		bool				ContactWeaponOnWorld; // whether the weapon casts onto the world
		int					ContactDebug;         // debug view: 1 the strongest lamp's raw contact shadow, 2 where that lamp is on screen
		bool				UseCastShadowFlag;
		bool				PlayerShadowThirdPerson;
		bool				PlayerShadowFirstPerson;
	};

	struct ScreenSpaceStruct {
		bool				Enabled;
		float				BlurRadius;
		float				RenderDistance;
	};

	struct SunSmoothingStruct {
		bool				SmoothSun;
		bool				QuantizeSun;
		float				SmoothingFactor;
		float				YawStepSize;
		float				PitchStepSize;
		float				MaxJumpAngle;
	};

	struct SettingsShadowStruct {
		ShadowMapStruct     ShadowMaps;
		OrthoStruct			OrthoMap;
		ScreenSpaceStruct	ScreenSpace;
		ExteriorsStruct		Exteriors;
		InteriorsStruct		Interiors;
		SunSmoothingStruct  SunSmoothing;
	};
	SettingsShadowStruct	Settings;
	ShadowStruct			Constants;
	ShadowMapSettings		ShadowMaps[5];

	struct ShadowTextures {
		IDirect3DTexture9* ShadowPassTexture;
		IDirect3DSurface9* ShadowPassSurface;
		IDirect3DTexture9* ContactHistoryTexture;   // last frame's contact shadow (r) and depth (g), for the lighting
		IDirect3DSurface9* ContactHistorySurface;
		IDirect3DCubeTexture9* ShadowCubeMapTexture[ShadowSlotsMax];   // past ShadowCubeMapsMax made when first needed (EnsureShadowCube)
		IDirect3DSurface9* ShadowCubeMapSurface[ShadowSlotsMax][6];
		IDirect3DTexture9* ShadowSpotlightTexture[SpotLightsMax];
		IDirect3DSurface9* ShadowSpotlightSurface[SpotLightsMax];
		IDirect3DSurface9* ShadowCubeMapDepthSurface;
		// Every slot's six faces in one texture, for the object shaders (Shaders/Includes/PointShadow.hlsl):
		// 9 tiles by 8, slot s face f at tile s * 6 + f. ShadowManager::RenderShadowCubeMap copies each
		// face it draws.
		IDirect3DTexture9* PointShadowAtlasTexture;
		IDirect3DSurface9* PointShadowAtlasSurface;
		UInt32 PointShadowAtlasColumns;   // tiles per row (tile slot * 6 + face)
		UInt32 PointShadowAtlasRows;
		UInt32 PointShadowAtlasSlots;     // the slots it has room for: LightPoints at startup, at least 12
		UInt32 PointShadowAtlasWidth;     // in texels
		UInt32 PointShadowAtlasHeight;
	};
	bool EnsureShadowCube(UInt32 Slot);
	ShadowTextures	Textures;

	// Main shadow atlas, used for cascades.
	IDirect3DTexture9* ShadowAtlasTexture;
	IDirect3DSurface9* ShadowAtlasSurface;
	IDirect3DSurface9* ShadowAtlasSurfaceMSAA;
	IDirect3DSurface9* ShadowAtlasDepthSurface;

	// Ping-pong target for the separable prefilter blur, so that neither pass samples the atlas
	// while the atlas is bound as the render target -- undefined in D3D9, and a read/write
	// feedback loop on Vulkan under DXVK.
	// Initialised here because they are only allocated when Prefilter is on -- unlike the
	// members above, which RegisterTextures always assigns before anything tests them.
	IDirect3DTexture9* ShadowAtlasBlurTexture = nullptr;
	IDirect3DSurface9* ShadowAtlasBlurSurface = nullptr;

	IDirect3DVertexBuffer9* ShadowAtlasVertexBuffer;

	// Ortho shadows for various effects.
	IDirect3DTexture9* ShadowMapOrthoTexture;
	IDirect3DSurface9* ShadowMapOrthoSurface;
	IDirect3DSurface9* ShadowMapOrthoDepthSurface;

	void		clearShadowsBuffer();
	void		StoreContactHistory();   // after the contact pass: its result and the camera, for next frame's lighting
	// The camera the contact history was made with: last frame's, rotation-only view times unjittered projection.
	D3DXMATRIX	ContactHistoryViewProj;
	D3DXVECTOR3	ContactHistoryCamera = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	UInt32		ContactHistoryFrame = 0;   // frame it was stored in (0: none)
	UInt32		ContactFrame = 0;
	void		UpdateContactLight();      // right before the contact pass: the sun's light coordinate
	// Frames left before contact shadows return after a change of cell (the sun, the sky and the lamp
	// lists settle first), and the cell it was counted from.
	// After a transition: off for the first ContactOffFrames, then faded back in over ContactFadeFrames.
	static constexpr UInt32 ContactOffFrames = 10, ContactFadeFrames = 10;
	UInt32		ContactSettleFrames = 0;
	float		ContactWeaponDepthSetting = 0.0f;   // [Shaders.ShadowsExteriors.ScreenSpace] ContactWeaponDepth; applied in first person only
	float		ContactFade() const { return ContactSettleFrames > ContactFadeFrames ? 0.0f : 1.0f - (float)ContactSettleFrames / (float)(ContactFadeFrames + 1); }
	void*		ContactCell = nullptr;
	bool		ContactWasExterior = false;
	D3DXVECTOR3	ContactCamera = D3DXVECTOR3(0.0f, 0.0f, 0.0f);   // last frame's, for a jump
	UInt32		ContactFrameIndex = 0;   // ShaderManager::FrameIndex the contact state was last advanced in
	void		UpdateConstants();
	void		UpdateLightColors();
	void		UpdateSettings();
	void		RegisterConstants();
	void		RegisterTextures();
	void		RecreateTextures(bool cascades, bool ortho, bool cubemaps);

	D3DXVECTOR3	CalculateSmoothedSunDir();

	// Set by ShadowManager::RenderShadowMaps when it skipped the sun/moon cascades because the light
	// is below the horizon. The atlas and the camera-relative ShadowCameraToLight transforms are then
	// stale -- see UpdateConstants for why they must not be sampled.
	bool		SunMapsStale = false;

	void		GetCascadeDepths();
	D3DXMATRIX	GetCascadeViewProj(ShadowMapSettings* ShadowMap, D3DXVECTOR3* SunDir);

private:
	bool		texturesInitialized;

	bool		UpdateSettingsFromQuality(int quality);
};
