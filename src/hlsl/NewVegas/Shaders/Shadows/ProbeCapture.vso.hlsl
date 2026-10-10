// A light probe's cubemap (BounceLightingShaders::CaptureProbe): a static lit mesh, camera relative,
// through one face's view.

row_major float4x4 TESR_ShadowWorldTransform : register(c0);
row_major float4x4 TESR_ShadowViewProjTransform : register(c4);

struct VS_INPUT {
    float4 position : POSITION;
    float3 normal : NORMAL;
    float4 texcoord_0 : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 worldPos : TEXCOORD0;   // camera relative
    float3 normal : TEXCOORD1;     // world (0 for a mesh without normals)
    float2 uv : TEXCOORD2;
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;
    float4 world = mul(float4(IN.position.xyz, 1.0f), TESR_ShadowWorldTransform);
    OUT.position = mul(world, TESR_ShadowViewProjTransform);
    OUT.worldPos = float4(world.xyz, 1.0f);
    OUT.normal = mul(IN.normal, (float3x3)TESR_ShadowWorldTransform);
    OUT.uv = IN.texcoord_0.xy;
    return OUT;
}
