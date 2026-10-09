#pragma once

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
		D3DXVECTOR4		SunLight;			// rgb: the sun colour the object shaders receive
		D3DXVECTOR4		AmbientLight;		// rgb: the ambient colour the object shaders receive
		D3DXVECTOR4		ContactDebug;		// x: debug view of the contact shadows, 0 off
		D3DXVECTOR4		OrthoData;
		D3DXVECTOR4		ShadowFade;
		D3DXMATRIXA16	ShadowWorld;
		D3DXMATRIX		ShadowViewProj;
		D3DXMATRIX		ShadowSpotlightCameraToLight[SpotLightsMax];
		D3DXVECTOR4		ShadowCubeMapLightPosition;
		D3DXVECTOR4		ShadowLightPosition[ShadowCubeMapsMax];
		D3DXVECTOR4		ShadowLightFade[ShadowCubeMapsMax / 4];   // slot i's shadow strength in component i % 4 of [i / 4]: 0 none, 1 full (fading as clusters trade slots)
		D3DXVECTOR4		PointShadowData;   // Shaders/Includes/PointShadow.hlsl: x 1 when lamps are shadowed in the object shaders, y 1 / atlas width, z 1 / atlas height, w a face's size
		D3DXVECTOR4		PointShadowParams; // Shaders/Includes/PointShadow.hlsl: x NearFade, y the atlas's tiles per row
		D3DXVECTOR4		ShadowMapRadius;
		D3DXVECTOR4		ShadowBlur;
		// Forward sun shadows, runtime side. x: 1 when the forward path is SUPPRESSED.
		//
		// The polarity is deliberate. A constant that fails to reach a shader reads as zero,
		// and zero here means "not suppressed" -- i.e. the current, working behaviour. The
		// opposite polarity would turn a dropped constant into silently missing shadows,
		// which is exactly the failure that made this a compile-time macro in the first place.
		D3DXVECTOR4		ForwardData;
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
		float				ShadowSoftness;       // lamp shadow blur, in cube texels (ShadowManager::ConvertCubeFaces)
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
