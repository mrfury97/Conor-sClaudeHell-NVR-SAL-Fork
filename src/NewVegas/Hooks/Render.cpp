#pragma once
#include <cfloat>

// See ReflectionPassScope: the next shader bind re-uploads NVR's constants.
static bool ForcePixelConstants = false;

void (__thiscall* Render)(Main*, BSRenderedTexture*, int, int) = (void (__thiscall*)(Main*, BSRenderedTexture*, int, int))Hooks::Render;
void __fastcall RenderHook(Main* This, UInt32 edx, BSRenderedTexture* RenderedTexture, int Arg2, int Arg3) {
	
	SettingsMainStruct* SettingsMain = &TheSettingManager->SettingsMain;

	TheFrameRateManager->UpdatePerformance();
	TheCameraManager->SetSceneGraph();
	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();

	TheShaderManager->UpdateConstants();

	// Reset the material pass queue for the frame. Passing false when the flashlight is
	// not lit leaves it inactive, so the scene walk in RenderWorldSceneGraphHook costs
	// nothing while the light is off.
	FlashlightEffect* Flashlight = TheShaderManager->Effects.Flashlight;
	MaterialPass::BeginFrame(Flashlight->Enabled && Flashlight->spotLightActive);

	// The skin scattering target is cleared on the frame's first skin draw.
	if (TheShaderManager->Effects.SkinScattering) TheShaderManager->Effects.SkinScattering->BeginFrame();
	UpdateFaceGenInteriorFlag();

	//if (SettingsMain->Develop.TraceShaders && InterfaceManager->IsActive(Menu::MenuType::kMenuType_None) && Global->OnKeyDown(SettingsMain->Develop.TraceShaders) && DWNode::Get() == NULL) DWNode::Create();
	(*Render)(This, RenderedTexture, Arg2, Arg3);

}

void (__thiscall* SetShaders)(BSShader*, UInt32) = (void (__thiscall*)(BSShader*, UInt32))Hooks::SetShaders;
void __fastcall SetShadersHook(BSShader* This, UInt32 edx, UInt32 PassIndex) {
	
	void* CurrentRenderPass = *(void**)0x011F91E0;   // BSShaderManager::pCurrentRenderPass; pGeometry is its first member
	NiGeometry* Geometry = CurrentRenderPass ? *(NiGeometry**)CurrentRenderPass : nullptr;
	NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;
	NiD3DVertexShaderEx* VertexShader = (NiD3DVertexShaderEx*)Pass->VertexShader;
	NiD3DPixelShaderEx* PixelShader = (NiD3DPixelShaderEx*)Pass->PixelShader;
	IDirect3DVertexShader9* VertexShader2 = TheRenderManager->renderState->GetVertexShader();
	IDirect3DPixelShader9* PixelShader2 = TheRenderManager->renderState->GetPixelShader();

	if (VertexShader) {
		VertexShader->SetupShader(VertexShader2);
	}
	else {
		Logger::Log("Error getting vertex shader for pass %s", Pointers::Functions::GetPassDescription(PassIndex));
	}
	if (PixelShader) {
		PixelShader->SetupShader(ForcePixelConstants ? nullptr : PixelShader2);   // see ForcePixelConstants
		ForcePixelConstants = false;
	}
	else {
		Logger::Log("Error getting pixel shader for pass %s", Pointers::Functions::GetPassDescription(PassIndex));
	}

	// trace pipeline active shaders
	if (TheSettingManager->SettingsMain.Develop.DebugMode && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Console) && Global->OnKeyDown(TheSettingManager->SettingsMain.Develop.TraceShaders)) {
		char Name[256];
		sprintf(Name, "Pass %i %s, %s (%s %s)", PassIndex, Pointers::Functions::GetPassDescription(PassIndex), Geometry->m_pcName, VertexShader->Name, PixelShader->Name);
		if (VertexShader->ShaderHandle == VertexShader->ShaderHandleBackup) strcat(Name, " - Vertex: vanilla");
		if (PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup) strcat(Name, " - Pixel: vanilla");
		Logger::Log("%s", Name);
		InterfaceManager->ShowMessage("Shaders Traced");
		//DWNode::AddNode(Name, Geometry->m_parent, Geometry);
	}
	(*SetShaders)(This, PassIndex);

	// Per-object material data for the PBR object shaders: ObjectMaterial, pixel register c150
	// (Shaders/Includes/Object.hlsl). Set directly, per draw: the TESR_ constant table only
	// uploads on a shader change. Shaders that do not declare c150 ignore it.
	//   x = 1 when the mesh has the engine's Specular flag, so the diffuse-only shader variants
	//       know not to add a highlight the game draws in its own specular pass.
	//   y = the engine's specular distance fade, BSShaderPPLightingProperty::GetSpecularLODFade
	//       (0xB66B80): 1 up to fSpecularLODStartFade, 0 from fSpecularLODEnd on, where the game
	//       stops drawing the specular pass. Vanilla fades the highlight with it so it does not pop.
	// This runs once per batch; the ShadowLightShader, HairShader and ParallaxShader per-geometry
	// hooks (Hooks/Shaders.cpp) write it again for every draw.
	WriteObjectMaterial(Geometry);

}

