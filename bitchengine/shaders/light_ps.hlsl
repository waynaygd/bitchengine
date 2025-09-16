cbuffer CBLighting : register(b1)
{
    float3 camPos;
    float debugMode;
    float3 lightDir;
    float _pad1;
    float3 lightColor;
    float _pad2;
    float4x4 invP;
};

Texture2D gAlbedo : register(t0);
Texture2D gNormalR : register(t1);
Texture2D gDepth : register(t2);
SamplerState gSamp : register(s0);

float3 ReconstructPosV(float2 uv, float depth01, float4x4 invP)
{
    // D3D: depth в [0..1], NDC = uv*2-1
    float4 clip = float4(uv * 2 - 1, depth01, 1);
    float4 view = mul(clip, invP);
    return view.xyz / view.w;
}

float4 main(float4 posH : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    // Debug views
    if (debugMode >= 1.0 && debugMode < 2.0) // Albedo
        return float4(gAlbedo.Sample(gSamp, uv).rgb, 1);

    if (debugMode >= 2.0 && debugMode < 3.0) // Normal
    {
        float3 n = normalize(gNormalR.Sample(gSamp, uv).rgb);
        return float4(0.5 * n + 0.5, 1);
    }

    if (debugMode >= 3.0 && debugMode < 4.0)
    {
        float d = gDepth.Sample(gSamp, uv).r;
        return float4(d, d, d, 1);
    }

    // Lighting
    float3 albedo = gAlbedo.Sample(gSamp, uv).rgb;
    float3 nrm = normalize(gNormalR.Sample(gSamp, uv).rgb);
    float depth = gDepth.Sample(gSamp, uv).r;

    // фон (depth ~1) Ч чЄрный; skybox дорисуем отдельно
    if (depth >= 1.0 - 1e-5)
        return float4(0, 0, 0, 1);

    float3 P = ReconstructPosV(uv, depth, invP); // view-space
    float3 V = normalize(-P);
    float3 L = normalize(-lightDir); // если lightDir Ђиз источникаї

    float ndl = saturate(dot(nrm, L));
    float3 col = albedo * (0.05 + 0.95 * ndl) * lightColor;

    return float4(col, 1);
}
