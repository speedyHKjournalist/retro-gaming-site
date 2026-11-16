/* Additional native graph observations; share only the immutable probe helpers. */
#define WinMainCRTStartup attachment_base_startup
#include "ddraw_attachment_reference.c"
#undef WinMainCRTStartup
static void graph(const char *action,unsigned view,unsigned node,DWORD selector,
        HRESULT hr,int found,unsigned count,unsigned callback_refs)
{
    char s[240];wsprintfA(s,"GRAPH action=%s view=%u node=%u selector=%08lX hr=%08lX found=%d count=%u callback_refs=%u",action,view,node,selector,(DWORD)hr,found,count,callback_refs);line(s);
}
typedef struct EnumContext { Pair *pair;unsigned found,count,ref; } EnumContext;
static HRESULT WINAPI enumerated(IDirectDrawSurface7 *surface,DDSURFACEDESC2 *desc,void *data)
{
    EnumContext *c=data;(void)desc;++c->count;c->found=0;
    for(unsigned i=0;i<5;++i)if((IUnknown*)surface==c->pair->depth[i])c->found=versions[i];
    c->ref=refs((IUnknown*)surface);IDirectDrawSurface7_Release(surface);return DDENUMRET_OK;
}
static BOOL enum_graph(HMODULE module,HWND window)
{
    HRESULT(WINAPI*create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreate");
    IDirectDraw*base=NULL;IDirectDraw4*draw=NULL;Pair p;
    if(!create||FAILED(create(NULL,&base,NULL)))return FALSE;
    HRESULT hr=IDirectDraw_QueryInterface(base,&IID_IDirectDraw4,(void**)&draw);IDirectDraw_Release(base);if(FAILED(hr))return FALSE;
    BOOL ok=SUCCEEDED(IDirectDraw4_SetCooperativeLevel(draw,window,DDSCL_NORMAL));
    for(unsigned i=0;ok&&i<5;++i){ok=pair(draw,64,&p);if(!ok)break;
        hr=add(&p,3);if(FAILED(hr)){dispose(&p);ok=FALSE;break;}
        EnumContext c={.pair=&p};hr=IDirectDrawSurface7_EnumAttachedSurfaces((IDirectDrawSurface7*)p.color[i],&c,enumerated);
        graph("enum",versions[i],0,0,hr,c.found,c.count,c.ref);dispose(&p);}
    IDirectDraw4_Release(draw);return ok;
}
static HRESULT create_graph_surface(IUnknown *draw,unsigned version,DWORD caps,
        UINT width,UINT height,DWORD backs,IDirectDrawSurface7 **out)
{
    DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS;d.ddsCaps.dwCaps=caps;
    if(width){d.dwFlags|=DDSD_WIDTH|DDSD_HEIGHT;d.dwWidth=width;d.dwHeight=height;}
    if(backs){d.dwFlags|=DDSD_BACKBUFFERCOUNT;d.dwBackBufferCount=backs;}
    if(version==1){DDSURFACEDESC old;ZeroMemory(&old,sizeof(old));old.dwSize=sizeof(old);old.dwFlags=d.dwFlags;old.dwWidth=d.dwWidth;old.dwHeight=d.dwHeight;old.dwBackBufferCount=backs;old.ddsCaps.dwCaps=caps;
        return IDirectDraw_CreateSurface((IDirectDraw*)draw,&old,(IDirectDrawSurface**)out,NULL);}
    return IDirectDraw7_CreateSurface((IDirectDraw7*)draw,&d,out,NULL);
}
static void queries(const char *action,unsigned version,IDirectDrawSurface7 **nodes,unsigned count)
{
    const DWORD selectors[]={DDSCAPS_PRIMARYSURFACE,DDSCAPS_FRONTBUFFER,DDSCAPS_BACKBUFFER,DDSCAPS_FLIP,DDSCAPS_COMPLEX};
    for(unsigned n=0;n<count;++n)for(unsigned c=0;c<5;++c){DDSCAPS2 caps={selectors[c],0,0,{0}};IDirectDrawSurface7*s=NULL;
        HRESULT hr=IDirectDrawSurface7_GetAttachedSurface(nodes[n],&caps,&s);int found=-1;
        for(unsigned j=0;j<count;++j)if(nodes[j]==s)found=(int)j;
        graph(action,version,n,selectors[c],hr,found,0,0);if(s)IDirectDrawSurface7_Release(s);}
}
static BOOL flip_graph(HMODULE module,HWND window,unsigned version)
{
    HRESULT(WINAPI*create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreate");
    const GUID *iid=version==1?&IID_IDirectDraw:version==4?&IID_IDirectDraw4:&IID_IDirectDraw7;
    IDirectDraw *base=NULL;IUnknown *draw=NULL;IDirectDrawSurface7 *s[4]={NULL,NULL,NULL,NULL};BOOL ok=FALSE;
    if(!create||FAILED(create(NULL,&base,NULL)))return FALSE;
    HRESULT hr=IDirectDraw_QueryInterface(base,iid,(void**)&draw);IDirectDraw_Release(base);if(FAILED(hr))return FALSE;
    if(FAILED(IDirectDraw7_SetCooperativeLevel((IDirectDraw7*)draw,window,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)))goto done;
    if(FAILED(create_graph_surface(draw,version,DDSCAPS_PRIMARYSURFACE|DDSCAPS_COMPLEX|DDSCAPS_FLIP,0,0,2,&s[0])))goto done;
    DDSCAPS2 caps={DDSCAPS_BACKBUFFER,0,0,{0}};
    if(FAILED(IDirectDrawSurface7_GetAttachedSurface(s[0],&caps,&s[1])))goto done;
    caps.dwCaps=DDSCAPS_FLIP;if(FAILED(IDirectDrawSurface7_GetAttachedSurface(s[1],&caps,&s[2])))goto done;
    DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=version==1?sizeof(DDSURFACEDESC):sizeof(d);
    if(FAILED(IDirectDrawSurface7_GetSurfaceDesc(s[0],&d)))goto done;
    if(FAILED(create_graph_surface(draw,version,DDSCAPS_3DDEVICE,d.dwWidth,d.dwHeight,0,&s[3])))goto done;
    hr=IDirectDrawSurface7_AddAttachedSurface(s[0],s[3]);graph("complex_add",version,0,0,hr,-1,0,0);
    queries("complex",version,s,4);
    ULONG before=refs((IUnknown*)s[1]);hr=IDirectDrawSurface7_DeleteAttachedSurface(s[0],0,s[3]);ULONG after=refs((IUnknown*)s[1]);
    graph("complex_delete",version,0,0,hr,-1,before,after);
    while(after>before){IDirectDrawSurface7_Release(s[1]);--after;}
    for(unsigned i=1;i<4;++i){IDirectDrawSurface7_Release(s[i]);s[i]=NULL;}IDirectDrawSurface7_Release(s[0]);s[0]=NULL;
    if(FAILED(create_graph_surface(draw,version,DDSCAPS_PRIMARYSURFACE,0,0,0,&s[0])))goto done;
    if(FAILED(create_graph_surface(draw,version,DDSCAPS_OFFSCREENPLAIN,d.dwWidth,d.dwHeight,0,&s[1])))goto done;
    hr=IDirectDrawSurface7_AddAttachedSurface(s[1],s[0]);graph("reverse_add",version,0,0,hr,-1,0,0);
    queries("reverse",version,s,2);
    hr=IDirectDrawSurface7_DeleteAttachedSurface(s[1],0,s[0]);graph("reverse_delete",version,0,0,hr,-1,0,0);ok=TRUE;
done:
    for(unsigned i=1;i<4;++i)if(s[i])IDirectDrawSurface7_Release(s[i]);
    if(s[0])IDirectDrawSurface7_Release(s[0]);
    IDirectDraw7_SetCooperativeLevel((IDirectDraw7*)draw,NULL,DDSCL_NORMAL);IUnknown_Release(draw);return ok;
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,path,MAX_PATH);unsigned at=lstrlenA(path);while(at&&path[at-1]!=92)--at;path[at]=0;lstrcatA(path,"ATTREF.TXT");
    output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(2);
    HWND window=CreateWindowA("static","Attachment graph reference",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,320,240,NULL,NULL,GetModuleHandleA(NULL),NULL);
#ifdef DDRAW_ATTACHMENT_PROXY
    GetModuleFileNameA(NULL,path,MAX_PATH);at=lstrlenA(path);while(at&&path[at-1]!=92)--at;path[at]=0;lstrcatA(path,"ddraw.dll");
#else
    GetSystemDirectoryA(path,MAX_PATH);lstrcatA(path,"\\ddraw.dll");
#endif
    HMODULE module=LoadLibraryA(path);BOOL ok=window&&module&&enum_graph(module,window)&&flip_graph(module,window,1)&&flip_graph(module,window,4)&&flip_graph(module,window,7);
    line(ok?"COMPLETE attachment reference":"FAIL attachment graph setup");
    if(window)DestroyWindow(window);
    if(module)FreeLibrary(module);
    CloseHandle(output);ExitProcess(ok?0:1);
}
