/* One app-local COM probe, compiled against the three actual public SDKs.
 * Expected colors below come from simple independent lighting equations. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <stdint.h>
#if D9WG_TNL_API == 9
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 Buffer;
typedef D3DMATERIAL9 Material;
typedef D3DLIGHT9 Light;
#define CALL(name) IDirect3DDevice9_##name
#define BUF(name) IDirect3DVertexBuffer9_##name
#define FACT(name) IDirect3D9_##name
#elif D9WG_TNL_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 Buffer;
typedef D3DMATERIAL8 Material;
typedef D3DLIGHT8 Light;
#define CALL(name) IDirect3DDevice8_##name
#define BUF(name) IDirect3DVertexBuffer8_##name
#define FACT(name) IDirect3D8_##name
#else
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <ddraw.h>
#include <d3d.h>
typedef IDirect3D7 Factory;
typedef IDirect3DDevice7 Device;
typedef IDirect3DVertexBuffer7 Buffer;
typedef D3DMATERIAL7 Material;
typedef D3DLIGHT7 Light;
#define CALL(name) IDirect3DDevice7_##name
#define BUF(name) IDirect3DVertexBuffer7_##name
#define FACT(name) IDirect3D7_##name
#define Diffuse dcvDiffuse
#define Ambient dcvAmbient
#define Specular dcvSpecular
#define Emissive dcvEmissive
#define Power power
#define Type dltType
#define Direction dvDirection
#define Position dvPosition
#define Range dvRange
#define Falloff dvFalloff
#define Attenuation0 dvAttenuation0
#define Theta dvTheta
#define Phi dvPhi
#define D3DTS_WORLD D3DTRANSFORMSTATE_WORLD
#define D3DTS_TEXTURE0 D3DTRANSFORMSTATE_TEXTURE0
#define D3DRS_LIGHTING D3DRENDERSTATE_LIGHTING
#define D3DRS_COLORVERTEX D3DRENDERSTATE_COLORVERTEX
#define D3DRS_SPECULARENABLE D3DRENDERSTATE_SPECULARENABLE
#define D3DRS_LOCALVIEWER D3DRENDERSTATE_LOCALVIEWER
#define D3DRS_NORMALIZENORMALS D3DRENDERSTATE_NORMALIZENORMALS
#define D3DRS_AMBIENT D3DRENDERSTATE_AMBIENT
#define D3DRS_DIFFUSEMATERIALSOURCE D3DRENDERSTATE_DIFFUSEMATERIALSOURCE
#define D3DRS_SPECULARMATERIALSOURCE D3DRENDERSTATE_SPECULARMATERIALSOURCE
#define D3DRS_AMBIENTMATERIALSOURCE D3DRENDERSTATE_AMBIENTMATERIALSOURCE
#define D3DRS_EMISSIVEMATERIALSOURCE D3DRENDERSTATE_EMISSIVEMATERIALSOURCE
#define D3DRS_FOGENABLE D3DRENDERSTATE_FOGENABLE
#define D3DRS_FOGVERTEXMODE D3DRENDERSTATE_FOGVERTEXMODE
#define D3DRS_FOGSTART D3DRENDERSTATE_FOGSTART
#define D3DRS_FOGEND D3DRENDERSTATE_FOGEND
#define D3DRS_FOGDENSITY D3DRENDERSTATE_FOGDENSITY
#define D3DRS_RANGEFOGENABLE D3DRENDERSTATE_RANGEFOGENABLE
#endif
typedef struct Input { float x,y,z,nx,ny,nz; DWORD diffuse,specular; float tex[4]; } Input;
typedef struct Output { float x,y,z,rhw; DWORD diffuse,specular; float tex[4]; } Output;
static HANDLE log_file;
static Factory *factory;
static Device *device;
static Buffer *source, *destination;
static DWORD light_index;
static void report(const char *text)
{
    DWORD written;WriteFile(log_file,text,(DWORD)lstrlenA(text),&written,NULL);
    WriteFile(log_file,"\r\n",2,&written,NULL);FlushFileBuffers(log_file);
}
static void check(BOOL condition,const char *label)
{
    if (!condition) { char text[180];wsprintfA(text,"FAIL API %u %s error=%lu",D9WG_TNL_API,label,GetLastError());report(text);ExitProcess(1); }
}
static void state(DWORD name,DWORD value)
{ check(SUCCEEDED(CALL(SetRenderState)(device,name,value)),"SetRenderState"); }
static void float_state(DWORD name,float value)
{ DWORD bits;CopyMemory(&bits,&value,4);state(name,bits); }
static void texture_state(DWORD index,DWORD flags)
{
    check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_TEXCOORDINDEX,index)),"texture coordinate index");
    check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_TEXTURETRANSFORMFLAGS,flags)),"texture transform flags");
}
static BOOL near_value(float got,float expected)
{ float delta=got-expected;return delta<.0002f && delta>-.0002f; }
static void identity(D3DMATRIX *matrix)
{
    ZeroMemory(matrix,sizeof(*matrix));matrix->_11=matrix->_22=matrix->_33=matrix->_44=1;
}
static void create_buffer(DWORD fvf,UINT size,Buffer **buffer)
{
#if D9WG_TNL_API == 9
    check(SUCCEEDED(CALL(CreateVertexBuffer)(device,size,0,fvf,D3DPOOL_SYSTEMMEM,buffer,NULL)),"create VB");
#elif D9WG_TNL_API == 8
    check(SUCCEEDED(CALL(CreateVertexBuffer)(device,size,0,fvf,D3DPOOL_SYSTEMMEM,buffer)),"create VB");
#else
    D3DVERTEXBUFFERDESC desc;ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);
    desc.dwCaps=D3DVBCAPS_SYSTEMMEMORY;desc.dwFVF=fvf;desc.dwNumVertices=1;
    (void)size;check(SUCCEEDED(FACT(CreateVertexBuffer)(factory,&desc,buffer,0)),"create VB");
#endif
}
static void *lock_buffer(Buffer *buffer)
{
    void *data;
#if D9WG_TNL_API == 7
    check(SUCCEEDED(BUF(Lock)(buffer,0,&data,NULL)),"lock VB");
#else
    check(SUCCEEDED(BUF(Lock)(buffer,0,0,(void *)&data,0)),"lock VB");
#endif
    return data;
}
static void color(DWORD actual,DWORD expected,const char *label)
{
    for (unsigned shift=0;shift<32;shift+=8) {
        int delta=(int)((actual>>shift)&255)-(int)((expected>>shift)&255);
        if (delta < -1 || delta > 1) { char text[160];wsprintfA(text,"color actual=%08lx expected=%08lx",actual,expected);report(text);check(FALSE,label); }
    }
}
static Output process_flags(const Input *input,DWORD flags)
{
    Output result;
    Output initial={0,0,0,0,0x10203040u,0x50607080u,{17,19,23,29}};
    *(Output *)lock_buffer(destination)=initial;check(SUCCEEDED(BUF(Unlock)(destination)),"prepare output");
    *(Input *)lock_buffer(source)=*input;check(SUCCEEDED(BUF(Unlock)(source)),"unlock input");
#if D9WG_TNL_API == 9
    check(SUCCEEDED(CALL(ProcessVertices)(device,0,0,1,destination,NULL,flags)),"ProcessVertices");
#elif D9WG_TNL_API == 8
    check(SUCCEEDED(CALL(ProcessVertices)(device,0,0,1,destination,flags)),"ProcessVertices");
#else
    check(SUCCEEDED(BUF(ProcessVertices)(destination,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,1,source,0,device,flags)),"ProcessVertices");
#endif
    result=*(Output *)lock_buffer(destination);check(SUCCEEDED(BUF(Unlock)(destination)),"unlock output");return result;
}
static Output process(const Input *input) { return process_flags(input,0); }
static void light(Light *value)
{
    check(SUCCEEDED(CALL(SetLight)(device,light_index,value)),"SetLight");
    check(SUCCEEDED(CALL(LightEnable)(device,light_index,TRUE)),"LightEnable");
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH],dll[MAX_PATH],text[100];UINT at;HMODULE module;HWND window;
    Material material;Light value;D3DMATRIX matrix;Output output;
    Input input={0,0,.5f,0,0,-1,0xcc336699u,0x596633ccu,{.25f,.75f,.6f,.2f}};
    const DWORD input_fvf=D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX2;
    GetModuleFileNameA(NULL,path,sizeof(path));at=(UINT)lstrlenA(path);while(at&&path[at-1]!='\\')--at;
    path[at]=0;lstrcpyA(dll,path);wsprintfA(text,"TNL%u.TXT",D9WG_TNL_API);lstrcatA(path,text);
    log_file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(log_file==INVALID_HANDLE_VALUE)ExitProcess(2);
    report("READY real XP ProcessVertices lighting COM probe");
    window=CreateWindowExA(0,"STATIC","TNL probe",WS_OVERLAPPEDWINDOW,0,0,96,96,NULL,NULL,GetModuleHandleA(NULL),NULL);check(window!=NULL,"window");
#if D9WG_TNL_API == 7
    IDirectDraw7 *ddraw;IDirectDrawSurface7 *target;DDSURFACEDESC2 desc;
    HRESULT (WINAPI *make)(GUID *,void **,REFIID,IUnknown *);
    lstrcatA(dll,"DDRAW.DLL");module=LoadLibraryA(dll);check(module!=NULL,"local DLL");
    make=(void *)(uintptr_t)GetProcAddress(module,"DirectDrawCreateEx");check(make!=NULL,"export");
    check(SUCCEEDED(make(NULL,(void **)&ddraw,&IID_IDirectDraw7,NULL)),"DirectDrawCreateEx");
    check(SUCCEEDED(IDirectDraw7_SetCooperativeLevel(ddraw,window,DDSCL_NORMAL)),"cooperative level");
    check(SUCCEEDED(IDirectDraw7_QueryInterface(ddraw,&IID_IDirect3D7,(void **)&factory)),"D3D7 factory");
    ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=64;desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE;
    desc.ddpfPixelFormat.dwSize=sizeof(desc.ddpfPixelFormat);desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
    check(SUCCEEDED(IDirectDraw7_CreateSurface(ddraw,&desc,&target,NULL)),"target surface");
    check(SUCCEEDED(FACT(CreateDevice)(factory,&IID_IDirect3DHALDevice,target,&device)),"device");
#else
    Factory *(WINAPI *make)(UINT);D3DPRESENT_PARAMETERS pp;
    wsprintfA(text,"D3D%u.DLL",D9WG_TNL_API);lstrcatA(dll,text);module=LoadLibraryA(dll);check(module!=NULL,"local DLL");
    wsprintfA(text,"Direct3DCreate%u",D9WG_TNL_API);make=(void *)(uintptr_t)GetProcAddress(module,text);check(make!=NULL,"export");
    factory=make(D3D_SDK_VERSION);check(factory!=NULL,"factory");
    ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=1;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    check(SUCCEEDED(FACT(CreateDevice)(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)),"device");
#endif
    create_buffer(input_fvf,sizeof(Input),&source);create_buffer(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE4(0),sizeof(Output),&destination);
#if D9WG_TNL_API == 9
    check(SUCCEEDED(CALL(SetStreamSource)(device,0,source,0,sizeof(Input))),"source stream");check(SUCCEEDED(CALL(SetFVF)(device,input_fvf)),"source FVF");
#elif D9WG_TNL_API == 8
    check(SUCCEEDED(CALL(SetStreamSource)(device,0,source,sizeof(Input))),"source stream");check(SUCCEEDED(CALL(SetVertexShader)(device,input_fvf)),"source FVF");
#endif
    identity(&matrix);check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"identity world");
    state(D3DRS_LIGHTING,TRUE);state(D3DRS_COLORVERTEX,TRUE);state(D3DRS_SPECULARENABLE,FALSE);state(D3DRS_LOCALVIEWER,FALSE);state(D3DRS_NORMALIZENORMALS,FALSE);state(D3DRS_AMBIENT,0);
    state(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);state(D3DRS_SPECULARMATERIALSOURCE,D3DMCS_COLOR2);state(D3DRS_AMBIENTMATERIALSOURCE,D3DMCS_MATERIAL);state(D3DRS_EMISSIVEMATERIALSOURCE,D3DMCS_MATERIAL);
    ZeroMemory(&material,sizeof(material));material.Diffuse.r=.4f;material.Diffuse.g=.2f;material.Diffuse.b=.6f;material.Diffuse.a=.75f;material.Power=8;
    check(SUCCEEDED(CALL(SetMaterial)(device,&material)),"material");
    ZeroMemory(&value,sizeof(value));value.Type=D3DLIGHT_DIRECTIONAL;value.Direction.z=1;value.Diffuse.r=value.Diffuse.g=value.Diffuse.b=1;value.Specular=value.Diffuse;value.Range=100;value.Attenuation0=1;
    light_index=D9WG_TNL_API == 8 ? 37 : 3;light(&value);output=process(&input);color(output.diffuse,input.diffuse,"COLOR1 and high-index light");
    state(D3DRS_COLORVERTEX,FALSE);output=process(&input);color(output.diffuse,0xbf663399u,"material source when COLORVERTEX false");
    state(D3DRS_COLORVERTEX,TRUE);state(D3DRS_SPECULARENABLE,TRUE);output=process(&input);color(output.specular,input.specular,"COLOR2 specular and fog alpha");
    state(D3DRS_SPECULARENABLE,FALSE);value.Type=D3DLIGHT_POINT;value.Position.z=-.5f;value.Attenuation0=.25f;light(&value);output=process(&input);color(output.diffuse,0xccccffffu,"point attenuation may exceed one");
    value.Type=D3DLIGHT_SPOT;value.Position.z=-1;value.Theta=0;value.Phi=3.14159265358979323846f;value.Falloff=2;value.Attenuation0=1;light(&value);
    input.x=1.7320508075688772f;input.z=0;input.diffuse=0xffffffffu;output=process(&input);color(output.diffuse,0xff202020u,"spot cosine cone and falloff squared");
    input.x=input.z=0;input.nx=.6f;input.ny=.8f;input.nz=0;value.Type=D3DLIGHT_DIRECTIONAL;value.Direction.x=-1;value.Direction.z=0;light(&value);
    matrix._11=2;matrix._33=.5f;check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"nonuniform world");output=process(&input);color(output.diffuse,0xff4d4d4du,"inverse transpose unnormalized");
    state(D3DRS_NORMALIZENORMALS,TRUE);output=process(&input);color(output.diffuse,0xff5a5a5au,"normalize transformed normal");
    input.nx=input.ny=input.nz=0;value.Ambient.r=value.Ambient.g=value.Ambient.b=.5f;light(&value);material.Ambient.r=material.Ambient.g=material.Ambient.b=.4f;material.Emissive.r=.1f;material.Diffuse.a=.75f;check(SUCCEEDED(CALL(SetMaterial)(device,&material)),"ambient material");
    state(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_MATERIAL);output=process(&input);color(output.diffuse,0xbf4d3333u,"zero normal keeps ambient and emissive");
#if D9WG_TNL_API == 7
    input.nz=-1;identity(&matrix);check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"restore world");value.Ambient.r=value.Ambient.g=value.Ambient.b=0;value.Type=D3DLIGHT_PARALLELPOINT;value.Position.x=value.Position.y=0;value.Position.z=-2;value.Range=0;light(&value);material.Emissive.r=0;check(SUCCEEDED(CALL(SetMaterial)(device,&material)),"parallel material");output=process(&input);color(output.diffuse,0xbf663399u,"parallel point ignores range");
#endif
    input.nz=-1;input.nx=input.ny=0;value.Type=D3DLIGHT_DIRECTIONAL;value.Direction.x=value.Direction.y=0;value.Direction.z=1;value.Ambient.r=value.Ambient.g=value.Ambient.b=0;light(&value);
    material.Emissive.r=0;check(SUCCEEDED(CALL(SetMaterial)(device,&material)),"copy flag material");
    identity(&matrix);check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"restore world for copy flags");
    output=process_flags(&input,1);color(output.diffuse,0xbf663399u,"DONOTCOPYDATA still computes lighting");
    check(output.tex[0]==17 && output.tex[1]==19 && output.tex[2]==23 && output.tex[3]==29,"DONOTCOPYDATA preserves texcoords");
    state(D3DRS_LIGHTING,FALSE);
#if D9WG_TNL_API == 7
    /* D3D7's D3DVOP_LIGHT, rather than render state, selects the operation.
     * Use an explicit transform-only call for the passthrough flag checks. */
    *(Input *)lock_buffer(source)=input;check(SUCCEEDED(BUF(Unlock)(source)),"restore source");
    Output initial={0,0,0,0,0x10203040u,0x50607080u,{17,19,23,29}};
    *(Output *)lock_buffer(destination)=initial;check(SUCCEEDED(BUF(Unlock)(destination)),"restore output");
    check(SUCCEEDED(BUF(ProcessVertices)(destination,D3DVOP_TRANSFORM,0,1,source,0,device,1)),"D3D7 transform only DONOTCOPYDATA");
    output=*(Output *)lock_buffer(destination);check(SUCCEEDED(BUF(Unlock)(destination)),"read preserved output");
