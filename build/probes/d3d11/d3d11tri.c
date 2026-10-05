/* d3d11tri: offscreen D3D11 draw tests with readback (HW device on the default adapter).
   Each test clears to blue and draws a full-viewport-ish triangle that should paint the centre. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

static ID3D11Device *dev; static ID3D11DeviceContext *ctx;
static ID3D11Texture2D *rt, *staging, *ds; static ID3D11RenderTargetView *rtv; static ID3D11DepthStencilView *dsv;
enum { W = 256, H = 256 };

static ID3DBlob *compile(const char *src, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = D3DCompile(src, strlen(src), NULL, NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr)) { printf("  compile %s failed: %s\n", entry, err ? (char *)ID3D10Blob_GetBufferPointer(err) : "?"); return NULL; }
    return code;
}

static unsigned centre(void)
{
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)rt);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) return 0xdeadbeef;
    unsigned v = *(unsigned *)((char *)m.pData + (H / 2) * m.RowPitch + (W / 2) * 4);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    return v;
}

static void begin(int depth)
{
    float blue[4] = { 0, 0, 1, 1 };
    ID3D11DeviceContext_ClearState(ctx);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, blue);
    if (depth) ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, depth ? dsv : NULL);
    D3D11_VIEWPORT vp = { 0, 0, W, H, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void report(const char *name)
{
    unsigned v = centre();
    printf("%-34s centre=0x%08x %s\n", name, v, v == 0xff0000ff ? "(clear colour only: DRAW LOST)" : v == 0xff00ff00 ? "OK (green)" : "(unexpected)");
}

static const char *vs_id =
    "float4 main(uint id : SV_VertexID) : SV_Position {"
    " float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, 0.5, 1); }";
static const char *ps_green = "float4 main() : SV_Target { return float4(0,1,0,1); }";
static const char *vs_vb = "float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1); }";
static const char *vs_cb =
    "cbuffer C : register(b0) { float4x4 m; float4 col; };"
    "struct O { float4 p : SV_Position; float4 c : COLOR; };"
    "O main(float3 p : POSITION) { O o; o.p = mul(m, float4(p,1)); o.c = col; return o; }";
static const char *ps_col = "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }";
static const char *ps_tex =
    "Texture2D t : register(t0); SamplerState s : register(s0);"
    "float4 main(float4 p : SV_Position) : SV_Target { return t.Sample(s, p.xy / 256); }";

int main(void)
{
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
    printf("device 0x%08lx FL %x.%x\n", hr, fl >> 12, (fl >> 8) & 15); if (FAILED(hr)) return 1;
    D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET };
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt);
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)rt, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging);
    D3D11_TEXTURE2D_DESC dd = { W, H, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL };
    ID3D11Device_CreateTexture2D(dev, &dd, NULL, &ds);
    ID3D11Device_CreateDepthStencilView(dev, (ID3D11Resource *)ds, NULL, &dsv);

    ID3DBlob *b;
    ID3D11VertexShader *vsid, *vsvb, *vscb; ID3D11PixelShader *psg, *psc, *pst;
    b = compile(vs_id, "main", "vs_4_0"); ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &vsid);
    b = compile(ps_green, "main", "ps_4_0"); ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &psg);
    b = compile(ps_col, "main", "ps_4_0"); ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &psc);
    b = compile(ps_tex, "main", "ps_4_0"); ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &pst);
    ID3DBlob *bvb = compile(vs_vb, "main", "vs_4_0"); ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bvb), ID3D10Blob_GetBufferSize(bvb), NULL, &vsvb);
    ID3DBlob *bcb = compile(vs_cb, "main", "vs_4_0"); ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bcb), ID3D10Blob_GetBufferSize(bcb), NULL, &vscb);

    D3D11_INPUT_ELEMENT_DESC ied = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    ID3D11InputLayout *il; ID3D11Device_CreateInputLayout(dev, &ied, 1, ID3D10Blob_GetBufferPointer(bvb), ID3D10Blob_GetBufferSize(bvb), &il);
    float verts[] = { -1, -1, 0.5f, -1, 3, 0.5f, 3, -1, 0.5f };
    D3D11_BUFFER_DESC bd = { sizeof verts, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA sd = { verts };
    ID3D11Buffer *vb; ID3D11Device_CreateBuffer(dev, &bd, &sd, &vb);
    ID3D11Buffer *vbdyn; D3D11_BUFFER_DESC bdd = { sizeof verts, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
    ID3D11Device_CreateBuffer(dev, &bdd, NULL, &vbdyn);
    unsigned short idx[] = { 0, 1, 2 };
    D3D11_BUFFER_DESC ibd = { sizeof idx, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER }; D3D11_SUBRESOURCE_DATA isd = { idx };
    ID3D11Buffer *ib; ID3D11Device_CreateBuffer(dev, &ibd, &isd, &ib);
    float cbdata[20] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 0,1,0,1 };
    D3D11_BUFFER_DESC cbd = { sizeof cbdata, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *cb; ID3D11Device_CreateBuffer(dev, &cbd, NULL, &cb);
    unsigned texel[16]; for (int i = 0; i < 16; i++) texel[i] = 0xff00ff00;
    D3D11_TEXTURE2D_DESC ttd = { 4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE };
    D3D11_SUBRESOURCE_DATA tsd = { texel, 16 };
    ID3D11Texture2D *tex; ID3D11Device_CreateTexture2D(dev, &ttd, &tsd, &tex);
    ID3D11ShaderResourceView *srv; ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)tex, NULL, &srv);
    D3D11_SAMPLER_DESC smp = { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0, 1, D3D11_COMPARISON_NEVER, {0}, 0, D3D11_FLOAT32_MAX };
    ID3D11SamplerState *ss; ID3D11Device_CreateSamplerState(dev, &smp, &ss);
    UINT stride = 12, off = 0;

    begin(0); report("clear only");
    begin(0); ID3D11DeviceContext_VSSetShader(ctx, vsid, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, psg, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); report("SV_VertexID, no buffers");
    begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsvb, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, psg, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); report("static vertex buffer");
    begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &off);
    ID3D11DeviceContext_IASetIndexBuffer(ctx, ib, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_VSSetShader(ctx, vsvb, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, psg, NULL, 0);
    ID3D11DeviceContext_DrawIndexed(ctx, 3, 0, 0); report("indexed draw (u16)");
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)vbdyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, verts, sizeof verts); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)vbdyn, 0);
    begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbdyn, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsvb, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, psg, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); report("dynamic vertex buffer (discard)");
    ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, cbdata, sizeof cbdata); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
    begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vscb, NULL, 0); ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cb);
    ID3D11DeviceContext_PSSetShader(ctx, psc, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); report("constant buffer + varying");
    begin(1); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsvb, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, psg, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); report("with depth buffer (default DS)");
    begin(0); ID3D11DeviceContext_VSSetShader(ctx, vsid, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pst, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &srv); ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &ss);
    ID3D11DeviceContext_Draw(ctx, 3, 0); report("texture sample");
    return 0;
}
