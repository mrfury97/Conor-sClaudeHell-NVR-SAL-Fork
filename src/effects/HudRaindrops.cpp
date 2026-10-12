#include "HudRaindrops.h"

void HudRaindropsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_HudRainData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_HudRainTime", &Constants.Time);
	TheShaderManager->RegisterConstant("TESR_HudRainShade", &Constants.Shade);
	TheShaderManager->RegisterConstant("TESR_HudRainDebug", &Constants.Debug);
	TheShaderManager->RegisterConstant("TESR_HudRainScreen", &Constants.Screen);
}

void HudRaindropsEffect::RegisterTextures() {
	// 32 bits: at 16 the wetness's small steps at high frame rates rounded away (as AutoExposure's).
	// TESR_HudRainState is also what the arms' and weapon's shaders read (s14).
	TheTextureManager->InitTexture("TESR_HudRainState", &Textures.StateTexture, &Textures.StateSurface, 1, 1, D3DFMT_G32R32F);
	TheTextureManager->InitTexture("TESR_HudRainNew", &Textures.NewTexture, &Textures.NewSurface, 1, 1, D3DFMT_G32R32F);
	// The Pip-Boy screen's copy: made at its size the first time it is drawn (DrawOnScreen).
	TheTextureManager->RegisterTexture("TESR_HudRainScreenCopy", (IDirect3DBaseTexture9**)&Textures.ScreenCopy);
	Cleared = false;
}

void HudRaindropsEffect::UpdateSettings() {
	const char* Section = "Shaders.HudRaindrops.Main";
	Settings.DropSize = std::clamp(TheSettingManager->GetSettingF(Section, "DropSize"), 0.5f, 64.0f);
	Settings.Density = std::clamp(TheSettingManager->GetSettingF(Section, "Density"), 0.0f, 1.0f);
	Settings.WetTime = std::clamp(TheSettingManager->GetSettingF(Section, "WetTime"), 0.5f, 600.0f);
	Settings.DryTime = std::clamp(TheSettingManager->GetSettingF(Section, "DryTime"), 0.5f, 600.0f);
	Settings.Refraction = std::clamp(TheSettingManager->GetSettingF(Section, "Refraction"), 0.0f, 4.0f);
	Settings.Reflection = std::clamp(TheSettingManager->GetSettingF(Section, "Reflection"), 0.0f, 8.0f);
	Settings.Gloss = std::clamp(TheSettingManager->GetSettingF(Section, "Gloss"), 0.0f, 1.0f);
	Settings.WetGloss = std::clamp(TheSettingManager->GetSettingF(Section, "WetGloss"), 0.0f, 1.0f);
	Settings.AnimSpeed = std::clamp(TheSettingManager->GetSettingF(Section, "AnimSpeed"), 0.0f, 8.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(Section, "DebugView"), 0, 1);
	Settings.World = TheSettingManager->GetSettingI(Section, "World") != 0;
	Settings.WorldWetTime = std::clamp(TheSettingManager->GetSettingF(Section, "WorldWetTime"), 0.5f, 3600.0f);
	Settings.WorldDryTime = std::clamp(TheSettingManager->GetSettingF(Section, "WorldDryTime"), 0.5f, 3600.0f);
	Settings.WorldFadeEnd = std::clamp(TheSettingManager->GetSettingF(Section, "WorldFadeEnd"), 50.0f, 20000.0f);
	Settings.WorldDroplets = TheSettingManager->GetSettingI(Section, "WorldDroplets") != 0;
	Settings.RippleSize = std::clamp(TheSettingManager->GetSettingF(Section, "RippleSize"), 4.0f, 1024.0f);
	Settings.NpcBeads = TheSettingManager->GetSettingI(Section, "NpcBeads") != 0;
	Settings.NpcDensity = std::clamp(TheSettingManager->GetSettingF(Section, "NpcDensity"), 0.0f, 1.0f);
	Settings.NpcDropSize = std::clamp(TheSettingManager->GetSettingF(Section, "NpcDropSize"), 0.5f, 64.0f);
	Settings.NpcFadeEnd = std::clamp(TheSettingManager->GetSettingF(Section, "NpcFadeEnd"), 50.0f, 20000.0f);
}

