#include "InverseSquareLighting.h"

void InverseSquareLightingShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_InverseSquare", &Constants.Data);
}

// light(x) = (q / (q + x) - k) / (1 - k) over x = (d / r)^2 (Shaders/Includes/InverseSquare.hlsl),
// scaled so a lamp lights the floor round it as much as vanilla did: their integrals over the lamp's
// disc (u = d / r, area u du) match. Vanilla's linear light is (1 - u^2)^2 (its gamma-space 1 - u^2,
// decoded): 1/6. The inverse square's: [q/2 ln((q + 1) / q) - k/2] / (1 - k).
void InverseSquareLightingShaders::UpdateSettings() {
	Settings.Size = std::clamp(TheSettingManager->GetSettingF("Shaders.InverseSquareLighting.Main", "Size"), 0.02f, 1.0f);
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF("Shaders.InverseSquareLighting.Main", "Strength"), 0.0f, 4.0f);
	Settings.FillLightRadius = std::clamp(TheSettingManager->GetSettingF("Shaders.InverseSquareLighting.Main", "FillLightRadius"), 0.0f, 100000.0f);

	const float q = Settings.Size * Settings.Size;
	const float k = q / (q + 1.0f);
	const float Integral = (0.5f * q * logf((q + 1.0f) / q) - 0.5f * k) / (1.0f - k);
	const float Scale = Settings.Strength * (1.0f / 6.0f) / max(Integral, 1e-6f);
	Constants.Data.y = q;
	Constants.Data.z = k;
	Constants.Data.w = Scale / (1.0f - k);
}

// Run every frame, on or off (ShaderManager::UpdateConstants): off, the shaders fall back to vanilla.
void InverseSquareLightingShaders::UpdateConstants() {
	Constants.Data.x = !Enabled ? 0.0f : (Settings.FillLightRadius > 0.0f ? Settings.FillLightRadius : 1e30f);
}
