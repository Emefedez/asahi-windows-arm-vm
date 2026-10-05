/* d3d11blend: dual-source blending (ClearType-style SRC1_COLOR / INV_SRC1_COLOR). */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
static ID3DBlob *compile(const char *src, const char *t)
{ ID3DBlob *c = NULL, *e = NULL; if (FAILED(D3DCompile(src, strlen(src), NULL, NULL, NULL, "main", t, 0, 0, &c, &e))) { printf("%s\n", (char *)ID3D10Blob_GetBufferPointer(e)); exit(3); } return c; }
int main(void)
{
    ID3D11Device *dev; ID3D11DeviceContext *ctx; D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) return 9;
    ID3D11Texture2D *rt, *st; ID3D11RenderTargetView *rtv;
    D3D11_TEXTURE2D_DESC td = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET };
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt); ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)rt, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; ID3D11Device_CreateTexture2D(dev, &td, NULL, &st);
    ID3DBlob *bv = compile("float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2,-2) + float2(-1,1), 0.5, 1); }", "vs_4_0");
    ID3DBlob *bp = compile("void main(out float4 c0 : SV_Target0, out float4 c1 : SV_Target1) { c0 = float4(1,1,1,1); c1 = float4(0,1,0,1); }", "ps_4_0");
    ID3D11VertexShader *vs; ID3D11PixelShader *ps;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bv), ID3D10Blob_GetBufferSize(bv), NULL, &vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(bp), ID3D10Blob_GetBufferSize(bp), NULL, &ps);
    D3D11_BLEND_DESC bd = { 0 };
    bd.RenderTarget[0] = (D3D11_RENDER_TARGET_BLEND_DESC){ TRUE, D3D11_BLEND_SRC1_COLOR, D3D11_BLEND_INV_SRC1_COLOR, D3D11_BLEND_OP_ADD,
                                                          D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, 0xf };
    ID3D11BlendState *bs; HRESULT hr = ID3D11Device_CreateBlendState(dev, &bd, &bs); printf("CreateBlendState 0x%08lx\n", hr);
    float blue[4] = { 0, 0, 1, 1 }; ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, blue);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL); ID3D11DeviceContext_OMSetBlendState(ctx, bs, NULL, 0xffffffff);
    D3D11_VIEWPORT vp = { 0, 0, 64, 64, 0, 1 }; ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)rt);
    D3D11_MAPPED_SUBRESOURCE m; ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m);
    unsigned v = *(unsigned *)((char *)m.pData + 8 * m.RowPitch + 8 * 4);
    printf("pixel 0x%08x %s\n", v, v == 0xffffff00 ? "OK (cyan: dual-source blend works)" : "WRONG (want 0xffffff00 cyan)");
    return v != 0xffffff00;
}
