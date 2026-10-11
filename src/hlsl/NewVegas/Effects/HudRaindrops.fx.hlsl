// The wetness of the HUD raindrops (src/effects/HudRaindrops.h), one value kept from frame to
// frame: the drops themselves are drawn by the first-person arms' and weapon's own shaders
// (Shaders/Includes/HudRaindrops.hlsl), which read it.
//
//   technique 0  the wetness eased toward the rain (1 x 1, from the one kept last frame): up over
//                WetTime while the player stands in the rain, down over DryTime under cover, indoors
//                or after it; wiped underwater; held at 1 with DebugView 1
//   technique 1  kept for next frame
//   technique 2  the drops on the Pip-Boy's screen: drawn into the screen's own texture once the game
//                has rendered the menu into it (HudRaindropsEffect::DrawOnScreen), so they stay on
//                the screen. The screen is unlit, so they refract the menu under them, darken toward
//                their rims and catch a small glint, as the arms' drops do with real light.

float4 TESR_HudRainData;    // x rain (0-1)
float4 TESR_HudRainTime;    // y this frame's seconds, z seconds to wet through, w seconds to dry
float4 TESR_HudRainShade;   // z 1 under cover regardless (indoors), w 1 to wipe (underwater)
float4 TESR_HudRainDebug;   // x 1 always wet
float4 TESR_HudRainScreen;  // the Pip-Boy screen's drops: x threshold (Density), y clock, z refraction, w drop texture tiles across the screen
float4x4 TESR_ShadowCameraToLightTransformOrtho;

sampler2D TESR_OrthoMapBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_HudRainState : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_HudRainNew : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_HudRainScreenCopy : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_HudRainTexture : register(s4) < string ResourceName = "Effects\hud_raindrops.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

// How far above the camera the cover test looks (game units): above the player's own head, which the
// ortho map may hold, and below any roof worth the name.
#define COVER_HEIGHT 48.0f
// The ortho map's depth slack (Effects/WetWorld.fx uses the same).
#define COVER_THICKNESS 0.003f

struct VSOUT {
    float4 vertPos : POSITION;
    float2 UVCoord : TEXCOORD0;
};

struct VSIN {
    float4 vertPos : POSITION0;
    float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN) {
    VSOUT OUT = (VSOUT)0.0f;
    OUT.vertPos = IN.vertPos;
    OUT.UVCoord = IN.UVCoord;
    return OUT;
}

float4 UpdateWetness(VSOUT IN) : COLOR0 {
    float wet = tex2D(TESR_HudRainState, float2(0.5f, 0.5f)).r;

    // Under cover: indoors, or something in the ortho map above the camera. Outside the ortho map
    // the sky is open.
    float covered = TESR_HudRainShade.z;
    [branch] if (covered < 0.5f && TESR_HudRainData.x > 0.0f) {
        float4 o = mul(float4(0.0f, 0.0f, COVER_HEIGHT, 1.0f), TESR_ShadowCameraToLightTransformOrtho);
        o.xyz /= o.w;
        float2 ouv = float2(o.x * 0.5f + 0.5f, o.y * -0.5f + 0.5f);
        float top = tex2Dlod(TESR_OrthoMapBuffer, float4(ouv, 0.0f, 0.0f)).r;
        bool inside = all(ouv > 0.0f) && all(ouv < 1.0f);
        covered = (inside && o.z > top + COVER_THICKNESS) ? 1.0f : 0.0f;
    }

    float exposure = TESR_HudRainData.x * (1.0f - covered);
    float dt = TESR_HudRainTime.y;
    wet = exposure > 0.0f ? min(wet + dt * exposure / TESR_HudRainTime.z, 1.0f) : max(wet - dt / TESR_HudRainTime.w, 0.0f);
    if (TESR_HudRainShade.w > 0.5f) wet = 0.0f;
    if (TESR_HudRainDebug.x > 0.5f) wet = 1.0f;
    return float4(wet, covered, 0.0f, 1.0f);
}

float4 KeepWetness(VSOUT IN) : COLOR0 {
    return tex2D(TESR_HudRainNew, float2(0.5f, 0.5f));
}

// One layer, as Shaders/Includes/HudRaindrops.hlsl's HudRainLayer.
float3 ScreenDropLayer(float4 texel, float speed) {
    float2 slope = texel.gb * 2.0f - 1.0f;
    float life = 1.0f - frac(texel.a + TESR_HudRainScreen.y * speed);
    float strength = saturate(life * texel.r - TESR_HudRainScreen.x);
    return float3(slope * strength, strength);
}

float4 ScreenDrops(VSOUT IN) : COLOR0 {
    float2 uv = IN.UVCoord;
    float wet = tex2D(TESR_HudRainState, float2(0.5f, 0.5f)).r;
    float2 aspect = float2(1.0f, 0.75f);   // the menu is drawn 1280 x 960 (FORenderedMenu::Render)
    float2 p = uv * aspect * TESR_HudRainScreen.w;
    float3 drops = ScreenDropLayer(tex2D(TESR_HudRainTexture, p), 0.2f)
                 + ScreenDropLayer(tex2D(TESR_HudRainTexture, (p + float2(0.23f, 0.46f)) * 1.87f), 0.1f);
    drops *= wet;
    drops.xy = clamp(drops.xy, -1.0f, 1.0f);

    // The menu under the drop, shifted along its slope (in screen coordinates, back from the tiles).
    float2 shift = drops.xy * TESR_HudRainScreen.z / (TESR_HudRainScreen.w * aspect);
    float4 color = tex2D(TESR_HudRainScreenCopy, uv + shift);

    // Darker toward the rim, where the drop bends the screen's light away; a glint from above.
    float3 N = normalize(float3(drops.xy * 2.5f, 1.0f));
    float glint = pow(saturate(dot(N, float3(-0.37f, 0.46f, 0.81f))), 60.0f) * saturate(drops.z * 2.0f);
    color.rgb = color.rgb * (1.0f - 0.35f * saturate(length(drops.xy))) + glint * 0.5f;
    return color;
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 UpdateWetness();
    }
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 KeepWetness();
    }
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 ScreenDrops();
    }
}
