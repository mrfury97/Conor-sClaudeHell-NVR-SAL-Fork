// Inverse square lighting for lamps (as Community Shaders' Inverse Square Lighting), keeping each
// lamp's own radius.
//
// Vanilla lamps fall off as 1 - (d / r)^2 (in the gamma-space light colour): bright to near their
// edge, then gone -- a spotlight look. Real light falls off as 1 / d^2. Community Shaders works out a
// new radius from each lamp's brightness and a cutoff; here the engine's radius stays (it decides
// which objects a lamp lights, and what that costs) and only the curve inside it changes:
//
//   light(x) = (q / (q + x) - k) / (1 - k),   x = (d / r)^2,   q = size^2,   k = q / (q + 1)
//
// an inverse square law, 1 / (1 + (d / s)^2), from a bulb of radius s = size x r (finite at the
// bulb), lowered by its value at the radius so that it reaches 0 there. It is then scaled so that a
// lamp puts as much light on the floor round it as vanilla's falloff did (their integrals over the
// lamp's disc match, InverseSquareLightingShaders::UpdateSettings), times Strength:
// brighter near the lamp, dimmer toward its edge, the same light overall.
//
// [Shaders.InverseSquareLighting]: Status Enabled; Main Size, Strength, FillLightRadius.
//
// Lamps reaching farther than [Shaders.InverseSquareLighting.Main] FillLightRadius keep vanilla's
// falloff: a level's big fill lights, hung high to light an area evenly, turned blotchy.
//
// TESR_InverseSquare: x 0 off, else that radius; y q, z k, w the scale over (1 - k).
// TESR_InverseSquareFill: per draw, for the shaders that do not have their lamps' radii (the skin
// shaders' light vectors come divided by it): 1 for pass light k (PSLightColor[k]) if it is a fill
// light (NewVegas/Hooks/Shaders.cpp, ForwardPointShadows::OnDraw); 0 otherwise and between draws.

#ifndef INVERSE_SQUARE_HLSL
#define INVERSE_SQUARE_HLSL

float4 TESR_InverseSquare : register(c215);
float4 TESR_InverseSquareFill[2] : register(c216);

// The lamp's light at x = (distance / radius)^2, linear.
float InverseSquareLight(float x) {
    float q = TESR_InverseSquare.y;
    return max(q / (q + saturate(x)) - TESR_InverseSquare.z, 0.0f) * TESR_InverseSquare.w;
}

// For the shaders that scale the gamma-space light colour by the falloff (as vanilla's 1 - x),
// then decode it (or, without LinearLighting, show it as it is): the square root of the linear
// light, so that what is seen falls off as the inverse square. vanilla: keep vanilla's.
float lampFalloffAs(float x, bool vanilla) {
    return (TESR_InverseSquare.x > 0.0f && !vanilla) ? sqrt(InverseSquareLight(x)) : 1.0f - saturate(x);
}
// By the lamp's radius: one comparison does for both tests (x is 0 when off, and radii are above
// 0), which keeps the skin shaders' merged lamp loop within ps_3_0's 32 temporaries.
float lampFalloff(float x, float radius) {
    return radius < TESR_InverseSquare.x ? sqrt(InverseSquareLight(x)) : 1.0f - saturate(x);
}

// For the shaders that scale the already linear light by it (hair, SM3002 / SM3003).
float lampFalloffLinear(float x, float radius) {
    return radius < TESR_InverseSquare.x ? InverseSquareLight(x) : 1.0f - saturate(x);
}

#endif
