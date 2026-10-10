#define RESZ_CODE 0x7FA05000

/**
* Initializes the Shader Manager Singleton.
* The Shader Manager creates and holds onto the Effects activated in the Settings Manager, and sets the constants.
*/
void ShaderManager::Initialize() {

	auto timer = TimeLogger();

	Logger::Log("Starting the shaders manager...");
	TheShaderManager = new ShaderManager();
	TheShaderManager->FrameVertex = NULL;

	TheShaderManager->EffectReloadQueued = false;

	memset(TheShaderManager->WaterVertexShaders, NULL, sizeof(WaterVertexShaders));
	memset(TheShaderManager->WaterPixelShaders, NULL, sizeof(WaterPixelShaders));

	TheShaderManager->CreateFrameVertex(TheRenderManager->width, TheRenderManager->height, &TheShaderManager->FrameVertex);

	TheShaderManager->PreviousCell = nullptr;
	TheShaderManager->IsMenuSwitch = false;

	// Make sure the shader/effect cache directories exist.
	std::error_code ec;

	std::filesystem::create_directories("data/Shaders/NewVegasReloaded/Shaders/Cache/Bink", ec);
	if (ec) {
		Logger::Log("Failed to create shader cache directory: %s", ec.message());
	}
	std::filesystem::create_directories("data/Shaders/NewVegasReloaded/Shaders/Cache/Shadows", ec);
	if (ec) {
		Logger::Log("Failed to create shader cache directory: %s", ec.message());
	}
	std::filesystem::create_directories("data/Shaders/NewVegasReloaded/Effects/Cache", ec);
	if (ec) {
		Logger::Log("Failed to create effect cache directory: %s", ec.message());
	}
	std::filesystem::create_directories("Data/Textures/NewVegasReloaded/LUTs", ec);
	if (ec) {
		Logger::Log("Failed to create LUT directory: %s", ec.message());
	}

	// initializing the list of effect names
	TheShaderManager->RegisterEffect<AvgLumaEffect>(&TheShaderManager->Effects.AvgLuma);
	TheShaderManager->RegisterEffect<AmbientOcclusionEffect>(&TheShaderManager->Effects.AmbientOcclusion);
	TheShaderManager->RegisterEffect<SkinScatteringEffect>(&TheShaderManager->Effects.SkinScattering);
	TheShaderManager->RegisterEffect<DynamicCubemapsEffect>(&TheShaderManager->Effects.DynamicCubemaps);
	TheShaderManager->RegisterEffect<BloodLensEffect>(&TheShaderManager->Effects.BloodLens);
	TheShaderManager->RegisterEffect<BloomEffect>(&TheShaderManager->Effects.Bloom);
	TheShaderManager->RegisterEffect<BloomLegacyEffect>(&TheShaderManager->Effects.BloomLegacy);
	TheShaderManager->RegisterEffect<ColoringEffect>(&TheShaderManager->Effects.Coloring);
	TheShaderManager->RegisterEffect<LUTEffect>(&TheShaderManager->Effects.LUT);
	TheShaderManager->RegisterEffect<CinemaEffect>(&TheShaderManager->Effects.Cinema);
	TheShaderManager->RegisterEffect<CombineDepthEffect>(&TheShaderManager->Effects.CombineDepth);
	TheShaderManager->RegisterEffect<DepthOfFieldEffect>(&TheShaderManager->Effects.DepthOfField);
	TheShaderManager->RegisterEffect<DebugEffect>(&TheShaderManager->Effects.Debug);
	TheShaderManager->RegisterEffect<ExposureEffect>(&TheShaderManager->Effects.Exposure);
	TheShaderManager->RegisterEffect<FlashlightEffect>(&TheShaderManager->Effects.Flashlight);
	TheShaderManager->RegisterEffect<FlashlightBeamEffect>(&TheShaderManager->Effects.FlashlightBeam);
	TheShaderManager->RegisterEffect<GodRaysEffect>(&TheShaderManager->Effects.GodRays);
	TheShaderManager->RegisterEffect<ImageAdjustEffect>(&TheShaderManager->Effects.ImageAdjust);
	TheShaderManager->RegisterEffect<LensEffect>(&TheShaderManager->Effects.Lens);
	TheShaderManager->RegisterEffect<LowHFEffect>(&TheShaderManager->Effects.LowHF);
	TheShaderManager->RegisterEffect<MotionBlurEffect>(&TheShaderManager->Effects.MotionBlur);
	TheShaderManager->RegisterEffect<NormalsEffect>(&TheShaderManager->Effects.Normals);
	TheShaderManager->RegisterEffect<RainEffect>(&TheShaderManager->Effects.Rain);
	TheShaderManager->RegisterEffect<SharpeningEffect>(&TheShaderManager->Effects.Sharpening);
	TheShaderManager->RegisterEffect<ShadowsExteriorEffect>(&TheShaderManager->Effects.ShadowsExteriors);
	TheShaderManager->RegisterEffect<ShadowsInteriorsEffect>(&TheShaderManager->Effects.ShadowsInteriors);
	TheShaderManager->RegisterEffect<PointShadowsEffect>(&TheShaderManager->Effects.PointShadows);
	TheShaderManager->RegisterEffect<PointShadows2Effect>(&TheShaderManager->Effects.PointShadows2);
	TheShaderManager->RegisterEffect<SunShadowsEffect>(&TheShaderManager->Effects.SunShadows);
	TheShaderManager->RegisterEffect<SnowEffect>(&TheShaderManager->Effects.Snow);
	TheShaderManager->RegisterEffect<SnowAccumulationEffect>(&TheShaderManager->Effects.SnowAccumulation);
	TheShaderManager->RegisterEffect<UnderwaterEffect>(&TheShaderManager->Effects.Underwater);
	TheShaderManager->RegisterEffect<VolumetricLightEffect>(&TheShaderManager->Effects.VolumetricLight);
	TheShaderManager->RegisterEffect<VolumetricFogEffect>(&TheShaderManager->Effects.VolumetricFog);
	TheShaderManager->RegisterEffect<WaterLensEffect>(&TheShaderManager->Effects.WaterLens);
	TheShaderManager->RegisterEffect<WetWorldEffect>(&TheShaderManager->Effects.WetWorld);
	TheShaderManager->RegisterEffect<DitherBusterEffect>(&TheShaderManager->Effects.DitherBuster);
	TheShaderManager->RegisterEffect<SMAAEffect>(&TheShaderManager->Effects.SMAA);
	TheShaderManager->RegisterEffect<CinematicDOFEffect>(&TheShaderManager->Effects.CinematicDOF);
	TheShaderManager->RegisterEffect<TAAEffect>(&TheShaderManager->Effects.TAA);

	TheShaderManager->RegisterShaderCollection<TonemappingShaders>(&TheShaderManager->Shaders.Tonemapping);
	TheShaderManager->RegisterShaderCollection<POMShaders>(&TheShaderManager->Shaders.POM);
	TheShaderManager->RegisterShaderCollection<PBRShaders>(&TheShaderManager->Shaders.PBR);
	TheShaderManager->RegisterShaderCollection<WaterShaders>(&TheShaderManager->Shaders.Water);
	TheShaderManager->RegisterShaderCollection<SkyShaders>(&TheShaderManager->Shaders.Sky);
	TheShaderManager->RegisterShaderCollection<SkinShaders>(&TheShaderManager->Shaders.Skin);
	TheShaderManager->RegisterShaderCollection<GrassShaders>(&TheShaderManager->Shaders.Grass);
	TheShaderManager->RegisterShaderCollection<TerrainShaders>(&TheShaderManager->Shaders.Terrain);
	TheShaderManager->RegisterShaderCollection<InverseSquareLightingShaders>(&TheShaderManager->Shaders.InverseSquareLighting);
	
	//setup map of constant names
	TheShaderManager->RegisterConstant("TESR_WorldTransform", (D3DXVECTOR4*)&TheRenderManager->worldMatrix);
	TheShaderManager->RegisterConstant("TESR_ViewTransform", (D3DXVECTOR4*)&TheRenderManager->viewMatrix);
	TheShaderManager->RegisterConstant("TESR_InvViewTransform", (D3DXVECTOR4*)&TheRenderManager->InvViewMatrix);
	TheShaderManager->RegisterConstant("TESR_ProjectionTransform", (D3DXVECTOR4*)&TheRenderManager->projMatrix);
	TheShaderManager->RegisterConstant("TESR_InvProjectionTransform",  (D3DXVECTOR4*)&TheRenderManager->InvProjMatrix);
	TheShaderManager->RegisterConstant("TESR_WorldViewProjectionTransform",  (D3DXVECTOR4*)&TheRenderManager->WorldViewProjMatrix);
	TheShaderManager->RegisterConstant("TESR_InvViewProjectionTransform", (D3DXVECTOR4*)&TheRenderManager->InvViewProjMatrix);
	TheShaderManager->RegisterConstant("TESR_ViewProjectionTransform", (D3DXVECTOR4*)&TheRenderManager->ViewProjMatrix);
	TheShaderManager->RegisterConstant("TESR_OcclusionWorldViewProjTransform", (D3DXVECTOR4*)&TheShaderManager->ShaderConst.OcclusionMap.OcclusionWorldViewProj);
	TheShaderManager->RegisterConstant("TESR_LightPosition", (D3DXVECTOR4*) &TheShaderManager->LightPosition);
	TheShaderManager->RegisterConstant("TESR_LightColor", (D3DXVECTOR4*) &TheShaderManager->LightColor);
	TheShaderManager->RegisterConstant("TESR_ContactLampPosition", (D3DXVECTOR4*) &TheShaderManager->ContactLampPosition);
	TheShaderManager->RegisterConstant("TESR_ContactLampColor", (D3DXVECTOR4*) &TheShaderManager->ContactLampColor);
	TheShaderManager->RegisterConstant("TESR_ContactLampAnchor", (D3DXVECTOR4*) &TheShaderManager->ContactLampAnchor);
	TheShaderManager->RegisterConstant("TESR_SpotLightPosition", (D3DXVECTOR4*) &TheShaderManager->SpotLightPosition);
	TheShaderManager->RegisterConstant("TESR_SpotLightColor", (D3DXVECTOR4*) &TheShaderManager->SpotLightColor);
	TheShaderManager->RegisterConstant("TESR_SpotLightDirection", (D3DXVECTOR4*) &TheShaderManager->SpotLightDirection);
	TheShaderManager->RegisterConstant("TESR_SpotLightToWorldTransform", (D3DXVECTOR4*) &TheShaderManager->SpotLightWorldToLightMatrix[0]);
	TheShaderManager->RegisterConstant("TESR_VolumetricData", &TheShaderManager->VolumetricData);
	TheShaderManager->RegisterConstant("TESR_ViewSpaceLightDir", &TheShaderManager->ShaderConst.ViewSpaceLightDir);
	TheShaderManager->RegisterConstant("TESR_ScreenSpaceLightDir", &TheShaderManager->ShaderConst.ScreenSpaceLightDir);
	TheShaderManager->RegisterConstant("TESR_ReciprocalResolution", &TheShaderManager->ShaderConst.ReciprocalResolution);
	TheShaderManager->RegisterConstant("TESR_CameraForward", &TheRenderManager->CameraForward);
	TheShaderManager->RegisterConstant("TESR_DepthConstants", &TheRenderManager->DepthConstants);
	TheShaderManager->RegisterConstant("TESR_CameraData", &TheRenderManager->CameraData);
	TheShaderManager->RegisterConstant("TESR_CameraPosition", &TheRenderManager->CameraPosition);
	TheShaderManager->RegisterConstant("TESR_SunDirection", &TheShaderManager->ShaderConst.SunDir);
	TheShaderManager->RegisterConstant("TESR_SunPosition", &TheShaderManager->ShaderConst.SunPosition);
	TheShaderManager->RegisterConstant("TESR_SunTiming", &TheShaderManager->ShaderConst.SunTiming);
	TheShaderManager->RegisterConstant("TESR_SunAmount", &TheShaderManager->ShaderConst.SunAmount);
	TheShaderManager->RegisterConstant("TESR_GameTime", &TheShaderManager->ShaderConst.GameTime);
	TheShaderManager->RegisterConstant("TESR_FogData", &TheShaderManager->ShaderConst.fogData);
	TheShaderManager->RegisterConstant("TESR_FogDistance", &TheShaderManager->ShaderConst.fogDistance);
	TheShaderManager->RegisterConstant("TESR_FogColor", &TheShaderManager->ShaderConst.fogColor);
	TheShaderManager->RegisterConstant("TESR_SunColor", &TheShaderManager->ShaderConst.sunColor);
	TheShaderManager->RegisterConstant("TESR_SunDiskColor", &TheShaderManager->ShaderConst.sunDiskColor);
	TheShaderManager->RegisterConstant("TESR_SunAmbient", &TheShaderManager->ShaderConst.sunAmbient);
	TheShaderManager->RegisterConstant("TESR_SkyColor", &TheShaderManager->ShaderConst.skyColor);
	TheShaderManager->RegisterConstant("TESR_SkyLowColor", &TheShaderManager->ShaderConst.skyLowColor);
	TheShaderManager->RegisterConstant("TESR_HorizonColor", &TheShaderManager->ShaderConst.horizonColor);

	TheShaderManager->InitializeConstants();

	timer.LogTime("ShaderManager::Initialize");
}

