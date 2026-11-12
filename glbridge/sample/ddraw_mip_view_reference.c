#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static void line(const char*s){DWORD n;WriteFile(output,s,lstrlenA(s),&n,NULL);WriteFile(output,"\r\n",2,&n,NULL);FlushFileBuffers(output);}
static HRESULT set_palette(void*s,UINT view,IDirectDrawPalette*p){
 switch(view){case 1:return IDirectDrawSurface_SetPalette((IDirectDrawSurface*)s,p);case 2:return IDirectDrawSurface2_SetPalette((IDirectDrawSurface2*)s,p);case 3:return IDirectDrawSurface3_SetPalette((IDirectDrawSurface3*)s,p);case 4:return IDirectDrawSurface4_SetPalette((IDirectDrawSurface4*)s,p);default:return IDirectDrawSurface7_SetPalette((IDirectDrawSurface7*)s,p);}}
static HRESULT set_key(void*s,UINT view,DDCOLORKEY*k){
 switch(view){case 1:return IDirectDrawSurface_SetColorKey((IDirectDrawSurface*)s,DDCKEY_SRCBLT,k);case 2:return IDirectDrawSurface2_SetColorKey((IDirectDrawSurface2*)s,DDCKEY_SRCBLT,k);case 3:return IDirectDrawSurface3_SetColorKey((IDirectDrawSurface3*)s,DDCKEY_SRCBLT,k);case 4:return IDirectDrawSurface4_SetColorKey((IDirectDrawSurface4*)s,DDCKEY_SRCBLT,k);default:return IDirectDrawSurface7_SetColorKey((IDirectDrawSurface7*)s,DDCKEY_SRCBLT,k);}}
void WINAPI WinMainCRTStartup(void){
 char path[MAX_PATH],text[400];GetModuleFileNameA(NULL,path,sizeof(path));UINT end=lstrlenA(path);while(end&&path[end-1]!='\\')--end;lstrcpyA(path+end,"MIPVIEW.TXT");output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(1);
 HMODULE module=LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");HRESULT(WINAPI*ex)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreateEx");HRESULT(WINAPI*legacy)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreate");
 IDirectDraw7*root=NULL;IDirectDraw4*old=NULL,*ex_old=NULL;IDirectDraw*first=NULL;if(!ex||!legacy||FAILED(ex(NULL,(void**)&root,&IID_IDirectDraw7,NULL))||FAILED(legacy(NULL,&first,NULL)))ExitProcess(2);
 if(FAILED(IDirectDraw_QueryInterface(first,&IID_IDirectDraw4,(void**)&old))||FAILED(IDirectDraw7_QueryInterface(root,&IID_IDirectDraw4,(void**)&ex_old)))ExitProcess(3);
 IDirectDraw_Release(first);IDirectDraw4_SetCooperativeLevel(old,NULL,DDSCL_NORMAL);IDirectDraw7_SetCooperativeLevel(root,NULL,DDSCL_NORMAL);
 static const UINT views[]={1,2,3,4,7};const IID*iids[]={&IID_IDirectDrawSurface,&IID_IDirectDrawSurface2,&IID_IDirectDrawSurface3,&IID_IDirectDrawSurface4,&IID_IDirectDrawSurface7};
 PALETTEENTRY entries[256];ZeroMemory(entries,sizeof(entries));
 for(UINT kind=0;kind<3;kind++){
  IDirectDrawPalette*palette=NULL;IDirectDraw4*factory=kind?ex_old:old;IDirectDraw4_CreatePalette(factory,DDPCAPS_8BIT|DDPCAPS_ALLOW256,entries,&palette,NULL);if(!palette)ExitProcess(4);
  DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_MIPMAPCOUNT;d.dwWidth=d.dwHeight=16;d.dwMipMapCount=2;d.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP;d.ddpfPixelFormat.dwSize=sizeof(d.ddpfPixelFormat);d.ddpfPixelFormat.dwFlags=DDPF_RGB|DDPF_PALETTEINDEXED8;d.ddpfPixelFormat.dwRGBBitCount=8;
  void*surface=NULL,*child=NULL;HRESULT hr=kind==2?IDirectDraw7_CreateSurface(root,&d,(IDirectDrawSurface7**)&surface,NULL):IDirectDraw4_CreateSurface(factory,&d,(IDirectDrawSurface4**)&surface,NULL);wsprintfA(text,"CREATE kind=%u hr=%08lX",kind,(DWORD)hr);line(text);if(FAILED(hr))ExitProcess(5);
  DDSCAPS2 caps;ZeroMemory(&caps,sizeof(caps));caps.dwCaps=DDSCAPS_COMPLEX;hr=kind==2?IDirectDrawSurface7_GetAttachedSurface((IDirectDrawSurface7*)surface,&caps,(IDirectDrawSurface7**)&child):IDirectDrawSurface4_GetAttachedSurface((IDirectDrawSurface4*)surface,&caps,(IDirectDrawSurface4**)&child);if(FAILED(hr)||!child)ExitProcess(6);
  for(UINT level=0;level<2;level++)for(UINT i=0;i<5;i++){
   void*iface=NULL;hr=IUnknown_QueryInterface((IUnknown*)(level?child:surface),iids[i],&iface);wsprintfA(text,"QI kind=%u level=%u view=%u hr=%08lX",kind,level,views[i],(DWORD)hr);line(text);if(FAILED(hr))continue;
   HRESULT empty=set_palette(iface,views[i],NULL),set=set_palette(iface,views[i],palette),clear=set_palette(iface,views[i],NULL);DDCOLORKEY key={0x22,0x22};HRESULT ks=set_key(iface,views[i],&key),kc=set_key(iface,views[i],NULL);
   wsprintfA(text,"VIEW kind=%u level=%u view=%u empty=%08lX set=%08lX clear=%08lX keyset=%08lX keyclear=%08lX",kind,level,views[i],(DWORD)empty,(DWORD)set,(DWORD)clear,(DWORD)ks,(DWORD)kc);line(text);IUnknown_Release((IUnknown*)iface);
  }
  IUnknown_Release((IUnknown*)child);IUnknown_Release((IUnknown*)surface);IDirectDrawPalette_Release(palette);
 }
 IDirectDraw4_Release(old);IDirectDraw4_Release(ex_old);IDirectDraw7_Release(root);FreeLibrary(module);line("PASS native mip view reference complete");CloseHandle(output);ExitProcess(0);
}
