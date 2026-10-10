#include "BounceLighting.h"

// --- the probes' pass ----------------------------------------------------------------------------

class ProbeCapturePass : public RenderPass {
public:
	bool AccumObject(NiGeometry* Geo);
	void UpdateConstants(NiGeometry* Geo);
	void RenderGeometry(NiGeometry* Geo);   // both faces, whatever the mesh says
};


// A render state set on the device itself as well as through the game's cache (NiDX9RenderState).
// The cache skips a state it believes already set, and NVR's shadow code puts the depth test back on
// the device directly: the cache said LESSEQUAL while the device was GREATEREQUAL, so the probes'
// LESSEQUAL never reached it, and with their depth cleared to 1 not one mesh passed the test.
static void ForceRenderState(D3DRENDERSTATETYPE State, DWORD Value) {
	TheRenderManager->renderState->SetRenderState(State, Value, RenderStateArgs);
	TheRenderManager->device->SetRenderState(State, Value);
}

// Whether a bounding sphere, its centre relative to the probe, reaches the 90 degree frustum of the
// cube face looking along Axis (as ShadowManager's).
static bool ProbeSphereInFace(const D3DXVECTOR3& Centre, float Radius, const D3DXVECTOR3& Axis) {
	const float Along = D3DXVec3Dot(&Centre, &Axis);
	if (Along < -Radius) return false;
	const D3DXVECTOR3 SideA = Axis.x != 0.0f ? D3DXVECTOR3(0.0f, 1.0f, 0.0f) : D3DXVECTOR3(1.0f, 0.0f, 0.0f);
	const D3DXVECTOR3 SideB = Axis.z != 0.0f ? D3DXVECTOR3(0.0f, 1.0f, 0.0f) : D3DXVECTOR3(0.0f, 0.0f, 1.0f);
	const float A = D3DXVec3Dot(&Centre, &SideA);
	const float B = D3DXVec3Dot(&Centre, &SideB);
	const float Reach = -Radius * 1.41421356f;
	return Along - A >= Reach && Along + A >= Reach && Along - B >= Reach && Along + B >= Reach;
}

void BounceLightingShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_ProbeGrid", &Constants.Grid);
	TheShaderManager->RegisterConstant("TESR_ProbeGridSize", &Constants.GridSize);
	TheShaderManager->RegisterConstant("TESR_ProbeGridScale", &Constants.GridScale);
	TheShaderManager->RegisterConstant("TESR_ProbeLighting", &Constants.Lighting);
	TheShaderManager->RegisterConstant("TESR_ProbeCapture", &Constants.Capture);
	TheShaderManager->RegisterConstant("TESR_ProbeReduce", &Constants.Reduce);
	TheShaderManager->RegisterConstant("TESR_ProbeCaptureDebug", &Constants.CaptureDebug);
	TheShaderManager->RegisterConstant("TESR_ProbeLampPosition", LampPosition);
	TheShaderManager->RegisterConstant("TESR_ProbeLampColor", LampColor);
	TheShaderManager->RegisterConstant("TESR_ProbeLampAnchor", LampAnchor);

	// The atlases exist from the start, on or off: the object shaders bind them by name, and look
	// again (and log) on every draw until they find them. Probe p's texel: (z * GridX + x, y).
	TheTextureManager->InitTexture("TESR_ProbeAtlasA", &AtlasA, &AtlasASurface, GridX * GridZ, GridY, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_ProbeAtlasB", &AtlasB, &AtlasBSurface, GridX * GridZ, GridY, D3DFMT_A16B16G16R16F);
	Constants.GridSize = D3DXVECTOR4((float)GridX, (float)GridY, (float)GridZ, (float)(GridX * GridZ));
	CapturedFrame.assign(ProbeCount, 0);
	ProbeSignature.assign(ProbeCount, 0);
	ProbeCaptures.assign(ProbeCount, 0);
}

void BounceLightingShaders::UpdateSettings() {
	const char* Section = "Shaders.BounceLighting.Main";
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF(Section, "Strength"), 0.0f, 1.0f);
	Settings.AmbientFloor = std::clamp(TheSettingManager->GetSettingF(Section, "AmbientFloor"), 0.0f, 1.0f);
	Settings.Intensity = std::clamp(TheSettingManager->GetSettingF(Section, "Intensity"), 0.0f, 8.0f);
	Settings.Bounces = std::clamp(TheSettingManager->GetSettingF(Section, "Bounces"), 0.0f, 1.0f);
	Settings.Spacing = std::clamp(TheSettingManager->GetSettingF(Section, "Spacing"), 32.0f, 1024.0f);
	Settings.Range = std::clamp(TheSettingManager->GetSettingF(Section, "Range"), 128.0f, 4096.0f);
	Settings.ProbesPerFrame = std::clamp(TheSettingManager->GetSettingI(Section, "ProbesPerFrame"), 1, 64);
	Settings.Debug = std::clamp(TheSettingManager->GetSettingI(Section, "Debug"), 0, 4);
	if (Settings.Spacing != FittedSpacing) GridCell = nullptr;   // refit (only for this: it starts the probes over)
}