void ShaderManager::CreateFrameVertex(UInt32 Width, UInt32 Height, IDirect3DVertexBuffer9** FrameVertex) {
	
	void* VertexData = NULL;
	float OffsetX = (1.0f / (float)Width) * 0.5f;
	float OffsetY = (1.0f / (float)Height) * 0.5f;
	
	FrameVS FrameVertices[] = {
		{ -1.0f,  1.0f, 1.0f, 0.0f + OffsetX, 0.0f + OffsetY },
		{ -1.0f, -1.0f, 1.0f, 0.0f + OffsetX, 1.0f + OffsetY },
		{  1.0f,  1.0f, 1.0f, 1.0f + OffsetX, 0.0f + OffsetY },
		{  1.0f, -1.0f, 1.0f, 1.0f + OffsetX, 1.0f + OffsetY }
	};
	TheRenderManager->device->CreateVertexBuffer(4 * sizeof(FrameVS), D3DUSAGE_WRITEONLY, FrameFVF, D3DPOOL_DEFAULT, FrameVertex, NULL);
	(*FrameVertex)->Lock(0, 0, &VertexData, NULL);
	memcpy(VertexData, FrameVertices, sizeof(FrameVertices));
	(*FrameVertex)->Unlock();

}


/*
* Initializes and register an effect and its constants
*/
template <typename T> void ShaderManager::RegisterEffect(T** Pointer)
{
	T* effect = new T();
	*Pointer = effect;

	EffectsNames[effect->Name] = (EffectRecord**)Pointer;
	effect->UpdateSettings();
	effect->RegisterConstants();
	effect->RegisterTextures();
	effect->LoadEffect();
}


template <typename T> void ShaderManager::RegisterShaderCollection(T** Pointer)
{
	T* collection = new T();
	*Pointer = collection;
	
	ShaderNames[collection->Name] = (ShaderCollection**)Pointer;
	collection->RegisterConstants();
}


/*
 * Drops the cached texture pointer for a named sampler across EVERY loaded game shader.
 *
 * Counterpart to EffectRecord's per-effect ClearSampler. Call both whenever a TESR_ texture
 * is released and recreated (see ShadowsExteriorEffect::RecreateTextures): each shader owns
 * a private TextureRecord holding a raw IDirect3DTexture9*, which SetCT only re-resolves
 * when it is null. Miss one and it keeps sampling the released texture -- which the D3D9
 * device is usually still holding a reference to, so instead of failing it quietly returns
 * the last contents that were rendered into it.
 */
void ShaderManager::ClearShaderSamplers(const char* TextureName, size_t Length)
{
	// Effects as well as game shaders. This used to walk ShaderNames alone, so effects had to be
	// cleared individually by name at each call site -- and only SunShadows ever was. Any other
	// effect sampling a recreated texture kept its dangling pointer, which the device still
	// references, so it silently went on reading whatever was last rendered into the dead one.
	// VolumetricLight samples TESR_ShadowAtlas and hit exactly that: after any shadow setting
	// change its shafts were carved by a frozen copy of the atlas and no longer matched the
	// scene. Covering every effect here fixes it for all of them rather than adding one more
	// name to a list that has to be maintained by hand.
	for (const auto& Entry : EffectsNames) {
		EffectRecord* Effect = Entry.second ? *Entry.second : nullptr;
		if (Effect) Effect->ClearSampler(TextureName, Length);
	}

	for (const auto& Entry : ShaderNames) {
		ShaderCollection* Collection = Entry.second ? *Entry.second : nullptr;
		if (!Collection) continue;

		for (auto& VertexShader : Collection->VertexShaderList) {
			for (int i = 0; i < 3; i++)  // Default / Exterior / Interior
				if (VertexShader->ShaderProg[i]) VertexShader->ShaderProg[i]->ClearSampler(TextureName, Length);
		}
		for (auto& PixelShader : Collection->PixelShaderList) {
			for (int i = 0; i < 3; i++)
				if (PixelShader->ShaderProg[i]) PixelShader->ShaderProg[i]->ClearSampler(TextureName, Length);
		}
	}
}


void ShaderManager::RegisterConstant(const char* Name, D3DXVECTOR4* FloatValue)
{
	ConstantsTable[Name] = FloatValue;
}


void ShaderManager::InitializeConstants() {

	ShaderConst.pWeather = NULL;

	ShaderConst.ReciprocalResolution.x = 1.0f / (float)TheRenderManager->width;
	ShaderConst.ReciprocalResolution.y = 1.0f / (float)TheRenderManager->height;
	ShaderConst.ReciprocalResolution.z = (float)TheRenderManager->width / (float)TheRenderManager->height;
	ShaderConst.ReciprocalResolution.w = 0.0f; // Reserved to store the FoV
}


