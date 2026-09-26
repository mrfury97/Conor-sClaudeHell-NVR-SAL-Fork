#include "ImGuiManager.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <unordered_set>
#include <deque>
#include <algorithm>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef HRESULT(WINAPI* GetDeviceState_t)(IDirectInputDevice8*, DWORD, LPVOID);
static GetDeviceState_t OriginalGetDeviceState = nullptr;

bool    ImGuiManager::Initialized     = false;
bool    ImGuiManager::Visible         = false;
bool    ImGuiManager::DeviceLost      = false;
HWND    ImGuiManager::GameWindow      = nullptr;
WNDPROC ImGuiManager::OriginalWndProc = nullptr;

static std::string SelectedSection;

// ---- DirectInput mouse block (zeros lX/lY/lZ deltas + buttons) ---------------
// xNVSE has no public API to block mouse movement deltas, so we patch the
// IDirectInputDevice8 vtable directly.  The hook is installed once and never
// removed; it simply passes through when the overlay is not visible.

static float s_pendingWheelDelta    = 0.0f;
static float s_shaderStepSize       = 0.1f;
static bool  s_colorStatesNeedReset = false;
static bool  s_plusPressed          = false;
static bool  s_minusPressed         = false;
static int   s_masterMod            = -1; // -1 = not yet read from settings

// Keyboard state snapshot for edge-detection polling in NewFrame.
// Initialized to current key states when overlay opens so held keys don't misfire.
static BYTE s_prevKeyState[256] = {};

// Per-setting revert snapshot: section -> key -> value at overlay open time.
// Populated lazily on first render of each setting; cleared on overlay open.
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> s_snapshot;

// ---- Confabulator -------------------------------------------------------

static bool  s_cfabOpen         = false;
static bool  s_cfabActive       = false;
static float s_cfabX            = 0.0f;
static float s_cfabY            = 0.0f;
static float s_cfabZ            = 0.0f;
static bool  s_cfabLFO          = false;
static float s_cfabLFORate      = 0.04f;
static float s_cfabLFOPhases[3] = { 0.0f, 2.094f, 4.189f };

struct CfabSnapshot {
	char  name[64];
	float x, y, z;
};
static CfabSnapshot s_cfabSnaps[8]        = {};
static int          s_cfabSnapCount       = 0;
static char         s_cfabSnapNameBuf[64] = "snapshot";

struct CfabBaselines {
	float saturation   = -0.1f;
	float curveR       =  0.9f;
	float curveG       =  1.0f;
	float curveB       =  1.0f;
	float dofBlur      =  1.0f;
	float sharpening   =  0.75f;
	float bloom        =  1.0f;
	float chroma       =  1.0f;
	float filmGrain    =  0.3f;
	float vignetteDark =  1.2f;
	float vignetteRad  =  0.6f;
	float lensStrength =  0.4f;
	float lensSmudge   =  0.0f;
	float tmSaturation =  1.0f;
	float tmContrast   =  1.0f;
	float godRaysMult  =  1.0f;
	float fogAmount    =  0.4f;
	bool  loaded       =  false;
};
static CfabBaselines s_cfabBase;

struct CfabScales {
	float saturation   = 1.00f;
	float curveR       = 0.20f;
	float curveG       = 0.05f;
	float curveB       = 0.15f;
	float tmSat        = 0.05f;
	float tmContrast   = 0.03f;
	float dofBlur      = 6.0f;
	float sharpening   = 1.5f;
	float bloom        = 4.0f;
	float godRays      = 0.5f;
	float chroma       = 5.0f;
	float filmGrain    = 1.3f;
	float vignetteDark = 2.5f;
	float vignetteRad  = 0.5f;
	float lensSmudge   = 0.7f;
	float lensStrength = 0.5f;
	float fog          = 0.3f;
};
static CfabScales s_cfabScales;

// ---- Dev Tools panel -------------------------------------------------------
static bool  s_devOpen        = false;
static float s_savedTimeScale = -1.0f; // session snapshot; -1 = not yet taken
static bool  s_devFreecamOn   = false;
static bool  s_devMenusHidden = false;
static bool  s_screenshotMode = false;

// ---- Preset Manager panel ------------------------------------------------
// Session 2: minimal read-only skeleton. Session 5 (current): promoted into
// the real status-indicator/save-button UI (docs/preset-manager-design.md
// § "In-game UI -- location assignment").
static bool  s_presetManagerOpen = false;

// ---- Lighting panel -------------------------------------------------------
// QOL panel added after re-adding forward shadows changed the lighting model:
// a linkable-slider view over PBR/Terrain intensity, grouped by the condition
// a tester actually thinks in (Day/Night/Rain/Interiors) instead of by
// effect. Each condition row can be Unlinked (every slider independent),
// ByEffect (PBR's 3 link together, Terrain's 3 link together, separately),
// or AllLinked (all 6 move together) -- see RenderLightingConditionRow.
static bool s_lightingPanelOpen = false;

enum class LightLinkMode { Unlinked, ByEffect, AllLinked };

// Rain and Night+Rain both live under the same "Rain" toolbar condition
// (there's no separate NightRain button), each keeping its own link mode.
static LightLinkMode s_lightLinkDay       = LightLinkMode::Unlinked;
static LightLinkMode s_lightLinkNight     = LightLinkMode::Unlinked;
static LightLinkMode s_lightLinkRain      = LightLinkMode::Unlinked;
static LightLinkMode s_lightLinkNightRain = LightLinkMode::Unlinked;
static LightLinkMode s_lightLinkInteriors = LightLinkMode::Unlinked;

static void CfabSaveBaselines() {
	if (!TheSettingManager || s_cfabBase.loaded) return;
	s_cfabBase.saturation   = TheSettingManager->GetSettingF("Shaders.Coloring.Default",             "Saturation");
	s_cfabBase.curveR       = TheSettingManager->GetSettingF("Shaders.Coloring.Default",             "ColorCurveR");
	s_cfabBase.curveG       = TheSettingManager->GetSettingF("Shaders.Coloring.Default",             "ColorCurveG");
	s_cfabBase.curveB       = TheSettingManager->GetSettingF("Shaders.Coloring.Default",             "ColorCurveB");
	s_cfabBase.dofBlur      = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "BaseBlurRadius");
	s_cfabBase.sharpening   = TheSettingManager->GetSettingF("Shaders.Sharpening.Main",              "Strength");
	s_cfabBase.bloom        = TheSettingManager->GetSettingF("Shaders.Bloom.Main",                   "Strength");
	s_cfabBase.chroma       = TheSettingManager->GetSettingF("Shaders.Cinema.Main",                  "ChromaticAberration");
	s_cfabBase.filmGrain    = TheSettingManager->GetSettingF("Shaders.Cinema.Main",                  "FilmGrainAmount");
	s_cfabBase.vignetteDark = TheSettingManager->GetSettingF("Shaders.Cinema.Main",                  "VignetteDarkness");
	s_cfabBase.vignetteRad  = TheSettingManager->GetSettingF("Shaders.Cinema.Main",                  "VignetteRadius");
	s_cfabBase.lensStrength = TheSettingManager->GetSettingF("Shaders.Lens.Main",              "Strength");
	s_cfabBase.lensSmudge   = TheSettingManager->GetSettingF("Shaders.Lens.Main",              "Smudginess");
	s_cfabBase.tmSaturation = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main",       "Saturation");
	s_cfabBase.tmContrast   = TheSettingManager->GetSettingF("Shaders.Tonemapping.Main",       "TonemapContrast");
	s_cfabBase.godRaysMult  = TheSettingManager->GetSettingF("Shaders.GodRays.Main",           "DayMultiplier");
	s_cfabBase.fogAmount    = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Main",     "Amount");
	s_cfabBase.loaded = true;
}

static void CfabRestoreBaselines() {
	if (!TheSettingManager || !s_cfabBase.loaded) return;
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "Saturation",          s_cfabBase.saturation);
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "ColorCurveR",         s_cfabBase.curveR);
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "ColorCurveG",         s_cfabBase.curveG);
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "ColorCurveB",         s_cfabBase.curveB);
	TheSettingManager->SetSetting("Shaders.DepthOfField.FirstPersonView", "BaseBlurRadius",      s_cfabBase.dofBlur);
	TheSettingManager->SetSetting("Shaders.Sharpening.Main",              "Strength",            s_cfabBase.sharpening);
	TheSettingManager->SetSetting("Shaders.Bloom.Main",                   "Strength",            s_cfabBase.bloom);
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "ChromaticAberration", s_cfabBase.chroma);
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "FilmGrainAmount",     s_cfabBase.filmGrain);
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "VignetteDarkness",    s_cfabBase.vignetteDark);
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "VignetteRadius",      s_cfabBase.vignetteRad);
	TheSettingManager->SetSetting("Shaders.Lens.Main",            "Strength",        s_cfabBase.lensStrength);
	TheSettingManager->SetSetting("Shaders.Lens.Main",            "Smudginess",      s_cfabBase.lensSmudge);
	TheSettingManager->SetSetting("Shaders.Tonemapping.Main",     "Saturation",      s_cfabBase.tmSaturation);
	TheSettingManager->SetSetting("Shaders.Tonemapping.Main",     "TonemapContrast", s_cfabBase.tmContrast);
	TheSettingManager->SetSetting("Shaders.GodRays.Main",         "DayMultiplier",   s_cfabBase.godRaysMult);
	TheSettingManager->SetSetting("Shaders.VolumetricFog.Main",   "Amount",          s_cfabBase.fogAmount);
	TheSettingManager->LoadSettings();
	s_cfabBase.loaded = false;
}

static void CfabDeactivateIfActive() {
	if (!s_cfabActive) return;
	s_cfabActive = false;
	CfabRestoreBaselines();
}

static void CfabApply(float x, float y, float z) {
	if (!TheSettingManager || !s_cfabBase.loaded) return;
	const CfabBaselines& b  = s_cfabBase;
	const CfabScales&    sc = s_cfabScales;
	// X — Color Push
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "Saturation",          b.saturation    + x * sc.saturation);
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "ColorCurveR",         b.curveR        + x * sc.curveR);
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "ColorCurveG",         b.curveG        + x * sc.curveG);
	TheSettingManager->SetSetting("Shaders.Coloring.Default",             "ColorCurveB",         b.curveB        - x * sc.curveB);
	TheSettingManager->SetSetting("Shaders.Tonemapping.Main",             "Saturation",          b.tmSaturation  + x * sc.tmSat);
	TheSettingManager->SetSetting("Shaders.Tonemapping.Main",             "TonemapContrast",     b.tmContrast    + x * sc.tmContrast);
	// Y — Focus/Dream
	TheSettingManager->SetSetting("Shaders.DepthOfField.FirstPersonView", "BaseBlurRadius",      ImMax(0.0f, b.dofBlur     + y * sc.dofBlur));
	TheSettingManager->SetSetting("Shaders.Sharpening.Main",              "Strength",            ImMax(0.0f, b.sharpening  - y * sc.sharpening));
	TheSettingManager->SetSetting("Shaders.Bloom.Main",                   "Strength",            ImMax(0.0f, b.bloom       + y * sc.bloom));
	TheSettingManager->SetSetting("Shaders.GodRays.Main",                 "DayMultiplier",       ImMax(0.0f, b.godRaysMult + y * sc.godRays));
	// Z — Grime/Cinema
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "ChromaticAberration", ImMax(0.0f, b.chroma        + z * sc.chroma));
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "FilmGrainAmount",     ImMax(0.0f, b.filmGrain     + z * sc.filmGrain));
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "VignetteDarkness",    ImMax(0.0f, b.vignetteDark  + z * sc.vignetteDark));
	TheSettingManager->SetSetting("Shaders.Cinema.Main",                  "VignetteRadius",      ImClamp(b.vignetteRad - z * sc.vignetteRad, 0.0f, 1.0f));
	TheSettingManager->SetSetting("Shaders.Lens.Main",                    "Smudginess",          ImMax(0.0f, b.lensSmudge    + z * sc.lensSmudge));
	TheSettingManager->SetSetting("Shaders.Lens.Main",                    "Strength",            ImMax(0.0f, b.lensStrength  + z * sc.lensStrength));
	TheSettingManager->SetSetting("Shaders.VolumetricFog.Main",           "Amount",              ImMax(0.0f, b.fogAmount     + z * sc.fog));
	TheSettingManager->LoadSettings();
}

static void CfabUpdate() {
	if (s_cfabActive && s_cfabLFO) {
		float dPhase = ImGui::GetIO().DeltaTime * s_cfabLFORate * 6.2832f;
		s_cfabLFOPhases[0] += dPhase;
		s_cfabLFOPhases[1] += dPhase;
		s_cfabLFOPhases[2] += dPhase;
		s_cfabX = sinf(s_cfabLFOPhases[0]);
		s_cfabY = sinf(s_cfabLFOPhases[1]);
		s_cfabZ = sinf(s_cfabLFOPhases[2]);
	}
	if (s_cfabActive) CfabApply(s_cfabX, s_cfabY, s_cfabZ);
}

