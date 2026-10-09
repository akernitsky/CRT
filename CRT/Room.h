#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cstring>

// Self-contained procedural scene. Positions are in metres, with the TV facing -Z.
// The broadcast and shadow textures remain GPU-local; no readback is needed.
class Room
{
    using V3 = DirectX::XMFLOAT3;
    struct Vertex { V3 position, normal, color; DirectX::XMFLOAT2 uv; float material; };
    struct Constants {
        DirectX::XMMATRIX viewProjection, lightProjection;
        V3 eye; float power;
        float tvPower; float padding[3];
    };
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices, constants;
    ComPtr<ID3D11RenderTargetView> broadcastTarget;
    ComPtr<ID3D11ShaderResourceView> broadcastView, shadowView;
    ComPtr<ID3D11DepthStencilView> depth, shadowDepth;
    ComPtr<ID3D11SamplerState> sampler, shadowSampler;
    ComPtr<ID3D11RasterizerState> raster, shadowRaster;
    ComPtr<ID3D11DepthStencilState> depthState;
    UINT vertexCount = 0, shadowVertexCount = 0, width = 0, height = 0;
    bool shadowReady = false;
    std::vector<Vertex> mesh;

    void Triangle(Vertex a, Vertex b, Vertex c) { mesh.insert(mesh.end(), {a,b,c}); }

    void Box(V3 center, V3 half, V3 color, float material = 0, float radius = 0)
    {
        // Project a subdivided cube onto a rounded box for smooth cabinet edges.
        radius = (std::min)({radius,half.x,half.y,half.z});
        const int steps = radius > 0 ? 8 : 1;
        const float h[3] = {half.x,half.y,half.z};
        const float ctr[3] = {center.x,center.y,center.z};
        for (int axis=0; axis<3; ++axis) for (int sign : {-1,1})
        {
            auto vertex = [&](int x,int y) {
                float p[3] = {};
                auto coordinate = [&](int index, float extent) {
                    if (radius <= 0) return (2.0f*index/steps-1)*extent;
                    const float core=extent-radius;
                    const float samples[9]={-extent,-core-radius*.7071f,-core,-core*.5f,0,
                        core*.5f,core,core+radius*.7071f,extent};
                    return samples[index];
                };
                p[axis] = sign*h[axis];
                p[(axis+1)%3] = coordinate(x,h[(axis+1)%3]);
                p[(axis+2)%3] = coordinate(y,h[(axis+2)%3]);
                float n[3] = {};
                if (radius > 0) {
                    float q[3], length=0;
                    for(int k=0;k<3;++k) {
                        q[k]=std::clamp(p[k],-h[k]+radius,h[k]-radius);
                        n[k]=p[k]-q[k]; length+=n[k]*n[k];
                    }
                    length=std::sqrt(length);
                    for(int k=0;k<3;++k) { n[k]/=length; p[k]=q[k]+radius*n[k]; }
                } else n[axis]=static_cast<float>(sign);
                return Vertex{{p[0]+ctr[0],p[1]+ctr[1],p[2]+ctr[2]},
                    {n[0],n[1],n[2]},color,{0,0},material};
            };
            for(int y=0;y<steps;++y) for(int x=0;x<steps;++x) {
                auto a=vertex(x,y), b=vertex(x+1,y), c=vertex(x+1,y+1), d=vertex(x,y+1);
                Triangle(a,b,c); Triangle(a,c,d);
            }
        }
    }

