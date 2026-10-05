/* d3d12probe: create a D3D12 device on the first adapter, report feature level / shader model,
   record + execute an (empty) command list and wait on a fence. Uses whatever d3d12.dll/dxgi.dll
   the loader finds (vkd3d-proton + DXVK when placed next to the exe by dx-enable.ps1). */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>

int main(void)
{
    IDXGIFactory4 *fac = NULL; IDXGIAdapter1 *ad = NULL; ID3D12Device *dev = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&fac);
    printf("CreateDXGIFactory1: 0x%08lx\n", hr); if (FAILED(hr)) return 2;
    for (UINT i = 0; IDXGIFactory4_EnumAdapters1(fac, i, &ad) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d; IDXGIAdapter1_GetDesc1(ad, &d);
        printf("adapter%u: %ls | vendor 0x%04x device 0x%04x | dedicated %llu MiB shared %llu MiB | flags 0x%x\n", i,
               d.Description, d.VendorId, d.DeviceId, (unsigned long long)d.DedicatedVideoMemory >> 20,
               (unsigned long long)d.SharedSystemMemory >> 20, d.Flags);
        if (!dev) hr = D3D12CreateDevice((IUnknown *)ad, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
        IDXGIAdapter1_Release(ad);
    }
    printf("D3D12CreateDevice: 0x%08lx\n", hr); if (!dev) return 3;
    printf("latency candidate loaded: %s\n", GetModuleHandleA("vulkan_latency.dll") ? "yes" : "no");

    D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl = { 5, want, 0 };
    ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof fl);
    printf("max feature level: %x_%x\n", fl.MaxSupportedFeatureLevel >> 12, (fl.MaxSupportedFeatureLevel >> 8) & 0xf);
    D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_7 };
    ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm);
    printf("max shader model: %x.%x\n", sm.HighestShaderModel >> 4, sm.HighestShaderModel & 0xf);
    D3D12_FEATURE_DATA_D3D12_OPTIONS o = {0};
    ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof o);
    printf("resource binding tier %d, tiled resources tier %d, ROVs %d\n", o.ResourceBindingTier, o.TiledResourcesTier, o.ROVsSupported);

    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    ID3D12CommandQueue *q; ID3D12CommandAllocator *ca; ID3D12GraphicsCommandList *cl; ID3D12Fence *f;
    hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q); printf("CreateCommandQueue: 0x%08lx\n", hr);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&ca);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, NULL, &IID_ID3D12GraphicsCommandList, (void **)&cl);
    ID3D12GraphicsCommandList_Close(cl);
    ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&f);
    LARGE_INTEGER t0, t1, fr; QueryPerformanceFrequency(&fr); QueryPerformanceCounter(&t0);
    int rounds = 200;
    for (int i = 1; i <= rounds; i++) {
        ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&cl);
        ID3D12CommandQueue_Signal(q, f, i);
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        ID3D12Fence_SetEventOnCompletion(f, i, ev); WaitForSingleObject(ev, 5000); CloseHandle(ev);
    }
    QueryPerformanceCounter(&t1);
    printf("fence value %llu after %d submits; avg round trip %.1f us\n", (unsigned long long)ID3D12Fence_GetCompletedValue(f), rounds,
           (t1.QuadPart - t0.QuadPart) * 1e6 / fr.QuadPart / rounds);
    printf(ID3D12Fence_GetCompletedValue(f) == (UINT64)rounds ? "RESULT: OK\n" : "RESULT: GPU did not complete\n");
    return 0;
}
