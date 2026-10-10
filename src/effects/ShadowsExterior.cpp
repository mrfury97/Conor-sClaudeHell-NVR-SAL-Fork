#include "ShadowsExterior.h"

void ShadowsExteriorEffect::UpdateConstants() {

	Constants.ShadowFade.x = 0; // Fade 1.0 == no shadows
	if (TheShaderManager->GameState.isExterior) {
		Constants.ShadowFade.x = smoothStep(0.5f, 0.1f, abs(TheShaderManager->GameState.dayLight - 0.5f)); // fade shadows to 0 at sunrise/sunset.  

		TimeGlobals* GameTimeGlobals = TimeGlobals::Get();
		float DaysPassed = GameTimeGlobals->GameDaysPassed ? GameTimeGlobals->GameDaysPassed->data : 1.0f;

		if(TheShaderManager->GameState.isDayTime < 0.5f) {
			// at night time, fade based on moonphase
			// moonphase goes from 0 to 8
			float MoonPhase = (fmod(DaysPassed, 8 * Tes->sky->firstClimate->phaseLength & 0x3F)) / (Tes->sky->firstClimate->phaseLength & 0x3F);

			float PI = 3.1416f; // use cos curve to fade moon light shadows strength
			MoonPhase = std::lerp(-PI, PI, MoonPhase / 8) - PI / 4; // map moonphase to 1/2PI/2PI + 1/2

			// map MoonVisibility to MinNightDarkness/1 range
			float nightMinDarkness = 1 - Settings.Exteriors.NightMinDarkness;
			float MoonVisibility = std::lerp((float)0.0, nightMinDarkness, (float)(cos(MoonPhase) * 0.5 + 0.5));
			Constants.ShadowFade.x = std::lerp(MoonVisibility, (float)1.0, Constants.ShadowFade.x);
		}

		if (TheShaderManager->GameState.isDayTimeChanged) {
			// pass the enabled/disabled property of the pointlight shadows to the shadowfade constant
			bool usePointLights = (TheShaderManager->GameState.isDayTime > 0.5) ? Settings.Exteriors.UsePointShadowsDay: Settings.Exteriors.UsePointShadowsNight;
			Constants.ShadowFade.z = usePointLights;
		}
		Constants.ShadowFade.y = Settings.Exteriors.Enabled && Enabled;

		// Fade shadows out as the light nears the horizon, reaching none at all where it sets.
		// ShadowManager::RenderShadowMaps stops drawing the cascades once the light is below the
		// horizon (SmoothedSunDir.z <= 0). The day/night fade above cannot cover that on its own: it
		// runs on dayLight, which trails the climate's sunset by an hour, and the sun mesh position
		// stays the light direction until dayLight reaches 0.5 -- so the sun sets while shadows are
		// still partly visible. Without this they would switch off with a pop at that moment (with
		// the check below) or, before it existed, freeze to the camera. SmoothedSunDir is last
		// frame's, the same value RenderShadowMaps tests, and 0.1 is about 6 degrees of elevation.
		// Only while shadows are on: SmoothedSunDir is only refreshed while the maps are rendered.
		if (Constants.ShadowFade.y) {
			float horizonFade = smoothStep(0.1f, 0.0f, Constants.SmoothedSunDir.z);
			Constants.ShadowFade.x = (std::max)(Constants.ShadowFade.x, horizonFade);
		}

		// Never sample cascades RenderShadowMaps did not draw. ShadowCameraToLight is camera-relative,
		// so a frozen atlas read through frozen transforms projects every shadow from wherever the
		// camera is now: the shadows ride along with the player. .y gates the atlas for the game
		// shaders, VolumetricLight and VolumetricFog; .x = 1 (fully faded) covers the effects that
		// only read the fade. RenderShadowMaps also clears these itself for the frame it skips, since
		// the game shaders draw before this runs again. Only while shadows are on: the flag is not
		// refreshed while they are off, and forcing the fade then would take sun specular with it.
		if (SunMapsStale && Constants.ShadowFade.y) {
			Constants.ShadowFade.x = 1.0f;
			Constants.ShadowFade.y = 0.0f;
		}
		Constants.ShadowFade.w = Constants.ShadowMapRadius.w; //furthest distance for point lights shadows

		// Update constants used by shadow shaders: x=quality, y=darkness
		Constants.Data.x = Settings.Exteriors.Quality;
		//if (Enabled) Constants.ShadowData->x = -1; // Disable the forward shadowing
		Constants.Data.y = Settings.Exteriors.Darkness;

		// Mode and format data. x=mode, y=bits per pixel
		Constants.FormatData.x = Settings.ShadowMaps.Mode;
		Constants.FormatData.y = Settings.ShadowMaps.FormatBits;

		// z: are we in an exterior cell right now. GetSunShadow reads this on its own --
		// ShadowFade.y means something different indoors (interior point-shadows enabled,
		// not sun shadows enabled), so the sun-shadow path can't share it. See the interior
		// branch below.
		Constants.FormatData.z = 1.0f;
	}
	else {
		// pass the enabled/disabled property of the shadow maps to the shadowfade constant
		Constants.ShadowFade.y = TheShaderManager->Effects.ShadowsInteriors->Enabled;
		Constants.ShadowFade.z = 1; // z enables point lights
		Constants.ShadowFade.w = Settings.Interiors.DrawDistance; //furthest distance for point lights shadows

		// Sun shadows never apply indoors -- unlike ShadowFade.y, this one exists solely for
		// GetSunShadow, so it's safe to just say "no" here rather than borrow another flag.
		Constants.FormatData.z = 0.0f;

		// Update constants used by shadow shaders: x=quality, y=darkness
		Constants.Data.x = Settings.Interiors.Quality;
		//if (TheShaderManager->Effects.ShadowsInteriors->Enabled) Constants.Data.x = -1; // Disable the forward shadowing
		Constants.Data.y = Settings.Interiors.Darkness;
		Constants.Data.z = 1.0f / (float)Settings.Interiors.ShadowCubeMapSize;
	}

	// Lamps shadowed in the object shaders (Shaders/Includes/PointShadow.hlsl), indoors, when the atlas
	// exists. ShaderManager then skips the point shadow post-process.
	const float FaceSize = (float)Settings.Interiors.ShadowCubeMapSize;
	const bool ForwardPoint = !TheShaderManager->GameState.isExterior && Settings.Interiors.ForwardPointShadows
		&& TheShaderManager->Effects.ShadowsInteriors->Enabled && Textures.PointShadowAtlasTexture && TheShadowManager && TheShadowManager->ShadowCubeToAtlasPixel;
	const float Columns = (float)max(Textures.PointShadowAtlasColumns, 1u);
	const float AtlasWidth = (float)max(Textures.PointShadowAtlasWidth, 1u), AtlasHeight = (float)max(Textures.PointShadowAtlasHeight, 1u);
	Constants.PointShadowData = D3DXVECTOR4(ForwardPoint ? 1.0f : 0.0f, 1.0f / AtlasWidth, 1.0f / AtlasHeight, FaceSize);
	// A face's texel spans about 2 / size of the distance (90 degrees over size texels).
	Constants.PointShadowParams = D3DXVECTOR4(Settings.Interiors.NearFade, Columns, Settings.Interiors.ShadowNormalOffset * 2.0f / FaceSize, Settings.Interiors.ShadowSoftness);
	// Lamps' contact shadows: only with the lamps' cube shadows in the object shaders, which keep
	// each lamp's light apart (the contact pass takes away what they left).
	Constants.LampContactData = D3DXVECTOR4(ForwardPoint ? Settings.Interiors.ContactStrength : 0.0f, Settings.Interiors.ContactLength,
		Settings.Interiors.ContactThickness, Settings.Interiors.ContactDistance);
	Constants.LampContactExtra = D3DXVECTOR4((float)Settings.Interiors.ContactSamples, (float)Settings.Interiors.ContactLamps, Settings.Interiors.ContactScreenLength, 0.0f);
	Constants.LampContactShape = D3DXVECTOR4(Settings.Interiors.ContactNormalOffset, Settings.Interiors.PCSSLightSize * Settings.Interiors.ContactSoftness, 0.0f, 0.0f);
	// PCSS: its filter dithered (1: cheap, grainy unless TAA averages it) or smooth (2: no grain, no
	// TAA needed); PCSSFilter 0 picks smooth without TAA, dithered with it. The dithered pattern turns
	// by the golden ratio each frame while TAA is on, so it averages many patterns; without TAA a
	// pattern that changed each frame crawled, so it holds still (each pixel's own turn comes from its
	// screen position either way).
	static UInt32 PCSSFrame = 0;
	PCSSFrame++;
	const bool TAAOn = TheShaderManager->Effects.TAA && TheShaderManager->Effects.TAA->Enabled;
	const int Filter = Settings.Interiors.PCSSFilter == 0 ? (TAAOn ? 2 : 1) : Settings.Interiors.PCSSFilter;   // setting: 1 smooth, 2 dithered
	const float Mode = !(Settings.Interiors.PCSS && Settings.Interiors.PCSSLightSize > 0.0f) ? 0.0f : (Filter == 2 ? 1.0f : 2.0f);   // shader: 1 dithered, 2 smooth
	Constants.PointShadowPCSS = D3DXVECTOR4(Mode, Settings.Interiors.PCSSLightSize, Settings.Interiors.PCSSMaxSpread,
		TAAOn ? fmodf((float)(PCSSFrame % 4096) * 0.61803399f, 1.0f) : 0.0f);

	// Force-rebind FormatData/ForwardData directly, once per frame, bypassing the
	// per-shader "bound by name" constant table.
	//
	// GetSunShadow's gates on TESR_ShadowFormatData.z (interior/exterior) and
	// TESR_ShadowForwardData.x (forward suppressed) live inside the
	// !DIFFUSE && !POINT block in ObjectTemplate.hlsl. A DIFFUSE- or POINT-lit
	// permutation never reaches that code, so its own compiled constant table
	// never lists these names -- ShaderRecord::CreateCT has nothing to bind, and
	// SetCT() never issues the SetPixelShaderConstantF that would refresh c129/
	// c133 for that draw call. D3D9 pixel shader constant registers persist raw
	// values across draw calls (they are not per-shader, per-draw state), so
	// that object silently inherits whatever an earlier, unrelated draw left in
	// those registers -- which is how exterior shadow state was observed to
	// stick after walking into an interior lit mostly by DIFFUSE/POINT objects.
	//
	// Both values are frame-invariant (same for every object drawn this frame),
	// so one unconditional bind here, ahead of the frame's world geometry, is
	// sufficient: any shader that DOES list these names will simply rewrite them
	// with the identical value when it draws.
	TheRenderManager->device->SetPixelShaderConstantF(129, (const float*)&Constants.FormatData, 1);
	TheRenderManager->device->SetPixelShaderConstantF(133, (const float*)&Constants.ForwardData, 1);

	UpdateLightColors();
}