void HudRaindropsEffect::UpdateConstants() {
	const float Elapsed = std::clamp((float)TheFrameRateManager->ElapsedTime, 0.0f, 0.25f);
	const auto& State = TheShaderManager->GameState;
	// How much it rains, from the sky itself, not the Precipitations effect (whose animator only runs
	// while that effect is on): the weathers flagged rainy, across a weather transition the outgoing
	// one fading as the incoming one comes in (Sky::weatherPercent, 0-1 toward firstWeather).
	float Rain = 0.0f;
	Sky* WorldSky = Tes ? Tes->sky : nullptr;
	if (State.isExterior && WorldSky) {
		auto Rainy = [](TESWeather* Weather) { return (Weather && (Weather->GetWeatherType() & TESWeather::WeatherType::kType_Rainy)) ? 1.0f : 0.0f; };
		const float Progress = std::clamp(WorldSky->weatherPercent, 0.0f, 1.0f);
		Rain = WorldSky->secondWeather ? Rainy(WorldSky->firstWeather) * Progress + Rainy(WorldSky->secondWeather) * (1.0f - Progress) : Rainy(WorldSky->firstWeather);
	}

	// While it rains, and for DryTime after (the drops could still be drying), the passes run.
	if (Rain > 0.0f) DryingLeft = Settings.DryTime + 1.0f;
	else DryingLeft = max(DryingLeft - Elapsed, 0.0f);
	if (Rain > 0.0f) TheShaderManager->orthoRequired = true;   // the cover test (Effects/HudRaindrops.fx)

	// The world's wetness, from the weather alone: up over WorldWetTime of heavy rain, down over
	// WorldDryTime after it (kept through interiors, for the way back out). Whether a surface is under
	// cover each pixel looks up in the ortho map, which must then be drawn as long as anything is wet.
	RainNow = Settings.DebugView > 0 ? max(Rain, 1.0f) : Rain;
	if (Rain > 0.0f) WorldWet = min(WorldWet + Elapsed * Rain / Settings.WorldWetTime, 1.0f);
	else if (State.isExterior) WorldWet = max(WorldWet - Elapsed / Settings.WorldDryTime, 0.0f);
	WorldOn = Settings.World && State.isExterior && !State.isUnderwater && (WorldWet > 0.0f || Settings.DebugView > 0);
	if (WorldOn) TheShaderManager->orthoRequired = true;

	// The drops' clock runs while it rains, faster in heavy rain (SSS's script slows it in light
	// rain), and stands while they dry. Wrapped on a whole number of both layers' cycles (5 and 10).
	if (Rain > 0.0f || Settings.DebugView > 0) Clock = fmodf(Clock + Elapsed * Settings.AnimSpeed * (0.5f + max(Rain, Settings.DebugView > 0 ? 1.0f : 0.0f)), 1000.0f);

	Constants.Data = D3DXVECTOR4(Rain, 0.0f, 0.0f, 0.0f);
	Constants.Time = D3DXVECTOR4(0.0f, Elapsed, Settings.WetTime, Settings.DryTime);
	Constants.Shade = D3DXVECTOR4(0.0f, 0.0f, State.isExterior ? 0.0f : 1.0f, State.isUnderwater ? 1.0f : 0.0f);
	Constants.Debug = D3DXVECTOR4((float)Settings.DebugView, 0.0f, 0.0f, 0.0f);
	// The screen is about 9 game units across: the drops come out the size they are on the arms.
	Constants.Screen = D3DXVECTOR4(0.5f - 0.45f * Settings.Density, Clock, Settings.Refraction * 0.01f, 9.0f / Settings.DropSize);
}

bool HudRaindropsEffect::ShouldRender() {
	return (DryingLeft > 0.0f || Settings.DebugView > 0) && Textures.StateSurface && Textures.NewSurface;
}

// The wetness eased (technique 0, into NewTexture, reading StateTexture) and kept (1, into
// StateTexture), for the arms' and weapon's shaders next frame.
void HudRaindropsEffect::UpdateWetness(IDirect3DDevice9* Device) {
	if (!Enabled || !Effect || !ShouldRender()) return;
	if (!Cleared) {
		Device->SetRenderTarget(0, Textures.StateSurface);
		Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L);
		Cleared = true;
	}
	Device->SetRenderTarget(0, Textures.NewSurface);
	Render(Device, Textures.NewSurface, nullptr, 0, false, nullptr);
	Device->SetRenderTarget(0, Textures.StateSurface);
	Render(Device, Textures.StateSurface, nullptr, 1, false, nullptr);
}

