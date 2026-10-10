#pragma once
#define FrameFVF D3DFVF_XYZ | D3DFVF_TEX1

#include "../Effects/Animator.h"
#include "ShaderRecord.h"
#include "EffectRecord.h"
#include "ShaderCollection.h"
#include "../Effects/Effects.h"

struct ShaderConstants {
	
	struct OcclusionMapStruct {
		D3DXMATRIX		OcclusionWorldViewProj;
	};

	D3DXMATRIXA16			ShadowWorld;
	D3DXVECTOR4				ReciprocalResolution;
	D3DXVECTOR4				SunDir;
	D3DXVECTOR4				SunPosition;
	D3DXVECTOR4				SunTiming;
	D3DXVECTOR4				SunAmount;
	D3DXVECTOR4				ViewSpaceLightDir;
	D3DXVECTOR4				ScreenSpaceLightDir;
	D3DXVECTOR4				GameTime;
	D3DXVECTOR4				FrameTime;
	TESWeather*				pWeather;
	float					sunGlare;
	float					windSpeed;
	D3DXVECTOR4				fogColor;
	D3DXVECTOR4				horizonColor;
	D3DXVECTOR4				skyLowColor;
	float					skyObjectID;
	D3DXVECTOR4				sunDiskColor;
	D3DXVECTOR4				sunColor;
	D3DXVECTOR4				sunAmbient;
	D3DXVECTOR4				skyColor;
	D3DXVECTOR4				fogData;
	D3DXVECTOR4				fogDistance;
	float					fogStart;
	float					fogEnd;
	float					fogPower;
	OcclusionMapStruct		OcclusionMap;
};


typedef std::map<std::string, EffectRecord**> EffectsList;
typedef std::map<std::string, ShaderCollection**> ShaderList;
typedef std::map<std::string, D3DXVECTOR4> CustomConstants;

struct		FrameVS { float x, y, z, u, v; };