// The sun and ambient colours exactly as BSShaderLightingProperty::SetLight1x2x (0xB70820) hands
// them to the object shaders as PSLightColor[0] and AmbientColor, for the composite's contact
// shadows to split a pixel's light the way the forward path does. They are not the weather's
// raw colours (TESR_SunColor, TESR_SunAmbient): both are scaled by the light's dimmer, the sun
// by the HDR sunlight dimmer outdoors, and the ambient floored at fMinAmbient. Left out are the
// per-object factors SetLight also applies -- forced darkness and the LOD dimmers -- which a
// screen-space pass cannot know and which are 1 on almost everything.
void ShadowsExteriorEffect::UpdateLightColors() {
	static const bool* bHDR = (const bool*)0x11F941E;				// BSShaderManager::bHDR
	static const bool* bInterior = (const bool*)0x11F9427;			// BSShaderManager::bInterior
	static const float* fSunlightDimmer = (const float*)0x11F9190;	// BSShaderManager::fSunlightDimmer
	static const float* fMinAmbient = (const float*)0x11F947C;		// BSShaderManager::fMinAmbient

	NiDirectionalLight* sun = Tes ? Tes->directionalLight : nullptr;
	if (!sun) {
		Constants.SunLight = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
		Constants.AmbientLight = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	float dimmer = *bHDR ? sun->Dimmer : (std::min)(sun->Dimmer, 1.0f);

	D3DXVECTOR3 ambient(sun->Amb.r * dimmer, sun->Amb.g * dimmer, sun->Amb.b * dimmer);
	if (*fMinAmbient > 0.0f) {
		float luminance = ambient.x * 0.33f + ambient.y * 0.34f + ambient.z * 0.33f;
		if (luminance > 0.0f) {
			float boost = luminance <= *fMinAmbient ? *fMinAmbient / luminance : 1.0f;
			ambient = (ambient + D3DXVECTOR3(0.1f, 0.1f, 0.1f)) * boost;
		}
		else {
			ambient = D3DXVECTOR3(*fMinAmbient, *fMinAmbient, *fMinAmbient);
		}
	}

	D3DXVECTOR3 sunColor(sun->Diff.r * dimmer, sun->Diff.g * dimmer, sun->Diff.b * dimmer);
	if (*bHDR && !*bInterior) sunColor *= *fSunlightDimmer;

	Constants.SunLight = D3DXVECTOR4(sunColor.x, sunColor.y, sunColor.z, 1.0f);
	Constants.AmbientLight = D3DXVECTOR4(ambient.x, ambient.y, ambient.z, 1.0f);
}

bool ShadowsExteriorEffect::UpdateSettingsFromQuality(int quality) {
	bool cascadeSettingsChanged = false;
	
	D3DFORMAT oldFormat = Settings.ShadowMaps.Format;
	int oldCascadeResolution = Settings.ShadowMaps.CascadeResolution;
	bool oldMSAA = Settings.ShadowMaps.MSAA;
	bool oldPrefilter = Settings.ShadowMaps.Prefilter;
	
	// Custom settings.
	if (quality < 0 || quality > 3) {
		Settings.ShadowMaps.Mode = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Mode"), 0, Modes-1);
		Settings.ShadowMaps.FormatBits = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Format"), 0, FormatBits-1);

		Settings.ShadowMaps.Distance = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "Distance"), 100.0f);
		Settings.ShadowMaps.CascadeLambda = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "CascadeLambda"), 0.0f, 1.0f);
		Settings.ShadowMaps.LimitFrequency = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "LimitFrequency");

		// 0..5 -> 1024, 1536, 2048, 2560, 3072, 3584 per cascade. The atlas packs 2x2 cascades,
		// so the texture is twice this in each dimension: index 5 is a 7168x7168 atlas.
		int cascadeStep = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "CascadeResolution"), 0, 5);
		ULONG cascadeRes = (ULONG)(cascadeStep + 2) * 512;

		// Cap against what the device can actually create. InitTexture does not report failure,
		// so an atlas past the hardware limit would come back null and read as frozen or missing
		// shadows rather than as an error.
		if (TheRenderManager && TheRenderManager->device) {
			D3DCAPS9 caps;
			if (SUCCEEDED(TheRenderManager->device->GetDeviceCaps(&caps))) {
				ULONG maxCascade = (ULONG)min(caps.MaxTextureWidth, caps.MaxTextureHeight) / 2;
				while (cascadeRes > maxCascade && cascadeRes > 1024) cascadeRes -= 512;
			}
		}

		Settings.ShadowMaps.CascadeResolution = cascadeRes;

		Settings.ShadowMaps.MSAA = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "MSAA");

		// Mipmaps and anisotropy are disabled due to deferred shadows - derivatives are messed up and causing artifacts.
		// https://aras-p.info/blog/2010/01/07/screenspace-vs-mip-mapping/
		/*bool oldMips = Settings.ShadowMaps.Mipmaps;
		Settings.ShadowMaps.Mipmaps = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Mipmaps");

		if (oldMips != Settings.ShadowMaps.Mipmaps)
			cascadeSettingsChanged = true;

		Settings.ShadowMaps.Anisotropy = (std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Anisotropy"), 0, 2)) * 8;*/

		Settings.ShadowMaps.Prefilter = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Prefilter");

		for (int shadowType = 0; shadowType <= MapLod; shadowType++) {
			char sectionName[256] = "Shaders.ShadowsExteriors.Forms";
			switch (shadowType) {
			case MapNear:
				strcat(sectionName, "Near");
				break;
			case MapMiddle:
				strcat(sectionName, "Middle");
				break;
			case MapFar:
				strcat(sectionName, "Far");
				break;
			case MapLod:
				strcat(sectionName, "Lod");
				break;
			case MapOrtho:
				strcat(sectionName, "Ortho");
				break;
			}
			ShadowMapSettings* ShadowMap = &ShadowMaps[shadowType];

			ShadowMap->Forms.AlphaEnabled = TheSettingManager->GetSettingI(sectionName, "AlphaEnabled");
			ShadowMap->Forms.Activators = TheSettingManager->GetSettingI(sectionName, "Activators");
			ShadowMap->Forms.Actors = TheSettingManager->GetSettingI(sectionName, "Actors");
			ShadowMap->Forms.Apparatus = TheSettingManager->GetSettingI(sectionName, "Apparatus");
			ShadowMap->Forms.Books = TheSettingManager->GetSettingI(sectionName, "Books");
			ShadowMap->Forms.Containers = TheSettingManager->GetSettingI(sectionName, "Containers");
			ShadowMap->Forms.Doors = TheSettingManager->GetSettingI(sectionName, "Doors");
			ShadowMap->Forms.Furniture = TheSettingManager->GetSettingI(sectionName, "Furniture");
			ShadowMap->Forms.Misc = TheSettingManager->GetSettingI(sectionName, "Misc");
			ShadowMap->Forms.Statics = TheSettingManager->GetSettingI(sectionName, "Statics");
			ShadowMap->Forms.Terrain = TheSettingManager->GetSettingI(sectionName, "Terrain");
			ShadowMap->Forms.Trees = TheSettingManager->GetSettingI(sectionName, "Trees");
			ShadowMap->Forms.Lod = TheSettingManager->GetSettingI(sectionName, "Lod");
			ShadowMap->Forms.MinRadius = TheSettingManager->GetSettingF(sectionName, "MinRadius");
			ShadowMap->Forms.OrigMinRadius = TheSettingManager->GetSettingF(sectionName, "MinRadius");
		};
	}
	else {
		for (int shadowType = 0; shadowType <= MapLod; shadowType++) {
			char sectionName[256] = "Shaders.ShadowsExteriors.Forms";
			switch (shadowType) {
			case MapNear:
				strcat(sectionName, "Near");
				break;
			case MapMiddle:
				strcat(sectionName, "Middle");
				break;
			case MapFar:
				strcat(sectionName, "Far");
				break;
			case MapLod:
				strcat(sectionName, "Lod");
				break;
			case MapOrtho:
				strcat(sectionName, "Ortho");
				break;
			}
			ShadowMapSettings* ShadowMap = &ShadowMaps[shadowType];

			ShadowMap->Forms.AlphaEnabled = (shadowType == MapOrtho) ? 0 : 1;
			ShadowMap->Forms.Activators = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Actors = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Apparatus = 0;
			ShadowMap->Forms.Books = (shadowType < MapFar) ? 1 : 0;
			ShadowMap->Forms.Containers = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Doors = (shadowType == MapOrtho) ? 0 : 1;
			ShadowMap->Forms.Furniture = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Misc = 1;
			ShadowMap->Forms.Statics = 1;
			ShadowMap->Forms.Terrain = 1;
			ShadowMap->Forms.Trees = 1;
			ShadowMap->Forms.Lod = quality < 2 ? 0 : 1;
			ShadowMap->Forms.MinRadius = (MapFar <= shadowType && shadowType <= MapLod) ? 10.0f : 1.0f;
			ShadowMap->Forms.OrigMinRadius = (MapFar <= shadowType && shadowType <= MapLod) ? 10.0f : 1.0f;
		};

		Settings.ShadowMaps.CascadeLambda = 0.9f;
		Settings.ShadowMaps.LimitFrequency = 1;
		Settings.ShadowMaps.MSAA = 1;
		Settings.ShadowMaps.Prefilter = 1;

		switch (quality) {
		case 0:
			Settings.ShadowMaps.Mode = 0;
			Settings.ShadowMaps.FormatBits = 0;
			Settings.ShadowMaps.Distance = 3000.0f;
			Settings.ShadowMaps.CascadeResolution = 1024;
			Settings.ShadowMaps.MSAA = 0;
			break;
		case 1:
			Settings.ShadowMaps.Mode = 0;
			Settings.ShadowMaps.FormatBits = 0;
			Settings.ShadowMaps.Distance = 4000.0f;
			Settings.ShadowMaps.CascadeResolution = 1024;
			break;
		case 2:
			Settings.ShadowMaps.Mode = 1;
			Settings.ShadowMaps.FormatBits = 1;
			Settings.ShadowMaps.Distance = 4500.0f;
			Settings.ShadowMaps.CascadeResolution = 2048;
			break;
		case 3:
			Settings.ShadowMaps.Mode = 2;
			Settings.ShadowMaps.FormatBits = 0;
			Settings.ShadowMaps.Distance = 6000.0f;
			Settings.ShadowMaps.CascadeResolution = 2048;
			break;
		}
	}
	
	Settings.ShadowMaps.Format = Formats[Settings.ShadowMaps.Mode][Settings.ShadowMaps.FormatBits];

	// Set clear color for clearing the cascades.
	float pos = exp(Settings.ShadowMaps.FormatBits ? 40.0f : 5.54f);
	float neg = -exp(-5.0f);

	for (int shadowType = 0; shadowType <= MapLod; shadowType++) {
		ShadowMapSettings* ShadowMap = &ShadowMaps[shadowType];
		ShadowMap->CustomClearRequired = false;

		switch (Settings.ShadowMaps.Mode) {
		case 0:
			ShadowMap->ClearColor = D3DXVECTOR4(1.0f, 1.0f, 0.0f, 1.0f);
			break;
		case 1:
			ShadowMap->ClearColor = D3DXVECTOR4(pos, neg, 0.0f, 1.0f);
			ShadowMap->CustomClearRequired = true;
			break;
		case 2:
			ShadowMap->ClearColor = D3DXVECTOR4(pos, neg, pos * pos, neg * neg);
			ShadowMap->CustomClearRequired = true;
			break;
		default:
			ShadowMap->ClearColor = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 1.0f);
		}
	}
	ShadowMaps[MapOrtho].CustomClearRequired = false;

	if (oldFormat != Settings.ShadowMaps.Format)
		cascadeSettingsChanged = true;

	if (oldCascadeResolution != 0 && oldCascadeResolution != Settings.ShadowMaps.CascadeResolution)
		cascadeSettingsChanged = true;

	if (oldMSAA != Settings.ShadowMaps.MSAA)
		cascadeSettingsChanged = true;

	// The prefilter's ping-pong scratch target is only allocated when the prefilter is on,
	// so toggling it has to go through RecreateTextures.
	if (oldPrefilter != Settings.ShadowMaps.Prefilter)
		cascadeSettingsChanged = true;

	return cascadeSettingsChanged;
}

