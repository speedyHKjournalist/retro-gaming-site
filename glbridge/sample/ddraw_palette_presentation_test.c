/* Palette animation with no following drawing call. Uses the bridge's small
 * virtual fullscreen mode; it is not native HAL mode validation. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>
static HANDLE logfile;
static IUnknown*factories[5];
static const UINT versions[]={1,2,3,4,7};
static const GUID*ids[]={&IID_IDirectDraw,&IID_IDirectDraw2,&IID_IDirectDraw3,&IID_IDirectDraw4,&IID_IDirectDraw7};
static void report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL ok,const char*s){if(!ok){report(s);ExitProcess(1);}}
static void expect(HRESULT hr,HRESULT wanted,const char*s){if(hr!=wanted){char text[180];wsprintfA(text,"%s actual=%08lX expected=%08lX",s,(DWORD)hr,(DWORD)wanted);report(text);ExitProcess(1);}}
static PALETTEENTRY color(UINT api,UINT index,UINT step){PALETTEENTRY p={(BYTE)(32+api*16+step*8),(BYTE)(32+index*96),(BYTE)(16+step*64),0};return p;}
static IDirectDrawPalette*palette(UINT at,PALETTEENTRY*entries){IDirectDrawPalette*p=NULL;DWORD flags=DDPCAPS_8BIT|DDPCAPS_ALLOW256;HRESULT hr=at==0?IDirectDraw_CreatePalette((IDirectDraw*)factories[0],flags,entries,&p,NULL):at==1?IDirectDraw2_CreatePalette((IDirectDraw2*)factories[1],flags,entries,&p,NULL):at==2?IDirectDraw3_CreatePalette((IDirectDraw3*)factories[2],flags,entries,&p,NULL):at==3?IDirectDraw4_CreatePalette((IDirectDraw4*)factories[3],flags,entries,&p,NULL):IDirectDraw7_CreatePalette((IDirectDraw7*)factories[4],flags,entries,&p,NULL);expect(hr,DD_OK,"FAIL palette creation");return p;}
static DWORD packed(PALETTEENTRY p){return 0xff000000|((DWORD)p.peRed<<16)|((DWORD)p.peGreen<<8)|p.peBlue;}
static void frame(UINT api,UINT step,PALETTEENTRY a,PALETTEENTRY b){char text[180];wsprintfA(text,"PALETTE FRAME api=%u step=%u zero=%08lX one=%08lX drawAfter=0",api,step,packed(a),packed(b));report(text);}
static void cases(IDirectDrawSurface7*primary,UINT at)
{
    UINT api=versions[at];PALETTEENTRY table[256],before[2],got[2];ZeroMemory(table,sizeof(table));table[255]=(PALETTEENTRY){9,10,11,0};IDirectDrawPalette*p=palette(at,table),*unbound=palette(at,table);expect(IDirectDrawSurface7_SetPalette(primary,p),DD_OK,"FAIL palette binding");
    table[0]=color(api,0,0);table[1]=color(api,1,0);before[0]=table[0];before[1]=table[1];expect(IDirectDrawPalette_SetEntries(p,0,0,2,table),DD_OK,"FAIL immediate palette update");check(table[0].peRed==before[0].peRed&&table[1].peRed==before[1].peRed,"FAIL palette input modified");frame(api,0,table[0],table[1]);
    table[1]=color(api,1,1);expect(IDirectDrawPalette_SetEntries(p,0,1,1,&table[1]),DD_OK,"FAIL partial palette update");frame(api,1,table[0],table[1]);expect(IDirectDrawPalette_GetEntries(p,0,0,2,got),DD_OK,"FAIL palette query");check(packed(got[0])==packed(table[0])&&packed(got[1])==packed(table[1]),"FAIL palette retained entries");PALETTEENTRY last;expect(IDirectDrawPalette_GetEntries(p,0,255,1,&last),DD_OK,"FAIL palette untouched last");check(last.peRed==9&&last.peGreen==10&&last.peBlue==11,"FAIL partial update changed unrelated entry");
    expect(IDirectDrawPalette_SetEntries(unbound,0,0,1,table),DD_OK,"FAIL unbound update");expect(IDirectDrawPalette_SetEntries(p,0,256,1,table),DDERR_INVALIDPARAMS,"FAIL invalid range");expect(IDirectDrawPalette_SetEntries(p,0,0,0,NULL),DDERR_INVALIDPARAMS,"FAIL null entries");expect(IDirectDrawPalette_SetEntries(p,0,256,0,table),DD_OK,"FAIL empty end range");frame(api,2,table[0],table[1]);
    IDirectDrawPalette_Release(unbound);IDirectDrawPalette_Release(p);char text[180];wsprintfA(text,"PALETTE API api=%u visibleUpdates=3 unboundUpdates=1 refusals=2 textureUploads=0",api);report(text);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"PALT.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Palette presentation",WS_POPUP,30,40,32,24,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"FAIL window");ShowWindow(window,SW_SHOW);lstrcpyA(path,directory);lstrcatA(path,"DDRAW.DLL");HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL DLL");HRESULT(WINAPI*make)(GUID*,void**,REFIID,IUnknown*)=(void*)(uintptr_t)GetProcAddress(module,"DirectDrawCreateEx");check(make!=NULL,"FAIL export");IDirectDraw7*root=NULL;expect(make(NULL,(void**)&root,&IID_IDirectDraw7,NULL),DD_OK,"FAIL factory");expect(IDirectDraw7_SetCooperativeLevel(root,window,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN),DD_OK,"FAIL cooperation");expect(IDirectDraw7_SetDisplayMode(root,32,24,8,0,0),DD_OK,"FAIL virtual mode");for(UINT i=0;i<5;i++)expect(IDirectDraw7_QueryInterface(root,ids[i],(void**)&factories[i]),DD_OK,"FAIL factory view");
    DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS;d.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;IDirectDrawSurface7*primary=NULL;expect(IDirectDraw7_CreateSurface(root,&d,&primary,NULL),DD_OK,"FAIL P8 primary");PALETTEENTRY table[256];ZeroMemory(table,sizeof(table));table[0]=color(0,0,0);table[1]=color(0,1,0);IDirectDrawPalette*seed=palette(4,table);expect(IDirectDrawSurface7_SetPalette(primary,seed),DD_OK,"FAIL seed palette");ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);expect(IDirectDrawSurface7_Lock(primary,NULL,&d,DDLOCK_WRITEONLY|DDLOCK_WAIT,NULL),DD_OK,"FAIL initial indices");check(d.dwWidth==32&&d.dwHeight==24,"FAIL virtual primary extent");for(UINT y=0;y<24;y++)for(UINT x=0;x<32;x++)*((BYTE*)d.lpSurface+y*d.lPitch+x)=(BYTE)((x+y)&1);expect(IDirectDrawSurface7_Unlock(primary,NULL),DD_OK,"FAIL initial presentation");frame(0,0,table[0],table[1]);IDirectDrawPalette_Release(seed);
    for(UINT i=0;i<5;i++){cases(primary,i);
    }check(IDirectDrawSurface7_Release(primary)==0,"FAIL primary references");for(UINT i=0;i<5;i++){IUnknown_Release(factories[i]);
    }check(IDirectDraw7_Release(root)==0,"FAIL factory references");FreeLibrary(module);DestroyWindow(window);report("PALETTE STATUS versions=5 palettes=11 frames=16 indexUploads=1 surfaceRefs=0 factoryRefs=0");report("PASS DirectDraw palette presentation");CloseHandle(logfile);ExitProcess(0);
}
