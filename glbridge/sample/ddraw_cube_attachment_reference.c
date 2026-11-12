#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

static HANDLE output;
static UINT api;
static const DWORD faces[] = {
    DDSCAPS2_CUBEMAP_POSITIVEX, DDSCAPS2_CUBEMAP_NEGATIVEX,
    DDSCAPS2_CUBEMAP_POSITIVEY, DDSCAPS2_CUBEMAP_NEGATIVEY,
    DDSCAPS2_CUBEMAP_POSITIVEZ, DDSCAPS2_CUBEMAP_NEGATIVEZ,
};
static void line(const char *text)
{
    DWORD written;
    WriteFile(output,text,lstrlenA(text),&written,NULL);
    WriteFile(output,"\r\n",2,&written,NULL);
    FlushFileBuffers(output);
}
static HRESULT attached(void *surface, DDSCAPS2 *caps, void **out)
{
    return api==4?IDirectDrawSurface4_GetAttachedSurface((IDirectDrawSurface4*)surface,caps,(IDirectDrawSurface4**)out)
        :IDirectDrawSurface7_GetAttachedSurface((IDirectDrawSurface7*)surface,caps,(IDirectDrawSurface7**)out);
}
static HRESULT describe(void *surface,DDSURFACEDESC2 *desc)
{
    ZeroMemory(desc,sizeof(*desc));desc->dwSize=sizeof(*desc);
    return api==4?IDirectDrawSurface4_GetSurfaceDesc((IDirectDrawSurface4*)surface,desc)
        :IDirectDrawSurface7_GetSurfaceDesc((IDirectDrawSurface7*)surface,desc);
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH],text[400];UINT end;
    IDirectDraw7 *root=NULL;IDirectDraw4 *old=NULL;IDirectDraw *first=NULL;
    HMODULE module;
    HRESULT(WINAPI*ex)(GUID*,void**,REFIID,IUnknown*);
    HRESULT(WINAPI*legacy)(GUID*,IDirectDraw**,IUnknown*);
    GetModuleFileNameA(NULL,path,sizeof(path));end=lstrlenA(path);
    while(end&&path[end-1]!='\\')--end;
    lstrcpyA(path+end,"CUBEREF.TXT");
    output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(output==INVALID_HANDLE_VALUE)ExitProcess(1);
    module=LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");
    ex=(void*)GetProcAddress(module,"DirectDrawCreateEx");legacy=(void*)GetProcAddress(module,"DirectDrawCreate");
    if(!ex||!legacy||FAILED(ex(NULL,(void**)&root,&IID_IDirectDraw7,NULL))||FAILED(legacy(NULL,&first,NULL)))ExitProcess(2);
    if(FAILED(IDirectDraw_QueryInterface(first,&IID_IDirectDraw4,(void**)&old)))ExitProcess(3);
    IDirectDraw_Release(first);
    IDirectDraw4_SetCooperativeLevel(old,NULL,DDSCL_NORMAL);
    IDirectDraw7_SetCooperativeLevel(root,NULL,DDSCL_NORMAL);
    for(api=4;api<=7;api+=3)for(UINT levels=1;levels<=3;levels+=2){
        DDSURFACEDESC2 desc;void *surface=NULL;HRESULT hr;
        ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);
        desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        desc.dwWidth=desc.dwHeight=16;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY|DDSCAPS_COMPLEX;
        desc.ddsCaps.dwCaps2=DDSCAPS2_CUBEMAP|DDSCAPS2_CUBEMAP_ALLFACES;
        if(levels>1){desc.dwFlags|=DDSD_MIPMAPCOUNT;desc.dwMipMapCount=levels;desc.ddsCaps.dwCaps|=DDSCAPS_MIPMAP;}
        desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBBitCount=32;
        desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
        hr=api==4?IDirectDraw4_CreateSurface(old,&desc,(IDirectDrawSurface4**)&surface,NULL):IDirectDraw7_CreateSurface(root,&desc,(IDirectDrawSurface7**)&surface,NULL);
        wsprintfA(text,"CREATE api=%u levels=%u hr=%08lX",api,levels,(DWORD)hr);line(text);
        if(FAILED(hr))continue;
        describe(surface,&desc);wsprintfA(text,"ROOT api=%u levels=%u width=%lu caps=%08lX caps2=%08lX",api,levels,desc.dwWidth,desc.ddsCaps.dwCaps,desc.ddsCaps.dwCaps2);line(text);
        for(UINT source_face=0;source_face<6;source_face++){
            void *source=NULL;DDSCAPS2 caps;ZeroMemory(&caps,sizeof(caps));caps.dwCaps=DDSCAPS_COMPLEX;caps.dwCaps2=faces[source_face];
            if(!source_face){source=surface;IUnknown_AddRef((IUnknown*)source);hr=DD_OK;}
            else hr=attached(surface,&caps,&source);
            wsprintfA(text,"FACE api=%u levels=%u face=%u hr=%08lX",api,levels,source_face,(DWORD)hr);line(text);
            if(FAILED(hr))continue;
            for(UINT wanted=0;wanted<6;wanted++)for(UINT role=0;role<3;role++){
                void *found=(void*)0xdeadbeef;ZeroMemory(&caps,sizeof(caps));caps.dwCaps=role==0?DDSCAPS_COMPLEX:role==1?DDSCAPS_TEXTURE:0;caps.dwCaps2=faces[wanted];
                hr=attached(source,&caps,&found);
                DWORD width=0,caps2=0;if(SUCCEEDED(hr)&&found){describe(found,&desc);width=desc.dwWidth;caps2=desc.ddsCaps.dwCaps2;}
                wsprintfA(text,"QUERY api=%u levels=%u source=%u wanted=%u role=%u hr=%08lX width=%lu self=%u caps2=%08lX pointer=%08lX",api,levels,source_face,wanted,role,(DWORD)hr,width,found==source,caps2,(DWORD)(uintptr_t)found);line(text);
                if(SUCCEEDED(hr)&&found)IUnknown_Release((IUnknown*)found);
            }
            void *current=source;IUnknown_AddRef((IUnknown*)current);
            for(UINT step=0;step<levels;step++){
                void *next=(void*)0xdeadbeef;ZeroMemory(&caps,sizeof(caps));caps.dwCaps=DDSCAPS_COMPLEX;caps.dwCaps2=faces[source_face];
                hr=attached(current,&caps,&next);DWORD width=0;
                if(SUCCEEDED(hr)&&next){describe(next,&desc);width=desc.dwWidth;}
                wsprintfA(text,"WALK api=%u levels=%u face=%u step=%u hr=%08lX width=%lu self=%u pointer=%08lX",api,levels,source_face,step,(DWORD)hr,width,current==next,(DWORD)(uintptr_t)next);line(text);
                IUnknown_Release((IUnknown*)current);current=NULL;
                if(FAILED(hr)||!next)break;
                current=next;
            }
            if(current)IUnknown_Release((IUnknown*)current);
            IUnknown_Release((IUnknown*)source);
        }
        IUnknown_Release((IUnknown*)surface);
    }
    IDirectDraw4_Release(old);IDirectDraw7_Release(root);FreeLibrary(module);
    line("PASS native cube attachment reference complete");CloseHandle(output);ExitProcess(0);
}