// Run every frame, on or off (ShaderManager::UpdateConstants): off, outdoors or before any probe is
// captured the object shaders keep the flat ambient.
void BounceLightingShaders::UpdateConstants() {
	FrameCounter++;
	const D3DXVECTOR4& Camera = TheRenderManager->CameraPosition;
	Constants.Grid = D3DXVECTOR4(GridOrigin.x - Camera.x, GridOrigin.y - Camera.y, GridOrigin.z - Camera.z, 0.0f);
	Constants.GridScale = D3DXVECTOR4(1.0f / GridSpacing.x, 1.0f / GridSpacing.y, 1.0f / GridSpacing.z, 0.0f);
	const bool On = Enabled && Ready && !TheShaderManager->GameState.isExterior && Player && GridCell && Player->parentCell == GridCell && CapturedAny > 0;
	Constants.Lighting = D3DXVECTOR4(On ? Settings.Strength : 0.0f, Settings.AmbientFloor, (float)(Settings.Debug == 4 ? 1 : Settings.Debug), Settings.Intensity);
	Constants.CaptureDebug.x = Settings.Debug == 4 ? 1.0f : 0.0f;
	const PBRShaders* PBR = TheShaderManager->Shaders.PBR;
	Constants.Capture.x = PBR->Constants.ExtraData.w;   // LinearLighting
	Constants.Capture.y = PBR->Constants.Data.z;        // the lights' scale (LightingScale)
	Constants.Capture.z = Settings.Bounces * Settings.Intensity;
}

bool BounceLightingShaders::EnsureResources() {
	if (Ready) return true;
	if (Failed || !AtlasASurface || !AtlasBSurface) return false;
	IDirect3DDevice9* Device = TheRenderManager->device;
	CaptureVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ProbeCapture.vso", "Shadows\\");
	CapturePixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ProbeCapture.pso", "Shadows\\");
	ReducePixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ProbeReduce.pso", "Shadows\\");
	if (!CaptureVertex || !CapturePixel || !ReducePixel || !TheShadowManager->ShadowMapBlurVertex) {
		Logger::Log("[ERROR] BounceLighting: could not load its shaders (Shaders\\Shadows\\ProbeCapture / ProbeReduce): off");
		Failed = true;
		return false;
	}
	CaptureVertex->ClearSamplers = false;
	CapturePixel->ClearSamplers = false;   // the pass binds each mesh's diffuse texture before SetCT
	ReducePixel->ClearSamplers = false;
	if (FAILED(Device->CreateCubeTexture(CaptureSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &CaptureCube, NULL)) ||
		FAILED(Device->CreateDepthStencilSurface(CaptureSize, CaptureSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &CaptureDepth, NULL))) {
		Logger::Log("[ERROR] BounceLighting: could not create its capture targets: off");
		Failed = true;
		return false;
	}
	for (int Face = 0; Face < 6; Face++) CaptureCube->GetCubeMapSurface((D3DCUBEMAP_FACES)Face, 0, &CaptureFace[Face]);
	TheShaderManager->CreateFrameVertex(1, 1, &TexelQuad);
	if (!TexelQuad) { Failed = true; return false; }
	CapturePass = new ProbeCapturePass();
	CapturePass->VertexShader = CaptureVertex;
	CapturePass->PixelShader = CapturePixel;
	Ready = true;
	Logger::Log("BounceLighting: %i x %i x %i probes, %i x %i texel captures", GridX, GridY, GridZ, CaptureSize, CaptureSize);
	return true;
}

D3DXVECTOR3 BounceLightingShaders::ProbePosition(int Probe) const {
	const int x = Probe % GridX, y = (Probe / GridX) % GridY, z = Probe / (GridX * GridY);
	return GridOrigin + D3DXVECTOR3(x * GridSpacing.x, y * GridSpacing.y, z * GridSpacing.z);
}