HRESULT (__thiscall* SetSamplerState)(NiDX9RenderState*, UInt32, D3DSAMPLERSTATETYPE, UInt32, UInt8) = (HRESULT (__thiscall*)(NiDX9RenderState*, UInt32, D3DSAMPLERSTATETYPE, UInt32, UInt8))Hooks::SetSamplerState;
HRESULT __fastcall SetSamplerStateHook(NiDX9RenderState* This, UInt32 edx, UInt32 Sampler, D3DSAMPLERSTATETYPE Type, UInt32 Value, UInt8 Save) {

	UInt16* TypeMap = (UInt16*)0x126F92C;
	HRESULT r = D3D_OK;

	if (TypeMap[Type] < 5)
		r = (*SetSamplerState)(This, Sampler, Type, Value, Save);
	else
		r = TheRenderManager->device->SetSamplerState(Sampler, Type, Value);
	return r;

}

void (__thiscall* RenderWorldSceneGraph)(Main*, Sun*, UInt8, UInt8, UInt8) = (void (__thiscall*)(Main*, Sun*, UInt8, UInt8, UInt8))Hooks::RenderWorldSceneGraph;
void __fastcall RenderWorldSceneGraphHook(Main* This, UInt32 edx, Sun* SkySun, UInt8 IsFirstPerson, UInt8 WireFrame, UInt8 Arg4) {
	// TAA sub-pixel jitter covers the world scene and nothing else -- see TAAEffect::BeginJitter.
	TAAEffect* TAA = TheShaderManager->Effects.TAA;
	if (TAA) TAA->BeginJitter();

	// Skin scattering's target is bound only for skin drawn inside the world scene.
	SkinScatteringEffect* SkinScattering = TheShaderManager->Effects.SkinScattering;
	if (SkinScattering) SkinScattering->InWorldScene = true;

	// Terrain blending copies the world's depth once, as the first land shader binds (TerrainShaders::PrepareBlending).
	TheTextureManager->TerrainBlendDepthState = 1;
	// The world's depth surface, so terrain blending knows the main scene from reflections and cube maps (their own
	// depth). Only the pointer is kept, for comparing; the reference is released at once.
	{
		IDirect3DSurface9* worldDepth = nullptr;
		TheRenderManager->device->GetDepthStencilSurface(&worldDepth);
		TheTextureManager->TerrainBlendWorldDepth = worldDepth;
		if (worldDepth) worldDepth->Release();
	}
	// Diagnostic: every 2000th exterior scene, log the order NVR's shaders bind in, to show whether the land draws
	// before or after the objects it should blend with.
	static UInt32 sceneCount = 0;
	TheTextureManager->TerrainBlendLogScene = TheSettingManager->SettingsMain.Develop.DebugMode && TheShaderManager->GameState.isExterior && (++sceneCount % 2000) == 300;
	TheTextureManager->TerrainBlendLog.clear();

	(*RenderWorldSceneGraph)(This, SkySun, IsFirstPerson, WireFrame, Arg4);

	if (TheTextureManager->TerrainBlendLogScene)
		Logger::Log("[TerrainBlend] world scene shader order: %s", TheTextureManager->TerrainBlendLog.c_str());
	TheTextureManager->TerrainBlendLogScene = false;
	TheTextureManager->TerrainBlendDepthState = 0;

	// The SkinShader hooks unbind the skin scattering target after every skin draw; this only
	// guarantees nothing below (the material pass, depth resolves) ever draws into it.
	if (SkinScattering) {
		SkinScattering->InWorldScene = false;
		SkinScattering->Unbind();
	}

	// Re-light nearby statics inside the flashlight cone. This has to happen here, before
	// the viewmodel depth handling below clears the Z buffer: the pass draws with depth
	// testing on and depth writes off, so with a cleared Z buffer it would draw straight
	// through world geometry.
	MaterialPass::CaptureScene(WorldSceneGraph);
	MaterialPass::RenderWorld();

	// After the material pass, not before: it redraws world geometry over the scene, and must
	// land on exactly the same jittered pixels the world did.
	if (TAA) TAA->EndJitter();

	const bool bPipBoyOpen = InterfaceManager->IsPipBoyOpen();
	const bool bPipBoyLive = (TheGameMenuManager->IsLiveMenu && TheGameMenuManager->IsLiveMenu(Menu::kMenuType_BigFour, false, false) == GameMenuManager::MenuPauseState::MENU_LIVE);

	if (!bPipBoyOpen || bPipBoyLive)
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTexture); // disable updating the world buffer when pipboy is out

	if (!IsFirstPerson) {
		// clear the viewmodel depth buffer
		TheRenderManager->Clear(NULL, NiRenderer::kClear_ZBUFFER);
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTextureViewModel);
	}
}

