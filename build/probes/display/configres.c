#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    UINT32 np=0,nm=0;
    GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&np,&nm);
    DISPLAYCONFIG_PATH_INFO *p=calloc(np,sizeof(*p));
    DISPLAYCONFIG_MODE_INFO *m=calloc(nm,sizeof(*m));
    LONG r=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&np,p,&nm,m,NULL);
    printf("query=%ld paths=%u modes=%u\n",r,np,nm);
    if(r || np != 1) return 1;
    printf("src idx %u target idx %u flags %x refresh %u/%u scanline %u\n",p[0].sourceInfo.modeInfoIdx,p[0].targetInfo.modeInfoIdx,p[0].flags,p[0].targetInfo.refreshRate.Numerator,p[0].targetInfo.refreshRate.Denominator,p[0].targetInfo.scanLineOrdering);
    DISPLAYCONFIG_MODE_INFO source=m[p[0].sourceInfo.modeInfoIdx];
    source.sourceMode.width=atoi(argv[1]); source.sourceMode.height=atoi(argv[2]);
    p[0].sourceInfo.modeInfoIdx=0;
    p[0].targetInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    p[0].flags=DISPLAYCONFIG_PATH_ACTIVE;
    if(argc>3) {
      p[0].targetInfo.refreshRate.Numerator=0;
      p[0].targetInfo.refreshRate.Denominator=0;
      p[0].targetInfo.scanLineOrdering=DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
    }
    r=SetDisplayConfig(1,p,1,&source,SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES|SDC_FORCE_MODE_ENUMERATION|SDC_SAVE_TO_DATABASE);
    printf("set=%ld\n",r);
    DEVMODEW dm={.dmSize=sizeof(dm)};
    EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&dm);
    printf("current %lux%lu @ %lu\n",dm.dmPelsWidth,dm.dmPelsHeight,dm.dmDisplayFrequency);
    return r!=0;
}