__declspec(align(16)) class ShaderManager : public ShaderManagerBase { // Never disposed
public:
	static void Initialize();

	void* operator new(size_t i) { return _mm_malloc(i, 16); }

	template <typename T> void RegisterEffect(T** Pointer);
	template <typename T> void RegisterShaderCollection(T** Pointer);
	EffectRecord*			GetEffectByName(const char* Name);
	ShaderCollection*		GetShaderCollectionByName(const char* Name);
	void					ClearShaderSamplers(const char* TextureName, size_t Length);
	void					RegisterConstant(const char* Name, D3DXVECTOR4* FloatValue);
	void					CreateFrameVertex(UInt32 Width, UInt32 Height, IDirect3DVertexBuffer9** FrameVertex);
	void					InitializeConstants();
	void					UpdateConstants();
	void					GetNearbyLights(ShadowSceneLight* ShadowLightsList[], NiPointLight* LightsList[], NiSpotLight* SpotLightList[]);
	std::vector<ShadowSceneLight*>	RankedLightsScratch;   // GetNearbyLights: shadow casters by distance, capacity kept
	std::vector<ShadowSceneLight*>	PresentLightsScratch;  // GetNearbyLights: every scene light, any state
	// GetNearbyLights: lamps close together share one shadow-casting slot.
	struct ShadowLampCluster {
		NiPointLight*		Key;        // its widest lamp: names the cluster from frame to frame
		ShadowSceneLight*	KeyScene;
		D3DXVECTOR3			Centre;     // the lamps' mean
		float				Radius;     // reaching every lamp's sphere from the centre
		D3DXVECTOR3			Colour;     // the active lamps' colour x dimmer, summed
		int					Rank;       // >= 0 when one of its lamps is active, -1 when none is
		float				Score;      // its best active lamp's distance from the player over the lamp's reach: lower ranks first
		bool				Fill;       // its key lamp reaches past FillLightRadius: a fill light
		bool				Mobile;     // a lamp that moves, alone in its cluster
		UInt32				First, Count;   // its lamps in ClusterLampsScratch
	};
	std::vector<char>				ClusterMobileScratch;
	std::unordered_map<NiPointLight*, D3DXVECTOR3>	LampFirstSeen;   // where each lamp was first seen (mobile lamps)
	std::vector<ShadowSceneLight*>	EligibleLightsScratch;
	std::vector<ShadowSceneLight*>	ClusterLampsScratch;
	std::vector<ShadowLampCluster>	ClustersScratch;
	std::vector<int>				ClusterOrderScratch;
	std::vector<char>				ClusterTakenScratch;
	std::vector<NiPointLight*>		ShadowedLampsScratch;
	bool					LoadShader(NiD3DVertexShader* VertexShader);
	bool					LoadShader(NiD3DPixelShader* PixelShader);
	void					ReloadEffects();
	ShaderCollection*		GetShaderCollection(const char* Name);
	float					GetTransitionValue(float Day, float Night, float Interior);
	bool					ShouldRenderShadowMaps();
	void					RenderEffects(IDirect3DSurface9* RenderTarget);
	void					RenderEffectsPreTonemapping(IDirect3DSurface9* RenderTarget);
	void					RenderEffectToRT(IDirect3DSurface9* RenderTarget, EffectRecord* Effect, bool clearRenderTarget);
	void					SwitchShaderStatus(const char* Name);
	void					SetCustomConstant(const char* Name, D3DXVECTOR4 Value);
		
	struct	EffectsStruct {
		AmbientOcclusionEffect*	AmbientOcclusion;
		SkinScatteringEffect*	SkinScattering;
		DynamicCubemapsEffect*	DynamicCubemaps;
		AvgLumaEffect*			AvgLuma;
		BloodLensEffect*		BloodLens;
		BloomEffect*			Bloom;
		BloomLegacyEffect*		BloomLegacy;
		ColoringEffect*			Coloring;
		LUTEffect*				LUT;
		CinemaEffect*			Cinema;
		ExposureEffect*			Exposure;
		AutoExposureEffect*		AutoExposure;
		FlashlightEffect*		Flashlight;
		FlashlightBeamEffect*	FlashlightBeam;
		CombineDepthEffect*		CombineDepth;
		DepthOfFieldEffect*		DepthOfField;
		DebugEffect*			Debug;
		GodRaysEffect*			GodRays;
		ImageAdjustEffect*		ImageAdjust;
		LensEffect*				Lens;
		LowHFEffect*			LowHF;
		MotionBlurEffect*		MotionBlur;
		NormalsEffect*			Normals;
		RainEffect*				Rain;
		SharpeningEffect*		Sharpening;
		SnowEffect*				Snow;
		SnowAccumulationEffect*	SnowAccumulation;
		ShadowsExteriorEffect*	ShadowsExteriors;
		ShadowsInteriorsEffect*	ShadowsInteriors;
		PointShadowsEffect*		PointShadows;
		PointShadows2Effect*	PointShadows2;
		SunShadowsEffect*		SunShadows;
		UnderwaterEffect*		Underwater;
		VolumetricLightEffect*	VolumetricLight;
		VolumetricFogEffect*	VolumetricFog;
		WaterLensEffect*		WaterLens;
		WetWorldEffect*			WetWorld;
		DitherBusterEffect*		DitherBuster;
		SMAAEffect*				SMAA;
		CinematicDOFEffect*		CinematicDOF;
		TAAEffect*				TAA;
	};

	struct ShadersStruct{
		WaterShaders*			Water;
		TonemappingShaders*		Tonemapping;
		POMShaders*				POM;
		PBRShaders*				PBR;
		ShaderCollection*		Blood;
		SkyShaders*				Sky;
		SkinShaders*			Skin;
		GrassShaders*			Grass;
		TerrainShaders*			Terrain;
		InverseSquareLightingShaders*	InverseSquareLighting;
	};

	struct GameStateStruct {
		float					isDayTime;
		bool					isDayTimeChanged;
		float					transitionCurve;
		float					dayLight;
		bool					isExterior;
		bool					isUnderwater;
		bool					isDialog;
		bool					isPersuasion;
		bool					isCellChanged;
		bool					VATSIsOn;
		bool					PipBoyIsOn;
		bool					OverlayIsOn;
		bool					isRainy;
		bool					isSnow;
		bool					isCloudy;
	};

	EffectsStruct			Effects;
	ShadersStruct			Shaders;
	EffectsList				EffectsNames;
	ShaderList				ShaderNames;
	GameStateStruct			GameState;
	ShaderConstants			ShaderConst;
	CustomConstants			CustomConst;
	std::map<std::string, D3DXVECTOR4*>	ConstantsTable;
	IDirect3DVertexBuffer9*	FrameVertex;
	NiD3DVertexShader*		WaterVertexShaders[51];
	NiD3DPixelShader*		WaterPixelShaders[51];
    TESObjectCELL*          PreviousCell;
    bool                    IsMenuSwitch;
    bool                    orthoRequired;
    bool                    avglumaRequired;
	bool					EffectReloadQueued;
	D3DXVECTOR4				SpotLightPosition[SpotLightsMax];
	D3DXVECTOR4				SpotLightColor[SpotLightsMax];
	D3DXVECTOR4				SpotLightDirection[SpotLightsMax];
	D3DXVECTOR4				VolumetricData;		// xyz march light position, w first person strength floor
	D3DXMATRIX				SpotLightWorldToLightMatrix[SpotLightsMax];
	D3DXVECTOR4				LightPosition[TrackedLightsMax];
	D3DXVECTOR4				LightColor[TrackedLightsMax + ShadowCubeMapsMax];
	D3DXVECTOR4				LightAttenuation[TrackedLightsMax];
	// The nearest active point lights, shadow casters or not, for their contact shadows indoors
	// (Effects/SunShadows.fx, technique 1): xyz world position, w radius; rgb colour x dimmer, w the
	// lamp's shadow info (slot * 2 + fade, -1 none, as Shaders/Includes/PointShadow.hlsl); its slot's
	// anchor, w the slot's radius (0 none).
	D3DXVECTOR4				ContactLampPosition[ContactLampsMax];
	D3DXVECTOR4				ContactLampColor[ContactLampsMax];
	D3DXVECTOR4				ContactLampAnchor[ContactLampsMax];
};

