// light_vs.hlsl
struct VSOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;

    // большой треугольник на весь экран
    const float2 verts[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };

    float2 pos = verts[vid];
    o.posH = float4(pos, 0.0, 1.0);

    // uv.x как раньше; uv.y Ч с инвертом
    float2 uv = pos * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y; // <Ч ключева€ строка
    o.uv = uv;

    return o;
}