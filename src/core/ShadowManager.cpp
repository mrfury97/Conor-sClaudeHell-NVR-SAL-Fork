#define ShadowMapFarPlane 32768;

/*
* Initializes the Shadow Manager by grabbing the relevant settings and shaders, and setting up map sizes.
*/
void ShadowManager::Initialize() {
	
	Logger::Log("Starting the shadows manager...");
	TheShadowManager = new ShadowManager();

	// setup the shadow render passes for the shadowmaps
	TheShadowManager->geometryPass = new ShadowRenderPass();
	TheShadowManager->alphaPass = new AlphaShadowRenderPass();
	TheShadowManager->skinnedGeoPass = new SkinnedGeoShadowRenderPass();
	TheShadowManager->speedTreePass = new SpeedTreeShadowRenderPass();
	TheShadowManager->terrainLODPass = new TerrainLODPass();

	// load the shaders
	TheShadowManager->ShadowMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowMap.vso", "Shadows\\");
	TheShadowManager->ShadowMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowMap.pso", "Shadows\\");
	TheShadowManager->ShadowCubeMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowCubeMap.vso", "Shadows\\");
	TheShadowManager->ShadowCubeMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowCubeMap.pso", "Shadows\\");

    TheShadowManager->ShadowMapBlurVertex = (ShaderRecordVertex*) ShaderRecord::LoadShader("ShadowMapBlur.vso", "Shadows\\");
    TheShadowManager->ShadowMapBlurPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapBlur.pso", "Shadows\\");

	TheShadowManager->ShadowMapClearPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapClear.pso", "Shadows\\");
	TheShadowManager->ShadowCubeToAtlasPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowCubeToAtlas.pso", "Shadows\\");
	if (TheShadowManager->ShadowCubeToAtlasPixel) TheShadowManager->ShadowCubeToAtlasPixel->ClearSamplers = false;
	else Logger::Log("[ERROR]: Could not load ShadowCubeToAtlas.pso: lamps are shadowed in post-process.");

	// Make sure samplers are not reset on SetCT as that causes errors.
	TheShadowManager->ShadowMapVertex->ClearSamplers = false;
	TheShadowManager->ShadowMapPixel->ClearSamplers = false;
	TheShadowManager->ShadowCubeMapVertex->ClearSamplers = false;
	TheShadowManager->ShadowCubeMapPixel->ClearSamplers = false;
	TheShadowManager->ShadowMapBlurVertex->ClearSamplers = false;
	TheShadowManager->ShadowMapBlurPixel->ClearSamplers = false;
	TheShadowManager->ShadowMapClearPixel->ClearSamplers = false;

	TheShadowManager->ShadowShadersLoaded = true;
    if (TheShadowManager->ShadowMapVertex == nullptr || TheShadowManager->ShadowMapPixel == nullptr  || TheShadowManager->ShadowMapBlurVertex  == nullptr
        || TheShadowManager->ShadowCubeMapVertex == nullptr || TheShadowManager->ShadowCubeMapPixel == nullptr || TheShadowManager->ShadowMapBlurPixel  == nullptr ){
		TheShadowManager->ShadowShadersLoaded = false;
		Logger::Log("[ERROR]: Could not load one or more of the ShadowMap generation shaders. Reinstall the mod.");
    }

	UINT ShadowCubeMapSize = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.ShadowCubeMapSize;
	TheShadowManager->ShadowCubeMapViewPort = { 0, 0, ShadowCubeMapSize, ShadowCubeMapSize, 0.0f, 1.0f };

	TheShadowManager->shadowMapsRenderTime = 0;
	TheShadowManager->InvalidateCubeCache();
}

// Forget every cached point light cubemap: their textures were recreated, or their content lost.
void ShadowManager::InvalidateCubeCache() {
	memset(CubeCache, 0, sizeof(CubeCache));   // the static layers' state too
}

// Drop what the passes accumulated without drawing it.
void ShadowManager::ClearAccums() {
	RenderPass* Passes[] = { geometryPass, terrainLODPass, alphaPass, skinnedGeoPass, speedTreePass };
	for (RenderPass* Pass : Passes)
		while (!Pass->GeometryList.empty()) Pass->GeometryList.pop();
}

// Whether a bounding sphere, its centre relative to the light, reaches the 90 degree frustum of
// the cube face looking along Axis (a world axis): inside the four planes through the light at 45
// degrees to it, and not wholly behind the light.
static bool SphereInCubeFace(const D3DXVECTOR3& Centre, float Radius, const D3DXVECTOR3& Axis) {
	const float Along = D3DXVec3Dot(&Centre, &Axis);
	if (Along < -Radius) return false;
	// the other two world axes
	const D3DXVECTOR3 SideA = Axis.x != 0.0f ? D3DXVECTOR3(0.0f, 1.0f, 0.0f) : D3DXVECTOR3(1.0f, 0.0f, 0.0f);
	const D3DXVECTOR3 SideB = Axis.z != 0.0f ? D3DXVECTOR3(0.0f, 1.0f, 0.0f) : D3DXVECTOR3(0.0f, 0.0f, 1.0f);
	const float A = D3DXVec3Dot(&Centre, &SideA);
	const float B = D3DXVec3Dot(&Centre, &SideB);
	const float Reach = -Radius * 1.41421356f;   // the planes' normals are (Axis +- Side) / sqrt(2)
	return Along - A >= Reach && Along + A >= Reach && Along - B >= Reach && Along + B >= Reach;
}

static inline void HashMix(UInt32& Hash, UInt32 Value) {
	Hash = (Hash ^ Value) * 16777619u;   // FNV-1a, a word at a time
}

static inline void HashTransform(UInt32& Hash, const NiTransform& Transform) {
	const UInt32* Words = (const UInt32*)&Transform;   // rotation, position, scale
	for (int w = 0; w < (int)(sizeof(NiTransform) / sizeof(UInt32)); w++) HashMix(Hash, Words[w]);
}