// The grid over the interior: where its references are, the probes spread over that, Spacing apart
// at the least on each axis (wider where the interior is bigger), centred. A reference counts with at
// most 128 units round its centre: by their bounding spheres, a building's shell made the bounds a
// cube 2900 units tall, and the probes 578 apart. The atlases cleared: nothing captured yet.
void BounceLightingShaders::FitGrid(TESObjectCELL* Cell) {
	D3DXVECTOR3 Low(FLT_MAX, FLT_MAX, FLT_MAX), High(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
		TESObjectREFR* Ref = Entry->item;
		if (!Ref || !Ref->baseForm) continue;
		const UInt8 Type = Ref->baseForm->formType;
		if (Type == TESForm::FormType::kFormType_NPC || Type == TESForm::FormType::kFormType_Creature) continue;
		NiNode* Node = Ref->GetNode();
		if (!Node || !Node->m_kWorldBound) continue;
		const float Radius = Node->m_kWorldBound->Radius;
		if (Radius <= 0.0f || Radius > 4000.0f) continue;   // a cell-wide backdrop would stretch the grid over nothing
		const D3DXVECTOR3 Centre(Node->m_kWorldBound->Center.x, Node->m_kWorldBound->Center.y, Node->m_kWorldBound->Center.z);
		const float Margin = min(Radius, 128.0f);
		const D3DXVECTOR3 Reach(Margin, Margin, Margin);
		D3DXVECTOR3 A = Centre - Reach, B = Centre + Reach;
		D3DXVec3Minimize(&Low, &Low, &A);
		D3DXVec3Maximize(&High, &High, &B);
	}
	GridCell = Cell;
	FitLoaded = CountLoaded(Cell);
	FitFrame = FrameCounter;
	FitChecked = false;
	CapturedFrame.assign(ProbeCount, 0);
	ProbeSignature.assign(ProbeCount, 0);
	ProbeCaptures.assign(ProbeCount, 0);
	CapturedAny = 0;
	IDirect3DDevice9* Device = TheRenderManager->device;
	Device->SetRenderTarget(0, AtlasASurface);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L);
	Device->SetRenderTarget(0, AtlasBSurface);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L);
	if (Low.x > High.x) {   // nothing to go by: round the camera
		const D3DXVECTOR4& Camera = TheRenderManager->CameraPosition;
		Low = D3DXVECTOR3(Camera.x - 1000.0f, Camera.y - 1000.0f, Camera.z - 300.0f);
		High = Low + D3DXVECTOR3(2000.0f, 2000.0f, 600.0f);
	}
	const D3DXVECTOR3 Extent = High - Low;
	GridSpacing = D3DXVECTOR3(max(Settings.Spacing, Extent.x / (GridX - 1)), max(Settings.Spacing, Extent.y / (GridY - 1)), max(Settings.Spacing, Extent.z / (GridZ - 1)));
	FittedSpacing = Settings.Spacing;
	const D3DXVECTOR3 Centre = (Low + High) * 0.5f;
	GridOrigin = Centre - D3DXVECTOR3((GridX - 1) * GridSpacing.x, (GridY - 1) * GridSpacing.y, (GridZ - 1) * GridSpacing.z) * 0.5f;
	Logger::Log("BounceLighting: interior %08X, %.0f x %.0f x %.0f units, probes %.0f x %.0f x %.0f apart", Cell->refID, Extent.x, Extent.y, Extent.z, GridSpacing.x, GridSpacing.y, GridSpacing.z);
}

int BounceLightingShaders::CountLoaded(TESObjectCELL* Cell) const {
	int Count = 0;
	for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
		TESObjectREFR* Ref = Entry->item;
		if (!Ref || !Ref->baseForm) continue;
		const UInt8 Type = Ref->baseForm->formType;
		if (Type == TESForm::FormType::kFormType_NPC || Type == TESForm::FormType::kFormType_Creature || Type == TESForm::FormType::kFormType_LeveledCreature) continue;
		NiNode* Node = Ref->GetNode();
		if (Node && Node->m_kWorldBound && Node->m_kWorldBound->Radius > 0.0f && Node->m_kWorldBound->Radius <= 4000.0f) Count++;
	}
	return Count;
}

