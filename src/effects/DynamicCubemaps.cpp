#include <algorithm>

#include "DynamicCubemaps.h"

// Farther than this between two frames, the camera went through a door or a load: what the cube
// holds is somewhere else.
static const float DynamicCubemapJumpDistance = 1500.0f;
// The anchor stored positions are relative to moves to the camera once it is this far away. FP16
// steps grow with the value: within 500 units of the anchor a surface near the camera is stored to
// a quarter or half a unit. (At 2000 the steps were 1-2
// units, enough to fade close surfaces with the camera standing still.)
static const float DynamicCubemapAnchorRange = 500.0f;
// Only materials with an _rmaos map reflect the cube. On frames that draw none it is updated only
// this often, in seconds, so it is not out of date when one comes into view.
static const float DynamicCubemapIdleInterval = 0.5f;
// Faces that point away from everything on screen cannot capture anything new; only their slow,
// distance-based fade changes. They are captured every this many updates and copied in between.
static const UINT DynamicCubemapHiddenFaceInterval = 8;

// Each face's basis in D3D9's cube addressing: direction = Forward + s * Right + t * Up, s and t
// -1..1 across the face from its top left (the sc/tc of the cube face selection, inverted).
static const D3DXVECTOR3 FaceBasis[6][3] = {
	{ D3DXVECTOR3( 1, 0, 0), D3DXVECTOR3( 0, 0,-1), D3DXVECTOR3( 0,-1, 0) },   // +X
	{ D3DXVECTOR3(-1, 0, 0), D3DXVECTOR3( 0, 0, 1), D3DXVECTOR3( 0,-1, 0) },   // -X
	{ D3DXVECTOR3( 0, 1, 0), D3DXVECTOR3( 1, 0, 0), D3DXVECTOR3( 0, 0, 1) },   // +Y
	{ D3DXVECTOR3( 0,-1, 0), D3DXVECTOR3( 1, 0, 0), D3DXVECTOR3( 0, 0,-1) },   // -Y
	{ D3DXVECTOR3( 0, 0, 1), D3DXVECTOR3( 1, 0, 0), D3DXVECTOR3( 0,-1, 0) },   // +Z
	{ D3DXVECTOR3( 0, 0,-1), D3DXVECTOR3(-1, 0, 0), D3DXVECTOR3( 0,-1, 0) },   // -Z
};

// Changing a setting no longer restarts the capture (it used to, on every change anywhere in NVR).
void DynamicCubemapsEffect::UpdateSettings() {
	const char* Section = "Shaders.DynamicCubemaps.Main";
	auto orDefault = [](float v, float d) { return v > 0.0f ? v : d; };
	Tracking.KeepDistance = (std::max)(orDefault(TheSettingManager->GetSettingF(Section, "KeepDistance"), 2000.0f), 100.0f);
	Tracking.DropTime = std::clamp(orDefault(TheSettingManager->GetSettingF(Section, "DropTime"), 1.0f), 0.05f, 30.0f);
	Tracking.StaleHalfLife = (std::max)(TheSettingManager->GetSettingF(Section, "StaleHalfLife"), 0.0f);   // 0: no time fade
	Constants.Debug.x = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.DynamicCubemaps.Main", "DebugView"), 0, 3);
	Constants.Debug.y = std::clamp(TheSettingManager->GetSettingF("Shaders.DynamicCubemaps.Main", "DebugRoughness"), 0.0f, 1.0f) * (Mips - 1);
}

// Only authored PBR materials reflect the cube.
bool DynamicCubemapsEffect::ShouldRender() {
	return TheShaderManager->Shaders.PBR->Enabled;
}