    void Cylinder(V3 center, float bottomRadius, float topRadius, float halfHeight, V3 color, float material=0)
    {
        for(int i=0;i<48;++i) {
            float a=i*DirectX::XM_2PI/48, b=(i+1)*DirectX::XM_2PI/48;
            auto edge=[&](float angle,float y,float r) {
                V3 n{std::cos(angle),(bottomRadius-topRadius)/(2*halfHeight),std::sin(angle)};
                DirectX::XMStoreFloat3(&n,DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&n)));
                return Vertex{{center.x+r*std::cos(angle),center.y+y,center.z+r*std::sin(angle)},n,color,{0,0},material};
            };
            auto v0=edge(a,-halfHeight,bottomRadius),v1=edge(b,-halfHeight,bottomRadius);
            auto v2=edge(b,halfHeight,topRadius),v3=edge(a,halfHeight,topRadius);
            Triangle(v0,v1,v2); Triangle(v0,v2,v3);
            v2.normal=v3.normal={0,1,0};
            Triangle({{center.x,center.y+halfHeight,center.z},{0,1,0},color,{0,0},material},v2,v3);
        }
    }

    void BuildScene()
    {
        const V3 wood{0.30f,0.14f,0.065f}, dark{0.035f,0.032f,0.029f}, brass{0.45f,0.31f,0.13f};
        Box({0,-.09f,0},{5,.09f,6},{.36f,.23f,.13f},2);
        Box({0,2.3f,3.3f},{5,2.3f,.08f},{.28f,.36f,.32f},4);
        Box({-4,2.3f,0},{.08f,2.3f,3.3f},{.34f,.41f,.36f},4);
        Box({0,.10f,3.18f},{4,.10f,.035f},wood,2);
        Box({-3.9f,.10f,0},{.035f,.10f,3.2f},wood,2);
        // Rust wool rug, with a woven border.
        Box({0,.012f,-.5f},{2.35f,.012f,1.75f},{.34f,.14f,.09f},3);
        Box({0,.025f,-.5f},{2.17f,.006f,1.57f},{.49f,.27f,.16f},3);
        Box({0,.032f,-.5f},{1.98f,.004f,1.38f},{.29f,.12f,.075f},3);
        // Open wooden TV stand, shelf, books and four feet.
        Box({0,.70f,1},{1.22f,.08f,.65f},wood,2,.04f);
        Box({0,.25f,1},{1.18f,.05f,.60f},wood,2);
        for(float x : {-1.08f,1.08f}) {
            Box({x,.47f,1},{.055f,.22f,.60f},wood,2);
            for(float z : {.5f,1.5f}) Box({x,.12f,z},{.055f,.12f,.055f},dark);
        }
        for(int i=0;i<7;++i) Box({-.8f+i*.115f,.40f,.66f},{.045f,.10f+(i%3)*.025f,.22f},
            { .18f+(i%3)*.10f,.20f+(i%2)*.08f,.16f },3);
        Box({.55f,.36f,.65f},{.30f,.06f,.23f},dark,0,.02f);
        // Cabinet, front bezel, curved glass and controls.
        Box({0,1.50f,1.09f},{1.03f,.72f,.53f},wood,2,.12f);
        Box({0,1.50f,.525f},{.96f,.655f,.035f},dark,0,.035f);
        Box({-.13f,1.52f,.495f},{.79f,.602f,.020f},{.09f,.085f,.075f},0,.020f);
        const int nx=40, ny=30;
        auto screen=[](int x,int y) {
            float u=x/40.0f,v=y/30.0f, sx=(u-.5f)*1.47f,sy=(.5f-v)*1.1025f;
            float z=.355f+.055f*(sx*sx/.54f+sy*sy/.304f);
            V3 n{.11f*sx/.54f,.11f*sy/.304f,-1};
            return Vertex{{sx-.13f,sy+1.52f,z},n,{1,1,1},{u,v},1};
        };
        for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
            Triangle(screen(x,y),screen(x+1,y),screen(x+1,y+1));
            Triangle(screen(x,y),screen(x+1,y+1),screen(x,y+1));
        }
        for(int i=0;i<14;++i) Box({.80f,1.28f+i*.035f,.432f},{.07f,.006f,.012f},{.14f,.13f,.11f});
        Box({.8f,1.92f,.40f},{.064f,.064f,.035f},brass,0,.025f);
        Box({.8f,1.77f,.40f},{.045f,.045f,.035f},brass,0,.02f);
        Box({.79f,1.03f,.395f},{.012f,.012f,.008f},{.3f,1,.18f},6);
        for(float x : {-.72f,.72f}) Box({x,.80f,1},{.12f,.035f,.32f},dark);
        // Sofa at the left of the TV, with separate cushions and piping-like edges.
        V3 fabric{.19f,.30f,.29f};
        Box({-2.7f,.49f,.45f},{.73f,.29f,1.20f},fabric,3,.15f);
        Box({-3.28f,1.00f,.45f},{.20f,.62f,1.20f},fabric,3,.15f);
        for(float z : {-.55f,1.45f}) Box({-2.7f,.87f,z},{.72f,.30f,.18f},fabric,3,.12f);
        for(float z : {-.05f,.89f}) Box({-2.6f,.80f,z},{.52f,.12f,.43f},{.24f,.36f,.34f},3,.10f);
        Box({-3.03f,1.11f,.90f},{.15f,.32f,.33f},{.65f,.38f,.16f},3,.10f);
        // Side table and warm floor lamp.
        Cylinder({2.05f,.70f,1.1f},.48f,.48f,.045f,wood,2);
        for(float x : {1.8f,2.3f}) for(float z : {.85f,1.35f}) Box({x,.34f,z},{.035f,.34f,.035f},wood,2);
        Cylinder({2.05f,.81f,1.1f},.17f,.17f,.06f,brass);
        Cylinder({2.05f,1.43f,1.1f},.024f,.024f,.59f,brass);
        Cylinder({2.05f,2.03f,1.1f},.45f,.28f,.31f,{.84f,.62f,.34f},5);
        // Framed abstract print on the back wall, all actual scene geometry.
        Box({-.65f,2.78f,3.16f},{.83f,.59f,.045f},wood,2);
        Box({-.65f,2.78f,3.10f},{.76f,.52f,.018f},{.76f,.70f,.53f});
        Box({-.91f,2.80f,3.075f},{.23f,.38f,.005f},{.32f,.44f,.40f});
        Box({-.35f,2.63f,3.06f},{.27f,.21f,.006f},{.65f,.28f,.14f});
        // The ceiling closes the view on tall windows. The soft key light models
        // room fill, so the ceiling receives it without occluding that light.
        shadowVertexCount=static_cast<UINT>(mesh.size());
        Box({0,4.6f,-1},{5,.08f,4.3f},{.46f,.46f,.40f},4);
    }