// A skinned mesh's bound as posed now, and its pose hashed: each bone's vertex sphere (NiSkinData,
// in bone space) through the bone's world transform, merged; and each bone's world transform. The
// mesh's own world bound does not follow the pose, and its world transform does not change as it
// animates. False when the skin has no bones to go by.
static bool GetSkinnedBound(NiGeometry* Geo, NiBound* Bound, UInt32* PoseHash) {
	NiSkinInstance* Skin = Geo->skinInstance;
	if (!Skin || !Skin->BoneObjects || !Skin->SkinData || !Skin->SkinData->BoneData) return false;
	// NiSkinData's count: NiSkinInstance's word at 0x1C is the software skinning matrix count, 0 for
	// the hardware-skinned meshes the game draws (every actor's skin returned false here).
	const UInt32 Bones = Skin->SkinData->Bones;
	if (!Bones) return false;

	D3DXVECTOR3 Min(FLT_MAX, FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	UInt32 Hash = 2166136261u;
	for (UInt32 b = 0; b < Bones; b++) {
		const NiAVObject* Bone = Skin->BoneObjects[b];
		if (!Bone) continue;
		const NiTransform& World = Bone->m_worldTransform;
		HashTransform(Hash, World);
		const NiBound& Local = Skin->SkinData->BoneData[b].Bound;
		const NiPoint3 Rotated = World.rot * NiPoint3{ Local.Center.x * World.scale, Local.Center.y * World.scale, Local.Center.z * World.scale };
		const D3DXVECTOR3 Centre(Rotated.x + World.pos.x, Rotated.y + World.pos.y, Rotated.z + World.pos.z);
		const float Radius = max(Local.Radius * World.scale, 0.0f);
		const D3DXVECTOR3 Low(Centre.x - Radius, Centre.y - Radius, Centre.z - Radius), High(Centre.x + Radius, Centre.y + Radius, Centre.z + Radius);
		D3DXVec3Minimize(&Min, &Min, &Low);
		D3DXVec3Maximize(&Max, &Max, &High);
	}
	if (Min.x > Max.x) return false;
	const D3DXVECTOR3 Centre = (Min + Max) * 0.5f;
	const D3DXVECTOR3 HalfSize = (Max - Min) * 0.5f;
	Bound->Center = NiPoint3{ Centre.x, Centre.y, Centre.z };
	Bound->Radius = D3DXVec3Length(&HalfSize) + 8.0f;   // a little slack for the sphere fit
	*PoseHash = Hash;
	return true;
}


/*
* Returns the given object ref's NiNode if it passes the test for excluded form types, otherwise returns NULL.
*/
NiNode* ShadowManager::GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms) {
	
	if (!Ref) return NULL;
	if (Ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows) return NULL;

	// The form filter runs before GetNode() and the extra-data walk below, both of which are the
	// expensive part of this function. Called once per reference per cascade, so a reference the
	// filter rejects should cost only the flag test and the switch.
	TESForm* Form = Ref->baseForm;
	UInt8 TypeID = Form->formType;
	switch (TypeID) {
	case TESForm::FormType::kFormType_Land:
		return NULL; // land is handled separately
		break;
	case TESForm::FormType::kFormType_Activator:
		if (!Forms->Activators) return NULL; 
		break;
	case TESForm::FormType::kFormType_Apparatus:
		if (!Forms->Apparatus) return NULL;
		break;
	case TESForm::FormType::kFormType_Book:
		if (!Forms->Books) return NULL;
		break;
	case TESForm::FormType::kFormType_Container:
		if (!Forms->Containers) return NULL;
		break;
	case TESForm::FormType::kFormType_Door:
		if (!Forms->Doors) return NULL;
		break;
	case TESForm::FormType::kFormType_Misc:
		if (!Forms->Misc) return NULL;
		break;
	case TESForm::FormType::kFormType_Tree:
		if (!Forms->Trees) return NULL;
		break;
	case TESForm::FormType::kFormType_Furniture:
		if (!Forms->Furniture) return NULL;
		break;
	case TESForm::FormType::kFormType_NPC:
	case TESForm::FormType::kFormType_Creature:
	case TESForm::FormType::kFormType_LeveledCreature:
		if (!Forms->Actors) return NULL;  // D3D9 breaks on actors for some unknown reason
		break;
	case TESForm::FormType::kFormType_Stat:
	case TESForm::FormType::kFormType_StaticCollection:
	case TESForm::FormType::kFormType_MoveableStatic:
		//return NULL;
		if (!Forms->Statics) return NULL;
		break;
	default:
		break;
	}

	// GetNode() is a plain pointer read, so it stays here. The refraction test is NOT: it calls
	// into the engine to walk the reference's extra-data list. Callers apply IsRefracting()
	// after the cascade test instead, where only the references that survive it pay the cost --
	// measured at roughly 2 acceptances per 3400 references for the Near cascade.
	return Ref->GetNode();
}

// Split out of GetRefNode. Walks the extra-data list, so call it as late as possible.
bool ShadowManager::IsRefracting(TESObjectREFR* Ref) {
	ExtraRefractionProperty* RefractionExtraProperty = (ExtraRefractionProperty*)Ref->extraDataList.GetExtraData(BSExtraData::ExtraDataType::kExtraData_RefractionProperty);
	float Refraction = RefractionExtraProperty ? (1 - RefractionExtraProperty->refractionAmount) : 0.0f;
	return Refraction >= 0.5;
}


// Detech shader flags that we do not want.
bool ShadowManager::CheckShaderFlags(NiGeometry* Geometry) {
	BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));

	if (!shaderProp)
		return false;

	return !(shaderProp->GetFlag(BSSP_REFRACTION) ||
		shaderProp->GetFlag(BSSP_FIRE_REFRACTION) ||
		shaderProp->GetFlag(BSSP_DECAL) ||
		shaderProp->GetFlag(BSSP_DYNAMIC_DECAL));
}


// Detect which pass the object must be added to
void ShadowManager::AccumObject(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand) {
	NiGeometry* geo = static_cast<NiGeometry*>(NiObject);
	if (!geo->shader) return; // skip Geometry without a shader

	if (!CheckShaderFlags(geo))
		return;

#if defined(OBLIVION)
	if (geo->m_pcName && !memcmp(geo->m_pcName, "Torch", 5)) return; // No torch geo, it is too near the light and a bad square is rendered.
#endif

	if (skinnedGeoPass->AccumObject(geo)) {}
	else if (speedTreePass->AccumObject(geo)) {}
	else if (Forms->Lod && isLODLand && terrainLODPass->AccumObject(geo)) {}
	else if (Forms->AlphaEnabled && alphaPass->AccumObject(geo)) {}
	else geometryPass->AccumObject(geo);
}


// go through the Object children and sort the ones that will be rendered based on their properties
void ShadowManager::AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes *arPlanes) {
	if (!NiObject) return;

	std::vector<NiAVObject*>& containers = containersScratch;
	containers.clear();

	NiAVObject* child;
	NiAVObject* object;
	NiNode* Node;

	//list all objects contained, or sort the object if not a container
	if (!NiObject->IsGeometry())
		containers.push_back(NiObject);
	else
		AccumObject(NiObject, Forms, isLand && isLOD);
		

	// Gather geometry
	while (!containers.empty()) {
    	object = containers.back();
    	containers.pop_back();

		if (!object) continue;

		Node = object->IsNiNode();
    	if (!Node || Node->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling containers
		if (!isLand && Node->GetWorldBoundRadius() < Forms->MinRadius) continue;

		if (Node->IsKindOf<NiSwitchNode>()) {
			// NiSwitchNode - only render active children (if exists) to the shadow map.
			NiSwitchNode* SwitchNode = static_cast<NiSwitchNode*>(Node);
			if (SwitchNode->m_iIndex < 0)
				continue;

			child = Node->m_children.data[SwitchNode->m_iIndex];
			if (!child->IsGeometry())
				containers.push_back(child);
			else
				AccumObject(child, Forms, false);
			continue;
		}

		for (int i = 0; i < Node->m_children.end; i++) {
			child = Node->m_children.data[i];
			if (!child || child->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling children
			if (!isLand && child->GetWorldBoundRadius() < Forms->MinRadius) continue;

			// Frustum culling.
			if (arPlanes && (isLand || isLOD)) {
				BSMultiBoundNode* multibound = child->IsMultiBoundNode();

				if (multibound && !multibound->spMultiBound->spShape->WithinFrustum(*arPlanes)) continue;
			}
			else if (arPlanes && !child->WithinFrustum(arPlanes)) continue;

			if (child->IsFadeNode() && static_cast<BSFadeNode*>(child)->FadeAlpha < 0.75f) continue; // stop rendering fadenodes below a certain opacity
			if (!child->IsGeometry())
				containers.push_back(child);
			else
				AccumObject(child, Forms, isLand && isLOD);
		}
	}
}

// Go through accumulations and render found objects
void ShadowManager::RenderAccums() {
	geometryPass->RenderAccum();
	terrainLODPass->RenderAccum();
	alphaPass->RenderAccum();
	skinnedGeoPass->RenderAccum();
	speedTreePass->RenderAccum();
}


void ShadowManager::RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	ShadowMap->ShadowCameraToLight = (*ViewProj);
	TheCameraManager->SetFrustum(&ShadowMap->ShadowMapFrustum, ViewProj);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_DEPTHBIAS, (DWORD)0.0f, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, (DWORD)0.0f, RenderStateArgs);

	if (ShadowMap->Forms.Lod) {
		AccumChildren(BGSTerrainManager::GetRootLandLODNode(), &ShadowMap->Forms, true, true, &ShadowMap->ShadowMapFrustumPlanes);
		AccumChildren(BGSTerrainManager::GetRootObjectLODNode(), &ShadowMap->Forms, false, true, &ShadowMap->ShadowMapFrustumPlanes);
	}

	if (Player->GetWorldSpace()) {
		GridCellArray* CellArray = Tes->gridCellArray;
		UInt32 CellArraySize = CellArray->size * CellArray->size;

		for (UInt32 i = 0; i < CellArraySize; i++) {
			AccumExteriorCell(CellArray->GetCell(i), ShadowMap);
		}
	}
	else {
		AccumExteriorCell(Player->parentCell, ShadowMap);
	}

	Device->SetViewport(&ShadowMap->ShadowMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	if (ShadowMap->CustomClearRequired)
		ClearShadowCascade(&ShadowMap->ShadowMapViewPort, &ShadowMap->ClearColor);

	RenderAccums();
}


void ShadowManager::AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap) {
	if (!Cell || Cell->IsInterior())
		return;

	if (ShadowMap->Forms.Terrain)
		AccumChildren(Cell->GetChildNode(TESObjectCELL::kCellNode_Land), &ShadowMap->Forms, true, false, &ShadowMap->ShadowMapFrustumPlanes);

	TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &ShadowMap->Forms);

		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		if (RefNode->WithinFrustum(&ShadowMap->ShadowMapFrustumPlanes) && !IsRefracting(Entry->item))
			AccumChildren(RefNode, &ShadowMap->Forms, false, false, &ShadowMap->ShadowMapFrustumPlanes);

		Entry = Entry->next;
	}
}