static void RenderConfabulator() {
	if (!s_cfabOpen) return;

	ImGui::SetNextWindowSize(ImVec2(480.0f, 700.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(980.0f, 60.0f),   ImGuiCond_FirstUseEver);

	ImGuiWindowFlags wflags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (!ImGui::Begin("Salamandrastic Retro-Encabulator", &s_cfabOpen, wflags)) {
		ImGui::End();
		if (!s_cfabOpen) CfabDeactivateIfActive();
		return;
	}
	if (!s_cfabOpen) { ImGui::End(); CfabDeactivateIfActive(); return; }

	if (ImGui::Checkbox("Active", &s_cfabActive)) {
		if (s_cfabActive) CfabSaveBaselines();
		else              CfabRestoreBaselines();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("overrides Coloring / Tonemapping / DoF / Bloom / GodRays / Cinema / Fog");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// ---------- XY pad ----------
	const float padSize = 220.0f;
	ImVec2 padTL = ImGui::GetCursorScreenPos();
	ImVec2 padBR = ImVec2(padTL.x + padSize, padTL.y + padSize);

	bool lfoLocked = s_cfabLFO && s_cfabActive;
	if (lfoLocked) ImGui::BeginDisabled();
	ImGui::InvisibleButton("##xypad", ImVec2(padSize, padSize));
	if (ImGui::IsItemActive()) {
		ImVec2 mp = ImGui::GetIO().MousePos;
		s_cfabX = ImClamp(((mp.x - padTL.x) / padSize) * 2.0f - 1.0f, -1.0f, 1.0f);
		s_cfabY = ImClamp(-((mp.y - padTL.y) / padSize) * 2.0f + 1.0f, -1.0f, 1.0f);
	}
	if (lfoLocked) ImGui::EndDisabled();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilledMultiColor(padTL, padBR,
		IM_COL32(15, 20, 55, 255),
		IM_COL32(55, 25, 10, 255),
		IM_COL32(40, 18,  5, 255),
		IM_COL32( 5, 15, 35, 255));
	dl->AddRect(padTL, padBR, IM_COL32(110, 110, 170, 220), 3.0f, 0, 1.5f);
	float cx = padTL.x + padSize * 0.5f, cy = padTL.y + padSize * 0.5f;
	dl->AddLine(ImVec2(padTL.x, cy), ImVec2(padBR.x, cy), IM_COL32(70, 70, 100, 160));
	dl->AddLine(ImVec2(cx, padTL.y), ImVec2(cx, padBR.y), IM_COL32(70, 70, 100, 160));
	ImU32 lc = IM_COL32(150, 150, 200, 200);
	dl->AddText(ImVec2(padTL.x + 5, padTL.y + 4),  lc,                            "dream");
	dl->AddText(ImVec2(padTL.x + 5, padBR.y - 17), lc,                            "crisp");
	dl->AddText(ImVec2(padTL.x + 4, cy - 8),        IM_COL32(110, 150, 220, 200), "< cool");
	dl->AddText(ImVec2(padBR.x - 44, cy - 8),       IM_COL32(220, 160,  90, 200), "warm >");
	float dotX = padTL.x + (s_cfabX + 1.0f) * 0.5f * padSize;
	float dotY = padTL.y + (1.0f - (s_cfabY + 1.0f) * 0.5f) * padSize;
	dl->AddCircleFilled(ImVec2(dotX, dotY), 9.0f, IM_COL32(255, 200, 50, 240));
	dl->AddCircle(ImVec2(dotX, dotY),       9.0f, IM_COL32(255, 255, 255, 180), 0, 1.5f);
	dl->AddLine(ImVec2(dotX - 5, dotY), ImVec2(dotX + 5, dotY), IM_COL32(0, 0, 0, 180));
	dl->AddLine(ImVec2(dotX, dotY - 5), ImVec2(dotX, dotY + 5), IM_COL32(0, 0, 0, 180));

	ImGui::SameLine(0.0f, 8.0f);

	// ---------- Z slider ----------
	ImGui::BeginGroup();
	ImGui::TextDisabled("grime");
	if (lfoLocked) ImGui::BeginDisabled();
	ImGui::VSliderFloat("##z", ImVec2(28.0f, padSize - 34.0f), &s_cfabZ, -1.0f, 1.0f, "");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Z  %.2f\ngrime / cinema axis", s_cfabZ);
	if (lfoLocked) ImGui::EndDisabled();
	ImGui::TextDisabled("clean");
	ImGui::EndGroup();

	ImGui::Spacing();
	ImGui::Text("X %+.2f   Y %+.2f   Z %+.2f", s_cfabX, s_cfabY, s_cfabZ);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// ---------- LFO controls ----------
	ImGui::Checkbox("LFO Drift", &s_cfabLFO);
	if (s_cfabLFO) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderFloat("##rate", &s_cfabLFORate, 0.002f, 0.25f, "%.3f Hz");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drift speed. Axes are 120 deg out of phase with each other.");
		ImGui::SameLine();
		if (ImGui::SmallButton("Shuffle")) {
			s_cfabLFOPhases[0] = (float)(rand() % 628) / 100.0f;
			s_cfabLFOPhases[1] = (float)(rand() % 628) / 100.0f;
			s_cfabLFOPhases[2] = (float)(rand() % 628) / 100.0f;
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize phase offsets so axes fall out of sync.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// ---------- Snapshots ----------
	if (ImGui::CollapsingHeader("Snapshots")) {
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputText("##snapname", s_cfabSnapNameBuf, sizeof(s_cfabSnapNameBuf));
		ImGui::SameLine();
		bool canSave = s_cfabSnapCount < 8 && s_cfabSnapNameBuf[0] != '\0';
		if (!canSave) ImGui::BeginDisabled();
		if (ImGui::SmallButton("Save")) {
			CfabSnapshot& sn = s_cfabSnaps[s_cfabSnapCount++];
			strncpy_s(sn.name, s_cfabSnapNameBuf, sizeof(sn.name) - 1);
			sn.x = s_cfabX; sn.y = s_cfabY; sn.z = s_cfabZ;
		}
		if (!canSave) ImGui::EndDisabled();
		ImGui::Spacing();
		for (int i = 0; i < s_cfabSnapCount; i++) {
			ImGui::PushID(i);
			ImGui::Text("%-18s  %+.2f %+.2f %+.2f",
				s_cfabSnaps[i].name, s_cfabSnaps[i].x, s_cfabSnaps[i].y, s_cfabSnaps[i].z);
			ImGui::SameLine();
			if (ImGui::SmallButton("Recall")) {
				s_cfabX   = s_cfabSnaps[i].x;
				s_cfabY   = s_cfabSnaps[i].y;
				s_cfabZ   = s_cfabSnaps[i].z;
				s_cfabLFO = false;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) {
				for (int j = i; j < s_cfabSnapCount - 1; j++)
					s_cfabSnaps[j] = s_cfabSnaps[j + 1];
				s_cfabSnapCount--;
			}
			ImGui::PopID();
		}
		if (s_cfabSnapCount == 0) ImGui::TextDisabled("No snapshots saved.");
		ImGui::Spacing();
	}

	// ---------- Scales ----------
	if (ImGui::CollapsingHeader("Scales")) {
		auto scaleRow = [](const char* label, float* val, float defVal) {
			ImGui::PushID(label);
			ImGui::SetNextItemWidth(70.0f);
			ImGui::DragFloat("##s", val, 0.005f, 0.0f, 0.0f, "%.3f");
			ImGui::SameLine();
			if (ImGui::SmallButton("-")) *val = ImMax(0.0f, *val - s_shaderStepSize);
			ImGui::SameLine();
			if (ImGui::SmallButton("+")) *val += s_shaderStepSize;
			ImGui::SameLine();
			if (ImGui::SmallButton("=")) *val = defVal;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to %.3f", defVal);
			ImGui::SameLine();
			ImGui::TextUnformatted(label);
			ImGui::PopID();
		};
		ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Color Push (X)");
		scaleRow("Saturation",    &s_cfabScales.saturation, 1.00f);
		scaleRow("CurveR",        &s_cfabScales.curveR,     0.20f);
		scaleRow("CurveG",        &s_cfabScales.curveG,     0.05f);
		scaleRow("CurveB",        &s_cfabScales.curveB,     0.15f);
		scaleRow("TM Saturation", &s_cfabScales.tmSat,      0.05f);
		scaleRow("TM Contrast",   &s_cfabScales.tmContrast, 0.03f);
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Focus/Dream (Y)");
		scaleRow("DoF Blur",   &s_cfabScales.dofBlur,    6.0f);
		scaleRow("Sharpening", &s_cfabScales.sharpening, 1.5f);
		scaleRow("Bloom",      &s_cfabScales.bloom,      4.0f);
		scaleRow("GodRays",    &s_cfabScales.godRays,    0.5f);
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.7f, 0.4f, 0.9f, 1.0f), "Grime/Cinema (Z)");
		scaleRow("Chroma",       &s_cfabScales.chroma,       5.0f);
		scaleRow("FilmGrain",    &s_cfabScales.filmGrain,    1.3f);
		scaleRow("VignetteDark", &s_cfabScales.vignetteDark, 2.5f);
		scaleRow("VignetteRad",  &s_cfabScales.vignetteRad,  0.5f);
		scaleRow("LensSmudge",   &s_cfabScales.lensSmudge,   0.7f);
		scaleRow("LensStrength", &s_cfabScales.lensStrength, 0.5f);
		scaleRow("Fog",          &s_cfabScales.fog,          0.3f);
		ImGui::Spacing();
	}

	ImGui::Separator();
	ImGui::Spacing();

	// ---------- Live parameter readout ----------
	const CfabBaselines& b  = s_cfabBase;
	const CfabScales&    sc = s_cfabScales;
	float bSat  = b.loaded ? b.saturation   : -0.1f;
	float bCR   = b.loaded ? b.curveR       :  0.9f;
	float bCB   = b.loaded ? b.curveB       :  1.0f;
	float bDof  = b.loaded ? b.dofBlur      :  1.0f;
	float bShrp = b.loaded ? b.sharpening   :  0.75f;
	float bBlm  = b.loaded ? b.bloom        :  1.0f;
	float bGR   = b.loaded ? b.godRaysMult  :  1.0f;
	float bCA   = b.loaded ? b.chroma       :  1.0f;
	float bFG   = b.loaded ? b.filmGrain    :  0.3f;
	float bVD   = b.loaded ? b.vignetteDark :  1.2f;
	float bLSm  = b.loaded ? b.lensSmudge   :  0.0f;
	float bFog  = b.loaded ? b.fogAmount    :  0.4f;
	float bTmS  = b.loaded ? b.tmSaturation :  1.0f;
	float bTmC  = b.loaded ? b.tmContrast   :  1.0f;

	if (ImGui::BeginTable("##cfab", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableSetupColumn("Color Push  (X)");
		ImGui::TableSetupColumn("Focus/Dream  (Y)");
		ImGui::TableSetupColumn("Grime/Cinema  (Z)");
		ImGui::TableHeadersRow();

		struct Row { const char* la; float va; const char* lb; float vb; const char* lc; float vc; };
		Row rows[] = {
			{ "Saturation", bSat + s_cfabX * sc.saturation,
			  "DoF Blur",   ImMax(0.f, bDof  + s_cfabY * sc.dofBlur),
			  "Chroma",     ImMax(0.f, bCA   + s_cfabZ * sc.chroma) },
			{ "CurveR",     bCR  + s_cfabX * sc.curveR,
			  "Sharpening", ImMax(0.f, bShrp - s_cfabY * sc.sharpening),
			  "FilmGrain",  ImMax(0.f, bFG   + s_cfabZ * sc.filmGrain) },
			{ "CurveB",     bCB  - s_cfabX * sc.curveB,
			  "Bloom",      ImMax(0.f, bBlm  + s_cfabY * sc.bloom),
			  "Vignette",   ImMax(0.f, bVD   + s_cfabZ * sc.vignetteDark) },
			{ "TM Sat",     bTmS + s_cfabX * sc.tmSat,
			  "GodRays",    ImMax(0.f, bGR   + s_cfabY * sc.godRays),
			  "LensSmudge", ImMax(0.f, bLSm  + s_cfabZ * sc.lensSmudge) },
			{ "TM Contrast",bTmC + s_cfabX * sc.tmContrast,
			  "",           0.f,
			  "Fog",        ImMax(0.f, bFog  + s_cfabZ * sc.fog) },
		};
		for (const auto& r : rows) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%-10s %.3f", r.la, r.va);
			ImGui::TableSetColumnIndex(1); if (r.lb[0]) ImGui::Text("%-10s %.3f", r.lb, r.vb);
			ImGui::TableSetColumnIndex(2); ImGui::Text("%-10s %.3f", r.lc, r.vc);
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

// ---- Preset Manager debug window (Session 2) --------------------------

static const char* PresetTierName(PresetManager::ResolvedTier tier) {
	switch (tier) {
	case PresetManager::ResolvedTier::Keyword:  return "Keyword";
	case PresetManager::ResolvedTier::Override: return "Override";
	default:                                    return "Default";
	}
}

// ---- Session 5: Save/Reload confirmation --------------------------------
// docs/preset-manager-design.md § "In-game UI -- location assignment"

enum class PresetPendingAction { None, Save, Reload, Load, RefreshAll, SaveVariant, Delete };
static PresetPendingAction s_presetPendingAction = PresetPendingAction::None;
static std::string         s_presetPendingTarget;  // preset/Variant name a pending action targets
static std::string         s_presetPendingWarning;
// Set by RequestPresetAction, consumed by RenderPresetConfirmPopup. The
// actual ImGui::OpenPopup() call is deferred to RenderPresetConfirmPopup
// (called from BuildUI()'s top level) rather than fired immediately from
// inside the "NVR Preset Manager" window's button handler -- see the long
// comment on RenderPresetConfirmPopup for why that distinction matters.
static bool s_presetPopupRequested = false;

// ---- Session 6: Preset browser / Load -----------------------------------
// docs § "In-game UI -- Preset browser / Load", § "Unsaved/previewing state"
static std::string s_presetSelectedName;    // row selected in the browser list, if any
static bool        s_previewActive = false; // true while a Load preview hasn't been saved/discarded yet
static std::string s_previewSourceName;     // name of the preset currently being previewed
static UInt32      s_previewStartGeneration = 0; // PresetManager::GetResolveGeneration() when the preview started

static void PresetManagerPerformSave(const std::string& TargetName) {
	PresetManager::PresetData live;
	if (!PresetManager::CaptureLiveState(live)) {
		Logger::Log("PresetManager: [Preset] Save to '%s' failed -- couldn't capture live state", TargetName.c_str());
		return;
	}
	if (!PresetManager::WritePreset(TargetName, live)) {
		Logger::Log("PresetManager: [Preset] Save to '%s' failed -- couldn't write file", TargetName.c_str());
		return;
	}
	Logger::Log("PresetManager: [Preset] Saved current settings to '%s'", TargetName.c_str());
	// Refresh the resolved tier immediately -- otherwise the status
	// indicators wouldn't reflect the new file until the next cell change.
	if (Player && Player->parentCell)
		PresetManager::ResolveAndApply(Player->parentCell);
}

static void PresetManagerPerformReload() {
	if (Player && Player->parentCell)
		PresetManager::ResolveAndApply(Player->parentCell);
	Logger::Log("PresetManager: [Preset] Reloaded current preset, discarding unsaved edits");
}

static void PresetManagerPerformLoad(const std::string& Name) {
	PresetManager::PresetData data;
	if (!PresetManager::ReadPreset(Name, data)) {
		Logger::Log("PresetManager: [Preset] Load '%s' failed -- couldn't read file", Name.c_str());
		return;
	}
	// Wholesale replace, not stacked with the current tier's contents or the
	// enabled Variants -- docs: "replaces the live editing state wholesale
	// with that preset's full contents." Nothing is written to disk; the
	// current location's actual resolved assignment is untouched until an
	// explicit Save.
	PresetManager::ApplyPreviewPreset(data);
	s_previewActive = true;
	s_previewSourceName = Name;
	s_previewStartGeneration = PresetManager::GetResolveGeneration();
	Logger::Log("PresetManager: [Preset] Loaded '%s' as an unsaved preview", Name.c_str());
}

static void PresetManagerPerformDelete(const std::string& Name) {
	if (!PresetManager::DeletePreset(Name)) {
		Logger::Log("PresetManager: [Preset] Delete '%s' failed -- reserved name, missing file, or filesystem error", Name.c_str());
		return;
	}
	Logger::Log("PresetManager: [Preset] Deleted '%s'", Name.c_str());

	if (s_presetSelectedName == Name) s_presetSelectedName.clear();
	// The file backing an in-progress preview is gone -- the already-applied
	// live values are unaffected (preview doesn't re-read the file), but drop
	// the "previewing X" state so the UI doesn't keep pointing at it.
	if (s_previewActive && s_previewSourceName == Name) s_previewActive = false;

	// Refresh the resolved tier immediately, same reasoning as Save/Reload --
	// otherwise the status indicators wouldn't reflect the deletion until the
	// next real location change, and if the deleted preset was the one
	// actually active here, they'd keep showing it as live.
	if (Player && Player->parentCell)
		PresetManager::ResolveAndApply(Player->parentCell);
}

// ---- Session 7: Refresh All Presets, Save Variant ------------------------
// docs § "Refresh all presets", § "Variants" authoring flow

static void PresetManagerPerformRefreshAll() {
	auto summaries = PresetManager::RefreshAllPresets();
	for (const auto& summary : summaries) {
		Logger::Log("PresetManager: [Preset] Refresh backfilled %u key(s) into '%s':",
			(UInt32)summary.AddedKeys.size(), summary.PresetName.c_str());
		for (const auto& line : summary.AddedKeys)
			Logger::Log("PresetManager: [Preset]   %s", line.c_str());
	}
	Logger::Log("PresetManager: [Preset] Refresh All Presets done -- %u file(s) touched", (UInt32)summaries.size());
}

static void PresetManagerPerformSaveVariant(const std::string& Name) {
	PresetManager::PresetData diff;
	PresetManager::CaptureVariantDiff(diff);
	if (diff.empty()) {
		Logger::Log("PresetManager: [Preset] Save Variant '%s' skipped -- no live changes since the base preset loaded", Name.c_str());
		return;
	}
	if (!PresetManager::WriteVariant(Name, diff)) {
		Logger::Log("PresetManager: [Preset] Save Variant '%s' failed -- couldn't write file", Name.c_str());
		return;
	}
	UInt32 keyCount = 0;
	for (const auto& [section, keys] : diff) keyCount += (UInt32)keys.size();
	Logger::Log("PresetManager: [Preset] Saved Variant '%s' (%u changed key(s))", Name.c_str(), keyCount);
}

// Requests a Save or Reload confirmation. Call exactly once, from the
// triggering button's click handler. Does NOT call ImGui::OpenPopup itself --
// see RenderPresetConfirmPopup for why that has to happen elsewhere.
static void RequestPresetAction(PresetPendingAction Kind, const std::string& TargetName, const std::string& Warning) {
	s_presetPendingAction = Kind;
	s_presetPendingTarget = TargetName;
	s_presetPendingWarning = Warning;
	Logger::Log("PresetManager: [Preset] Confirmation requested for '%s' (kind=%d)", TargetName.c_str(), (int)Kind);
	s_presetPopupRequested = true;
}

// Call unconditionally, once per frame, from BuildUI()'s own top level (NOT
// from inside another window's Begin/End, and NOT from inside the button
// handler that requests the popup).
//
// Why: ImGui::GetID(str_id) -- used internally by both OpenPopup and
// BeginPopupModal -- hashes str_id against whatever window is current on the
// ID stack *at the moment of the call*. The original code called OpenPopup()
// immediately from inside RenderPresetManagerPanel()'s Begin/End (so it
// hashed against the "NVR Preset Manager" window's ID), then called
// BeginPopupModal() separately from BuildUI()'s top level, outside any
// window (a different ID context). The two IDs never matched, so the modal
// could never actually open -- RequestPresetAction's log line fired every
// time, but "Confirm OK clicked" never did, and no popup ever appeared.
// Deferring the OpenPopup() call itself to this function (called from the
// same top-level context as BeginPopupModal, every frame) keeps both calls
// on the same ID stack, and also means the popup keeps working even if the
// "NVR Preset Manager" window is closed mid-confirm.
static void RenderPresetConfirmPopup() {
	if (s_presetPopupRequested) {
		ImGui::OpenPopup("Confirm##presetaction");
		s_presetPopupRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal("Confirm##presetaction", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::TextWrapped("%s", s_presetPendingWarning.c_str());
	ImGui::Spacing();

	if (ImGui::Button("OK", ImVec2(120.0f, 0.0f))) {
		Logger::Log("PresetManager: [Preset] Confirm OK clicked for '%s' (kind=%d)",
			s_presetPendingTarget.c_str(), (int)s_presetPendingAction);
		if (s_presetPendingAction == PresetPendingAction::Reload)
			PresetManagerPerformReload();
		else if (s_presetPendingAction == PresetPendingAction::Load)
			PresetManagerPerformLoad(s_presetPendingTarget);
		else if (s_presetPendingAction == PresetPendingAction::RefreshAll)
			PresetManagerPerformRefreshAll();
		else if (s_presetPendingAction == PresetPendingAction::SaveVariant)
			PresetManagerPerformSaveVariant(s_presetPendingTarget);
		else if (s_presetPendingAction == PresetPendingAction::Delete)
			PresetManagerPerformDelete(s_presetPendingTarget);
		else
			PresetManagerPerformSave(s_presetPendingTarget);
		s_presetPendingAction = PresetPendingAction::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
		s_presetPendingAction = PresetPendingAction::None;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

static void RenderPresetManagerPanel() {
	if (!s_presetManagerOpen) return;

	ImGui::SetNextWindowSize(ImVec2(460.0f, 640.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(540.0f, 380.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("NVR Preset Manager", &s_presetManagerOpen)) {
		ImGui::End();
		return;
	}

	// A smidge bigger throughout this window -- purely a render-time scale,
	// applied before any CalcTextSize call below so the label-column math
	// that follows already accounts for it.
	ImGui::SetWindowFontScale(1.1f);

	// ---- Master on/off toggle -------------------------------------------
	// Off: the automatic per-location resolve (ShaderManager::UpdateConstants's
	// gate) never fires, so whatever's currently live stays live as you move
	// around -- your own settings are authoritative. Everything below (Save/
	// Load/Delete/Reload, Variants) still works regardless -- this only stops
	// the automatic switching, not deliberate authoring. Persisted through the
	// normal TOML settings (Main.Main.Misc, blacklisted from preset capture
	// the same way RenderEffects itself already is), not the ImGui ini --
	// consistent with how every other persistent preference in this mod is
	// stored, and it rides along with the existing Save/Save Copy/Disk reload
	// machinery for free.
	{
		bool enabled = TheSettingManager->SettingsMain.Main.PresetManagerEnabled;
		if (ImGui::Checkbox("Preset Manager enabled", &enabled)) {
			TheSettingManager->SetSetting("Main.Main.Misc", "PresetManagerEnabled", enabled);
			TheSettingManager->LoadSettings();
			Logger::Log("PresetManager: [Preset] Master toggle set to %s", enabled ? "enabled" : "disabled");
			// Re-enabling: resolve immediately rather than waiting for the next
			// location change to notice -- same immediate-refresh pattern Save/
			// Reload/Delete already use.
			if (enabled && Player && Player->parentCell)
				PresetManager::ResolveAndApply(Player->parentCell);
		}
		if (!enabled)
			ImGui::TextDisabled("Automatic switching is off -- your own settings are authoritative. Save/Load/Delete below still work.");
	}
	ImGui::Separator();

	// Walking to a different location without saving silently discards an
	// in-progress Load preview (docs § "Unsaved/previewing state") -- the
	// generation counter bumps on every real ResolveAndApply, i.e. every
	// actual cell transition, so a mismatch here means exactly that happened.
	if (s_previewActive && PresetManager::GetResolveGeneration() != s_previewStartGeneration) {
		Logger::Log("PresetManager: [Preset] Unsaved preview of '%s' discarded -- location changed", s_previewSourceName.c_str());
		s_previewActive = false;
	}

	const auto& r = PresetManager::GetLastResolveResult();
	const ImVec4 kHighlight(0.4f, 1.0f, 0.7f, 1.0f);
	const ImVec4 kWarning(1.0f, 0.8f, 0.2f, 1.0f);

	ImGui::Text("Cell EditorID:       %s", r.CellEditorID.empty() ? "(none)" : r.CellEditorID.c_str());
	ImGui::Text("Worldspace EditorID: %s", r.WorldspaceEditorID.empty() ? "(none)" : r.WorldspaceEditorID.c_str());
	ImGui::Text("IsInterior:          %s", r.IsInterior ? "true" : "false");

	// While previewing, the three status indicators below switch to this one
	// distinct "Unsaved" banner in place of their normal shown/highlighted
	// display -- the location's actually-resolved tier hasn't changed, only
	// the live editing state has (docs § "Unsaved/previewing state").
	if (s_previewActive)
		ImGui::TextColored(kWarning, "Unsaved -- previewing '%s'", s_previewSourceName.c_str());

	ImGui::Separator();

	// Label column width is measured, not guessed -- a fixed SameLine offset
	// (the previous "140.0f") clips as soon as a keyword name is long enough
	// to run into the button (e.g. "[Keyword: DCHouse]"). Measure whichever
	// of the three labels will actually render this frame and align the
	// button column just past the widest one, so it can never overlap
	// regardless of how long a keyword name gets.
	bool showKeywordRow = r.IsInterior && !r.Keyword.empty();
	char keywordLabel[160];
	snprintf(keywordLabel, sizeof(keywordLabel), "[Keyword: %s]", r.Keyword.c_str());
	// Parenthesized calls (std::max)(...) -- windows.h's max/min macros
	// (absent NOMINMAX) would otherwise mangle a bare std::max(a, b) here.
	float labelColumnW = ImGui::CalcTextSize("[Default]").x;
	labelColumnW = (std::max)(labelColumnW, ImGui::CalcTextSize("[Override]").x);
	if (showKeywordRow)
		labelColumnW = (std::max)(labelColumnW, ImGui::CalcTextSize(keywordLabel).x);
	const float buttonColumnX = labelColumnW + 24.0f; // breathing room past the longest label

	// ---- Default -- always shown, highlighted when nothing more specific applies.
	{
		bool highlighted = !s_previewActive && (r.Tier == PresetManager::ResolvedTier::Default);
		if (highlighted) ImGui::TextColored(kHighlight, "[Default]"); else ImGui::Text("[Default]");
		ImGui::SameLine(buttonColumnX);
		if (ImGui::SmallButton("Save to Default")) {
			std::string targetName = r.IsInterior ? PresetManager::kDefaultInteriorName : PresetManager::kDefaultExteriorName;
			const char* scope = r.IsInterior ? "every interior" : "every exterior";
			char warning[192];
			snprintf(warning, sizeof(warning), "This will rewrite the floor for %s in the game. Continue?", scope);
			Logger::Log("PresetManager: [Preset] Save to Default clicked, target='%s'", targetName.c_str());
			RequestPresetAction(PresetPendingAction::Save, targetName, warning);
		}
	}

	ImGui::Spacing();

	// ---- Keyword -- interior only, shown whenever the cell has a tag at all,
	// even with no preset authored for it yet (docs' shown-vs-highlighted table).
	if (showKeywordRow) {
		bool highlighted = !s_previewActive && (r.Tier == PresetManager::ResolvedTier::Keyword);
		if (highlighted) ImGui::TextColored(kHighlight, "%s", keywordLabel);
		else ImGui::Text("%s", keywordLabel);
		ImGui::SameLine(buttonColumnX);
		if (ImGui::SmallButton("Save to Keyword")) {
			UInt32 count = PresetManager::CountCellsForKeyword(r.Keyword);
			char warning[192];
			snprintf(warning, sizeof(warning), "This affects %u cell(s) using keyword '%s'. Continue?", count, r.Keyword.c_str());
			RequestPresetAction(PresetPendingAction::Save, r.Keyword, warning);
		}
	}
	// Save-to-Keyword is deliberately absent, not just disabled, when the
	// cell has no tag -- keyword membership is strictly offline (docs
	// "Keyword files"), no in-game path exists to unlock this button.

	ImGui::Spacing();

	// ---- Override -- if one exists it's always the active tier (resolution
	// checks it first), so "shown" and "highlighted" collapse to the same test.
	{
		bool overrideExists = (r.Tier == PresetManager::ResolvedTier::Override);
		bool highlighted = !s_previewActive && overrideExists;
		std::string identity = r.IsInterior ? r.CellEditorID : r.WorldspaceEditorID;
		if (highlighted) ImGui::TextColored(kHighlight, "[Override]"); else ImGui::Text("[Override]");
		ImGui::SameLine(buttonColumnX);
		bool canSave = !identity.empty();
		if (!canSave) ImGui::BeginDisabled();
		if (ImGui::SmallButton("Save to Override")) {
			if (overrideExists)
				RequestPresetAction(PresetPendingAction::Save, identity,
					"An override for this location already exists and will be overwritten. Continue?");
			else
				PresetManagerPerformSave(identity); // nothing to overwrite -- no warning needed
		}
		if (!canSave) ImGui::EndDisabled();
	}

	ImGui::Separator();

	if (ImGui::Button("Reload current preset"))
		RequestPresetAction(PresetPendingAction::Reload, "",
			"This will discard any unsaved live edits and reload what's actually assigned to this location. Continue?");

	ImGui::Separator();

	// ---- Session 6: Preset browser / Load -- a persistent, always-visible
	// list of every existing Default/Keyword/Override preset (docs § "In-game
	// UI -- Preset browser / Load"). Re-enumerated fresh off disk every
	// frame the window's open, same never-cached philosophy as the rest of
	// this panel -- a preset saved a moment ago via the buttons above shows
	// up immediately, with no separate refresh step.
	ImGui::TextUnformatted("Preset browser");
	{
		auto presets = PresetManager::ListAllPresets();

		ImGui::BeginChild("##presetBrowser", ImVec2(0.0f, 130.0f), true);
		for (const auto& entry : presets) {
			const char* kindTag =
				entry.Kind == PresetManager::PresetKind::Keyword  ? "[Keyword] " :
				entry.Kind == PresetManager::PresetKind::Default  ? "[Default] " :
				                                                     "[Override] ";
			char label[256];
			snprintf(label, sizeof(label), "%s%s", kindTag, entry.Name.c_str());
			if (ImGui::Selectable(label, s_presetSelectedName == entry.Name))
				s_presetSelectedName = entry.Name;
		}
		if (presets.empty())
			ImGui::TextDisabled("(no presets on disk yet)");
		ImGui::EndChild();

		bool canLoad = !s_presetSelectedName.empty();
		if (!canLoad) ImGui::BeginDisabled();
		if (ImGui::Button("Load")) {
			Logger::Log("PresetManager: [Preset] Load clicked, target='%s'", s_presetSelectedName.c_str());
			RequestPresetAction(PresetPendingAction::Load, s_presetSelectedName,
				"This will override your current unsaved settings. Continue?");
		}
		if (!canLoad) ImGui::EndDisabled();

		// DefaultInterior/DefaultExterior are protected -- disable rather than
		// let the click through and rely on DeletePreset's own refusal, so the
		// button honestly reflects what's about to happen.
		bool canDelete = canLoad && !PresetManager::IsReservedName(s_presetSelectedName);
		ImGui::SameLine();
		if (!canDelete) ImGui::BeginDisabled();
		if (ImGui::Button("Delete")) {
			Logger::Log("PresetManager: [Preset] Delete clicked, target='%s'", s_presetSelectedName.c_str());
			char warning[288];
			snprintf(warning, sizeof(warning), "Permanently delete '%s'? This cannot be undone.", s_presetSelectedName.c_str());
			RequestPresetAction(PresetPendingAction::Delete, s_presetSelectedName, warning);
		}
		if (!canDelete) ImGui::EndDisabled();
	}

	ImGui::Separator();

	// ---- Session 7: Variants -- a panel of checkboxes reflecting which
	// Variants are currently enabled (docs § "In-game UI -- Variants"),
	// global toggles independent of the current location.
	ImGui::TextUnformatted("Variants");
	{
		auto variantNames = PresetManager::ListAllVariants();
		const auto& enabled = PresetManager::GetEnabledVariants();

		if (variantNames.empty()) {
			ImGui::TextDisabled("(no Variants on disk yet)");
		} else {
			for (const auto& name : variantNames) {
				bool isEnabled = std::find(enabled.begin(), enabled.end(), name) != enabled.end();
				if (ImGui::Checkbox(name.c_str(), &isEnabled))
					PresetManager::SetVariantEnabled(name, isEnabled);
			}
		}

		if (!enabled.empty()) {
			std::string joined;
			for (size_t i = 0; i < enabled.size(); i++) {
				if (i) joined += " -> ";
				joined += enabled[i];
			}
			ImGui::TextDisabled("Priority: %s (left lowest, right highest)", joined.c_str());
		}
	}

	ImGui::Spacing();

	// ---- Save Variant -- captures exactly the key(s) changed since the
	// current base preset became live (docs § "Variants" authoring flow):
	// load any base preset, tweak only the intended setting(s), name and
	// save. Reuses the same confirm-popup machinery as Save/Load when it
	// would overwrite an existing Variant of the same name.
	static char s_variantNameBuf[64] = "";
	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputText("##variantname", s_variantNameBuf, sizeof(s_variantNameBuf));
	ImGui::SameLine();
	if (ImGui::Button("Save Variant")) {
		std::string name = s_variantNameBuf;
		if (name.empty()) {
			Logger::Log("PresetManager: [Preset] Save Variant clicked with an empty name -- ignored");
		} else if (PresetManager::IsReservedName(name)) {
			Logger::Log("PresetManager: [Preset] Save Variant '%s' rejected -- reserved name", name.c_str());
		} else if (PresetManager::VariantExists(name)) {
			RequestPresetAction(PresetPendingAction::SaveVariant, name,
				"A Variant with this name already exists and will be overwritten. Continue?");
		} else {
			PresetManagerPerformSaveVariant(name);
		}
	}

	ImGui::End();
}

// ---- Dev Tools panel -------------------------------------------------------

static void RunConsoleCommand(const char* cmd) {
	if (g_ConsoleInterface) g_ConsoleInterface->RunScriptLine(cmd, nullptr);
}

// Custom message used to defer a console command off the render thread --
// see RunConsoleCommandDeferred's comment. Minted via RegisterWindowMessage
// rather than a hardcoded WM_APP + offset: WM_APP's collision-free private
// range only covers WM_APP..WM_APP+0x3FFF (0x8000-0xBFFF); anything past
// that lands in 0xC000-0xFFFF, the range RegisterWindowMessage hands out,
// where some other component in-process could legitimately own that exact
// value for its own signaling and collide with this one. RegisterWindowMessage
// is guaranteed unique system-wide for a given name, which is what this
// needs. Cached in a function-local static so every caller gets the same
// value without re-registering.
static UINT GetDeferredConsoleCommandMessage() {
	static UINT message = RegisterWindowMessageA("NVR_DeferredConsoleCommand");
	return message;
}

// coc (and any other command that triggers a multi-frame loading screen)
// cannot be run synchronously via RunConsoleCommand from a button handler --
// every button handler in this file executes inside RenderInterfaceHook,
// itself part of the game's own D3D9 render call chain for the CURRENT
// frame (see NewVegas/Hooks/Render.cpp: ImGuiManager::NewFrame()/Render()
// are both called from directly inside it). A command that needs to
// Present() further frames to show loading progress can never get past the
// frame we're still inside of -- confirmed root cause of "CoC freezes the
// game completely." Posting a message defers the actual RunConsoleCommand
// call to the next time the game's message pump processes its queue, which
// happens outside of any render call.
static void RunConsoleCommandDeferred(const char* cmd) {
	HWND window = ImGuiManager::GetWindow();
	if (!window) { RunConsoleCommand(cmd); return; } // no window yet -- best effort
	PostMessage(window, GetDeferredConsoleCommandMessage(), 0, (LPARAM)_strdup(cmd));
}

// Unlike worldspaces, interior TESObjectCELL records are NOT preloaded into
// a stable "every cell in the load order" list -- the DataHandler's own cell
// array is a runtime cache of cells actually instantiated so far, not a
// static catalog, and there's no xNVSE API here for a true engine-level
// enumerator (deliberately out of scope for the preset manager itself, see
// docs § "Scope"). So this list is honest about being partial: every
// interior cell mentioned in a keyword file (already cached at boot) plus
// every interior cell the player has actually stood in this session, most
// recent first. It grows as you play instead of claiming completeness it
// can't back up.
static std::vector<std::string> s_visitedInteriorCells; // MRU, most-recent first
static std::string              s_lastTrackedCellID;

static void TrackVisitedInteriorCell() {
	if (!Player || !Player->parentCell || !Player->parentCell->IsInterior()) return;
	const char* name = Player->parentCell->GetEditorName();
	if (!name || !name[0] || s_lastTrackedCellID == name) return;
	s_lastTrackedCellID = name;

	auto it = std::find(s_visitedInteriorCells.begin(), s_visitedInteriorCells.end(), name);
	if (it != s_visitedInteriorCells.end()) s_visitedInteriorCells.erase(it);
	s_visitedInteriorCells.insert(s_visitedInteriorCells.begin(), name);
	if (s_visitedInteriorCells.size() > 50) s_visitedInteriorCells.resize(50);
}

// Renders up to 6 DragFloats (PBR's 3, then Terrain's 3 when TotalCount ==
// 6) and, on any single edit, multiplies every other value currently
// *grouped* with it by the same before/after ratio -- which values that is
// depends on Mode: AllLinked groups everything passed in, ByEffect groups
// PBR's 3 and Terrain's 3 separately, Unlinked groups nothing (plain
// independent drag). Returns true if anything changed, so the caller knows
// to write the whole row back.
static bool RenderLightingCells(LightLinkMode Mode, const char* const* Labels, float** Values, int PbrCount, int TotalCount) {
	bool anyChanged = false;
	for (int i = 0; i < TotalCount; i++) {
		float before = *Values[i];
		ImGui::PushID(i);
		bool changed = ImGui::DragFloat(Labels[i], Values[i], 0.005f, 0.0f, 0.0f, "%.3f");
		ImGui::PopID();
		if (!changed) continue;
		anyChanged = true;
		if (before == 0.0f) continue; // no ratio to infer from a zero baseline -- leave siblings alone

		float ratio = *Values[i] / before;
		int lo = i, hi = i + 1; // Unlinked: nothing else moves
		if (Mode == LightLinkMode::AllLinked) { lo = 0; hi = TotalCount; }
		else if (Mode == LightLinkMode::ByEffect) { lo = (i < PbrCount) ? 0 : PbrCount; hi = (i < PbrCount) ? PbrCount : TotalCount; }

		for (int j = lo; j < hi; j++) {
			if (j == i) continue;
			*Values[j] *= ratio;
		}
	}
	return anyChanged;
}

// HasTerrain == false (Interiors) hides "All Linked" rather than offering it
// as a confusing no-op identical to "By Effect" when there's no Terrain group.
static void RenderLightLinkModeSelector(LightLinkMode& Mode, bool HasTerrain) {
	int mode = (int)Mode;
	ImGui::RadioButton("Unlinked", &mode, (int)LightLinkMode::Unlinked); ImGui::SameLine();
	ImGui::RadioButton("By Effect", &mode, (int)LightLinkMode::ByEffect);
	if (HasTerrain) {
		ImGui::SameLine();
		ImGui::RadioButton("All Linked", &mode, (int)LightLinkMode::AllLinked);
	}
	Mode = (LightLinkMode)mode;
}

// Renders one condition row (e.g. "Night") and writes back whatever changed.
// PbrSection/TerrainSection are the exact section strings each effect's own
// UpdateSettings() reads (see PBRShaders::UpdateSettings / TerrainShaders::
// UpdateSettings) -- TerrainSection == nullptr for Interiors, which never
// renders terrain. Returns true if anything was written, so the panel can
// call LoadSettings() once for the whole frame instead of once per row.
static bool RenderLightingConditionRow(const char* RowLabel, const char* PbrSection, const char* TerrainSection, LightLinkMode& Mode) {
	ImGui::PushID(RowLabel);
	ImGui::TextUnformatted(RowLabel);
	ImGui::Separator();
	RenderLightLinkModeSelector(Mode, TerrainSection != nullptr);

	float pbr[3] = {
		TheSettingManager->GetSettingF(PbrSection, "LightingScale"),
		TheSettingManager->GetSettingF(PbrSection, "AmbientScale"),
		TheSettingManager->GetSettingF(PbrSection, "SkylightingScale"),
	};
	float terrain[3] = {};
	if (TerrainSection) {
		terrain[0] = TheSettingManager->GetSettingF(TerrainSection, "LightingScale");
		terrain[1] = TheSettingManager->GetSettingF(TerrainSection, "AmbientScale");
		terrain[2] = TheSettingManager->GetSettingF(TerrainSection, "SkylightingScale");
	}

	static const char* kLabels[6] = { "PBR Light", "PBR Ambient", "PBR Sky", "Terrain Light", "Terrain Ambient", "Terrain Sky" };
	float* values[6] = { &pbr[0], &pbr[1], &pbr[2], &terrain[0], &terrain[1], &terrain[2] };
	int total = TerrainSection ? 6 : 3;

	bool changed = RenderLightingCells(Mode, kLabels, values, 3, total);
	if (changed) {
		TheSettingManager->SetSettingF(PbrSection, "LightingScale", pbr[0]);
		TheSettingManager->SetSettingF(PbrSection, "AmbientScale", pbr[1]);
		TheSettingManager->SetSettingF(PbrSection, "SkylightingScale", pbr[2]);
		if (TerrainSection) {
			TheSettingManager->SetSettingF(TerrainSection, "LightingScale", terrain[0]);
			TheSettingManager->SetSettingF(TerrainSection, "AmbientScale", terrain[1]);
			TheSettingManager->SetSettingF(TerrainSection, "SkylightingScale", terrain[2]);
		}
	}
	ImGui::PopID();
	return changed;
}

static void RenderLightingPanel() {
	if (!s_lightingPanelOpen) return;

	ImGui::SetNextWindowSize(ImVec2(440.0f, 560.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(620.0f, 200.0f),  ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("NVR Lighting", &s_lightingPanelOpen)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Intensity-only view over the same PBR/Terrain settings the "
		"main menu already edits, grouped by condition, with an optional "
		"proportional link per group.");

	bool anyChanged = false;
	if (ImGui::CollapsingHeader("Day", ImGuiTreeNodeFlags_DefaultOpen))
		anyChanged |= RenderLightingConditionRow("Day", "Shaders.PBR.Main", "Shaders.Terrain.Main", s_lightLinkDay);
	if (ImGui::CollapsingHeader("Night", ImGuiTreeNodeFlags_DefaultOpen))
		anyChanged |= RenderLightingConditionRow("Night", "Shaders.PBR.Night", "Shaders.Terrain.Night", s_lightLinkNight);
	if (ImGui::CollapsingHeader("Rain", ImGuiTreeNodeFlags_DefaultOpen)) {
		anyChanged |= RenderLightingConditionRow("Rain", "Shaders.PBR.Rain", "Shaders.Terrain.Rain", s_lightLinkRain);
		anyChanged |= RenderLightingConditionRow("Night + Rain", "Shaders.PBR.NightRain", "Shaders.Terrain.NightRain", s_lightLinkNightRain);
	}
	if (ImGui::CollapsingHeader("Interiors", ImGuiTreeNodeFlags_DefaultOpen))
		anyChanged |= RenderLightingConditionRow("Interiors", "Shaders.PBR.Interiors", nullptr, s_lightLinkInteriors);

	if (anyChanged) TheSettingManager->LoadSettings();

	ImGui::End();
}

static void DevPanelCleanup() {
	if (s_devMenusHidden) {
		RunConsoleCommand("tm");
		s_devMenusHidden = false;
	}
	if (s_devFreecamOn) {
		RunConsoleCommand("tfc");
		s_devFreecamOn = false;
	}
}

static void RenderDevPanel() {
	if (!s_devOpen) return;

	TimeGlobals* tg = TimeGlobals::Get();

	// Snapshot TimeScale once per NVR session (survives panel open/close)
	if (s_savedTimeScale < 0.0f && tg && tg->TimeScale)
		s_savedTimeScale = tg->TimeScale->data;

	ImGui::SetNextWindowSize(ImVec2(420.0f, 460.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(100.0f, 200.0f),  ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("NVR Dev Tools", &s_devOpen)) {
		ImGui::End();
		if (!s_devOpen) DevPanelCleanup();
		return;
	}
	if (!s_devOpen) { ImGui::End(); DevPanelCleanup(); return; }

	ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.1f, 1.0f),
		"WARNING: For testing saves only.");
	ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.1f, 1.0f),
		"TimeScale and freecam changes can break active scripts and quests.");
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Time & World", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (!tg || !tg->GameHour || !tg->TimeScale) {
			ImGui::TextDisabled("Time globals unavailable.");
		} else {
			// Time of Day
			float hour = fmodf(tg->GameHour->data, 24.0f);
			if (ImGui::SliderFloat("Time of Day", &hour, 0.0f, 23.99f, "%.2f h"))
				tg->GameHour->data = hour;

			// TimeScale
			float ts = tg->TimeScale->data;
			if (ImGui::SliderFloat("TimeScale", &ts, 1.0f, 200.0f, "%.1f"))
				tg->TimeScale->data = ts;

			ImGui::Spacing();

			// Restore buttons
			bool atSession = (s_savedTimeScale >= 0.0f && fabsf(tg->TimeScale->data - s_savedTimeScale) < 0.05f);
			if (atSession) ImGui::BeginDisabled();
			char sessionLbl[64];
			snprintf(sessionLbl, sizeof(sessionLbl), "Restore: %.1f###RestoreSession",
				s_savedTimeScale >= 0.0f ? s_savedTimeScale : 30.0f);
			if (ImGui::Button(sessionLbl) && s_savedTimeScale >= 0.0f)
				tg->TimeScale->data = s_savedTimeScale;
			if (atSession) ImGui::EndDisabled();

			ImGui::SameLine();

			bool atVanilla = fabsf(tg->TimeScale->data - 30.0f) < 0.05f;
			if (atVanilla) ImGui::BeginDisabled();
			if (ImGui::Button("Vanilla (30)"))
				tg->TimeScale->data = 30.0f;
			if (atVanilla) ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Game Speed (sgtm) — reads raw game address, writes via console command
			{
				float sgtm = *(float*)0x11AC3A0;
				float sgtmEdit = sgtm;
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::SliderFloat("Game Speed", &sgtmEdit, 0.05f, 4.0f, "%.3fx")) {
					char cmd[64];
					snprintf(cmd, sizeof(cmd), "sgtm %.4f", sgtmEdit);
					RunConsoleCommand(cmd);
				}
				ImGui::SameLine();
				bool atNormal = fabsf(sgtm - 1.0f) < 0.005f;
				if (atNormal) ImGui::BeginDisabled();
				if (ImGui::SmallButton("Reset (1x)"))
					RunConsoleCommand("sgtm 1.0");
				if (atNormal) ImGui::EndDisabled();
			}

			ImGui::Spacing();

			// FreeCam + Timestop via tfc console command
			if (ImGui::Checkbox("FreeCam + Timestop  (tfc 1)", &s_devFreecamOn))
				RunConsoleCommand(s_devFreecamOn ? "tfc 1" : "tfc");
		}
	}

	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Display")) {
		if (ImGui::Checkbox("Hide Game HUD  (tm)", &s_devMenusHidden))
			RunConsoleCommand("tm");

		ImGui::Spacing();

		if (ImGui::Button("Hide UI for Screenshot")) {
			s_screenshotMode = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(Press NVR key to restore)");
	}

	ImGui::Spacing();

	// ---- Session 7: Cell coc picker ---------------------------------------
	// docs § "Scope" -- explicitly out of scope for the preset manager
	// itself (assignment is always done by physically standing in a
	// location), but a genuinely useful utility on its own merits, so it
	// lives here in Dev Tools instead, independent of and unrelated to
	// presets. cow was dropped -- nobody used it, and exterior worldspaces
	// are reachable by walking anyway.
	if (ImGui::CollapsingHeader("Location (coc)")) {
		static char s_devLocationBuf[64] = "";
		static ImGuiTextFilter s_cellFilter;

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputText("Interior cell EditorID", s_devLocationBuf, sizeof(s_devLocationBuf));
		ImGui::TextDisabled("(type freely, or pick from a list below)");

		ImGui::Spacing();

		// Keyword-tagged cells plus whatever's actually been visited this
		// session (see s_visitedInteriorCells's comment for why this, not a
		// complete engine-level list).
		ImGui::TextUnformatted("Interior cells (keyword-tagged + visited this session)");
		s_cellFilter.Draw("##cellfilter", 200.0f);
		ImGui::BeginChild("##celllist", ImVec2(0.0f, 90.0f), true);
		for (const auto& name : s_visitedInteriorCells) {
			if (!s_cellFilter.PassFilter(name.c_str())) continue;
			char label[96];
			snprintf(label, sizeof(label), "[Visited] %s", name.c_str());
			if (ImGui::Selectable(label))
				strncpy_s(s_devLocationBuf, name.c_str(), _TRUNCATE);
		}
		for (const auto& name : PresetManager::GetKnownKeywordCells()) {
			if (!s_cellFilter.PassFilter(name.c_str())) continue;
			char label[96];
			snprintf(label, sizeof(label), "[Keyword] %s", name.c_str());
			if (ImGui::Selectable(label))
				strncpy_s(s_devLocationBuf, name.c_str(), _TRUNCATE);
		}
		ImGui::EndChild();

		ImGui::Spacing();

		bool canGo = s_devLocationBuf[0] != '\0';
		if (!canGo) ImGui::BeginDisabled();

		if (ImGui::Button("COC")) {
			char cmd[96];
			snprintf(cmd, sizeof(cmd), "coc %s", s_devLocationBuf);
			// coc triggers a multi-frame loading screen that needs to Present()
			// new frames -- calling it synchronously here would deadlock, since
			// this button handler runs inside RenderInterfaceHook, itself part
			// of the game's own D3D9 render call chain for the CURRENT frame
			// (confirmed root cause of "CoC freezes the game completely").
			// Deferred via a posted window message instead, so it actually
			// runs once we're back out on the next message-pump cycle.
			RunConsoleCommandDeferred(cmd);
		}

		if (!canGo) ImGui::EndDisabled();
	}

	ImGui::Spacing();

	// ---- Session 7: Refresh All Presets ----------------------------------
	// docs § "Refresh all presets" -- global, infrequent, not tied to the
	// player's current location, so it lives here rather than in the
	// per-location NVR Preset Manager panel. Shares the same confirm-popup
	// machinery as the Save buttons there (RequestPresetAction works from
	// any window -- see RenderPresetConfirmPopup's own comment for why).
	if (ImGui::CollapsingHeader("Presets")) {
		if (ImGui::Button("Refresh All Presets")) {
			UInt32 fileCount = (UInt32)PresetManager::ListAllPresets().size();
			char warning[192];
			snprintf(warning, sizeof(warning),
				"This will check %u preset file(s) and backfill any settings missing from current defaults. Continue?", fileCount);
			RequestPresetAction(PresetPendingAction::RefreshAll, "", warning);
		}
		ImGui::TextDisabled("Backfills missing settings only -- never overwrites an existing value, never prunes.");
	}

	ImGui::Spacing();

	// ---- Log (Session 3; filter added Session 7) -------------------------
	// docs/preset-manager-design.md § "Debug/authoring tooling".
	if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen)) {
		static ImGuiTextFilter s_logFilter;
		static bool s_logPresetOnly = false;

		s_logFilter.Draw("Filter", 180.0f);
		ImGui::SameLine();
		ImGui::Checkbox("Preset only", &s_logPresetOnly);

		std::deque<std::string> lines;
		Logger::GetRecentLines(lines);

		ImGui::BeginChild("##logscroll", ImVec2(0.0f, 180.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
		for (const auto& line : lines) {
			if (s_logPresetOnly && line.find("[Preset]") == std::string::npos) continue;
			if (!s_logFilter.PassFilter(line.c_str())) continue;
			ImGui::TextUnformatted(line.c_str());
		}
		// Auto-scroll to bottom, but only if the user was already at the
		// bottom -- don't yank them back down if they scrolled up to read.
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();
	}

	ImGui::End();
}

static HRESULT WINAPI HookedGetDeviceState(IDirectInputDevice8* device, DWORD cbData, LPVOID lpvData) {
	HRESULT hr = OriginalGetDeviceState(device, cbData, lpvData);
	if (SUCCEEDED(hr) && cbData == sizeof(DIMOUSESTATE2) && ImGuiManager::IsVisible() && !s_screenshotMode) {
		// Capture scroll wheel delta before zeroing — WM_MOUSEWHEEL is suppressed by DXVK.
		s_pendingWheelDelta += ((DIMOUSESTATE2*)lpvData)->lZ / (float)WHEEL_DELTA;
		memset(lpvData, 0, cbData);
	}
	return hr;
}

static void** GetMouseVTable() {
	InputControl* input = Global->GetInputControl();
	if (!input || !input->mouseInterface) return nullptr;
	return *reinterpret_cast<void***>(input->mouseInterface);
}

static void PatchMouseVTable() {
	// Patch once, ever -- not "if slot 9 isn't already my hook". The COM
	// vtable doesn't change across an overlay open or a D3D9 device
	// reset/reacquire (it belongs to the concrete class implementation, not
	// the instance), so re-patching on those events was never actually
	// necessary. Worse, it's actively unsafe with a second mod in the mix
	// (e.g. CacheUI) also hooking this same slot: if it hooks in AFTER us, it
	// correctly chains under our hook (saves our hook as its own "original").
	// Our OLD per-call guard only checked "is slot 9 currently my hook" --
	// once the other mod re-hooks over us, that check fails, so our next
	// call here (overlay open, device reset, ...) sees "not patched" and
	// re-patches: it captures the OTHER mod's hook into OriginalGetDeviceState
	// (clobbering the true original we'd already saved) and reinstalls our
	// own hook -- the same static function, so the vtable slot doesn't
	// visibly change, but our hook now calls the other mod's hook, which
	// calls what IT saved as its original (our hook, from before we
	// clobbered ourselves) -- unbounded mutual recursion, stack overflow.
	// Confirmed root cause of a crash on opening the NVR menu with CacheUI
	// installed: ntdll.dll/0xC0000005, no NVR-side crash log (a stack-
	// overflow guard-page fault blows past the normal SEH-based logger).
	// Patching exactly once means OriginalGetDeviceState, once set, always
	// points at whatever was genuinely there at that moment -- correct
	// regardless of what any other mod does to the slot afterward.
	if (OriginalGetDeviceState) return;

	void** vtable = GetMouseVTable();
	if (!vtable) return; // input not ready yet -- OriginalGetDeviceState stays null, a later call site gets a real attempt
	OriginalGetDeviceState = reinterpret_cast<GetDeviceState_t>(vtable[9]);
	DWORD old;
	VirtualProtect(&vtable[9], sizeof(void*), PAGE_READWRITE, &old);
	vtable[9] = reinterpret_cast<void*>(HookedGetDeviceState);
	VirtualProtect(&vtable[9], sizeof(void*), old, &old);
}

// ---- xNVSE input block (mouse buttons / wheel via DIHookControl) -------------

static void BlockGameInput(bool block) {
	if (!g_DIHookCtrl) return;
	// Exclude modifier keys (Shift, Ctrl, Alt) from blocking.  xNVSE tracks
	// tap/hold state continuously; a disable→re-enable cycle breaks the state
	// machine for mods that bind actions to these keys (e.g. sprint on Shift).
	// Not blocking them is safe — the overlay doesn't need to suppress sprinting.
	static const UInt32 kModifiers[] = {
		0x2A, 0x36,        // DIK_LSHIFT, DIK_RSHIFT
		0x1D, 0x9D,        // DIK_LCONTROL, DIK_RCONTROL
		0x38, 0xB8,        // DIK_LMENU, DIK_RMENU
	};
	for (UInt32 code = 0; code < kMaxMacros; code++) {
		bool isMod = false;
		for (UInt32 m : kModifiers) if (code == m) { isMod = true; break; }
		if (!isMod)
			g_DIHookCtrl->SetKeyDisableState(code, block, DIHookControl::kDisable_User);
	}
}

// ---- Visibility --------------------------------------------------------------

static void SetOverlayVisible(bool visible) {
	if (ImGuiManager::IsVisible() == visible) return;
	ImGuiManager::SetVisible(visible);
	if (visible) {
		s_snapshot.clear();
		// Snapshot current held keys so they don't register as new presses
		for (int vk = 0; vk < 256; vk++)
			s_prevKeyState[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0;
		PatchMouseVTable();
		ClipCursor(nullptr);
		BlockGameInput(true);
		ImGui::GetIO().MouseDrawCursor = true;
		ImGui::GetIO().ClearInputKeys();
		// If the cursor is outside the client rect (e.g. left on another monitor
		// before alt-tabbing back), warp it to the window center so UpdateMouseData
		// gets a valid position from frame one instead of injecting stale off-screen
		// coords every frame and leaving the ImGui cursor permanently stuck.
		{
			HWND  hwnd = TheRenderManager->m_kWndFocus;
			RECT  cr;
			POINT pt;
			if (::GetCursorPos(&pt) && ::GetClientRect(hwnd, &cr)) {
				POINT ptClient = pt;
				::ScreenToClient(hwnd, &ptClient);
				if (ptClient.x < cr.left || ptClient.x >= cr.right ||
				    ptClient.y < cr.top  || ptClient.y >= cr.bottom) {
					POINT center = { (cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2 };
					::ClientToScreen(hwnd, &center);
					::SetCursorPos(center.x, center.y);
				}
			}
		}
	} else {
		s_screenshotMode = false;
		CfabDeactivateIfActive();
		DevPanelCleanup();
		BlockGameInput(false);
		ImGui::GetIO().MouseDrawCursor = false;
	}
}

// Restore all snapshotted values and sync shader states.
static void RevertToSnapshot() {
	LUTEffect* lut = TheShaderManager->Effects.LUT;
	for (auto& [section, keys] : s_snapshot) {
		for (auto& [key, value] : keys) {
			// LUT filenames need the texture/index reloaded at runtime, not just
			// the TOML value rewritten -- LUTEffect::UpdateSettings() never
			// re-reads DayLUT/NightLUT/InteriorLUT, so a plain SetSettingS would
			// revert the config but leave the picker/texture showing the stale pick.
			if (lut && section == "Shaders.LUT.Main" &&
				(key == "DayLUT" || key == "NightLUT" || key == "InteriorLUT")) {
				int slot = (key == "DayLUT") ? 0 : (key == "NightLUT") ? 1 : 2;
				lut->LoadLUT(slot, value.c_str());
				continue;
			}
			TheSettingManager->SetSettingS(const_cast<char*>(section.c_str()),
			                               const_cast<char*>(key.c_str()),
			                               const_cast<char*>(value.c_str()));
		}
	}
	TheSettingManager->LoadSettings();
	// Sync shader enabled flags (same as RevertSettings does)
	StringList shaders;
	TheSettingManager->FillMenuSections(&shaders, "Shaders");
	for (const auto& name : shaders) {
		bool want = TheSettingManager->GetMenuShaderEnabled(name.c_str());
		EffectRecord* effect = TheShaderManager->GetEffectByName(name.c_str());
		if (effect) { effect->Enabled = want; continue; }
		ShaderCollection* shader = TheShaderManager->GetShaderCollectionByName(name.c_str());
		if (shader) shader->Enabled = want;
	}
	s_colorStatesNeedReset = true;
	SelectedSection.clear();
}

// ---- WndProc -----------------------------------------------------------------

LRESULT CALLBACK ImGuiManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// Deferred console command (see RunConsoleCommandDeferred) -- processed
	// here because message-pump dispatch happens outside any render call,
	// unlike the button handler that posted this.
	if (msg == GetDeferredConsoleCommandMessage()) {
		char* cmd = (char*)lParam;
		if (cmd) {
			RunConsoleCommand(cmd);
			free(cmd);
		}
		return 0;
	}

	if (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)
		SetOverlayVisible(false);

	// Eat WM_SYSKEYDOWN for Alt so Windows never activates the system menu.
	if (Visible && msg == WM_SYSKEYDOWN && wParam == VK_MENU)
		return TRUE;

	if (Visible && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return TRUE;
	// Eat scroll messages so the game's WndProc doesn't interfere with ImGui scroll.
	if (Visible && (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL))
		return 0;
	return CallWindowProc(OriginalWndProc, hwnd, msg, wParam, lParam);
}

// ---- Init / shutdown ---------------------------------------------------------

void ImGuiManager::Initialize() {
	if (Initialized) return;

	IDirect3DDevice9* device = TheRenderManager->device;
	HWND hwnd = TheRenderManager->m_kWndFocus;
	if (!device || !hwnd) { Logger::Log("ImGuiManager: deferring init"); return; }

	GameWindow = hwnd;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Derive INI path from DLL location: e.g. NewVegasReloaded.imgui.ini
	// Static so the pointer remains valid for the lifetime of the ImGui context.
	static char s_iniPath[MAX_PATH] = {};
	if (s_iniPath[0] == '\0') {
		HMODULE hMod = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&ImGuiManager::Initialize, &hMod);
		GetModuleFileNameA(hMod, s_iniPath, MAX_PATH);
		char* ext = strrchr(s_iniPath, '.');
		if (ext) strcpy(ext, ".imgui.ini");
	}
	io.IniFilename = s_iniPath;

	// Restore font scale from TextSize setting.
	// Legacy pixel sizes (old menu system) are typically 8-24; treat anything
	// below 50 as stale and default to 100 (1.0x).
	if (TheSettingManager) {
		int stored = (int)TheSettingManager->SettingsMain.Menu.TextSize;
		io.FontGlobalScale = (stored >= 50) ? (stored / 100.0f) : 1.0f;
	}

	// Comfortable Dark Cyan style by SouthCraftX from ImThemes
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.Alpha = 1.0f;
		style.DisabledAlpha = 1.0f;
		style.WindowPadding = ImVec2(20.0f, 20.0f);
		style.WindowRounding = 11.5f;
		style.WindowBorderSize = 0.0f;
		style.WindowMinSize = ImVec2(20.0f, 20.0f);
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.ChildRounding = 20.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupRounding = 17.4f;
		style.PopupBorderSize = 1.0f;
		style.FramePadding = ImVec2(20.0f, 3.4f);
		style.FrameRounding = 11.9f;
		style.FrameBorderSize = 0.0f;
		style.ItemSpacing = ImVec2(8.9f, 13.4f);
		style.ItemInnerSpacing = ImVec2(7.1f, 1.8f);
		style.CellPadding = ImVec2(12.1f, 9.2f);
		style.IndentSpacing = 0.0f;
		style.ColumnsMinSpacing = 8.7f;
		style.ScrollbarSize = 11.6f;
		style.ScrollbarRounding = 15.9f;
		style.GrabMinSize = 3.7f;
		style.GrabRounding = 20.0f;
		style.TabRounding = 9.8f;
		style.TabBorderSize = 0.0f;
		style.TabCloseButtonMinWidthUnselected = 0.0f;
		style.ColorButtonPosition = ImGuiDir_Right;
		style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
		style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

		style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.27450982f, 0.31764707f, 0.4509804f, 1.0f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09411765f, 0.101960786f, 0.11764706f, 1.0f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.11372549f, 0.1254902f, 0.15294118f, 1.0f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.09803922f, 0.105882354f, 0.12156863f, 1.0f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.03137255f, 0.9490196f, 0.84313726f, 1.0f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.03137255f, 0.9490196f, 0.84313726f, 1.0f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.6f, 0.9647059f, 0.03137255f, 1.0f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18039216f, 0.1882353f, 0.19607843f, 1.0f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.15294118f, 0.15294118f, 0.15294118f, 1.0f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.14117648f, 0.16470589f, 0.20784314f, 1.0f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.105882354f, 0.105882354f, 0.105882354f, 1.0f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.12941177f, 0.14901961f, 0.19215687f, 1.0f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.14509805f, 0.14509805f, 0.14509805f, 1.0f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.03137255f, 0.9490196f, 0.84313726f, 1.0f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		style.Colors[ImGuiCol_Tab] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_TabHovered] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
		style.Colors[ImGuiCol_TabActive] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
		style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
		style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1254902f, 0.27450982f, 0.57254905f, 1.0f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.52156866f, 0.6f, 0.7019608f, 1.0f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.039215688f, 0.98039216f, 0.98039216f, 1.0f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.03137255f, 0.9490196f, 0.84313726f, 1.0f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
		style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
		style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
		style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
		style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
		style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.09803922f, 0.105882354f, 0.12156863f, 1.0f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.9372549f, 0.9372549f, 0.9372549f, 1.0f);
		style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.49803922f, 0.5137255f, 1.0f, 1.0f);
		style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26666668f, 0.2901961f, 1.0f, 1.0f);
		style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.49803922f, 0.5137255f, 1.0f, 1.0f);
		style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.19607843f, 0.1764706f, 0.54509807f, 0.5019608f);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.19607843f, 0.1764706f, 0.54509807f, 0.5019608f);
	}

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX9_Init(device);

	OriginalWndProc = (WNDPROC)SetWindowLong(hwnd, GWL_WNDPROC, (LONG)(LONG_PTR)WndProc);
	PatchMouseVTable();

	Initialized = true;
	Logger::Log("ImGuiManager: Initialized");
}

