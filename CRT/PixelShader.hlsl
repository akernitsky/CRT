cbuffer CB : register(b0)
{
    float time;
    float width;
    float height;
    float channel;

    float power;
    float channelTimer;
    float clockSeconds;
    float channelNumber;
    float volume;
    float volumeTimer;
    float2 reserved;
}

float rand(float2 co)
{
    return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}
float StaticGrain(uint2 cell, uint frame, uint salt)
{
    uint value = cell.x * 1973u + cell.y * 9277u + frame * 26699u + salt * 31847u;
    value = (value ^ (value >> 16)) * 2246822519u;
    value = (value ^ (value >> 13)) * 3266489917u;
    value ^= value >> 16;
    return (value & 0x00ffffffu) / 16777215.0;
}
float SnowSample(float2 p, uint frame, uint salt)
{
    uint2 cell = (uint2)floor(max(p,0));
    float2 f = frac(max(p,0));
    // Receiver bandwidth stretches speckles horizontally; only a small
    // vertical transition blends adjacent scan lines.
    f.x = smoothstep(0.12,0.88,f.x);
    f.y = smoothstep(0.34,0.66,f.y);
    return lerp(lerp(StaticGrain(cell,frame,salt),StaticGrain(cell + uint2(1,0),frame,salt),f.x),
        lerp(StaticGrain(cell + uint2(0,1),frame,salt),StaticGrain(cell + uint2(1,1),frame,salt),f.x),f.y);
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

// Seven-segment digits, shared by the channel indicator and broadcast clock.
float Digit(float2 uv, int digit)
{
    const uint masks[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};
    uint bits = masks[clamp(digit, 0, 9)];
    float2 p = uv * float2(1, 2);
    float thickness = 0.10;
    bool horizontal = p.x > 0.15 && p.x < 0.85;
    bool upper = p.y > 0.15 && p.y < 0.95;
    bool lower = p.y > 1.05 && p.y < 1.85;
    bool on = ((bits & 1) != 0 && horizontal && abs(p.y - 0.10) < thickness)
        || ((bits & 2) != 0 && upper && abs(p.x - 0.90) < thickness)
        || ((bits & 4) != 0 && lower && abs(p.x - 0.90) < thickness)
        || ((bits & 8) != 0 && horizontal && abs(p.y - 1.90) < thickness)
        || ((bits & 16) != 0 && lower && abs(p.x - 0.10) < thickness)
        || ((bits & 32) != 0 && upper && abs(p.x - 0.10) < thickness)
        || ((bits & 64) != 0 && horizontal && abs(p.y - 1.00) < thickness);
    return all(uv >= 0) && all(uv <= 1) && on ? 1.0 : 0.0;
}

float DrawDigit(float2 uv, int digit)
{
    float2 p = uv - float2(0.85, 0.12);
    p.x *= width / height;
    return Digit(p / float2(0.04, 0.08), digit);
}

float3 BroadcastBars(float2 p)
{
    float3 col = (float3)(floor((p.x - 0.51) * 12) / 35);
    if (p.x < 0.17) col = float3(0, 0.12, 0.3);
    else if (p.x < 0.34) col = 1;
    else if (p.x < 0.51) col = float3(0.2, 0, 0.3);
    int stripe = min((int)(p.x * 7), 6);
    float3 stripeColor = stripe == 0 ? float3(0, 0, 0.75) : stripe == 2 ? float3(0.75, 0, 0.75)
        : stripe == 4 ? float3(0, 0.75, 0.75) : float3(0.75, 0.75, 0.75);
    if ((stripe & 1) != 0) stripeColor = 0.03;
    float3 bars = Bars(p) * 0.75;
    if (p.y < 0.66) col = bars;
    else if (p.y < 0.75) col = stripeColor;
    return col;
}
// Historical charts use a 768 x 576 reference plane. No sampled image assets.
float Box(float2 p, float4 bounds)
{
    return all(p >= bounds.xy) && all(p < bounds.zw) ? 1 : 0;
}
float Stroke(float2 p, float2 a, float2 b, float radius)
{
    float2 v = b - a;
    float distanceToLine = length(p - a - v * saturate(dot(p - a, v) / max(dot(v, v), 0.00001)));
    return 1 - smoothstep(radius - 0.45, radius + 0.45, distanceToLine);
}
float Ring(float2 p, float2 center, float radius, float thickness)
{
    return 1 - smoothstep(thickness - 0.45, thickness + 0.45, abs(length(p - center) - radius));
}
// Filter periodic lines at their actual screen footprint to avoid shimmering.
float Stripes(float coordinate, float period)
{
    float footprint = max(768 / width, 576 / height);
    float visibility = saturate(period / (2 * footprint) - 0.5);
    return 0.5 + 0.5 * visibility * sin(coordinate * 6.2831853 / period);
}
float CheckerBorder(float2 p)
{
    float check = fmod(floor(p.x / 48) + floor(p.y / 48), 2);
    float edge = Box(p, float4(0, 0, 768, 8)) + Box(p, float4(0, 568, 768, 576))
        + Box(p, float4(0, 0, 16, 576)) + Box(p, float4(752, 0, 768, 576));
    return edge > 0 ? check : -1;
}
float ReferenceGrid(float2 p, float2 spacing)
{
    float2 d = abs(frac(p / spacing + 0.5) - 0.5) * spacing;
    return min(d.x, d.y) < 1 ? 1 : 0;
}
// Compact procedural 5 x 7 type for station labels and measurement markings.
uint2 GlyphBits(int c)
{
    uint2 bits = 0;
    switch(c)
    {
    case 48: bits = uint2(2738546222u, 3u); break;
    case 49: bits = uint2(2286031044u, 3u); break;
    case 50: bits = uint2(3292807726u, 7u); break;
    case 51: bits = uint2(3775349263u, 3u); break;
    case 52: bits = uint2(301246856u, 2u); break;
    case 53: bits = uint2(3775366207u, 3u); break;
    case 54: bits = uint2(2736227374u, 3u); break;
    case 55: bits = uint2(2216829471u, 0u); break;
    case 56: bits = uint2(2736211502u, 3u); break;
    case 57: bits = uint2(2702132782u, 3u); break;
    case 65: bits = uint2(1663026734u, 4u); break;
    case 66: bits = uint2(3809986095u, 3u); break;
    case 67: bits = uint2(2182120510u, 7u); break;
    case 68: bits = uint2(3810051631u, 3u); break;
    case 69: bits = uint2(3256321087u, 7u); break;
    case 72: bits = uint2(1663026737u, 4u); break;
    case 73: bits = uint2(3359772831u, 7u); break;
    case 75: bits = uint2(1381078321u, 4u); break;
    case 76: bits = uint2(3255862305u, 7u); break;
    case 77: bits = uint2(1662703473u, 4u); break;
    case 78: bits = uint2(1662834289u, 4u); break;
    case 79: bits = uint2(2736309806u, 3u); break;
    case 80: bits = uint2(1108854319u, 0u); break;
    case 82: bits = uint2(1381484079u, 4u); break;
    case 83: bits = uint2(3775333438u, 3u); break;
    case 84: bits = uint2(138547359u, 1u); break;
    case 85: bits = uint2(2736309809u, 3u); break;
    case 86: bits = uint2(353945137u, 1u); break;
    case 128: bits = uint2(3809969215u, 3u); break;
    case 129: bits = uint2(1697204828u, 4u); break;
    case 130: bits = uint2(1664800561u, 4u); break;
    case 131: bits = uint2(1049929001u, 4u); break;
    default: break;
    }
    return bits;
}
float Glyph(float2 q, int c)
{
    int2 cell = (int2)floor(q);
    uint2 bits = GlyphBits(c);
    int bit = clamp(cell.y * 5 + cell.x, 0, 34);
    uint value = bit < 32 ? bits.x >> bit : bits.y >> (bit - 32);
    return all(cell >= 0) && cell.x < 5 && cell.y < 7 ? (float)(value & 1) : 0;
}
float Number(float2 p, float2 origin, float size, int value, int digits)
{
    float2 q = (p - origin) / size;
    int index = (int)floor(q.x / 6);
    int divisor = digits == 4 ? (index == 0 ? 1000 : index == 1 ? 100 : index == 2 ? 10 : 1) : digits == 3 ? (index == 0 ? 100 : index == 1 ? 10 : 1)
        : digits == 2 && index == 0 ? 10 : 1;
    return index >= 0 && index < digits ? Glyph(float2(q.x - index * 6, q.y), 48 + (int)(((uint)value / (uint)divisor) % 10u)) : 0;
}
float Label(float2 p, float2 origin, float size, int label)
{
    const int philips[14] = {80,72,73,76,73,80,83,32,32,32,32,32,32,32};
    const int model[14] = {80,77,53,53,52,52,32,32,32,32,32,32,32,32};
    const int radio[14] = {68,65,78,77,65,82,75,83,32,82,65,68,73,79};
    const int table[14] = {84,65,128,129,130,131,65,32,32,32,32,32,32,32};
    float2 q = (p - origin) / size;
    int index = (int)floor(q.x / 6);
    int i = clamp(index, 0, 13);
    int c = label == 0 ? philips[i] : label == 1 ? model[i] : label == 2 ? radio[i] : table[i];
    return index >= 0 && index < 14 ? Glyph(float2(q.x - index * 6, q.y), c) : 0;
}
float3 Philips(float2 p)
{
    float3 col = ReferenceGrid(p - float2(16, 0), float2(48, 48)) ? 1 : 0.5;
    // PAL colour-difference patches outside the circle.
    if (Box(p, float4(64, 48, 112, 312))) col = float3(0.25, 0.62, 0.5);
    if (Box(p, float4(64, 312, 112, 528))) col = float3(0.7, 0.3, 0.5);
    if (Box(p, float4(112, 48, 160, 144))) col = float3(0.3, 0.45, 0.9);
    if (Box(p, float4(112, 432, 160, 528))) col = float3(0.65, 0.5, 0.1);
    if (Box(p, float4(656, 48, 704, 312))) col = float3(0.5, 0.6, 0.08);
    if (Box(p, float4(656, 312, 704, 528))) col = float3(0.5, 0.35, 0.9);
    if (Box(p, float4(608, 48, 656, 144))) col = float3(0.3, 0.45, 0.9);
    if (Box(p, float4(608, 432, 656, 528))) col = float3(0.65, 0.5, 0.1);
    if (length(p - float2(384, 288)) < 264)
    {
        col = 1;
        if (p.y >= 96 && p.y < 192) col = 0;
        if (Box(p, float4(240,96,528,144))) col = 1;
        if (p.y >= 144 && p.y < 192) col = fmod(floor((p.x - 152) / 30), 2) ? 0.75 : 0;
        if (p.y >= 192 && p.y < 288) col = Bars(float2((p.x - 120) / 616 + 1.0/7.0,0)) * 0.75;
        if (p.y >= 288 && p.y < 432) col = 0;
        if (p.y >= 336 && p.y < 432 && p.x >= 144 && p.x < 624)
        {
            int band = clamp((int)((p.x - 144) / 96),0,4);
            const float periods[5] = {16,8,6,4,3};
            col = Stripes(p.x - 144, periods[band]);
        }
        if (p.y >= 432 && p.y < 480) col = floor(saturate((p.x - 192)/480)*6)/5;
        if (Box(p,float4(240,480,528,528))) col = 0;
        if (p.y >= 528) col = abs(p.x - 384) < 24 ? float3(0.75,0,0) : float3(0.75,0.75,0);
        if (Box(p,float4(288,48,480,96))) col = 0;
        col = lerp(col,1,Label(p,float2(300,58),4,0));
        col = lerp(col,1,Label(p,float2(300,491),4,1));
        // Centre cross and horizontal graticule.
        if (Box(p,float4(360,240,408,384))) col = 0;
        float cross = Stroke(p,float2(384,240),float2(384,384),0.8)
            + Stroke(p,float2(120,312),float2(648,312),0.8);
        if (p.y >= 288 && p.y < 336 && abs(frac((p.x-120)/48+0.5)-0.5)<0.015) cross=1;
        col = lerp(col,1,saturate(cross));
        if (Box(p,float4(260,96,264,144)) || Box(p,float4(260,480,264,528))) col=0;
    }
    float border = CheckerBorder(p);
    return border >= 0 ? (float3)border : col;
}
float3 UEIT(float2 p)
{
    float grid = ReferenceGrid(p - float2(24,16), float2(32,32));
    float3 col = lerp((float3)0.35,(float3)0.72,grid);
    if (length(p-float2(384,288))<256) col=0.75;
    if (Box(p,float4(160,112,608,144)) || Box(p,float4(320,64,448,96))
        || Box(p,float4(320,480,448,512))) col=lerp((float3)0.35,(float3)0.72,grid);
    // Four corner resolution discs.
    float2 center = float2(p.x < 384 ? 120 : 648, p.y < 288 ? 80 : 496);
    float2 c = p - center;
    if (length(c)<64)
    {
        col=0.75;
        if (abs(c.x)<32 && abs(c.y)<32)
            col=Stripes(c.x,c.y<0 ? 3.2 : 2.4);
        if (abs(c.y)<2 && abs(c.x)<32) col=0.35;
        float ink=Number(p,center+float2(-48,-32),2,p.y<288?3:4,1)
            + Number(p,center+float2(-48,0),2,p.y<288?4:3,1);
        col=lerp(col,0,saturate(ink));
    }
    if (p.y>=144 && p.y<208)
    {
        float3 bar=Bars(float2((p.x-24)/672,0))*0.75;
        col=p.x>=696 ? (float3)0.35 : lerp((float3)0.35,bar,0.55);
    }
    if (p.y>=208 && p.y<240) col=floor(saturate((p.x-120)/576)*8)/7;
    if (Box(p,float4(128,240,640,272)))
    {
        int zone=clamp((int)((p.x-128)/170.7),0,2);
        float3 a=zone==0?float3(0,0.75,0):zone==1?float3(0,0,0.75):float3(0,0.75,0.75);
        float3 b=zone==0?float3(0.75,0,0.75):zone==1?float3(0.75,0.75,0):float3(0.75,0,0);
        col=lerp(a,b,step(0.5,frac((p.x-128)/32)));
    }
    if (Box(p,float4(128,272,288,336)) || Box(p,float4(480,272,640,336)))
    {
        float x=p.x<384?(p.x-128)/160:(p.x-480)/160;
        col=p.y < (p.x<384?304:304) ? (p.x<384?0.75:0) : (p.x<384?0:0.75);
        float y=336-32*x-(p.x<384?0:32);
        if(abs(p.y-y)<0.8) col=1;
    }
    if (Box(p,float4(288,272,480,336))) col=lerp((float3)0.35,(float3)0.72,grid);
    if (Box(p,float4(128,336,640,368)))
        col=lerp(float3(0,0.75,0),float3(0.75,0,0.75),(p.x-128)/512);
    if(p.y>=368 && p.y<400)
    {
        float zone=abs(p.x-384)/96;
        col=Stripes(p.x,zone<1?2.0:zone<2?2.5:zone<3?3.3:5);
        int number=5-min((int)zone,3);
        float origin=384+floor((p.x-384)/96)*96;
        col=lerp(col,0.75,Number(p,float2(origin+4,372),2,number,1));
    }
    if(p.y>=400 && p.y<464) col=p.x>=696?0:Bars(float2((p.x-24)/672,0))*0.75;
    if(Box(p,float4(128,464,640,496))) col=fmod(floor(p.x/32),2)?0.75:0;
    float border=CheckerBorder(p);
    return border>=0?(float3)border:col;
}
float3 TIT0249(float2 p)
{
    float ink=ReferenceGrid(p-float2(24,24),float2(96,64));
    float gray=0.83 + 0.06 * Stripes(p.y,3);
    float r=length(p-float2(384,288));
    ink=max(ink,Ring(p,float2(384,288),192,2));
    // Diagonal corner guides.
    ink=max(ink,Stroke(p,float2(216,120),float2(552,456),1));
    ink=max(ink,Stroke(p,float2(552,120),float2(216,456),1));
    if(r<189)
    {
        ink=0;
        if(Box(p,float4(280,154,488,186)) || Box(p,float4(280,390,488,422))) ink=1-Stripes(p.x,3);
        // Four tapering resolution wedges meeting at the centre.
        float2 q=p-float2(384,288);
        float2 a=abs(q);
        if(a.y>28 && a.y<112 && a.x<(a.y-16)*0.14) ink=1-Stripes(q.x,2.5);
        if(a.x>28 && a.x<112 && a.y<(a.x-16)*0.14) ink=1-Stripes(q.y,2.5);
        if(Box(p,float4(224,226,254,350)) || Box(p,float4(514,226,544,350))) ink=1-Stripes(p.y,3);
        if(Box(p,float4(272,324,496,356))) gray=floor((p.x-272)/22.4)/9;
        ink=max(ink,Ring(p,float2(384,288),10,2));
        ink=max(ink,Ring(p,float2(384,288),3,1));
        ink=max(ink,Label(p,float2(284,204),2,3));
        ink=max(ink,Number(p,float2(310,225),2,249,4));
        [unroll] for(int i=0;i<5;i++)
        {
            ink=max(ink,Number(p,float2(415,203+i*16),1.5,300+i*100,3));
            ink=max(ink,Number(p,float2(226,239+i*21),1.4,3+i,1));
            ink=max(ink,Number(p,float2(527,239+i*21),1.4,3+i,1));
        }
        [unroll] for(int j=0;j<9;j++)
        {
            ink=max(ink,Number(p,float2(281+j*23,190),1.4,j+1,1));
            ink=max(ink,Number(p,float2(281+j*23,374),1.4,j+1,1));
        }
    }
    [unroll] for(int edgeIndex=0;edgeIndex<6;edgeIndex++)
    {
        ink=max(ink,Number(p,float2(167+edgeIndex*96,30),1.6,edgeIndex+2,1));
        ink=max(ink,Number(p,float2(167+edgeIndex*96,526),1.6,edgeIndex+2,1));
    }
    // Corner circles, alternating horizontal/vertical tapered gratings.
    float2 center=float2(p.x<384?144:624,p.y<288?88:488);
    float2 q=p-center;
    float cr=length(q);
    if(cr<64)
    {
        gray=0.86; ink=Ring(p,center,62,1.5);
        bool vertical=(p.x<384)==(p.y>=288);
        float2 a=vertical?q.yx:q;
        if(abs(a.x)<42 && abs(a.y)<8+0.13*(a.x+42)) ink=1-Stripes(a.y,3);
        ink=max(ink,Number(p,center+float2(-30,25),1.5,3,1));
        ink=max(ink,Number(p,center+float2(0,25),1.5,4,1));
        ink=max(ink,Number(p,center+float2(27,25),1.5,5,1));
    }
    float2 target=float2(p.x<384?198:570,p.y<288?176:400);
    ink=max(ink,Ring(p,target,10,1)); ink=max(ink,Ring(p,target,6,1)); ink=max(ink,Ring(p,target,2,1));
    [unroll] for(int n=0;n<4;n++)
    {
        ink=max(ink,Number(p,float2(133,207+n*44),1.6,n < 2 ? 200+n*100 : 250+n*50,3));
        ink=max(ink,Number(p,float2(599,207+n*44),1.6,450+n*50,3));
        if(Box(p,float4(140,224+n*44,180,233+n*44))
            || Box(p,float4(606,224+n*44,646,233+n*44))) ink=1-Stripes(p.x,6-n);
    }
    if(Box(p,float4(290,454,478,464)) || Box(p,float4(260,478,508,490))) ink=1;
    if(abs(p.x-24)<2 || abs(p.x-744)<2 || abs(p.y-24)<2 || abs(p.y-552)<2) ink=1;
    return (float3)lerp(gray,0,saturate(ink));
}
float3 DanishClock(float2 p)
{
    float3 col=0.48;
    float2 dialCenter=float2(354,258);
    float2 grayCenter=float2(520,258);
    float2 v=p-grayCenter;
    float angle=atan2(v.y,v.x);
    float sector=floor((angle+3.14159265)/0.78539816);
    if(length(v)<176) col=0.15+sector*0.095;
    float2 q=p-dialCenter;
    float r=length(q);
    // The overlap follows the archival DR clock ident.
    if(r<176 && length(v)<176) col=0.8;
    float tickAngle=atan2(q.x,-q.y);
    float tick=floor(tickAngle*60/6.2831853+0.5);
    float major=fmod(abs(tick),5)<0.5?1:0;
    float a=tick*6.2831853/60;
    float2 direction=float2(sin(a),-cos(a));
    float ink=Stroke(q,direction*(major?157:166),direction*175,major?2.2:1.1);
    float seconds=floor(clockSeconds);
    float minuteAngle=seconds/3600*6.2831853;
    float hourAngle=seconds/43200*6.2831853;
    float secondAngle=seconds/60*6.2831853;
    ink=max(ink,Stroke(q,0,float2(sin(hourAngle),-cos(hourAngle))*104,3.3));
    ink=max(ink,Stroke(q,0,float2(sin(minuteAngle),-cos(minuteAngle))*150,2.3));
    ink=max(ink,Stroke(q,-float2(sin(secondAngle),-cos(secondAngle))*24,
        float2(sin(secondAngle),-cos(secondAngle))*166,0.8));
    ink=max(ink,1-smoothstep(4,6,r));
    col=lerp(col,0.03,ink);
    col=lerp(col,0.03,Label(p,float2(62,500),3,2));
    // DR logotype drawn with the same procedural letter strokes.
    col=lerp(col,0.9,max(Glyph((p-float2(62,448))/6,68),Glyph((p-float2(102,448))/6,82)));
    return col;
}
float3 ReceptionBars(float2 p)
{
    const float3 colors[8] = {
        float3(1,1,1), float3(1,1,0), float3(0,1,1), float3(0,1,0),
        float3(1,0,1), float3(1,0,0), float3(0,0,1), float3(0,0,0)
    };
    return colors[clamp((int)floor(p.x * 8), 0, 7)];
}
float3 BroadcastImage(float2 p)
{
    float3 col = ReceptionBars(p);
    // Small procedural station bug and local-time broadcast clock.
    float2 badge = (p - float2(0.865,0.145)) / float2(0.055,0.027);
    float ellipse = 1 - smoothstep(0.9,1.1,length(badge));
    col = lerp(col,float3(0.65,0.7,0.75),ellipse * 0.42);
    float label = Glyph((p - float2(0.837,0.132))/float2(0.003,0.0035),84)
        + Glyph((p - float2(0.860,0.132))/float2(0.003,0.0035),66)
        + Glyph((p - float2(0.883,0.132))/float2(0.003,0.0035),49);
    col = lerp(col,0.9,saturate(label) * 0.65);
    uint seconds = (uint)clockSeconds;
    float2 q = (p - float2(0.803,0.19)) / float2(0.012,0.023);
    float clockInk = Digit(float2(q.x / 0.78,q.y),(int)(seconds / 36000))
        + Digit(float2((q.x-1)/0.78,q.y),(int)(seconds / 3600 % 10))
        + Digit(float2((q.x-3)/0.78,q.y),(int)(seconds / 600 % 6))
        + Digit(float2((q.x-4)/0.78,q.y),(int)(seconds / 60 % 10))
        + Digit(float2((q.x-6)/0.78,q.y),(int)(seconds % 60 / 10))
        + Digit(float2((q.x-7)/0.78,q.y),(int)(seconds % 10));
    if((abs(q.y-0.3)<0.07 || abs(q.y-0.7)<0.07)
        && (abs(q.x-2.39)<0.09 || abs(q.x-5.39)<0.09)) clockInk=1;
    float3 image = lerp(saturate(col),0.9,saturate(clockInk) * 0.8);
    return (all(p >= 0) && all(p <= 1)) ? image : float3(0,0,0);
}
float3 GhostBroadcast(float2 p)
{
    // A delayed copy of the complete signal, including the station clock.
    float delay = 0.028 + 0.00045 * sin(time * 17.3)
        + 0.00025 * sin(time * 29.7 + 1.4);
    float3 direct = BroadcastImage(p);
    float3 echo = (BroadcastImage(p - float2(delay - 0.0007,0))
        + BroadcastImage(p - float2(delay,0))
        + BroadcastImage(p - float2(delay + 0.0007,0))) / 3;
    return direct * 0.82 + echo * 0.18;
}
// Fit the reference plane without stretching circles on wide or tall windows.
float3 TestTable(float2 uv, int kind)
{
    float2 scale=float2(max(width/height/(4.0/3.0),1),max((4.0/3.0)/(width/height),1));
    float2 p=(uv-0.5)*scale+0.5;
    bool inside = all(p >= 0) && all(p <= 1);
    float2 ref=p*float2(768,576);
    float3 col = 0;
    if(kind==4) col = BroadcastBars(p);
    else if(kind==5) col = Philips(ref);
    else if(kind==6) col = UEIT(ref);
    else if(kind==7) col = TIT0249(ref);
    else if(kind==8) col = DanishClock(ref);
    else col = GhostBroadcast(p);
    return inside ? col : float3(0,0,0);
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

    if (channel >= 4)
        col = TestTable(uv, (int)channel);
    else if (channel == 3)
        col = Bars(uv);
    else
    {
        // Fixed coarse grain scale; animation changes samples, not their size.
        float2 grainUV = uv * float2(150 * width / height,220);
        uint frame = (uint)floor(time * 60);
        uint salt = (uint)channel * 3;
        // Shift the sampling lattice every frame so it cannot read as a
        // stationary grid. Independent overlapping grains break up blocks.
        float2 offset = float2(StaticGrain(uint2(0,0),frame,17),
            StaticGrain(uint2(1,0),frame,19)) * 31;
        float coarse = SnowSample(grainUV + offset,frame,salt);
        float detail = SnowSample(grainUV * float2(1.73,1.31) + offset.yx + 13.7,frame,salt + 1);
        float n = saturate(0.5 + (coarse * 0.65 + detail * 0.35 - 0.5) * 2.1);
        float i = (channel == 0) ? 1 : (channel == 1) ? 0.6 : 0.85;

        col = float3(n,n,n) * i;

        col *= 0.97 + 0.03 * sin(uv.y * 576 * 3.14159265);
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
        int digit = clamp((int)floor(channelNumber), 0, 9) + 1;
        float on;
        if (digit == 10)
        {
            float2 p = screenUV - float2(0.85,0.12);
            p.x *= width / height;
            on = max(Digit(p / float2(0.04,0.08),1),
                Digit((p - float2(0.05,0)) / float2(0.04,0.08),0));
        }
        else on = DrawDigit(screenUV, digit);

        float3 osdColor = float3(0.0, 1.0, 0.0);

        finalColor = lerp(finalColor, osdColor, on);
    }

    if (volumeTimer > 0 || volume <= 0)
    {
        float2 p = screenUV * float2(768,576);
        const int volumeText[6] = {86,79,76,85,77,69};
        const int muteText[4] = {77,85,84,69};
        float2 q = (p - float2(128,438)) / 3;
        int index = (int)floor(q.x / 6);
        int count = volume <= 0 ? 4 : 6;
        int letter = volume <= 0 ? muteText[clamp(index,0,3)] : volumeText[clamp(index,0,5)];
        float ink = index >= 0 && index < count ? Glyph(float2(q.x - index * 6,q.y),letter) : 0;
        if (volumeTimer > 0)
        {
            ink = max(ink,Number(p,float2(596,438),3,(int)round(volume * 40),2));
            float2 bar = p - float2(128,475);
            int segment = (int)floor(bar.x / 12);
            bool inBar = bar.x >= 0 && segment < 40 && bar.y >= 0 && bar.y < 14 && frac(bar.x / 12) < 0.65;
            if (inBar)
                finalColor = lerp(finalColor,float3(0,0.28,0),0.85);
            if (inBar && segment < (int)round(volume * 40)) ink = 1;
        }
        finalColor = lerp(finalColor,float3(0.05,1,0.08),ink);
    }
    return float4(finalColor * power, 1.0);
}