void ShadowManager::RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex) {
	NiSpotLight* pNiLight = Lights[LightIndex];
	if (pNiLight == NULL || !pNiLight->CastShadows) return;

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	D3DXMATRIX View, Proj;

	NiPoint3* LightPos = &pNiLight->m_worldTransform.pos;
	float Radius = pNiLight->Spec.r;

#if defined(OBLIVION)
	if (pNiLight->CanCarry)
		Radius = 256.0f;
#endif

	D3DXVECTOR3 Up = D3DXVECTOR3(0, 0, 1);
	D3DXVECTOR3 Eye = LightPos->toD3DXVEC3();
	Eye.x -= TheRenderManager->CameraPosition.x;
	Eye.y -= TheRenderManager->CameraPosition.y;
	Eye.z -= TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(pNiLight->OuterSpotAngle * 2), 1.0f, 0.1f, Radius);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, 0, RenderStateArgs);

	D3DXVECTOR3 CameraDirection = D3DXVECTOR3(pNiLight->m_worldTransform.rot.data[0][0], pNiLight->m_worldTransform.rot.data[1][0], pNiLight->m_worldTransform.rot.data[2][0]);
	D3DXVECTOR3 At = Eye + CameraDirection;

	TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms);
		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		// Detect if the object is in front of the light in the direction of the current face
		// TODO: improve to base on frustum
		D3DXVECTOR3 ObjectPos = RefNode->m_worldTransform.pos.toD3DXVEC3();
		D3DXVECTOR3 ObjectToLight = ObjectPos - LightPos->toD3DXVEC3();

		D3DXVec3Normalize(&ObjectToLight, &ObjectToLight);
		bool inFront = D3DXVec3Dot(&ObjectToLight, &CameraDirection) > 0;
		if (inFront && RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius()) 
			AccumChildren(RefNode, &Settings->Forms, false, false);

		Entry = Entry->next;
	}

	D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
	Shadows->Constants.ShadowViewProj = View * Proj;
	TheShaderManager->SpotLightWorldToLightMatrix[LightIndex] = (Shadows->Constants.ShadowViewProj);

	Device->SetRenderTarget(0, Shadows->Textures.ShadowSpotlightSurface[LightIndex]);
	Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

	Device->SetViewport(&ShadowCubeMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	RenderAccums();
}


// True when Object hangs anywhere under Root in the scene graph.
static bool IsUnderNode(const NiAVObject* Object, const NiNode* Root) {
	if (!Root) return false;
	for (const NiAVObject* Node = Object; Node; Node = Node->m_parent)
		if (Node == Root) return true;
	return false;
}