void ImGuiManager::Shutdown() {
	if (!Initialized) return;
	SetOverlayVisible(false);
	SetWindowLong(GameWindow, GWL_WNDPROC, (LONG)(LONG_PTR)OriginalWndProc);
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	Initialized = false;
	Logger::Log("ImGuiManager: Shutdown");
}

static ImGuiKey VkToImGuiKey(USHORT vk) {
	switch (vk) {
	case VK_TAB:      return ImGuiKey_Tab;
	case VK_LEFT:     return ImGuiKey_LeftArrow;
	case VK_RIGHT:    return ImGuiKey_RightArrow;
	case VK_UP:       return ImGuiKey_UpArrow;
	case VK_DOWN:     return ImGuiKey_DownArrow;
	case VK_PRIOR:    return ImGuiKey_PageUp;
	case VK_NEXT:     return ImGuiKey_PageDown;
	case VK_HOME:     return ImGuiKey_Home;
	case VK_END:      return ImGuiKey_End;
	case VK_INSERT:   return ImGuiKey_Insert;
	case VK_DELETE:   return ImGuiKey_Delete;
	case VK_BACK:     return ImGuiKey_Backspace;
	case VK_RETURN:   return ImGuiKey_Enter;
	case VK_ESCAPE:   return ImGuiKey_Escape;
	case VK_LSHIFT:   return ImGuiKey_LeftShift;
	case VK_RSHIFT:   return ImGuiKey_RightShift;
	case VK_LCONTROL: return ImGuiKey_LeftCtrl;
	case VK_RCONTROL: return ImGuiKey_RightCtrl;
	case VK_LMENU:    return ImGuiKey_LeftAlt;
	case VK_RMENU:    return ImGuiKey_RightAlt;
	case VK_SPACE:    return ImGuiKey_Space;
	case VK_ADD:      return ImGuiKey_KeypadAdd;
	case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
	case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
	case VK_DIVIDE:   return ImGuiKey_KeypadDivide;
	case VK_NUMPAD0:  return ImGuiKey_Keypad0;
	case VK_NUMPAD1:  return ImGuiKey_Keypad1;
	case VK_NUMPAD2:  return ImGuiKey_Keypad2;
	case VK_NUMPAD3:  return ImGuiKey_Keypad3;
	case VK_NUMPAD4:  return ImGuiKey_Keypad4;
	case VK_NUMPAD5:  return ImGuiKey_Keypad5;
	case VK_NUMPAD6:  return ImGuiKey_Keypad6;
	case VK_NUMPAD7:  return ImGuiKey_Keypad7;
	case VK_NUMPAD8:  return ImGuiKey_Keypad8;
	case VK_NUMPAD9:  return ImGuiKey_Keypad9;
	case 'A': return ImGuiKey_A; case 'B': return ImGuiKey_B;
	case 'C': return ImGuiKey_C; case 'D': return ImGuiKey_D;
	case 'E': return ImGuiKey_E; case 'F': return ImGuiKey_F;
	case 'G': return ImGuiKey_G; case 'H': return ImGuiKey_H;
	case 'I': return ImGuiKey_I; case 'J': return ImGuiKey_J;
	case 'K': return ImGuiKey_K; case 'L': return ImGuiKey_L;
	case 'M': return ImGuiKey_M; case 'N': return ImGuiKey_N;
	case 'O': return ImGuiKey_O; case 'P': return ImGuiKey_P;
	case 'Q': return ImGuiKey_Q; case 'R': return ImGuiKey_R;
	case 'S': return ImGuiKey_S; case 'T': return ImGuiKey_T;
	case 'U': return ImGuiKey_U; case 'V': return ImGuiKey_V;
	case 'W': return ImGuiKey_W; case 'X': return ImGuiKey_X;
	case 'Y': return ImGuiKey_Y; case 'Z': return ImGuiKey_Z;
	case '0': return ImGuiKey_0; case '1': return ImGuiKey_1;
	case '2': return ImGuiKey_2; case '3': return ImGuiKey_3;
	case '4': return ImGuiKey_4; case '5': return ImGuiKey_5;
	case '6': return ImGuiKey_6; case '7': return ImGuiKey_7;
	case '8': return ImGuiKey_8; case '9': return ImGuiKey_9;
	default:  return ImGuiKey_None;
	}
}

