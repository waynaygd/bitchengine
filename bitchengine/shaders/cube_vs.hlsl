cbuffer VSConstants : register(b0)
{
    float4x4 mvp;
}

struct VSIn
{
    float3 pos : POSITION;
    float3 col : COLOR;
    float2 uv : TEXCOORD0;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 col : COLOR;
    float2 uv : TEXCOORD0;
};

VSOut main(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 1), mvp);
    o.col = i.col;
    o.uv = i.uv;
    return o;
}