void (__thiscall* RenderFirstPerson)(Main*, NiDX9Renderer*, NiGeometry*, Sun*, BSRenderedTexture*) = (void (__thiscall*)(Main*, NiDX9Renderer*, NiGeometry*, Sun*, BSRenderedTexture*))Hooks::RenderFirstPerson;
void __fastcall RenderFirstPersonHook(Main* This, UInt32 edx, NiDX9Renderer* Renderer, NiGeometry* Geo, Sun* SkySun, BSRenderedTexture* RenderedTexture) {
	// Clear the depth buffer before rendering first person model to prevent clipping with world objects & other artefacts
	TheRenderManager->Clear(NULL, NiRenderer::kClear_ZBUFFER);
	//ThisCall(0x00874C10, Global);
	(*RenderFirstPerson)(This, Renderer, Geo, SkySun, RenderedTexture);
	if (TheShaderManager->Effects.SkinScattering) TheShaderManager->Effects.SkinScattering->Unbind();
	TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTextureViewModel);
}

// Set when NVR changes, mid-frame, a TESR_ constant that game shaders read (the water reflection pass).
// Constants reach a game shader only in ShaderRecord::SetCT, which runs when the pixel shader CHANGES, so
// the next bind is told that no pixel shader is bound: an NVR shader then uploads even if the game keeps
// the one already bound (SetShadersHook). Ported from NVR UNOFFICIAL Optimized (P40).

