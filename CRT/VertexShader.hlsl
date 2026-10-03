struct VS_OUT
{
    float4 pos : SV_POSITION;
};

VS_OUT main(uint id : SV_VertexID)
{
    float2 p[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    VS_OUT o;
    o.pos = float4(p[id], 0, 1);
    return o;
}