void ShadowsExteriorEffect::UpdateSettings() {

	Constants.ScreenSpaceData.x = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ScreenSpace", "Enabled") && Enabled;
	Constants.ScreenSpaceData.y = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "BlurRadius");
	Constants.ScreenSpaceData.z = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "RenderDistance");
	Constants.ScreenSpaceData.w = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "Intensity"), 0.0f);

	// Contact shadows ride on the screen space pass, so they are off whenever it is.
	Constants.ContactData.x = Constants.ScreenSpaceData.x ? max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "ContactStrength"), 0.0f) : 0.0f;
	Constants.ContactData.y = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "ContactLength"), 1.0f);
	Constants.ContactData.z = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "ContactThickness"), 0.1f);
	Constants.ContactData.w = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "ContactDistance"), 1.0f);
	Constants.ContactDebug.x = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ScreenSpace", "ContactDebug");

	// Sun smoothing settings.
	Settings.SunSmoothing.SmoothSun = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.SunSmoothing", "SmoothSun");
	Settings.SunSmoothing.QuantizeSun = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.SunSmoothing", "QuantizeSun");
	Settings.SunSmoothing.SmoothingFactor = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "SmoothingFactor"), 0.0f, 1.0f);
	Settings.SunSmoothing.YawStepSize = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "YawStepSize"), 0.0f, 15.0f);
	Settings.SunSmoothing.PitchStepSize = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "PitchStepSize"), 0.0f, 15.0f);
	Settings.SunSmoothing.MaxJumpAngle = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "MaxJumpAngle"), 5.0f, 30.0f);

	// Generic exterior shadows settings
	Settings.Exteriors.Enabled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "Enabled");
	Settings.Exteriors.ForwardShadows = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "ForwardShadows");

	// Runtime half of the forward/deferred switch.
	//
	// The FORWARD_SHADOWS macro decides whether the forward code is COMPILED IN; this decides
	// whether it RUNS. Both the game shaders and SunShadows.fx read it, so the two halves hand
	// over in the same frame -- which matters, because the alternative is both paths applying
	// shadows at once. A macro alone cannot do this: game shaders have no runtime reload path
	// (ShaderCollection::SwitchShader is a stub), and the effect reload path is never triggered
	// (nothing ever sets ShaderManager::EffectReloadQueued), so the macro is frozen at whatever
	// it was when the shader was first compiled.
	Constants.ForwardData.x = Settings.Exteriors.ForwardShadows ? 0.0f : 1.0f;
	Constants.ForwardData.y = 0.0f;
	Constants.ForwardData.z = 0.0f;
	Constants.ForwardData.w = 0.0f;
	Settings.Exteriors.Quality = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "Quality"), 0, 4);
	Settings.Exteriors.Darkness = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "Darkness");
	Settings.Exteriors.NightMinDarkness = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "NightMinDarkness");
	Settings.Exteriors.UsePointShadowsDay = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "UsePointShadowsDay");
	Settings.Exteriors.UsePointShadowsNight = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "UsePointShadowsNight");

	// Shadow maps specific configuration.
	bool cascadeSettingsChanged = UpdateSettingsFromQuality(Settings.Exteriors.Quality);

	// Ortho map.
	bool orthoSettingsChanged = false;

	int oldOrthoResolution = Settings.OrthoMap.Resolution;

	Settings.OrthoMap.Resolution = 128 * pow(2, (std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Ortho", "Resolution"), 0, 4)));
	Settings.OrthoMap.Distance = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Ortho", "Distance"), 100.0f);
	Settings.OrthoMap.LimitFrequency = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Ortho", "LimitFrequency");

	if (oldOrthoResolution != 0 && oldOrthoResolution != Settings.OrthoMap.Resolution)
		orthoSettingsChanged = true;

	ShadowMaps[MapOrtho].Forms.AlphaEnabled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "AlphaEnabled");
	ShadowMaps[MapOrtho].Forms.Activators = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Activators");
	ShadowMaps[MapOrtho].Forms.Actors = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Actors");
	ShadowMaps[MapOrtho].Forms.Apparatus = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Apparatus");
	ShadowMaps[MapOrtho].Forms.Books = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Books");
	ShadowMaps[MapOrtho].Forms.Containers = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Containers");
	ShadowMaps[MapOrtho].Forms.Doors = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Doors");
	ShadowMaps[MapOrtho].Forms.Furniture = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Furniture");
	ShadowMaps[MapOrtho].Forms.Misc = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Misc");
	ShadowMaps[MapOrtho].Forms.Statics = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Statics");
	ShadowMaps[MapOrtho].Forms.Terrain = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Terrain");
	ShadowMaps[MapOrtho].Forms.Trees = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Trees");
	ShadowMaps[MapOrtho].Forms.Lod = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Lod");
	ShadowMaps[MapOrtho].Forms.MinRadius = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.FormsOrtho", "MinRadius");
	ShadowMaps[MapOrtho].Forms.OrigMinRadius = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.FormsOrtho", "MinRadius");

	// Interiors.
	Settings.Interiors.Enabled = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Enabled");
	Settings.Interiors.Forms.AlphaEnabled = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "AlphaEnabled");
	Settings.Interiors.Forms.Activators = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Activators");
	Settings.Interiors.Forms.Actors = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Actors");
	Settings.Interiors.Forms.Apparatus = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Apparatus");
	Settings.Interiors.Forms.Books = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Books");
	Settings.Interiors.Forms.Containers = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Containers");
	Settings.Interiors.Forms.Doors = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Doors");
	Settings.Interiors.Forms.Furniture = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Furniture");
	Settings.Interiors.Forms.Misc = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Misc");
	Settings.Interiors.Forms.Statics = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Statics");
	Settings.Interiors.Forms.MinRadius = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "MinRadius");
	Settings.Interiors.Quality = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Quality");
	// Up to ShadowSlotsMax with ForwardPointShadows indoors (and room in the atlas, made at startup
	// for LightPoints then: ShaderManager::GetNearbyLights caps it), else ShadowCubeMapsMax.
	Settings.Interiors.LightPoints = max(0, min(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "LightPoints"), ShadowSlotsMax));
	Settings.Interiors.TorchesCastShadows = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "TorchesCastShadows");
	Settings.Interiors.ShadowCubeMapSize = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ShadowCubeMapSize");
	Settings.Interiors.Darkness = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "Darkness");
	Settings.Interiors.LightRadiusMult = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "LightRadiusMult");
	Settings.Interiors.LightClusterRadius = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "LightClusterRadius");
	Settings.Interiors.ForwardPointShadows = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ForwardPointShadows");
	Settings.Interiors.FillLightRadius = max(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "FillLightRadius"), 0.0f);
	Settings.Interiors.FillLightShadowStrength = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "FillLightShadowStrength"), 0.0f, 1.0f);
	Settings.Interiors.NearFade = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "NearFade"), 0.0f, 256.0f);
	Settings.Interiors.PlayerInsideLamp = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "PlayerInsideLamp");
	Settings.Interiors.ShadowSoftness = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ShadowSoftness"), 0.0f, 4.0f);
	Settings.Interiors.ShadowNormalOffset = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ShadowNormalOffset"), 0.0f, 8.0f);
	Settings.Interiors.PCSS = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "PCSS");
	Settings.Interiors.RedrawActorsOnly = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "RedrawActorsOnly");
	Settings.Interiors.PCSSLightSize = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PCSSLightSize"), 0.0f, 32.0f);
	Settings.Interiors.PCSSMaxSpread = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PCSSMaxSpread"), 1.0f, 8.0f);
	Settings.Interiors.PCSSFilter = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "PCSSFilter"), 0, 2);
	Settings.Interiors.ContactStrength = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactStrength"), 0.0f, 1.0f);
	Settings.Interiors.ContactLength = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactLength"), 1.0f, 100.0f);
	Settings.Interiors.ContactThickness = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactThickness"), 0.5f, 50.0f);
	Settings.Interiors.ContactDistance = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactDistance"), 100.0f, 10000.0f);
	Settings.Interiors.ContactSamples = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ContactSamples"), 8, 64);
	Settings.Interiors.ContactLamps = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ContactLamps"), 1, 4);
	Settings.Interiors.ContactScreenLength = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactScreenLength"), 8.0f, 400.0f);
	Settings.Interiors.ContactNormalOffset = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactNormalOffset"), 0.0f, 4.0f);
	Settings.Interiors.ContactSoftness = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "ContactSoftness"), 0.0f, 4.0f);
	Settings.Interiors.ContactBlur = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ContactBlur");
	Settings.Interiors.DrawDistance = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "DrawDistance");
	Settings.Interiors.UseCastShadowFlag = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "UseCastShadowFlag");
	Settings.Interiors.PlayerShadowFirstPerson = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PlayerShadowFirstPerson");
	Settings.Interiors.PlayerShadowThirdPerson = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PlayerShadowThirdPerson");

	bool isExterior = TheShaderManager->GameState.isExterior;

	// if the effect was turned off the buffer must be cleared
	if (!Enabled || (isExterior && !Settings.Exteriors.Enabled) || (!isExterior && !Settings.Interiors.Enabled)) clearShadowsBuffer();

	// If certain shadow map settings were changed, recreate the textures and surfaces.
	if (texturesInitialized)
		RecreateTextures(cascadeSettingsChanged, orthoSettingsChanged, false);
}