// What the water reflection map is drawn with, restored when the pass ends:
//   [Main.Main.Water] ForceReflections: the old path's TESR_ShadowData.x = -1 and no terrain parallax.
//   [Main.Main.ReducedQuality] CheapReflections: no forward sun shadows (TESR_ShadowForwardData.x = 1, also
//     written to c133, which ShadowsExteriorEffect binds directly once per frame) and no terrain parallax or
//     parallax shadows, like the game's own reflections.
// Ported from NVR UNOFFICIAL Optimized (P40).
class ReflectionPassScope {
public:
	ReflectionPassScope() {
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		TerrainShaders* terrain = TheShaderManager->Shaders.Terrain;
		if (!shadows || !terrain) return;
		const bool force = TheSettingManager->SettingsMain.Main.ForceReflections;
		const bool cheap = TheSettingManager->GetSettingI("Main.Main.ReducedQuality", "CheapReflections") != 0;
		Shadows = shadows; Terrain = terrain;
		Saved[0] = shadows->Constants.Data.x;
		Saved[1] = shadows->Constants.ForwardData.x;
		Saved[2] = terrain->ParallaxConstants.Data.x;
		Saved[3] = terrain->ParallaxConstants.Data.y;
		if (force) shadows->Constants.Data.x = -1.0f;
		if (force || cheap) { terrain->ParallaxConstants.Data.x = 0.0f; terrain->ParallaxConstants.Data.y = 0.0f; }
		if (cheap) {
			shadows->Constants.ForwardData.x = 1.0f;
			TheRenderManager->device->SetPixelShaderConstantF(133, (const float*)&shadows->Constants.ForwardData, 1);
		}
		ForcePixelConstants = true;
	}
	~ReflectionPassScope() {
		if (!Shadows) return;
		Shadows->Constants.Data.x = Saved[0];
		Shadows->Constants.ForwardData.x = Saved[1];
		Terrain->ParallaxConstants.Data.x = Saved[2];
		Terrain->ParallaxConstants.Data.y = Saved[3];
		TheRenderManager->device->SetPixelShaderConstantF(133, (const float*)&Shadows->Constants.ForwardData, 1);
		ForcePixelConstants = true;
	}
private:
	ShadowsExteriorEffect* Shadows = nullptr;
	TerrainShaders* Terrain = nullptr;
	float Saved[4] = {};
};
void (__thiscall* RenderReflections)(WaterManager*, NiCamera*, ShadowSceneNode*) = (void (__thiscall*)(WaterManager*, NiCamera*, ShadowSceneNode*))Hooks::RenderReflections;
void __fastcall RenderReflectionsHook(WaterManager* This, UInt32 edx, NiCamera* Camera, ShadowSceneNode* SceneNode) {
	ReflectionPassScope scope;
	if (DWNode::Get()) DWNode::AddNode("BEGIN REFLECTIONS RENDERING", NULL, NULL);
	(*RenderReflections)(This, Camera, SceneNode);
	if (DWNode::Get()) DWNode::AddNode("END REFLECTIONS RENDERING", NULL, NULL);
}
void (__thiscall* RenderPipboy)(Main*, NiGeometry*, NiDX9Renderer*) = (void (__thiscall*)(Main*, NiGeometry*, NiDX9Renderer*))Hooks::RenderPipboy;
void __fastcall RenderPipboyHook(Main* This, UInt32 edx, NiGeometry* Geo, NiDX9Renderer* Renderer) {
	WorldSceneGraph->UpdateParticleShaderFoV(Player->firstPersonFoV);
//	Player->SetFoV(Player->firstPersonFoV);
	(*RenderPipboy)(This, Geo, Renderer);
}

float (__thiscall* GetWaterHeightLOD)(TESWorldSpace*) = (float (__thiscall*)(TESWorldSpace*))Hooks::GetWaterHeightLOD;
float __fastcall GetWaterHeightLODHook(TESWorldSpace* This, UInt32 edx) {
	
	float r = This->waterHeight;
	if (*(void**)This == (void*)0x0103195C) r = TheShaderManager->Shaders.Water->Constants.Default.waterSettings.x;
	return r;

}

bool bSkippedRender_RenderedMenu = false;
bool bDoneRender_LockPickMenu = false;

