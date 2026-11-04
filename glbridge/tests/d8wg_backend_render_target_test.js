"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-render-target-")),source=fs.readFileSync(process.env.D8WG_BACKEND_RENDER_TARGET_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
try{const old=fs.readFileSync(root+"/tests/fixtures/d3d8_render_target_before_core.c","utf8");const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD;typedef int32_t BOOL,WINBOOL,HRESULT,D3DFORMAT,D3DMULTISAMPLE_TYPE;typedef void*HANDLE;
typedef struct{UINT api;}D9Device,IDirect3DDevice9,IDirect3DDevice8;
typedef struct{void*iface;BOOL lockable_render_target,private_back_buffer;}D8Texture,IDirect3DTexture8;
typedef struct{void*iface;BOOL device_container;UINT refcount,canary;BOOL lockable;}Surface,IDirect3DSurface8,IDirect3DSurface9;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define D3DUSAGE_RENDERTARGET 1
#define D3DPOOL_DEFAULT 0
#define D3DMULTISAMPLE_NONE 0
#define D3DMULTISAMPLE_4_SAMPLES 4
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_R5G6B5 23
#define D3DFMT_X1R5G5B5 24
#define D3DFMT_A1R5G5B5 25
#define D3DFMT_A4R4G4B4 26
#define TRUE 1
#define FALSE 0
#define D8WG_TRACE_ERROR(v) (v)
#define FAILED(v) ((v)<0)
#define SUCCEEDED(v) ((v)>=0)
static D8Texture texture;static Surface surface;static HRESULT failure;static UINT old_storage_calls,new_storage_calls,observed[6],expected[6];static BOOL expected_lock;
static D9Device*device_from_iface(D9Device*d){return d;}static UINT device_compatibility_api(const D9Device*d){return d->api;}
static BOOL supported_render_target_format(D3DFORMAT f){return(f>=21&&f<=26)||f==113;}
${extract(source,'supported_render_target_format8')}
static BOOL supported_multisample_type(UINT m){return m==0||m==4;}
static D8Texture*texture_from_iface(D8Texture*t){return t;}static Surface*surface_from_iface(Surface*s){return s;}
static HRESULT create_texture_internal(D9Device*d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,UINT pool,UINT ms,D8Texture**out){assert(d->api==8&&levels==1&&usage==1&&!pool);*out=NULL;if(!w||!h||w>4096||h>4096||format<21||format>26)return -1;old_storage_calls++;UINT args[]={w,h,(UINT)format,ms,0,0};memcpy(expected,args,sizeof(args));if(failure)return failure;texture=(D8Texture){0};*out=&texture;return 0;}
static HRESULT IDirect3DTexture8_GetSurfaceLevel(D8Texture*t,UINT level,Surface**out){assert(t==&texture&&!level);surface=(Surface){.canary=0x1234abcd,.lockable=t->lockable_render_target};*out=&surface;return 0;}
static UINT IDirect3DTexture8_Release(D8Texture*t){assert(t==&texture);return 1;}
${old.replace(/\bdevice_create_render_target\b/g,'legacy_create')}
static HRESULT provider(IDirect3DDevice9*d,UINT w,UINT h,D3DFORMAT f,UINT ms,DWORD quality,BOOL lockable,Surface**out,HANDLE*shared){assert(d->api==8);new_storage_calls++;UINT args[]={w,h,(UINT)f,ms,quality,shared!=NULL};memcpy(observed,args,sizeof(args));assert(!!lockable==expected_lock);if(failure)return failure;surface=(Surface){.canary=0x1234abcd,.lockable=!!lockable};*out=&surface;return 0;}
#define IDirect3DDevice9_CreateRenderTarget provider
${extract(source,'d9wg_d3d9_core_render_target8')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 render target line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&provider;IDirect3DDevice9 device={8},wrong={9};const UINT sizes[]={0,1,4,4096,4097,8192,8193,0xffffffffu},samples[]={0,1,4,16};const BOOL lockable[]={0,1,-1};const HRESULT errors[]={0,-2,-3};
for(UINT w=0;w<8;w++)for(UINT h=0;h<6;h++)for(UINT f=0;f<122;f++)for(UINT m=0;m<4;m++)for(UINT l=0;l<3;l++)for(UINT err=0;err<3;err++)for(UINT no_out=0;no_out<2;no_out++){
 failure=errors[err];Surface*before=(void*)(uintptr_t)99,*actual=(void*)(uintptr_t)99;old_storage_calls=new_storage_calls=0;memset(expected,0xcc,sizeof(expected));memset(observed,0xcc,sizeof(observed));UINT height=h==0?0:h==1?4:h==2?4096:h==3?4097:h==4?8192:0xffffffffu;expected_lock=!!lockable[l];
 HRESULT wanted=legacy_create(&device,sizes[w],height,f,samples[m],lockable[l],no_out?NULL:&before);BOOL old_lock=surface.lockable;surface.lockable=9;HRESULT hr=d9wg_d3d9_core_render_target8(&device,sizes[w],height,f,samples[m],lockable[l],no_out?NULL:&actual);
 CHECK(hr==wanted&&actual==before&&old_storage_calls==new_storage_calls);CHECK(!memcmp(expected,observed,sizeof(expected)));if(!hr)CHECK(actual==&surface&&surface.lockable==old_lock&&surface.canary==0x1234abcd);else CHECK(surface.lockable==9);cases++;
}
for(UINT bad=0;bad<3;bad++){Surface*out=(void*)(uintptr_t)99;UINT calls=new_storage_calls;CHECK(d9wg_d3d9_core_render_target8(bad==0?NULL:bad==1?&wrong:&device,4,4,21,0,1,bad==2?NULL:&out)==-1);CHECK(calls==new_storage_calls&&(bad==2||!out));cases++;}
printf("PASS actual backend8 render target: %u checks,%u cases; frozen public8 validation/results/output,4096 bounds,six formats,MSAA/lockable,zero quality/no shared handle,provider errors andprofile guards\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/target.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/target.c","-o",dir+"/target"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/target",[],{encoding:"utf8"});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