// The lit, static meshes near this frame's probes, with their bounds.
void BounceLightingShaders::GatherGeometry(TESObjectCELL* Cell, const std::vector<int>& Probes) {
	FrameGeometry.clear();
	D3DXVECTOR3 Middle(0.0f, 0.0f, 0.0f);
	for (int Probe : Probes) Middle += ProbePosition(Probe);
	Middle /= (float)Probes.size();
	float Reach = 0.0f;
	for (int Probe : Probes) {
		const D3DXVECTOR3 Offset = ProbePosition(Probe) - Middle;
		Reach = max(Reach, D3DXVec3Length(&Offset));
	}
	Reach += Settings.Range;
	NiPoint3 MiddlePoint = { Middle.x, Middle.y, Middle.z };
	GatherLamps(Middle, Reach);

	// Every reference but the actors (they move: not part of the room's light) -- not the shadow maps'
	// filter (ShadowManager::GetRefNode), which leaves out references that do not cast shadows: room
	// shells and wall pieces often say so, and the probes need the walls and ceilings most of all.
	for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
		TESObjectREFR* Ref = Entry->item;
		if (!Ref || !Ref->baseForm) continue;
		const UInt8 Type = Ref->baseForm->formType;
		if (Type == TESForm::FormType::kFormType_NPC || Type == TESForm::FormType::kFormType_Creature || Type == TESForm::FormType::kFormType_LeveledCreature) continue;
		NiNode* Node = Ref->GetNode();
		if (!Node || Node->m_flags & NiAVObject::NiFlags::APP_CULLED) continue;
		if (Node->GetDistance(&MiddlePoint) > Reach + Node->GetWorldBoundRadius()) continue;

		std::vector<NiAVObject*>& Walk = WalkScratch;
		Walk.clear();
		Walk.push_back(Node);
		while (!Walk.empty()) {
			NiAVObject* Object = Walk.back();
			Walk.pop_back();
			if (!Object || Object->m_flags & NiAVObject::NiFlags::APP_CULLED) continue;
			if (Object->IsGeometry()) {
				NiGeometry* Geo = static_cast<NiGeometry*>(Object);
				if (Geo->skinInstance || !Geo->shader || !Geo->m_kWorldBound) continue;
				if (Geo->m_kWorldBound->Radius < 4.0f) continue;   // a texel at most, at any distance a probe sees it
				BSShaderProperty* Shader = static_cast<BSShaderProperty*>(Geo->GetProperty(NiProperty::kType_Shade));
				if (!Shader || Shader->GetFlag(BSSP_REFRACTION) || Shader->GetFlag(BSSP_FIRE_REFRACTION) || Shader->GetFlag(BSSP_DECAL) || Shader->GetFlag(BSSP_DYNAMIC_DECAL)) continue;
				ProbeGeometry Item;
				Item.Geo = Geo;
				Item.Centre = D3DXVECTOR3(Geo->m_kWorldBound->Center.x, Geo->m_kWorldBound->Center.y, Geo->m_kWorldBound->Center.z);
				Item.Radius = Geo->m_kWorldBound->Radius;
				FrameGeometry.push_back(Item);
				continue;
			}
			NiNode* Inner = Object->IsNiNode();
			if (!Inner) continue;
			if (Inner->IsKindOf<NiSwitchNode>()) {
				NiSwitchNode* Switch = static_cast<NiSwitchNode*>(Inner);
				if (Switch->m_iIndex >= 0) Walk.push_back(Inner->m_children.data[Switch->m_iIndex]);
				continue;
			}
			if (Inner->IsFadeNode() && static_cast<BSFadeNode*>(Inner)->FadeAlpha < 0.75f) continue;
			for (int i = 0; i < Inner->m_children.end; i++) Walk.push_back(Inner->m_children.data[i]);
		}
	}
}

// The lamps that can light what this frame's probes see, nearest first: every point light in the
// scene (ShaderManager::GetNearbyLights' PresentLights), with its shadow slot as the object shaders
// have it (ForwardPointShadows).
void BounceLightingShaders::GatherLamps(const D3DXVECTOR3& Middle, float Reach) {
	LampCandidates.clear();
	for (ShadowSceneLight* SceneLight : TheShaderManager->PresentLightsScratch) {
		NiPointLight* Light = SceneLight ? SceneLight->sourceLight : nullptr;
		if (!Light || Light->EffectType != NiDynamicEffect::EffectTypes::POINT_LIGHT) continue;
		if (Light->Spec.r <= 0.0f || Light->Dimmer <= 0.0f) continue;
		const D3DXVECTOR3 Offset = TheShaderManager->LampRestPosition(Light) - Middle;
		const float Distance = D3DXVec3Length(&Offset);
		if (Distance > Reach + Light->Spec.r) continue;
		LampCandidates.emplace_back(Distance, SceneLight);
	}
	std::sort(LampCandidates.begin(), LampCandidates.end(), [](const auto& A, const auto& B) { return A.first < B.first; });
	const D3DXVECTOR4 Empty(0.0f, 0.0f, 0.0f, 0.0f);
	int Count = 0;
	for (const auto& Candidate : LampCandidates) {
		if (Count >= ProbeLampsMax) break;
		ShadowSceneLight* SceneLight = Candidate.second;
		NiPointLight* Light = SceneLight->sourceLight;
		D3DXVECTOR4 Anchor = Empty;
		float Info = -1.0f;
		const auto Found = TheShadowManager->LampSlot.find(SceneLight);
		if (TheShaderManager->Effects.ShadowsExteriors->Constants.PointShadowData.x > 0.0f && Found != TheShadowManager->LampSlot.end() &&
			Found->second >= 0 && Found->second < ShadowSlotsMax && TheShadowManager->SlotPosition[Found->second].w > 0.0f) {
			Anchor = TheShadowManager->SlotPosition[Found->second];
			Info = (float)Found->second * 2.0f + std::clamp(TheShadowManager->SlotFadeValue[Found->second], 0.0f, 1.0f);
		}
		const D3DXVECTOR3 Pos = TheShaderManager->LampRestPosition(Light);   // a swaying lamp from where it rests, as its shadow
		LampPosition[Count] = D3DXVECTOR4(Pos.x, Pos.y, Pos.z, Light->Spec.r);
		LampColor[Count] = D3DXVECTOR4(Light->Diff.r * Light->Dimmer, Light->Diff.g * Light->Dimmer, Light->Diff.b * Light->Dimmer, Info);
		LampAnchor[Count] = Anchor;
		Count++;
	}
	LogLamps = max(LogLamps, (UInt32)Count);
	for (; Count < ProbeLampsMax; Count++) {
		LampPosition[Count] = Empty;
		LampColor[Count] = D3DXVECTOR4(0.0f, 0.0f, 0.0f, -1.0f);
		LampAnchor[Count] = Empty;
	}
}

