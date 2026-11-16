/* Independent original XP D3D7 RGB empty draws and clear parameter reference. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE output;
static void line(const char *s){DWORD n;WriteFile(output,s,lstrlenA(s),&n,NULL);WriteFile(output,"\r\n",2,&n,NULL);FlushFileBuffers(output);}
static void result(const char*name,UINT index,HRESULT hr){char s[180];wsprintfA(s,"CALL name=%s index=%u hr=%08lX",name,index,(DWORD)hr);line(s);}
#define TRY(x) do{HRESULT status=(x);if(FAILED(status)){result("setup",__LINE__,status);return FALSE;}}while(0)
static BOOL reference(HMODULE module,HWND window){
 HRESULT(WINAPI*create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreateEx");IDirectDraw7*dd=NULL;IDirect3D7*d3d=NULL;IDirectDrawSurface7*rt=NULL;IDirect3DDevice7*dev=NULL;IDirect3DVertexBuffer7*vb=NULL;
 TRY(create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL));TRY(IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL));TRY(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void**)&d3d));DDSURFACEDESC2 desc;ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;desc.dwWidth=desc.dwHeight=32;desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_SYSTEMMEMORY;desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;TRY(IDirectDraw7_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev));D3DVIEWPORT7 vp={0,0,32,32,0,1};TRY(IDirect3DDevice7_SetViewport(dev,&vp));TRY(IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_LIGHTING,FALSE));
 D3DVERTEXBUFFERDESC vd={sizeof(vd),D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZ,4};TRY(IDirect3D7_CreateVertexBuffer(d3d,&vd,&vb,0));float vertices[4][3]={{-1,-1,0},{-1,1,0},{1,-1,0},{1,1,0}};void*data=NULL;TRY(IDirect3DVertexBuffer7_Lock(vb,0,&data,NULL));CopyMemory(data,vertices,sizeof(vertices));TRY(IDirect3DVertexBuffer7_Unlock(vb));WORD indices[4]={0,1,2,3};D3DDRAWPRIMITIVESTRIDEDDATA strided;ZeroMemory(&strided,sizeof(strided));
 /* Only safe empty streams: either vertex_count or index_count is zero. */
 for(UINT state=0;state<1;state++){
  if(state)TRY(IDirect3DDevice7_BeginScene(dev));
  const DWORD fvfs[]={D3DFVF_XYZ,0,D3DFVF_XYZ|0x80000000u,1,D3DFVF_XYZRHW|D3DFVF_NORMAL,D3DFVF_XYZ|0x1000u,D3DFVF_XYZ|0x8000u};
  for(UINT kind=0;kind<7;kind++){DWORD fvf=fvfs[kind];
   for(UINT type=1;type<=7;type++){UINT id=state*100+kind*10+type;result("up",id,IDirect3DDevice7_DrawPrimitive(dev,type,fvf,NULL,0,0));result("strided",id,IDirect3DDevice7_DrawPrimitiveStrided(dev,type,fvf,&strided,0,0));result("indexed",id,IDirect3DDevice7_DrawIndexedPrimitive(dev,type,fvf,NULL,0,NULL,0,0));result("istrided",id,IDirect3DDevice7_DrawIndexedPrimitiveStrided(dev,type,fvf,&strided,0,NULL,0,0));}
  }
  for(UINT type=1;type<=7;type++){
   UINT id=state*100+type;result("vb",id,IDirect3DDevice7_DrawPrimitiveVB(dev,type,vb,0,0,0));result("ivb",id,IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,type,vb,0,0,NULL,0,0));
   result("indexed_v0",id,IDirect3DDevice7_DrawIndexedPrimitive(dev,type,D3DFVF_XYZ,NULL,0,indices,4,0));result("indexed_i0",id,IDirect3DDevice7_DrawIndexedPrimitive(dev,type,D3DFVF_XYZ,vertices,4,NULL,0,0));
   result("istrided_v0",id,IDirect3DDevice7_DrawIndexedPrimitiveStrided(dev,type,D3DFVF_XYZ,&strided,0,indices,4,0));strided.position.lpvData=vertices;strided.position.dwStride=12;result("istrided_i0",id,IDirect3DDevice7_DrawIndexedPrimitiveStrided(dev,type,D3DFVF_XYZ,&strided,4,NULL,0,0));strided.position.lpvData=NULL;
   result("ivb_v0",id,IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,type,vb,0,0,indices,4,0));result("ivb_i0",id,IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,type,vb,0,4,NULL,0,0));
  }
  /* Null VB object pointers are outside this safe empty-stream matrix. */
  if(state)TRY(IDirect3DDevice7_EndScene(dev));
 }
 /* Keep Clear observations independent of negative empty-format calls. */
 IDirect3DDevice7_Release(dev);dev=NULL;TRY(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DRGBDevice,rt,&dev));TRY(IDirect3DDevice7_SetViewport(dev,&vp));
 D3DRECT rectangle={{0},{0},{32},{32}};const DWORD counts[]={0,0,1,0xffffffffu,1,0,0};const BOOL present[]={FALSE,TRUE,FALSE,FALSE,TRUE,FALSE,FALSE};const DWORD flags[]={D3DCLEAR_TARGET,D3DCLEAR_TARGET,D3DCLEAR_TARGET,D3DCLEAR_TARGET,D3DCLEAR_TARGET,0,D3DCLEAR_TARGET|0x80000000u};
 for(UINT i=0;i<7;i++){TRY(IDirect3DDevice7_Clear(dev,0,NULL,D3DCLEAR_TARGET,0xffffff,1,0));HRESULT hr=IDirect3DDevice7_Clear(dev,counts[i],present[i]?&rectangle:NULL,flags[i],0x00ff00,1,0);result("clear",i,hr);ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);TRY(IDirectDrawSurface7_Lock(rt,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL));DWORD rgb=*(DWORD*)((BYTE*)desc.lpSurface+16*desc.lPitch+16*4)&0xffffff;char s[120];wsprintfA(s,"PIXEL clear=%u rgb=%06lX",i,rgb);line(s);TRY(IDirectDrawSurface7_Unlock(rt,NULL));}
 IDirect3DVertexBuffer7_Release(vb);IDirect3DDevice7_Release(dev);IDirectDrawSurface7_Release(rt);IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);line("COMPLETE empty=1");return TRUE;
}
void WINAPI WinMainCRTStartup(void){char p[MAX_PATH];GetModuleFileNameA(NULL,p,sizeof(p));UINT at=lstrlenA(p);while(at&&p[at-1]!='\\')--at;p[at]=0;lstrcatA(p,"EMPTY.TXT");output=CreateFileA(p,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(2);GetSystemDirectoryA(p,sizeof(p));lstrcatA(p,"\\ddraw.dll");HMODULE module=LoadLibraryA(p);HWND window=CreateWindowExA(0,"STATIC","Native empty draw reference",WS_OVERLAPPEDWINDOW,0,0,128,128,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL ok=module&&reference(module,window);if(!ok)line("COMPLETE empty=0");DestroyWindow(window);if(module)FreeLibrary(module);CloseHandle(output);ExitProcess(ok?0:1);}