/*
Updates the values of the constants that can be accessed from shader code, with values representing the state of the game's elements.
*/
void ShaderManager::UpdateConstants() {

	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle

	auto timer = TimeLogger();

	bool IsThirdPersonView = !TheCameraManager->IsFirstPerson();
	Sky* WorldSky = Tes->sky;
	NiNode* SunRoot = WorldSky->sun->RootNode;
	TESClimate* currentClimate = WorldSky->firstClimate;
	TESWeather* currentWeather = WorldSky->firstWeather;
	TESWeather* previousWeather = WorldSky->secondWeather;
	TESObjectCELL* currentCell = Player->parentCell;
	TESWorldSpace* currentWorldSpace = Player->GetWorldSpace();
	TESRegion* currentRegion = Player->GetRegion();
	float weatherPercent = WorldSky->weatherPercent;
	float lastGameTime = ShaderConst.GameTime.y;
	const char* sectionName = NULL;
	avglumaRequired = false; // toggle for rendering AvgLuma
	orthoRequired = false; // toggle for rendering Ortho map

	if (Effects.Debug->Enabled) avglumaRequired = true;

	// context variables
	GameState.PipBoyIsOn = InterfaceManager->IsPipBoyOpen();
	GameState.VATSIsOn = InterfaceManager->IsActive(Menu::kMenuType_VATS);

	const bool bHasRTM = TheGameMenuManager->IsLiveMenu != nullptr;
	bool bComputersMenu = InterfaceManager->IsActive(Menu::kMenuType_Computers) && (bHasRTM ? (TheGameMenuManager->IsLiveMenu(Menu::kMenuType_Computers, false, false) == GameMenuManager::MENU_PAUSED) : true);
	if (!bComputersMenu)
		bComputersMenu = InterfaceManager->IsActive(Menu::kMenuType_Hacking) && (bHasRTM ? (TheGameMenuManager->IsLiveMenu(Menu::kMenuType_Hacking, false, false) == GameMenuManager::MENU_PAUSED) : true);
	bool bLockpickMenu = LockPickMenu::GetSingleton() && (bHasRTM ? (TheGameMenuManager->IsLiveMenu(Menu::kMenuType_LockPick, false, false) == GameMenuManager::MENU_PAUSED) : true);

	GameState.OverlayIsOn = bComputersMenu || bLockpickMenu ||
		InterfaceManager->IsActive(Menu::kMenuType_Surgery) ||
		InterfaceManager->IsActive(Menu::kMenuType_SlotMachine) ||
		InterfaceManager->IsActive(Menu::kMenuType_Blackjack) ||
		InterfaceManager->IsActive(Menu::kMenuType_Roulette) ||
		InterfaceManager->IsActive(Menu::kMenuType_Caravan);
	GameState.isDialog = InterfaceManager->IsActive(Menu::MenuType::kMenuType_Dialog);
	GameState.isPersuasion = InterfaceManager->IsActive(Menu::MenuType::kMenuType_Persuasion);
	
	if (!currentCell) return; // if no cell is present, we skip update to avoid trying to access values that don't exist
	
	GameState.isExterior = !currentCell->IsInterior();// || Player->parentCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior; // < use exterior flag, broken for now
	GameState.isCellChanged = currentCell != PreviousCell;
	PreviousCell = currentCell;
	if (GameState.isCellChanged) {
		TheSettingManager->SettingsChanged = true; // force update constants during cell transition
		// Preset resolve+apply as early in this function as possible -- before
		// anything below reads SettingsMain, so nothing this frame observes a
		// stale value (docs/preset-manager-design.md § "Application mechanism").
		//
		// Gated on the master toggle and on ShouldReResolve, not on isCellChanged
		// alone: Override presets are assigned per-worldspace outdoors (no
		// per-cell keyword tier exists for exteriors), so re-resolving on every
		// exterior cell border within the same worldspace was a no-op at best
		// and, at worst, silently clobbered any live tweak that hadn't been
		// saved yet the moment the player happened to cross one. ShouldReResolve
		// keeps interiors at today's per-cell granularity and always re-resolves
		// across an interior<->exterior boundary. The short-circuit order
		// matters: ShouldReResolve has side effects (updates its cached "last
		// resolved identity"), so it must not run while the master toggle is
		// off, or re-enabling it later would see a stale-but-matching cache and
		// skip the immediate resolve it needs to do.
		if (TheSettingManager->SettingsMain.Main.PresetManagerEnabled && PresetManager::ShouldReResolve(currentCell))
			PresetManager::ResolveAndApply(currentCell);
	}

	GameState.isUnderwater = Tes->sky->GetIsUnderWater();
	GameState.isRainy = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Rainy : false;
	GameState.isSnow = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Snow : false;
	GameState.isCloudy = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Cloudy : false;

	TimeGlobals* GameTimeGlobals = TimeGlobals::Get();
	float GameHour = fmod(GameTimeGlobals->GameHour->data, 24); // make sure the hours values are less than 24

	float SunriseStart = WorldSky->GetSunriseBegin();
	float SunriseEnd = WorldSky->GetSunriseEnd();
	float SunsetStart = WorldSky->GetSunsetBegin();
	float SunsetEnd = WorldSky->GetSunsetEnd();

	// calculating sun amount for shaders (currently not used by any shaders)
	float sunRise = step(SunriseStart, SunriseEnd, GameHour); // 0 at night to 1 after sunrise
	float sunSet = step(SunsetEnd, SunsetStart, GameHour);  // 1 before sunset to 0 at night
	GameState.isDayTime = sunRise * sunSet;

	ShaderConst.SunTiming.x = WorldSky->GetSunriseColorBegin();
	ShaderConst.SunTiming.y = SunriseEnd;
	ShaderConst.SunTiming.z = SunsetStart;
	ShaderConst.SunTiming.w = WorldSky->GetSunsetColorEnd();

	// fake sunset time tracking with more time given before sunrise/sunset
	float sunRiseLight = step(SunriseStart - 1.0f, SunriseEnd - 1.0f, GameHour); // 0 at night to 1 after sunrise
	float sunSetLight = step(SunsetEnd + 1.0f, SunsetStart + 1.0f, GameHour);  // 1 before sunset to 0 at night
	float newDayLight = sunRiseLight * sunSetLight;
	float transitionPower = max(0.01f, TheSettingManager->SettingsMain.Transitions.TransitionCurvePower);
	GameState.transitionCurve = pow(smoothStep(0.0f, 1.0f, newDayLight), transitionPower); // a curve for day/night transitions that occurs mostly during second half of sunset

	GameState.isDayTimeChanged = (newDayLight != GameState.dayLight);  // allow effects to fire settings update during sunset/sunrise transitions
	GameState.dayLight = newDayLight;

	ShaderConst.GameTime.x = TimeGlobals::GetGameTime(); //time in milliseconds
	ShaderConst.GameTime.y = GameHour; //time in hours
	ShaderConst.GameTime.z = (float)TheFrameRateManager->Time;
	ShaderConst.GameTime.w = TheFrameRateManager->ElapsedTime; // frameTime in seconds

	ShaderConst.SunPosition = SunRoot->m_localTransform.pos.toD3DXVEC4();
	ShaderConst.SunPosition.w = 0.0f;
	// SunRoot (the sun disc's NiNode) is only positioned by the game's own sky-dome rendering,
	// which only runs while an exterior sky is actually drawn. On a fresh process load straight
	// into an interior save, before the player has ever seen an exterior sky this session, it
	// sits at its post-load default -- a zero vector -- and D3DXVec4Normalize of a zero-length
	// vector divides by zero, producing NaN. That NaN then poisons every downstream consumer of
	// SunPosition, notably EvalSky's per-sample radiance in SkyShaders::UpdateConstants (Sky.cpp)
	// -- every one of its 512 integration samples comes out NaN, so all 9 SH sky-irradiance
	// coefficients do too, and PBR Skylighting goes dark (indoors AND out) until the player
	// visits an exterior cell once and the sun disc gets a real position, matching the "only
	// works after having been outdoors" symptom exactly -- and matches SkyDebug's logged
	// Irradiance[0]=(nan,nan,nan) on a cold interior load.
	if (D3DXVec4LengthSq(&ShaderConst.SunPosition) > 0.0001f)
		D3DXVec4Normalize(&ShaderConst.SunPosition, &ShaderConst.SunPosition);
	else
		ShaderConst.SunPosition = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f); // straight up: a safe, neutral default
	ShaderConst.SunPosition.w = 1.0f;

	ShaderConst.SunDir = Tes->directionalLight->direction.toD3DXVEC4() * -1.0f;
	ShaderConst.SunDir.w = 0.0f;
	D3DXVec4Normalize(&ShaderConst.SunDir, &ShaderConst.SunDir);
	ShaderConst.SunDir.w = 1.0f;

	// during the day, track the sun mesh position instead of the lighting direction in exteriors
	if (GameState.isExterior && GameState.dayLight > 0.5)
		ShaderConst.SunDir = ShaderConst.SunPosition;
	else
		ShaderConst.SunPosition.z = -ShaderConst.SunPosition.z;


	// expose the light vector in view space for screen space lighting
	D3DXVec4Transform(&ShaderConst.ScreenSpaceLightDir, &ShaderConst.SunDir, &TheRenderManager->ViewProjMatrix);
	D3DXVec4Normalize(&ShaderConst.ScreenSpaceLightDir, &ShaderConst.ScreenSpaceLightDir);

	D3DXVec4Transform(&ShaderConst.ViewSpaceLightDir, &ShaderConst.SunDir, &TheRenderManager->ViewMatrix);
	D3DXVec4Normalize(&ShaderConst.ViewSpaceLightDir, &ShaderConst.ViewSpaceLightDir);

	ShaderConst.sunGlare = currentWeather ? (currentWeather->GetSunGlare() / 255.0f) : 0.5f;

	ShaderConst.SunAmount.y = GameState.isDayTime; // accurate 0 - 1 value based on weather transition times
	GameState.isDayTime = smoothStep(0, 1, GameState.dayLight); // smooth daytime progression -- more accurate to light changes
	ShaderConst.SunAmount.x = GameState.isDayTime;

	ShaderConst.sunColor.x = WorldSky->sunDirectional.r;
	ShaderConst.sunColor.y = WorldSky->sunDirectional.g;
	ShaderConst.sunColor.z = WorldSky->sunDirectional.b;
	ShaderConst.sunColor.w = ShaderConst.sunGlare;

	if (Shaders.Sky->useSunDiskColor) {
		// experimental color used for more sky tinting capabilities
		ShaderConst.sunDiskColor.x = WorldSky->SunColor.r;
		ShaderConst.sunDiskColor.y = WorldSky->SunColor.g;
		ShaderConst.sunDiskColor.z = WorldSky->SunColor.b;
		ShaderConst.sunDiskColor.w = 1.0;
	}
	else {
		ShaderConst.sunDiskColor = ShaderConst.sunColor; // override with the color of the lighting
	}

	ShaderConst.windSpeed = WorldSky->windSpeed;

	ShaderConst.fogColor.x = WorldSky->fogColor.r;
	ShaderConst.fogColor.y = WorldSky->fogColor.g;
	ShaderConst.fogColor.z = WorldSky->fogColor.b;
	ShaderConst.fogColor.w = 1.0f;

	ShaderConst.horizonColor.x = WorldSky->Horizon.r;
	ShaderConst.horizonColor.y = WorldSky->Horizon.g;
	ShaderConst.horizonColor.z = WorldSky->Horizon.b;
	ShaderConst.horizonColor.w = 1.0f;

	ShaderConst.sunAmbient.x = WorldSky->sunAmbient.r;
	ShaderConst.sunAmbient.y = WorldSky->sunAmbient.g;
	ShaderConst.sunAmbient.z = WorldSky->sunAmbient.b;
	ShaderConst.sunAmbient.w = 1.0f;

	ShaderConst.skyLowColor.x = WorldSky->SkyLower.r;
	ShaderConst.skyLowColor.y = WorldSky->SkyLower.g;
	ShaderConst.skyLowColor.z = WorldSky->SkyLower.b;
	ShaderConst.skyLowColor.w = 1.0f;

	ShaderConst.skyColor.x = WorldSky->skyUpper.r;
	ShaderConst.skyColor.y = WorldSky->skyUpper.g;
	ShaderConst.skyColor.z = WorldSky->skyUpper.b;
	ShaderConst.skyColor.w = 1.0f;

	// replicate vanilla behavior of enforcing max fog distance in interiors
	ShaderConst.fogData.y = WorldSky->fogFarPlane;
	if (!GameState.isExterior && (WorldSky->fogFarPlane < 0 || WorldSky->fogFarPlane > 163840)) {
		ShaderConst.fogData.y = 163840;
	}

	// for near plane, ensure that far > near
	ShaderConst.fogData.x = WorldSky->fogNearPlane;
	if (WorldSky->fogNearPlane < 0 || ShaderConst.fogData.y < WorldSky->fogNearPlane)
		ShaderConst.fogData.x = ShaderConst.fogData.y * 0.17;

	ShaderConst.fogData.z = ShaderConst.sunGlare;
	ShaderConst.fogData.w = WorldSky->fogPower;

	ShaderConst.fogDistance.x = ShaderConst.fogData.x;
	ShaderConst.fogDistance.y = ShaderConst.fogData.y;
	ShaderConst.fogDistance.z = 1.0f;
	ShaderConst.fogDistance.w = ShaderConst.sunGlare;

	timer.LogTime("ShaderManager::UpdateConstants for generic constants");

	if (TheSettingManager->SettingsChanged) {
		// TheGameMenuManager->UpdateSettings(); — replaced by ImGui overlay

		// update settings
		for (const auto [Name, effect] : EffectsNames) {
			(*effect)->UpdateSettings();
		}
		for (const auto [Name, shader] : ShaderNames) {
			(*shader)->UpdateSettings();
		}

		// sky settings are used in several shaders whether the shader is active or not
		ShaderConst.SunAmount.w = TheSettingManager->GetSettingF("Shaders.Sky.Main", "GlareStrength");
		timer.LogTime("ShaderManager::UpdateSettings for shaders & effects");
	}

	// update Constants
	for (const auto [Name, shader] : ShaderNames) {
		if ((*shader)->Enabled) (*shader)->UpdateConstants();
	}
	timer.LogTime("ShaderManager::UpdateConstants for shaders");

	for (const auto [Name, effect] : EffectsNames) {
		if ((*effect)->Enabled) {
			(*effect)->UpdateConstants();
			(*effect)->constantUpdateTime = timer.LogTime((*effect)->Name);
		}
		else {
			(*effect)->constantUpdateTime = 0;
		}
	}

	// The skin, hair and grass shaders route their light and ambient terms through
	// TESR_PBRData (see Shaders/Includes/PBRScale.hlsl) and render black at a zero scale, so
	// these constants stay current whether or not the PBR collection is enabled.
	if (!Shaders.PBR->Enabled) Shaders.PBR->UpdateConstants();
	// Off, the lamps' falloff goes back to vanilla: its constant says so (Shaders/Includes/InverseSquare.hlsl).
	if (!Shaders.InverseSquareLighting->Enabled) Shaders.InverseSquareLighting->UpdateConstants();

	// Underwater effect uses constants from the water shader
	if (Effects.Underwater->Enabled && !Shaders.Water->Enabled) Shaders.Water->UpdateConstants();
	if (!Effects.ShadowsExteriors->Enabled && Effects.ShadowsInteriors->Enabled) Effects.ShadowsExteriors->UpdateConstants(); // Interior and exterior shadows share settings

	TheSettingManager->SettingsChanged = false;
	timer.LogTime("ShaderManager::UpdateConstants");
}


