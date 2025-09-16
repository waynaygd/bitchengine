cbuffer CBPerObject : register(b0)
{
    float4x4 M;
    float4x4 V;
    float4x4 P;
    float4x4 MIT; // ? inverse-transpose(M)
    float uvMul;
    float3 _pad;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOut
{
    float4 posH : SV_Position;
    float3 nrmV : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VSOut main(VSIn i)
{
    VSOut o;

    float4 wpos = mul(float4(i.pos, 1), M);
    float4 vpos = mul(wpos, V);
    o.posH = mul(vpos, P);

    // �������: object -> world (����� (M^-1)^T), ����� world -> view
    float3 nrmW = normalize(mul(i.nrm, (float3x3) MIT)); // row * matrix (��� � � �������)
    o.nrmV = normalize(mul(nrmW, (float3x3) V)); // � view-space

    o.uv = i.uv * uvMul;
    return o;
}

