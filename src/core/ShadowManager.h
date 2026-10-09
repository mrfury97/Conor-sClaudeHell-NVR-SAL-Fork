#pragma once


class ShadowManager { // Never disposed
public:
	static void Initialize();
	
	enum ShadowMapTypeEnum {
		MapNear = 0,
		MapMiddle = 1,
		MapFar = 2,
		MapLod = 3,
		MapOrtho = 4,
	};


	NiNode*					GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms);
	void					AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes* arPlanes = nullptr);
	bool					IsRefracting(TESObjectREFR* Ref);
	void					AccumObject(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand);
	void					RenderAccums();
	void					RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj);
	void					AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap);
	void					RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex);
	void					ClearAccums();
	void					ConvertCubeFaces(UInt32 LightIndex, UInt32 Faces, float Radius);
	void					InvalidateCubeCache();
	void					RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex);
	void					RenderShadowMaps();
	void					ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor);
	void                    BlurShadowAtlas();

	ShadowRenderPass*				geometryPass;
	AlphaShadowRenderPass*			alphaPass;
	SkinnedGeoShadowRenderPass*		skinnedGeoPass;
	SpeedTreeShadowRenderPass*		speedTreePass;
	TerrainLODPass*					terrainLODPass;

	NiVector4				BillboardRight;
	NiVector4				BillboardUp;
	ShaderRecordVertex*		ShadowMapVertex;
	ShaderRecordPixel*		ShadowMapPixel;
	ShaderRecordVertex*		ShadowCubeMapVertex;
	ShaderRecordPixel*		ShadowCubeMapPixel;
	ShaderRecordVertex*		ShadowMapBlurVertex;
	ShaderRecordPixel*		ShadowMapBlurPixel;
	ShaderRecordPixel*		ShadowMapClearPixel;
	ShaderRecordPixel*		ShadowCubeToAtlasPixel;     // a cube face into the point shadow atlas, as an exponential shadow map
	IDirect3DVertexBuffer9*	AtlasTileVertexBuffer;      // a quad over one atlas tile, AtlasTileVertexSize texels on a side
	UInt32					AtlasTileVertexSize;
	D3DVIEWPORT9			ShadowCubeMapViewPort;
	ShaderRecordVertex*		CurrentVertex;
	ShaderRecordPixel*		CurrentPixel;
	bool					AlphaEnabled;
	// Reused by AccumChildren. That runs per reference, per cell, per cascade, and a fresh
	// std::stack allocated a deque block on every call; clear() keeps the capacity.
	std::vector<NiAVObject*>	containersScratch;
	int						PointLightsNum;
	float					shadowMapsRenderTime;
	bool					ShadowShadersLoaded;
	int						FrameCounter;

	// The point light cubemap of each slot, kept while nothing it sees changes (RenderShadowCubeMap).
	// A face is redrawn only when its light moved (past 6 units from its anchor) or changed, or the
	// geometry it draws -- the meshes, their transforms, skinned meshes' bone poses, and which of
	// them pass the filters -- hashes differently from when it was drawn.
	struct CubeCacheEntry {
		NiPointLight*			Light;
		IDirect3DCubeTexture9*	Texture;
		D3DXVECTOR3				Position;
		float					Radius;
		UInt32					Signature[6];
		bool					Valid[6];
	};
	CubeCacheEntry			CubeCache[ShadowSlotsMax];
	// Each slot's anchor (xyz, world) and radius, and its shadow's strength, this frame
	// (ShaderManager::GetNearbyLights, the anchor from RenderShadowCubeMap); slots under
	// ShadowCubeMapsMax are also in ShadowsExteriorEffect's ShadowLightPosition / ShadowLightFade.
	D3DXVECTOR4				SlotPosition[ShadowSlotsMax];
	float					SlotFadeValue[ShadowSlotsMax];
	int						SlotCount;          // the slots in use: LightPoints, capped

	// Each slot's lamps (ShaderManager::GetNearbyLights clusters lamps close together into one slot):
	// its cube draws what any of them lights.
	std::vector<ShadowSceneLight*>	SlotMembers[ShadowSlotsMax];
	// This frame's slot of every lamp in one (GetNearbyLights), for the object shaders' lamp shadows
	// (NewVegas/Hooks/Shaders.cpp, ForwardPointShadows).
	std::unordered_map<const ShadowSceneLight*, int>	LampSlot;

	// RenderShadowCubeMap: a light's casters, gathered and filtered once for its six faces.
	struct CubeCaster {
		NiGeometry*	Geo;
		NiBound		Bound;          // what the faces cull it by (a skinned mesh's from its posed bones)
		UInt32		PoseHash;       // a skinned mesh's bone transforms, hashed
		bool		HasBound;
		bool		AlwaysRedraw;   // a skin without bones to go by
	};
	std::vector<NiGeometry*>	GatheredScratch;
	std::vector<CubeCaster>		CastersScratch;
	// Develop.DebugMode log: summed over the frames between two log lines.
	UInt32					CubeFacesDrawn;
	UInt32					CubeFacesKept;
	UInt32					CubeResetLight, CubeResetMoved, CubeResetRadius, CubeResetTexture;   // why a whole cube was redrawn
	UInt32					CubeFacesDynamic, CubeFacesChanged, CubeFacesFirst;               // why a face was redrawn (dynamic: a skin without bones, or no geometry list)
	float					CubeMaxDrift, CubeMaxRadiusChange;
	char					CubeDynamicExample[64];
	UInt32					SlotAssigned, SlotEvicted, SlotDropOutranked, SlotDropGone, SlotDropExpired, SlotMaxGathered, SlotMaxClusters;   // ShaderManager::GetNearbyLights

private:
	bool					CheckShaderFlags(NiGeometry* Geometry);
	void					RecalculateBillboardVectors(D3DXVECTOR3* SunDir);
};