void ShadowsExteriorEffect::clearShadowsBuffer() {
	// clear shadows buffer
	IDirect3DSurface9* currentRT;
	TheRenderManager->device->GetRenderTarget(0, &currentRT);
	TheRenderManager->device->SetRenderTarget(0, Textures.ShadowPassSurface);
	TheRenderManager->device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 255, 0, 0), 1.0f, 0L);
	TheRenderManager->device->SetRenderTarget(0, currentRT);
	currentRT->Release();
}


void ShadowsExteriorEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SmoothedSunDir", &Constants.SmoothedSunDir);
	TheShaderManager->RegisterConstant("TESR_ShadowData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_ShadowFormatData", &Constants.FormatData);
	TheShaderManager->RegisterConstant("TESR_ShadowForwardData", &Constants.ForwardData);
	TheShaderManager->RegisterConstant("TESR_ShadowBlur", &Constants.ShadowBlur);
	TheShaderManager->RegisterConstant("TESR_ShadowScreenSpaceData", &Constants.ScreenSpaceData);
	TheShaderManager->RegisterConstant("TESR_ShadowContactData", &Constants.ContactData);
	TheShaderManager->RegisterConstant("TESR_ShadowSunLight", &Constants.SunLight);
	TheShaderManager->RegisterConstant("TESR_ShadowAmbientLight", &Constants.AmbientLight);
	TheShaderManager->RegisterConstant("TESR_ShadowContactDebug", &Constants.ContactDebug);
	TheShaderManager->RegisterConstant("TESR_OrthoData", &Constants.OrthoData);
	TheShaderManager->RegisterConstant("TESR_ShadowFade", &Constants.ShadowFade);
	TheShaderManager->RegisterConstant("TESR_ShadowRadius", &Constants.ShadowMapRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowViewProjTransform", (D3DXVECTOR4*)&Constants.ShadowViewProj);
	TheShaderManager->RegisterConstant("TESR_ShadowNearCenter", &ShadowMaps[MapNear].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformNear", (D3DXVECTOR4*)&ShadowMaps[MapNear].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowMiddleCenter", &ShadowMaps[MapMiddle].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformMiddle", (D3DXVECTOR4*)&ShadowMaps[MapMiddle].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowFarCenter", &ShadowMaps[MapFar].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformFar", (D3DXVECTOR4*)&ShadowMaps[MapFar].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowLodCenter", &ShadowMaps[MapLod].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformLod", (D3DXVECTOR4*)&ShadowMaps[MapLod].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformOrtho", (D3DXVECTOR4*)&ShadowMaps[MapOrtho].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowCubeMapLightPosition", &Constants.ShadowCubeMapLightPosition);
	TheShaderManager->RegisterConstant("TESR_ShadowLightPosition", (D3DXVECTOR4*)&Constants.ShadowLightPosition);
	TheShaderManager->RegisterConstant("TESR_ShadowLightFade", (D3DXVECTOR4*)&Constants.ShadowLightFade);
	TheShaderManager->RegisterConstant("TESR_PointShadowData", &Constants.PointShadowData);
	TheShaderManager->RegisterConstant("TESR_PointShadowParams", &Constants.PointShadowParams);
	TheShaderManager->RegisterConstant("TESR_LampContactData", &Constants.LampContactData);
	TheShaderManager->RegisterConstant("TESR_LampContactExtra", &Constants.LampContactExtra);
	TheShaderManager->RegisterConstant("TESR_LampContactShape", &Constants.LampContactShape);
	TheShaderManager->RegisterConstant("TESR_PointShadowPCSS", &Constants.PointShadowPCSS);
}

// A shadow slot's cubemap, made the first time a slot past the first ShadowCubeMapsMax is used.
bool ShadowsExteriorEffect::EnsureShadowCube(UInt32 Slot) {
	if (Slot >= ShadowSlotsMax) return false;
	if (Textures.ShadowCubeMapTexture[Slot]) return true;
	const UINT Size = Settings.Interiors.ShadowCubeMapSize;
	if (FAILED(TheRenderManager->device->CreateCubeTexture(Size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &Textures.ShadowCubeMapTexture[Slot], NULL))) {
		Textures.ShadowCubeMapTexture[Slot] = nullptr;
		return false;
	}
	for (int Face = 0; Face < 6; Face++)
		Textures.ShadowCubeMapTexture[Slot]->GetCubeMapSurface((D3DCUBEMAP_FACES)Face, 0, &Textures.ShadowCubeMapSurface[Slot][Face]);
	return true;
}

void ShadowsExteriorEffect::RegisterTextures() {
	ULONG ShadowMapSize = Settings.ShadowMaps.CascadeResolution;
	ULONG ShadowCubeMapSize = Settings.Interiors.ShadowCubeMapSize;
	ULONG ShadowAtlasSize = ShadowMapSize * 2;

	TheTextureManager->InitTexture("TESR_ShadowAtlas", &ShadowAtlasTexture, &ShadowAtlasSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, Settings.ShadowMaps.Mipmaps);

	// Intermediate for the separable prefilter. Only allocated when the prefilter is on.
	if (Settings.ShadowMaps.Prefilter)
		TheTextureManager->InitTexture("TESR_ShadowAtlasBlur", &ShadowAtlasBlurTexture, &ShadowAtlasBlurSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, false);

	if (!Settings.ShadowMaps.MSAA)
		TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowAtlasDepthSurface, NULL);
	else {
		TheRenderManager->device->CreateRenderTarget(ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, D3DMULTISAMPLE_4_SAMPLES, 0, 0, &ShadowAtlasSurfaceMSAA, NULL);
		TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, true, &ShadowAtlasDepthSurface, NULL);
	}

	for (int i = 0; i <= MapLod; i++) {
		ShadowMaps[i].ShadowMapViewPort = { i % 2 == 0 ? 0 : ShadowMapSize, i < 2 ? 0 : ShadowMapSize, ShadowMapSize, ShadowMapSize, 0.0f, 1.0f };
		ShadowMaps[i].ShadowMapResolution = (float)ShadowMapSize;
		ShadowMaps[i].ShadowMapInverseResolution = 1.0f / (float)ShadowMapSize;
	}

	TheShaderManager->CreateFrameVertex(ShadowAtlasSize, ShadowAtlasSize, &ShadowAtlasVertexBuffer);
	Constants.ShadowBlur.x = 1.0f / (float)ShadowAtlasSize;

	// ortho texture
	ULONG orthoMapRes = Settings.OrthoMap.Resolution;
	TheTextureManager->InitTexture("TESR_OrthoMapBuffer", &ShadowMapOrthoTexture, &ShadowMapOrthoSurface, orthoMapRes, orthoMapRes, D3DFMT_R32F);
	TheRenderManager->device->CreateDepthStencilSurface(orthoMapRes, orthoMapRes, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowMapOrthoDepthSurface, NULL);
	ShadowMaps[MapOrtho].ShadowMapViewPort = { 0, 0, orthoMapRes, orthoMapRes, 0.0f, 1.0f };
	ShadowMaps[MapOrtho].ShadowMapResolution = (float)orthoMapRes;
	ShadowMaps[MapOrtho].ShadowMapInverseResolution = 1.0f / (float)orthoMapRes;


	// initialize spot lights maps
	for (int i = 0; i < SpotLightsMax; i++) {
		std::string textureName = "TESR_ShadowSpotlightBuffer" + std::to_string(i);
		TheTextureManager->InitTexture(textureName.c_str(), &Textures.ShadowSpotlightTexture[i], &Textures.ShadowSpotlightSurface[i], ShadowCubeMapSize, ShadowCubeMapSize, D3DFMT_R32F);
	}


	// initialize point lights cubemaps
	for (int i = 0; i < ShadowCubeMapsMax; i++) {
		TheRenderManager->device->CreateCubeTexture(ShadowCubeMapSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &Textures.ShadowCubeMapTexture[i], NULL);
		for (int j = 0; j < 6; j++) {
			Textures.ShadowCubeMapTexture[i]->GetCubeMapSurface((D3DCUBEMAP_FACES)j, 0, &Textures.ShadowCubeMapSurface[i][j]);
		}
		std::string textureName = "TESR_ShadowCubeMapBuffer" + std::to_string(i);
		TheTextureManager->RegisterTexture(textureName.c_str(), (IDirect3DBaseTexture9**)&Textures.ShadowCubeMapTexture[i]);
	}
	// Create the stencil surface used for rendering cubemaps
	TheRenderManager->device->CreateDepthStencilSurface(ShadowCubeMapSize, ShadowCubeMapSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &Textures.ShadowCubeMapDepthSurface, NULL);
	if (TheShadowManager) TheShadowManager->InvalidateCubeCache();   // new cubemaps: nothing cached is in them

	// The faces again, all in one texture, for the object shaders: room for LightPoints slots (at
	// least 12), six tiles each, in a near-square grid that fits the device's largest texture (fewer
	// slots if it must). None fits: lamps keep the post-process shadows.
	D3DCAPS9 Caps = {};
	TheRenderManager->device->GetDeviceCaps(&Caps);
	const UINT MaxColumns = Caps.MaxTextureWidth / ShadowCubeMapSize, MaxRows = Caps.MaxTextureHeight / ShadowCubeMapSize;
	UINT AtlasSlots = (UINT)std::clamp(Settings.Interiors.LightPoints, (int)ShadowCubeMapsMax, (int)ShadowSlotsMax);
	UINT Columns = 0, Rows = 0;
	for (; AtlasSlots > 0; AtlasSlots--) {
		const UINT Tiles = AtlasSlots * 6;
		Columns = min(MaxColumns, (UINT)ceilf(sqrtf((float)Tiles)));
		Rows = Columns ? (Tiles + Columns - 1) / Columns : 0;
		if (Columns && Rows <= MaxRows) break;
	}
	const UINT AtlasWidth = Columns * ShadowCubeMapSize;
	const UINT AtlasHeight = Rows * ShadowCubeMapSize;
	if (AtlasSlots)
		TheTextureManager->InitTexture("TESR_PointShadowAtlas", &Textures.PointShadowAtlasTexture, &Textures.PointShadowAtlasSurface, AtlasWidth, AtlasHeight, D3DFMT_R32F);
	Textures.PointShadowAtlasColumns = Columns;
	Textures.PointShadowAtlasRows = Rows;
	Textures.PointShadowAtlasWidth = AtlasWidth;
	Textures.PointShadowAtlasHeight = AtlasHeight;
	Textures.PointShadowAtlasSlots = Textures.PointShadowAtlasTexture ? AtlasSlots : 0;
	if (!Textures.PointShadowAtlasTexture)
		Logger::Log("[ERROR] Point shadow atlas: no room for even one lamp at face size %u on this device (%u x %u); lamps are shadowed in post-process", ShadowCubeMapSize, Caps.MaxTextureWidth, Caps.MaxTextureHeight);
	else
		Logger::Log("Point shadow atlas: %u lamp shadow slots, %u x %u", AtlasSlots, AtlasWidth, AtlasHeight);
	if (Textures.PointShadowAtlasSurface) {   // nothing drawn yet: lit everywhere
		IDirect3DSurface9* Target = nullptr;
		TheRenderManager->device->GetRenderTarget(0, &Target);
		TheRenderManager->device->SetRenderTarget(0, Textures.PointShadowAtlasSurface);
		TheRenderManager->device->Clear(0, NULL, D3DCLEAR_TARGET, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0);
		TheRenderManager->device->SetRenderTarget(0, Target);
		if (Target) Target->Release();
	}

	//TheShadowManager->ShadowCubeMapViewPort = { 0, 0, ShadowCubeMapSize, ShadowCubeMapSize, 0.0f, 1.0f };
	//memset(TheShadowManager->ShadowCubeMapLights, NULL, sizeof(ShadowCubeMapLights));

	// Initialize shadow buffer
	TheTextureManager->InitTexture("TESR_PointShadowBuffer", &Textures.ShadowPassTexture, &Textures.ShadowPassSurface, TheRenderManager->width, TheRenderManager->height, D3DFMT_G16R16);

	texturesInitialized = true;
}