float ShaderManager::GetTransitionValue(float Day, float Night, float Interior) {
	if (GameState.isExterior) {
		return std::lerp(Night, Day, GameState.transitionCurve);
	}
	else {
		return Interior;
	}
}


ShaderCollection* ShaderManager::GetShaderCollection(const char* Name) {

	if (!memcmp(Name, "WATER", 5)) return Shaders.Water;
	if (!memcmp(Name, "GRASS", 5)) return Shaders.Grass;
	if (!memcmp(Name, "ISHDR", 5) || !memcmp(Name, "HDR", 3)) return Shaders.Tonemapping; // tonemapping shaders have different names between New vegas and Oblivion
	if (!memcmp(Name, "PAR", 3)) return Shaders.POM;
	if (!memcmp(Name, "SKIN", 4)) return Shaders.Skin;
	// Hair (BSSM_3XLIGHTING_*) lives in the SM3 family, not HAIR*. Only SM3003 has a
	// replacement on disk; the rest resolve to no file and fall through to vanilla.
	// The decal shaders (SM3004.vso, SM3005/SM3007.pso: blood, bullet holes) stay the game's own,
	// as every decal does (VanillaDecals, Hooks/Shaders.cpp): replaced, they flickered in interiors.
	if (!memcmp(Name, "SM3004", 6) || !memcmp(Name, "SM3005", 6) || !memcmp(Name, "SM3007", 6)) return NULL;
	if (!memcmp(Name, "SM3", 3)) return Shaders.PBR;
	// SpeedTree leaves. STLEAF001/003.vso are vs_3_0 replacements, so every leaf PS must have
	// a ps_3_0 replacement too: D3D9 rejects a 2.x VS paired with a 3.0 PS.
	if (!memcmp(Name, "STLEAF", 6)) return Shaders.PBR;
	if (!memcmp(Name, "SKY", 3)) return Shaders.Sky;
	if (strstr(BloodShaders, Name)) return Shaders.Blood;

	if (Shaders.PBR->GetTemplate(Name).Name != NULL) return Shaders.PBR;
	if (Shaders.Terrain->GetTemplate(Name).Name != NULL) return Shaders.Terrain;

	return NULL;
}

/*
* Reload all effects.
*/
void ShaderManager::ReloadEffects() {
	for (const auto [Name, effect] : EffectsNames) {
		(*effect)->DisposeEffect();
		(*effect)->LoadEffect();
	}
}

/*
* Load generic Vertex Shaders as well as the ones for interiors and exteriors if the exist. 
* Returns false if generic one isn't found (as other ones are optional)
*/
bool ShaderManager::LoadShader(NiD3DVertexShader* Shader) {
	
	NiD3DVertexShaderEx* VertexShader = (NiD3DVertexShaderEx*)Shader;
	ShaderCollection* Collection = GetShaderCollection(VertexShader->Name);

	if (!Collection) {
		VertexShader->ShaderProg[ShaderRecordType::Default] = NULL;
		VertexShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
		VertexShader->ShaderProg[ShaderRecordType::Interior] = NULL;
		VertexShader->Enabled = false;
		return false;
	}
	
	bool enabled = Collection->Enabled;

	ShaderTemplate Template = Collection->GetTemplate(VertexShader->Name);

	// Load generic, interior and exterior shaders
	VertexShader->ShaderProg[ShaderRecordType::Default]  = (ShaderRecordVertex*)ShaderRecord::LoadShader(VertexShader->Name, NULL, Template);
	VertexShader->ShaderProg[ShaderRecordType::Exterior] = (ShaderRecordVertex*)ShaderRecord::LoadShader(VertexShader->Name, "Exteriors\\", Template);
	VertexShader->ShaderProg[ShaderRecordType::Interior] = (ShaderRecordVertex*)ShaderRecord::LoadShader(VertexShader->Name, "Interiors\\", Template);
	VertexShader->Enabled = enabled;

	if (VertexShader->ShaderProg[ShaderRecordType::Default] != nullptr || VertexShader->ShaderProg[ShaderRecordType::Exterior] != nullptr || VertexShader->ShaderProg[ShaderRecordType::Interior] != nullptr) {
		Collection->VertexShaderList.push_back(VertexShader);
		Logger::Log("Loaded %s Vertex Shader %s", Collection->Name, VertexShader->Name);
	}

	return enabled;
}


/*
* Load generic Pixel Shaders as well as the ones for interiors and exteriors if the exist. 
* Returns false if generic one isn't found (as other ones are optional)
*/
bool ShaderManager::LoadShader(NiD3DPixelShader* Shader) {

	NiD3DPixelShaderEx* PixelShader = (NiD3DPixelShaderEx*)Shader;
	ShaderCollection* Collection = GetShaderCollection(PixelShader->Name);

	if (!Collection) {
		PixelShader->ShaderProg[ShaderRecordType::Default] = NULL;
		PixelShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
		PixelShader->ShaderProg[ShaderRecordType::Interior] = NULL;
		PixelShader->Enabled = false;
		return false;
	}

	bool enabled = Collection->Enabled;

	ShaderTemplate Template = Collection->GetTemplate(PixelShader->Name);

	PixelShader->ShaderProg[ShaderRecordType::Default]  = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, NULL, Template);
	PixelShader->ShaderProg[ShaderRecordType::Exterior] = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, "Exteriors\\", Template);
	PixelShader->ShaderProg[ShaderRecordType::Interior] = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, "Interiors\\", Template);
	PixelShader->Enabled = enabled;

	if (PixelShader->ShaderProg[ShaderRecordType::Default] != nullptr || PixelShader->ShaderProg[ShaderRecordType::Exterior] != nullptr || PixelShader->ShaderProg[ShaderRecordType::Interior] != nullptr) {
		Collection->PixelShaderList.push_back(PixelShader);
		Logger::Log("Loaded %s Pixel Shader %s", Collection->Name, PixelShader->Name);
	}

	return enabled;
}


