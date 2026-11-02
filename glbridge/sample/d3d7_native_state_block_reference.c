/* Original Microsoft D3D7 RGB state-block reference, RAM tools only. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE logfile;
static void line(const char* s)
{
    DWORD n;
    WriteFile(logfile, s, lstrlenA(s), &n, 0);
    WriteFile(logfile, "\r\n", 2, &n, 0);
    FlushFileBuffers(logfile);
}
static void status(const char* s, HRESULT hr)
{
    char t[160];
    wsprintfA(t, "%s hr=%08lx", s, hr);
    line(t);
}
#define TRY(x)            \
    do {                  \
        HRESULT hr = (x); \
        status(#x, hr);   \
        if (FAILED(hr))   \
            goto done;    \
    } while (0)
static IDirectDrawSurface7 *state_textures[2];
static void state_value(unsigned type,const char *phase,const char *field,HRESULT hr,const void *data,unsigned words)
{
    char text[600],word[16];wsprintfA(text,"STATE type=%u phase=%s field=%s hr=%08lx words=",type,phase,field,hr);
    for(unsigned i=0;i<words;++i){DWORD value;CopyMemory(&value,(const BYTE*)data+i*4,4);wsprintfA(word,i?",%08lx":"%08lx",value);lstrcatA(text,word);}line(text);
}
static const DWORD state_rs[]={D3DRENDERSTATE_CULLMODE,D3DRENDERSTATE_SHADEMODE,D3DRENDERSTATE_TEXTUREFACTOR,D3DRENDERSTATE_SPECULARENABLE,D3DRENDERSTATE_LIGHTING,D3DRENDERSTATE_FOGENABLE};
static const char *state_rs_names[]={"RS_CULL","RS_SHADE","RS_FACTOR","RS_SPECULAR","RS_LIGHTING","RS_FOG"};
static const DWORD state_rs_values[2][6]={{D3DCULL_NONE,D3DSHADE_FLAT,0xff102030,0,0,0},{D3DCULL_CW,D3DSHADE_GOURAUD,0xff405060,1,1,1}};
static const DWORD state_tss[]={D3DTSS_COLOROP,D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS};
static const char *state_tss_names[]={"TSS_COLOR","TSS_COORD","TSS_TRANSFORM"};
static const DWORD state_tss_values[2][3]={{D3DTOP_SELECTARG1,0,0},{D3DTOP_ADD,1,D3DTTFF_COUNT2}};
static const D3DTRANSFORMSTATETYPE state_matrices[]={D3DTRANSFORMSTATE_WORLD,D3DTRANSFORMSTATE_VIEW,D3DTRANSFORMSTATE_PROJECTION,D3DTRANSFORMSTATE_TEXTURE0};
static const char *state_matrix_names[]={"WORLD","VIEW","PROJECTION","TEX_MATRIX"};
#define STATE_TRY(x) do{HRESULT result=(x);if(FAILED(result)){status(#x,result);return result;}}while(0)
static HRESULT state_seed(IDirect3DDevice7 *dev,unsigned variant)
{
    for(unsigned i=0;i<6;++i)STATE_TRY(IDirect3DDevice7_SetRenderState(dev,state_rs[i],state_rs_values[variant][i]));
    for(unsigned i=0;i<3;++i)STATE_TRY(IDirect3DDevice7_SetTextureStageState(dev,0,state_tss[i],state_tss_values[variant][i]));
    D3DMATRIX matrix;ZeroMemory(&matrix,sizeof(matrix));matrix._11=variant?2:1;matrix._22=matrix._33=matrix._44=1;matrix._41=variant?4:0;
    for(unsigned i=0;i<4;++i)STATE_TRY(IDirect3DDevice7_SetTransform(dev,state_matrices[i],&matrix));
    D3DVIEWPORT7 viewport={variant?1:0,variant?2:0,variant?64:128,variant?60:128,variant?.25f:0,variant?.75f:1};STATE_TRY(IDirect3DDevice7_SetViewport(dev,&viewport));
    D3DMATERIAL7 material;ZeroMemory(&material,sizeof(material));material.dcvDiffuse.r=variant?.5f:.25f;material.dcvDiffuse.g=variant?.75f:.5f;material.dcvDiffuse.b=variant?.25f:.75f;material.dcvDiffuse.a=variant?.5f:1;STATE_TRY(IDirect3DDevice7_SetMaterial(dev,&material));
    float plane[4]={variant?0:1,variant?1:0,0,variant?2:0};STATE_TRY(IDirect3DDevice7_SetClipPlane(dev,5,plane));STATE_TRY(IDirect3DDevice7_SetTexture(dev,0,state_textures[variant]));return D3D_OK;
}
#undef STATE_TRY
static void state_query(IDirect3DDevice7 *dev,unsigned type,const char *phase)
{
    for(unsigned i=0;i<6;++i){DWORD value=0xcccccccc;HRESULT hr=IDirect3DDevice7_GetRenderState(dev,state_rs[i],&value);state_value(type,phase,state_rs_names[i],hr,&value,1);}
    for(unsigned i=0;i<3;++i){DWORD value=0xcccccccc;HRESULT hr=IDirect3DDevice7_GetTextureStageState(dev,0,state_tss[i],&value);state_value(type,phase,state_tss_names[i],hr,&value,1);}
    for(unsigned i=0;i<4;++i){D3DMATRIX matrix;FillMemory(&matrix,sizeof(matrix),0xcc);HRESULT hr=IDirect3DDevice7_GetTransform(dev,state_matrices[i],&matrix);state_value(type,phase,state_matrix_names[i],hr,&matrix,16);}
    D3DVIEWPORT7 viewport;FillMemory(&viewport,sizeof(viewport),0xcc);HRESULT hr=IDirect3DDevice7_GetViewport(dev,&viewport);state_value(type,phase,"VIEWPORT",hr,&viewport,6);
    D3DMATERIAL7 material;FillMemory(&material,sizeof(material),0xcc);hr=IDirect3DDevice7_GetMaterial(dev,&material);state_value(type,phase,"MATERIAL",hr,&material,17);
    float plane[4];FillMemory(plane,sizeof(plane),0xcc);hr=IDirect3DDevice7_GetClipPlane(dev,5,plane);state_value(type,phase,"CLIP_PLANE",hr,plane,4);
    IDirectDrawSurface7 *texture=NULL;hr=IDirect3DDevice7_GetTexture(dev,0,&texture);DWORD which=texture==state_textures[0]?1:texture==state_textures[1]?2:texture?3:0;state_value(type,phase,"TEXTURE",hr,&which,1);if(texture)IDirectDrawSurface7_Release(texture);
}
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH], t[200];
    GetModuleFileNameA(0, p, sizeof(p));
    int at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "CLIP.TXT");
    logfile = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    if (logfile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    line("READY native Microsoft DDraw7 state-block reference");
#ifdef D9WG_PROXY_CAPTURE
    GetModuleFileNameA(NULL, p, sizeof(p));
    at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "ddraw.dll");
#else
    GetSystemDirectoryA(p, sizeof(p));
    lstrcatA(p, "\\ddraw.dll");
#endif
    HMODULE lib = LoadLibraryA(p);
    if (!lib) {
        status("load native ddraw", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    HRESULT(WINAPI * create)(GUID*, void**, REFIID, IUnknown*)
        = (void*)GetProcAddress(lib, "DirectDrawCreateEx");
    if (!create) {
        status("native DirectDrawCreateEx export", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    IDirectDraw7* dd = 0;
    IDirect3D7* d3d = 0;
    IDirectDrawSurface7 *target = 0, *depth = 0;
    IDirect3DDevice7* dev = 0;
    HWND window = CreateWindowExA(
        0, "STATIC", "Clip reference", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, 0, 0, GetModuleHandleA(0), 0);
    TRY(create(0, (void**)&dd, &IID_IDirectDraw7, 0));
    TRY(IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL));
    TRY(IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void**)&d3d));
    DDSURFACEDESC2 desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = desc.dwHeight = 128;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask = 0xff;
    TRY(IDirectDraw7_CreateSurface(dd, &desc, &target, 0));
    DDSURFACEDESC2 zd;
    ZeroMemory(&zd, sizeof(zd));
    zd.dwSize = sizeof(zd);
    zd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zd.dwWidth = zd.dwHeight = 128;
    zd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
    zd.ddpfPixelFormat.dwSize = sizeof(zd.ddpfPixelFormat);
    zd.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    zd.ddpfPixelFormat.dwZBufferBitDepth = 16;
    zd.ddpfPixelFormat.dwZBitMask = 0xffff;
    TRY(IDirectDraw7_CreateSurface(dd, &zd, &depth, 0));
    TRY(IDirectDrawSurface7_AddAttachedSurface(target, depth));
    TRY(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DRGBDevice, target, &dev));
    D3DDEVICEDESC7 caps;
    TRY(IDirect3DDevice7_GetCaps(dev, &caps));
    wsprintfA(t,"CAPS maxActive=%lu",caps.dwMaxActiveLights);line(t);
    DDSURFACEDESC2 texture_desc=desc;texture_desc.dwWidth=texture_desc.dwHeight=2;texture_desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY;
    for(unsigned i=0;i<2;++i)TRY(IDirectDraw7_CreateSurface(dd,&texture_desc,&state_textures[i],NULL));
    for(unsigned type=1;type<=3;++type){DWORD block;TRY(state_seed(dev,0));TRY(IDirect3DDevice7_CreateStateBlock(dev,(D3DSTATEBLOCKTYPE)type,&block));TRY(state_seed(dev,1));TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));state_query(dev,type,"restore");TRY(state_seed(dev,1));TRY(IDirect3DDevice7_CaptureStateBlock(dev,block));TRY(state_seed(dev,0));TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));state_query(dev,type,"recapture");TRY(IDirect3DDevice7_DeleteStateBlock(dev,block));}
    DWORD block;TRY(state_seed(dev,0));TRY(IDirect3DDevice7_BeginStateBlock(dev));TRY(state_seed(dev,1));state_query(dev,0,"during");TRY(IDirect3DDevice7_EndStateBlock(dev,&block));state_query(dev,0,"end");TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));state_query(dev,0,"record_apply");TRY(state_seed(dev,0));TRY(IDirect3DDevice7_CaptureStateBlock(dev,block));TRY(state_seed(dev,1));TRY(IDirect3DDevice7_ApplyStateBlock(dev,block));state_query(dev,0,"record_capture");TRY(IDirect3DDevice7_DeleteStateBlock(dev,block));
    TRY(IDirect3DDevice7_SetTexture(dev,0,NULL));for(unsigned i=0;i<2;++i)IDirectDrawSurface7_Release(state_textures[i]);
    IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(depth);IDirectDrawSurface7_Release(target);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);DestroyWindow(window);FreeLibrary(lib);
    line("PASS native state-block reference finished");
done:
    CloseHandle(logfile);ExitProcess(0);
}