// A probe's cubemap drawn: the meshes round it, each face.
void BounceLightingShaders::RenderProbeCube(int Probe) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	const D3DXVECTOR3 Position = ProbePosition(Probe);
	const D3DXVECTOR4& Camera = TheRenderManager->CameraPosition;
	const D3DXVECTOR3 Eye(Position.x - Camera.x, Position.y - Camera.y, Position.z - Camera.z);
	D3DXMATRIX View, Proj;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(90.0f), 1.0f, 1.0f, Settings.Range);
	Shadows->Constants.ShadowCubeMapLightPosition = D3DXVECTOR4(Eye.x, Eye.y, Eye.z, Settings.Range);   // the probe, for the capture shader

	ForceRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	ForceRenderState(D3DRS_ZWRITEENABLE, TRUE);
	ForceRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	ForceRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	ForceRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	const D3DVIEWPORT9 FaceViewport = { 0, 0, (DWORD)CaptureSize, (DWORD)CaptureSize, 0.0f, 1.0f };
	for (int Face = 0; Face < 6; Face++) {
		// The faces as the lamps' cubes have them (ShadowManager::RenderShadowCubeMap), so the
		// cube reads back the same way (Shaders/Shadows/ProbeReduce).
		D3DXVECTOR3 Direction, Up;
		switch (Face) {
		case D3DCUBEMAP_FACE_POSITIVE_X: Direction = D3DXVECTOR3(1.0f, 0.0f, 0.0f); Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f); break;
		case D3DCUBEMAP_FACE_NEGATIVE_X: Direction = D3DXVECTOR3(-1.0f, 0.0f, 0.0f); Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f); break;
		case D3DCUBEMAP_FACE_POSITIVE_Y: Direction = D3DXVECTOR3(0.0f, 1.0f, 0.0f); Up = D3DXVECTOR3(0.0f, 0.0f, 1.0f); break;
		case D3DCUBEMAP_FACE_NEGATIVE_Y: Direction = D3DXVECTOR3(0.0f, -1.0f, 0.0f); Up = D3DXVECTOR3(0.0f, 0.0f, -1.0f); break;
		case D3DCUBEMAP_FACE_POSITIVE_Z: Direction = D3DXVECTOR3(0.0f, 0.0f, -1.0f); Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f); break;
		default:                         Direction = D3DXVECTOR3(0.0f, 0.0f, 1.0f); Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f); break;
		}
		const D3DXVECTOR3 At = Eye + Direction;
		D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
		Shadows->Constants.ShadowViewProj = View * Proj;
		Device->SetRenderTarget(0, CaptureFace[Face]);
		Device->SetDepthStencilSurface(CaptureDepth);
		Device->SetViewport(&FaceViewport);
		Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L);
		for (const ProbeGeometry& Item : FrameGeometry) {
			const D3DXVECTOR3 Centre = Item.Centre - Position;
			const float Distance2 = D3DXVec3LengthSq(&Centre);
			if (Distance2 > (Settings.Range + Item.Radius) * (Settings.Range + Item.Radius)) continue;
			// Smaller than about a third of a texel of the 8 x 8 face (90 degrees across: a texel is
			// about 0.2 radians): it changes nothing, and each is a draw call.
			if (Item.Radius * Item.Radius < Distance2 * 0.0036f) continue;
			if (!ProbeSphereInFace(Centre, Item.Radius, Direction)) continue;
			if (CapturePass->AccumObject(Item.Geo)) LogDraws++;
		}
		CapturePass->RenderAccum();
	}
}