void ShaderManager::GetNearbyLights(ShadowSceneLight* ShadowLightsList[], NiPointLight* LightsList[], NiSpotLight* SpotLightList[]) {
	//Logger::Log(" ==== Getting lights ====");
	auto timer = TimeLogger();

	// create a map of all nearby valid lights and sort them per distance to player
	std::multimap<int, ShadowSceneLight*> SceneLights;   // a multimap: two lamps at the same distance used to overwrite each other
	NiTList<ShadowSceneLight>::Entry* Entry = SceneNode->lights.start;

	ShadowsExteriorEffect::InteriorsStruct* Settings = &Effects.ShadowsExteriors->Settings.Interiors;
	ShadowsExteriorEffect::ShadowStruct* ShadowsConstants = &Effects.ShadowsExteriors->Constants;

	std::vector<ShadowSceneLight*>& PresentLights = PresentLightsScratch;   // every scene light, any state
	PresentLights.clear();

	// Creating list of lights in order of distance to the player
	while (Entry) {
		NiPointLight* Light = Entry->data->sourceLight;
		PresentLights.push_back(Entry->data);
		D3DXVECTOR4 LightPosition = Light->m_worldTransform.pos.toD3DXVEC4();

		bool lightCulled = Light->m_flags & NiAVObject::NiFlags::APP_CULLED;
		bool lightOn = (Light->Diff.r + Light->Diff.g + Light->Diff.b) * Light->Dimmer > 5.0 / 255.0; // Check for low values in case of human error
		if (lightCulled || !lightOn) {
			Entry = Entry->next;
			continue;
		}

		float Distance = Light->GetDistance(&Player->pos);
		float radius = Light->Spec.r * Settings->LightRadiusMult;

		// Drop only lights whose whole sphere is behind the camera: nothing on screen can be lit by
		// them. This used to test the light's CENTRE against the camera's facing, from the player's
		// position, so a lamp just behind or beside the camera that still lit the walls in view was
		// dropped as the view turned, and its light and highlights vanished at once.
		const D3DXVECTOR3 CameraToLight(LightPosition.x - TheRenderManager->CameraPosition.x,
			LightPosition.y - TheRenderManager->CameraPosition.y, LightPosition.z - TheRenderManager->CameraPosition.z);
		const D3DXVECTOR3 Forward(TheRenderManager->CameraForward.x, TheRenderManager->CameraForward.y, TheRenderManager->CameraForward.z);
		bool inFront = D3DXVec3Dot(&CameraToLight, &Forward) > -radius;

		// select lights that will be tracked by removing culled lights and lights entirely behind the camera
		float drawDistance = 8000;//TheShaderManager->GameState.isExterior ? TheSettingManager->SettingsShadows.Exteriors.ShadowMapRadius[TheShadowManager->ShadowMapTypeEnum::MapLod] : TheSettingManager->SettingsShadows.Interiors.DrawDistance;
		if ((inFront || Distance < radius) && (Distance + radius) < drawDistance) {
			SceneLights.emplace((int)(Distance * 10000), Entry->data); // distance x 10000 as the key, to sort by
		}

		Entry = Entry->next;
	}

	// save only the n first lights (based on #define TrackedLightsMax)
	memset(&TheShaderManager->LightPosition, 0, TrackedLightsMax * sizeof(D3DXVECTOR4)); // clear previous lights from array
	// Must be cleared. The fill loop below only zeroes trailing slots once it runs out of scene
	// lights; with more lights than slots it never reaches that branch, and a slot left holding
	// last frame's position keeps GetPointLightAmount sampling a cubemap nobody redraws.
	memset(&ShadowsConstants->ShadowLightPosition, 0, ShadowCubeMapsMax * sizeof(D3DXVECTOR4));
	memset(&TheShaderManager->LightColor, 0, (TrackedLightsMax + ShadowCubeMapsMax) * sizeof(D3DXVECTOR4)); // clear previous lights from array

	// ShadowManager::RenderShadowMaps only renders cubemaps for the first LightPoints slots.
	// Filling past that gives the shader a live position and colour for a face that is never
	// redrawn, so it samples whatever that cubemap last held -- a shadow frozen from an earlier
	// frame or cell. Lights beyond the cap fall through to the non-shadowing tracked list.
	// Past ShadowCubeMapsMax only with the object shaders' lamp shadows (indoors), as far as the
	// atlas has room (sized at startup): the post-process and the other effects see 12.
	const bool ForwardSlots = Effects.ShadowsExteriors->Constants.PointShadowData.x > 0.0f;
	const int ShadowLightsMax = min(Settings->LightPoints, ForwardSlots ? (int)Effects.ShadowsExteriors->Textures.PointShadowAtlasSlots : (int)ShadowCubeMapsMax);
	TheShadowManager->SlotCount = ShadowLightsMax;

	// get the data for all tracked lights
	int LightIndex = 0;
	TheShadowManager->PointLightsNum = 0;

#if defined(OBLIVION)
	bool TorchOnBeltEnabled = TheSettingManager->SettingsMain.EquipmentMode.Enabled && TheSettingManager->SettingsMain.EquipmentMode.TorchKey != 255;
#endif

	D3DXVECTOR4 Empty = D3DXVECTOR4(0, 0, 0, 0);

	// TEMP : get data for spotlights. Right now, is done manually since spotlights aren't implemented in the engine
	// FlashlightEffect::UpdateConstants publishes SpotLightPosition/Direction/Color itself,
	// so the cone constants and the cookie matrix always come from one read of the bone.
	TheShaderManager->Effects.Flashlight->UpdateConstants();
	if (TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive) {
		SpotLightList[0] = TheShaderManager->Effects.Flashlight->SpotLight;
	}
	else {
		SpotLightList[0] = nullptr;
	}

	// Shadow-casting lamps keep their cubemap slot from frame to frame (ShadowManager caches each
	// slot's cubemap while nothing it sees moves). The slots used to be refilled in distance order
	// every frame, so walking past a lamp moved every farther lamp to another slot: shadows popped,
	// and no slot could keep its map. Now the LightPoints lamps lighting the player's surroundings
	// most (Score) always have a slot, and a lamp keeps its slot while it stays among the first
	// LightPoints + SlotMargin, so two lamps close in rank no longer trade it back and forth as the
	// player moves.
	//
	// A lamp holding a slot that drops out of the lights gathered above -- culled by the game's room
	// and portal system, a flickering lamp dipping dark for a frame, its sphere passing behind the
	// camera, or out of range -- keeps it for SlotGrace seconds while it still exists: such lamps
	// came back a moment later, and losing and regaining the slot made their shadows blink on and
	// off and handed the slot (and a redraw) to another lamp each time. While inactive it lights
	// nothing on screen, so keeping its shadow costs nothing visible; a lamp that needs the slot
	// takes it first.
	//
	// Lamps closer together than LightClusterRadius share one slot and one shadow, drawn from their
	// centre: a chandelier's bulbs, a pair of sconces. The Prospector Saloon gathers 45 shadow-casting
	// lamps for 12 slots; the nearest 12 changed every few steps, and shadows came and went. Clusters
	// are built from the lamps alone, in a fixed order (their addresses), so they do not change as
	// the player moves. A cluster is known by its key lamp (its widest), lights as its active lamps
	// summed, and its cube draws what any of its lamps lights (ShadowManager::RenderShadowCubeMap).
	// What follows says lamp for a cluster.
	static NiPointLight* SlotLights[ShadowSlotsMax] = {};   // each slot's cluster's key lamp
	static float SlotIdle[ShadowSlotsMax] = {};             // seconds the holder has been inactive
	// A slot's shadow fades in over SlotFadeTime when a cluster gets it, and out before the cluster
	// gives it up (it keeps the slot until its shadow is gone): shadows no longer pop on and off as
	// clusters trade slots.
	static float SlotFade[ShadowSlotsMax] = {};
	static bool SlotLeaving[ShadowSlotsMax] = {};
	const float SlotFadeTime = 0.5f;
	const int SlotMargin = 4;
	const float SlotGrace = 1.5f;
	const int InactiveRank = 1 << 20;   // ranks after every active cluster: evicted first
	const float FrameTime = (float)TheFrameRateManager->ElapsedTime;
	const float FadeStep = FrameTime / SlotFadeTime;
	const float ClusterRadius = max(Settings->LightClusterRadius, 0.0f);
	ShadowManager* SlotStats = TheShadowManager;

	auto IsShadowCaster = [&](NiPointLight* Light) {
		if (!Light || Light->EffectType != NiDynamicEffect::EffectTypes::POINT_LIGHT) return false;
		//bool CastShadow = Settings->UseCastShadowFlag ? Light->CastShadows : true; // Flag is broken by JIP
#if defined(OBLIVION)
		// Oblivion exception for carried torch lights
		if (TorchOnBeltEnabled && Light->CanCarry == 2) {
			HighProcessEx* Process = (HighProcessEx*)Player->process;
			if (Process->OnBeltState == HighProcessEx::State::In) return false;
		}
#endif
		return Light->Spec.r * Settings->LightRadiusMult > 10;
	};

	// The active shadow casters (gathered above: on, not culled, in range, not wholly behind the
	// camera), nearest first.
	std::vector<ShadowSceneLight*>& Ranked = RankedLightsScratch;
	Ranked.clear();
	for (auto& Entry : SceneLights)
		if (IsShadowCaster(Entry.second->sourceLight)) Ranked.push_back(Entry.second);
	auto RankOf = [&Ranked](const NiPointLight* Light) {
		for (int r = 0; r < (int)Ranked.size(); r++)
			if (Ranked[r]->sourceLight == Light) return r;
		return -1;
	};

	// Every shadow caster in the scene, active or not, clustered greedily in address order: each lamp
	// not yet taken starts a cluster and takes the lamps within ClusterRadius of it.
	std::vector<ShadowSceneLight*>& Casters = EligibleLightsScratch;
	Casters.clear();
	for (ShadowSceneLight* SceneLight : PresentLights)
		if (SceneLight && IsShadowCaster(SceneLight->sourceLight)) Casters.push_back(SceneLight);
	std::sort(Casters.begin(), Casters.end());
	Casters.erase(std::unique(Casters.begin(), Casters.end()), Casters.end());

	// Lamps that move stay out of clusters: carried ones, any hung on the player (the Pip-Boy light),
	// and any found more than 16 units from where it was first seen (a companion's torch, a lamp on
	// something moving; flickering lamps wander less). In a cluster, a lamp walking with the player
	// dragged the cluster's centre along, and with it the shadow of every fixed lamp in it: the cube
	// was drawn again from the new centre each time it passed 6 units, and the shadows followed the
	// player in steps.
	auto UnderNode = [](const NiAVObject* Object, const NiNode* Root) {
		if (!Root) return false;
		for (const NiAVObject* Node = Object; Node; Node = Node->m_parent)
			if (Node == Root) return true;
		return false;
	};
	if (LampFirstSeen.size() > 4096) LampFirstSeen.clear();   // lamps long gone
	std::vector<char>& Mobile = ClusterMobileScratch;
	Mobile.assign(Casters.size(), 0);
	for (size_t i = 0; i < Casters.size(); i++) {
		NiPointLight* Light = Casters[i]->sourceLight;
		const D3DXVECTOR3 P = Light->m_worldTransform.pos.toD3DXVEC3();
		auto Seen = LampFirstSeen.find(Light);
		if (Seen == LampFirstSeen.end()) Seen = LampFirstSeen.emplace(Light, P).first;
		const D3DXVECTOR3 Moved = P - Seen->second;
		Mobile[i] = Light->CanCarry || (Player && (UnderNode(Light, Player->GetNode()) || UnderNode(Light, Player->firstPersonNiNode)))
			|| D3DXVec3LengthSq(&Moved) > 16.0f * 16.0f;
	}

	std::vector<ShadowLampCluster>& Clusters = ClustersScratch;
	std::vector<ShadowSceneLight*>& ClusterLamps = ClusterLampsScratch;
	std::vector<char>& Taken = ClusterTakenScratch;
	Clusters.clear();
	ClusterLamps.clear();
	Taken.assign(Casters.size(), 0);
	for (size_t i = 0; i < Casters.size(); i++) {
		if (Taken[i]) continue;
		ShadowLampCluster Cluster = {};
		Cluster.Colour = D3DXVECTOR3(0.0f, 0.0f, 0.0f);   // D3DXVECTOR3's constructor leaves it unset, = {} or not
		Cluster.First = (UInt32)ClusterLamps.size();
		const NiPoint3& Seed = Casters[i]->sourceLight->m_worldTransform.pos;
		Cluster.Mobile = Mobile[i] != 0;
		for (size_t j = i; j < Casters.size(); j++) {
			if (Taken[j]) continue;
			if (j != i && (Cluster.Mobile || Mobile[j])) continue;   // a moving lamp is a cluster of its own
			const NiPoint3& P = Casters[j]->sourceLight->m_worldTransform.pos;
			const float dx = P.x - Seed.x, dy = P.y - Seed.y, dz = P.z - Seed.z;
			if (j != i && dx * dx + dy * dy + dz * dz > ClusterRadius * ClusterRadius) continue;
			Taken[j] = 1;
			ClusterLamps.push_back(Casters[j]);
		}
		Cluster.Count = (UInt32)ClusterLamps.size() - Cluster.First;

		// Centre: the lamps' mean. Radius: reaching every lamp's sphere from there. Key: the widest.
		D3DXVECTOR3 Sum(0.0f, 0.0f, 0.0f);
		for (UInt32 k = 0; k < Cluster.Count; k++)
			Sum += ClusterLamps[Cluster.First + k]->sourceLight->m_worldTransform.pos.toD3DXVEC3();
		Cluster.Centre = Sum / (float)Cluster.Count;
		Cluster.Rank = -1;
		Cluster.Score = FLT_MAX;
		float KeyRadius = -1.0f;
		for (UInt32 k = 0; k < Cluster.Count; k++) {
			ShadowSceneLight* Lamp = ClusterLamps[Cluster.First + k];
			NiPointLight* Light = Lamp->sourceLight;
			const float LampRadius = Light->Spec.r * Settings->LightRadiusMult;
			const D3DXVECTOR3 Offset = Light->m_worldTransform.pos.toD3DXVEC3() - Cluster.Centre;
			Cluster.Radius = max(Cluster.Radius, LampRadius + D3DXVec3Length(&Offset));
			if (LampRadius > KeyRadius) {
				KeyRadius = LampRadius;
				Cluster.Key = Light;
				Cluster.KeyScene = Lamp;
				Cluster.Fill = Settings->FillLightRadius > 0.0f && Light->Spec.r > Settings->FillLightRadius;
			}
			const int Rank = RankOf(Light);
			if (Rank >= 0) {
				Cluster.Rank = Cluster.Rank < 0 ? Rank : min(Cluster.Rank, Rank);
				Cluster.Colour += D3DXVECTOR3(Light->Diff.r, Light->Diff.g, Light->Diff.b) * Light->Dimmer;
				// How strongly it lights where the player is: distance over reach (below 1 inside its
				// sphere). By distance alone a candle in the next room took a slot from the chandelier
				// lighting the whole room in view. A fill light counts as reaching FillLightRadius at
				// most, so it competes like a big lamp instead of always winning.
				float Reach = max(LampRadius, 1.0f);
				if (Settings->FillLightRadius > 0.0f) Reach = min(Reach, Settings->FillLightRadius * Settings->LightRadiusMult);
				Cluster.Score = min(Cluster.Score, Light->GetDistance(&Player->pos) / Reach);
			}
		}
		Clusters.push_back(Cluster);
	}

	// The active clusters, the ones lighting the player's surroundings most first (Score).
	std::vector<int>& Order = ClusterOrderScratch;
	Order.clear();
	for (int c = 0; c < (int)Clusters.size(); c++)
		if (Clusters[c].Rank >= 0) Order.push_back(c);
	std::sort(Order.begin(), Order.end(), [&Clusters](int A, int B) {
		if (Clusters[A].Score != Clusters[B].Score) return Clusters[A].Score < Clusters[B].Score;
		return Clusters[A].Key < Clusters[B].Key;   // a fixed order for equal scores
	});
	auto ClusterOf = [&Clusters](const NiPointLight* Key) {
		for (int c = 0; c < (int)Clusters.size(); c++)
			if (Clusters[c].Key == Key) return c;
		return -1;
	};
	auto OrderOf = [&Order](int Cluster) {
		for (int r = 0; r < (int)Order.size(); r++)
			if (Order[r] == Cluster) return r;
		return -1;
	};

	// Keep the clusters still near enough, and inactive ones within their grace; the rest fade
	// their shadow out and then free the slot (a cluster whose lamps are gone frees it at once, and
	// so does every slot past LightPoints).
	int SlotCluster[ShadowSlotsMax];
	int SlotRank[ShadowSlotsMax];
	auto FreeSlot = [&](int s) {
		SlotLights[s] = nullptr;
		SlotCluster[s] = SlotRank[s] = -1;
		SlotFade[s] = 0.0f;
		SlotLeaving[s] = false;
	};
	for (int s = 0; s < ShadowSlotsMax; s++) {
		SlotCluster[s] = SlotRank[s] = -1;
		if (!SlotLights[s]) continue;
		if (s >= ShadowLightsMax) {
			FreeSlot(s);
			continue;
		}
		const int Cluster = ClusterOf(SlotLights[s]);
		if (Cluster < 0) {   // the lamp is gone, or keys another cluster no more
			SlotStats->SlotDropGone++;
			FreeSlot(s);
			continue;
		}
		const int Rank = OrderOf(Cluster);
		SlotCluster[s] = Cluster;
		if (Rank >= 0 && Rank < ShadowLightsMax + SlotMargin) {
			SlotRank[s] = Rank;
			SlotIdle[s] = 0.0f;
			if (Rank < ShadowLightsMax) SlotLeaving[s] = false;   // among the nearest again: fade back in
		}
		else if (Rank >= 0) {   // active, but more than SlotMargin clusters past the nearest LightPoints
			if (!SlotLeaving[s]) SlotStats->SlotDropOutranked++;
			SlotRank[s] = Rank;
			SlotLeaving[s] = true;
		}
		else {
			SlotRank[s] = InactiveRank;
			SlotIdle[s] += FrameTime;
			if (SlotIdle[s] > SlotGrace) {
				if (!SlotLeaving[s]) SlotStats->SlotDropExpired++;
				SlotLeaving[s] = true;
			}
		}
	}
	// Fade: leaving slots out (freed when their shadow is gone), the rest in.
	for (int s = 0; s < ShadowLightsMax; s++) {
		if (SlotCluster[s] < 0) continue;
		if (SlotLeaving[s]) {
			SlotFade[s] -= FadeStep;
			if (SlotFade[s] <= 0.0f) FreeSlot(s);
		}
		else
			SlotFade[s] = min(SlotFade[s] + FadeStep, 1.0f);
	}
	// Give each of the nearest LightPoints clusters a slot: a free one (its shadow fades in from
	// nothing), else make one -- the farthest cluster kept only by the margin or the grace starts
	// fading out, and the slot goes to whichever cluster still waits for one when it is free. A
	// slot already fading out counts as one on its way.
	int Arriving = 0;
	for (int s = 0; s < ShadowLightsMax; s++)
		if (SlotCluster[s] >= 0 && SlotLeaving[s]) Arriving++;
	for (int r = 0; r < min((int)Order.size(), ShadowLightsMax); r++) {
		NiPointLight* Key = Clusters[Order[r]].Key;
		if (std::find(SlotLights, SlotLights + ShadowLightsMax, Key) != SlotLights + ShadowLightsMax) continue;
		int Target = -1;
		for (int s = 0; s < ShadowLightsMax && Target < 0; s++)
			if (!SlotLights[s]) Target = s;
		if (Target >= 0) {
			SlotStats->SlotAssigned++;
			SlotLights[Target] = Key;
			SlotCluster[Target] = Order[r];
			SlotRank[Target] = r;
			SlotIdle[Target] = 0.0f;
			SlotFade[Target] = 0.0f;
			SlotLeaving[Target] = false;
			continue;
		}
		if (Arriving > 0) {   // a slot is already fading out for this one
			Arriving--;
			continue;
		}
		int Victim = -1;
		for (int s = 0; s < ShadowLightsMax; s++)
			if (!SlotLeaving[s] && SlotRank[s] >= ShadowLightsMax && (Victim < 0 || SlotRank[s] > SlotRank[Victim])) Victim = s;
		if (Victim < 0) break;
		SlotStats->SlotEvicted++;
		SlotLeaving[Victim] = true;
	}
	SlotStats->SlotMaxGathered = max(SlotStats->SlotMaxGathered, (UInt32)Ranked.size());
	SlotStats->SlotMaxClusters = max(SlotStats->SlotMaxClusters, (UInt32)Order.size());

	// The slots' constants: the cluster's centre and radius, its active lamps' light summed. Slots
	// past ShadowCubeMapsMax go only to the object shaders (ShadowManager::SlotPosition, LampSlot);
	// their lamps stay in the lights tracked without shadows, which the other effects read.
	std::vector<NiPointLight*>& Shadowed = ShadowedLampsScratch;   // every lamp of a cluster with one of the first ShadowCubeMapsMax slots
	Shadowed.clear();
	SlotStats->LampSlot.clear();
	for (int s = 0; s < ShadowSlotsMax; s++) {
		SlotStats->SlotMembers[s].clear();
		// A fill light casts FillLightShadowStrength of a shadow (at full strength they blacked out
		// whole floors under ceilings and stairs).
		const float Fade = SlotCluster[s] < 0 ? 0.0f
			: SlotFade[s] * (Clusters[SlotCluster[s]].Fill ? Settings->FillLightShadowStrength : 1.0f);
		SlotStats->SlotFadeValue[s] = Fade;
		if (s < ShadowCubeMapsMax) ((float*)ShadowsConstants->ShadowLightFade)[s] = Fade;
		SlotStats->SlotMobile[s] = SlotCluster[s] >= 0 && Clusters[SlotCluster[s]].Mobile;
		if (SlotCluster[s] < 0) {
			ShadowLightsList[s] = NULL;
			SlotStats->SlotPosition[s] = Empty;
			if (s < ShadowCubeMapsMax) {
				ShadowsConstants->ShadowLightPosition[s] = Empty;
				LightColor[s] = Empty;
			}
			continue;
		}
		const ShadowLampCluster& Cluster = Clusters[SlotCluster[s]];
		ShadowLightsList[s] = Cluster.KeyScene;
		SlotStats->SlotPosition[s] = D3DXVECTOR4(Cluster.Centre.x, Cluster.Centre.y, Cluster.Centre.z, Cluster.Radius);
		if (s < ShadowCubeMapsMax) {
			ShadowsConstants->ShadowLightPosition[s] = SlotStats->SlotPosition[s];
			LightColor[s] = D3DXVECTOR4(Cluster.Colour.x, Cluster.Colour.y, Cluster.Colour.z, 1.0f);
		}
		for (UInt32 k = 0; k < Cluster.Count; k++) {
			ShadowSceneLight* Lamp = ClusterLamps[Cluster.First + k];
			SlotStats->SlotMembers[s].push_back(Lamp);
			SlotStats->LampSlot[Lamp] = s;
			if (s < ShadowCubeMapsMax) Shadowed.push_back(Lamp->sourceLight);
		}
		TheShadowManager->PointLightsNum++; // Constant to track number of shadow casting lights are present
	}

	// Every other point light, nearest first, goes to the lights tracked without shadows.
	for (auto& Entry : SceneLights) {
		if (LightIndex >= TrackedLightsMax) break;
		NiPointLight* Light = Entry.second->sourceLight;
		if (!Light || Light->EffectType != NiDynamicEffect::EffectTypes::POINT_LIGHT) continue;
		if (std::find(Shadowed.begin(), Shadowed.end(), Light) != Shadowed.end()) continue;   // lit as part of its cluster
		D3DXVECTOR4 LightPos = Light->m_worldTransform.pos.toD3DXVEC4();
		LightPos.w = Light->Spec.r * Settings->LightRadiusMult;
		LightsList[LightIndex] = Light;
		LightPosition[LightIndex] = LightPos;
		LightColor[ShadowCubeMapsMax + LightIndex] = D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
		LightIndex++;
	}
	for (; LightIndex < TrackedLightsMax; LightIndex++) {
		LightsList[LightIndex] = NULL;
		LightPosition[LightIndex] = Empty;
		LightColor[ShadowCubeMapsMax + LightIndex] = Empty;
	}

	// The nearest active point lights, each with its slot's anchor and shadow info, for their
	// screen-space contact shadows (Effects/SunShadows.fx, technique 1). There each lamp's contact
	// shadow takes away only what the lamp's cube shadow has not already, so the info and anchor
	// are the ones the object shaders read (ForwardPointShadows in Hooks/Shaders.cpp), in world space.
	int ContactIndex = 0;
	for (auto& Entry : SceneLights) {
		if (ContactIndex >= ContactLampsMax) break;
		NiPointLight* Light = Entry.second->sourceLight;
		if (!Light || Light->EffectType != NiDynamicEffect::EffectTypes::POINT_LIGHT) continue;
		// The radius the object shaders light it with (its specular red): LightRadiusMult only
		// widens what its shadow map covers.
		const float Radius = Light->Spec.r;
		if (Radius <= 0.0f || Light->Dimmer <= 0.0f) continue;
		D3DXVECTOR4 Anchor = Empty;
		float Info = -1.0f;
		if (ForwardSlots) {
			const auto Found = SlotStats->LampSlot.find(Entry.second);
			if (Found != SlotStats->LampSlot.end() && Found->second >= 0 && Found->second < ShadowSlotsMax && SlotStats->SlotPosition[Found->second].w > 0.0f) {
				Anchor = SlotStats->SlotPosition[Found->second];
				Info = (float)Found->second * 2.0f + std::clamp(SlotStats->SlotFadeValue[Found->second], 0.0f, 1.0f);
			}
		}
		const NiPoint3& Pos = Light->m_worldTransform.pos;
		ContactLampPosition[ContactIndex] = D3DXVECTOR4(Pos.x, Pos.y, Pos.z, Radius);
		ContactLampColor[ContactIndex] = D3DXVECTOR4(Light->Diff.r * Light->Dimmer, Light->Diff.g * Light->Dimmer, Light->Diff.b * Light->Dimmer, Info);
		ContactLampAnchor[ContactIndex] = Anchor;
		ContactIndex++;
	}
	for (; ContactIndex < ContactLampsMax; ContactIndex++) {
		ContactLampPosition[ContactIndex] = Empty;
		ContactLampColor[ContactIndex] = D3DXVECTOR4(0.0f, 0.0f, 0.0f, -1.0f);
		ContactLampAnchor[ContactIndex] = Empty;
	}

	timer.LogTime("ShaderManager::GetNearbyLights");
}