void(__cdecl* ProcessImageSpaceShaders)(NiDX9Renderer*, BSRenderedTexture*, BSRenderedTexture*) = (void(__cdecl*)(NiDX9Renderer*, BSRenderedTexture*, BSRenderedTexture*))Hooks::ProcessImageSpaceShaders;
void __cdecl ProcessImageSpaceShadersHook(NiDX9Renderer* Renderer, BSRenderedTexture* SourceTarget, BSRenderedTexture* DestinationTarget) {
	if (TheShaderManager->Effects.SkinScattering) TheShaderManager->Effects.SkinScattering->Unbind();
	bool bLiveRenderedMenu = false; // FORenderedMenu, FOPipBoyManager
	bool bLive3DMenu = false; // Normal menus, but 3D, lockpick etc
	if (TESMain::IsMenuBackgroundReady() && TheGameMenuManager->IsLiveMenu && InterfaceManager->currentMode != 1) {
		const bool bLockPickMenu = LockPickMenu::GetSingleton() && TheGameMenuManager->IsLiveMenu(Menu::kMenuType_LockPick, false, false) == GameMenuManager::MENU_LIVE;
		const bool bPipBoyLive = InterfaceManager->IsPipBoyOpen() && TheGameMenuManager->IsLiveMenu(Menu::kMenuType_BigFour, false, false) == GameMenuManager::MenuPauseState::MENU_LIVE;
		const bool bRenderedMenuLive = InterfaceManager->pRenderedMenu && TheGameMenuManager->IsLiveMenu(InterfaceManager->menuStack[0], false, false) == GameMenuManager::MenuPauseState::MENU_LIVE;
		bLiveRenderedMenu = bPipBoyLive || bRenderedMenuLive;
		bLive3DMenu = bLockPickMenu;
	}

	if (bLive3DMenu) {
		if (bDoneRender_LockPickMenu) {
			bDoneRender_LockPickMenu = false;
			ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);
			return;
		}
		else {
			bDoneRender_LockPickMenu = true;
		}
	}
	else {
		bDoneRender_LockPickMenu = false;
	}
	
	if (bLiveRenderedMenu) {
		if (!bSkippedRender_RenderedMenu) {
			bSkippedRender_RenderedMenu = true;
			ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);
			return;
		}
		else {
			bSkippedRender_RenderedMenu = false;
		}
	}
	else {
		bSkippedRender_RenderedMenu = false;
	}

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* GameSurface = NULL;
	IDirect3DSurface9* OutputSurface = NULL;
	
	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();
	TheShaderManager->UpdateConstants();

	if (SourceTarget && TheSettingManager->SettingsMain.Main.RenderPreTonemapping) {
		SourceTarget->GetD3DTexture(0)->GetSurfaceLevel(0, &GameSurface); // get the surface from the game render target

		// Disable render state settings that create artefacts
		RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILMASK, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILWRITEMASK, 255, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILREF, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_COLORWRITEENABLE, 15, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_NORMALIZENORMALS, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_POINTSIZE, 810365505, RenderStateArgs); // fix flickering linked to alpha somehow

		TheShaderManager->RenderEffectsPreTonemapping(GameSurface);
	
	}

	ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);

	if (!DestinationTarget && TheRenderManager->currentRTGroup) {
		OutputSurface = TheRenderManager->currentRTGroup->RenderTargets[0]->data->Surface;
		if (!TheSettingManager->SettingsMain.Main.RenderPreTonemapping) TheShaderManager->RenderEffectsPreTonemapping(OutputSurface);
		TheShaderManager->RenderEffects(OutputSurface);
		TheRenderManager->CheckAndTakeScreenShot(OutputSurface, TheSettingManager->SettingsMain.Main.HDRScreenshot);
	}

	if (GameSurface) GameSurface->Release();
}

static void RenderMainMenuMovie() {

	if (TheSettingManager->SettingsMain.Main.ReplaceIntro && InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main))
		TheBinkManager->Render(MainMenuMovie);
	else
		TheBinkManager->Close();

}

CallDetour kRenderInterfaceDetour;
void __fastcall RenderInterfaceHook(void* apThis, void*, void* apCuller, bool abPipboyVisible) {
	RenderMainMenuMovie();
	ImGuiManager::NewFrame();
	ThisCall(kRenderInterfaceDetour.GetOverwrittenAddr(), apThis, apCuller, abPipboyVisible);
	ImGuiManager::Render();
}

static void SetTileShaderConstants() {
	
	float ViewProj[16];
	NiVector4 TintColor = { 1.0f, 1.0f, 1.0f, 0.0f };

	if (InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main)) {
		TheRenderManager->device->GetVertexShaderConstantF(0, ViewProj, 4);
		if ((int)ViewProj[3] == -1 && (int)ViewProj[7] == 1 && (int)ViewProj[15] == 1) TheRenderManager->device->SetPixelShaderConstantF(0, (const float*)&TintColor, 1);
	}

}

__declspec(naked) void SetTileShaderConstantsHook() {

	__asm {
		pushad
		call	SetTileShaderConstants
		popad
		cmp		byte ptr [esi + 0xAC], 0
		jmp		Jumpers::SetTileShaderConstants::Return
	}

}

