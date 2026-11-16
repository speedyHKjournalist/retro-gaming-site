#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static UINT api;
static void line(const char *text) { DWORD written; WriteFile(output,text,lstrlenA(text),&written,NULL); WriteFile(output,"\r\n",2,&written,NULL); FlushFileBuffers(output); }
static HRESULT create(IDirectDraw7 *root,IDirectDraw4 *old,DDSURFACEDESC2 *d,void **surface) { return api==4?IDirectDraw4_CreateSurface(old,d,(IDirectDrawSurface4**)surface,NULL):IDirectDraw7_CreateSurface(root,d,(IDirectDrawSurface7**)surface,NULL); }
static HRESULT describe(void *s,DDSURFACEDESC2 *d) { return api==4?IDirectDrawSurface4_GetSurfaceDesc((IDirectDrawSurface4*)s,d):IDirectDrawSurface7_GetSurfaceDesc((IDirectDrawSurface7*)s,d); }
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH],text[300];GetModuleFileNameA(NULL,path,sizeof(path));UINT end=lstrlenA(path);while(end&&path[end-1]!='\\')--end;lstrcpyA(path+end,"BLOCK.TXT");output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(output==INVALID_HANDLE_VALUE)ExitProcess(1);
    HMODULE module=LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");HRESULT(WINAPI*factory)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreateEx");IDirectDraw7*root=NULL;IDirectDraw4*old=NULL;if(!factory||FAILED(factory(NULL,(void**)&root,&IID_IDirectDraw7,NULL)))ExitProcess(2);IDirectDraw7_SetCooperativeLevel(root,NULL,DDSCL_NORMAL);if(FAILED(IDirectDraw7_QueryInterface(root,&IID_IDirectDraw4,(void**)&old)))ExitProcess(3);
    static const DWORD formats[]={0x31545844u,0x35545844u};
    static const struct{DWORD flags,value;int rel;}cases[]={
      {DDSD_LINEARSIZE,0,0},{DDSD_LINEARSIZE,2,0},{DDSD_PITCH,0,0},{DDSD_PITCH,1,0},
      {DDSD_LPSURFACE,0,0},{DDSD_LPSURFACE|DDSD_LINEARSIZE,0,0},{DDSD_LPSURFACE|DDSD_LINEARSIZE,1,0},
      {DDSD_LPSURFACE|DDSD_LINEARSIZE,2,0},{DDSD_LPSURFACE|DDSD_LINEARSIZE,2,1},{DDSD_LPSURFACE|DDSD_LINEARSIZE,2,-1},
      {DDSD_LPSURFACE|DDSD_PITCH,0,0},{DDSD_LPSURFACE|DDSD_PITCH,1,0},{DDSD_LPSURFACE|DDSD_PITCH,2,0},{DDSD_LPSURFACE|DDSD_PITCH|DDSD_LINEARSIZE,2,0}};
    union{DWORD align;BYTE data[128];}memory;ZeroMemory(&memory,sizeof(memory));
    for(api=4;api<=7;api+=3)for(UINT f=0;f<2;f++){
      for(UINT w=1;w<=8;w++)for(UINT h=1;h<=8;h++){
        DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE;d.dwWidth=w;d.dwHeight=h;d.ddpfPixelFormat.dwSize=sizeof(d.ddpfPixelFormat);d.ddpfPixelFormat.dwFlags=DDPF_FOURCC;d.ddpfPixelFormat.dwFourCC=formats[f];void*surface=NULL;HRESULT hr=create(root,old,&d,&surface);wsprintfA(text,"DIM api=%u format=%u width=%u height=%u hr=%08lX",api,f,w,h,(DWORD)hr);line(text);if(surface)IUnknown_Release((IUnknown*)surface);
      }
      for(UINT i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        DDSURFACEDESC2 d,out;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|cases[i].flags;d.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE;d.dwWidth=d.dwHeight=8;d.lpSurface=memory.data;d.dwLinearSize=(cases[i].value==1?2:cases[i].value==2?4:0)*(f?16:8)+cases[i].rel;d.ddpfPixelFormat.dwSize=sizeof(d.ddpfPixelFormat);d.ddpfPixelFormat.dwFlags=DDPF_FOURCC;d.ddpfPixelFormat.dwFourCC=formats[f];void*surface=NULL;HRESULT hr=create(root,old,&d,&surface);wsprintfA(text,"MEM api=%u format=%u case=%u hr=%08lX",api,f,i,(DWORD)hr);line(text);
        if(surface){ZeroMemory(&out,sizeof(out));out.dwSize=sizeof(out);hr=describe(surface,&out);wsprintfA(text,"DESC api=%u format=%u case=%u hr=%08lX flags=%08lX size=%08lX pointer=%u",api,f,i,(DWORD)hr,out.dwFlags,out.dwLinearSize,!!out.lpSurface);line(text);IUnknown_Release((IUnknown*)surface);}
      }
    }
    IDirectDraw4_Release(old);IDirectDraw7_Release(root);FreeLibrary(module);line("PASS native block surface reference complete");CloseHandle(output);ExitProcess(0);
}