#else
    output=process_flags(&input,1);
#endif
    check(output.diffuse==0x10203040u && output.specular==0x50607080u,"unaffected colors preserved");
    check(output.tex[0]==17 && output.tex[1]==19 && output.tex[2]==23 && output.tex[3]==29,"unaffected texture values preserved");
    output=process(&input);check(output.tex[0]==.25f && output.tex[1]==.75f && output.tex[2]==23 && output.tex[3]==29,"extra texture components preserved");
#if D9WG_TNL_API == 9
    Buffer *offset_source;void *bytes;create_buffer(input_fvf,sizeof(Input)+4,&offset_source);
    bytes=lock_buffer(offset_source);CopyMemory((BYTE *)bytes+4,&input,sizeof(input));check(SUCCEEDED(BUF(Unlock)(offset_source)),"offset source upload");
    check(SUCCEEDED(CALL(SetStreamSource)(device,0,offset_source,4,sizeof(Input))),"source offset");
    check(SUCCEEDED(CALL(ProcessVertices)(device,0,0,1,destination,NULL,0)),"ProcessVertices with source offset");
    output=*(Output *)lock_buffer(destination);check(SUCCEEDED(BUF(Unlock)(destination)),"read offset output");color(output.diffuse,input.diffuse,"offset source color");
    check(output.tex[0]==.25f && output.tex[1]==.75f,"offset source texcoords");
    check(SUCCEEDED(CALL(SetStreamSource)(device,0,source,0,sizeof(Input))),"restore source stream");BUF(Release)(offset_source);
    check(FAILED(CALL(ProcessVertices)(device,0xffffffffu,0,2,destination,NULL,0)),"overflow source rejected");
    check(FAILED(CALL(ProcessVertices)(device,0,0xffffffffu,2,destination,NULL,0)),"overflow destination rejected");
    D3DVERTEXELEMENT9 decl_elements[]={{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITIONT,0},
        {0,16,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,20,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,1},
        {0,24,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9 *decl;check(SUCCEEDED(CALL(CreateVertexDeclaration)(device,decl_elements,&decl)),"alias output declaration");
    check(SUCCEEDED(CALL(ProcessVertices)(device,0,0,1,source,decl,0)),"aliased source/destination");
    output=*(Output *)lock_buffer(source);check(SUCCEEDED(BUF(Unlock)(source)),"read aliased output");color(output.diffuse,input.diffuse,"alias snapshot color");
    check(output.tex[0]==.25f && output.tex[1]==.75f,"alias snapshot texture");IDirect3DVertexDeclaration9_Release(decl);
#elif D9WG_TNL_API == 8
    check(FAILED(CALL(ProcessVertices)(device,0xffffffffu,0,2,destination,0)),"overflow source rejected");
    check(FAILED(CALL(ProcessVertices)(device,0,0xffffffffu,2,destination,0)),"overflow destination rejected");
#endif
    input.x=input.y=0;input.z=.5f;state(D3DRS_LIGHTING,TRUE);state(D3DRS_FOGENABLE,TRUE);
    state(D3DRS_RANGEFOGENABLE,FALSE);float_state(D3DRS_FOGSTART,0);float_state(D3DRS_FOGEND,1);float_state(D3DRS_FOGDENSITY,1);
    state(D3DRS_FOGVERTEXMODE,D3DFOG_LINEAR);output=process_flags(&input,1);check((output.specular>>24)==128,"linear fog is generated with DONOTCOPYDATA");
    state(D3DRS_FOGVERTEXMODE,D3DFOG_EXP);output=process(&input);check((output.specular>>24)==155,"exponential fog factor");
    state(D3DRS_FOGVERTEXMODE,D3DFOG_EXP2);output=process(&input);check((output.specular>>24)==199,"exponential squared fog factor");
    input.x=3;input.y=4;input.z=0;state(D3DRS_RANGEFOGENABLE,TRUE);state(D3DRS_FOGVERTEXMODE,D3DFOG_LINEAR);float_state(D3DRS_FOGEND,10);
    output=process(&input);check((output.specular>>24)==128,"range fog uses camera distance");
    float_state(D3DRS_FOGSTART,10);float_state(D3DRS_FOGEND,0);output=process(&input);check((output.specular>>24)==128,"reversed linear fog bounds");
    state(D3DRS_FOGVERTEXMODE,D3DFOG_NONE);output=process(&input);check((output.specular>>24)==0x59,"fog none retains source alpha");
    texture_state(1,0);output=process(&input);check(output.tex[0]==.6f && output.tex[1]==.2f,"source texture set routing");
    identity(&matrix);matrix._11=2;matrix._22=.5f;matrix._31=.25f;matrix._32=.125f;matrix._33=2;
    check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_TEXTURE0,&matrix)),"texture matrix");
    texture_state(0,2);output=process_flags(&input,1);check(output.tex[0]==.75f && output.tex[1]==.5f && output.tex[2]==23 && output.tex[3]==29,"texture transform with DONOTCOPYDATA");
    texture_state(0,1);output=process(&input);check(output.tex[0]==.75f && output.tex[1]==19 && output.tex[2]==23 && output.tex[3]==29,"COUNT1 transforms one component");
    texture_state(0,259);output=process(&input);check(output.tex[0]==.75f && output.tex[1]==.5f && output.tex[2]==2 && output.tex[3]==29,"projected coordinate keeps undivided q");
    input.x=.2f;input.y=.3f;input.z=.4f;texture_state(0x20000,0);output=process_flags(&input,1);
    check(near_value(output.tex[0],.2f) && near_value(output.tex[1],.3f) && near_value(output.tex[2],.4f) && output.tex[3]==29,"camera position coordinates");
    input.nx=.6f;input.ny=.8f;input.nz=0;identity(&matrix);matrix._11=2;matrix._33=.5f;check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"texture normal world");
    state(D3DRS_NORMALIZENORMALS,FALSE);texture_state(0x10000,0);output=process(&input);check(near_value(output.tex[0],.3f) && near_value(output.tex[1],.8f),"inverse transpose texture normal");
    state(D3DRS_NORMALIZENORMALS,TRUE);output=process_flags(&input,1);check(near_value(output.tex[0],.35112344f) && near_value(output.tex[1],.93632918f),"normalized texture normal");
    identity(&matrix);check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"restore texture normal world");
    input.x=input.y=0;input.z=1;input.nx=input.ny=0;input.nz=-1;texture_state(0x30000,0);output=process(&input);
    check(output.tex[0]==0 && output.tex[1]==0 && output.tex[2]==-1 && output.tex[3]==29,"camera reflection vector");
#if D9WG_TNL_API == 9
    texture_state(0x40000,0);output=process(&input);check(output.tex[0]==.5f && output.tex[1]==.5f && output.tex[2]==23 && output.tex[3]==29,"sphere map degenerate axis");
#endif
    texture_state(7,0);output=process(&input);check(output.tex[0]==17 && output.tex[1]==19 && output.tex[2]==23 && output.tex[3]==29,"missing source texture set preserved");
    report("PASS shared texture coordinate checks");
    report("PASS shared vertex fog checks");
    report("PASS shared vertex copy, bounds and layout checks");
    report("PASS all real XP ProcessVertices lighting checks");BUF(Release)(source);BUF(Release)(destination);CALL(Release)(device);FACT(Release)(factory);
#if D9WG_TNL_API == 7
    IDirectDrawSurface7_Release(target);IDirectDraw7_Release(ddraw);
#endif
    DestroyWindow(window);check(FreeLibrary(module),"unload DLL");CloseHandle(log_file);ExitProcess(0);
}