public:
    static constexpr UINT BroadcastWidth=1024, BroadcastHeight=768;
    ID3D11RenderTargetView* BroadcastTarget() const { return broadcastTarget.Get(); }

    void Init(ID3D11Device* device)
    {
        static const char shader[] = R"hlsl(
cbuffer Scene : register(b0) {
    row_major float4x4 viewProjection;
    row_major float4x4 lightProjection;
    float3 eye; float power;
    float tvPower; float3 padding;
};
struct Input { float3 p:POSITION; float3 n:NORMAL; float3 color:COLOR; float2 uv:TEXCOORD0; float material:TEXCOORD1; };
struct Output { float4 p:SV_POSITION; float3 world:POSITION0; float3 n:NORMAL; float3 color:COLOR; float2 uv:TEXCOORD0; float material:TEXCOORD1; float4 shadow:TEXCOORD2; };
Output VS(Input i) {
    Output o; o.p=mul(float4(i.p,1),viewProjection); o.world=i.p; o.n=i.n;
    o.color=i.color; o.uv=i.uv; o.material=i.material;
    o.shadow=mul(float4(i.p,1),lightProjection); return o;
}
Texture2D broadcast:register(t0); Texture2D shadowMap:register(t1);
SamplerState linearSampler:register(s0); SamplerComparisonState shadowSampler:register(s1);
float4 PS(Output i):SV_TARGET {
    float3 n=normalize(i.n), v=normalize(eye-i.world);
    float3 base=i.color;
    if(i.material>1.5 && i.material<2.5) {
        float grain=sin(i.world.x*14+sin(i.world.z*3)*2+sin(i.world.x*43+i.world.z)*.22);
        base*=.88+.12*grain;
        if(i.world.y<.01) base*= .78+.22*smoothstep(0,.025,abs(frac(i.world.x*1.6)-.5));
    }
    if(i.material>2.5 && i.material<3.5)
        base*=.94+.06*sin(i.world.x*230)*sin((i.world.y+i.world.z)*230);
    float3 l=normalize(float3(-3,6,-4));
    float3 sc=i.shadow.xyz/i.shadow.w;
    float2 suv=sc.xy*float2(.5,-.5)+.5;
    float visibility=0;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x)
        visibility+=shadowMap.SampleCmpLevelZero(shadowSampler,suv+float2(x,y)/1536,sc.z-.0015)/9;
    float3 lamp=float3(2.05,1.99,1.1)-i.world;
    float lampLight=saturate(dot(n,normalize(lamp)))/(1+dot(lamp,lamp)*1.3);
    float3 illumination=float3(.30,.32,.34)+float3(.75,.70,.59)*saturate(dot(n,l))*visibility;
    illumination+=float3(1,.56,.22)*lampLight*1.9;
    float3 tv=float3(-.13,1.52,.30)-i.world;
    illumination+=float3(.18,.28,.38)*saturate(dot(n,normalize(tv)))*tvPower/(1+dot(tv,tv)*3);
    float3 col=base*illumination;
    if(i.material>.5 && i.material<1.5) {
        float2 edge=abs(i.uv-.5)*2;
        float mask=1-smoothstep(.94,1,pow(pow(edge.x,12)+pow(edge.y,12),1.0/12));
        float3 signal=broadcast.Sample(linearSampler,i.uv).rgb;
        float highlight=pow(saturate(dot(reflect(-normalize(float3(2,4,-3)),n),v)),60)*.12;
        col=signal*mask+float3(.035,.045,.055)*(1-mask)+highlight;
    }
    if(i.material>4.5 && i.material<5.5) col=base*(.85+.3*saturate(n.y));
    if(i.material>5.5) col=base*(.02+.98*tvPower);
    // Gentle display gamma; broadcasts already carry their intended display response.
    if(i.material<.5 || i.material>1.5) col=pow(max(col,0),1/1.35);
    return float4(col*power,1);
}
)hlsl";
        ComPtr<ID3DBlob> vsCode,psCode,errors;
        auto compile=[&](const char* entry,const char* target,ID3DBlob** result) {
            HRESULT hr=D3DCompile(shader,sizeof(shader)-1,"Room",nullptr,nullptr,entry,target,
                D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,result,&errors);
            if(FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Room shader compilation failed");
        };
        compile("VS","vs_5_0",&vsCode); compile("PS","ps_5_0",&psCode);
        CheckHR(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs),"Room VS failed");
        CheckHR(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps),"Room PS failed");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0}};
        CheckHR(device->CreateInputLayout(elements,5,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),&layout),"Room layout failed");
        BuildScene(); vertexCount=static_cast<UINT>(mesh.size());
        D3D11_BUFFER_DESC bd={}; bd.ByteWidth=vertexCount*sizeof(Vertex); bd.Usage=D3D11_USAGE_IMMUTABLE; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial={mesh.data(),0,0};
        CheckHR(device->CreateBuffer(&bd,&initial,&vertices),"Room mesh failed");
        mesh.clear(); mesh.shrink_to_fit();
        bd.ByteWidth=sizeof(Constants); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        CheckHR(device->CreateBuffer(&bd,nullptr,&constants),"Room constants failed");
        D3D11_TEXTURE2D_DESC td={}; td.Width=BroadcastWidth; td.Height=BroadcastHeight; td.MipLevels=1; td.ArraySize=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        CheckHR(device->CreateTexture2D(&td,nullptr,&texture),"Broadcast texture failed");
        CheckHR(device->CreateRenderTargetView(texture.Get(),nullptr,&broadcastTarget),"Broadcast target failed");
        CheckHR(device->CreateShaderResourceView(texture.Get(),nullptr,&broadcastView),"Broadcast view failed");
        td.Width=td.Height=1536; td.Format=DXGI_FORMAT_R32_TYPELESS; td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        texture.Reset(); CheckHR(device->CreateTexture2D(&td,nullptr,&texture),"Shadow texture failed");
        D3D11_DEPTH_STENCIL_VIEW_DESC dd={}; dd.Format=DXGI_FORMAT_D32_FLOAT; dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        CheckHR(device->CreateDepthStencilView(texture.Get(),&dd,&shadowDepth),"Shadow depth failed");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd={}; sd.Format=DXGI_FORMAT_R32_FLOAT; sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels=1;
        CheckHR(device->CreateShaderResourceView(texture.Get(),&sd,&shadowView),"Shadow view failed");
        D3D11_SAMPLER_DESC samp={}; samp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU=samp.AddressV=samp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; samp.MaxLOD=D3D11_FLOAT32_MAX;
        CheckHR(device->CreateSamplerState(&samp,&sampler),"Room sampler failed");
        samp.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; samp.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;
        samp.AddressU=samp.AddressV=D3D11_TEXTURE_ADDRESS_BORDER; for(float& c:samp.BorderColor)c=1;
        CheckHR(device->CreateSamplerState(&samp,&shadowSampler),"Shadow sampler failed");
        D3D11_RASTERIZER_DESC rd={}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        CheckHR(device->CreateRasterizerState(&rd,&raster),"Room raster failed");
        rd.DepthBias=100; rd.SlopeScaledDepthBias=1.5f;
        CheckHR(device->CreateRasterizerState(&rd,&shadowRaster),"Shadow raster failed");
        D3D11_DEPTH_STENCIL_DESC ds={}; ds.DepthEnable=TRUE; ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc=D3D11_COMPARISON_LESS;
        CheckHR(device->CreateDepthStencilState(&ds,&depthState),"Room depth state failed");
    }

    void Draw(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* target, UINT w, UINT h, float power, float tvPower)
    {
        using namespace DirectX;
        if(w!=width || h!=height) {
            depth.Reset();
            D3D11_TEXTURE2D_DESC td={}; td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
            td.Format=DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count=1; td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
            ComPtr<ID3D11Texture2D> texture;
            CheckHR(device->CreateTexture2D(&td,nullptr,&texture),"Room depth texture failed");
            CheckHR(device->CreateDepthStencilView(texture.Get(),nullptr,&depth),"Room depth view failed");
            width=w; height=h;
        }
        UINT stride=sizeof(Vertex),offset=0;
        ctx->IASetInputLayout(layout.Get()); ctx->IASetVertexBuffers(0,1,vertices.GetAddressOf(),&stride,&offset);
        ctx->VSSetShader(vs.Get(),nullptr,0); ctx->VSSetConstantBuffers(0,1,constants.GetAddressOf());
        ctx->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        ctx->OMSetDepthStencilState(depthState.Get(),0);
        const auto light=XMMatrixLookAtLH(XMVectorSet(-3,6,-4,1),XMVectorSet(0,0,0,1),XMVectorSet(0,1,0,0))*XMMatrixOrthographicLH(12,12,.1f,20);
        Constants cb{}; cb.viewProjection=light; cb.lightProjection=light; cb.power=power;
        cb.tvPower=tvPower;
        D3D11_VIEWPORT vp={0,0,1536,1536,0,1};
        if (!shadowReady) {
            ctx->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);
            ctx->RSSetViewports(1,&vp);
            ctx->RSSetState(shadowRaster.Get()); ctx->PSSetShader(nullptr,nullptr,0);
            ctx->OMSetRenderTargets(0,nullptr,shadowDepth.Get()); ctx->ClearDepthStencilView(shadowDepth.Get(),D3D11_CLEAR_DEPTH,1,0);
            ctx->Draw(shadowVertexCount,0);
            shadowReady = true;
        }
        // Widen the vertical field on narrow windows to keep the room in frame.
        const float aspect=static_cast<float>(w)/h;
        const float fov=(std::min)(1.3f,2*std::atan(std::tan(.57f/2)*(std::max)(1.0f,1.6f/aspect)));
        cb.eye={2.65f,2.65f,-6.1f};
        cb.viewProjection=XMMatrixLookAtLH(XMLoadFloat3(&cb.eye),XMVectorSet(-.25f,1.30f,.8f,1),XMVectorSet(0,1,0,0))*XMMatrixPerspectiveFovLH(fov,aspect,.1f,30);
        ctx->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);
        vp.Width=static_cast<float>(w); vp.Height=static_cast<float>(h); ctx->RSSetViewports(1,&vp); ctx->RSSetState(raster.Get());
        ctx->OMSetRenderTargets(1,&target,depth.Get());
        const float background[4]={0,0,0,1}; ctx->ClearRenderTargetView(target,background); ctx->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);
        ID3D11ShaderResourceView* views[]={broadcastView.Get(),shadowView.Get()}; ctx->PSSetShaderResources(0,2,views);
        ID3D11SamplerState* samplers[]={sampler.Get(),shadowSampler.Get()}; ctx->PSSetSamplers(0,2,samplers);
        ctx->PSSetShader(ps.Get(),nullptr,0); ctx->Draw(vertexCount,0);
        ID3D11ShaderResourceView* empty[2]={}; ctx->PSSetShaderResources(0,2,empty);
        ctx->OMSetRenderTargets(1,&target,nullptr);
        ctx->OMSetDepthStencilState(nullptr,0); ctx->RSSetState(nullptr);
        ctx->IASetInputLayout(nullptr);
        ID3D11Buffer* noBuffer=nullptr; ctx->IASetVertexBuffers(0,1,&noBuffer,&stride,&offset);
    }
};