// Hardcoded DIK->VK for extended keys where MapVirtualKey returns wrong/zero.
static int DikToVk(BYTE dik) {
	switch (dik) {
	case 0x9C: return VK_RETURN;   // DIK_NUMPADENTER
	case 0x9D: return VK_RCONTROL; // DIK_RCONTROL
	case 0xB5: return VK_DIVIDE;   // DIK_NUMPADSLASH
	case 0xB8: return VK_RMENU;    // DIK_RALT
	case 0xC5: return VK_PAUSE;    // DIK_PAUSE
	case 0xC7: return VK_HOME;     // DIK_HOME
	case 0xC8: return VK_UP;       // DIK_UP
	case 0xC9: return VK_PRIOR;    // DIK_PGUP
	case 0xCB: return VK_LEFT;     // DIK_LEFT
	case 0xCD: return VK_RIGHT;    // DIK_RIGHT
	case 0xCF: return VK_END;      // DIK_END
	case 0xD0: return VK_DOWN;     // DIK_DOWN
	case 0xD1: return VK_NEXT;     // DIK_PGDN
	case 0xD2: return VK_INSERT;   // DIK_INSERT
	case 0xD3: return VK_DELETE;   // DIK_DELETE
	default:   return (int)MapVirtualKey(dik, MAPVK_VSC_TO_VK_EX);
	}
}