/*
 * Recreate specific shadow maps, to be used after specific settings change.
 */
void ShadowsExteriorEffect::RecreateTextures(bool cascades, bool ortho, bool cubemaps) {
	if (cascades) {
		if (ShadowAtlasSurface) {
			ShadowAtlasSurface->Release();
			ShadowAtlasSurface = nullptr;
		}
		if (ShadowAtlasSurfaceMSAA) {
			ShadowAtlasSurfaceMSAA->Release();
			ShadowAtlasSurfaceMSAA = nullptr;
		}
		if (ShadowAtlasTexture) {
			ShadowAtlasTexture->Release();
			ShadowAtlasTexture = nullptr;
		};
		if (ShadowAtlasBlurSurface) {
			ShadowAtlasBlurSurface->Release();
			ShadowAtlasBlurSurface = nullptr;
		}
		if (ShadowAtlasBlurTexture) {
			ShadowAtlasBlurTexture->Release();
			ShadowAtlasBlurTexture = nullptr;
		}
		if (ShadowAtlasDepthSurface) {
			ShadowAtlasDepthSurface->Release();
			ShadowAtlasDepthSurface = nullptr;
		};
		if (ShadowAtlasVertexBuffer) {
			ShadowAtlasVertexBuffer->Release();
			ShadowAtlasVertexBuffer = nullptr;
		}

		ULONG ShadowMapSize = Settings.ShadowMaps.CascadeResolution;
		ULONG ShadowAtlasSize = ShadowMapSize * 2;

		TheTextureManager->InitTexture("TESR_ShadowAtlas", &ShadowAtlasTexture, &ShadowAtlasSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, Settings.ShadowMaps.Mipmaps);

		if (Settings.ShadowMaps.Prefilter)
			TheTextureManager->InitTexture("TESR_ShadowAtlasBlur", &ShadowAtlasBlurTexture, &ShadowAtlasBlurSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, false);

		if (!Settings.ShadowMaps.MSAA)
			TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowAtlasDepthSurface, NULL);
		else {
			TheRenderManager->device->CreateRenderTarget(ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, D3DMULTISAMPLE_4_SAMPLES, 0, 0, &ShadowAtlasSurfaceMSAA, NULL);
			TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, true, &ShadowAtlasDepthSurface, NULL);
		}

		for (int i = 0; i <= MapLod; i++) {
			ShadowMaps[i].ShadowMapViewPort = { i % 2 == 0 ? 0 : ShadowMapSize, i < 2 ? 0 : ShadowMapSize, ShadowMapSize, ShadowMapSize, 0.0f, 1.0f };
			ShadowMaps[i].ShadowMapResolution = (float)ShadowMapSize;
			ShadowMaps[i].ShadowMapInverseResolution = 1.0f / (float)ShadowMapSize;
		}

		TheShaderManager->CreateFrameVertex(ShadowAtlasSize, ShadowAtlasSize, &ShadowAtlasVertexBuffer);
		Constants.ShadowBlur.x = 1.0f / (float)ShadowAtlasSize;

		TheShaderManager->Effects.SunShadows->ClearSampler("TESR_ShadowAtlas", 16);

		// Game shaders sample the atlas too when forward sun shadows are on, and each holds
		// its own cached pointer to the texture just released above. Clearing only the
		// deferred effect leaves every object/terrain/parallax shader reading the dead
		// texture, which the device still references -- so the shadows freeze at whatever
		// was last rendered into it instead of failing outright.
		TheShaderManager->ClearShaderSamplers("TESR_ShadowAtlas", 16);
	}

	if (ortho) {
		if (ShadowMapOrthoSurface) {
			ShadowMapOrthoSurface->Release();
			ShadowMapOrthoSurface = nullptr;
		}
		if (ShadowMapOrthoTexture) {
			ShadowMapOrthoTexture->Release();
			ShadowMapOrthoTexture = nullptr;
		}
		// Released here because it is recreated below.
		if (ShadowMapOrthoDepthSurface) {
			ShadowMapOrthoDepthSurface->Release();
			ShadowMapOrthoDepthSurface = nullptr;
		}

		ULONG orthoMapRes = Settings.OrthoMap.Resolution;
		TheTextureManager->InitTexture("TESR_OrthoMapBuffer", &ShadowMapOrthoTexture, &ShadowMapOrthoSurface, orthoMapRes, orthoMapRes, D3DFMT_R32F);
		TheRenderManager->device->CreateDepthStencilSurface(orthoMapRes, orthoMapRes, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowMapOrthoDepthSurface, NULL);
		ShadowMaps[MapOrtho].ShadowMapViewPort = { 0, 0, orthoMapRes, orthoMapRes, 0.0f, 1.0f };
		ShadowMaps[MapOrtho].ShadowMapResolution = (float)orthoMapRes;
		ShadowMaps[MapOrtho].ShadowMapInverseResolution = 1.0f / (float)orthoMapRes;

		TheShaderManager->Effects.Rain->ClearSampler("TESR_OrthoMapBuffer", 19);
		TheShaderManager->Effects.Snow->ClearSampler("TESR_OrthoMapBuffer", 19);
		TheShaderManager->Effects.SnowAccumulation->ClearSampler("TESR_OrthoMapBuffer", 19);
		TheShaderManager->Effects.WetWorld->ClearSampler("TESR_OrthoMapBuffer", 19);
	}

	// Reset shadow manager frame counter.
	TheShadowManager->FrameCounter = 0;
}


