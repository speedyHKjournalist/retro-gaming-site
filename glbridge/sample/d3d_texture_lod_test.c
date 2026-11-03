/* XP resource LOD and mip-filter probe; explicit simulated CPU replies, separate GPU replay. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_LOD_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
#define DEV(method) IDirect3DDevice9_##method
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static const DWORD lod_palette[3] = {0xffff0000u,0xff00ff00u,0xff0000ffu};
static Factory* factory;
static Device* device;
static HANDLE logfile;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL condition, const char* message)
{
    if (!condition) {
        report(message);
        ExitProcess(1);
    }
}

#if D9WG_LOD_API == 8
typedef IDirect3DBaseTexture8 BaseTexture;
typedef IDirect3DTexture8 Texture;
typedef IDirect3DCubeTexture8 Cube;
typedef IDirect3DVolumeTexture8 Volume;
#define TEX(method) IDirect3DTexture8_##method
#define CUBE(method) IDirect3DCubeTexture8_##method
#define VOL(method) IDirect3DVolumeTexture8_##method
#define BASE(method) IDirect3DBaseTexture8_##method
#else
typedef IDirect3DBaseTexture9 BaseTexture;
typedef IDirect3DTexture9 Texture;
typedef IDirect3DCubeTexture9 Cube;
typedef IDirect3DVolumeTexture9 Volume;
#define TEX(method) IDirect3DTexture9_##method
#define CUBE(method) IDirect3DCubeTexture9_##method
#define VOL(method) IDirect3DVolumeTexture9_##method
#define BASE(method) IDirect3DBaseTexture9_##method
#endif
static UINT mip_size(UINT size, UINT mip)
{
    size >>= mip;
    return size ? size : 1;
}
static BaseTexture* create_texture(UINT kind, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DPOOL pool)
{
    void* texture = NULL;
    HRESULT result;
    if (kind == 0)
        result = DEV(CreateTexture)(device, w, h, levels, usage, D3DFMT_A8R8G8B8, pool, (Texture**)&texture
#if D9WG_LOD_API == 9
            ,
            NULL
#endif
        );
    else if (kind == 1)
        result = DEV(CreateCubeTexture)(device, w, levels, usage, D3DFMT_A8R8G8B8, pool, (Cube**)&texture
#if D9WG_LOD_API == 9
            ,
            NULL
#endif
        );
    else
        result = DEV(CreateVolumeTexture)(device, w, h, d, levels, usage, D3DFMT_A8R8G8B8, pool, (Volume**)&texture
#if D9WG_LOD_API == 9
            ,
            NULL
#endif
        );
    check(result == D3D_OK && texture != NULL, "FAIL CreateTexture");
    return texture;
}
static void fill_texture(BaseTexture* texture, UINT kind, UINT w, UINT h, UINT d, UINT levels)
{
    for (UINT face = 0; face < (kind == 1 ? 6u : 1u); ++face) {
        for (UINT mip = 0; mip < levels; ++mip) {
            D3DLOCKED_RECT rect;
            D3DLOCKED_BOX box;
            BYTE* data;
            UINT row_pitch, slice_pitch;
            if (kind == 2) {
                check(VOL(LockBox)((Volume*)texture, mip, &box, NULL, 0) == D3D_OK, "FAIL LockBox");
                data = box.pBits;
                row_pitch = box.RowPitch;
                slice_pitch = box.SlicePitch;
            } else {
                HRESULT hr = kind == 1 ? CUBE(LockRect)((Cube*)texture, (D3DCUBEMAP_FACES)face, mip, &rect, NULL, 0)
                                       : TEX(LockRect)((Texture*)texture, mip, &rect, NULL, 0);
                check(hr == D3D_OK, "FAIL LockRect");
                data = rect.pBits;
                row_pitch = rect.Pitch;
                slice_pitch = row_pitch * mip_size(h, mip);
            }
            for (UINT z = 0; z < (kind == 2 ? mip_size(d, mip) : 1u); ++z) {
                DWORD color = lod_palette[mip];
                for (UINT y = 0; y < mip_size(h, mip); ++y) {
                    DWORD* row = (DWORD*)(data + z * slice_pitch + y * row_pitch);
                    for (UINT x = 0; x < mip_size(w, mip); ++x)
                        row[x] = color;
                }
            }
            check((kind == 2          ? VOL(UnlockBox)((Volume*)texture, mip)
                          : kind == 1 ? CUBE(UnlockRect)((Cube*)texture, (D3DCUBEMAP_FACES)face, mip)
                                      : TEX(UnlockRect)((Texture*)texture, mip))
                    == D3D_OK,
                "FAIL unlock texture");
        }
    }
}
static void sampler(UINT state, DWORD value)
{
#if D9WG_LOD_API == 8
    check(DEV(SetTextureStageState)(device, 0, (D3DTEXTURESTAGESTATETYPE)state, value) == D3D_OK, "FAIL sampler8");
#else
    check(DEV(SetSamplerState)(device, 0, (D3DSAMPLERSTATETYPE)state, value) == D3D_OK, "FAIL sampler9");
#endif
}
#if D9WG_LOD_API == 8
#define SAMP(name) D3DTSS_##name
#else
#define SAMP(name) D3DSAMP_##name
#endif
#if D9WG_LOD_API == 8
typedef IDirect3DSurface8 Surface;
#define SURFACE(method) IDirect3DSurface8_##method
#else
typedef IDirect3DSurface9 Surface;
#define SURFACE(method) IDirect3DSurface9_##method
#endif
static void verify(Surface* image, DWORD color)
{
    D3DLOCKED_RECT lock;
    check(SURFACE(LockRect)(image, &lock, NULL, D3DLOCK_READONLY) == D3D_OK, "FAIL read lock");
    for (UINT y=0;y<64;y++) for(UINT x=0;x<64;x++)
        check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==color,"FAIL LOD pixels");
    check(SURFACE(UnlockRect)(image)==D3D_OK,"FAIL read unlock");
}
static void run_lod_cases(void)
{
    Surface *back=NULL,*image=NULL;
#if D9WG_LOD_API == 8
    check(DEV(GetRenderTarget)(device,&back)==D3D_OK,"FAIL back");
    check(DEV(CreateImageSurface)(device,64,64,D3DFMT_X8R8G8B8,&image)==D3D_OK,"FAIL image");
#else
    check(DEV(GetRenderTarget)(device,0,&back)==D3D_OK,"FAIL back");
    check(DEV(CreateOffscreenPlainSurface)(device,64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&image,NULL)==D3D_OK,"FAIL image");
#endif
    DEV(SetRenderState)(device,D3DRS_LIGHTING,FALSE);
    DEV(SetRenderState)(device,D3DRS_ZENABLE,FALSE);
    DEV(SetRenderState)(device,D3DRS_CULLMODE,D3DCULL_NONE);
    DEV(SetRenderState)(device,D3DRS_ALPHABLENDENABLE,FALSE);
    DEV(SetTextureStageState)(device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    DEV(SetTextureStageState)(device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    sampler(SAMP(MAGFILTER),D3DTEXF_POINT);
    sampler(SAMP(MINFILTER),D3DTEXF_POINT);
    sampler(SAMP(ADDRESSU),D3DTADDRESS_CLAMP);
    sampler(SAMP(ADDRESSV),D3DTADDRESS_CLAMP);
    sampler(SAMP(ADDRESSW),D3DTADDRESS_CLAMP);
#if D9WG_LOD_API == 8
    check(DEV(SetVertexShader)(device,D3DFVF_XYZRHW|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE3(0))==D3D_OK,"FAIL FVF8");
#else
    check(DEV(SetFVF)(device,D3DFVF_XYZRHW|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE3(0))==D3D_OK,"FAIL FVF9");
#endif
    const DWORD requests[7]={0,1,2,1,1,0xffffffffu,0};
    const DWORD filter[7]={0,0,1,1,0,1,0},max_mip[7]={3,0,1,2,2,0,0},sample_mip[7]={0,1,2,2,1,2,0};
    for(UINT kind=0;kind<3;kind++) for(UINT pool=0;pool<4;pool++) {
        BaseTexture* texture=create_texture(kind,8,8,8,3,0,(D3DPOOL)pool);
        check(BASE(GetLOD)(texture)==0,"FAIL initial LOD");
        if(pool!=D3DPOOL_MANAGED) {
            check(BASE(SetLOD)(texture,1)==0&&BASE(SetLOD)(texture,0xffffffffu)==0&&BASE(GetLOD)(texture)==0,"FAIL ignored pool LOD");
            BASE(Release)(texture);
            continue;
        }
        fill_texture(texture,kind,8,8,8,3);
        check(DEV(SetTexture)(device,0,texture)==D3D_OK,"FAIL bind");
        check(DEV(BeginStateBlock)(device)==D3D_OK,"FAIL begin recording");
        check(BASE(SetLOD)(texture,1)==0&&BASE(GetLOD)(texture)==1,"FAIL LOD recording deferred");
#if D9WG_LOD_API == 8
        DWORD block=0;
#else
        IDirect3DStateBlock9* block=NULL;
#endif
        check(DEV(EndStateBlock)(device,&block)==D3D_OK,"FAIL end recording");
        check(BASE(SetLOD)(texture,0)==1,"FAIL LOD reset");
#if D9WG_LOD_API == 8
        check(DEV(ApplyStateBlock)(device,block)==D3D_OK,"FAIL apply");
        check(DEV(DeleteStateBlock)(device,block)==D3D_OK,"FAIL delete block");
#else
        check(IDirect3DStateBlock9_Apply(block)==D3D_OK,"FAIL apply");
        IDirect3DStateBlock9_Release(block);
#endif
        check(BASE(GetLOD)(texture)==0,"FAIL resource LOD captured in state block");
        DWORD previous=0;
        for(UINT n=0;n<7;n++) {
            check(BASE(SetLOD)(texture,requests[n])==previous,"FAIL SetLOD previous");
            previous=requests[n]<3?requests[n]:2;
            check(BASE(GetLOD)(texture)==previous,"FAIL stored LOD");
            sampler(SAMP(MIPFILTER),filter[n]);
            sampler(SAMP(MAXMIPLEVEL),max_mip[n]);
            struct {float x,y,z,rhw,u,v,w;} vertices[4];
            for(UINT v=0;v<4;v++) {
                vertices[v].x=v&1?65:-1;vertices[v].y=v&2?65:-1;
                vertices[v].z=.5f;vertices[v].rhw=1;
                vertices[v].u=kind==1?1:.5f;vertices[v].v=kind==1?0:.5f;vertices[v].w=kind==2?.5f:0;
            }
            check(DEV(Clear)(device,0,NULL,D3DCLEAR_TARGET,0xff101010u,1,0)==D3D_OK,"FAIL clear");
            check(DEV(BeginScene)(device)==D3D_OK,"FAIL begin");
            check(DEV(DrawPrimitiveUP)(device,D3DPT_TRIANGLESTRIP,2,vertices,sizeof(vertices[0]))==D3D_OK,"FAIL draw");
            check(DEV(EndScene)(device)==D3D_OK,"FAIL end");
#if D9WG_LOD_API == 8
            check(DEV(CopyRects)(device,back,NULL,0,image,NULL)==D3D_OK,"FAIL readback8");
#else
            check(DEV(GetRenderTargetData)(device,back,image)==D3D_OK,"FAIL readback9");
#endif
            verify(image,lod_palette[sample_mip[n]]);
            char line[100];
            wsprintfA(line,"LOD %u %u %u %u %u %08lX",kind,n,(UINT)previous,(UINT)filter[n],(UINT)max_mip[n],lod_palette[sample_mip[n]]);
            report(line);
        }
        check(DEV(SetTexture)(device,0,NULL)==D3D_OK,"FAIL unbind");
        BASE(Release)(texture);
    }
    SURFACE(Release)(image);
    SURFACE(Release)(back);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "LOD%u.TXT", D9WG_LOD_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "Texture LOD probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_LOD_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_LOD_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_LOD_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    run_lod_cases();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS texture LOD API=%u", D9WG_LOD_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