void ShadowManager::RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex) {
	if (Lights[LightIndex] == NULL) return; // No light at current index

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	float Radius = 0.0f;
	NiPoint3* LightPos = NULL;
	D3DXMATRIX View, Proj;
	D3DXVECTOR3 Eye, At, Up, CameraDirection;

	// The slot holds a cluster of lamps (ShaderManager::GetNearbyLights): Lights[LightIndex] is its
	// key lamp, SlotMembers its lamps, and ShadowLightPosition its centre and radius.
	if (!Shadows->EnsureShadowCube(LightIndex)) return;
	NiPointLight* pNiLight = Lights[LightIndex]->sourceLight;
	D3DXVECTOR4* SlotPosition = &this->SlotPosition[LightIndex];
	Radius = SlotPosition->w;
	if (pNiLight->CanCarry) {
		// Carried lamps draw with a 256 unit radius; the shaders now compare against the same one
		// (they were given the lamp's own radius, so the depths written and compared disagreed).
		Radius = 256.0f;
		SlotPosition->w = Radius;
	}

	// This slot's cached faces hold another light, or this one from another place or radius, or a
	// texture since recreated: none of them can be kept. A light's place is its anchor: where its
	// cube was drawn from, kept while the light stays within 6 units of it. Flickering lamps wander
	// a few units around a point; at half a unit of tolerance they redrew their whole cube every
	// frame for nothing. The cube is drawn from the anchor, and the shaders get the anchor too
	// (ShadowLightPosition), so depth written and depth compared come from the same point.
	CubeCacheEntry* Cache = &CubeCache[LightIndex];
	IDirect3DCubeTexture9* CubeTexture = Shadows->Textures.ShadowCubeMapTexture[LightIndex];
	const D3DXVECTOR3 Position(SlotPosition->x, SlotPosition->y, SlotPosition->z);
	const D3DXVECTOR3 Drift = Position - Cache->Position;
	// A lamp that moves (carried, on the player, walking about: ShaderManager::GetNearbyLights) is
	// followed within half a unit, not six: its shadows would lag behind it in steps.
	const float DriftLimit = SlotMobile[LightIndex] ? 0.25f : 36.0f;
	if (Cache->Light != pNiLight || Cache->Texture != CubeTexture || D3DXVec3LengthSq(&Drift) > DriftLimit || fabsf(Cache->Radius - Radius) > 0.5f) {
		if (Cache->Light != pNiLight) CubeResetLight++;
		else if (Cache->Texture != CubeTexture) CubeResetTexture++;
		else if (D3DXVec3LengthSq(&Drift) > DriftLimit) {
			if (SlotMobile[LightIndex]) CubeResetMobile++;
			else { CubeResetMoved++; CubeMaxDrift = max(CubeMaxDrift, D3DXVec3Length(&Drift)); }
		}
		else { CubeResetRadius++; CubeMaxRadiusChange = max(CubeMaxRadiusChange, fabsf(Cache->Radius - Radius)); }
		memset(Cache->Valid, 0, sizeof(Cache->Valid));
		Cache->Light = pNiLight;
		Cache->Texture = CubeTexture;
		Cache->Position = Position;
		Cache->Radius = Radius;
	}
	NiPoint3 Anchor = { Cache->Position.x, Cache->Position.y, Cache->Position.z };
	LightPos = &Anchor;
	SlotPosition->x = Anchor.x;
	SlotPosition->y = Anchor.y;
	SlotPosition->z = Anchor.z;
	if (LightIndex < ShadowCubeMapsMax) Shadows->Constants.ShadowLightPosition[LightIndex] = *SlotPosition;   // the effects' copy

	Eye.x = LightPos->x - TheRenderManager->CameraPosition.x;
	Eye.y = LightPos->y - TheRenderManager->CameraPosition.y;
	Eye.z = LightPos->z - TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(90.0f), 1.0f, 0.1f, Radius);

	// The casters, gathered and filtered once for the six faces: every mesh any lamp of the cluster
	// lights (the game's per-lamp lists, merged, in pointer order so the face hashes do not depend
	// on list order), with the bound each face culls it by. Each face used to walk the list and run
	// the filters (including two walks up the scene graph per mesh) itself.
	std::vector<NiGeometry*>& Gathered = GatheredScratch;
	Gathered.clear();
	for (ShadowSceneLight* Member : SlotMembers[LightIndex]) {
		for (auto iter = Member ? Member->kGeometryList.start : nullptr; iter; iter = iter->next)
			if (iter->data) Gathered.push_back(iter->data);
	}
	std::sort(Gathered.begin(), Gathered.end());
	Gathered.erase(std::unique(Gathered.begin(), Gathered.end()), Gathered.end());
	const bool UseGeometryLists = !Gathered.empty();

	// A lamp of the cluster whose light point is inside the player, or within 16 units of the body
	// (PlayerInsideLamp): the player is left out of this cube. A point inside a model is blocked on
	// every side, so walking through a lamp without a fixture threw the player's shadow onto every
	// wall. The body as an upright cylinder from the feet: 32 units across (by the player's scale),
	// as tall as the player's bound reaches (at most 200), as NVR Unofficial Optimized has it.
	bool LampInsidePlayer = false;
	if (Settings->PlayerInsideLamp && Player) {
		const float Scale = Player->scale > 0.0f ? Player->scale : 1.0f;
		const float Margin = 16.0f;
		const NiPoint3& Feet = Player->pos;
		float Top = Feet.z + 128.0f * Scale;
		if (NiNode* Node = Player->GetNode())
			if (Node->m_kWorldBound) Top = min(Node->m_kWorldBound->Center.z + Node->m_kWorldBound->Radius, Feet.z + 200.0f * Scale);
		const float Reach = 32.0f * Scale + Margin;
		float Nearest = FLT_MAX, NearestAbove = 0.0f, NearestAcross = 0.0f;
		for (ShadowSceneLight* Member : SlotMembers[LightIndex]) {
			if (!Member || !Member->sourceLight) continue;
			const NiPoint3& Lamp = Member->sourceLight->m_worldTransform.pos;
			const float dx = Lamp.x - Feet.x, dy = Lamp.y - Feet.y;
			const float Across = sqrtf(dx * dx + dy * dy);
			if (Across < Reach && Lamp.z > Feet.z - Margin && Lamp.z < Top + Margin) {
				LampInsidePlayer = true;
				break;
			}
			// Develop.DebugMode: how far the nearest lamp is from the body, for the log below.
			const float Gap = max(Across - 32.0f * Scale, 0.0f) + max(Lamp.z - Top, 0.0f) + max(Feet.z - Lamp.z, 0.0f);
			if (Gap < Nearest) { Nearest = Gap; NearestAbove = Lamp.z - Top; NearestAcross = Across; }
		}
		static UInt32 LogFrames = 0;
		if (TheSettingManager->SettingsMain.Develop.DebugMode && Nearest < 96.0f && ++LogFrames >= 120) {
			LogFrames = 0;
			Logger::Log("ShadowManager: slot %u lamp %.0f units from the player's body (%.0f above the head, %.0f across from the feet): not inside, the player casts its shadow",
				LightIndex, Nearest, NearestAbove, NearestAcross);
		}
	}

	std::vector<CubeCaster>& Casters = CastersScratch;
	Casters.clear();
	for (NiGeometry* geo : Gathered) {
		if (geo->m_flags & NiAVObject::APP_CULLED)
			continue;

		BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(geo->GetProperty(NiProperty::kType_Shade));
		NiMaterialProperty* matProp = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));

		if (!shaderProp)
			continue;

		// Skip refraction and fire refraction.
		if (!CheckShaderFlags(geo))
			continue;

		// The flags FlagPlayerGeometry sets are refreshed only every 50 frames, so meshes attached to
		// the player since (a drawn weapon, equipment, a first-person body mod's parts) went unflagged
		// and cast the player's shadow in first person: an arm or weapon next to a lamp at eye height
		// threw a shadow over the room that swung with the camera. Where the mesh hangs decides it.
		bool isFirstPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kFirstPerson) || IsUnderNode(geo, Player->firstPersonNiNode);
		bool isThirdPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kThirdPerson) || IsUnderNode(geo, Player->GetNode());

		// Skip objects if they are barely visible.
		if ((matProp && matProp->fAlpha < 0.05f))
			continue;

		// Also skip viewmodel due to issues, and render player's model only in 3rd person
		if (isFirstPerson) continue;

		if (!Player->isThirdPerson && !Settings->PlayerShadowFirstPerson && isThirdPerson)
			continue;

		if (Player->isThirdPerson && !Settings->PlayerShadowThirdPerson && isThirdPerson)
			continue;

		if (LampInsidePlayer && isThirdPerson)
			continue;

		// A skinned mesh goes by its bones (GetSkinnedBound): its own bound does not follow the
		// pose, and its pose joins the hash, so an actor redraws only the faces it is in while it
		// moves, and a still one (a corpse) is kept like any other mesh. A skin without bones to go
		// by is drawn in every face, every frame.
		CubeCaster Caster = {};
		Caster.Geo = geo;
		if (geo->skinInstance) {
			if (GetSkinnedBound(geo, &Caster.Bound, &Caster.PoseHash))
				Caster.HasBound = true;
			else {
				Caster.AlwaysRedraw = true;
				if (!CubeDynamicExample[0])
					strncpy_s(CubeDynamicExample, geo->m_pcName ? geo->m_pcName : "(unnamed)", _TRUNCATE);
			}
		}
		else if (geo->m_kWorldBound) {
			Caster.Bound = *geo->m_kWorldBound;
			Caster.HasBound = true;
		}
		// Moving: its transform or pose changed within the last 30 frames (RedrawActorsOnly). Updated
		// once a frame, however many lamps it is near.
		CasterMotion& Mo = Motion[geo];
		if (Mo.Frame != MotionFrame) {
			UInt32 Hash = 2166136261u;
			HashTransform(Hash, geo->m_worldTransform);
			HashMix(Hash, Caster.PoseHash);
			if (Hash == Mo.Hash) Mo.StillFrames++;
			else { Mo.Hash = Hash; Mo.StillFrames = 0; }
			Mo.Frame = MotionFrame;
		}
		Caster.Moving = Caster.AlwaysRedraw || Mo.StillFrames < 30;
		Casters.push_back(Caster);
	}

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	UInt32 DrawnFaces = 0;   // into the atlas after the loop (ConvertCubeFaces)
	for (int Face = 0; Face < 6; Face++) {
		At = Eye;
		switch (Face) {
		case D3DCUBEMAP_FACE_POSITIVE_X:
			CameraDirection = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		case D3DCUBEMAP_FACE_NEGATIVE_X:
			CameraDirection = D3DXVECTOR3(-1.0f, 0.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		case D3DCUBEMAP_FACE_POSITIVE_Y:
			CameraDirection = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
			break;
		case D3DCUBEMAP_FACE_NEGATIVE_Y:
			CameraDirection = D3DXVECTOR3(0.0f, -1.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
			break;
		case D3DCUBEMAP_FACE_POSITIVE_Z:
			CameraDirection = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		case D3DCUBEMAP_FACE_NEGATIVE_Z:
			CameraDirection = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		}
		At += CameraDirection;

		// What this face draws, hashed as it is gathered; a skin without bones or the form-based
		// fallback below make it redraw regardless.
		UInt32 Signature = 2166136261u;
		bool Dynamic = false;
		HashMix(Signature, Settings->Forms.AlphaEnabled ? 1u : 0u);

		// The face's casters, still and moving apart (RedrawActorsOnly), and the still ones' own hash.
		UInt32 StaticSignature = Signature;
		std::vector<const CubeCaster*>& Still = FaceStaticScratch;
		std::vector<const CubeCaster*>& Moving = FaceMovingScratch;
		Still.clear();
		Moving.clear();
		if (UseGeometryLists) {
			for (const CubeCaster& Caster : Casters) {
				// Only what this face can see. Each face used to draw the light's whole list.
				if (Caster.HasBound) {
					const D3DXVECTOR3 Centre(Caster.Bound.Center.x - LightPos->x, Caster.Bound.Center.y - LightPos->y, Caster.Bound.Center.z - LightPos->z);
					if (!SphereInCubeFace(Centre, Caster.Bound.Radius, CameraDirection)) continue;
				}
				Dynamic |= Caster.AlwaysRedraw;
				NiGeometry* geo = Caster.Geo;
				HashMix(Signature, (UInt32)geo);
				HashTransform(Signature, geo->m_worldTransform);
				HashMix(Signature, Caster.PoseHash);
				if (Caster.Moving) Moving.push_back(&Caster);
				else {
					Still.push_back(&Caster);
					HashMix(StaticSignature, (UInt32)geo);
					HashTransform(StaticSignature, geo->m_worldTransform);
				}
			}
		}
		else {
			// old form based geo accumulation when the one perform by the game has not handled this light
			Dynamic = true;
			TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
			while (Entry) {
				if (NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms)) {
					if (RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius() && !IsRefracting(Entry->item))
						AccumChildren(RefNode, &Settings->Forms, false, false);
				}
				Entry = Entry->next;
			}
		}


		// Nothing this face sees changed since it was drawn: keep it. (A face last drawn only into the
		// atlas is not, once the post-process shadows read the cube instead.)
		const bool ForwardShadows = Shadows->Constants.PointShadowData.x > 0.0f && LightIndex < Shadows->Textures.PointShadowAtlasSlots;
		if (!Dynamic && Cache->Valid[Face] && Cache->Signature[Face] == Signature && (ForwardShadows || !Cache->AtlasOnly[Face])) {
			ClearAccums();   // the form based fallback accumulates as it goes
			CubeFacesKept++;
			continue;
		}
		if (!Cache->Valid[Face]) CubeFacesFirst++;
		else if (Dynamic) CubeFacesDynamic++;
		else CubeFacesChanged++;
		Cache->Valid[Face] = true;
		Cache->Signature[Face] = Signature;
		CubeFacesDrawn++;

		D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
		Shadows->Constants.ShadowViewProj = View * Proj;

		// Something moving in it (RedrawActorsOnly, lamps shadowed in the object shaders): the still
		// casters from their static layer (drawn again only when they change), copied into the face's
		// atlas tile, and the moving ones drawn over it, the nearest kept. The live cube face is then
		// not drawn (the atlas is what is read).
		const bool Split = UseGeometryLists && !Moving.empty() && Settings->RedrawActorsOnly && ForwardShadows
			&& ShadowCubeToAtlasPixel && ShadowMapBlurVertex && SplitAvailable() && EnsureStaticCube(LightIndex);
		Cache->AtlasOnly[Face] = Split;
		if (Split) {
			if (!Cache->StaticValid[Face] || Cache->StaticSignature[Face] != StaticSignature) {
				AccumCasters(Still, Settings->Forms.AlphaEnabled);
				Device->SetRenderTarget(0, StaticCubeSurface[LightIndex][Face]);
				Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);
				Device->SetViewport(&ShadowCubeMapViewPort);
				Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);
				RenderAccums();
				Cache->StaticValid[Face] = true;
				Cache->StaticSignature[Face] = StaticSignature;
				CubeStaticDrawn++;
			}
			ConvertCubeFaces(LightIndex, 1u << Face, Radius, StaticCube[LightIndex]);   // render target: the atlas
			const UINT Size = ShadowCubeMapViewPort.Width, Columns = Shadows->Textures.PointShadowAtlasColumns, Tile = LightIndex * 6 + Face;
			const D3DVIEWPORT9 TileViewport = { (Tile % Columns) * Size, (Tile / Columns) * Size, Size, Size, 0.0f, 1.0f };
			Device->SetViewport(&TileViewport);
			AccumCasters(Moving, Settings->Forms.AlphaEnabled);
			BeginMinBlend();
			RenderAccums();
			EndMinBlend();
			CubeFacesSplit++;
			continue;
		}

		if (UseGeometryLists) {
			AccumCasters(Still, Settings->Forms.AlphaEnabled);
			AccumCasters(Moving, Settings->Forms.AlphaEnabled);
		}
		Device->SetRenderTarget(0, Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face]);
		Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

		Device->SetViewport(&ShadowCubeMapViewPort);
		Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

		RenderAccums();
		DrawnFaces |= 1u << Face;
	}
	ConvertCubeFaces(LightIndex, DrawnFaces, Radius);
}