/*
* Produce a smooth sun direction.
*/
D3DXVECTOR3 ShadowsExteriorEffect::CalculateSmoothedSunDir() {
	const float yawStepSize = D3DXToRadian(Settings.SunSmoothing.YawStepSize);		// horizontal rotation
	const float pitchStepSize = D3DXToRadian(Settings.SunSmoothing.PitchStepSize);	// vertical rotation
	const float smoothingFactor = Settings.SunSmoothing.SmoothingFactor;			// smoothing strength (0 = no smoothing, 1 = instant)
	const float maxJumpAngle = D3DXToRadian(Settings.SunSmoothing.MaxJumpAngle);	// if sun moves more than setting, apply instantly

	const bool quantizeSun = Settings.SunSmoothing.QuantizeSun && yawStepSize > 0.0f && pitchStepSize > 0.0f;
	const bool smoothSun = Settings.SunSmoothing.SmoothSun && smoothingFactor > 0.0f;

	D3DXVECTOR3 SunDir(TheShaderManager->ShaderConst.SunDir);

	if (quantizeSun) {
		float theta = atan2f(SunDir.y, SunDir.x);   // Yaw
		float phi = acosf(SunDir.z);				// Pitch

		theta = roundf(theta / yawStepSize) * yawStepSize;
		phi = roundf(phi / pitchStepSize) * pitchStepSize;

		SunDir.x = sinf(phi) * cosf(theta);
		SunDir.y = sinf(phi) * sinf(theta);
		SunDir.z = cosf(phi);
		D3DXVec3Normalize(&SunDir, &SunDir);
	}

	D3DXVECTOR3 SmoothedSunDir(Constants.SmoothedSunDir);

	if (smoothSun) {
		// Compute angle difference between smoothed and new direction
		float dotProduct = D3DXVec3Dot(&SunDir, &SmoothedSunDir);
		dotProduct = max(-1.0f, min(1.0f, dotProduct)); // Clamp to avoid NaN
		float angleDifference = acosf(dotProduct); // Angle between old and new direction

		// Apply smoothing only if the change is small
		if (angleDifference < maxJumpAngle) {
			D3DXVec3Lerp(&SmoothedSunDir, &SmoothedSunDir, &SunDir, smoothingFactor);
		}
		else {
			SmoothedSunDir = SunDir;
		}
	}
	else {
		SmoothedSunDir = SunDir;
	}
	
	Constants.SmoothedSunDir = D3DXVECTOR4(SmoothedSunDir, 0.0f);
	return SmoothedSunDir;
}


