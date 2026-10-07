#include <algorithm>
#include <cmath>

#include "CinematicDOF.h"

// Sampler registers, fixed in CinematicDOF.fx.hlsl. Render binds these itself for every pass so no
// pass samples the texture it is drawing into -- undefined in D3D9 and a feedback loop under DXVK.
static const DWORD FocusSampler = 4;
static const DWORD HalfASampler = 5;
static const DWORD HalfBSampler = 6;

void CinematicDOFEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_CinematicDOFLens", &Constants.Lens);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFFocus", &Constants.Focus);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFNear", &Constants.Near);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFAperture", &Constants.Aperture);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFBokeh", &Constants.Bokeh);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFShape", &Constants.Shape);
	TheShaderManager->RegisterConstant("TESR_CinematicDOFWeapon", &Constants.Weapon);
}

void CinematicDOFEffect::RegisterTextures() {
	int halfWidth = TheRenderManager->width / 2;
	int halfHeight = TheRenderManager->height / 2;

	TheTextureManager->InitTexture("TESR_CinematicDOFHalfA", &Textures.HalfATexture, &Textures.HalfASurface, halfWidth, halfHeight, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_CinematicDOFHalfB", &Textures.HalfBTexture, &Textures.HalfBSurface, halfWidth, halfHeight, D3DFMT_A16B16G16R16F);
	// 32-bit float: focus distances run past FP16's 65504 limit on long views. G32R32F because it is
	// the float render-target format NVR already relies on (CombineDepth's TESR_DepthBuffer).
	TheTextureManager->InitTexture("TESR_CinematicDOFFocusA", &Textures.FocusTexture[0], &Textures.FocusSurface[0], 1, 1, D3DFMT_G32R32F);
	TheTextureManager->InitTexture("TESR_CinematicDOFFocusB", &Textures.FocusTexture[1], &Textures.FocusSurface[1], 1, 1, D3DFMT_G32R32F);
}

void CinematicDOFEffect::UpdateSettings() {
	const char* Section = "Shaders.CinematicDOF.Main";

	// A key absent from the toml reads as 0. For the lens that would mean a 0mm lens or f/0 -- no
	// blur or a division by zero -- so 0 falls back to the shipped value instead.
	auto orDefault = [](float value, float fallback) { return value > 0.0f ? value : fallback; };

	// 0 always, 1 dialogue only. Aiming no longer switches the world blur on; the old values 2
	// (dialogue) and 3 (aiming or dialogue) both mean dialogue now, and so does the old 1 (aiming).
	Settings.Mode = TheSettingManager->GetSettingI(Section, "Mode") == 0 ? 0 : 1;
	Settings.FocalLength = std::clamp(orDefault(TheSettingManager->GetSettingF(Section, "FocalLength"), 50.0f), 10.0f, 300.0f);
	Settings.FStop = std::clamp(orDefault(TheSettingManager->GetSettingF(Section, "FStop"), 2.0f), 0.7f, 32.0f);
	Settings.AutoFocus = TheSettingManager->GetSettingI(Section, "AutoFocus") != 0;
	Settings.FocusDistance = orDefault(TheSettingManager->GetSettingF(Section, "FocusDistance"), 1000.0f);
	Settings.FocusSpeed = orDefault(TheSettingManager->GetSettingF(Section, "FocusSpeed"), 6.0f);
	Settings.MinFocusDistance = orDefault(TheSettingManager->GetSettingF(Section, "MinFocusDistance"), 50.0f);
	Settings.MaxBlur = std::clamp(orDefault(TheSettingManager->GetSettingF(Section, "MaxBlur"), 2.5f), 0.1f, 6.0f);
	Settings.HighlightBoost = std::clamp(TheSettingManager->GetSettingF(Section, "HighlightBoost"), 0.0f, 0.95f);
	Settings.WeaponBlur = std::clamp(TheSettingManager->GetSettingF(Section, "WeaponBlur"), 0.0f, 1.0f);
	Settings.TransitionTime = (std::max)(TheSettingManager->GetSettingF(Section, "TransitionTime"), 0.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(Section, "DebugView"), 0, 3);

	// Both are meaningful at 0 -- no extended sharp zone, and background-only depth of field -- so a
	// missing key reading as 0 is taken at its word.
	Settings.NearFocusRange = (std::max)(TheSettingManager->GetSettingF(Section, "NearFocusRange"), 0.0f);
	Settings.NearBlurStrength = std::clamp(TheSettingManager->GetSettingF(Section, "NearBlurStrength"), 0.0f, 2.0f);

	// Bokeh shape. Every one of these is the plain round disc at 0 -- a missing key included -- except
	// Anamorphic, where 0 would be a line, so it falls back to 1.
	Settings.ApertureBlades = std::clamp(TheSettingManager->GetSettingI(Section, "ApertureBlades"), 0, 9);
	Settings.BladeRotation = TheSettingManager->GetSettingF(Section, "BladeRotation");
	Settings.BladeCurvature = std::clamp(TheSettingManager->GetSettingF(Section, "BladeCurvature"), 0.0f, 1.0f);
	Settings.Anamorphic = std::clamp(orDefault(TheSettingManager->GetSettingF(Section, "Anamorphic"), 1.0f), 0.5f, 2.0f);
	Settings.CatsEye = std::clamp(TheSettingManager->GetSettingF(Section, "CatsEye"), 0.0f, 1.0f);
	Settings.RingBrightness = std::clamp(TheSettingManager->GetSettingF(Section, "RingBrightness"), -1.0f, 1.0f);
	Settings.HighlightThreshold = std::clamp(TheSettingManager->GetSettingF(Section, "HighlightThreshold"), 0.0f, 0.95f);
	Settings.BokehShape = std::clamp(TheSettingManager->GetSettingI(Section, "BokehShape"), 0, 4);
	Settings.ShapeDetail = std::clamp(TheSettingManager->GetSettingF(Section, "ShapeDetail"), 0.0f, 1.0f);
	// A missing key reads as 0, the 48-sample level this effect shipped with.
	Settings.BokehQuality = std::clamp(TheSettingManager->GetSettingI(Section, "BokehQuality"), 0, 2);
	// 0 is a real setting here (no smoothing), so a missing key reading as 0 is taken at its word.
	Settings.PostfilterRadius = std::clamp(TheSettingManager->GetSettingF(Section, "PostfilterRadius"), 0.0f, 2.0f);

	// Weapon depth of field. Off when the key is missing; the distances fall back to their defaults
	// rather than 0, which would put the whole weapon in focus or ramp it in a single step.
	Settings.WeaponDOF = std::clamp(TheSettingManager->GetSettingI(Section, "WeaponDOF"), 0, 2);
	Settings.WeaponFocusDistance = orDefault(TheSettingManager->GetSettingF(Section, "WeaponFocusDistance"), 40.0f);
	Settings.WeaponBlurRange = orDefault(TheSettingManager->GetSettingF(Section, "WeaponBlurRange"), 30.0f);
	Settings.WeaponMaxBlur = std::clamp(orDefault(TheSettingManager->GetSettingF(Section, "WeaponMaxBlur"), 0.6f), 0.05f, 3.0f);
}

void CinematicDOFEffect::UpdateConstants() {
	float dt = (float)TheFrameRateManager->ElapsedTime;
	if (!(dt > 0.0f) || dt > 0.5f) dt = 0.0f; // paused, first frame, or a hitch: hold rather than jump

	// When the world blur is on: always, or only in dialogue. Aiming is still tracked, but only for
	// weapon depth of field below, which switches off while aiming so the sights stay sharp.
	bool aiming = Player && Player->IsAiming();
	bool dialogue = TheShaderManager->GameState.isDialog || TheShaderManager->GameState.isPersuasion;
	bool active = Settings.Mode == 0 || dialogue;

	// Off in VATS, and cut rather than faded: VATS drives its own camera and screen effects, and the
	// blur showed ghosted copies of the scene there. The debug view still runs, for diagnosing it.
	bool vats = TheShaderManager->GameState.VATSIsOn;

	// Weapon depth of field has its own conditions: first person with a weapon in view, so not in
	// dialogue or VATS, and in hip-fire only mode not while aiming, where the sights have to stay sharp.
	bool firstPerson = Player && Player->IsFirstPerson();
	bool weaponActive = firstPerson && !dialogue &&
		(Settings.WeaponDOF == 2 || (Settings.WeaponDOF == 1 && !aiming));

	// Fade rather than snap, so raising the sights pulls focus instead of flicking a switch.
	auto ease = [&](float& value, float target) {
		if (Settings.TransitionTime <= 0.0f || vats) value = target;
		else if (dt > 0.0f) {
			float step = dt / Settings.TransitionTime;
			value = value < target ? (std::min)(value + step, target) : (std::max)(value - step, target);
		}
	};
	ease(blend, active && !vats ? 1.0f : 0.0f);
	ease(weaponBlend, weaponActive && !vats ? 1.0f : 0.0f);

	// Thin lens, blur in screen heights: f^2 / (N (zf - f)) * (z - zf) / z, over the sensor height.
	// The sensor is a 36mm-wide full frame matched to the screen's aspect, so FocalLength means what it
	// does on a real camera. The (zf - f) and depth terms need the focus distance, which lives on the
	// GPU, so only this focus-independent part is computed here.
	float aspect = (float)TheRenderManager->width / (float)TheRenderManager->height;
	Constants.Lens.x = Settings.FocalLength * Settings.FocalLength * aspect / (Settings.FStop * 36.0f);
	Constants.Lens.y = Settings.MaxBlur / 100.0f;
	Constants.Lens.z = Settings.HighlightBoost;
	Constants.Lens.w = Settings.WeaponBlur;

	Constants.Focus.x = Settings.FocusDistance;
	Constants.Focus.y = Settings.AutoFocus ? 1.0f : 0.0f;
	// Frame-rate independent easing toward the new focus distance.
	Constants.Focus.z = dt > 0.0f ? 1.0f - std::exp(-Settings.FocusSpeed * dt) : 0.0f;

	// The debug view forces full strength: the CoC is scaled by the fade, so without this the map
	// would be solid black whenever Mode has the effect switched off.
	Constants.Data.x = Settings.DebugView > 0 ? 1.0f : blend;
	Constants.Data.y = Settings.MinFocusDistance;
	Constants.Data.z = Settings.FocalLength;
	Constants.Data.w = (float)Settings.DebugView;
	Constants.Near = D3DXVECTOR4(Settings.NearFocusRange, Settings.NearBlurStrength, 0.0f, 0.0f);
	Constants.Aperture = D3DXVECTOR4((float)Settings.ApertureBlades, D3DXToRadian(Settings.BladeRotation), Settings.BladeCurvature, Settings.Anamorphic);
	Constants.Bokeh = D3DXVECTOR4(Settings.CatsEye, Settings.RingBrightness, Settings.HighlightThreshold, 0.0f);
	Constants.Shape = D3DXVECTOR4((float)Settings.BokehShape, Settings.ShapeDetail, Settings.PostfilterRadius, 0.0f);
	// Like Data.x, forced to full in the debug view so the weapon's blur shows on the map -- but only
	// when weapon depth of field is switched on at all.
	float weaponStrength = Settings.WeaponDOF == 0 ? 0.0f : (Settings.DebugView > 0 ? 1.0f : weaponBlend);
	Constants.Weapon = D3DXVECTOR4(Settings.WeaponFocusDistance, Settings.WeaponBlurRange, Settings.WeaponMaxBlur / 100.0f, weaponStrength);
}

bool CinematicDOFEffect::ShouldRender() {
	return blend > 0.001f || weaponBlend > 0.001f || Settings.DebugView > 0;
}

bool CinematicDOFEffect::DrawTechnique(Technique technique) {
	static const char* const Names[TechniqueCount] = { "Focus", "Prefilter", "Bokeh48", "Bokeh96", "Bokeh160", "Postfilter", "Combine" };

	if (techniquesGeneration != LoadGeneration) {
		for (int i = 0; i < TechniqueCount; i++) techniques[i] = Effect->GetTechniqueByName(Names[i]);
		techniquesGeneration = LoadGeneration;
	}

	D3DXHANDLE handle = techniques[technique];
	if (!handle) {
		Logger::Log("[ERROR] CinematicDOF : technique %s not found", Names[technique]);
		return false;
	}

	Effect->SetTechnique(handle);
	UINT passes;
	Effect->Begin(&passes, 0);
	Effect->BeginPass(0);
	TheRenderManager->device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	Effect->EndPass();
	Effect->End();
	return true;
}

void CinematicDOFEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || Effect == nullptr || !ShouldRender() ||
		!Textures.HalfASurface || !Textures.HalfBSurface || !Textures.FocusSurface[0] || !Textures.FocusSurface[1]) {
		// Autofocus should start from where the view is when it comes back, not ease in from
		// wherever it was last time.
		focusValid = false;
		renderTime = 0.0f;
		return;
	}

	auto timer = TimeLogger();

	Constants.Focus.w = focusValid ? 1.0f : 0.0f;
	if (SourceBuffer) Device->StretchRect(RenderTarget, NULL, SourceBuffer, NULL, D3DTEXF_NONE);
	SetCT();

	// Focus: read last frame's value from one 1x1 texture, write this frame's into the other. The
	// roles swap each frame, so no copy is needed and nothing is read while being written.
	int focusWrite = 1 - focusRead;
	Device->SetTexture(FocusSampler, Textures.FocusTexture[focusRead]);
	Device->SetRenderTarget(0, Textures.FocusSurface[focusWrite]);
	bool ok = DrawTechnique(TechniqueFocus);
	Device->SetTexture(FocusSampler, Textures.FocusTexture[focusWrite]); // every later pass reads this frame's focus

	// Prefilter: full-res frame -> HalfA.
	Device->SetTexture(HalfASampler, NULL);
	Device->SetTexture(HalfBSampler, NULL);
	Device->SetRenderTarget(0, Textures.HalfASurface);
	ok = ok && DrawTechnique(TechniquePrefilter);

	// Bokeh: HalfA -> HalfB.
	Device->SetTexture(HalfASampler, Textures.HalfATexture);
	Device->SetRenderTarget(0, Textures.HalfBSurface);
	static const Technique BokehByQuality[3] = { TechniqueBokeh48, TechniqueBokeh96, TechniqueBokeh160 };
	ok = ok && DrawTechnique(BokehByQuality[Settings.BokehQuality]);

	// Postfilter: HalfB -> HalfA.
	Device->SetTexture(HalfASampler, NULL);
	Device->SetTexture(HalfBSampler, Textures.HalfBTexture);
	Device->SetRenderTarget(0, Textures.HalfASurface);
	ok = ok && DrawTechnique(TechniquePostfilter);

	// Combine: HalfA over the sharp frame -> the frame. Always restores the frame as the target, so a
	// failed pass above leaves the image untouched rather than the device pointed at a scratch buffer.
	Device->SetTexture(HalfBSampler, NULL);
	Device->SetTexture(HalfASampler, Textures.HalfATexture);
	Device->SetRenderTarget(0, RenderTarget);
	if (ok && DrawTechnique(TechniqueCombine)) {
		if (RenderedSurface) Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		focusRead = focusWrite;
		focusValid = true;
	}
	else {
		focusValid = false;
	}

	renderTime = timer.LogTime("EffectRecord::Render CinematicDOF");
}