// --- RedrawActorsOnly helpers ---------------------------------------------------------------------

// Min blending into the R32F atlas, asked of the device once.
bool ShadowManager::SplitAvailable() {
	if (SplitSupport == 0) {
		SplitSupport = -1;
		IDirect3DDevice9* Device = TheRenderManager->device;
		IDirect3D9* D3D = nullptr;
		D3DDEVICE_CREATION_PARAMETERS Creation = {};
		D3DDISPLAYMODE Mode = {};
		if (SUCCEEDED(Device->GetDirect3D(&D3D)) && D3D) {
			if (SUCCEEDED(Device->GetCreationParameters(&Creation)) && SUCCEEDED(Device->GetDisplayMode(0, &Mode)) &&
				D3D->CheckDeviceFormat(Creation.AdapterOrdinal, Creation.DeviceType, Mode.Format, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, D3DFMT_R32F) == D3D_OK)
				SplitSupport = 1;
			D3D->Release();
		}
		Logger::Log(SplitSupport > 0 ? "ShadowManager: RedrawActorsOnly available (min blending into R32F)"
			: "ShadowManager: RedrawActorsOnly unavailable: this GPU cannot min-blend into R32F; shadows with something moving are redrawn whole");
	}
	return SplitSupport > 0;
}

// A slot's static layer: a cube like its own, made the first time the slot has something moving.
bool ShadowManager::EnsureStaticCube(UInt32 Slot) {
	if (Slot >= ShadowSlotsMax) return false;
	if (StaticCube[Slot]) return true;
	const UINT Size = ShadowCubeMapViewPort.Width;
	if (FAILED(TheRenderManager->device->CreateCubeTexture(Size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &StaticCube[Slot], NULL))) {
		StaticCube[Slot] = nullptr;
		return false;
	}
	for (int Face = 0; Face < 6; Face++) StaticCube[Slot]->GetCubeMapSurface((D3DCUBEMAP_FACES)Face, 0, &StaticCubeSurface[Slot][Face]);
	memset(CubeCache[Slot].StaticValid, 0, sizeof(CubeCache[Slot].StaticValid));
	return true;
}

// The passes take the casters as the cube faces always have (skinned, trees, alpha tested, the rest).
void ShadowManager::AccumCasters(const std::vector<const CubeCaster*>& List, bool Alpha) {
	for (const CubeCaster* Caster : List) {
		NiGeometry* geo = Caster->Geo;
		if (skinnedGeoPass->AccumObject(geo)) {}
		else if (speedTreePass->AccumObject(geo)) {}
		else if (Alpha && alphaPass->AccumObject(geo)) {}
		else geometryPass->AccumObject(geo);
	}
}

// Drawing over a copied layer: no depth, each texel keeping the nearest distance (min blending).
void ShadowManager::BeginMinBlend() {
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	TheRenderManager->device->SetDepthStencilSurface(NULL);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MIN, RenderStateArgs);
}

void ShadowManager::EndMinBlend() {
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	RenderState->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, TRUE, RenderStateArgs);
}

