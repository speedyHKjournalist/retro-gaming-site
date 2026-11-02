/* Public SDK COM probe; procedural pixels are replayed on real WebGPU. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <stdint.h>
#if D9WG_PALETTE_API == 9
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DTexture9 Texture;
typedef IDirect3DSurface9 Surface;
typedef IDirect3DStateBlock9 *Block;
#define CALL(n) IDirect3DDevice9_##n
#define FACT(n) IDirect3D9_##n
#define TEX(n) IDirect3DTexture9_##n
#define SURF(n) IDirect3DSurface9_##n
#else
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DTexture8 Texture;
typedef IDirect3DSurface8 Surface;
typedef DWORD Block;
#define CALL(n) IDirect3DDevice8_##n
#define FACT(n) IDirect3D8_##n
#define TEX(n) IDirect3DTexture8_##n
#define SURF(n) IDirect3DSurface8_##n
#endif
static HANDLE logfile;
static Device *device;
static Texture *textures[2];
static void report(const char *s) {DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
static void check(BOOL yes,const char *s) {if(!yes){report(s);ExitProcess(1);}}
#define OK(x) check(SUCCEEDED(x),"FAIL " #x)
static Block block_create(D3DSTATEBLOCKTYPE type) {Block b;OK(CALL(CreateStateBlock)(device,type,&b));return b;}
static void block_apply(Block b) {
#if D9WG_PALETTE_API == 9
 OK(IDirect3DStateBlock9_Apply(b));
#else
 OK(CALL(ApplyStateBlock)(device,b));
#endif
}
static void block_capture(Block b) {
#if D9WG_PALETTE_API == 9
 OK(IDirect3DStateBlock9_Capture(b));
#else
 OK(CALL(CaptureStateBlock)(device,b));
#endif
}
static void block_free(Block b) {
#if D9WG_PALETTE_API == 9
 IDirect3DStateBlock9_Release(b);
#else
 OK(CALL(DeleteStateBlock)(device,b));
#endif
}
static void current(UINT expected) {UINT got=0xcccccccc;OK(CALL(GetCurrentTexturePalette)(device,&got));check(got==expected,"FAIL current palette");}
static void fill(PALETTEENTRY *p,BYTE r,BYTE g,BYTE b,BYTE a) {for(unsigned i=0;i<256;++i)p[i]=(PALETTEENTRY){r,g,b,a};}
static void entries(UINT index,const PALETTEENTRY *expected) {PALETTEENTRY got[256];OK(CALL(GetPaletteEntries)(device,index,got));for(unsigned i=0;i<sizeof(got);++i)check(((BYTE*)got)[i]==((const BYTE*)expected)[i],"FAIL palette entries");}
static void frame(void) {
 struct Vertex {float x,y,z,w,u,v;} vertices[4]={{0,0,.5f,1,0,0},{32,0,.5f,1,1,0},{0,64,.5f,1,0,1},{32,64,.5f,1,1,1}};
 OK(CALL(Clear)(device,0,NULL,D3DCLEAR_TARGET,0,1,0));OK(CALL(BeginScene)(device));
 for(unsigned side=0;side<2;++side){for(unsigned i=0;i<4;++i)vertices[i].x=(i&1?32.f:0.f)+side*32.f;OK(CALL(SetTexture)(device,0,(void*)textures[side]));OK(CALL(DrawPrimitiveUP)(device,D3DPT_TRIANGLESTRIP,2,vertices,sizeof(vertices[0])));}
 OK(CALL(EndScene)(device));OK(CALL(Present)(device,NULL,NULL,NULL,NULL));
}
void WINAPI WinMainCRTStartup(void) {
 char directory[MAX_PATH],path[MAX_PATH],name[32];GetModuleFileNameA(NULL,directory,sizeof(directory));unsigned at=lstrlenA(directory);while(at&&directory[at-1]!='\\')--at;directory[at]=0;
 lstrcpyA(path,directory);lstrcatA(path,"PALETTE.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");
 lstrcpyA(path,directory);wsprintfA(name,"D3D%u.DLL",D9WG_PALETTE_API);lstrcatA(path,name);HMODULE module=LoadLibraryA(path);check(module!=NULL,"FAIL local DLL");wsprintfA(name,"Direct3DCreate%u",D9WG_PALETTE_API);Factory *(WINAPI *make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,name);check(make!=NULL,"FAIL export");Factory *factory=make(D3D_SDK_VERSION);check(factory!=NULL,"FAIL factory");
 HWND window=CreateWindowExA(0,"STATIC","Palette probe",WS_OVERLAPPEDWINDOW,0,0,96,96,NULL,NULL,GetModuleHandleA(NULL),NULL);D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=1;pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;OK(FACT(CreateDevice)(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device));
 Surface *target;
#if D9WG_PALETTE_API == 9
 OK(CALL(CreateRenderTarget)(device,64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL));OK(CALL(SetRenderTarget)(device,0,target));OK(CALL(SetFVF)(device,D3DFVF_XYZRHW|D3DFVF_TEX1));
#else
 OK(CALL(CreateRenderTarget)(device,64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,FALSE,&target));OK(CALL(SetRenderTarget)(device,target,NULL));OK(CALL(SetVertexShader)(device,D3DFVF_XYZRHW|D3DFVF_TEX1));
#endif
 OK(CALL(SetRenderState)(device,D3DRS_ZENABLE,FALSE));OK(CALL(SetRenderState)(device,D3DRS_LIGHTING,FALSE));OK(CALL(SetRenderState)(device,D3DRS_CULLMODE,D3DCULL_NONE));
 OK(CALL(SetTextureStageState)(device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));OK(CALL(SetTextureStageState)(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE));OK(CALL(SetTextureStageState)(device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));OK(CALL(SetTextureStageState)(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));OK(CALL(SetTextureStageState)(device,1,D3DTSS_COLOROP,D3DTOP_DISABLE));
 for(unsigned i=0;i<2;++i){
#if D9WG_PALETTE_API == 9
 OK(CALL(CreateTexture)(device,2,2,1,0,i?D3DFMT_A8P8:D3DFMT_P8,D3DPOOL_MANAGED,&textures[i],NULL));
#else
 OK(CALL(CreateTexture)(device,2,2,1,0,i?D3DFMT_A8P8:D3DFMT_P8,D3DPOOL_MANAGED,&textures[i]));
#endif
 D3DLOCKED_RECT lock;OK(TEX(LockRect)(textures[i],0,&lock,NULL,0));for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){BYTE *p=(BYTE*)lock.pBits+y*lock.Pitch+x*(i?2:1);p[0]=173;if(i)p[1]=64;}OK(TEX(UnlockRect)(textures[i],0));}
 static PALETTEENTRY white[256],a[256],b[256],c[256],sentinel[256];fill(white,255,255,255,255);fill(a,17,83,149,201);fill(b,211,47,101,99);fill(c,61,179,23,151);
 current(0);const UINT ids[]={0,15,16,255,256,40000,65535};for(unsigned i=0;i<7;++i)entries(ids[i],white);frame();
 OK(CALL(SetPaletteEntries)(device,0,a));entries(0,a);frame();OK(CALL(SetCurrentTexturePalette)(device,40000));current(40000);frame();
 OK(CALL(SetPaletteEntries)(device,65535,b));OK(CALL(SetCurrentTexturePalette)(device,65535));frame();Block all=block_create(D3DSBT_ALL);OK(CALL(SetCurrentTexturePalette)(device,0));frame();block_apply(all);current(65535);frame();
 Block recorded;OK(CALL(BeginStateBlock)(device));OK(CALL(SetCurrentTexturePalette)(device,65535));OK(CALL(SetCurrentTexturePalette)(device,0));current(65535);check(FAILED(CALL(SetCurrentTexturePalette)(device,65536)),"FAIL invalid recorded palette accepted");OK(CALL(EndStateBlock)(device,&recorded));current(65535);frame();block_apply(recorded);current(0);frame();
 OK(CALL(SetPaletteEntries)(device,0,c));block_apply(all);current(65535);frame();block_apply(recorded);current(0);entries(0,c);frame();
 Block pixel=block_create(D3DSBT_PIXELSTATE),vertex=block_create(D3DSBT_VERTEXSTATE);OK(CALL(SetCurrentTexturePalette)(device,65535));block_apply(pixel);block_apply(vertex);current(65535);frame();block_capture(recorded);OK(CALL(SetCurrentTexturePalette)(device,0));block_apply(recorded);current(65535);frame();
 /* Palette contents are resources: even SetPaletteEntries inside a recording
  * changes them immediately, and applying that block must not roll them back. */
 Block content;OK(CALL(BeginStateBlock)(device));OK(CALL(SetPaletteEntries)(device,65535,c));entries(65535,c);OK(CALL(EndStateBlock)(device,&content));OK(CALL(SetPaletteEntries)(device,65535,b));block_apply(content);entries(65535,b);
 FillMemory(sentinel,sizeof(sentinel),0xcc);check(FAILED(CALL(GetPaletteEntries)(device,65536,sentinel)),"FAIL invalid palette query accepted");for(unsigned i=0;i<sizeof(sentinel);++i)check(((BYTE*)sentinel)[i]==0xcc,"FAIL invalid palette query changed output");check(FAILED(CALL(SetPaletteEntries)(device,65536,a)),"FAIL invalid palette set accepted");check(FAILED(CALL(SetPaletteEntries)(device,0,NULL)),"FAIL null entries accepted");check(FAILED(CALL(GetCurrentTexturePalette)(device,NULL)),"FAIL null selection query accepted");current(65535);
 block_free(content);block_free(recorded);block_free(all);block_free(pixel);block_free(vertex);OK(CALL(SetTexture)(device,0,NULL));for(unsigned i=0;i<2;++i)TEX(Release)(textures[i]);SURF(Release)(target);CALL(Release)(device);FACT(Release)(factory);FreeLibrary(module);DestroyWindow(window);report("PASS D3D8/9 palette COM: defaults, 65536 IDs, selection-only state blocks, deferred recording, resource contents, alpha and 12 procedural frames");CloseHandle(logfile);ExitProcess(0);
}