void DynamicCubemapsEffect::UpdateConstants() {
	const bool isExterior = TheShaderManager->GameState.isExterior;
	D3DXVECTOR3 camera(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	D3DXVECTOR3 moved = camera - LastCameraPosition;
	if (isExterior != WasExterior || D3DXVec3Length(&moved) > DynamicCubemapJumpDistance) Reset = true;
	WasExterior = isExterior;
	LastCameraPosition = camera;

	// Indoors, before anything has been seen, the cube holds the room's ambient light: what the
	// objects reflected before the cube existed (Object.hlsl). The object shaders decode it and
	// scale it by AmbientScale; the cube is linear and is taken back to gamma space by them
	// without LinearLighting.
	TheShaderManager->Effects.ShadowsExteriors->UpdateLightColors();
	const D3DXVECTOR4& ambient = TheShaderManager->Effects.ShadowsExteriors->Constants.AmbientLight;
	PBRShaders* PBR = TheShaderManager->Shaders.PBR;
	const float scale = PBR->Constants.Data.w;
	const bool linear = PBR->MaterialSettings.LinearLighting;
	for (int i = 0; i < 3; i++) {
		float c = ((const float*)&ambient)[i];
		((float*)&Constants.Fallback)[i] = linear ? c * c * scale : (c * scale) * (c * scale);
	}
	Constants.Fallback.w = isExterior ? 1.0f : 0.0f;

	Constants.Face.y = 1.0f / Size;

	SinceUpdate += (float)TheFrameRateManager->ElapsedTime;
	Constants.Fade.x = 32.0f;   // walked into: a little past the capture's own minimum distance (24)
	Constants.Fade.y = Tracking.KeepDistance;
	Constants.Motion.w = Tracking.KeepDistance;      // captured farther than this: kept off screen, like the sky
}

// --- Textures -------------------------------------------------------------------------------------

void DynamicCubemapsEffect::ReleaseTextures() {
	for (int c = 0; c < 2; c++) for (int f = 0; f < 6; f++) for (UINT m = 0; m < Mips; m++)
		if (CaptureSurfaces[c][f][m]) { CaptureSurfaces[c][f][m]->Release(); CaptureSurfaces[c][f][m] = nullptr; }
	for (int f = 0; f < 6; f++) for (UINT m = 0; m < Mips; m++) {
		if (InferredSurfaces[f][m]) { InferredSurfaces[f][m]->Release(); InferredSurfaces[f][m] = nullptr; }
		if (EnvSurfaces[f][m]) { EnvSurfaces[f][m]->Release(); EnvSurfaces[f][m] = nullptr; }
	}
	for (int c = 0; c < 2; c++) for (int f = 0; f < 6; f++)
		if (PositionSurfaces[c][f]) { PositionSurfaces[c][f]->Release(); PositionSurfaces[c][f] = nullptr; }
	for (int c = 0; c < 2; c++) if (PositionCube[c]) { PositionCube[c]->Release(); PositionCube[c] = nullptr; }
	for (int c = 0; c < 2; c++) if (CaptureCube[c]) { CaptureCube[c]->Release(); CaptureCube[c] = nullptr; }
	if (Inferred) { Inferred->Release(); Inferred = nullptr; }
	if (Env) { Env->Release(); Env = nullptr; }
	Created = false;
	Valid = false;
}

static bool CreateCube(IDirect3DDevice9* Device, UINT Size, UINT Mips, IDirect3DCubeTexture9** Cube, IDirect3DSurface9* Surfaces[6][DynamicCubemapsEffect::Mips]) {
	if (FAILED(Device->CreateCubeTexture(Size, Mips, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, Cube, NULL))) {
		*Cube = nullptr;
		return false;
	}
	for (UINT f = 0; f < 6; f++) for (UINT m = 0; m < Mips; m++)
		if (FAILED((*Cube)->GetCubeMapSurface((D3DCUBEMAP_FACES)f, m, &Surfaces[f][m]))) return false;
	return true;
}

bool DynamicCubemapsEffect::EnsureTextures(IDirect3DDevice9* Device) {
	if (Created) return true;
	if (Failed) return false;

	bool ok = CreateCube(Device, Size, Mips, &CaptureCube[0], CaptureSurfaces[0])
		&& CreateCube(Device, Size, Mips, &CaptureCube[1], CaptureSurfaces[1])
		&& CreateCube(Device, Size, Mips, &Inferred, InferredSurfaces)
		&& CreateCube(Device, Size, Mips, &Env, EnvSurfaces);
	if (!ok) {
		Logger::Log("[ERROR] DynamicCubemaps: could not create the cubemaps; PBR materials reflect the sky and ambient light instead");
		ReleaseTextures();
		Failed = true;
		return false;
	}

	// Position tracking writes a second render target in the capture pass.
	D3DCAPS9 Caps;
	Device->GetDeviceCaps(&Caps);
	TrackPositions = Caps.NumSimultaneousRTs >= 2;
	for (int c = 0; TrackPositions && c < 2; c++) {
		if (FAILED(Device->CreateCubeTexture(Size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &PositionCube[c], NULL))) {
			PositionCube[c] = nullptr;
			TrackPositions = false;
			break;
		}
		for (UINT f = 0; f < 6; f++)
			if (FAILED(PositionCube[c]->GetCubeMapSurface((D3DCUBEMAP_FACES)f, 0, &PositionSurfaces[c][f]))) TrackPositions = false;
	}
	if (!TrackPositions) Logger::Log("[WARNING] DynamicCubemaps: no second render target; reflections fade by time instead of by position");

	FaceHandle = Effect->GetParameterByName(NULL, "CubeFace");
	FallbackHandle = Effect->GetParameterByName(NULL, "CubeFallback");
	CaptureHandle = Effect->GetParameterByName(NULL, "CubeCapture");
	DebugHandle = Effect->GetParameterByName(NULL, "CubeDebug");
	MotionHandle = Effect->GetParameterByName(NULL, "CubeMotion");
	AnchorShiftHandle = Effect->GetParameterByName(NULL, "CubeAnchorShift");
	FadeHandle = Effect->GetParameterByName(NULL, "CubeFade");
	static const char* BasisNames[6] = { "CubeForward", "CubeRight", "CubeUp", "CubeViewForward", "CubeViewRight", "CubeViewUp" };
	for (int i = 0; i < 6; i++) BasisHandles[i] = Effect->GetParameterByName(NULL, BasisNames[i]);
	Created = true;
	Reset = true;
	Logger::Log("DynamicCubemaps: %u px cubemaps, %u mips, position tracking %s", Size, Mips, TrackPositions ? "on" : "off");
	return true;
}

// --- Rendering ------------------------------------------------------------------------------------

void DynamicCubemapsEffect::DrawFace(IDirect3DDevice9* Device, IDirect3DSurface9* Target, UINT Face, UINT Mip, float Roughness) {
	Device->SetRenderTarget(0, Target);   // also sets the viewport to the face
	Constants.Face.x = (float)Face;
	Constants.Face.y = (float)(1 << Mip) / Size;
	Constants.Face.z = Roughness;
	Effect->SetVector(FaceHandle, &Constants.Face);
	for (int i = 0; i < 3; i++) {
		const D3DXVECTOR4 world(FaceBasis[Face][i].x, FaceBasis[Face][i].y, FaceBasis[Face][i].z, 0.0f);
		Effect->SetVector(BasisHandles[i], &world);
		Effect->SetVector(BasisHandles[3 + i], &ViewBasis[Face][i]);
	}
	Effect->CommitChanges();
	Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
}

// The fades for one face, stepped by the time since that face was last captured.
void DynamicCubemapsEffect::SetFaceFade(UINT Face) {
	const float dt = std::clamp(FaceSince[Face], 0.0f, 1.0f);
	FaceSince[Face] = 0.0f;
	Constants.Fade.z = expf(-dt / Tracking.DropTime);
	Constants.Fade.w = Tracking.StaleHalfLife > 0.0f ? powf(0.5f, dt / Tracking.StaleHalfLife) : 1.0f;
	Constants.Capture.w = powf(0.5f, dt / 2.3f);   // without position tracking: the old fade, halving in 2.3 s
	Effect->SetVector(FadeHandle, &Constants.Fade);
	Effect->SetVector(CaptureHandle, &Constants.Capture);
}

// A box filter: each mip is half the one above it, averaged by linear filtering. The capture is
// premultiplied by its coverage, so this averages only what was seen.
void DynamicCubemapsEffect::DownsampleMips(IDirect3DDevice9* Device, IDirect3DSurface9* Surfaces[6][Mips]) {
	for (UINT f = 0; f < 6; f++)
		for (UINT m = 1; m < Mips; m++)
			Device->StretchRect(Surfaces[f][m - 1], NULL, Surfaces[f][m], NULL, D3DTEXF_LINEAR);
}

// Called on the scene exactly as drawn, with RenderedSurface holding a copy of it. Leaves the
// render target as it found it.
void DynamicCubemapsEffect::RenderCubemaps(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget) {
	if (!Enabled || Effect == nullptr || !ShouldRender()) {
		renderTime = 0.0f;
		Valid = false;
		Reset = true;
		return;
	}
	if (!EnsureTextures(Device)) return;

	// Skipped on frames that drew no material with an _rmaos map: nothing reflects the cube then. A
	// reset (load, door, interior/exterior) or the idle interval still updates it.
	const bool pbrDrawn = PBRDrawn;
	PBRDrawn = false;
	if (Valid && !Reset && !pbrDrawn && SinceUpdate < DynamicCubemapIdleInterval) {
		renderTime = 0.0f;
		return;
	}

	// The fades step by the time since each face was last captured (SetFaceFade), which covers
	// skipped frames and skipped faces.
	for (UINT f = 0; f < 6; f++) FaceSince[f] += SinceUpdate;
	SinceUpdate = 0.0f;

	// This update's camera: each face's basis in view space, and which faces point at anything on
	// screen. A face's directions lie within 54.74 degrees of its axis (to its corners), the screen's
	// within the half-diagonal of the field of view of the camera's forward; a face farther off than
	// both together, plus a margin, sees nothing on screen.
	const D3DXMATRIX& View = TheRenderManager->viewMatrix;
	const D3DXMATRIX& Proj = TheRenderManager->projMatrix;
	D3DXVECTOR3 forward(TheRenderManager->CameraForward.x, TheRenderManager->CameraForward.y, TheRenderManager->CameraForward.z);
	D3DXVec3Normalize(&forward, &forward);
	const float tanX = Proj._11 != 0.0f ? 1.0f / Proj._11 : 1.0f;
	const float tanY = Proj._22 != 0.0f ? 1.0f / Proj._22 : 1.0f;
	const float screenHalfAngle = atanf(sqrtf(tanX * tanX + tanY * tanY));
	const float visibleCos = cosf((std::min)(screenHalfAngle + 0.9553f + 0.1f, D3DX_PI));   // 0.9553: 54.74 degrees
	const bool everyFace = Reset || (Updates++ % DynamicCubemapHiddenFaceInterval) == 0;
	bool captureFace[6];
	for (UINT f = 0; f < 6; f++) {
		for (int i = 0; i < 3; i++) {
			D3DXVECTOR3 v;
			D3DXVec3TransformNormal(&v, &FaceBasis[f][i], &View);
			ViewBasis[f][i] = D3DXVECTOR4(v.x, v.y, v.z, 0.0f);
		}
		captureFace[f] = everyFace || D3DXVec3Dot(&FaceBasis[f][0], &forward) > visibleCos;
	}

	auto timer = TimeLogger();
	const int next = Current ^ 1;

	// The cube may still be bound to the stage the objects sample it from.
	Device->SetTexture(11, NULL);

	// No depth: the scene's may be multisampled, which a plain render target cannot be paired with.
	IDirect3DSurface9* DepthStencil = nullptr;
	Device->GetDepthStencilSurface(&DepthStencil);
	Device->SetDepthStencilSurface(NULL);

	Constants.Capture.y = Reset ? 1.0f : 0.0f;
	Constants.Capture.z = TrackPositions ? 1.0f : 0.0f;

	// Positions are stored relative to Anchor; it follows the camera once it is too far away, and the
	// capture pass shifts every stored position by the difference on that frame.
	const D3DXVECTOR3 camera(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	D3DXVECTOR3 shift(0.0f, 0.0f, 0.0f);
	D3DXVECTOR3 fromAnchor = camera - Anchor;
	if (Reset) Anchor = camera;
	else if (D3DXVec3Length(&fromAnchor) > DynamicCubemapAnchorRange) {
		shift = Anchor - camera;
		Anchor = camera;
	}
	fromAnchor = camera - Anchor;
	Constants.Motion.x = fromAnchor.x;
	Constants.Motion.y = fromAnchor.y;
	Constants.Motion.z = fromAnchor.z;
	Constants.AnchorShift = D3DXVECTOR4(shift.x, shift.y, shift.z, 0.0f);

	Effect->SetTechnique(Effect->GetTechnique(0));
	SetCT();   // the scene, depth and view model mask on s0-s2; the TESR_ constants
	Effect->SetVector(FallbackHandle, &Constants.Fallback);
	Effect->SetVector(CaptureHandle, &Constants.Capture);
	Effect->SetVector(MotionHandle, &Constants.Motion);
	Effect->SetVector(AnchorShiftHandle, &Constants.AnchorShift);
	Effect->SetVector(FadeHandle, &Constants.Fade);

	UINT passes = 0;
	Effect->Begin(&passes, 0);   // restores the device's states at End
	Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

	// 1 Capture into the other cube, reading last frame's; positions likewise, on render target 1,
	// whose write mask others change (skin scattering's MRT draws): set here, put back after.
	DWORD savedWriteMask1 = 0xF;
	Effect->BeginPass(0);
	Device->SetTexture(4, CaptureCube[Current]);
	if (TrackPositions) {
		Device->SetTexture(8, PositionCube[Current]);
		Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &savedWriteMask1);
		Device->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
	}
	for (UINT f = 0; f < 6; f++) {
		if (!captureFace[f]) continue;
		if (TrackPositions) Device->SetRenderTarget(1, PositionSurfaces[next][f]);
		SetFaceFade(f);
		DrawFace(Device, CaptureSurfaces[next][f][0], f, 0, 0.0f);
	}
	Effect->EndPass();
	// Faces not captured this update: last update's colour and positions, copied across.
	for (UINT f = 0; f < 6; f++) {
		if (captureFace[f]) continue;
		Device->StretchRect(CaptureSurfaces[Current][f][0], NULL, CaptureSurfaces[next][f][0], NULL, D3DTEXF_NONE);
		if (TrackPositions) Device->StretchRect(PositionSurfaces[Current][f], NULL, PositionSurfaces[next][f], NULL, D3DTEXF_NONE);
	}
	if (TrackPositions) {
		Device->SetRenderTarget(1, NULL);
		Device->SetTexture(8, NULL);
		Device->SetRenderState(D3DRS_COLORWRITEENABLE1, savedWriteMask1);
	}
	DownsampleMips(Device, CaptureSurfaces[next]);

	// 2 Infer what was never seen.
	Effect->BeginPass(1);
	Device->SetTexture(4, NULL);
	Device->SetTexture(5, CaptureCube[next]);
	for (UINT f = 0; f < 6; f++) DrawFace(Device, InferredSurfaces[f][0], f, 0, 0.0f);
	Effect->EndPass();
	DownsampleMips(Device, InferredSurfaces);

	// 3 Prefilter: mip 0 is the mirror, a copy; each mip below it one roughness step more. Mip 1
	// (roughness 1/8) is the inferred cube's own box-filtered mip 1, copied too: a GGX lobe that
	// narrow (alpha 0.016) is about a texel wide at 128 px, so the box filter is a close match, and
	// mip 1 is three quarters of the prefilter's texels. GGX from mip 2 on.
	for (UINT f = 0; f < 6; f++) {
		Device->StretchRect(InferredSurfaces[f][0], NULL, EnvSurfaces[f][0], NULL, D3DTEXF_NONE);
		Device->StretchRect(InferredSurfaces[f][1], NULL, EnvSurfaces[f][1], NULL, D3DTEXF_NONE);
	}
	Effect->BeginPass(2);
	Device->SetTexture(5, NULL);
	Device->SetTexture(6, Inferred);
	for (UINT m = 2; m < Mips; m++)
		for (UINT f = 0; f < 6; f++) DrawFace(Device, EnvSurfaces[f][m], f, m, (float)m / (Mips - 1));
	Effect->EndPass();
	Device->SetTexture(6, NULL);

	Effect->End();
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetDepthStencilSurface(DepthStencil);
	if (DepthStencil) DepthStencil->Release();

	// The stages this pass bound on the device (SetCT's samplers on 0-2, the cubes on 4-6, 11 cleared)
	// get back what the game's render state has cached for them. The game skips binding a texture its
	// cache says is already there, so a stage left empty here stayed empty for the next draw that
	// wanted the same texture: decals lost their environment map and highlights, depending on which
	// draws happened to rebind those stages first -- and so on the view.
	static const UINT TouchedStages[] = { 0, 1, 2, 4, 5, 6, 8, 11 };
	for (UINT Stage : TouchedStages)
		Device->SetTexture(Stage, TheRenderManager->renderState->GetTexture(Stage));

	Current = next;
	Reset = false;
	Valid = true;
	renderTime = timer.LogTime("DynamicCubemapsEffect::RenderCubemaps");
}

// The debug view (technique Debug in DynamicCubemaps.fx.hlsl), drawn over the finished frame. Uses
// the cubes RenderCubemaps built this frame: Env for the panorama and mirror views, the latest
// capture (CaptureCube[Current]) for coverage.
void DynamicCubemapsEffect::RenderDebug(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget) {
	if (Constants.Debug.x < 0.5f || !Enabled || Effect == nullptr || !Valid || !ShouldRender()) return;
	D3DXHANDLE Technique = Effect->GetTechniqueByName("Debug");
	if (!Technique) return;

	Effect->SetTechnique(Technique);
	SetCT();   // depth and normals buffers, TESR_ camera constants
	Effect->SetVector(DebugHandle, &Constants.Debug);

	Device->SetRenderTarget(0, RenderTarget);
	UINT passes = 0;
	Effect->Begin(&passes, 0);   // restores the device's states at End
	Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	Effect->BeginPass(0);
	Device->SetTexture(5, CaptureCube[Current]);
	Device->SetTexture(7, Env);
	Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	Effect->EndPass();
	Effect->End();

	// As in RenderCubemaps: the stages bound on the device get back what the game's render state caches.
	static const UINT TouchedStages[] = { 0, 1, 2, 3, 5, 7 };
	for (UINT Stage : TouchedStages)
		Device->SetTexture(Stage, TheRenderManager->renderState->GetTexture(Stage));
}
