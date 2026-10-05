/* setres WxH: switch the primary display with ChangeDisplaySettingsEx and report the result. */
#include <windows.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    DEVMODEW dm = { .dmSize = sizeof dm };
    if (argc < 2 || sscanf(argv[1], "%lux%lu", &dm.dmPelsWidth, &dm.dmPelsHeight) != 2) return 2;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    LONG r = ChangeDisplaySettingsExW(NULL, &dm, NULL, CDS_UPDATEREGISTRY, NULL);
    printf("ChangeDisplaySettingsEx(%lux%lu) = %ld\n", dm.dmPelsWidth, dm.dmPelsHeight, r);
    EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm);
    printf("current mode %lux%lu @ %lu Hz\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    return r != DISP_CHANGE_SUCCESSFUL;
}
