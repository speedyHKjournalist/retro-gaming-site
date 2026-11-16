/* Actual public8 shader handles before/after moving validation into core9. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
typedef void (*Report)(const char*);static Report report8;
static void verify(BOOL yes,const char*message){if(!yes){report8(message);ExitProcess(1);}}
static BOOL equal(const void*a,const void*b,UINT n){const volatile BYTE*x=a,*y=b;for(UINT i=0;i<n;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
static void shader_function(IDirect3DDevice8*d,DWORD handle,BOOL pixel,const DWORD*code,UINT words)
{
    DWORD bytes=0;HRESULT hr=pixel?IDirect3DDevice8_GetPixelShaderFunction(d,handle,NULL,&bytes):IDirect3DDevice8_GetVertexShaderFunction(d,handle,NULL,&bytes);verify(hr==D3D_OK&&bytes==words*4,"FAIL public8 function size");
    struct{DWORD pre[2],data[16],post[2];}out;for(UINT i=0;i<2;i++){out.pre[i]=0x1234abcd;out.post[i]=0xdcba4321;}hr=pixel?IDirect3DDevice8_GetPixelShaderFunction(d,handle,out.data,&bytes):IDirect3DDevice8_GetVertexShaderFunction(d,handle,out.data,&bytes);verify(hr==D3D_OK&&bytes==words*4&&equal(out.data,code,bytes)&&out.pre[0]==0x1234abcd&&out.post[1]==0xdcba4321,"FAIL public8 exact function");
    verify((pixel?IDirect3DDevice8_DeletePixelShader(d,handle):IDirect3DDevice8_DeleteVertexShader(d,handle))==D3D_OK,"FAIL public8 delete shader");bytes=0x12345678;hr=pixel?IDirect3DDevice8_GetPixelShaderFunction(d,handle,NULL,&bytes):IDirect3DDevice8_GetVertexShaderFunction(d,handle,NULL,&bytes);verify(hr==D3DERR_INVALIDCALL&&bytes==0x12345678,"FAIL public8 expired handle");
}
BOOL public8_shader_probe(const char*directory,HWND window,Report report)
{
    report8=report;static DWORD unterminated[8194];const DWORD declaration[]={D3DVSD_STREAM(0),D3DVSD_REG(0,D3DVSDT_FLOAT4),D3DVSD_REG(5,D3DVSDT_D3DCOLOR),D3DVSD_END()};DWORD baseline_errors[7]={0};
    for(UINT module=0;module<2;module++){
        char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,module?"D3D8.DLL":"BASE8.DLL");HMODULE library=LoadLibraryA(path);verify(library!=NULL,"FAIL public8 module");IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(library,"Direct3DCreate8");verify(make!=NULL,"FAIL public8 export");IDirect3D8*root=make(220);verify(root!=NULL,"FAIL public8 factory");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;IDirect3DDevice8*d=NULL;verify(IDirect3D8_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d)==D3D_OK,"FAIL public8 device");
        for(UINT minor=0;minor<2;minor++){const DWORD code[]={0xfffe0100u+minor,1,0xc00f0000,0x90e40000,1,0xd00f0000,0x90e40005,0xffff};DWORD handle=0;verify(IDirect3DDevice8_CreateVertexShader(d,declaration,code,&handle,0)==D3D_OK&&handle,"FAIL public8 legacy VS");shader_function(d,handle,FALSE,code,8);}
        for(UINT minor=0;minor<5;minor++){const DWORD code[]={0xffff0100u+minor,1,0x800f0000,0x90e40000,0xffff};DWORD handle=0;verify(IDirect3DDevice8_CreatePixelShader(d,code,&handle)==D3D_OK&&handle,"FAIL public8 legacy PS");shader_function(d,handle,TRUE,code,5);}
        for(UINT bad=0;bad<7;bad++){
            DWORD code[]={0xfffe0100,1,0xc00f0000,0x90e40000,0xffff};BOOL pixel=bad>=2&&bad<=3;if(bad==0)code[0]=0xfffe0200;if(bad==1)code[0]=0xfffe0102;if(bad==2)code[0]=0xffff0200;if(bad==3)code[0]=0xffff0105;if(bad==4)code[1]=87;if(bad==5)code[3]=0xf0e40000;unterminated[0]=0xfffe0100;const DWORD*input=bad==6?unterminated:code;DWORD handle=0xcccccccc;HRESULT hr=pixel?IDirect3DDevice8_CreatePixelShader(d,input,&handle):IDirect3DDevice8_CreateVertexShader(d,declaration,input,&handle,0);verify(hr==D3DERR_INVALIDCALL&&!handle,"FAIL public8 rejection/output");if(!module)baseline_errors[bad]=(DWORD)hr;else verify((DWORD)hr==baseline_errors[bad],"FAIL public8 old/current result");
        }
        verify(!IDirect3DDevice8_Release(d)&&!IDirect3D8_Release(root)&&FreeLibrary(library),"FAIL public8 final refs");char line[210];wsprintfA(line,"SHADER8 PUBLIC module=%s vsVersions=2 psVersions=5 invalid=7 byteComparisons=7 expiredHandles=7 finalRefs=0 baseline=4K12",module?"current":"baseline");report8(line);
    }
    return TRUE;
}