// The faces just drawn into the atlas the object shaders read (Shaders/Includes/PointShadow.hlsl):
// tile slot * 6 + face, PointShadowAtlasColumns to a row, the stored distances copied as they are
// (Shaders/Shadows/ShadowCubeToAtlas.pso; the object shaders filter them). Only redrawn faces: a
// kept face keeps its tile.
void ShadowManager::ConvertCubeFaces(UInt32 LightIndex, UInt32 Faces, float Radius, IDirect3DCubeTexture9* Source) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	if (!Faces || !Shadows->Textures.PointShadowAtlasSurface || !ShadowCubeToAtlasPixel || !ShadowMapBlurVertex) return;
	if (LightIndex >= Shadows->Textures.PointShadowAtlasSlots) return;   // no room in the atlas (not shadowed by the object shaders)
	const UINT Columns = Shadows->Textures.PointShadowAtlasColumns;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	const UINT Size = ShadowCubeMapViewPort.Width;
	if (!AtlasTileVertexBuffer || AtlasTileVertexSize != Size) {
		if (AtlasTileVertexBuffer) AtlasTileVertexBuffer->Release();
		AtlasTileVertexBuffer = nullptr;
		TheShaderManager->CreateFrameVertex(Size, Size, &AtlasTileVertexBuffer);
		AtlasTileVertexSize = Size;
		if (!AtlasTileVertexBuffer) return;
	}

	Device->SetRenderTarget(0, Shadows->Textures.PointShadowAtlasSurface);
	Device->SetDepthStencilSurface(NULL);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	RenderState->SetVertexShader(ShadowMapBlurVertex->ShaderHandle, false);   // passes the quad through
	RenderState->SetPixelShader(ShadowCubeToAtlasPixel->ShaderHandle, false);
	RenderState->SetFVF(FrameFVF, false);
	Device->SetStreamSource(0, AtlasTileVertexBuffer, 0, sizeof(FrameVS));
	RenderState->SetTexture(0, Source ? Source : Shadows->Textures.ShadowCubeMapTexture[LightIndex]);   // a static layer (RedrawActorsOnly), or the live cube
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP, false);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP, false);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP, false);
	RenderState->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT, false);
	RenderState->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT, false);
	RenderState->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE, false);

	for (UInt32 Face = 0; Face < 6; Face++) {
		if (!(Faces & (1u << Face))) continue;
		const UINT Tile = LightIndex * 6 + Face;
		const D3DVIEWPORT9 Viewport = { (Tile % Columns) * Size, (Tile / Columns) * Size, Size, Size, 0.0f, 1.0f };
		Device->SetViewport(&Viewport);
		const float Data[4] = { (float)Face, 0.0f, 0.0f, 0.0f };
		Device->SetPixelShaderConstantF(0, Data, 1);
		Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	}

	RenderState->SetTexture(0, nullptr);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, TRUE, RenderStateArgs);
}


static void FlagShaderPropertyRecurse(NiAVObject* apObject, UInt32 auiFlags, bool abSet) {
	if (!apObject)
		return;

	if (apObject->IsGeometry()) {
		NiGeometry* pGeometry = static_cast<NiGeometry*>(apObject);
		NiShadeProperty* shaderProperty = static_cast<NiShadeProperty*>(pGeometry->GetProperty(NiProperty::kType_Shade));
		if (shaderProperty) {
			if (abSet)
				shaderProperty->m_usFlags.Set(auiFlags);
			else
				shaderProperty->m_usFlags.Clear(auiFlags);
		}

	}
	else if (apObject->IsNiNode()) {
		NiNode* pNiNode = static_cast<NiNode*>(apObject);
		for (UInt32 i = 0; i < pNiNode->m_children.end; i++) {
			FlagShaderPropertyRecurse(pNiNode->m_children.data[i], auiFlags, abSet);
		}
	}
}

static SInt32 frames = -1;
static void FlagPlayerGeometry() {
	frames++;

	// Run this function every 50 frames, or on launch
	if (frames > 50 || frames == -1) {
		if (Player->firstPersonNiNode)
			FlagShaderPropertyRecurse(Player->firstPersonNiNode, NiShadeProperty::kFirstPerson, true);

		NiNode* node = Player->GetNode();
		if (node)
			FlagShaderPropertyRecurse(node, NiShadeProperty::kThirdPerson, true);

		frames = 0;
	}
}

void ShadowManager::RecalculateBillboardVectors(D3DXVECTOR3* SunDir) {
	D3DXVECTOR3 WorldUp = D3DXVECTOR3(0.0f, 0.0f, 1.0f);

	// Calculate BillboardRight as perpendicular to SunDir and WorldUp
	D3DXVECTOR3 BillboardRightVec;
	D3DXVec3Cross(&BillboardRightVec, &WorldUp, SunDir);

	// Handle case where sun is directly above/below
	if (D3DXVec3LengthSq(&BillboardRightVec) < 0.0001f) {
		BillboardRightVec = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
	}
	D3DXVec3Normalize(&BillboardRightVec, &BillboardRightVec);

	// Calculate BillboardUp perpendicular to both SunDir and BillboardRightVec
	D3DXVECTOR3 BillboardUpVec;
	D3DXVec3Cross(&BillboardUpVec, SunDir, &BillboardRightVec);
	D3DXVec3Normalize(&BillboardUpVec, &BillboardUpVec);

	// Set shader constants
	BillboardRight = NiVector4(BillboardRightVec.x, BillboardRightVec.y, BillboardRightVec.z, 0.0f);
	BillboardUp = NiVector4(BillboardUpVec.x, BillboardUpVec.y, BillboardUpVec.z, 0.0f);
}