void ShadowsExteriorEffect::GetCascadeDepths() {
	NiCamera* sceneCamera = WorldSceneGraph->camera;

	float nearClip = sceneCamera->Frustum.Near;
	float farClip = sceneCamera->Frustum.Far;
	float clipRange = farClip - nearClip;

	float minZ = nearClip + 10.0f;
	float maxZ = min(nearClip + Settings.ShadowMaps.Distance, farClip);

	float range = maxZ - minZ;
	float ratio = maxZ / minZ;

	int cascadeCount = 4;

	for (int i = 0; i < cascadeCount; ++i) {
		float p = (i + 1) / static_cast<float>(cascadeCount);
		float log = minZ * std::pow(ratio, p);
		float uniform = minZ + range * p;
		float d = Settings.ShadowMaps.CascadeLambda * (log - uniform) + uniform;
		ShadowMaps[i].ShadowMapRadius = (d - nearClip) / clipRange;
	}

	// Get Near distance for each cascade
	ShadowMaps[MapNear].ShadowMapNear = 10.0f / clipRange;
	ShadowMaps[MapMiddle].ShadowMapNear = ShadowMaps[MapNear].ShadowMapRadius;
	ShadowMaps[MapFar].ShadowMapNear = ShadowMaps[MapMiddle].ShadowMapRadius;
	ShadowMaps[MapLod].ShadowMapNear = ShadowMaps[MapFar].ShadowMapRadius;

	// Ortho.
	ShadowMaps[MapOrtho].ShadowMapNear = 10.0f / clipRange;
	ShadowMaps[MapOrtho].ShadowMapRadius = Settings.OrthoMap.Distance / clipRange;

	// Store absolute Shadow map splits in Constants to pass to the Shaders
	Constants.ShadowMapRadius.x = ShadowMaps[MapNear].ShadowMapRadius * clipRange;
	Constants.ShadowMapRadius.y = ShadowMaps[MapMiddle].ShadowMapRadius * clipRange;
	Constants.ShadowMapRadius.z = ShadowMaps[MapFar].ShadowMapRadius * clipRange;
	Constants.ShadowMapRadius.w = ShadowMaps[MapLod].ShadowMapRadius * clipRange;

	// Reset blur constant for handling limited refresh rate.
	Constants.ShadowBlur.y = 1.0f;
}

// Banker round helper.
float BankerRound(float num) {
	float rounded = round(num);
	if (fabs(rounded - num) == 0.5f) {
		return 2.0 * round(0.5 * num);
	}
	return rounded;
}


