/* Independent XP surface attachment view/size/refcount observations. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static const unsigned versions[] = {1,2,3,4,7};
static const GUID *ids[] = {&IID_IDirectDrawSurface,&IID_IDirectDrawSurface2,
    &IID_IDirectDrawSurface3,&IID_IDirectDrawSurface4,&IID_IDirectDrawSurface7};
typedef struct Pair { IUnknown *color[5], *depth[5]; } Pair;
static void line(const char *s) { DWORD n;WriteFile(output,s,lstrlenA(s),&n,NULL);WriteFile(output,"\r\n",2,&n,NULL);FlushFileBuffers(output); }
static void dispose(Pair *p) { for(unsigned i=0;i<5;++i)if(p->color[i])IUnknown_Release(p->color[i]);for(unsigned i=0;i<5;++i)if(p->depth[i])IUnknown_Release(p->depth[i]);ZeroMemory(p,sizeof(*p)); }
static BOOL make_surface(IDirectDraw4 *draw,BOOL depth,UINT size,IUnknown **views)
{
    DDSURFACEDESC2 d;IDirectDrawSurface4 *s=NULL;
    ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    d.dwWidth=d.dwHeight=size;d.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|(depth?DDSCAPS_ZBUFFER:DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE);
    d.ddpfPixelFormat.dwSize=sizeof(d.ddpfPixelFormat);d.ddpfPixelFormat.dwFlags=depth?DDPF_ZBUFFER:DDPF_RGB;
    d.ddpfPixelFormat.dwRGBBitCount=16;
    if(depth)d.ddpfPixelFormat.dwZBitMask=0xffff;
    else{d.ddpfPixelFormat.dwRBitMask=0xf800;d.ddpfPixelFormat.dwGBitMask=0x7e0;d.ddpfPixelFormat.dwBBitMask=0x1f;}
    HRESULT hr=IDirectDraw4_CreateSurface(draw,&d,&s,NULL);if(FAILED(hr))return FALSE;
    BOOL ok=TRUE;for(unsigned i=0;i<5;++i)if(FAILED(IDirectDrawSurface4_QueryInterface(s,ids[i],(void **)&views[i])))ok=FALSE;
    IDirectDrawSurface4_Release(s);return ok;
}
static BOOL pair(IDirectDraw4*d,UINT size,Pair*p){ZeroMemory(p,sizeof(*p));if(make_surface(d,FALSE,64,p->color)&&make_surface(d,TRUE,size,p->depth))return TRUE;dispose(p);return FALSE;}
static ULONG refs(IUnknown *p){IUnknown_AddRef(p);return IUnknown_Release(p);}
static void report(const char*action,unsigned a,unsigned b,HRESULT add,HRESULT op,Pair*p,UINT size,unsigned returned)
{
    char s[512];ULONG r[5];for(unsigned i=0;i<5;++i)r[i]=refs(p->depth[i]);
    wsprintfA(s,"ATTACH action=%s add_view=%u other_view=%u hr_add=%08lX hr_op=%08lX depth_size=%u same_view=%u returned_view=%u refs=%lu,%lu,%lu,%lu,%lu",action,versions[a],versions[b],(DWORD)add,(DWORD)op,size,returned==versions[b],returned,r[0],r[1],r[2],r[3],r[4]);line(s);
}
static HRESULT add(Pair*p,unsigned a){return IDirectDrawSurface7_AddAttachedSurface((IDirectDrawSurface7*)p->color[a],(IDirectDrawSurface7*)p->depth[a]);}
static HRESULT detach(Pair*p,unsigned a){return IDirectDrawSurface7_DeleteAttachedSurface((IDirectDrawSurface7*)p->color[a],0,(IDirectDrawSurface7*)p->depth[a]);}
static BOOL reference(HMODULE module,HWND window)
{
    HRESULT(WINAPI*create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreate");
    IDirectDraw *base=NULL;IDirectDraw4*draw=NULL;BOOL ok=FALSE;Pair p;
    if(!create||FAILED(create(NULL,&base,NULL)))return FALSE;
    HRESULT hr=IDirectDraw_QueryInterface(base,&IID_IDirectDraw4,(void**)&draw);IDirectDraw_Release(base);if(FAILED(hr))return FALSE;
    if(FAILED(IDirectDraw4_SetCooperativeLevel(draw,window,DDSCL_NORMAL)))goto done;
    for(unsigned a=0;a<5;++a){
        for(unsigned b=0;b<5;++b){
            if(!pair(draw,64,&p))goto done;
            HRESULT first=add(&p,a),op=detach(&p,b);report("delete",a,b,first,op,&p,64,FALSE);dispose(&p);
            if(!pair(draw,64,&p))goto done;
            first=add(&p,a);op=add(&p,b);report("duplicate",a,b,first,op,&p,64,FALSE);dispose(&p);
        }
        if(!pair(draw,64,&p))goto done;
        HRESULT first=add(&p,a);
        for(unsigned b=0;b<5;++b){DDSCAPS2 caps={DDSCAPS_ZBUFFER,0,0,{0}};IDirectDrawSurface7*s=NULL;
            HRESULT get=IDirectDrawSurface7_GetAttachedSurface((IDirectDrawSurface7*)p.color[b],&caps,&s);
            unsigned returned=0;for(unsigned v=0;v<5;++v)if((IUnknown*)s==p.depth[v])returned=versions[v];if(s)IDirectDrawSurface7_Release(s);report("query",a,b,first,get,&p,64,returned);}
        dispose(&p);
        if(!pair(draw,64,&p))goto done;
        first=add(&p,a);ULONG released=IUnknown_Release(p.color[a]);p.color[a]=NULL;
        report("retire",a,a,first,(HRESULT)released,&p,64,FALSE);dispose(&p);
        for(UINT size=32;size<=128;size*=2){if(!pair(draw,size,&p))goto done;
            first=add(&p,a);report("size",a,a,first,first,&p,size,FALSE);dispose(&p);}
    }
    line("COMPLETE attachment reference");ok=TRUE;
done:IDirectDraw4_Release(draw);return ok;
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,path,MAX_PATH);unsigned at=lstrlenA(path);while(at&&path[at-1]!=92)--at;path[at]=0;lstrcatA(path,"ATTREF.TXT");
    output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(2);
    HWND window=CreateWindowA("static","Attachment reference",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,320,240,NULL,NULL,GetModuleHandleA(NULL),NULL);
#ifdef DDRAW_ATTACHMENT_PROXY
    GetModuleFileNameA(NULL,path,MAX_PATH);at=lstrlenA(path);while(at&&path[at-1]!=92)--at;path[at]=0;lstrcatA(path,"ddraw.dll");
#else
    GetSystemDirectoryA(path,MAX_PATH);lstrcatA(path,"\\ddraw.dll");
#endif
    HMODULE module=LoadLibraryA(path);BOOL ok=window&&module&&reference(module,window);if(!ok)line("FAIL attachment reference setup");
    if(window)DestroyWindow(window);
    if(module)FreeLibrary(module);
    CloseHandle(output);
    ExitProcess(ok?0:1);
}
