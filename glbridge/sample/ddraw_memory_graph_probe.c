/* Targeted app-local DDraw graph pixel probe, using real GPU Blt/readback. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static BYTE client[6][4096];
static unsigned rows, failures;
static void line(const char*s){DWORD n;WriteFile(output,s,lstrlenA(s),&n,NULL);WriteFile(output,"\r\n",2,&n,NULL);FlushFileBuffers(output);}
static BOOL check(HRESULT hr,const char*name){if(FAILED(hr)){char b[160];wsprintfA(b,"FAIL operation=%s hr=%08lX",name,(DWORD)hr);line(b);failures++;return FALSE;}return TRUE;}
static DWORD pixel(unsigned tag,unsigned x,unsigned y){return ((tag*0x172b49u+x*0x030501u+y*0x010307u)&0xffffffu);}
static BOOL fill(IDirectDrawSurface7*s,unsigned tag){DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);if(!check(IDirectDrawSurface7_Lock(s,NULL,&d,DDLOCK_WAIT,NULL),"fill_lock"))return FALSE;for(unsigned y=0;y<d.dwHeight;y++)for(unsigned x=0;x<d.dwWidth;x++)*(DWORD*)((BYTE*)d.lpSurface+y*d.lPitch+x*4)=pixel(tag,x,y);return check(IDirectDrawSurface7_Unlock(s,NULL),"fill_unlock");}
static BOOL pixels(IDirectDrawSurface7*dst,IDirectDrawSurface7*src,unsigned tag,const char*name,unsigned sub){DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);if(!check(IDirectDrawSurface7_GetSurfaceDesc(src,&d),"src_desc"))return FALSE;RECT r={0,0,d.dwWidth,d.dwHeight};unsigned w=d.dwWidth,h=d.dwHeight;if(!check(IDirectDrawSurface7_Blt(dst,&r,src,&r,DDBLT_WAIT,NULL),"gpu_blt"))return FALSE;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);if(!check(IDirectDrawSurface7_Lock(dst,&r,&d,DDLOCK_WAIT|DDLOCK_READONLY,NULL),"gpu_readback"))return FALSE;unsigned bad=0;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)if((*(DWORD*)((BYTE*)d.lpSurface+y*d.lPitch+x*4)&0xffffffu)!=pixel(tag,x,y))bad++;BOOL ok=check(IDirectDrawSurface7_Unlock(dst,NULL),"readback_unlock");char b[180];wsprintfA(b,"PIXELS case=%s sub=%u width=%u height=%u compared=%u failed=%u",name,sub,w,h,w*h,bad);line(b);rows++;if(bad){failures++;line("FAIL graph pixels");}return ok&&!bad;}
static BOOL replace(IDirectDrawSurface7*s,unsigned width,unsigned slot){DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_LPSURFACE;d.lpSurface=client[slot];if(width){d.dwFlags|=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH;d.dwWidth=d.dwHeight=width;d.lPitch=width*4;}return check(IDirectDrawSurface7_SetSurfaceDesc(s,&d,0),"replace");}
static BOOL create(IDirectDraw7*dd,IDirectDrawSurface7**s,DWORD caps,DWORD caps2,unsigned width,unsigned mips){DDSURFACEDESC2 d;ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=d.dwHeight=width;d.ddsCaps.dwCaps=caps;d.ddsCaps.dwCaps2=caps2;d.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);d.ddpfPixelFormat.dwFlags=DDPF_RGB;d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;if(mips){d.dwFlags|=DDSD_MIPMAPCOUNT;d.dwMipMapCount=mips;}return check(IDirectDraw7_CreateSurface(dd,&d,s,NULL),"create");}
static BOOL probe(IDirectDraw7*dd){IDirectDrawSurface7*dst=NULL,*mip[5]={0},*cube[6]={0},*a=NULL,*b=NULL;BOOL ok=FALSE;DWORD caps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE|DDSCAPS_COMPLEX;
 if(!create(dd,&dst,DDSCAPS_VIDEOMEMORY|DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE,0,64,0))goto done;
 if(!create(dd,mip,caps|DDSCAPS_MIPMAP,0,16,5))goto done;
 for(unsigned i=1;i<5;i++){DDSCAPS2 c={DDSCAPS_TEXTURE,0,0,{0}};if(!check(IDirectDrawSurface7_GetAttachedSurface(mip[i-1],&c,mip+i),"mip_attach"))goto done;}
 for(unsigned i=0;i<5;i++)if(!fill(mip[i],i+1)||!pixels(dst,mip[i],i+1,"mip_initial",i))goto done;
 if(!replace(mip[2],0,2))goto done;
 for(unsigned i=0;i<5;i++)if(!fill(mip[i],i+11)||!pixels(dst,mip[i],i+11,"mip_pointer",i))goto done;
 if(!replace(mip[0],32,0)||!fill(mip[0],21))goto done;
 for(unsigned i=0;i<5;i++)if(!pixels(dst,mip[i],i?i+11:21,"mip_root_layout",i))goto done;
 if(!replace(mip[2],8,2)||!fill(mip[2],22))goto done;
 for(unsigned i=0;i<5;i++)if(!pixels(dst,mip[i],i==2?22:i?i+11:21,"mip_child_layout",i))goto done;
 if(!create(dd,cube,caps,DDSCAPS2_CUBEMAP|DDSCAPS2_CUBEMAP_ALLFACES,8,0))goto done;
 {static const DWORD faces[]={DDSCAPS2_CUBEMAP_POSITIVEX,DDSCAPS2_CUBEMAP_NEGATIVEX,DDSCAPS2_CUBEMAP_POSITIVEY,DDSCAPS2_CUBEMAP_NEGATIVEY,DDSCAPS2_CUBEMAP_POSITIVEZ,DDSCAPS2_CUBEMAP_NEGATIVEZ};for(unsigned i=1;i<6;i++){DDSCAPS2 c={0,DDSCAPS2_CUBEMAP|faces[i],0,{0}};if(!check(IDirectDrawSurface7_GetAttachedSurface(cube[0],&c,cube+i),"cube_attach"))goto done;}}
 for(unsigned i=0;i<6;i++)if(!fill(cube[i],31+i)||!pixels(dst,cube[i],31+i,"cube_initial",i))goto done;
 if(!replace(cube[2],0,3))goto done;
 for(unsigned i=0;i<6;i++)if(!fill(cube[i],41+i)||!pixels(dst,cube[i],41+i,"cube_pointer",i))goto done;
 if(!replace(cube[2],16,3)||!fill(cube[2],51))goto done;
 for(unsigned i=0;i<6;i++)if(!pixels(dst,cube[i],i==2?51:41+i,"cube_layout",i))goto done;
 if(!create(dd,&a,DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN,0,8,0)||!fill(a,61)||!check(IDirectDraw7_DuplicateSurface(dd,a,&b),"duplicate"))goto done;
 if(!pixels(dst,a,61,"alias_initial",0)||!pixels(dst,b,61,"alias_initial",1)||!replace(a,0,5)||!fill(a,62)||!pixels(dst,a,62,"alias_detach",0)||!pixels(dst,b,61,"alias_detach",1))goto done;
 ok=TRUE;
 done:if(b)IDirectDrawSurface7_Release(b);if(a)IDirectDrawSurface7_Release(a);for(unsigned i=6;i--;)if(cube[i])IDirectDrawSurface7_Release(cube[i]);for(unsigned i=5;i--;)if(mip[i])IDirectDrawSurface7_Release(mip[i]);if(dst)IDirectDrawSurface7_Release(dst);return ok;
}
void WINAPI WinMainCRTStartup(void){char path[MAX_PATH],*slash;GetModuleFileNameA(NULL,path,sizeof(path));slash=path;for(char*p=path;*p;p++)if(*p=='\\')slash=p+1;lstrcpyA(slash,"GRAPH.TXT");output=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);HMODULE module=LoadLibraryA("ddraw.dll");HRESULT(WINAPI*create_dd)(GUID*,void**,REFIID,IUnknown*)=module?(void*)GetProcAddress(module,"DirectDrawCreateEx"):NULL;IDirectDraw7*dd=NULL;HWND w=CreateWindowExA(0,"STATIC","DDraw memory graph",WS_OVERLAPPEDWINDOW,0,0,160,120,NULL,NULL,GetModuleHandleA(NULL),NULL);BOOL ok=create_dd&&check(create_dd(NULL,(void**)&dd,&IID_IDirectDraw7,NULL),"ddraw")&&check(IDirectDraw7_SetCooperativeLevel(dd,w,DDSCL_NORMAL),"normal")&&probe(dd);if(dd)IDirectDraw7_Release(dd);if(w)DestroyWindow(w);if(module)FreeLibrary(module);char b[100];wsprintfA(b,"%s memory graph rows=%u failures=%u",ok&&!failures&&rows==42?"COMPLETE":"FAIL",rows,failures);line(b);CloseHandle(output);ExitProcess(ok&&!failures&&rows==42?0:1);}
