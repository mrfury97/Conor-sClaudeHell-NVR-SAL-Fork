// Pointlight helper functions.

#if defined(__INTELLISENSE__)
    #include "Helpers.hlsl"
    #include "InverseSquare.hlsl"
#else
    #include "includes/InverseSquare.hlsl"
#endif

// Vanilla attenuation, lightVector not normalized. Or, with InverseSquare on, its inverse square
// counterpart (Includes/InverseSquare.hlsl): every caller scales the gamma-space light colour by it.
float vanillaAtt(float3 lightVector, float radius) {
    const float3 att = lightVector / radius;
    return lampFalloff(dot(att, att), radius);
}

// Same formula as vanillaAtt, but from a precomputed object-space squared distance rather than
// a light vector. vanillaAtt(lightVector, radius) == 1 - saturate(dot(lightVector, lightVector)
// / radius^2), so this is exact, not an approximation -- it just lets the caller supply
// dot(light, light) computed in object space instead of a vector that may have been carried
// through a (possibly non-orthonormal) TBN transform, where length is not preserved.
float vanillaAttSq(float distSq, float radius) {
    return lampFalloff(distSq / (radius * radius), radius);
}

// https://lisyarus.github.io/blog/posts/point-light-attenuation.html
float lisyarusAtt(float3 lightVector, float radius, float falloff=5.0) {
    const float3 normalized = lightVector / radius;
    const float s2 = shades(normalized, normalized);

    return sqr(1 - s2) / (1 + falloff * s2);
}

// Modified Frostbite attenuation (UE4), lightVector not normalized.
float frostbiteAtt(float3 lightVector, float radius) {
    const float squaredDistance = dot(lightVector, lightVector);
    const float invSqrRadius = 1.f / (radius * radius);
    const float factor = squaredDistance * invSqrRadius;
    const float smoothFactor = saturate(1.f - factor * factor);
    const float smoothDistanceAtt = smoothFactor * smoothFactor;
    
    return smoothDistanceAtt / (squaredDistance + 1);
}
