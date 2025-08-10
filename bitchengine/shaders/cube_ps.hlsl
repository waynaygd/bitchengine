Texture2D tex0 : register(t0);
SamplerState sam0 : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float3 col : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    float4 texColor = tex0.Sample(sam0, i.uv);
    return texColor * float4(i.col, 1);
}