// One probe: its cubemap drawn, then boiled down into its texels of the two atlases.
void BounceLightingShaders::CaptureProbe(int Probe) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	RenderProbeCube(Probe);

	// Into the atlases: the probe's texel, one quad each. The capture shader read them: unbound first,
	// as they are now drawn into.
	RenderState->SetTexture(2, nullptr);
	RenderState->SetTexture(3, nullptr);
	const int x = Probe % GridX, y = (Probe / GridX) % GridY, z = Probe / (GridX * GridY);
	const D3DVIEWPORT9 Texel = { (DWORD)(z * GridX + x), (DWORD)y, 1, 1, 0.0f, 1.0f };
	Device->SetDepthStencilSurface(NULL);
	ForceRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	ForceRenderState(D3DRS_ZWRITEENABLE, FALSE);
	ForceRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	RenderState->SetVertexShader(TheShadowManager->ShadowMapBlurVertex->ShaderHandle, false);   // passes the quad through
	RenderState->SetPixelShader(ReducePixel->ShaderHandle, false);
	RenderState->SetFVF(FrameFVF, false);
	Device->SetStreamSource(0, TexelQuad, 0, sizeof(FrameVS));
	RenderState->SetTexture(0, CaptureCube);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP, false);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP, false);
	RenderState->SetSamplerState(0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP, false);
	RenderState->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR, false);
	RenderState->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR, false);
	RenderState->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE, false);
	// Both atlases in one pass (COLOR0 and COLOR1, the cube read once), or one each where the device
	// has a single render target (Shaders/Shadows/ProbeReduce).
	if (MultipleTargets < 0) {
		D3DCAPS9 Caps;
		MultipleTargets = (SUCCEEDED(Device->GetDeviceCaps(&Caps)) && Caps.NumSimultaneousRTs >= 2) ? 1 : 0;
	}
	const int Passes = MultipleTargets ? 1 : 2;
	for (int Target = 0; Target < Passes; Target++) {
		Device->SetRenderTarget(0, Target ? AtlasBSurface : AtlasASurface);
		if (MultipleTargets) Device->SetRenderTarget(1, AtlasBSurface);
		Device->SetViewport(&Texel);
		const float Mode[4] = { (float)Target, 0.0f, 0.0f, 0.0f };
		Device->SetPixelShaderConstantF(0, Mode, 1);
		Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	}
	if (MultipleTargets) Device->SetRenderTarget(1, NULL);
	RenderState->SetTexture(0, nullptr);
	ForceRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	ForceRenderState(D3DRS_ZWRITEENABLE, TRUE);
	CapturedFrame[Probe] = FrameCounter;
	ProbeSignature[Probe] = ProbeSignatureOf(ProbePosition(Probe));
	if (ProbeCaptures[Probe] < 255) ProbeCaptures[Probe]++;
	CapturedAny++;
}

// This frame's lamps, each hashed from what a capture takes of it: its place (to 4 units), reach,
// colour (to 1/32) and whether it is shadowed. Every point light in the scene, as GatherLamps.
void BounceLightingShaders::BuildLampRecords() {
	FrameLamps.clear();
	for (ShadowSceneLight* SceneLight : TheShaderManager->PresentLightsScratch) {
		NiPointLight* Light = SceneLight ? SceneLight->sourceLight : nullptr;
		if (!Light || Light->EffectType != NiDynamicEffect::EffectTypes::POINT_LIGHT) continue;
		if (Light->Spec.r <= 0.0f || Light->Dimmer <= 0.0f) continue;
		const D3DXVECTOR3 Pos = TheShaderManager->LampRestPosition(Light);   // a swinging bulb does not keep its probes recapturing
		UInt32 Hash = 2166136261u;
		auto Mix = [&Hash](int Value) { Hash = (Hash ^ (UInt32)Value) * 16777619u; };
		Mix((int)floorf(Pos.x * 0.25f)); Mix((int)floorf(Pos.y * 0.25f)); Mix((int)floorf(Pos.z * 0.25f));
		Mix((int)Light->Spec.r);
		Mix((int)(Light->Diff.r * Light->Dimmer * 32.0f)); Mix((int)(Light->Diff.g * Light->Dimmer * 32.0f)); Mix((int)(Light->Diff.b * Light->Dimmer * 32.0f));
		Mix(TheShadowManager->LampSlot.find(SceneLight) != TheShadowManager->LampSlot.end() ? 1 : 0);
		LampRecord Record;
		Record.Position = D3DXVECTOR3(Pos.x, Pos.y, Pos.z);
		Record.Reach = Light->Spec.r;
		Record.Hash = Hash;
		FrameLamps.push_back(Record);
	}
}

