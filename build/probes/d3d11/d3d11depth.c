/* d3d11depth: isolates depth/stencil failures. Each case runs on a fresh device:
   argv[1] = format (d24s8|d32|d16|d32s8), argv[2] = step (create|clear|bind|draw-nodepth|draw|draw-stencil). */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
enum { W = 256, H = 256 };
static ID3DBlob *compile(const char *src, const char *target)
{ ID3DBlob *c = NULL, *e = NULL; D3DCompile(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &c, &e); return c; }
int main(int argc, char **argv)
{
    const char *fmt = argv[1], *step = argv[2];
    DXGI_FORMAT f = !strcmp(fmt, "d32") ? DXGI_FORMAT_D32_FLOAT : !strcmp(fmt, "d16") ? DXGI_FORMAT_D16_UNORM :
                    !strcmp(fmt, "d32s8") ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D24_UNORM_S8_UINT;
    ID3D11Device *dev; ID3D11DeviceContext *ctx; D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) return 9;
    ID3D11Texture2D *rt, *st, *ds; ID3D11RenderTargetView *rtv; ID3D11DepthStencilView *dsv;
    D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET };
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt); ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)rt, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; ID3D11Device_CreateTexture2D(dev, &td, NULL, &st);
    D3D11_TEXTURE2D_DESC dd = { W, H, 1, 1, f, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL };
    HRESULT hr = ID3D11Device_CreateTexture2D(dev, &dd, NULL, &ds); if (FAILED(hr)) { printf("%s %s: create tex 0x%08lx\n", fmt, step, hr); return 2; }
    hr = ID3D11Device_CreateDepthStencilView(dev, (ID3D11Resource *)ds, NULL, &dsv); if (FAILED(hr)) { printf("%s %s: dsv 0x%08lx\n", fmt, step, hr); return 2; }
    float blue[4] = { 0, 0, 1, 1 };
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, blue);
    int clear = strcmp(step, "create") != 0, bind = strcmp(step, "create") && strcmp(step, "clear");
    if (clear) ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, bind ? dsv : NULL);
    if (!strncmp(step, "draw", 4)) {
        ID3DBlob *bv = compile("float4 main(uint id : SV_VertexID) : SV_Position { float2 t[3] = { float2(-1,1), float2(0.2,1), float2(-1,-0.2) }; return float4(t[id], 0.5, 1); }", "vs_4_0");
        ID3DBlob *bp = compile("float4 main() : SV_Target { return float4(0,1,0,1); }", "ps_4_0");
        ID3D11VertexShader *vs; ID3D11PixelShader *ps;
        ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bv), ID3D10Blob_GetBufferSize(bv), NULL, &vs);
        ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(bp), ID3D10Blob_GetBufferSize(bp), NULL, &ps);
        D3D11_DEPTH_STENCIL_DESC dsd = { !strcmp(step, "draw-nodepth") ? FALSE : TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS,
            !strcmp(step, "draw-stencil"), 0xff, 0xff,
            { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR, D3D11_COMPARISON_ALWAYS },
            { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR, D3D11_COMPARISON_ALWAYS } };
        ID3D11DepthStencilState *dss; ID3D11Device_CreateDepthStencilState(dev, &dsd, &dss);
        ID3D11DeviceContext_OMSetDepthStencilState(ctx, dss, 0);
        D3D11_VIEWPORT vp = { 0, 0, W, H, 0, 1 }; ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)rt);
    D3D11_MAPPED_SUBRESOURCE m;
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) { printf("%-6s %-13s DEVICE REMOVED 0x%08lx\n", fmt, step, ID3D11Device_GetDeviceRemovedReason(dev)); return 1; }
    unsigned in = *(unsigned *)((char *)m.pData + 40 * m.RowPitch + 160);
    printf("%-6s %-13s ok in=0x%08x\n", fmt, step, in);
    return 0;
}