void* (__thiscall* ShowDetectorWindow)(DetectorWindow*, HWND, HINSTANCE, NiNode*, char*, int, int, int, int) = (void* (__thiscall*)(DetectorWindow*, HWND, HINSTANCE, NiNode*, char*, int, int, int, int))::Hooks::ShowDetectorWindow;
void* __fastcall ShowDetectorWindowHook(DetectorWindow* This, UInt32 edx, HWND Handle, HINSTANCE Instance, NiNode* RootNode, char* FormCaption, int X, int Y, int Width, int Height) {
	
	NiAVObject* Object = NULL;
	void* r = NULL;

	r = (ShowDetectorWindow)(This, Handle, Instance, RootNode, (char*)"Pipeline detector by Alenet", X, Y, 1280, 1024);
	for (int i = 0; i < RootNode->m_children.end; i++) {
		NiNode* Node = (NiNode*)RootNode->m_children.data[i];
		Node->m_children.data[0] = NULL;
		Node->m_children.data[1] = NULL;
		Node->m_children.end = 0;
		Node->m_children.numObjs = 0;
	}
	return r;

}

void DetectorWindowSetNodeName(char* Buffer, int Size, char* Format, char* ClassName, char* Name, float LPosX, float LPosY, float LPosZ) {

	sprintf(Buffer, "%s", Name);

}

static void DetectorWindowCreateTreeView(HWND TreeView) {

	HFONT Font = CreateFontA(14, 0, 0, 0, FW_DONTCARE, NULL, NULL, NULL, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
	SendMessageA(TreeView, WM_SETFONT, (WPARAM)Font, TRUE);
	SendMessageA(TreeView, TVM_SETBKCOLOR, NULL, 0x001E1E1E);
	SendMessageA(TreeView, TVM_SETTEXTCOLOR, NULL, 0x00DCDCDC);

}

__declspec(naked) void DetectorWindowCreateTreeViewHook() {

	__asm {
		pushad
		push	eax
		call	DetectorWindowCreateTreeView
		pop		eax
		popad
		mov     ecx, [ebp - 0x48]
		mov		[ecx + 0x0C], eax
		mov     esp, ebp
		pop     ebp
		jmp		Jumpers::DetectorWindow::CreateTreeViewReturn
	}

}

void DetectorWindowDumpAttributes(HWND TreeView, UInt32 Msg, WPARAM wParam, LPTVINSERTSTRUCTA lParam) {

	TVITEMEXA Item = { NULL };
	char T[260] = { '\0' };

	Item.pszText = T;
	Item.mask = TVIF_TEXT;
	Item.hItem = (HTREEITEM)SendMessageA(TreeView, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)lParam->hParent);
	Item.cchTextMax = 260;
	SendMessageA(TreeView, TVM_GETITEMA, 0, (LPARAM)&Item);
	if (!memcmp(Item.pszText, "Pass", 4))
		SendMessageA(TreeView, TVM_DELETEITEM, 0, (LPARAM)lParam->hParent);
	else
		if (strlen(Item.pszText)) SendMessageA(TreeView, Msg, wParam, (LPARAM)lParam);

}

__declspec(naked) void DetectorWindowDumpAttributesHook() {

	__asm {
		call	DetectorWindowDumpAttributes
		add		esp, 16
		jmp		Jumpers::DetectorWindow::DumpAttributesReturn
	}

}

__declspec(naked) void DetectorWindowConsoleCommandHook() {

	__asm {
		call	DWNode::Create
		jmp		Jumpers::DetectorWindow::ConsoleCommandReturn
	}

}

// Enables culling of muzzle flashes so they don't stay after firing
void __fastcall MuzzleLightCullingFix(MuzzleFlash* This) {
	if (This->light) {
		if (!This->bEnabled) {
			This->light->m_flags |= 1;
		}
		else {
			This->light->m_flags &= ~1;
		}
	}
	ThisCall(0x9BB8A0, This);
}

BSFogProperty* __cdecl ShadowSceneNode__GetFogPropertyEx(UInt32 aeType) {
	if (TheShaderManager->Effects.VolumetricFog->Enabled && !TheShaderManager->GameState.isUnderwater) {
		aeType = 1;  // Use UI scene node and thus render no fog.
	}

	return BSShaderManager::GetShadowSceneNode(static_cast<BSShaderManager::SceneGraphType>(aeType))->fogProperty;
}

