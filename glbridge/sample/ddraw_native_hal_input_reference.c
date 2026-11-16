/* Native Windows HAL contract diagnostic. No application proxy is loaded. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>
void * __cdecl memset(void *p,int c,size_t n) { BYTE *q=p;while(n--)*q++=(BYTE)c;return p; }
void * __cdecl memcpy(void *p,const void *s,size_t n) { BYTE*q=p;const BYTE*r=s;while(n--)*q++=*r++;return p; }
static HANDLE log_file;
static void line(const char *s) { DWORD n;WriteFile(log_file,s,lstrlenA(s),&n,NULL);FlushFileBuffers(log_file); }
static void observation(IDirectDraw7 *dd,IDirect3D7 *d3d,HWND window,const GUID *guid,const char *kind,unsigned variant)
{
    DDSURFACEDESC2 sd;IDirectDrawSurface7*target=NULL;IDirect3DDevice7*dev=NULL;
    IDirect3DVertexBuffer7*vb=NULL,*fresh=NULL;D3DVERTEXBUFFERDESC bd;
    const float vertices[9]={-.5f,-.5f,.5f,.5f,-.5f,.5f,0,.5f,.5f};WORD indices[3]={0,1,2};
    HRESULT hr,prime=S_FALSE,first=E_FAIL,second=E_FAIL,other=E_FAIL;char text[512];D3DDEVICEDESC7 caps;
    (void)window;memset(&sd,0,sizeof(sd));sd.dwSize=sizeof(sd);sd.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;
    sd.dwWidth=64;sd.dwHeight=64;sd.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE;
    if(!lstrcmpA(kind,"RGB"))sd.ddsCaps.dwCaps|=DDSCAPS_SYSTEMMEMORY;
    hr=IDirectDraw7_CreateSurface(dd,&sd,&target,NULL);if(FAILED(hr))goto setup_failed;
    hr=IDirect3D7_CreateDevice(d3d,guid,target,&dev);if(FAILED(hr))goto setup_failed;
    memset(&caps,0,sizeof(caps));hr=IDirect3DDevice7_GetCaps(dev,&caps);if(FAILED(hr))goto setup_failed;
    if(variant==0){
        MEMORY_BASIC_INFORMATION memory;char module_path[MAX_PATH];
        if(VirtualQuery((const void*)dev->lpVtbl->DrawIndexedPrimitiveVB,&memory,sizeof(memory))){
            GetModuleFileNameA((HMODULE)memory.AllocationBase,module_path,MAX_PATH);
            wsprintfA(text,"NATIVE_HAL_ENTRY class=%s module=%s rva=%08lX\r\n",kind,module_path,(DWORD)((const BYTE*)dev->lpVtbl->DrawIndexedPrimitiveVB-(const BYTE*)memory.AllocationBase));line(text);
        }
    }
    memset(&bd,0,sizeof(bd));bd.dwSize=sizeof(bd);bd.dwFVF=D3DFVF_XYZ;bd.dwNumVertices=4;
    if(variant==7)bd.dwCaps=D3DVBCAPS_SYSTEMMEMORY;
    if(variant==8)bd.dwCaps=D3DVBCAPS_WRITEONLY;
    hr=IDirect3D7_CreateVertexBuffer(d3d,&bd,&vb,0);if(FAILED(hr))goto setup_failed;
    if(variant==1){
        IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_LIGHTING,FALSE);IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_ZENABLE,FALSE);
        IDirect3DDevice7_BeginScene(dev);prime=IDirect3DDevice7_DrawIndexedPrimitive(dev,D3DPT_TRIANGLELIST,D3DFVF_XYZ,(void*)vertices,3,indices,3,0);IDirect3DDevice7_EndScene(dev);
    }else if(variant==2)prime=IDirect3DDevice7_DrawIndexedPrimitive(dev,D3DPT_TRIANGLESTRIP,D3DFVF_XYZ,NULL,0,NULL,0,0);
    else if(variant==3)prime=IDirect3DDevice7_DrawPrimitiveVB(dev,D3DPT_TRIANGLESTRIP,vb,0,0,0);
    else if(variant==5){
        void*data;prime=IDirect3DVertexBuffer7_Lock(vb,0,&data,NULL);
        if(SUCCEEDED(prime)){memcpy(data,vertices,sizeof(vertices));prime=IDirect3DVertexBuffer7_Unlock(vb);}
        if(SUCCEEDED(prime)){IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_LIGHTING,FALSE);IDirect3DDevice7_SetRenderState(dev,D3DRENDERSTATE_ZENABLE,FALSE);IDirect3DDevice7_BeginScene(dev);prime=IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,D3DPT_TRIANGLELIST,vb,0,3,indices,3,0);IDirect3DDevice7_EndScene(dev);}
    }else if(variant==6){
        D3DDRAWPRIMITIVESTRIDEDDATA strided;memset(&strided,0,sizeof(strided));prime=IDirect3DDevice7_DrawIndexedPrimitive(dev,D3DPT_TRIANGLESTRIP,D3DFVF_XYZ,NULL,0,NULL,0,0);
        IDirect3DDevice7_DrawIndexedPrimitiveStrided(dev,D3DPT_TRIANGLESTRIP,D3DFVF_XYZ,&strided,0,NULL,0,0);
    }
    first=IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,D3DPT_TRIANGLESTRIP,vb,0,0,variant==4?indices:NULL,variant==4?3:0,0);
    second=IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,D3DPT_TRIANGLESTRIP,vb,0,0,NULL,0,0);
    if(SUCCEEDED(IDirect3D7_CreateVertexBuffer(d3d,&bd,&fresh,0)))other=IDirect3DDevice7_DrawIndexedPrimitiveVB(dev,D3DPT_TRIANGLESTRIP,fresh,0,0,NULL,0,0);
    wsprintfA(text,"NATIVE_HAL_INDEX class=%s variant=%u available=1 caps=%08lX vbcaps=%08lX prime=%08lX first=%08lX second=%08lX fresh=%08lX\r\n",kind,variant,caps.dwDevCaps,bd.dwCaps,(DWORD)prime,(DWORD)first,(DWORD)second,(DWORD)other);line(text);
    goto done;
setup_failed:
    wsprintfA(text,"NATIVE_HAL_INDEX class=%s variant=%u available=0 hr=%08lX\r\n",kind,variant,(DWORD)hr);line(text);
done:
    if(fresh) IDirect3DVertexBuffer7_Release(fresh);
    if(vb) IDirect3DVertexBuffer7_Release(vb);
    if(dev) IDirect3DDevice7_Release(dev);
    if(target) IDirectDrawSurface7_Release(target);
}
void WINAPI entry(void)
{
    char path[MAX_PATH],*end;HMODULE module;IDirectDraw7*dd=NULL;IDirect3D7*d3d=NULL;HWND window;HRESULT hr;
    GetModuleFileNameA(NULL,path,MAX_PATH);end=path+lstrlenA(path);while(end>path&&end[-1]!='\\')--end;lstrcpyA(end,"NV.TXT");
    log_file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_FLAG_WRITE_THROUGH,NULL);if(log_file==INVALID_HANDLE_VALUE)ExitProcess(1);
    line("START native HAL indexed-VB contract; system DDraw only\r\n");GetSystemDirectoryA(path,MAX_PATH);lstrcatA(path,"\\ddraw.dll");module=LoadLibraryA(path);
    if(!module){line("FAIL native system library\r\n");ExitProcess(1);}
    HRESULT(WINAPI*create)(GUID*,void**,REFIID,IUnknown*)=(void*)GetProcAddress(module,"DirectDrawCreateEx");
    if(!create){line("FAIL native factory\r\n");ExitProcess(1);}
    window=CreateWindowExA(0,"STATIC","Native HAL index",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,160,120,NULL,NULL,GetModuleHandleA(NULL),NULL);
    hr=create(NULL,(void**)&dd,&IID_IDirectDraw7,NULL);if(FAILED(hr))goto failed;
    hr=IDirectDraw7_SetCooperativeLevel(dd,window,DDSCL_NORMAL);if(FAILED(hr))goto failed;
    hr=IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void**)&d3d);if(FAILED(hr))goto failed;
    {
        DDDEVICEIDENTIFIER2 id;char text[1536];memset(&id,0,sizeof(id));
        hr=IDirectDraw7_GetDeviceIdentifier(dd,&id,0);
        wsprintfA(text,"NATIVE_HAL_ADAPTER hr=%08lX vendor=%08lX device=%08lX version=%08lX:%08lX driver=%s description=%s\r\n",(DWORD)hr,id.dwVendorId,id.dwDeviceId,(DWORD)id.liDriverVersion.HighPart,id.liDriverVersion.LowPart,id.szDriver,id.szDescription);line(text);
    }
    for(unsigned v=0;v<9;v++){observation(dd,d3d,window,&IID_IDirect3DHALDevice,"HAL",v);observation(dd,d3d,window,&IID_IDirect3DTnLHalDevice,"TNL",v);if(v<7)observation(dd,d3d,window,&IID_IDirect3DRGBDevice,"RGB",v);}
    IDirect3D7_Release(d3d);IDirectDraw7_Release(dd);DestroyWindow(window);line("COMPLETE native_hal_index rows=25; availability and HRESULTs are observations, not a proxy conformance pass\r\n");CloseHandle(log_file);ExitProcess(0);
failed:
    wsprintfA(path,"FAIL native setup hr=%08lX\r\n",(DWORD)hr);line(path);CloseHandle(log_file);ExitProcess(1);
}