// Round a vector using the banker round method.
void Vector3Round(D3DXVECTOR3* out, D3DXVECTOR3* in) {
	out->x = BankerRound(in->x);
	out->y = BankerRound(in->y);
	out->z = BankerRound(in->z);
}

void Vector4Round(D3DXVECTOR4* out, D3DXVECTOR4* in) {
	out->x = BankerRound(in->x);
	out->y = BankerRound(in->y);
	out->z = BankerRound(in->z);
	out->w = BankerRound(in->w);
}


// Generate the ViewProj matrix for a particular shadow cascade.
// Inspired by MJP's https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/ article and code example.
D3DXMATRIX ShadowsExteriorEffect::GetCascadeViewProj(ShadowMapSettings* ShadowMap, D3DXVECTOR3* SunDir) {
	// Get z-range for this cascade.
	NiCamera* sceneCamera = WorldSceneGraph->camera;
	NiPoint3 cameraPosition = sceneCamera->m_worldTransform.pos;
	float zNear = ShadowMap->ShadowMapNear;
	float zFar = ShadowMap->ShadowMapRadius;

	// Calculate the frustum corners in world space (from a unit cube in projective space).
	D3DXMATRIX* invViewProj = &TheRenderManager->InvViewProjMatrix;

	float ndcNear = 1.0f ? TheRenderManager->IsReversedDepth() : 0.0f;
	float ndcFar = 1.0f - ndcNear;
	D3DXVECTOR3 frustumCorners[8] = {
		D3DXVECTOR3(-1.0f,  1.0f, ndcNear), // Near plane.
		D3DXVECTOR3( 1.0f,  1.0f, ndcNear),
		D3DXVECTOR3( 1.0f, -1.0f, ndcNear),
		D3DXVECTOR3(-1.0f, -1.0f, ndcNear),
		D3DXVECTOR3(-1.0f,  1.0f, ndcFar),  // Far plane.
		D3DXVECTOR3( 1.0f,  1.0f, ndcFar),
		D3DXVECTOR3( 1.0f, -1.0f, ndcFar),
		D3DXVECTOR3(-1.0f, -1.0f, ndcFar),
	};
	for (auto i = 0; i < 8; ++i) {
		D3DXVec3TransformCoord(&frustumCorners[i], &frustumCorners[i], invViewProj);
	}

	// Get the corners of the current cascade slice of the view frustum.
	for (auto i = 0; i < 4; ++i)
	{
		D3DXVECTOR3 cornerRay = frustumCorners[i + 4] - frustumCorners[i];
		D3DXVECTOR3 nearCornerRay = cornerRay * zNear;
		D3DXVECTOR3 farCornerRay = cornerRay * zFar;
		frustumCorners[i + 4] = frustumCorners[i] + farCornerRay;
		frustumCorners[i] = frustumCorners[i] + nearCornerRay;
	}

	// Calculate the centroid of the view frustum slice.
	D3DXVECTOR3 frustumCenter(0.0f, 0.0f, 0.0f);
	for (auto i = 0; i < 8; ++i)
		frustumCenter = frustumCenter + frustumCorners[i];
	frustumCenter *= 1.0f / 8.0f;
	
	// Must be kept stable.
	D3DXVECTOR3 upDir(0.0f, 0.0f, 1.0f);

	D3DXVECTOR3 minExtents, maxExtents;

	// Calculate the radius of a bounding sphere surrounding the frustum corners
	float sphereRadius = 0.0f;
	for (auto i = 0; i < 8; ++i)
	{
		D3DXVECTOR3 centerToCorner = frustumCorners[i] - frustumCenter;
		float dist = D3DXVec3Length(&centerToCorner);
		sphereRadius = max(sphereRadius, dist);
	}
	sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

	// Modify sphere radius to compensate for lower than default FOV (aiming, zooming, ...).
	float defaultWorldFOV = *(float*)(0x120315C + 4);
	float currentWorldFOV = WorldSceneGraph->cameraFOV;
	float radiusFOVCompensation = tan(defaultWorldFOV * 0.5f * (3.1416f / 180.0f)) / tan(currentWorldFOV * 0.5f * (3.1416f / 180.0f));
	sphereRadius *= radiusFOVCompensation;

	maxExtents = D3DXVECTOR3(sphereRadius, sphereRadius, sphereRadius);
	minExtents = -maxExtents;
	
	D3DXVECTOR3 cascadeExtents = maxExtents - minExtents;

	// Create a shadow frustum center by moving the view frustum slice center away from the camera.
	// Should make it so we can more easily use the full resolution, which is mostly wasted due to
	// stabilization.
	D3DXVECTOR3 shadowFrustumCenter = frustumCenter;
	//D3DXVec3Normalize(&shadowFrustumCenter, &frustumCenter);  // Get the direction from camera to the frustum center.
	//shadowFrustumCenter *= sphereRadius;  // Move the center so that the length is equal to the sphere radius.
	
	ShadowMap->ShadowMapCascadeCenterRadius.x = shadowFrustumCenter.x;
	ShadowMap->ShadowMapCascadeCenterRadius.y = shadowFrustumCenter.y;
	ShadowMap->ShadowMapCascadeCenterRadius.z = shadowFrustumCenter.z;
	ShadowMap->ShadowMapCascadeCenterRadius.w = sphereRadius;

	// Calculate correct bound size limit for current cascade.
	ShadowMap->Forms.MinRadius = ShadowMap->Forms.OrigMinRadius * sphereRadius * ShadowMap->ShadowMapInverseResolution;

	float nearPlane = 0.0f;  // Shadow casters are pancaked to near plane in the vertex shader.
	float farPlane = cascadeExtents.z;
	D3DXVECTOR3 shadowCameraPos = shadowFrustumCenter + D3DXVECTOR3(*SunDir) * -minExtents.z;
	
	D3DXMATRIX shadowView, shadowProj, shadowViewProj;

	D3DXMatrixLookAtRH(&shadowView, &shadowCameraPos, &shadowFrustumCenter, &upDir);
	D3DXMatrixOrthoOffCenterRH(&shadowProj, minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, nearPlane, farPlane);
	shadowViewProj = shadowView * shadowProj;

	// Create the rounding matrix, by projecting the world-space origin and determining
	// the fractional offset in texel space.
	float sMapSize = ShadowMap->ShadowMapResolution;
	// We are working in camera relative world space - camera position is our fixed point for stabilization.
	D3DXVECTOR4 shadowOrigin(-cameraPosition.x, -cameraPosition.y, -cameraPosition.z, 1.0f);
	D3DXVec4Transform(&shadowOrigin, &shadowOrigin, &shadowViewProj);
	D3DXVec4Scale(&shadowOrigin, &shadowOrigin, sMapSize / 2.0f);
	D3DXVECTOR4 roundedOrigin, roundOffset;
	Vector4Round(&roundedOrigin, &shadowOrigin);
	D3DXVec4Subtract(&roundOffset, &roundedOrigin, &shadowOrigin);
	D3DXVec4Scale(&roundOffset, &roundOffset, 2.0f / sMapSize);

	shadowProj._41 = shadowProj._41 + roundOffset.x;
	shadowProj._42 = shadowProj._42 + roundOffset.y;

	shadowViewProj = shadowView * shadowProj;

	NiFrustum frustum(minExtents.x, maxExtents.x, maxExtents.y, minExtents.y, nearPlane, farPlane, true);
	TheCameraManager->SetFrustumPlanes(&ShadowMap->ShadowMapFrustumPlanes, &shadowViewProj, shadowCameraPos, frustum);
	ShadowMap->ShadowMapFrustumPlanes.SetActivePlaneState(62);

	// Cache the current camera translation. Used to offset against camera movement when using a cached map.
	ShadowMap->CameraTranslation = D3DXVECTOR3(cameraPosition.x, cameraPosition.y, cameraPosition.z);

	return shadowViewProj;
}