bool ShaderManager::ShouldRenderShadowMaps() {
	if (GameState.isExterior)
		return orthoRequired || (
			Effects.ShadowsExteriors->Settings.Exteriors.Enabled &&
			Effects.ShadowsExteriors->Enabled);
	else
		return Effects.ShadowsInteriors->Enabled;
}

/*
* Renders a given effect to an arbitrary render target
*/
void ShaderManager::RenderEffectToRT(IDirect3DSurface9* RenderTarget, EffectRecord* Effect, bool clearRenderTarget) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	Device->SetRenderTarget(0, RenderTarget);
	Effect->Render(Device, RenderTarget, RenderTarget, 0, clearRenderTarget, RenderTarget);
};


void ShaderManager::RenderEffectsPreTonemapping(IDirect3DSurface9* RenderTarget) {
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle
	if (!Player->parentCell) return;
	if (GameState.OverlayIsOn && TESMain::IsMenuBackgroundReady()) return; // disable all effects during terminal/lockpicking sequences

	auto timer = TimeLogger();

	IDirect3DDevice9* Device = TheRenderManager->device;
	IDirect3DSurface9* SourceSurface = TheTextureManager->SourceSurface;
	IDirect3DSurface9* RenderedSurface = TheTextureManager->RenderedSurface;

	// prepare device for effects
	Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));
	Device->SetFVF(FrameFVF);

	// render post process normals for use by shaders
	RenderEffectToRT(Effects.CombineDepth->Textures.CombinedDepthSurface, Effects.CombineDepth, false);
	RenderEffectToRT(Effects.Normals->Textures.NormalsSurface, Effects.Normals, false);

	// Indoors, with ForwardPointShadows, the object shaders shadow each lamp's own light
	// (Shaders/Includes/PointShadow.hlsl): the point shadow pass and the interior shadow
	// post-process, which darkened the finished frame, are skipped.
	const bool ForwardPointShadows = !GameState.isExterior && Effects.ShadowsExteriors->Constants.PointShadowData.x > 0.0f;

	// render a shadow pass for point lights
	if (!ForwardPointShadows && ((GameState.isExterior && Effects.ShadowsExteriors->Enabled) || (!GameState.isExterior && Effects.ShadowsInteriors->Enabled))) {
		// separate lights in 2 batches
		RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows, true);
		if (Effects.ShadowsExteriors->Settings.Interiors.LightPoints > 6) RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows2, false);
		if (GameState.isExterior) RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.SunShadows, false);
	}
	else {
		// Nothing above ran this frame, so ShadowPassSurface keeps whatever it last
		// held -- e.g. an exterior sun-shadow composite, walked in from outdoors,
		// frozen here for as long as this branch keeps being skipped (interior with
		// Interior point-shadows off is the common case). It's still sampled
		// unconditionally by other independently-enabled effects, so reset it to the
		// neutral "no shadow" value rather than leaving
		// stale exterior data for them to read.
		Effects.ShadowsExteriors->clearShadowsBuffer();

		// Lamps' contact shadows (Effects/SunShadows.fx, technique 1): their share of each pixel's
		// light into the buffer's red channel, composited below (ShadowsInteriors.fx, technique 1).
		if (ForwardPointShadows && Effects.ShadowsExteriors->Constants.LampContactData.x > 0.0f) {
			Device->SetRenderTarget(0, Effects.ShadowsExteriors->Textures.ShadowPassSurface);
			Effects.SunShadows->Render(Device, Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.ShadowsExteriors->Textures.ShadowPassSurface,
				Effects.ShadowsExteriors->Settings.Interiors.ContactBlur ? 2 : 1, false, nullptr);   // sharp, or blurred too
		}
	}

	Device->SetRenderTarget(0, RenderTarget);

	// copy the source render target to both the rendered and source textures (rendered gets updated after every pass, source once per effect)
	Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	Device->StretchRect(RenderTarget, NULL, SourceSurface, NULL, D3DTEXF_NONE);

	// First, on the scene exactly as drawn: it recognises skin by comparing each pixel with the
	// colour the skin shader wrote, which any effect before it would change. Screen-space shadows
	// and AO then darken the scattered skin like everything else.
	Effects.SkinScattering->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);   // pass 2 reads the unblurred scene

	// The environment the PBR materials reflect next frame, from the scene as drawn (skin
	// scattering only blurs skin; RenderedSurface holds its result).
	Effects.DynamicCubemaps->RenderCubemaps(Device, RenderTarget);

	if (GameState.isExterior)
		Effects.ShadowsExteriors->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	else if (!ForwardPointShadows)
		Effects.ShadowsInteriors->Render(Device, RenderTarget, RenderedSurface, 0, true, SourceSurface);
	else if (Effects.ShadowsExteriors->Constants.LampContactData.x > 0.0f)
		Effects.ShadowsInteriors->Render(Device, RenderTarget, RenderedSurface, 1, false, SourceSurface);   // the lamps' contact shadows

	Effects.SnowAccumulation->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.AmbientOcclusion->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.WetWorld->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// March into VolumetricLight's own half res buffer first (technique 0); the Composite pass
	// below (technique 1) reads it back at full res. Same two-step pattern as FlashlightBeam,
	// including the guard, which is not optional: RenderEffectToRT switches the render target
	// BEFORE EffectRecord::Render can test Enabled/ShouldRender, so an unguarded call rebinds
	// every frame even in interiors where this effect never draws. Worse, if the surface is null
	// -- texture creation failed, or a device reset released it before RegisterTextures ran again
	// -- it becomes SetRenderTarget(0, NULL), which D3D9 forbids for target 0 and leaves the
	// device with no colour target for whatever draws next.
	if (Effects.VolumetricLight->Textures.VolumetricSurface &&
		Effects.VolumetricLight->Enabled &&
		Effects.VolumetricLight->ShouldRender()) {
		RenderEffectToRT(Effects.VolumetricLight->Textures.VolumetricSurface, Effects.VolumetricLight, true);
		Effects.VolumetricLight->RenderTemporal(Device);
		Device->SetRenderTarget(0, RenderTarget);
	}

	// Beam march first, into its own half res buffer, so the Flashlight Combine pass can
	// read it. Control.x already folds the effect toggle, the per view toggle and the
	// strength together, so this one test gates the whole thing.
	if (Effects.FlashlightBeam->Constants.Control.x > 0.0f) {
		RenderEffectToRT(Effects.FlashlightBeam->Textures.VolumetricSurface, Effects.FlashlightBeam, true);
		Device->SetRenderTarget(0, RenderTarget);
	}
	Effects.Flashlight->Render(Device, RenderTarget, RenderedSurface, Effects.Flashlight->selectedPass, true, SourceSurface);
	Effects.Underwater->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.VolumetricLight->Render(Device, RenderTarget, RenderedSurface, 1, false, SourceSurface);
	Effects.VolumetricFog->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.GodRays->Render(Device, RenderTarget, RenderedSurface, 0, true, SourceSurface);

	// calculate average luma for use by shaders
	if (avglumaRequired) {
		RenderEffectToRT(Effects.AvgLuma->Textures.AvgLumaSurface, Effects.AvgLuma, NULL);
		Device->SetRenderTarget(0, RenderTarget); 	// restore device used for effects
	}

	Effects.Exposure->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.Bloom->RenderBloomBuffer(RenderTarget);

	Effects.Lens->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	if (Effects.LUT->Settings.PreTonemapping)
		Effects.LUT->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	timer.LogTime("ShaderManager::RenderEffectsPreTonemapping");
}