/*
* Renders the different shadow maps: Near, Far, Ortho.
*/
void ShadowManager::RenderShadowMaps() {
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // cancel out if rendering effects is disabled

	// track point lights for interiors and exteriors
	ShadowSceneLight* ShadowLights[ShadowSlotsMax] = { NULL };
	NiPointLight* Lights[TrackedLightsMax] = { NULL };
	NiSpotLight* SpotLights[SpotLightsMax] = { NULL };

	TheShaderManager->GetNearbyLights(ShadowLights, Lights, SpotLights);

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::ExteriorsStruct* ShadowsExteriors = &Shadows->Settings.Exteriors;
	ShadowsExteriorEffect::InteriorsStruct* ShadowsInteriors = &Shadows->Settings.Interiors;

	bool isExterior = TheShaderManager->GameState.isExterior;// || currentCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior; // exterior flag currently broken
	bool ExteriorEnabled = isExterior && TheShaderManager->Effects.ShadowsExteriors->Enabled && ShadowsExteriors->Enabled;
	bool InteriorEnabled = !isExterior && TheShaderManager->Effects.ShadowsInteriors->Enabled;

	// early out in case shadow rendering is not required
	if (!ExteriorEnabled && !InteriorEnabled && !TheShaderManager->orthoRequired || !ShadowShadersLoaded) {
		return;
	}
	if (!Player->parentCell) return;

	auto timer = TimeLogger();

	// prepare some pointers to the device and surfaces
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* DepthSurface = NULL;
	IDirect3DSurface9* RenderSurface = NULL;
	D3DVIEWPORT9 viewport;

	D3DXVECTOR4* ShadowData = &TheShaderManager->Effects.ShadowsExteriors->Constants.Data;
	D3DXVECTOR4* OrthoData = &TheShaderManager->Effects.ShadowsExteriors->Constants.OrthoData;
	Device->GetDepthStencilSurface(&DepthSurface);
	Device->GetRenderTarget(0, &RenderSurface);
	Device->GetViewport(&viewport);	

	DWORD zfunc;
	Device->GetRenderState(D3DRS_ZFUNC, &zfunc); // backup in case of inverted depth

	DWORD oldStencilEnable, oldStencilRef, oldStencilFunc;
	DWORD oldAlphaRef, oldNormalizeNormals, oldPointSize;

	Device->GetRenderState(D3DRS_STENCILENABLE, &oldStencilEnable);
	Device->GetRenderState(D3DRS_STENCILREF, &oldStencilRef);
	Device->GetRenderState(D3DRS_STENCILFUNC, &oldStencilFunc);
	Device->GetRenderState(D3DRS_ALPHAREF, &oldAlphaRef);
	Device->GetRenderState(D3DRS_NORMALIZENORMALS, &oldNormalizeNormals);
	Device->GetRenderState(D3DRS_POINTSIZE, &oldPointSize);

	if (1.0 - NiDX9Renderer::GetSingleton()->m_fZClear) // inverted depth
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL, RenderStateArgs);
	else
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_STENCILENABLE, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_NORMALIZENORMALS, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_POINTSIZE, 810365505, RenderStateArgs);

	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();
	
	D3DXVECTOR4 PlayerPosition = Player->pos.toD3DXVEC4();
	TESObjectCELL* currentCell = Player->parentCell;

	// Flag player geometry so we can control if it should be rendered in shadow cubemaps
	FlagPlayerGeometry();

	// Render directional shadows for Sun/Moon
	NiNode* PlayerNode = Player->GetNode();
	D3DXVECTOR3 At;
	At.x = PlayerNode->m_worldTransform.pos.x - TheRenderManager->CameraPosition.x;
	At.y = PlayerNode->m_worldTransform.pos.y - TheRenderManager->CameraPosition.y;
	At.z = PlayerNode->m_worldTransform.pos.z - TheRenderManager->CameraPosition.z;

	// Render all shadow maps
	Device->BeginScene();

	// Quantize sun direction angle to reduce shimmer by a large factor.
	D3DXVECTOR3 SunDir = Shadows->CalculateSmoothedSunDir();

	if (isExterior && (ExteriorEnabled || TheShaderManager->orthoRequired)) {

		// Update cascade depths based on current camera.
		Shadows->GetCascadeDepths();

		geometryPass->VertexShader = ShadowMapVertex;
		geometryPass->PixelShader = ShadowMapPixel;
		alphaPass->VertexShader = ShadowMapVertex;
		alphaPass->PixelShader = ShadowMapPixel;
		skinnedGeoPass->VertexShader = ShadowMapVertex;
		skinnedGeoPass->PixelShader = ShadowMapPixel;
		speedTreePass->VertexShader = ShadowMapVertex;
		speedTreePass->PixelShader = ShadowMapPixel;
		terrainLODPass->VertexShader = ShadowMapVertex;
		terrainLODPass->PixelShader = ShadowMapPixel;

		// Below the horizon the sun/moon cascades are not drawn, which leaves the atlas and its
		// camera-relative transforms stale -- tell ShadowsExterior so nothing samples them (see
		// ShadowsExteriorEffect::UpdateConstants). Cleared here as well as there because the game
		// shaders for this frame draw before UpdateConstants runs again.
		Shadows->SunMapsStale = ExteriorEnabled && SunDir.z <= 0.0f;
		if (Shadows->SunMapsStale) {
			Shadows->Constants.ShadowFade.x = 1.0f;
			Shadows->Constants.ShadowFade.y = 0.0f;
		}

		if (ExteriorEnabled && SunDir.z > 0.0f) {
			// Recalculate billboard vectors for speedtree leaves shader.
			RecalculateBillboardVectors(&SunDir);

			ShadowData->z = 0; // set shader constant to identify other shadow maps
			auto shadowMapTimer = TimeLogger();

			if (Shadows->ShadowAtlasSurfaceMSAA)
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurfaceMSAA);
			else
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurface);

			Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);

			for (int i = MapNear; i < MapOrtho; i++) {
				ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[i];

				if (!Shadows->Settings.ShadowMaps.LimitFrequency || i != MapLod || !(FrameCounter % 4)) {
					Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &SunDir);
					RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj);
				}
				else {
					// We need to update the shadowprojmatrix of MapLod by the camera translation between frames to avoid jumps in the shadows.
					D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
					D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
					D3DXMATRIX translationMatrix;
					D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
					ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
					ShadowMap->CameraTranslation = newCameraTranslation;
					
					Shadows->Constants.ShadowBlur.y = Shadows->ShadowAtlasSurfaceMSAA ? 1.0f : 0.0f; // Disable blur for last cascade if MSAA is off.
				}

				std::string message = "ShadowManager::RenderShadowMap ";
				message += std::to_string(i);
				shadowMapTimer.LogTime(message.c_str());
			}

			// Resolve MSAA.
			if (Shadows->ShadowAtlasSurfaceMSAA)
				Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, NULL, Shadows->ShadowAtlasSurface, NULL, D3DTEXF_NONE);

			if (Shadows->Settings.ShadowMaps.Prefilter) BlurShadowAtlas();

			if (Shadows->Settings.ShadowMaps.Mipmaps)
				Shadows->ShadowAtlasTexture->GenerateMipSubLevels();
		}

		// render ortho map if one of the effects using ortho is active
		if (TheShaderManager->orthoRequired) {
			auto shadowMapTimer = TimeLogger();

			ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[MapOrtho];

			if (!Shadows->Settings.OrthoMap.LimitFrequency || !((FrameCounter + 2) % 4)) {
				Device->SetRenderTarget(0, Shadows->ShadowMapOrthoSurface);
				Device->SetDepthStencilSurface(Shadows->ShadowMapOrthoDepthSurface);

				ShadowData->z = 1; // identify ortho map in shader constant
				D3DXVECTOR3 OrthoDir = D3DXVECTOR3(0.05f, 0.05f, 1.0f);
				Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &OrthoDir);

				RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj);
			}
			else {
				D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
				D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
				D3DXMATRIX translationMatrix;
				D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
				ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
				ShadowMap->CameraTranslation = newCameraTranslation;
			}

			OrthoData->x = Shadows->Settings.OrthoMap.Distance * 2;
			OrthoData->y = ShadowMap->ShadowMapInverseResolution;
	
			shadowMapTimer.LogTime("ShadowManager::RenderShadowMap Ortho");
		}
	}

	// Render shadow maps for point lights
	bool usePointLights = (TheShaderManager->GameState.isDayTime > 0.5) ? ShadowsExteriors->UsePointShadowsDay : ShadowsExteriors->UsePointShadowsNight;

	AlphaEnabled = ShadowsInteriors->Forms.AlphaEnabled;
	geometryPass->VertexShader = ShadowCubeMapVertex;
	geometryPass->PixelShader = ShadowCubeMapPixel;
	alphaPass->VertexShader = ShadowCubeMapVertex;
	alphaPass->PixelShader = ShadowCubeMapPixel;
	skinnedGeoPass->VertexShader = ShadowCubeMapVertex;
	skinnedGeoPass->PixelShader = ShadowCubeMapPixel;
	speedTreePass->VertexShader = ShadowCubeMapVertex;
	speedTreePass->PixelShader = ShadowCubeMapPixel;

	auto shadowMapTimer = TimeLogger();
	MotionFrame++;   // RedrawActorsOnly: casters' motion is updated once a frame
	if (Motion.size() > 20000) Motion.clear();   // freed geometry left behind
	if ((isExterior && usePointLights) || (!isExterior && InteriorEnabled)) {
		// render the cubemaps for each light
		for (int i = 0; i < SlotCount; i++) {

			// Render targets set in function due to rendering multiple faces.
			RenderShadowCubeMap(ShadowLights, i);

			std::string message = "ShadowManager::RenderShadowCubeMap ";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}

		// Bounce lighting's probes, lit through the lamps' shadow maps just drawn (interiors).
		TheShaderManager->Shaders.BounceLighting->Capture();
		shadowMapTimer.LogTime("BounceLighting::Capture");

		// Develop.DebugMode: how much the cubemap cache saves, every 600 frames.
		static UInt32 LogFrames = 0;
		if (TheSettingManager->SettingsMain.Develop.DebugMode && ++LogFrames >= 600) {
			Logger::Log("ShadowManager: point light cubemaps over %u frames: %u faces drawn, %u kept. Whole cubes reset: %u new light, %u moved (max %.2f), %u following a moving lamp, %u radius (max %.2f), %u texture. Faces redrawn: %u first draw, %u skinned geometry (e.g. %s), %u content changed",
				LogFrames, CubeFacesDrawn, CubeFacesKept, CubeResetLight, CubeResetMoved, CubeMaxDrift, CubeResetMobile, CubeResetRadius, CubeMaxRadiusChange, CubeResetTexture,
				CubeFacesFirst, CubeFacesDynamic, CubeDynamicExample[0] ? CubeDynamicExample : "-", CubeFacesChanged);
			Logger::Log("ShadowManager: shadow slots over %u frames: %u clusters given a slot (%u taking a held one), dropped: %u outranked, %u gone, %u inactive past grace; up to %u shadow-casting lamps gathered, in %u clusters",
				LogFrames, SlotAssigned, SlotEvicted, SlotDropOutranked, SlotDropGone, SlotDropExpired, SlotMaxGathered, SlotMaxClusters);
			Logger::Log("ShadowManager: RedrawActorsOnly over %u frames: %u cube faces redrawn as their moving casters over a kept static layer (%u static layers drawn)",
				LogFrames, CubeFacesSplit, CubeStaticDrawn);
			CubeFacesSplit = CubeStaticDrawn = 0;
			SlotAssigned = SlotEvicted = SlotDropOutranked = SlotDropGone = SlotDropExpired = SlotMaxGathered = SlotMaxClusters = 0;
			LogFrames = 0;
			CubeFacesDrawn = CubeFacesKept = 0;
			CubeResetLight = CubeResetMoved = CubeResetMobile = CubeResetRadius = CubeResetTexture = 0;
			CubeFacesDynamic = CubeFacesChanged = CubeFacesFirst = 0;
			CubeMaxDrift = CubeMaxRadiusChange = 0.0f;
			CubeDynamicExample[0] = 0;
		}
	}

	if (TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive && TheShaderManager->Effects.Flashlight->Settings.renderShadows) {
		// render shadow maps for spotlights
		
		for (int i = 0; i < SpotLightsMax; i++) {
			if (!SpotLights[i] || SpotLights[i]->Spec.r == 0) continue; //bypass lights with no radius

			// Render targets set in function.
			RenderShadowSpotlight(SpotLights, i);

			std::string message = "ShadowManager::RenderShadowSpotLight";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
	}

	// reset renderer to previous state
	Device->SetDepthStencilSurface(DepthSurface);
	Device->SetRenderTarget(0, RenderSurface);
	Device->SetViewport(&viewport);
	Device->SetRenderState(D3DRS_ZFUNC, zfunc);
	Device->SetRenderState(D3DRS_STENCILENABLE, oldStencilEnable);
	Device->SetRenderState(D3DRS_STENCILREF, oldStencilRef);
	Device->SetRenderState(D3DRS_STENCILFUNC, oldStencilFunc);
	Device->SetRenderState(D3DRS_ALPHAREF, oldAlphaRef);
	Device->SetRenderState(D3DRS_NORMALIZENORMALS, oldNormalizeNormals);
	Device->SetRenderState(D3DRS_POINTSIZE, oldPointSize);

	//release smart pointers to prevent memory leak
	if (DepthSurface) DepthSurface->Release();
	if (RenderSurface) RenderSurface->Release();

	if (TheSettingManager->SettingsMain.Develop.DebugMode && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Console)) {
		if (Global->OnKeyDown(0x17)) { // TODO: setting for debug key ?
			char Filename[MAX_PATH];

			time_t CurrentTime = time(NULL);
			GetCurrentDirectoryA(MAX_PATH, Filename);
			strcat(Filename, "\\Test");
			if (GetFileAttributesA(Filename) == INVALID_FILE_ATTRIBUTES) CreateDirectoryA(Filename, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmapatlas.jpg", D3DXIFF_JPG, Shadows->ShadowAtlasSurface, NULL, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmaportho.jpg", D3DXIFF_JPG, Shadows->ShadowMapOrthoSurface, NULL, NULL);

			InterfaceManager->ShowMessage("Textures taken!");
		}
	}

	Device->EndScene();

	FrameCounter = (FrameCounter + 1) % 4;
	shadowMapsRenderTime = timer.LogTime("ShadowManager::RenderShadowMaps");
}


