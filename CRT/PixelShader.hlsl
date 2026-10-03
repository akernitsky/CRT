cbuffer CB : register(b0)
{
    float time;
    float width;
    float height;
    float channel;

    float power;
    float channelTimer;
    float2 padding;
}

float rand(float2 co)
{
    return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float2 CRT(float2 uv)
{
    uv = uv * 2 - 1;
    uv += uv * abs(uv) * 0.08;
    return uv * 0.47 + 0.5;
}

float3 Bars(float2 uv)
{
    float3 result = float3(0, 0, 1); // default
    float x = uv.x;

    if (x < 1.0 / 7.0)
        result = float3(1, 1, 1);
    else if (x < 2.0 / 7.0)
        result = float3(1, 1, 0);
    else if (x < 3.0 / 7.0)
        result = float3(0, 1, 1);
    else if (x < 4.0 / 7.0)
        result = float3(0, 1, 0);
    else if (x < 5.0 / 7.0)
        result = float3(1, 0, 1);
    else if (x < 6.0 / 7.0)
        result = float3(1, 0, 0);

    return result;
}

float DrawDigit(float2 screenUV, int digit)
{
    float aspect = width / height;
    float size = 0.08;
    float2 pos = float2(1.0 - size - 0.07, 0.12);
    float2 uv = screenUV - pos;
    uv.x *= aspect;
    uv /= size;

    bool inside = uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
    uv.y = 1.0 - uv.y;
    float thickness = 0.12;
    bool horizontal = uv.y > 0.85 || uv.y < 0.15 || abs(uv.y - 0.5) < thickness;
    bool on = (digit == 1 && abs(uv.x - 0.5) < thickness)
        || (digit == 2 && (horizontal || (uv.x > 0.85 && uv.y > 0.5) || (uv.x < 0.15 && uv.y < 0.5)))
        || (digit == 3 && (horizontal || uv.x > 0.85))
        || (digit == 4 && (uv.x > 0.85 || abs(uv.y - 0.5) < thickness || (uv.x < 0.15 && uv.y > 0.5)));
    return inside && on ? 1.0 : 0.0;
}
float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
    if (power <= 0.0 || width <= 0.0 || height <= 0.0)
        return float4(0, 0, 0, 1);
    // If the vertex shader passed a perspective position, divide by w to get screen coords.
    float2 screenUV = (pos.xy / pos.w) / float2(width, height);
    float2 uv = CRT(screenUV);

    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return float4(0, 0, 0, 1);

    float3 col;

    if (channel == 3)
        col = Bars(uv);
    else
    {
        float tear = sin(uv.y * 200 + time * 5) * 0.01;
        uv.x += tear;

        float n = rand(uv * time * 40);
        float i = (channel == 0) ? 1 : (channel == 1) ? 0.6 : 0.3;

        float r = rand((uv + float2(0.002, 0)) * time * 40);
        float b = rand((uv - float2(0.002, 0)) * time * 40);

        col = float3(r, n, b) * i;

        col *= 0.9 + 0.1 * sin(uv.y * 900);
    }

    float d = distance(uv, float2(0.5, 0.5));
    col *= 1 - d * 1.2;

    col += col * col * 0.2;

    if (power < 1.0)
    {
        float distToCenter = abs(uv.y - 0.5);
        float mask = smoothstep(0.0, power, distToCenter);
        mask = 1.0 - mask;
        col *= mask;
    }

    float3 finalColor = col;

    if (channelTimer > 0.0)
    {
        // Ensure we use a signed integer and a known non-negative integer index.
        int digit = clamp((int)floor(channel), 0, 3) + 1; // ensures 1..4

        float on = DrawDigit(screenUV, digit);

        float3 osdColor = float3(0.0, 1.0, 0.0);

        finalColor = lerp(finalColor, osdColor, on);
    }

    return float4(finalColor * power, 1.0);
}