/*
* Renders the effect that have been set to enabled.
*/
void ShaderManager::RenderEffects(IDirect3DSurface9* RenderTarget) {
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle
	if (!Player->parentCell) return;
	if (GameState.OverlayIsOn) return; // disable all effects during terminal/lockpicking sequences because they bleed through the overlay

	auto timer = TimeLogger();

	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* SourceSurface = TheTextureManager->SourceSurface;
	IDirect3DSurface9* RenderedSurface = TheTextureManager->RenderedSurface;

	Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));
	Device->SetFVF(FrameFVF);

	// prepare device for effects
	Device->SetRenderTarget(0, RenderTarget);

	// copy the source render target to both the rendered and source textures (rendered gets updated after every pass, source once per effect)
	Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	Device->StretchRect(RenderTarget, NULL, SourceSurface, NULL, D3DTEXF_NONE);

	// TAA first: after tonemapping, so it resolves LDR values that cannot ghost as HDR highlights
	// do, and ahead of everything below. Rain and snow are particles with no depth of their own
	// to reproject by, DoF and motion blur want the stable image as input, and the lens effects
	// and cinema overlay are fixed to the screen -- any of them run through a reprojection that
	// assumes a static world would smear across the frame as the camera turns.
	Effects.TAA->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	Effects.Rain->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.Snow->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	//Effects.Linearization->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.BloomLegacy->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// screenspace coloring/blurring effects get rendered last
	Effects.Coloring->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	if (!Effects.LUT->Settings.PreTonemapping)
		Effects.LUT->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.DepthOfField->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.CinematicDOF->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.MotionBlur->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// lens effects
	Effects.BloodLens->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.WaterLens->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.LowHF->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	Effects.DitherBuster->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);
	Effects.SMAA->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	Effects.Sharpening->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// cinema effect gets rendered very last because of vignetting/letterboxing
	Effects.Cinema->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// final adjustments
	Effects.ImageAdjust->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// debug shader allows to display some of the buffers
	Effects.Debug->Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface);

	// [Shaders.DynamicCubemaps.Main] DebugView: the environment cube over the finished frame.
	Effects.DynamicCubemaps->RenderDebug(Device, RenderTarget);

	timer.LogTime("ShaderManager::RenderEffects");
}

