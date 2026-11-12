/* Independent original system D3D3/7 RGB normal-transform reference. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE logfile;
static void line(const char* s)
{
    DWORD n;
    WriteFile(logfile, s, lstrlenA(s), &n, 0);
    WriteFile(logfile, "\r\n", 2, &n, 0);
    FlushFileBuffers(logfile);
}
static void status(const char* s, HRESULT hr)
{
    char t[160];
    wsprintfA(t, "%s hr=%08lx", s, hr);
    line(t);
}
#define TRY(x)            \
    do {                  \
        HRESULT hr = (x); \
        status(#x, hr);   \
        if (FAILED(hr))   \
            return FALSE;    \
    } while (0)


static void push(void**ptr,unsigned opcode,unsigned size,unsigned count,const void*records){D3DINSTRUCTION*i=*ptr;i->bOpcode=(BYTE)opcode;i->bSize=(BYTE)size;i->wCount=(WORD)count;BYTE*out=(BYTE*)(i+1);const BYTE*in=records;for(unsigned n=0;n<size*count;++n)out[n]=in[n];*ptr=out+size*count;}
static BOOL reference(HMODULE lib,HWND window){
 HRESULT(WINAPI*create)(GUID*,IDirectDraw**,IUnknown*)=(void*)GetProcAddress(lib,"DirectDrawCreate");IDirectDraw*dd;IDirect3D*d3d;IDirectDrawSurface*rt;IDirect3DDevice*dev;IDirect3DViewport*vp;IDirect3DExecuteBuffer*exec;D3DMATRIXHANDLE handles[5];
 TRY(create(NULL,&dd,NULL));TRY(IDirectDraw_SetCooperativeLevel(dd,window,DDSCL_NORMAL));TRY(IDirectDraw_QueryInterface(dd,&IID_IDirect3D,(void**)&d3d));DDSURFACEDESC desc;ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;desc.dwWidth=desc.dwHeight=128;desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_SYSTEMMEMORY;desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;TRY(IDirectDraw_CreateSurface(dd,&desc,&rt,NULL));TRY(IDirectDrawSurface_QueryInterface(rt,&IID_IDirect3DRGBDevice,(void**)&dev));TRY(IDirect3D_CreateViewport(d3d,&vp,NULL));TRY(IDirect3DDevice_AddViewport(dev,vp));D3DVIEWPORT viewport;ZeroMemory(&viewport,sizeof(viewport));viewport.dwSize=sizeof(viewport);viewport.dwWidth=viewport.dwHeight=128;viewport.dvScaleX=viewport.dvScaleY=64;viewport.dvMaxX=viewport.dvMaxY=1;viewport.dvMaxZ=1;TRY(IDirect3DViewport_SetViewport(vp,&viewport));
 D3DMATRIX identity;ZeroMemory(&identity,sizeof(identity));identity._11=identity._22=identity._33=identity._44=1;for(unsigned h=0;h<5;++h){TRY(IDirect3DDevice_CreateMatrix(dev,&handles[h]));D3DMATRIX m=identity;if(h==3)m._41=-.75f;if(h==4)m._11=.5f;TRY(IDirect3DDevice_SetMatrix(dev,handles[h],&m));}
 D3DEXECUTEBUFFERDESC ed;ZeroMemory(&ed,sizeof(ed));ed.dwSize=sizeof(ed);ed.dwFlags=D3DDEB_BUFSIZE|D3DDEB_CAPS;ed.dwCaps=D3DDEBCAPS_SYSTEMMEMORY;ed.dwBufferSize=4096;TRY(IDirect3DDevice_CreateExecuteBuffer(dev,&ed,&exec,NULL));
 for(unsigned phase=0;phase<5;++phase){if(phase==1||phase==2){D3DMATRIX m=identity;if(phase==1)m._41=.75f;TRY(IDirect3DDevice_SetMatrix(dev,handles[0],&m));}
  ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);TRY(IDirectDrawSurface_Lock(rt,NULL,&desc,DDLOCK_WAIT,NULL));for(unsigned y=0;y<128;++y){DWORD*row=(DWORD*)((BYTE*)desc.lpSurface+y*desc.lPitch);for(unsigned x=0;x<128;++x)row[x]=0x00ab4e69;}TRY(IDirectDrawSurface_Unlock(rt,NULL));
  ZeroMemory(&ed,sizeof(ed));ed.dwSize=sizeof(ed);TRY(IDirect3DExecuteBuffer_Lock(exec,&ed));D3DLVERTEX*v=ed.lpData;ZeroMemory(v,4*sizeof(*v));const float xy[4][2]={{-.5f,-.5f},{-.5f,.5f},{.5f,.5f},{.5f,-.5f}};for(unsigned n=0;n<4;++n){v[n].x=xy[n][0];v[n].y=xy[n][1];v[n].z=.5f;v[n].color=0xff00ff00;}void*ptr=v+4;
  if(phase==0)for(unsigned state=1;state<=3;++state){D3DSTATE t;ZeroMemory(&t,sizeof(t));t.dtstTransformStateType=state;t.dwArg[0]=handles[state-1];push(&ptr,D3DOP_STATETRANSFORM,sizeof(t),1,&t);}
  if(phase==3){D3DMATRIXLOAD load={handles[0],handles[3]};push(&ptr,D3DOP_MATRIXLOAD,sizeof(load),1,&load);}if(phase==4){D3DMATRIXMULTIPLY mul={handles[0],handles[3],handles[4]};push(&ptr,D3DOP_MATRIXMULTIPLY,sizeof(mul),1,&mul);}
  D3DSTATE rs;ZeroMemory(&rs,sizeof(rs));rs.drstRenderStateType=D3DRENDERSTATE_ZENABLE;rs.dwArg[0]=FALSE;push(&ptr,D3DOP_STATERENDER,sizeof(rs),1,&rs);rs.drstRenderStateType=D3DRENDERSTATE_CULLMODE;rs.dwArg[0]=D3DCULL_NONE;push(&ptr,D3DOP_STATERENDER,sizeof(rs),1,&rs);
  D3DPROCESSVERTICES process={D3DPROCESSVERTICES_TRANSFORM,0,0,4,0};push(&ptr,D3DOP_PROCESSVERTICES,sizeof(process),1,&process);D3DTRIANGLE triangles[2];ZeroMemory(triangles,sizeof(triangles));triangles[0].v1=0;triangles[0].v2=1;triangles[0].v3=2;triangles[1].v1=2;triangles[1].v2=3;triangles[1].v3=0;triangles[0].wFlags=triangles[1].wFlags=D3DTRIFLAG_START;push(&ptr,D3DOP_TRIANGLE,sizeof(*triangles),2,triangles);push(&ptr,D3DOP_EXIT,0,0,NULL);DWORD bytes=(DWORD)((BYTE*)ptr-(BYTE*)ed.lpData-4*sizeof(*v));TRY(IDirect3DExecuteBuffer_Unlock(exec));D3DEXECUTEDATA data;ZeroMemory(&data,sizeof(data));data.dwSize=sizeof(data);data.dwVertexCount=4;data.dwInstructionOffset=4*sizeof(*v);data.dwInstructionLength=bytes;TRY(IDirect3DExecuteBuffer_SetExecuteData(exec,&data));TRY(IDirect3DDevice_BeginScene(dev));TRY(IDirect3DDevice_Execute(dev,exec,vp,D3DEXECUTE_CLIPPED));TRY(IDirect3DDevice_EndScene(dev));
  ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);TRY(IDirectDrawSurface_Lock(rt,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL));DWORD colors[4];const unsigned xs[]={16,32,64,96};for(unsigned k=0;k<4;++k)CopyMemory(&colors[k],(BYTE*)desc.lpSurface+64*desc.lPitch+xs[k]*4,4);TRY(IDirectDrawSurface_Unlock(rt,NULL));D3DMATRIX got;TRY(IDirect3DDevice_GetMatrix(dev,handles[0],&got));DWORD words[16];CopyMemory(words,&got,64);char text[200];wsprintfA(text,"BOUND phase=%u colors=%08lx,%08lx,%08lx,%08lx scale=%08lx x=%08lx",phase,colors[0],colors[1],colors[2],colors[3],words[0],words[12]);line(text);
 }
 IDirect3DExecuteBuffer_Release(exec);for(unsigned h=0;h<5;++h)TRY(IDirect3DDevice_DeleteMatrix(dev,handles[h]));TRY(IDirect3DDevice_DeleteViewport(dev,vp));IDirect3DViewport_Release(vp);IDirect3DDevice_Release(dev);IDirectDrawSurface_Release(rt);IDirect3D_Release(d3d);IDirectDraw_Release(dd);return TRUE;
}
void WINAPI WinMainCRTStartup(void){char logfile_path[MAX_PATH];GetModuleFileNameA(NULL,logfile_path,sizeof(logfile_path));int pos=lstrlenA(logfile_path);while(pos&&logfile_path[pos-1]!='\\')--pos;logfile_path[pos]=0;lstrcatA(logfile_path,"BNDREF.TXT");logfile=CreateFileA(logfile_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(logfile==INVALID_HANDLE_VALUE)ExitProcess(1);HWND window=CreateWindowExA(0,"STATIC","Native bound matrix reference",WS_OVERLAPPEDWINDOW,0,0,160,160,NULL,NULL,GetModuleHandleA(NULL),NULL);HMODULE lib=LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");BOOL ok=lib&&reference(lib,window);if(lib)FreeLibrary(lib);DestroyWindow(window);line(ok?"COMPLETE bound=1":"COMPLETE bound=0");CloseHandle(logfile);ExitProcess(ok?0:1);}