// The lamps able to light what a probe at Position sees, hashed in any order (summed), with the
// settings that change a capture.
UInt32 BounceLightingShaders::ProbeSignatureOf(const D3DXVECTOR3& Position) const {
	UInt32 Signature = FrameSalt;
	for (const LampRecord& Lamp : FrameLamps) {
		const D3DXVECTOR3 Offset = Lamp.Position - Position;
		const float Reach = Settings.Range + Lamp.Reach;
		if (D3DXVec3LengthSq(&Offset) < Reach * Reach) Signature += Lamp.Hash * 2654435761u;
	}
	return Signature;
}

// A few probes a frame: never captured ones nearest the camera first, then the oldest round it
// (lamps change, doors open). Within RenderShadowMaps' scene and state; ZFUNC put back after.
void BounceLightingShaders::Capture() {
	if (!Enabled || TheShaderManager->GameState.isExterior || !Player || !Player->parentCell) return;
	if (!EnsureResources()) return;
	TESObjectCELL* Cell = Player->parentCell;
	if (Cell != GridCell)
		FitGrid(Cell);
	else if (FitLoaded == 0 && FrameCounter - FitFrame >= 30)
		FitGrid(Cell);   // nothing had loaded yet: try again
	else if (!FitChecked && FrameCounter - FitFrame >= 60) {
		FitChecked = true;
		const int Loaded = CountLoaded(Cell);
		if (Loaded > FitLoaded + max(8, FitLoaded / 8)) FitGrid(Cell);   // much more has loaded since
	}

	// What changes a capture: the settings it reads, hashed into every probe's signature.
	{
		const float Inputs[] = { Constants.Capture.x, Constants.Capture.y, Constants.Capture.z, Constants.CaptureDebug.x, Settings.Range,
			TheShaderManager->Shaders.InverseSquareLighting->Constants.Data.x, TheShaderManager->Shaders.InverseSquareLighting->Constants.Data.w };
		FrameSalt = 2166136261u;
		for (float Input : Inputs) FrameSalt = (FrameSalt ^ *(const UInt32*)&Input) * 16777619u;
	}
	BuildLampRecords();

	// Candidates: never captured ones nearest the camera first; then those still settling, the
	// oldest and nearest first; then settled ones whose lamps changed (a window of the grid checked a
	// frame) or that have gone RefreshFrames without a capture.
	const D3DXVECTOR3 Camera(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	const int WindowStart = SignatureCursor, WindowSize = 256;
	SignatureCursor = (SignatureCursor + WindowSize) % ProbeCount;
	Candidates.clear();
	for (int Probe = 0; Probe < ProbeCount; Probe++) {
		const D3DXVECTOR3 Offset = ProbePosition(Probe) - Camera;
		const float Distance2 = D3DXVec3LengthSq(&Offset);
		if (!CapturedFrame[Probe]) {
			Candidates.emplace_back(Distance2, Probe);
			continue;
		}
		const UInt32 Age = FrameCounter - CapturedFrame[Probe];
		if (Age < 120 || Distance2 > 2000.0f * 2000.0f) continue;   // fresh, or far from where it matters
		if (ProbeCaptures[Probe] < SettleCaptures) {
			Candidates.emplace_back(1e12f + Distance2 / (float)Age, Probe);
			continue;
		}
		if (Age >= RefreshFrames) {
			Candidates.emplace_back(1e13f + Distance2 / (float)Age, Probe);
			continue;
		}
		if ((Probe - WindowStart + ProbeCount) % ProbeCount >= WindowSize) continue;   // not this frame's window
		if (ProbeSignatureOf(ProbePosition(Probe)) != ProbeSignature[Probe]) {
			ProbeCaptures[Probe] = 0;   // its lamps changed: settles again
			Candidates.emplace_back(1e12f + Distance2 / (float)Age, Probe);
		}
	}
	if (Candidates.empty()) return;

	// The first by priority, the rest its nearest among the next few: neighbours see the same
	// meshes, so gathering covers a small area (two probes across the cell gathered most of it).
	const size_t Pool = min((size_t)32, Candidates.size());
	std::partial_sort(Candidates.begin(), Candidates.begin() + Pool, Candidates.end());
	static std::vector<int> Chosen;
	Chosen.clear();
	Chosen.push_back(Candidates[0].second);
	const D3DXVECTOR3 First = ProbePosition(Candidates[0].second);
	std::vector<bool> Taken(Pool, false);
	Taken[0] = true;
	const size_t Count = min((size_t)Settings.ProbesPerFrame, Pool);
	while (Chosen.size() < Count) {
		size_t Best = 0;
		float BestDistance = FLT_MAX;
		for (size_t i = 1; i < Pool; i++) {
			if (Taken[i]) continue;
			const D3DXVECTOR3 Offset = ProbePosition(Candidates[i].second) - First;
			const float Distance2 = D3DXVec3LengthSq(&Offset);
			if (Distance2 < BestDistance) { BestDistance = Distance2; Best = i; }
		}
		if (!Best) break;
		Taken[Best] = true;
		Chosen.push_back(Candidates[Best].second);
	}

	GatherGeometry(Cell, Chosen);
	IDirect3DDevice9* Device = TheRenderManager->device;
	DWORD ZFunc = D3DCMP_LESSEQUAL, ColorWrite = 0xF;
	Device->GetRenderState(D3DRS_ZFUNC, &ZFunc);
	Device->GetRenderState(D3DRS_COLORWRITEENABLE, &ColorWrite);
	ForceRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	// All four channels: the lamps' cubes are one channel, so a mask left on red alone would never
	// have shown there.
	ForceRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	// No user clip planes or scissor from the frame round it: either could cut a probe's draws away.
	DWORD ClipPlanes = 0, Scissor = FALSE;
	Device->GetRenderState(D3DRS_CLIPPLANEENABLE, &ClipPlanes);
	Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &Scissor);
	ForceRenderState(D3DRS_CLIPPLANEENABLE, 0);
	ForceRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	LogMeshes = max(LogMeshes, (UInt32)FrameGeometry.size());
	for (int Probe : Chosen) CaptureProbe(Probe);
	ForceRenderState(D3DRS_ZFUNC, ZFunc);
	ForceRenderState(D3DRS_COLORWRITEENABLE, ColorWrite);
	ForceRenderState(D3DRS_CLIPPLANEENABLE, ClipPlanes);
	ForceRenderState(D3DRS_SCISSORTESTENABLE, Scissor);
	FrameGeometry.clear();
	LogProbes += (UInt32)Chosen.size();
	if (TheSettingManager->SettingsMain.Develop.DebugMode && ++LogFrames >= 300) {
		Logger::Log("BounceLighting over %u frames: %u probes captured (%u so far in this interior), up to %u meshes gathered and %u lamps a frame, %.1f draws a probe",
			LogFrames, LogProbes, CapturedAny, LogMeshes, LogLamps, LogProbes ? (float)LogDraws / LogProbes : 0.0f);
		LogFrames = LogProbes = LogMeshes = LogDraws = LogLamps = 0;
	}
}


