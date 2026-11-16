/* Independent XP DirectDraw factory policy and allocation metadata. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static void line(const char *s){DWORD n;WriteFile(output,s,lstrlenA(s),&n,NULL);WriteFile(output,"\r\n",2,&n,NULL);FlushFileBuffers(output);}
static BOOL reference(HMODULE module){
 HRESULT(WINAPI*create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreateEx");
 for(unsigned flavor=0;flavor<3;flavor++){
  IDirectDraw7*d=NULL;char text[400];GUID*guid=flavor==1?(GUID*)DDCREATE_HARDWAREONLY:flavor==2?(GUID*)DDCREATE_EMULATIONONLY:NULL;
  HRESULT hr=create(guid,(void**)&d,&IID_IDirectDraw7,NULL);wsprintfA(text,"FACTORY flavor=%u hr=%08lX",flavor,(DWORD)hr);line(text);if(FAILED(hr))continue;
  hr=IDirectDraw7_SetCooperativeLevel(d,NULL,DDSCL_NORMAL);if(FAILED(hr)){IDirectDraw7_Release(d);return FALSE;}
  DDCAPS hal,hel;ZeroMemory(&hal,sizeof(hal));ZeroMemory(&hel,sizeof(hel));hal.dwSize=sizeof(hal);hel.dwSize=sizeof(hel);hr=IDirectDraw7_GetCaps(d,&hal,&hel);
  wsprintfA(text,"CAPS flavor=%u hr=%08lX hal=%08lX hel=%08lX hz=%08lX sz=%08lX hflags=%08lX sflags=%08lX",flavor,(DWORD)hr,hal.ddsCaps.dwCaps,hel.ddsCaps.dwCaps,hal.dwZBufferBitDepths,hel.dwZBufferBitDepths,hal.dwCaps,hel.dwCaps);line(text);
  for(unsigned depth=0;depth<4;depth++)for(unsigned place=0;place<3;place++){
   DDSURFACEDESC2 desc;ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;desc.dwWidth=desc.dwHeight=16;desc.ddsCaps.dwCaps=(depth?DDSCAPS_ZBUFFER:DDSCAPS_OFFSCREENPLAIN)|(place==1?DDSCAPS_SYSTEMMEMORY:place==2?DDSCAPS_VIDEOMEMORY:0);desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);
   if(depth){desc.ddpfPixelFormat.dwFlags=DDPF_ZBUFFER;desc.ddpfPixelFormat.dwZBufferBitDepth=depth==1?16:depth==2?24:32;desc.ddpfPixelFormat.dwZBitMask=depth==1?0xffff:depth==2?0xffffff:0xffffffffu;}
   else{desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;}
   IDirectDrawSurface7*s=NULL;hr=IDirectDraw7_CreateSurface(d,&desc,&s,NULL);DWORD caps=0,bpp=0,width=0,height=0;LONG pitch=0;
   if(SUCCEEDED(hr)){ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);HRESULT queried=IDirectDrawSurface7_GetSurfaceDesc(s,&desc);if(FAILED(queried)){IDirectDrawSurface7_Release(s);IDirectDraw7_Release(d);return FALSE;}caps=desc.ddsCaps.dwCaps;bpp=desc.ddpfPixelFormat.dwRGBBitCount;width=desc.dwWidth;height=desc.dwHeight;pitch=desc.lPitch;IDirectDrawSurface7_Release(s);}
   wsprintfA(text,"SURFACE flavor=%u depth=%u place=%u hr=%08lX caps=%08lX bpp=%lu width=%lu height=%lu pitch=%ld",flavor,depth,place,(DWORD)hr,caps,bpp,width,height,pitch);line(text);
  }
  IDirectDraw7_Release(d);
 }
 line("COMPLETE factory_reference=1");return TRUE;
}
void WINAPI WinMainCRTStartup(void){char path[MAX_PATH];GetModuleFileNameA(NULL,path,sizeof(path));UINT at=lstrlenA(path);while(at&&path[at-1]!='\\')--at;path[at]=0;lstrcatA(path,"FACTORY.TXT");output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(path,sizeof(path));lstrcatA(path,"\\ddraw.dll");HMODULE module=LoadLibraryA(path);BOOL ok=module&&reference(module);if(!ok)line("COMPLETE factory_reference=0");if(module)FreeLibrary(module);CloseHandle(output);ExitProcess(ok?0:1);}