/*
 * Clear a part of the shadow map atlas based on the shadow mapping mode.
 * 
 * Note: Render target, view port should be set beforehand. Scene has to be already being rendered.
 */
void ShadowManager::ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* TargetShadowMap = Shadows->ShadowAtlasSurface;

	Device->SetDepthStencilSurface(NULL);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetPixelShader(ShadowMapClearPixel->ShaderHandle, false);

	Device->SetPixelShaderConstantF(0, (const float*) ClearColor, 1);

	// Draw a full-screen quad (inside the viewport)
	struct VERTEX { float x, y, z, rhw; };
	VERTEX vertices[] = {
		{ (float)ViewPort->X - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)ViewPort->X - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f }
	};

	RenderState->SetFVF(D3DFVF_XYZRHW, false);

	Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VERTEX));

	Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}


/*
* Filters the Shadow Map of given index using a 2 pass gaussian blur
*/
void ShadowManager::BlurShadowAtlas() {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	
	IDirect3DDevice9* Device = TheRenderManager->device;
    NiDX9RenderState* RenderState = TheRenderManager->renderState;

	// Ping-pong: horizontal pass atlas -> scratch, vertical pass scratch -> atlas.
	//
	// Neither pass may sample the atlas while ShadowAtlasSurface -- level 0 of that same
	// texture -- is the bound render target. Reading a bound render target is undefined in
	// D3D9 and a read/write feedback loop on Vulkan under DXVK. This Gaussian is the only
	// filtering the shadow maps get (the cascade lookup is a single tap), so anything that
	// compromises it shows up directly as hard, unfiltered texels along every shadow edge.
	if (!Shadows->ShadowAtlasBlurTexture || !Shadows->ShadowAtlasBlurSurface) return;

    Device->SetDepthStencilSurface(NULL);
    RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetVertexShader(ShadowMapBlurVertex->ShaderHandle, false);
    RenderState->SetPixelShader(ShadowMapBlurPixel->ShaderHandle, false);
	RenderState->SetFVF(FrameFVF, false);
	Device->SetStreamSource(0, Shadows->ShadowAtlasVertexBuffer, 0, sizeof(FrameVS));

	// Pass map resolution to shader as a constant
	ShadowMapBlurPixel->SetShaderConstantF(0, &Shadows->Constants.ShadowBlur, 1);

	// blur in two passes, horizontally then vertically
	D3DXVECTOR4 Blur[2] = {
		D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f),
		D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f),
	};
	IDirect3DTexture9* Source[2] = { Shadows->ShadowAtlasTexture, Shadows->ShadowAtlasBlurTexture };
	IDirect3DSurface9* Target[2] = { Shadows->ShadowAtlasBlurSurface, Shadows->ShadowAtlasSurface };

	for (int i = 0; i < 2; i++) {
		// Unbind the previous pass's target before it becomes this pass's source, so the
		// two are never bound as texture and render target at the same time.
		RenderState->SetTexture(0, nullptr);
		Device->SetRenderTarget(0, Target[i]);
		RenderState->SetTexture(0, Source[i]);

		// set blur direction shader constants
		ShadowMapBlurPixel->SetShaderConstantF(1, &Blur[i], 1);

		Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2); // draw call to execute the shader
	}

	RenderState->SetTexture(0, nullptr);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}

