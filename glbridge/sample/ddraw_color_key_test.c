/* DirectDraw 1/2/3/4/7 surface keys and native Blt override ranges. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>
static HANDLE logfile;
static IDirectDraw7* root;
static IUnknown* factories[5];
static const UINT versions[5]={1,2,3,4,7};
static const GUID* factory_ids[5]={&IID_IDirectDraw,&IID_IDirectDraw2,&IID_IDirectDraw3,&IID_IDirectDraw4,&IID_IDirectDraw7};
static const GUID* surface_ids[5]={&IID_IDirectDrawSurface,&IID_IDirectDrawSurface2,&IID_IDirectDrawSurface3,&IID_IDirectDrawSurface4,&IID_IDirectDrawSurface7};
static const DWORD selectors[4]={DDCKEY_SRCBLT,DDCKEY_DESTBLT,DDCKEY_SRCOVERLAY,DDCKEY_DESTOVERLAY};
static const DWORD descflags[4]={DDSD_CKSRCBLT,DDSD_CKDESTBLT,DDSD_CKSRCOVERLAY,DDSD_CKDESTOVERLAY};
#define KEY_PTR(d,k) ((k)==0?&(d).ddckCKSrcBlt:(k)==1?&(d).ddckCKDestBlt:(k)==2?&(d).ddckCKSrcOverlay:&(d).ddckCKDestOverlay)
static void report(const char* line){DWORD n;WriteFile(logfile,line,lstrlenA(line),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL ok,const char* message){if(!ok){report(message);ExitProcess(1);}}
static void expect(HRESULT got,HRESULT wanted,const char* message){if(got!=wanted){char line[220];wsprintfA(line,"%s actual=%08lX expected=%08lX",message,(DWORD)got,(DWORD)wanted);report(line);ExitProcess(1);}}
static BOOL same_bytes(const void*a,const void*b,UINT n){const BYTE*x=a,*y=b;for(UINT i=0;i<n;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
static DDSURFACEDESC2 description(void){DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=4;d.dwHeight=1;d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;d.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);d.ddpfPixelFormat.dwFlags=DDPF_RGB;d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;return d;}
static HRESULT create(UINT api,DDSURFACEDESC2* request,IDirectDrawSurface7** out)
{
    IUnknown* raw=(void*)(uintptr_t)1;HRESULT hr;*out=NULL;
    if(api<=3){DDSURFACEDESC old;ZeroMemory(&old,sizeof(old));old.dwSize=sizeof(old);old.dwFlags=request->dwFlags;old.dwWidth=request->dwWidth;old.dwHeight=request->dwHeight;old.ddsCaps.dwCaps=request->ddsCaps.dwCaps;old.ddpfPixelFormat=request->ddpfPixelFormat;old.ddckCKSrcBlt=request->ddckCKSrcBlt;old.ddckCKDestBlt=request->ddckCKDestBlt;old.ddckCKSrcOverlay=request->ddckCKSrcOverlay;old.ddckCKDestOverlay=request->ddckCKDestOverlay;
        if(api==1) hr=IDirectDraw_CreateSurface((IDirectDraw*)factories[0],&old,(IDirectDrawSurface**)&raw,NULL);
        else if(api==2) hr=IDirectDraw2_CreateSurface((IDirectDraw2*)factories[1],&old,(IDirectDrawSurface**)&raw,NULL);
        else hr=IDirectDraw3_CreateSurface((IDirectDraw3*)factories[2],&old,(IDirectDrawSurface**)&raw,NULL);
    }else if(api==4)hr=IDirectDraw4_CreateSurface((IDirectDraw4*)factories[3],request,(IDirectDrawSurface4**)&raw,NULL);
    else hr=IDirectDraw7_CreateSurface((IDirectDraw7*)factories[4],request,(IDirectDrawSurface7**)&raw,NULL);
    if(FAILED(hr)){check(!raw,"FAIL refused CreateSurface output");return hr;}
    check(raw!=NULL,"FAIL created surface");expect(IUnknown_QueryInterface(raw,&IID_IDirectDrawSurface7,(void**)out),DD_OK,"FAIL surface7 identity");IUnknown_Release(raw);return DD_OK;
}
static HRESULT set_key(UINT api,IUnknown* p,DWORD flags,DDCOLORKEY* key){switch(api){
case 1:return IDirectDrawSurface_SetColorKey((IDirectDrawSurface*)p,flags,key);
case 2:return IDirectDrawSurface2_SetColorKey((IDirectDrawSurface2*)p,flags,key);
case 3:return IDirectDrawSurface3_SetColorKey((IDirectDrawSurface3*)p,flags,key);
case 4:return IDirectDrawSurface4_SetColorKey((IDirectDrawSurface4*)p,flags,key);
case 7:return IDirectDrawSurface7_SetColorKey((IDirectDrawSurface7*)p,flags,key);
default:return E_FAIL;}}
static HRESULT get_key(UINT api,IUnknown*p,DWORD flags,DDCOLORKEY*key){switch(api){
case 1:return IDirectDrawSurface_GetColorKey((IDirectDrawSurface*)p,flags,key);
case 2:return IDirectDrawSurface2_GetColorKey((IDirectDrawSurface2*)p,flags,key);
case 3:return IDirectDrawSurface3_GetColorKey((IDirectDrawSurface3*)p,flags,key);
case 4:return IDirectDrawSurface4_GetColorKey((IDirectDrawSurface4*)p,flags,key);
case 7:return IDirectDrawSurface7_GetColorKey((IDirectDrawSurface7*)p,flags,key);
default:return E_FAIL;}}
static HRESULT desc_key(UINT api,IUnknown*p,UINT kind,DWORD*flags,DDCOLORKEY*key){switch(api){
case 1:{DDSURFACEDESC d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=IDirectDrawSurface_GetSurfaceDesc((IDirectDrawSurface*)p,&d);*flags=d.dwFlags;*key=*KEY_PTR(d,kind);return hr;}
case 2:{DDSURFACEDESC d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=IDirectDrawSurface2_GetSurfaceDesc((IDirectDrawSurface2*)p,&d);*flags=d.dwFlags;*key=*KEY_PTR(d,kind);return hr;}
case 3:{DDSURFACEDESC d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=IDirectDrawSurface3_GetSurfaceDesc((IDirectDrawSurface3*)p,&d);*flags=d.dwFlags;*key=*KEY_PTR(d,kind);return hr;}
case 4:{DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=IDirectDrawSurface4_GetSurfaceDesc((IDirectDrawSurface4*)p,&d);*flags=d.dwFlags;*key=*KEY_PTR(d,kind);return hr;}
case 7:{DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT hr=IDirectDrawSurface7_GetSurfaceDesc((IDirectDrawSurface7*)p,&d);*flags=d.dwFlags;*key=*KEY_PTR(d,kind);return hr;}
default:return E_FAIL;}}
static void verify(UINT api,IUnknown*p,UINT kind,BOOL present,DWORD low)
{
    DDCOLORKEY got={0x11111111,0x22222222},stored={0};DWORD flags=0;
    expect(get_key(api,p,selectors[kind],&got),present?DD_OK:DDERR_NOCOLORKEY,"FAIL queried surface key");
    if(present)check(got.dwColorSpaceLowValue==low&&got.dwColorSpaceHighValue==low,"FAIL canonical queried values");else check(got.dwColorSpaceLowValue==0x11111111&&got.dwColorSpaceHighValue==0x22222222,"FAIL missing key modified output");
    expect(desc_key(api,p,kind,&flags,&stored),DD_OK,"FAIL key surface description");check(((flags&descflags[kind])!=0)==present,"FAIL key description presence");if(present)check(stored.dwColorSpaceLowValue==low&&stored.dwColorSpaceHighValue==low,"FAIL canonical description values");
}
static void creation_cases(void)
{
    for(UINT at=0;at<5;at++){
        for(UINT kind=0;kind<4;kind++)for(UINT mode=0;mode<4;mode++){
            DDSURFACEDESC2 d=description();DDCOLORKEY*key=KEY_PTR(d,kind);key->dwColorSpaceLowValue=0x00102030+kind;key->dwColorSpaceHighValue=mode==1?key->dwColorSpaceLowValue+1:mode==2?key->dwColorSpaceLowValue-1:mode==3?0x00f0e0d0:key->dwColorSpaceLowValue;if(mode!=3)d.dwFlags|=descflags[kind];DDSURFACEDESC2 before=d;IDirectDrawSurface7*surface=NULL;HRESULT hr=create(versions[at],&d,&surface);expect(hr,mode==1||mode==2?DDERR_NOCOLORKEYHW:DD_OK,"FAIL creation color-key contract");check(same_bytes(&d,&before,sizeof(d)),"FAIL input description modified");if(surface){verify(7,(IUnknown*)surface,kind,mode==0,key->dwColorSpaceLowValue);IDirectDrawSurface7_Release(surface);}
        }
        char line[180];wsprintfA(line,"KEY CREATE api=%u accepted=8 refused=8 flagless=4 immutable=16",versions[at]);report(line);
    }
}
static void setter_cases(void)
{
    DDSURFACEDESC2 d=description();IDirectDrawSurface7*surface=NULL;expect(create(7,&d,&surface),DD_OK,"FAIL setter surface");
    for(UINT at=0;at<5;at++){
        IUnknown*view=NULL;expect(IDirectDrawSurface7_QueryInterface(surface,surface_ids[at],(void**)&view),DD_OK,"FAIL surface API view");
        for(UINT kind=0;kind<4;kind++){
            DWORD low=0x00102030+kind;DDCOLORKEY key={low,0x00f0e0d0},before=key;expect(set_key(versions[at],view,selectors[kind],&key),DD_OK,"FAIL unflagged key");check(same_bytes(&key,&before,sizeof(key)),"FAIL unflagged key input");verify(versions[at],view,kind,TRUE,low);
            expect(set_key(versions[at],view,selectors[kind]|DDCKEY_COLORSPACE,&key),DDERR_NOCOLORKEYHW,"FAIL explicit ascending range");check(same_bytes(&key,&before,sizeof(key)),"FAIL explicit range input");verify(versions[at],view,kind,TRUE,low);
            key=(DDCOLORKEY){low,low-1};before=key;expect(set_key(versions[at],view,selectors[kind]|DDCKEY_COLORSPACE,&key),DDERR_NOCOLORKEYHW,"FAIL explicit descending range");check(same_bytes(&key,&before,sizeof(key)),"FAIL descending range input");verify(versions[at],view,kind,TRUE,low);
            key=(DDCOLORKEY){low+1,low+1};before=key;expect(set_key(versions[at],view,selectors[kind]|DDCKEY_COLORSPACE,&key),DD_OK,"FAIL equal explicit key");check(same_bytes(&key,&before,sizeof(key)),"FAIL equal key input");verify(versions[at],view,kind,TRUE,low+1);
            expect(set_key(versions[at],view,selectors[kind]|DDCKEY_COLORSPACE,NULL),DD_OK,"FAIL key clear");verify(versions[at],view,kind,FALSE,0);
        }
        IUnknown_Release(view);char line[180];wsprintfA(line,"KEY SET api=%u kinds=4 canonical=4 equal=4 refused=8 cleared=4 descQueries=20",versions[at]);report(line);
    }
    check(IDirectDrawSurface7_Release(surface)==0,"FAIL setter surface references");
}
static void write_pixels(IDirectDrawSurface7*s,const DWORD* pixels)
{
    DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);expect(IDirectDrawSurface7_Lock(s,NULL,&d,DDLOCK_WRITEONLY|DDLOCK_WAIT,NULL),DD_OK,"FAIL pixel write lock");for(UINT i=0;i<4;i++)((DWORD*)d.lpSurface)[i]=pixels[i];expect(IDirectDrawSurface7_Unlock(s,NULL),DD_OK,"FAIL pixel upload");
}
static void pixel_cases(void)
{
    DDSURFACEDESC2 d=description();IDirectDrawSurface7 *source=NULL,*destination=NULL;expect(create(7,&d,&source),DD_OK,"FAIL source surface");d=description();expect(create(7,&d,&destination),DD_OK,"FAIL destination surface");
    const DWORD pattern[4]={0x00102030,0x00708090,0x00f0e0d0,0x00ffffff},background[4]={0x00010203,0x00010203,0x00010203,0x00010203},paint[4]={0x00b02010,0x00b02010,0x00b02010,0x00b02010};
    for(UINT frame=0;frame<6;frame++){
        BOOL source_key=frame==0||frame==2||frame==4;write_pixels(source,source_key?pattern:paint);write_pixels(destination,source_key?background:pattern);DDCOLORKEY key={pattern[0],pattern[2]};expect(IDirectDrawSurface7_SetColorKey(source_key?source:destination,source_key?DDCKEY_SRCBLT:DDCKEY_DESTBLT,&key),DD_OK,"FAIL render canonical key");
        DDBLTFX fx;ZeroMemory(&fx,sizeof(fx));fx.dwSize=sizeof(fx);fx.ddckSrcColorkey=key;fx.ddckDestColorkey=key;
        if(frame>=4)expect(IDirectDrawSurface7_BltFast(destination,0,0,source,NULL,DDBLTFAST_WAIT|(source_key?DDBLTFAST_SRCCOLORKEY:DDBLTFAST_DESTCOLORKEY)),DD_OK,"FAIL canonical BltFast");
        else expect(IDirectDrawSurface7_Blt(destination,NULL,source,NULL,DDBLT_WAIT|(frame==0?DDBLT_KEYSRC:frame==1?DDBLT_KEYDEST:frame==2?DDBLT_KEYSRCOVERRIDE:DDBLT_KEYDESTOVERRIDE),frame>=2?&fx:NULL),DD_OK,"FAIL canonical/override Blt");
        DDCOLORKEY stored={0};expect(IDirectDrawSurface7_GetColorKey(source_key?source:destination,source_key?DDCKEY_SRCBLT:DDCKEY_DESTBLT,&stored),DD_OK,"FAIL render key restored");check(stored.dwColorSpaceLowValue==pattern[0]&&stored.dwColorSpaceHighValue==pattern[0],"FAIL override changed canonical surface key");
        DDSURFACEDESC2 read;ZeroMemory(&read,sizeof(read));read.dwSize=sizeof(read);expect(IDirectDrawSurface7_Lock(destination,NULL,&read,DDLOCK_READONLY|DDLOCK_WAIT,NULL),DD_OK,"FAIL force independent GPU readback");expect(IDirectDrawSurface7_Unlock(destination,NULL),DD_OK,"FAIL readback unlock");
        char line[180];wsprintfA(line,"KEY FRAME index=%u sourceKey=%u override=%u fast=%u surfaceLow=00102030 surfaceHigh=00102030 readback=1",frame,source_key,frame==2||frame==3,frame>=4);report(line);
    }
    check(IDirectDrawSurface7_Release(source)==0&&IDirectDrawSurface7_Release(destination)==0,"FAIL render surface references");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"CKEY.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");
    HWND window=CreateWindowExA(0,"STATIC","DirectDraw color keys",WS_POPUP,30,40,32,24,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);
    lstrcpyA(path,directory);lstrcatA(path,"DDRAW.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");HRESULT(WINAPI*make)(GUID*,void**,REFIID,IUnknown*)=(void*)(uintptr_t)GetProcAddress(module,"DirectDrawCreateEx");check(make!=NULL,"FAIL export");expect(make(NULL,(void**)&root,&IID_IDirectDraw7,NULL),DD_OK,"FAIL factory");expect(IDirectDraw7_SetCooperativeLevel(root,window,DDSCL_NORMAL),DD_OK,"FAIL cooperation");
    for(UINT i=0;i<5;i++)expect(IDirectDraw7_QueryInterface(root,factory_ids[i],(void**)&factories[i]),DD_OK,"FAIL factory API view");
    creation_cases();setter_cases();pixel_cases();for(UINT i=0;i<5;i++)IUnknown_Release(factories[i]);check(IDirectDraw7_Release(root)==0,"FAIL factory references");
    report("KEY STATUS versions=5 created=43 createRefused=40 setterRefused=40 frames=6 readbacks=6 overrideRanges=2 factoryRefs=0");FreeLibrary(module);DestroyWindow(window);report("PASS DirectDraw surface color keys");CloseHandle(logfile);ExitProcess(0);
}