EffectRecord* ShaderManager::GetEffectByName(const char* Name) {
	// effects
	EffectsList::iterator t = EffectsNames.find(Name);
	if (t == EffectsNames.end()) return nullptr;
	return *(t->second);
}


ShaderCollection* ShaderManager::GetShaderCollectionByName(const char* Name) {
	// shaders
	ShaderList::iterator t = ShaderNames.find(Name);
	if (t == ShaderNames.end()) return nullptr;
	return *(t->second);
}

/*
* Writes the settings corresponding to the shader/effect name, to switch it between enabled/disabled.*
* Also creates or deletes the corresponding Effect Record.
*/
void ShaderManager::SwitchShaderStatus(const char* Name) {
	IsMenuSwitch = true;

	// effects
	EffectRecord* effect = GetEffectByName(Name);
	if (effect) {
		bool setting = effect->SwitchEffect();
		TheSettingManager->SetMenuShaderEnabled(Name, setting);

		IsMenuSwitch = false;
		return;
	}

	// shaders
	ShaderCollection* shader = GetShaderCollectionByName(Name);
	if (shader) {
		bool setting = shader->SwitchShader();
		TheSettingManager->SetMenuShaderEnabled(Name, setting);

		IsMenuSwitch = false;
		return;
	}
}

void ShaderManager::SetCustomConstant(const char* Name, D3DXVECTOR4 Value) {
	CustomConstants::iterator v = CustomConst.find(std::string(Name));
	if (v != CustomConst.end()) v->second = Value;
}
