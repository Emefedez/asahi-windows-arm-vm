/* d3d11probe: hardware D3D11 device on the default adapter; report adapter, feature level and a
   GPU round trip (clear + query). Mirrors what Chromium/ANGLE needs. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
int main(void)
{
    D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3 };
    ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL; D3D_FEATURE_LEVEL got = 0;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, want, 5, D3D11_SDK_VERSION, &dev, &got, &ctx);
    printf("D3D11CreateDevice(HARDWARE): 0x%08lx feature level %x.%x\n", hr, got >> 12, (got >> 8) & 0xf);
    if (FAILED(hr)) return 1;
    IDXGIDevice *xd; IDXGIAdapter *ad; DXGI_ADAPTER_DESC d;
    ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&xd); IDXGIDevice_GetAdapter(xd, &ad); IDXGIAdapter_GetDesc(ad, &d);
    printf("adapter: %ls vendor 0x%04x device 0x%04x\n", d.Description, d.VendorId, d.DeviceId);
    D3D11_TEXTURE2D_DESC td = { 1920, 1080, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE };
    ID3D11Texture2D *tex; ID3D11RenderTargetView *rtv;
    hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &tex); printf("CreateTexture2D: 0x%08lx\n", hr); if (FAILED(hr)) return 2;
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)tex, NULL, &rtv);
    D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT }; ID3D11Query *q; ID3D11Device_CreateQuery(dev, &qd, &q);
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    float c[4] = { 0.2f, 0.4f, 0.8f, 1.0f };
    for (int i = 0; i < 200; i++) {
        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, c);
        ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)q);
        BOOL done = FALSE; while (ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)q, &done, sizeof done, 0) != S_OK) ;
    }
    QueryPerformanceCounter(&t1);
    printf("200 clears+syncs: %.1f us each\nRESULT: OK\n", (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / 200);
    return 0;
}