bool ProbeCapturePass::AccumObject(NiGeometry* Geo) {
	if (!Geo->geomData || !Geo->geomData->m_pkBuffData) return false;
	BSShaderProperty* ShaderProperty = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	if (!ShaderProperty || !ShaderProperty->IsLightingProperty()) return false;
	GeometryList.push(Geo);
	return true;
}

// Its world transform, its diffuse texture (the surface's colour; alpha tested ones cut by it) and
// both faces drawn: a probe inside a wall sees its back faces, and is told by them
// (Shaders/Shadows/ProbeCapture).
void ProbeCapturePass::UpdateConstants(NiGeometry* Geo) {
	ShadowsExteriorEffect::ShadowStruct* ShadowConstants = &TheShaderManager->Effects.ShadowsExteriors->Constants;
	BounceLightingShaders* Bounce = TheShaderManager->Shaders.BounceLighting;
	TheRenderManager->CreateD3DMatrix(&TheShaderManager->ShaderConst.ShadowWorld, &Geo->m_worldTransform);
	ShadowConstants->Data.x = 0.0f;
	ShadowConstants->Data.y = 0.0f;
	Bounce->Constants.Capture.w = 0.0f;
	BSShaderPPLightingProperty* Lighting = (BSShaderPPLightingProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
	NiTexture* Texture = (Lighting && Lighting->ppTextures[0]) ? *Lighting->ppTextures[0] : nullptr;
	if (Texture && Texture->rendererData && Texture->rendererData->dTexture) {
		Bounce->Constants.Capture.w = 1.0f;
		NiAlphaProperty* AProp = (NiAlphaProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Alpha);
		if (AProp && (AProp->flags & NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK)) ShadowConstants->Data.y = 1.0f;
		NiDX9RenderState* RenderState = TheRenderManager->renderState;
		RenderState->SetTexture(0, Texture->rendererData->dTexture);
		RenderState->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP, false);
		RenderState->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP, false);
		RenderState->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR, false);
		RenderState->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR, false);
		RenderState->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR, false);
	}
}

void ProbeCapturePass::RenderGeometry(NiGeometry* Geo) {
	NiGeometryData* ModelData = Geo->geomData;
	NiGeometryBufferData* GeoData = ModelData->m_pkBuffData;
	ForceRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	TheRenderManager->PackGeometryBuffer(GeoData, ModelData, NULL, Geo->shader->ShaderDeclaration);
	if (GeoData && GeoData->VertCount) DrawGeometryBuffer(Geo, GeoData);
}