// For each draw of the game's lit shaders: drops on (c218.x = 1) for first-person geometry while the
// effect runs, off for everything else. A mesh is first-person when it hangs under the player's
// first-person node (as ShadowManager tells the player's own geometry).
void HudRaindropsEffect::SetDrawConstants(NiGeometry* Geometry) {
	// One walk up the mesh's parents: under the player's first-person node (its drops), or under a
	// skeleton (a node named Bip01...: carried by an actor or a creature, as a held or holstered weapon is).
	bool UnderFirstPerson = false;
	bool UnderSkeleton = false;
	if (Enabled && Geometry && Player) {
		for (const NiAVObject* Node = Geometry; Node; Node = Node->m_parent) {
			if (Node == Player->firstPersonNiNode) { UnderFirstPerson = true; break; }
			if (!UnderSkeleton && Node->m_pcName && !strncmp(Node->m_pcName, "Bip01", 5)) UnderSkeleton = true;
		}
	}
	const bool FirstPerson = UnderFirstPerson && Cleared && ShouldRender();
	const bool World = Enabled && WorldOn && !UnderFirstPerson;
	if (!FirstPerson && !World) {   // written every draw: whatever the device held there before is not ours
		static const float Zero[12] = {};
		TheRenderManager->device->SetPixelShaderConstantF(218, Zero, 3);
		DrawSet = false;
		return;
	}
	// The layout of Shaders/Includes/HudRaindrops.hlsl:
	// c218: x the rain now (world draws), y the drops' threshold (Density 1: 0.05, 0: 0.5; nonzero: rain
	//       on this draw), z the clock, w the refraction (the texture coordinates' shift per unit of
	//       slope, in drop texture tiles).
	// c219: x the normal's tilt, y the drops' gloss, z the wet surface's, w the drop tile's size (units).
	// c220: x the world's wetness (-1: first person), y where the world's rain is gone (negative: no
	//       beads), z a ripple tile (units), w 1 for a character: a skinned mesh (NPCs' bodies,
	//       faces, armour and clothes, creatures) gets beads and no ripples.
	// c221-c223 (world draws): the ortho map camera's first three columns.
	// A character (a skinned mesh: NPCs' bodies, faces, armour and clothes, creatures; or anything hung on
	// a skeleton: held and holstered weapons) gets beads and no ripples, with its own settings in the
	// same constants.
	const bool Character = World && (Geometry->skinInstance || UnderSkeleton);
	const float Density = Character ? Settings.NpcDensity : Settings.Density;
	const float Reach = Character ? (Settings.NpcBeads ? Settings.NpcFadeEnd : -Settings.NpcFadeEnd) : (Settings.WorldDroplets ? Settings.WorldFadeEnd : -Settings.WorldFadeEnd);
	float Data[24] = {
		World ? RainNow : 0.0f, 0.5f - 0.45f * Density, Clock, Settings.Refraction * 0.01f,
		Settings.Reflection, Settings.Gloss, Settings.WetGloss, Character ? Settings.NpcDropSize : Settings.DropSize,
		World ? (Settings.DebugView > 0 ? 1.0f : WorldWet) : -1.0f, Reach,
		Settings.RippleSize, Character ? 1.0f : 0.0f,
	};
	if (World) {
		const D3DXMATRIX& Ortho = TheShaderManager->Effects.ShadowsExteriors->ShadowMaps[ShadowsExteriorEffect::MapOrtho].ShadowCameraToLight;
		for (int Column = 0; Column < 3; Column++)
			for (int Row = 0; Row < 4; Row++)
				Data[12 + Column * 4 + Row] = Ortho.m[Row][Column];
	}
	TheRenderManager->device->SetPixelShaderConstantF(218, Data, World ? 6 : 3);
	DrawSet = true;
}

