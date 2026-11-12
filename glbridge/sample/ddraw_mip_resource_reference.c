#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

static HANDLE output;
static UINT kind, api;
static void line(const char *text)
{
    DWORD written;
    WriteFile(output,text,lstrlenA(text),&written,NULL);
    WriteFile(output,"\r\n",2,&written,NULL);
    FlushFileBuffers(output);
}
static HRESULT create(IDirectDraw7 *root,IDirectDraw4 *old,DDSURFACEDESC2 *d,void **s)
{
    return api==4?IDirectDraw4_CreateSurface(old,d,(IDirectDrawSurface4**)s,NULL)
        :IDirectDraw7_CreateSurface(root,d,(IDirectDrawSurface7**)s,NULL);
}
static HRESULT palette_set(void *s,IDirectDrawPalette *p)
{
    return api==4?IDirectDrawSurface4_SetPalette((IDirectDrawSurface4*)s,p)
        :IDirectDrawSurface7_SetPalette((IDirectDrawSurface7*)s,p);
}
static HRESULT palette_get(void *s,IDirectDrawPalette **p)
{
    return api==4?IDirectDrawSurface4_GetPalette((IDirectDrawSurface4*)s,p)
        :IDirectDrawSurface7_GetPalette((IDirectDrawSurface7*)s,p);
}
static HRESULT key_set(void *s,DDCOLORKEY *key)
{
    return api==4?IDirectDrawSurface4_SetColorKey((IDirectDrawSurface4*)s,DDCKEY_SRCBLT,key)
        :IDirectDrawSurface7_SetColorKey((IDirectDrawSurface7*)s,DDCKEY_SRCBLT,key);
}
static HRESULT key_get(void *s,DDCOLORKEY *key)
{
    return api==4?IDirectDrawSurface4_GetColorKey((IDirectDrawSurface4*)s,DDCKEY_SRCBLT,key)
        :IDirectDrawSurface7_GetColorKey((IDirectDrawSurface7*)s,DDCKEY_SRCBLT,key);
}
static ULONG refs(IDirectDrawPalette *p)
{
    IDirectDrawPalette_AddRef(p);
    return IDirectDrawPalette_Release(p);
}
static void observe(void *surface,UINT resource,UINT format,UINT level,
        IDirectDrawPalette *normal,IDirectDrawPalette *alpha)
{
    char text[300];
    IDirectDrawPalette *out;
    HRESULT hr;
    DDCOLORKEY key;
    out=(void*)0xdeadbeef;hr=palette_get(surface,&out);
    wsprintfA(text,"GET kind=%u resource=%u format=%u level=%u hr=%08lX pointer=%08lX",kind,resource,format,level,(DWORD)hr,(DWORD)(uintptr_t)out);line(text);
    if(SUCCEEDED(hr)&&out)IDirectDrawPalette_Release(out);
    for(UINT p=0;p<3;p++){
        IDirectDrawPalette *input=p==0?normal:p==1?alpha:NULL;
        hr=palette_set(surface,input);
        wsprintfA(text,"SET kind=%u resource=%u format=%u level=%u palette=%u hr=%08lX refs=%lu",kind,resource,format,level,p,(DWORD)hr,refs(p==1?alpha:normal));line(text);
        out=(void*)0xdeadbeef;hr=palette_get(surface,&out);
        wsprintfA(text,"AFTER kind=%u resource=%u format=%u level=%u palette=%u hr=%08lX nonnull=%u match=%u",kind,resource,format,level,p,(DWORD)hr,!!out,out==input);line(text);
        if(SUCCEEDED(hr)&&out)IDirectDrawPalette_Release(out);
    }
    key.dwColorSpaceLowValue=key.dwColorSpaceHighValue=0x22;
    hr=key_set(surface,&key);
    wsprintfA(text,"KEYSET kind=%u resource=%u format=%u level=%u hr=%08lX",kind,resource,format,level,(DWORD)hr);line(text);
    key.dwColorSpaceLowValue=key.dwColorSpaceHighValue=0xdeadbeef;
    hr=key_get(surface,&key);
    wsprintfA(text,"KEYGET kind=%u resource=%u format=%u level=%u hr=%08lX low=%08lX high=%08lX",kind,resource,format,level,(DWORD)hr,key.dwColorSpaceLowValue,key.dwColorSpaceHighValue);line(text);
    hr=key_set(surface,NULL);
    wsprintfA(text,"KEYCLEAR kind=%u resource=%u format=%u level=%u hr=%08lX",kind,resource,format,level,(DWORD)hr);line(text);
    if(api==7){
        DWORD value=0xdeadbeef;
        hr=IDirectDrawSurface7_GetPriority((IDirectDrawSurface7*)surface,&value);
        wsprintfA(text,"PRIORITY kind=%u resource=%u format=%u level=%u stage=0 hr=%08lX value=%08lX",kind,resource,format,level,(DWORD)hr,value);line(text);
        /* Native SetPriority may crash on nontextures and implicit sublevels. */
        if(resource&& !level)for(UINT n=1;n<=2;n++){
            DWORD input=n==1?1:0xffffffffu;
            hr=IDirectDrawSurface7_SetPriority((IDirectDrawSurface7*)surface,input);
            wsprintfA(text,"SETPRIORITY kind=%u resource=%u format=%u level=%u input=%08lX hr=%08lX",kind,resource,format,level,input,(DWORD)hr);line(text);
            value=0xdeadbeef;hr=IDirectDrawSurface7_GetPriority((IDirectDrawSurface7*)surface,&value);
            wsprintfA(text,"PRIORITY kind=%u resource=%u format=%u level=%u stage=%u hr=%08lX value=%08lX",kind,resource,format,level,n,(DWORD)hr,value);line(text);
        }
    }
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH],text[250];UINT end;
    HMODULE module;
    HRESULT(WINAPI*factory)(GUID*,void**,REFIID,IUnknown*);
    HRESULT(WINAPI*legacy_factory)(GUID*,IDirectDraw**,IUnknown*);
    IDirectDraw7 *root=NULL;IDirectDraw4 *old=NULL,*extended_old=NULL;IDirectDraw *legacy=NULL;
    PALETTEENTRY entries[256];IDirectDrawPalette *normal=NULL,*alpha=NULL;
    GetModuleFileNameA(NULL,path,sizeof(path));end=lstrlenA(path);while(end&&path[end-1]!='\\')--end;lstrcpyA(path+end,"MIPREF.TXT");
    output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(1);
    module=LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");factory=(void*)GetProcAddress(module,"DirectDrawCreateEx");legacy_factory=(void*)GetProcAddress(module,"DirectDrawCreate");
    if(!factory||!legacy_factory||FAILED(factory(NULL,(void**)&root,&IID_IDirectDraw7,NULL))||FAILED(legacy_factory(NULL,&legacy,NULL)))ExitProcess(2);
    if(FAILED(IDirectDraw_QueryInterface(legacy,&IID_IDirectDraw4,(void**)&old))||FAILED(IDirectDraw7_QueryInterface(root,&IID_IDirectDraw4,(void**)&extended_old)))ExitProcess(3);
    IDirectDraw_Release(legacy);IDirectDraw4_SetCooperativeLevel(old,NULL,DDSCL_NORMAL);IDirectDraw7_SetCooperativeLevel(root,NULL,DDSCL_NORMAL);
    ZeroMemory(entries,sizeof(entries));
    for(kind=0;kind<3;kind++){
        IDirectDraw4 *palette_owner=kind==0?old:extended_old;
        if(FAILED(IDirectDraw4_CreatePalette(palette_owner,DDPCAPS_8BIT|DDPCAPS_ALLOW256,entries,&normal,NULL))||FAILED(IDirectDraw4_CreatePalette(palette_owner,DDPCAPS_8BIT|DDPCAPS_ALLOW256|DDPCAPS_ALPHA,entries,&alpha,NULL)))ExitProcess(4);
        for(UINT resource=0;resource<4;resource++)for(UINT format=0;format<2;format++){
        DDSURFACEDESC2 d;void *surface=NULL,*current=NULL;HRESULT hr;
        api=kind==2?7:4;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=d.dwHeight=16;
        d.ddsCaps.dwCaps=(resource?DDSCAPS_TEXTURE:DDSCAPS_OFFSCREENPLAIN)|(resource<2?DDSCAPS_SYSTEMMEMORY:0);
        if(resource>=2)d.ddsCaps.dwCaps2=DDSCAPS2_TEXTUREMANAGE;
        if(resource==3){d.dwFlags|=DDSD_MIPMAPCOUNT;d.dwMipMapCount=3;d.ddsCaps.dwCaps|=DDSCAPS_COMPLEX|DDSCAPS_MIPMAP;}
        d.ddpfPixelFormat.dwSize=sizeof(d.ddpfPixelFormat);d.ddpfPixelFormat.dwFlags=DDPF_RGB|(format?0:DDPF_PALETTEINDEXED8);d.ddpfPixelFormat.dwRGBBitCount=format?32:8;
        if(format){d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0x00ff00;d.ddpfPixelFormat.dwBBitMask=0x0000ff;}
        hr=create(root,kind==0?old:extended_old,&d,&surface);
        wsprintfA(text,"CREATE kind=%u resource=%u format=%u hr=%08lX",kind,resource,format,(DWORD)hr);line(text);if(FAILED(hr))continue;
        current=surface;IUnknown_AddRef((IUnknown*)current);
        for(UINT level=0;level<(resource==3?3u:1u);level++){
            observe(current,resource,format,level,normal,alpha);
            void *next=NULL;if(resource==3&&level<2){DDSCAPS2 caps;ZeroMemory(&caps,sizeof(caps));caps.dwCaps=DDSCAPS_COMPLEX;
                hr=api==4?IDirectDrawSurface4_GetAttachedSurface((IDirectDrawSurface4*)current,&caps,(IDirectDrawSurface4**)&next):IDirectDrawSurface7_GetAttachedSurface((IDirectDrawSurface7*)current,&caps,(IDirectDrawSurface7**)&next);
                if(FAILED(hr)||!next)ExitProcess(5);
            }
            IUnknown_Release((IUnknown*)current);current=next;
        }
        IUnknown_Release((IUnknown*)surface);
    }
    IDirectDrawPalette_Release(normal);IDirectDrawPalette_Release(alpha);
    }
    IDirectDraw4_Release(old);IDirectDraw4_Release(extended_old);IDirectDraw7_Release(root);FreeLibrary(module);
    line("PASS native mip resource reference complete");CloseHandle(output);ExitProcess(0);
}
