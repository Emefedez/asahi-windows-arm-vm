/* escprobe: ask viogpu3d (VIOGPU_GET_CUSTOM_RESOLUTION escape) which host size it last received,
   and print the current Windows desktop mode. Run in the user's desktop session. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
typedef LONG NTSTATUS_;
typedef struct { HDC hDc; UINT hAdapter; LUID AdapterLuid; UINT VidPnSourceId; } OPENFROMHDC;
typedef struct { UINT hAdapter; UINT hDevice; UINT Type; UINT Flags; void *pPrivateDriverData; UINT PrivateDriverDataSize; UINT hContext; } ESCAPE;
typedef struct { USHORT Type; USHORT DataLength; union { ULONG Id; struct { USHORT X, Y; } Res; BYTE pad[64]; } u; } VIOGPU_ESCAPE;
int main(void)
{
    DISPLAY_DEVICEW display = { .cb = sizeof(display) };
    for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &display, 0); i++)
        printf("display %lu: %ls id=%ls flags=%lx\n", i, display.DeviceName, display.DeviceID, display.StateFlags);
    HMODULE g = LoadLibraryA("gdi32.dll");
    NTSTATUS_ (WINAPI *open)(OPENFROMHDC *) = (void *)GetProcAddress(g, "D3DKMTOpenAdapterFromHdc");
    NTSTATUS_ (WINAPI *esc)(ESCAPE *) = (void *)GetProcAddress(g, "D3DKMTEscape");
    OPENFROMHDC o = { GetDC(NULL) };
    NTSTATUS_ st = open(&o);
    printf("OpenAdapterFromHdc: 0x%08lx\n", st); if (st) return 1;
    VIOGPU_ESCAPE d = { 0x001 /* VIOGPU_GET_CUSTOM_RESOLUTION */, 4 };
    ESCAPE e = { o.hAdapter, 0, 0 /* D3DKMT_ESCAPE_DRIVERPRIVATE */, 0, &d, sizeof d, 0 };
    st = esc(&e);
    printf("escape: 0x%08lx custom resolution %ux%u\n", st, d.u.Res.X, d.u.Res.Y);
    DEVMODEW dm = { .dmSize = sizeof dm };
    EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm);
    printf("current mode %lux%lu @ %lu Hz\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    for (DWORD i = 0, n = 0; EnumDisplaySettingsW(NULL, i, &dm); i++)
        if (dm.dmBitsPerPel == 32 && n++ < 40) printf("  mode %lux%lu@%lu\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    return 0;
}