// The menu the game has just rendered into the Pip-Boy's screen texture, copied and drawn back into it
// with the drops (technique 2). Inside the game's own rendering: what this changes on the device (the
// render target, viewport, vertex stream and format) goes back as it was; the effect restores its
// own states at End.
void HudRaindropsEffect::DrawOnScreen(BSRenderedTexture* Target) {
	if (!Enabled || !Effect || !Cleared || !ShouldRender() || !Target || !Target->GetTexture(0)) return;
	IDirect3DTexture9* TargetTexture = Target->GetD3DTexture(0);
	if (!TargetTexture) return;
	IDirect3DDevice9* Device = TheRenderManager->device;
	D3DSURFACE_DESC Desc;
	if (FAILED(TargetTexture->GetLevelDesc(0, &Desc)) || !(Desc.Usage & D3DUSAGE_RENDERTARGET)) return;

	if (!Textures.ScreenCopy || Textures.ScreenWidth != Desc.Width || Textures.ScreenHeight != Desc.Height || Textures.ScreenFormat != Desc.Format) {
		if (Textures.ScreenCopySurface) Textures.ScreenCopySurface->Release();
		if (Textures.ScreenCopy) Textures.ScreenCopy->Release();
		if (Textures.ScreenQuad) Textures.ScreenQuad->Release();
		Textures.ScreenCopy = nullptr; Textures.ScreenCopySurface = nullptr; Textures.ScreenQuad = nullptr;
		if (FAILED(Device->CreateTexture(Desc.Width, Desc.Height, 1, D3DUSAGE_RENDERTARGET, Desc.Format, D3DPOOL_DEFAULT, &Textures.ScreenCopy, NULL))) {
			Textures.ScreenCopy = nullptr;
			return;
		}
		Textures.ScreenCopy->GetSurfaceLevel(0, &Textures.ScreenCopySurface);
		TheShaderManager->CreateFrameVertex(Desc.Width, Desc.Height, &Textures.ScreenQuad);
		Textures.ScreenWidth = Desc.Width; Textures.ScreenHeight = Desc.Height; Textures.ScreenFormat = Desc.Format;
	}
	if (!Textures.ScreenCopySurface || !Textures.ScreenQuad) return;

	IDirect3DSurface9* TargetSurface = nullptr;
	if (FAILED(TargetTexture->GetSurfaceLevel(0, &TargetSurface))) return;
	Device->StretchRect(TargetSurface, NULL, Textures.ScreenCopySurface, NULL, D3DTEXF_NONE);

	IDirect3DSurface9* OldTarget = nullptr;
	D3DVIEWPORT9 OldViewport;
	IDirect3DVertexBuffer9* OldStream = nullptr;
	UINT OldOffset = 0, OldStride = 0;
	DWORD OldFVF = 0;
	Device->GetRenderTarget(0, &OldTarget);
	Device->GetViewport(&OldViewport);
	Device->GetStreamSource(0, &OldStream, &OldOffset, &OldStride);
	Device->GetFVF(&OldFVF);

	Device->SetRenderTarget(0, TargetSurface);
	const D3DVIEWPORT9 Whole = { 0, 0, Desc.Width, Desc.Height, 0.0f, 1.0f };
	Device->SetViewport(&Whole);
	Device->SetStreamSource(0, Textures.ScreenQuad, 0, sizeof(FrameVS));
	Device->SetFVF(FrameFVF);
	try {
		Effect->SetTechnique(Effect->GetTechnique(2));
		SetCT();
		UINT Passes = 0;
		Effect->Begin(&Passes, 0);   // restores the device's states at End
		Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		Device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
		Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		for (UINT p = 0; p < Passes; p++) {
			Effect->BeginPass(p);
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	catch (const std::exception& e) {
		Logger::Log("Error during rendering of effect %s: %s", Name, e.what());
	}

	Device->SetRenderTarget(0, OldTarget);
	Device->SetViewport(&OldViewport);
	Device->SetStreamSource(0, OldStream, OldOffset, OldStride);
	Device->SetFVF(OldFVF);
	if (OldTarget) OldTarget->Release();
	if (OldStream) OldStream->Release();
	TargetSurface->Release();
}

void HudRaindropsEffect::ClearDrawConstants() {
	if (!DrawSet) return;
	static const float Zero[12] = {};
	TheRenderManager->device->SetPixelShaderConstantF(218, Zero, 3);
	DrawSet = false;
}