// ---- Frame -------------------------------------------------------------------

void ImGuiManager::NewFrame() {
	if (!Initialized) { Initialize(); if (!Initialized) return; }

	HRESULT coop = TheRenderManager->device->TestCooperativeLevel();
	if (coop == D3DERR_DEVICELOST) {
		if (!DeviceLost) { ImGui_ImplDX9_InvalidateDeviceObjects(); DeviceLost = true; }
		return;
	}
	if (DeviceLost && coop == D3DERR_DEVICENOTRESET) {
		ImGui_ImplDX9_CreateDeviceObjects();
		DeviceLost = false;
		PatchMouseVTable();
	}

	// Close if a game menu is active (inventory, pip-boy, etc.)
	if (Visible && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_None))
		SetOverlayVisible(false);

	if (TheSettingManager) {
		BYTE dik = (BYTE)TheSettingManager->SettingsMain.Menu.KeyEnable;
		int  vk  = DikToVk(dik);
		if (vk) {
			if (s_masterMod < 0)
				s_masterMod = (int)TheSettingManager->SettingsMain.Menu.MasterSwitchModifier;

			bool keyDown = (GetAsyncKeyState(vk) & 0x8000) != 0;

			// Compute whether the FX modifier is currently held.
			bool modHeld = false;
			if      (s_masterMod == 1) modHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
			else if (s_masterMod == 2) modHeld = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
			else if (s_masterMod == 3) modHeld = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;

			// Open/close overlay — bare key only, never when FX modifier is held.
			// While in screenshot mode the key exits screenshot mode instead of closing.
			{
				static bool prev = false;
				if (keyDown && !prev && !modHeld) {
					if (s_screenshotMode)
						s_screenshotMode = false;
					else
						SetOverlayVisible(!Visible);
				}
				prev = keyDown;
			}

			// RenderEffects master switch — modifier + key only.
			if (s_masterMod > 0) {
				static bool prevMaster = false;
				bool masterDown = modHeld && keyDown;
				if (masterDown && !prevMaster) {
					bool cur = TheSettingManager->SettingsMain.Main.RenderEffects;
					TheSettingManager->SetSetting("Main.Main.Misc", "RenderEffects", !cur);
					TheSettingManager->LoadSettings();
				}
				prevMaster = masterDown;
			}
		}
	}

	// Screenshot mode — unblock/reblock game input and cursor on flag transition.
	if (Visible) {
		static bool prevSS = false;
		if (s_screenshotMode != prevSS) {
			if (s_screenshotMode) {
				BlockGameInput(false);
				ImGui::GetIO().MouseDrawCursor = false;
			} else {
				BlockGameInput(true);
				ImGui::GetIO().MouseDrawCursor = true;
			}
			prevSS = s_screenshotMode;
		}
	}

	// Shader toggle hotkey: Insert key when a shader panel is open.
	if (Visible && !SelectedSection.empty() && SelectedSection.find("Shaders.") == 0) {
		static bool prevInsert = false;
		bool curInsert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
		if (curInsert && !prevInsert) {
			size_t dot1 = SelectedSection.find('.');
			size_t dot2 = SelectedSection.find('.', dot1 + 1);
			if (dot1 != std::string::npos && dot2 != std::string::npos) {
				std::string shaderName = SelectedSection.substr(dot1 + 1, dot2 - dot1 - 1);
				TheShaderManager->SwitchShaderStatus(shaderName.c_str());
				TheSettingManager->LoadSettings();
			}
		}
		prevInsert = curInsert;
	}

	// Poll keyboard via GetAsyncKeyState — same thread as all other input injection.
	// WM_INPUT is unreliable under DXVK; GetAsyncKeyState works from any thread.
	if (Visible) {
		ImGuiIO& io = ImGui::GetIO();
		for (int vk = 1; vk < 256; vk++) {
			bool cur = (GetAsyncKeyState(vk) & 0x8000) != 0;
			bool was = (s_prevKeyState[vk] & 0x80) != 0;
			if (cur != was) {
				ImGuiKey imKey = VkToImGuiKey((USHORT)vk);
				if (imKey != ImGuiKey_None)
					io.AddKeyEvent(imKey, cur);
				if (cur) {
					WCHAR buf[4] = {};
					BYTE ks[256] = {};
					ks[VK_SHIFT]   = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) ? 0x80 : 0;
					ks[VK_CONTROL] = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 0x80 : 0;
					ks[VK_MENU]    = (GetAsyncKeyState(VK_MENU)    & 0x8000) ? 0x80 : 0;
					ks[VK_CAPITAL] = (GetKeyState(VK_CAPITAL) & 1) ? 0x01 : 0;
					ks[vk]         = 0x80;
					int n = ToUnicode(vk, MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, buf, 4, 0);
					for (int i = 0; i < n; i++)
						io.AddInputCharacterUTF16((ImWchar16)buf[i]);
				}
				s_prevKeyState[vk] = cur ? 0x80 : 0;
			}
		}
	}

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();

	// Override ImGui_ImplWin32_UpdateMouseData's position — it suppresses the
	// GetCursorPos fallback when MouseTrackedArea==1 (set by WM_MOUSEMOVE, which
	// fires on click-to-refocus but not again due to RIDEV_NOLEGACY).  Injecting
	// last wins in the event queue, restoring correct cursor position.
	if (Visible) {
		HWND  hwnd = TheRenderManager->m_kWndFocus;
		POINT pt;
		RECT  cr;
		if (::GetCursorPos(&pt) && ::GetClientRect(hwnd, &cr)) {
			POINT ptClient = pt;
			::ScreenToClient(hwnd, &ptClient);
			if (ptClient.x >= cr.left && ptClient.x < cr.right &&
			    ptClient.y >= cr.top  && ptClient.y < cr.bottom)
				ImGui::GetIO().AddMousePosEvent((float)ptClient.x, (float)ptClient.y);
		}
	}

	ImGui::NewFrame();

	if (Visible) {
		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
		if (s_pendingWheelDelta != 0.0f) {
			io.AddMouseWheelEvent(0.0f, s_pendingWheelDelta);
			s_pendingWheelDelta = 0.0f;
		}

		// Detect +/- key edges for hover-to-increment — covers numpad and main row.
		static bool prevPlus = false, prevMinus = false;
		bool curPlus  = (GetAsyncKeyState(VK_ADD)      & 0x8000) != 0
		             || (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
		bool curMinus = (GetAsyncKeyState(VK_SUBTRACT)  & 0x8000) != 0
		             || (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
		s_plusPressed  = curPlus  && !prevPlus;
		s_minusPressed = curMinus && !prevMinus;
		prevPlus  = curPlus;
		prevMinus = curMinus;
	}
}

void ImGuiManager::Render() {
	if (!Initialized || DeviceLost) return;
	BuildUI();
	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

// ---- Menu UI -----------------------------------------------------------------

static bool ShouldHideSection(const std::string& name) {
	// DepthOfField is the old DoF, superseded by CinematicDOF; still driven by its TOML section.
	return name == "WeatherMode" || name == "Status" || name == "DepthOfField";
}

static bool ShouldHideKey(const char* key) {
	if (strncmp(key, "TextColor", 9) == 0 || strncmp(key, "TextShadow", 10) == 0) return true;
	// LUT filenames are rendered as cycle pickers, not raw InputText
	if (strcmp(key, "DayLUT") == 0 || strcmp(key, "NightLUT") == 0 || strcmp(key, "InteriorLUT") == 0) return true;
	return false;
}

// Settings whose read path is commented out in the C++ (dead code) but that
// still exist in the TOML for compatibility. Greyed out rather than hidden
// so the keys stay visible/editable-in-file, or removed outright, instead of
// silently doing nothing behind a live-looking widget.
static bool IsInertSetting(const char* section, const char* key) {
	if (strcmp(section, "Shaders.ShadowsExteriors.ShadowMaps") != 0) return false;
	return strcmp(key, "Mipmaps") == 0 || strcmp(key, "Anisotropy") == 0;
}

static const struct { int dik; const char* name; } kDIKTable[] = {
	{ 0x01, "Escape" },
	{ 0x02, "1" }, { 0x03, "2" }, { 0x04, "3" }, { 0x05, "4" }, { 0x06, "5" },
	{ 0x07, "6" }, { 0x08, "7" }, { 0x09, "8" }, { 0x0A, "9" }, { 0x0B, "0" },
	{ 0x0C, "Minus (-)" }, { 0x0D, "Equals (=)" }, { 0x0E, "Backspace" },
	{ 0x0F, "Tab" },
	{ 0x10, "Q" }, { 0x11, "W" }, { 0x12, "E" }, { 0x13, "R" }, { 0x14, "T" },
	{ 0x15, "Y" }, { 0x16, "U" }, { 0x17, "I" }, { 0x18, "O" }, { 0x19, "P" },
	{ 0x1A, "[ Left Bracket" }, { 0x1B, "] Right Bracket" }, { 0x1C, "Enter" },
	{ 0x1D, "Left Ctrl" },
	{ 0x1E, "A" }, { 0x1F, "S" }, { 0x20, "D" }, { 0x21, "F" }, { 0x22, "G" },
	{ 0x23, "H" }, { 0x24, "J" }, { 0x25, "K" }, { 0x26, "L" },
	{ 0x27, "Semicolon (;)" }, { 0x28, "Apostrophe (')" }, { 0x29, "Grave (`)" },
	{ 0x2A, "Left Shift" }, { 0x2B, "Backslash (\\)" },
	{ 0x2C, "Z" }, { 0x2D, "X" }, { 0x2E, "C" }, { 0x2F, "V" },
	{ 0x30, "B" }, { 0x31, "N" }, { 0x32, "M" },
	{ 0x33, "Comma (,)" }, { 0x34, "Period (.)" }, { 0x35, "Slash (/)" },
	{ 0x36, "Right Shift" }, { 0x37, "Numpad *" }, { 0x38, "Left Alt" },
	{ 0x39, "Space" }, { 0x3A, "Caps Lock" },
	{ 0x3B, "F1" }, { 0x3C, "F2" }, { 0x3D, "F3" }, { 0x3E, "F4" },
	{ 0x3F, "F5" }, { 0x40, "F6" }, { 0x41, "F7" }, { 0x42, "F8" },
	{ 0x43, "F9" }, { 0x44, "F10" },
	{ 0x45, "Num Lock" }, { 0x46, "Scroll Lock" },
	{ 0x47, "Numpad 7" }, { 0x48, "Numpad 8" }, { 0x49, "Numpad 9" },
	{ 0x4A, "Numpad -" },
	{ 0x4B, "Numpad 4" }, { 0x4C, "Numpad 5" }, { 0x4D, "Numpad 6" },
	{ 0x4E, "Numpad +" },
	{ 0x4F, "Numpad 1" }, { 0x50, "Numpad 2" }, { 0x51, "Numpad 3" },
	{ 0x52, "Numpad 0" }, { 0x53, "Numpad ." },
	{ 0x57, "F11" }, { 0x58, "F12" },
	{ 0x9C, "Numpad Enter" }, { 0x9D, "Right Ctrl" },
	{ 0xB5, "Numpad /" }, { 0xB7, "Print Screen" }, { 0xB8, "Right Alt" },
	{ 0xC5, "Pause" }, { 0xC7, "Home" },
	{ 0xC8, "Up Arrow" }, { 0xC9, "Page Up" }, { 0xCB, "Left Arrow" },
	{ 0xCD, "Right Arrow" }, { 0xCF, "End" },
	{ 0xD0, "Down Arrow" }, { 0xD1, "Page Down" },
	{ 0xD2, "Insert" }, { 0xD3, "Delete" },
};

static const char* DikToName(BYTE dik) {
	for (auto& e : kDIKTable)
		if (e.dik == (int)dik) return e.name;
	return "Unknown";
}

static void RenderDIKPopup() {
	if (!ImGui::BeginPopup("DIKReference")) return;
	ImGui::Text("DirectInput Scancodes");
	ImGui::Separator();
	if (ImGui::BeginTable("diktbl", 2,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
		ImVec2(220.0f, 320.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Code");
		ImGui::TableSetupColumn("Key");
		ImGui::TableHeadersRow();
		for (auto& e : kDIKTable) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("0x%02X (%d)", e.dik, e.dik);
			ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(e.name);
		}
		ImGui::EndTable();
	}
	ImGui::EndPopup();
}

static void RenderColorTriple(
	SettingManager::Configuration::ConfigNode& nodeR,
	SettingManager::Configuration::ConfigNode& nodeG,
	SettingManager::Configuration::ConfigNode& nodeB,
	const std::string& prefix)
{
	// Persistent state: only initialise from node values once per section visit.
	// Re-reading each frame forces max(col)=1 every frame, pinning the picker
	// dot to the top edge of the triangle and resetting the intensity slider.
	struct ColorState { float col[3]; float scale; };
	static std::unordered_map<std::string, ColorState> s_states;

	if (s_colorStatesNeedReset) {
		s_states.clear();
		s_colorStatesNeedReset = false;
	}

	std::string stateKey = std::string(nodeR.Section) + "." + prefix;
	if (s_states.find(stateKey) == s_states.end()) {
		float rv = (float)atof(nodeR.Value);
		float gv = (float)atof(nodeG.Value);
		float bv = (float)atof(nodeB.Value);
		float sc = rv > gv ? rv : gv;
		if (bv > sc) sc = bv;
		ColorState cs;
		if (sc > 0.0f) {
			cs.scale    = sc;
			cs.col[0]   = rv / sc;
			cs.col[1]   = gv / sc;
			cs.col[2]   = bv / sc;
		} else {
			cs.scale    = 1.0f;
			cs.col[0]   = cs.col[1] = cs.col[2] = 0.0f;
		}
		s_states[stateKey] = cs;
	}
	ColorState& cs = s_states[stateKey];

	// Trim trailing underscores for display
	std::string label = prefix;
	while (!label.empty() && label.back() == '_') label.pop_back();

	// Lazy snapshot for R/G/B
	auto snapColor = [&](SettingManager::Configuration::ConfigNode& n) -> std::string& {
		auto& sec = s_snapshot[n.Section];
		auto  it  = sec.find(n.Key);
		if (it == sec.end()) it = sec.emplace(n.Key, n.Value).first;
		return it->second;
	};
	std::string& snapR = snapColor(nodeR);
	std::string& snapG = snapColor(nodeG);
	std::string& snapB = snapColor(nodeB);
	bool colorDirty = snapR != nodeR.Value || snapG != nodeG.Value || snapB != nodeB.Value;

	ImGui::PushID(prefix.c_str());

	// Extra top margin + separator so each triple reads as a distinct
	// section (e.g. DarkColor vs. LightColor in ImageAdjust), not a stray
	// label floating above the picker. Label itself is enlarged and tinted
	// with the theme's accent color so it clearly reads as a header for the
	// wheel/RGB row/Intensity slider below rather than a broken element.
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03137255f, 0.9490196f, 0.84313726f, 1.0f));
	ImGui::SetWindowFontScale(1.2f);
	ImGui::TextUnformatted(label.c_str());
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleColor();
	bool labelHovered = ImGui::IsItemHovered();
	ImGui::SameLine();
	if (!colorDirty) ImGui::BeginDisabled();
	if (ImGui::SmallButton("~")) {
		TheSettingManager->SetSettingS(nodeR.Section, nodeR.Key, const_cast<char*>(snapR.c_str()));
		TheSettingManager->SetSettingS(nodeG.Section, nodeG.Key, const_cast<char*>(snapG.c_str()));
		TheSettingManager->SetSettingS(nodeB.Section, nodeB.Key, const_cast<char*>(snapB.c_str()));
		TheSettingManager->LoadSettings();
		s_colorStatesNeedReset = true;
	}
	if (!colorDirty) ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to value at session start");

	bool changed = false;

	if (ImGui::ColorPicker3("##col", cs.col,
		ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview))
		changed = true;

	// R/G/B as a single horizontal row below the wheel, one field per third
	// of the available width, each labeled with a plain "R"/"G"/"B" prefix.
	{
		float fieldW = ImGui::GetContentRegionAvail().x / 3.0f;
		auto channel = [&](const char* label, const char* id, int idx) {
			float raw = cs.col[idx] * cs.scale;
			ImGui::Text("%s", label);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(fieldW);
			bool chg = ImGui::DragFloat(id, &raw, 0.001f, 0.0f, 0.0f, "%.4f");
			if (ImGui::IsItemHovered() && (s_plusPressed || s_minusPressed)) {
				raw += s_plusPressed ? s_shaderStepSize : -s_shaderStepSize;
				chg = true;
			}
			if (chg && cs.scale > 0.0f) {
				cs.col[idx] = raw / cs.scale;
				changed = true;
			}
		};
		channel("R", "##r", 0);
		ImGui::SameLine();
		channel("G", "##g", 1);
		ImGui::SameLine();
		channel("B", "##b", 2);
	}

	ImGui::Text("Intensity");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	if (ImGui::DragFloat("##intensity", &cs.scale, 0.01f, 0.0f, 0.0f, "%.4f"))
		changed = true;
	if (ImGui::IsItemHovered() && (s_plusPressed || s_minusPressed)) {
		cs.scale += s_plusPressed ? s_shaderStepSize : -s_shaderStepSize;
		changed = true;
	}

	if (changed) {
		TheSettingManager->SetSetting(nodeR.Section, nodeR.Key, cs.col[0] * cs.scale);
		TheSettingManager->SetSetting(nodeG.Section, nodeG.Key, cs.col[1] * cs.scale);
		TheSettingManager->SetSetting(nodeB.Section, nodeB.Key, cs.col[2] * cs.scale);
		TheSettingManager->LoadSettings();
	}

	if (labelHovered && !nodeR.Description.empty()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
		ImGui::TextUnformatted(nodeR.Description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	ImGui::PopID();
}

// Integer settings that are actually fixed enums render as labeled combos
// instead of a bare DragInt. Keyed by "Section.Key" (the Key alone isn't
// unique -- e.g. "Mode" means something different under Cinema, DepthOfField,
// SleepingMode, and Shadows), so adding a new enum is one line here rather
// than a new strcmp branch.
struct EnumOptions { const char* const* Names; int Count; };

static const char* kTonemappingModeNames[] = {
	"0 - None (vanilla)", "1 - VTLottes", "2 - NVRLottes",
	"3 - Reinhard", "4 - Reinhard Jodie", "5 - ACES Filmic",
	"6 - ACES Fitted", "7 - Uncharted 2", "8 - Uchimura (GT)",
	"9 - AGX", "10 - DICE",
};
// Cinema/DepthOfField Mode also accepts 3 and 4 in the C++ (Cinema.cpp /
// DepthOfField.cpp), gating on isDialog/isPersuasion -- leftover from the
// Oblivion codebase this was forked from. FNV has no Persuasion state, so
// isPersuasion is always false there, which makes the Mode==3 branch's
// condition always true: the effect is unconditionally disabled. Mode 4 is
// degenerate for the same reason (redundant with 1/2, or permanently off
// depending on effect) and isn't worth exposing, so the combo stops at the
// one genuinely useful extra value: 3, labeled "Disabled".
static const char* kCinemaDofModeNames[] = {
	"0 - Always", "1 - Not during dialog", "2 - Only during dialog", "3 - Disabled",
};
static const char* kShadowsModeNames[] = {
	"0 - VSM", "1 - EVSM2", "2 - EVSM4",
};
// SleepingMode.Mode has no TOML comment; values derived from
// src/core/Hooks/SleepingCommon.cpp's ShowSleepWaitMenuHook.
static const char* kSleepingModeNames[] = {
	"0 - Always (Wait, Sit, Sleep)", "1 - Sleeping only",
	"2 - Sitting only", "3 - Sitting & Sleeping",
};
static const char* kShadowQualityNames[] = {
	"0 - Low", "1 - Medium", "2 - High", "3 - Full", "4 - Custom",
};
static const char* kShadowFormatNames[] = {
	"0 - 16 bit", "1 - 32 bit",
};
static const char* kShadowCascadeResolutionNames[] = {
	"0 - 1024", "1 - 1536", "2 - 2048",
};
static const char* kShadowOrthoResolutionNames[] = {
	"0 - 128", "1 - 256", "2 - 512", "3 - 1024", "4 - 2048",
};
static const char* kEdgeDetectionNames[] = {
	"0 - Luma", "1 - Color", "2 - Depth", "3 - Luma + Depth",
};
static const char* kCinematicDofModeNames[] = {
	"0 - Always", "1 - Dialogue only",
};
static const char* kTAADebugNames[] = {
	"0 - Off", "1 - Motion", "2 - Reprojection error", "3 - History use", "4 - Weapon mask", "5 - Depth",
};
static const char* kCinematicDofDebugNames[] = {
	"0 - Off", "1 - Blur map (red far, blue near)", "2 - Weapon blur", "3 - Focus & autofocus taps",
};
static const char* kWeaponDofNames[] = {
	"0 - Off", "1 - Hip-fire only", "2 - Always",
};
static const char* kBokehQualityNames[] = {
	"0 - Low (48 samples)", "1 - Medium (96 samples)", "2 - High (160 samples)",
};
static const char* kBokehShapeNames[] = {
	"0 - Aperture (round/blades)", "1 - Star", "2 - Donut (mirror lens)", "3 - Heart", "4 - Cross",
};

#define ENUM_OPT(names) EnumOptions{ names, (int)(sizeof(names) / sizeof(names[0])) }

static const std::unordered_map<std::string, EnumOptions> kEnumSettings = {
	{ "Shaders.Tonemapping.Main.TonemappingMode",              ENUM_OPT(kTonemappingModeNames) },
	{ "Shaders.Tonemapping.Interiors.TonemappingMode",         ENUM_OPT(kTonemappingModeNames) },
	{ "Shaders.Cinema.Main.Mode",                              ENUM_OPT(kCinemaDofModeNames) },
	{ "Shaders.DepthOfField.FirstPersonView.Mode",             ENUM_OPT(kCinemaDofModeNames) },
	{ "Shaders.DepthOfField.ThirdPersonView.Mode",             ENUM_OPT(kCinemaDofModeNames) },
	{ "Shaders.DepthOfField.VanityView.Mode",                  ENUM_OPT(kCinemaDofModeNames) },
	{ "Main.SleepingMode.Main.Mode",                           ENUM_OPT(kSleepingModeNames) },
	{ "Shaders.ShadowsExteriors.ShadowMaps.Mode",              ENUM_OPT(kShadowsModeNames) },
	{ "Shaders.ShadowsExteriors.Main.Quality",                 ENUM_OPT(kShadowQualityNames) },
	{ "Shaders.ShadowsExteriors.ShadowMaps.Format",            ENUM_OPT(kShadowFormatNames) },
	{ "Shaders.ShadowsExteriors.ShadowMaps.CascadeResolution", ENUM_OPT(kShadowCascadeResolutionNames) },
	{ "Shaders.ShadowsExteriors.Ortho.Resolution",             ENUM_OPT(kShadowOrthoResolutionNames) },
	{ "Shaders.SMAA.Main.EdgeDetection",                       ENUM_OPT(kEdgeDetectionNames) },
	{ "Shaders.CinematicDOF.Main.BokehShape",                  ENUM_OPT(kBokehShapeNames) },
	{ "Shaders.CinematicDOF.Main.BokehQuality",                ENUM_OPT(kBokehQualityNames) },
	{ "Shaders.CinematicDOF.Main.WeaponDOF",                   ENUM_OPT(kWeaponDofNames) },
	{ "Shaders.CinematicDOF.Main.DebugView",                   ENUM_OPT(kCinematicDofDebugNames) },
	{ "Shaders.CinematicDOF.Main.Mode",                        ENUM_OPT(kCinematicDofModeNames) },
	{ "Shaders.TAA.Main.DebugView",                            ENUM_OPT(kTAADebugNames) },
};

#undef ENUM_OPT

static void RenderSetting(SettingManager::Configuration::ConfigNode& node, bool isShader = false) {
	using NodeType = SettingManager::Configuration::NodeType;

	ImGui::PushID(node.Key);

	// Lazy snapshot: record value on first encounter after overlay opens.
	auto& sectionSnap = s_snapshot[node.Section];
	auto  snapIt      = sectionSnap.find(node.Key);
	if (snapIt == sectionSnap.end())
		snapIt = sectionSnap.emplace(node.Key, node.Value).first;
	bool isDirty = snapIt->second != std::string(node.Value);

	auto RevertBtn = [&]() {
		ImGui::SameLine();
		if (!isDirty) ImGui::BeginDisabled();
		if (ImGui::SmallButton("~")) {
			TheSettingManager->SetSettingS(node.Section, node.Key,
			                               const_cast<char*>(snapIt->second.c_str()));
			TheSettingManager->LoadSettings();
		}
		if (!isDirty) ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to value at session start");
	};

	bool inert = IsInertSetting(node.Section, node.Key);
	if (inert) ImGui::BeginDisabled();

	// Hover state of the *primary* widget for each case, captured immediately
	// after that widget so RevertBtn()'s own "~" button (submitted right
	// after) doesn't steal IsItemHovered() out from under the description
	// tooltip below. Float/default capture their own local `hovered` instead
	// since they return before reaching the shared tail.
	bool mainHovered = false;

	switch (node.Type) {
	case NodeType::Boolean: {
		bool val = (strcmp(node.Value, "1") == 0 || _stricmp(node.Value, "true") == 0);
		if (ImGui::Checkbox(node.Key, &val)) {
			TheSettingManager->SetSetting(node.Section, node.Key, val);
			TheSettingManager->LoadSettings();
		}
		mainHovered = ImGui::IsItemHovered();
		if (inert && mainHovered)
			ImGui::SetTooltip("Disabled: has no effect. Mipmaps/anisotropy are commented out in "
				"ShadowsExterior.cpp (deferred shadows produce artifacted derivatives).");
		RevertBtn();
		break;
	}
	case NodeType::Float: {
		float val = (float)atof(node.Value);
		if (ImGui::DragFloat(node.Key, &val, 0.001f, 0.0f, 0.0f, "%.4f")) {
			TheSettingManager->SetSetting(node.Section, node.Key, val);
			TheSettingManager->LoadSettings();
		}
		bool hovered = ImGui::IsItemHovered();
		if (hovered && (s_plusPressed || s_minusPressed)) {
			val += s_plusPressed ? s_shaderStepSize : -s_shaderStepSize;
			TheSettingManager->SetSetting(node.Section, node.Key, val);
			TheSettingManager->LoadSettings();
		}
		if (isShader) {
			ImGui::SameLine();
			if (ImGui::SmallButton("-")) {
				val -= s_shaderStepSize;
				TheSettingManager->SetSetting(node.Section, node.Key, val);
				TheSettingManager->LoadSettings();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("+")) {
				val += s_shaderStepSize;
				TheSettingManager->SetSetting(node.Section, node.Key, val);
				TheSettingManager->LoadSettings();
			}
		}
		RevertBtn();
		if (inert) ImGui::EndDisabled();
		if (hovered && !node.Description.empty()) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
			ImGui::TextUnformatted(node.Description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::PopID();
		return;
	}
	case NodeType::Integer: {
		int val = atoi(node.Value);
		int newVal = val;
		std::string enumKey = std::string(node.Section) + "." + node.Key;
		auto enumIt = kEnumSettings.find(enumKey);
		if (enumIt != kEnumSettings.end()) {
			const char* const* names = enumIt->second.Names;
			int count = enumIt->second.Count;
			int clamped = ImClamp(val, 0, count - 1);
			if (ImGui::BeginCombo(node.Key, names[clamped])) {
				for (int i = 0; i < count; i++) {
					bool sel = (clamped == i);
					if (ImGui::Selectable(names[i], sel)) newVal = i;
					if (sel) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered() && (s_plusPressed || s_minusPressed))
				newVal = ImClamp(clamped + (s_plusPressed ? 1 : -1), 0, count - 1);
		} else {
			if (ImGui::DragInt(node.Key, &newVal, 1.0f)) {}
		}
		mainHovered = ImGui::IsItemHovered();
		if (inert && mainHovered)
			ImGui::SetTooltip("Disabled: has no effect. Mipmaps/anisotropy are commented out in "
				"ShadowsExterior.cpp (deferred shadows produce artifacted derivatives).");
		if (newVal != val) {
			TheSettingManager->SetSetting(node.Section, node.Key, newVal);
			TheSettingManager->LoadSettings();
		}
		RevertBtn();
		break;
	}
	default: {
		static std::unordered_map<ImGuiID, std::string> sBufs;
		ImGuiID id = ImGui::GetID(node.Key);
		std::string& persistent = sBufs[id];
		if (ImGui::GetActiveID() != id)
			persistent = node.Value;
		char buf[80];
		strncpy_s(buf, persistent.c_str(), sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if (ImGui::InputText(node.Key, buf, sizeof(buf)))
			persistent = buf;
		bool hovered = ImGui::IsItemHovered();
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			TheSettingManager->SetSettingS(node.Section, node.Key, buf);
			TheSettingManager->LoadSettings();
		}
		if (strcmp(node.Key, "KeyEnable") == 0) {
			ImGui::SameLine();
			if (ImGui::SmallButton("(?)"))
				ImGui::OpenPopup("DIKReference");
			RenderDIKPopup();
		}
		RevertBtn();
		if (inert) ImGui::EndDisabled();
		if (hovered && !node.Description.empty()) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
			ImGui::TextUnformatted(node.Description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::PopID();
		return;
	}
	}

	if (inert) ImGui::EndDisabled();

	if (mainHovered && !node.Description.empty()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
		ImGui::TextUnformatted(node.Description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	ImGui::PopID();
}

static void RenderContent() {
	if (SelectedSection.empty()) {
		ImGui::TextDisabled("Select a section from the left panel.");
		return;
	}

	SettingManager::Configuration::SettingList settings;
	TheSettingManager->FillMenuSettings(&settings, SelectedSection.c_str());

	if (settings.empty()) {
		ImGui::TextDisabled("No settings in this section.");
		return;
	}

	bool isShader = (SelectedSection.find("Shaders") == 0);

	ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", SelectedSection.c_str());

	// Shader enable toggle in content header
	if (isShader) {
		size_t dot1 = SelectedSection.find('.');
		size_t dot2 = SelectedSection.find('.', dot1 + 1);
		if (dot1 != std::string::npos && dot2 != std::string::npos) {
			std::string shaderName = SelectedSection.substr(dot1 + 1, dot2 - dot1 - 1);
			bool enabled = TheSettingManager->GetMenuShaderEnabled(shaderName.c_str());
			bool forced  = TheSettingManager->IsShaderForced(shaderName.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(forced);
			ImGui::PushStyleColor(ImGuiCol_Button, enabled
				? ImVec4(0.15f, 0.55f, 0.15f, 1.0f)
				: ImVec4(0.40f, 0.15f, 0.15f, 1.0f));
			if (ImGui::SmallButton(enabled ? "Enabled [Ins]" : "Disabled [Ins]")) {
				TheShaderManager->SwitchShaderStatus(shaderName.c_str());
				TheSettingManager->LoadSettings();
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();
		}
	}

	ImGui::Separator();
	ImGui::Spacing();

	// LUT section: render DNI cycle pickers before the normal settings loop
	if (SelectedSection == "Shaders.LUT.Main") {
		LUTEffect* lut = TheShaderManager->Effects.LUT;
		if (lut) {
			if (lut->Settings.PreTonemapping) {
				if (lut->Settings.HDRCompat)
					ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "HDR Compat: SDR LUTs normalized to HDR range (approximate).");
				else
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Pre-tonemapping mode: use HDR LUTs (float16 DDS converted from .CUBE).");
				ImGui::Spacing();
			}
			if (lut->LUTFiles.empty()) {
				ImGui::TextDisabled("No LUTs found in Data/Textures/NewVegasReloaded/LUTs/");
			} else {
				// DayLUT/NightLUT/InteriorLUT are hidden from the generic settings
				// loop (ShouldHideKey) since they're rendered as cycle pickers here
				// instead of raw InputText, which also means they never pass through
				// RenderSetting()'s lazy snapshot -- so without this, neither a
				// per-row revert button nor "Revert All" (RevertToSnapshot) has
				// anything to restore them to. Snapshot lazily here to match.
				auto snapLUT = [&](const char* key, int idx) -> std::string& {
					auto& sec = s_snapshot["Shaders.LUT.Main"];
					auto  it  = sec.find(key);
					if (it == sec.end())
						it = sec.emplace(key, lut->LUTFiles[idx]).first;
					return it->second;
				};
				std::string& snapDay      = snapLUT("DayLUT", lut->DayIdx);
				std::string& snapNight    = snapLUT("NightLUT", lut->NightIdx);
				std::string& snapInterior = snapLUT("InteriorLUT", lut->InteriorIdx);

				auto renderPicker = [&](const char* label, int& idx, int slot, const std::string& snapValue) {
					ImGui::PushID(label);
					ImGui::BeginGroup();
					ImGui::Text("%s", label);
					ImGui::SameLine();
					int delta = 0;
					if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) delta = -1;
					ImGui::SameLine();
					float nameStartX = ImGui::GetCursorPosX();
					ImGui::TextUnformatted(lut->LUTFiles[idx].c_str());
					ImGui::SameLine(nameStartX + 220.0f);
					if (ImGui::ArrowButton("##next", ImGuiDir_Right)) delta = 1;
					ImGui::SameLine();
					bool dirty = lut->LUTFiles[idx] != snapValue;
					if (!dirty) ImGui::BeginDisabled();
					if (ImGui::SmallButton("~"))
						lut->LoadLUT(slot, snapValue.c_str()); // re-syncs idx/texture via AssignLUTSlot
					if (!dirty) ImGui::EndDisabled();
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to value at session start");
					ImGui::EndGroup();
					if (ImGui::IsItemHovered() && (s_plusPressed || s_minusPressed))
						delta = s_plusPressed ? 1 : -1;
					if (delta != 0) {
						int n = (int)lut->LUTFiles.size();
						idx = ((idx + delta) % n + n) % n;
						lut->LoadLUT(slot, lut->LUTFiles[idx].c_str());
					}
					ImGui::PopID();
				};
				renderPicker("Day     ", lut->DayIdx,      0, snapDay);
				renderPicker("Night   ", lut->NightIdx,    1, snapNight);
				renderPicker("Interior", lut->InteriorIdx, 2, snapInterior);
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
	}

	// Build key->index map for RGB triple detection
	std::unordered_map<std::string, int> keyIdx;
	for (int i = 0; i < (int)settings.size(); i++)
		keyIdx[settings[i].Key] = i;

	std::unordered_set<std::string> handled;

	for (auto& s : settings) {
		std::string key(s.Key);
		if (handled.count(key)) continue;
		if (ShouldHideKey(s.Key)) continue;

		// Hide HDRCompat when PreTonemapping is off — it's meaningless in post-tonemapping mode
		if (strcmp(s.Key, "HDRCompat") == 0 && SelectedSection == "Shaders.LUT.Main") {
			LUTEffect* lut = TheShaderManager->Effects.LUT;
			if (lut && !lut->Settings.PreTonemapping) continue;
		}

		// RGB triple → color picker (shader sections only)
		// Settings iterate alphabetically (B, G, R), so detection must fire on
		// whichever channel is encountered first, not just 'R' — otherwise B
		// and G render as stray individual rows before R ever triggers this.
		if (isShader && key.size() > 1 &&
			(key.back() == 'R' || key.back() == 'G' || key.back() == 'B')) {
			std::string pfx = key.substr(0, key.size() - 1);
			std::string kR = pfx + "R", kG = pfx + "G", kB = pfx + "B";
			bool isColorCurve = (pfx == "ColorCurve" && strstr(s.Section, "Shaders.Coloring") != nullptr);
			if (!isColorCurve && keyIdx.count(kR) && keyIdx.count(kG) && keyIdx.count(kB) &&
				!handled.count(kR)) {
				handled.insert(kR);
				handled.insert(kG);
				handled.insert(kB);
				RenderColorTriple(settings[keyIdx[kR]], settings[keyIdx[kG]], settings[keyIdx[kB]], pfx);
				continue;
			}
		}

		RenderSetting(s, isShader);
	}
}

// Recursive sidebar tree — path is the full dotted section path, name is the display label.
static void RenderSectionNode(const std::string& path, const std::string& name, bool parentIsShaders) {
	if (ShouldHideSection(name)) return;

	StringList subsections;
	TheSettingManager->FillMenuSections(&subsections, path.c_str());

	bool isLeaf = subsections.empty();

	// Scope all widget IDs to this node's unique path.
	ImGui::PushID(path.c_str());

	if (isLeaf) {
		bool selected = (SelectedSection == path);
		// Mute non-selected leaves so they read as lower-weight nav targets
		// rather than siblings of the category/shader-toggle rows above them.
		if (!selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.75f, 1.0f));
		if (ImGui::Selectable(name.c_str(), selected))
			SelectedSection = path;
		if (!selected) ImGui::PopStyleColor();
		ImGui::PopID();
		return;
	}

	bool isShaderEntry = parentIsShaders;
	bool open = false;

	if (isShaderEntry) {
		bool enabled = TheSettingManager->GetMenuShaderEnabled(name.c_str());
		bool forced  = TheSettingManager->IsShaderForced(name.c_str());

		ImGui::PushStyleColor(ImGuiCol_Text, enabled
			? ImVec4(0.40f, 1.00f, 0.40f, 1.0f)
			: ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
		open = ImGui::TreeNode("##node");
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::BeginDisabled(forced);
		if (ImGui::Checkbox(name.c_str(), &enabled)) {
			TheShaderManager->SwitchShaderStatus(name.c_str());
			TheSettingManager->LoadSettings();
		}
		ImGui::EndDisabled();

		if (forced) {
			ImGui::SameLine();
			ImGui::TextDisabled("(forced)");
		}

		// Show render time in debug mode
		if (TheSettingManager->SettingsMain.Develop.DebugMode) {
			EffectRecord* effect = TheShaderManager->GetEffectByName(name.c_str());
			if (effect) {
				float total = max(effect->renderTime + effect->constantUpdateTime, 0.0f);
				if (!TheSettingManager->SettingsMain.Main.RenderEffects) total = 0.0f;
				std::stringstream ss;
				ss << std::fixed << std::setprecision(3) << total << " ms";
				ImGui::SameLine();
				ImGui::TextDisabled("%s", ss.str().c_str());
			}
		}
	} else {
		open = ImGui::TreeNode("##node");
		ImGui::SameLine();
		ImGui::TextUnformatted(name.c_str());
	}

	if (open) {
		bool childrenAreShaders = (path == "Shaders");
		for (auto& sub : subsections)
			RenderSectionNode(path + "." + sub, sub, childrenAreShaders);
		ImGui::TreePop();
	}

	ImGui::PopID();
}

static void RenderSidebar() {
	StringList topSections;
	TheSettingManager->FillMenuSections(&topSections, nullptr);
	// The active theme's global IndentSpacing is 0 (tight layout for the
	// settings panel), which leaves TreeNode children flush with their
	// parent here. Restore a modest per-depth indent scoped to just the
	// sidebar tree so nesting is visually legible without reintroducing
	// the wide default indent (21px) that would eat into this narrow panel.
	ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
	for (auto& section : topSections)
		RenderSectionNode(section, section, false);
	ImGui::PopStyleVar();
}

static void RenderMainMenuToast() {
	static auto startTime = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration<double>(std::chrono::system_clock::now() - startTime).count();
	if (elapsed > 5.0) return;

	std::string msg = std::string(PluginVersion::VersionString)
		+ " loaded  |  press "
		+ DikToName(TheSettingManager->SettingsMain.Menu.KeyEnable)
		+ " to open settings";

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImVec2 textSize    = ImGui::CalcTextSize(msg.c_str());
	ImVec2 pos         = ImVec2((displaySize.x - textSize.x) * 0.5f, displaySize.y - textSize.y - 12.0f);

	ImGui::GetForegroundDrawList()->AddText(
		ImVec2(pos.x + 1, pos.y + 1),
		IM_COL32(0, 0, 0, 200),
		msg.c_str());
	ImGui::GetForegroundDrawList()->AddText(
		pos,
		IM_COL32(220, 220, 220, 255),
		msg.c_str());
}

void ImGuiManager::BuildUI() {
	// Main menu: show toast only, no settings window
	if (InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main)) {
		RenderMainMenuToast();
		return;
	}

	if (!Visible) return;

	CfabUpdate();
	if (s_screenshotMode) return;

	// Dev Tools coc/cow picker's "visited this session" list -- see
	// TrackVisitedInteriorCell's own comment for why this, not an engine
	// enumerator, is what backs the interior-cell side of that picker.
	TrackVisitedInteriorCell();

	// Wait for Escape or Alt release before closing so the game doesn't see them held.
	static bool escapePending = false;
	static bool altPending    = false;
	if (ImGui::IsKeyPressed(ImGuiKey_Escape))   escapePending = true;
	if (ImGui::IsKeyPressed(ImGuiKey_LeftAlt) ||
	    ImGui::IsKeyPressed(ImGuiKey_RightAlt))  altPending    = true;
	if (escapePending && !ImGui::IsKeyDown(ImGuiKey_Escape)) {
		escapePending = false;
		SetOverlayVisible(false);
		return;
	}
	if (altPending &&
	    !ImGui::IsKeyDown(ImGuiKey_LeftAlt) &&
	    !ImGui::IsKeyDown(ImGuiKey_RightAlt)) {
		altPending = false;
		SetOverlayVisible(false);
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	bool open = true;
	if (!ImGui::Begin("New Vegas Reloaded", &open, flags) || !open) {
		ImGui::End();
		if (!open) SetOverlayVisible(false);
		return;
	}

	// Toolbar row 1: FX toggle | title/status | Revert / Disk / Save Copy / Save
	{
		bool renderFX = TheSettingManager->SettingsMain.Main.RenderEffects;
		ImGui::PushStyleColor(ImGuiCol_Button, renderFX
			? ImVec4(0.15f, 0.50f, 0.15f, 1.0f)
			: ImVec4(0.45f, 0.15f, 0.15f, 1.0f));
		if (ImGui::Button(renderFX ? "FX ON" : "FX OFF")) {
			TheSettingManager->SetSetting("Main.Main.Misc", "RenderEffects", !renderFX);
			TheSettingManager->LoadSettings();
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Toggle all NVR effects (hotkey: modifier + %s)",
				DikToName(TheSettingManager->SettingsMain.Menu.KeyEnable));
	}

	ImGui::SameLine();
	if (TheSettingManager->hasUnsavedChanges)
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "/!\\ Unsaved changes");
	else
		ImGui::TextDisabled("New Vegas Reloaded  %s", PluginVersion::VersionString);

	{
		// Size each button from its label so the theme's FramePadding is
		// respected (a fixed pixel width clips/squishes text if FramePadding
		// changes, e.g. with the wide horizontal padding of the current theme).
		auto BtnWidth = [](const char* label) {
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		};
		const float btnRevert = BtnWidth("Revert"), btnDisk = BtnWidth("Disk"),
			btnCopy = BtnWidth("Save Copy"), btnSave = BtnWidth("Save");
		const float spacing   = ImGui::GetStyle().ItemSpacing.x;
		const float totalW    = btnRevert + btnDisk + btnCopy + btnSave + spacing * 3.0f;
		ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalW);

		if (ImGui::Button("Revert", ImVec2(btnRevert, 0.0f))) {
			RevertToSnapshot();
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Revert all settings to values at session start");
		ImGui::SameLine();
		if (ImGui::Button("Disk", ImVec2(btnDisk, 0.0f)))
			ImGui::OpenPopup("##ConfirmDisk");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Reload all settings from saved TOML file");
		if (ImGui::BeginPopupModal("##ConfirmDisk", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Reload from disk? All unsaved changes will be lost.");
			ImGui::Spacing();
			if (ImGui::Button("Yes", ImVec2(80.0f, 0.0f))) {
				TheSettingManager->RevertSettings();
				s_snapshot.clear();
				s_masterMod = -1; // re-read from TOML after reload
				SelectedSection.clear();
				s_colorStatesNeedReset = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Save Copy", ImVec2(btnCopy, 0.0f))) {
			// Build timestamped path next to the DLL, e.g.:
			// NewVegasReloaded_20260519_2119_WastelandNV.dll.toml
			char dllPath[MAX_PATH] = {};
			HMODULE hMod = nullptr;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)&ImGuiManager::Initialize, &hMod);
			GetModuleFileNameA(hMod, dllPath, MAX_PATH);

			// Strip ".dll" suffix to get base path
			char* ext = strrchr(dllPath, '.');
			if (ext) *ext = '\0';

			// Timestamp
			time_t now = time(nullptr);
			tm lt = {};
			localtime_s(&lt, &now);
			char ts[32] = {};
			strftime(ts, sizeof(ts), "%Y%m%d_%H%M", &lt);

			// Location: always include worldspace + cell editor ID for TTW compatibility.
			auto sanitize = [](const char* s, std::string& out) {
				if (s && s[0])
					for (const char* c = s; *c; c++)
						out += (isalnum((unsigned char)*c) ? *c : '_');
			};
			std::string loc;
			if (Player && Player->parentCell) {
				TESWorldSpace* ws = Player->GetWorldSpace();
				std::string wsName, cellName;
				if (ws) sanitize(ws->GetEditorName(), wsName);
				sanitize(Player->parentCell->GetEditorName(), cellName);
				if (!wsName.empty()) loc = wsName;
				if (!cellName.empty()) loc += (loc.empty() ? "" : "_") + cellName;
				// Append grid coords for exterior cells
				TESObjectCELL::CellCoordinates* coords = Player->parentCell->coords;
				if (coords) {
					char grid[32] = {};
					_snprintf_s(grid, sizeof(grid), "_%d_%d", coords->x, coords->y);
					loc += grid;
				}
			}

			char savePath[MAX_PATH] = {};
			if (loc.empty())
				_snprintf_s(savePath, sizeof(savePath), "%s_%s.dll.toml", dllPath, ts);
			else
				_snprintf_s(savePath, sizeof(savePath), "%s_%s_%s.dll.toml", dllPath, ts, loc.c_str());

			TheSettingManager->SaveSettingsTo(savePath);
			InterfaceManager->ShowMessage("Settings copy saved.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Save", ImVec2(btnSave, 0.0f))) {
			TheSettingManager->SaveSettings();
			InterfaceManager->ShowMessage("Settings saved.");
		}
	}

	// Toolbar row 2: step size selector + text scale
	{
		ImGui::Text("Step:");
		static const float kSteps[]  = { 0.001f, 0.01f, 0.1f, 1.0f };
		static const char* kLabels[] = { "0.001", "0.01", "0.1", "1.0" };
		for (int i = 0; i < 4; i++) {
			ImGui::SameLine();
			bool active = fabsf(s_shaderStepSize - kSteps[i]) < 1e-6f;
			if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			if (ImGui::SmallButton(kLabels[i])) s_shaderStepSize = kSteps[i];
			if (active) ImGui::PopStyleColor();
		}
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::Text("Text:");
		ImGui::SameLine();
		if (ImGui::SmallButton("A-")) {
			io.FontGlobalScale = ImMax(0.5f, io.FontGlobalScale - 0.1f);
			TheSettingManager->SetSetting("Main.Menu.Style", "TextSize", (int)(io.FontGlobalScale * 100.0f + 0.5f));
			TheSettingManager->LoadSettings();
		}
		ImGui::SameLine();
		ImGui::Text("%.0f%%", io.FontGlobalScale * 100.0f);
		ImGui::SameLine();
		if (ImGui::SmallButton("A+")) {
			io.FontGlobalScale = ImMin(2.0f, io.FontGlobalScale + 0.1f);
			TheSettingManager->SetSetting("Main.Menu.Style", "TextSize", (int)(io.FontGlobalScale * 100.0f + 0.5f));
			TheSettingManager->LoadSettings();
		}

		// FX master switch modifier selector
		// s_masterMod is the authoritative runtime value; TOML provides initial value
		// and persistence (requires updated defaults.toml to be deployed).
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::Text("FX key:");
		static const char* kModLabels[] = { "Off", "Ctrl", "Alt", "Shift" };
		if (s_masterMod < 0)
			s_masterMod = (int)TheSettingManager->SettingsMain.Menu.MasterSwitchModifier;
		for (int i = 0; i < 4; i++) {
			ImGui::SameLine();
			bool active = (s_masterMod == i);
			if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			if (ImGui::SmallButton(kModLabels[i])) {
				s_masterMod = i;
				TheSettingManager->SetSetting("Main.Menu.Keys", "MasterSwitchModifier", i);
				TheSettingManager->LoadSettings();
			}
			if (active) ImGui::PopStyleColor();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Retro-Encab.")) s_cfabOpen = !s_cfabOpen;
		if (s_cfabActive) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.7f, 1.0f), "[active]");
		}
		ImGui::SameLine(0.0f, 20.0f);
		if (ImGui::SmallButton("Dev Tools")) {
			s_devOpen = !s_devOpen;
			if (!s_devOpen) DevPanelCleanup();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Presets")) s_presetManagerOpen = !s_presetManagerOpen;
		ImGui::SameLine();
		if (ImGui::SmallButton("Lighting")) s_lightingPanelOpen = !s_lightingPanelOpen;
		if (s_devFreecamOn) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.1f, 1.0f), "[freecam]");
		}
	}

	ImGui::Separator();

	// Two-panel layout
	float sidebarW = 260.0f;
	float contentH = ImGui::GetContentRegionAvail().y;

	ImGui::BeginChild("##sidebar", ImVec2(sidebarW, contentH), true);
	RenderSidebar();
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("##content", ImVec2(0.0f, contentH), true);
	RenderContent();
	ImGui::EndChild();

	ImGui::End();
	RenderConfabulator();
	RenderDevPanel();
	RenderPresetManagerPanel();
	RenderLightingPanel();
	RenderPresetConfirmPopup(); // unconditional -- stays functional even if the panel above gets closed mid-confirm
}