NiPoint2* __fastcall WaterFogRemover(NiPoint2* point, void*, float x, float y)
{
	point->x = 0.f;
	point->y = 0.f;
	return point;
}

// Compatibility patch for DXVK 16bits buffer upgrade.
typedef bool(__cdecl* DisableFormatUpgradeFunc)();
typedef bool(__cdecl* EnableFormatUpgradeFunc)();

BSRenderedTexture* (__cdecl* CreateBSRenderedTexture)(BSString*, const UInt32, const UInt32, NiTexture::FormatPrefs*, UInt32, bool, NiDepthStencilBuffer*, UInt32, UInt32) = (BSRenderedTexture * (__cdecl*)(BSString*, const UInt32, const UInt32, NiTexture::FormatPrefs*, UInt32, bool, NiDepthStencilBuffer*, UInt32, UInt32))Hooks::CreateRenderedTexture;
BSRenderedTexture* __cdecl CreateSaveTextureHook(BSString* apName, const UInt32 uiWidth, const UInt32 uiHeight, NiTexture::FormatPrefs* kPrefs, 
	UInt32 eMSAAPref, bool bUseDepthStencil, NiDepthStencilBuffer* pkDSBuffer, UInt32 a7, UInt32 uiBackgroundColor) {
	HMODULE hDLL = GetModuleHandle(L"d3d9.dll");

	// If the loaded library is DXVK-HDR (https://github.com/EndlesslyFlowering/dxvk), these will pass
	DisableFormatUpgradeFunc disable = (DisableFormatUpgradeFunc)GetProcAddress(hDLL, "DXVK_D3D9_HDR_DisableRenderTargetUpgrade");
	EnableFormatUpgradeFunc enable = (DisableFormatUpgradeFunc)GetProcAddress(hDLL, "DXVK_D3D9_HDR_EnableRenderTargetUpgrade");

	if (disable)
		disable(); // Temporarily disable the format upgrade for the texture
	BSRenderedTexture* pTexture = CreateBSRenderedTexture(apName, uiWidth, uiHeight, kPrefs, eMSAAPref, bUseDepthStencil, pkDSBuffer, a7, uiBackgroundColor);
	if (enable)
		enable(); // Restore the format upgrade functionality 

	return pTexture;
}

// 0xE69660
bool __fastcall NiDX9Renderer__Do_EndFrame(NiDX9Renderer* apThis, void*) {
	bool bResult = ThisStdCall<bool>(0xE69660, apThis);

	// Reload effects if queued.
	if (TheShaderManager && TheShaderManager->EffectReloadQueued) {
		TheShaderManager->ReloadEffects();
		TheShaderManager->EffectReloadQueued = false;
	}

	return bResult;
}


// Code to increase all lights strength
//__forceinline NiVector4* GetConstant(int index) {
//	return &((NiVector4*)0x11FA0C0)[index];
//}
//
//__forceinline NiColorAlpha* GetLightConstant(int index) {
//	return reinterpret_cast<NiColorAlpha*>(GetConstant(index));
//}
//
//__forceinline void ScaleColor(NiColorAlpha* Color, float scale) {
//	Color->r *= scale;
//	Color->g *= scale;
//	Color->b *= scale;
//}
//
//void __fastcall ShadowLightShader__UpdateLights(void* apThis, void*, void* apShaderProp, void* apRenderPass, D3DXMATRIX aMatrix, void* apTransform, UInt32 aeRenderPassType, void* apSkinInstance) {
//	ThisCall(0xB78A90, apThis, apShaderProp, apRenderPass, aMatrix, apTransform, aeRenderPassType, apSkinInstance);
	//Logger::Log("scaling light by %f", TheShaderManager->ShaderConst.HDR.PointLightMult);
	//NiColorAlpha* pColor;

	// ambient light is constant 0
	//ScaleColor(GetLightConstant(0), TheShaderManager->ShaderConst.HDR.PointLightMult);

	// pointlight registers go from 0 to 10
	//for (UInt32 i = 0; i < 12; i++) {
	//	ScaleColor(GetLightConstant(i), TheShaderManager->ShaderConst.HDR.PointLightMult);
	//}

	// emittance color is index 27
	//ScaleColor(GetLightConstant(27), TheShaderManager->ShaderConst.HDR.PointLightMult);
//}

