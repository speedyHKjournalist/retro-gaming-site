"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-depth-")),source=fs.readFileSync(process.env.D8WG_BACKEND_DEPTH_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
const names=['D16','D16_LOCKABLE','D32','D15S1','D24S8','D24X8','D24X4S4'];function sdk(api){const r=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P','-x','c','-'],{input:'#include <d3d'+api+'.h>\n',encoding:'utf8',maxBuffer:16<<20});assert.equal(r.status,0,r.stderr);return Object.fromEntries(names.map(n=>{const m=new RegExp('\\bD3DFMT_'+n+'\\s*=\\s*(\\d+)').exec(r.stdout);assert(m,n);return[n,Number(m[1])];}));}
try{const constants=sdk(8);assert.deepEqual(constants,sdk(9));const defines=Object.entries(constants).map(([n,v])=>'#define D3DFMT_'+n+' '+v).join('\n'),old=fs.readFileSync(root+"/tests/fixtures/d3d8_depth_surface_before_core.c","utf8");const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD;typedef int32_t BOOL,WINBOOL,HRESULT,D3DFORMAT,D3DMULTISAMPLE_TYPE;typedef void*HANDLE;
typedef struct{UINT api;}D9Device,IDirect3DDevice9,IDirect3DDevice8;
typedef struct{void*vtbl;}IDirect3DTexture8;typedef struct{IDirect3DTexture8 iface;BOOL private_back_buffer;}D8Texture;
typedef struct{void*iface;BOOL device_container;UINT refcount,canary;}Surface,IDirect3DSurface8,IDirect3DSurface9;
#define WINAPI
#define D3D_OK 0
#define D3DFMT_UNKNOWN 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define D3DMULTISAMPLE_NONE 0
#define D3DMULTISAMPLE_4_SAMPLES 4
#define TRUE 1
#define FALSE 0
#define D8WG_TRACE_ERROR(v) (v)
#define FAILED(v) ((v)<0)
#define SUCCEEDED(v) ((v)>=0)
${defines}
static D8Texture texture;static Surface surface;static HRESULT failure;static UINT old_storage_calls,new_storage_calls,observed[7],expected[7];
static D9Device*device_from_iface(D9Device*d){return d;}static UINT device_compatibility_api(const D9Device*d){return d->api;}
static BOOL legacy_supported_depth_stencil_format(D3DFORMAT f){return f==D3DFMT_D16||f==D3DFMT_D32||f==D3DFMT_D15S1||f==D3DFMT_D24S8||f==D3DFMT_D24X8||f==D3DFMT_D24X4S4;}
static BOOL supported_depth_stencil_format(D3DFORMAT f){return legacy_supported_depth_stencil_format(f)||f==D3DFMT_D16_LOCKABLE;}
${extract(source,'supported_depth_stencil_format8')}
static BOOL supported_multisample_type(UINT m){return m==0||m==4;}
static Surface*surface_from_iface(Surface*s){return s;}
static HRESULT create_depth_stencil_texture(D9Device*d,UINT w,UINT h,D3DFORMAT format,UINT ms,D8Texture**out){assert(d->api==8);*out=NULL;old_storage_calls++;UINT args[]={w,h,(UINT)format,ms,0,0,0};memcpy(expected,args,sizeof(args));if(!w||!h||w>8192||h>8192)return -1;if(failure)return failure;texture=(D8Texture){0};*out=&texture;return 0;}
static HRESULT IDirect3DTexture8_GetSurfaceLevel(IDirect3DTexture8*t,UINT level,Surface**out){assert(t==&texture.iface&&!level);surface=(Surface){.canary=0x1234abcd};*out=&surface;return 0;}
static UINT IDirect3DTexture8_Release(IDirect3DTexture8*t){assert(t==&texture.iface);return 1;}
${old.replace(/\bdevice_create_depth_surface\b/g,'legacy_create').replace(/\bsupported_depth_stencil_format\b/g,'legacy_supported_depth_stencil_format')}
static HRESULT provider(IDirect3DDevice9*d,UINT w,UINT h,D3DFORMAT f,UINT ms,DWORD quality,BOOL discard,Surface**out,HANDLE*shared){assert(d->api==8);new_storage_calls++;UINT args[]={w,h,(UINT)f,ms,quality,!!discard,shared!=NULL};memcpy(observed,args,sizeof(args));if(!w||!h||w>8192||h>8192)return -1;if(failure)return failure;surface=(Surface){.canary=0x1234abcd};*out=&surface;return 0;}
#define IDirect3DDevice9_CreateDepthStencilSurface provider
${extract(source,'d9wg_d3d9_core_depth_surface8')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 depth line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&provider;IDirect3DDevice9 device={8},wrong={9};const UINT sizes[]={0,1,4,8192,8193,0xffffffffu},heights[]={0,4,8192,8193},samples[]={0,1,4,16};const HRESULT errors[]={0,-2,-3};
for(UINT w=0;w<6;w++)for(UINT h=0;h<4;h++)for(UINT f=0;f<122;f++)for(UINT m=0;m<4;m++)for(UINT err=0;err<3;err++)for(UINT no_out=0;no_out<2;no_out++){
 failure=errors[err];Surface*before=(void*)(uintptr_t)99,*actual=(void*)(uintptr_t)99;old_storage_calls=new_storage_calls=0;memset(expected,0xcc,sizeof(expected));memset(observed,0xcc,sizeof(observed));
 HRESULT wanted=legacy_create(&device,sizes[w],heights[h],f,samples[m],no_out?NULL:&before);surface.canary=9;HRESULT hr=d9wg_d3d9_core_depth_surface8(&device,sizes[w],heights[h],f,samples[m],no_out?NULL:&actual);
 if(f && !no_out && (samples[m]==0||samples[m]==4) && !legacy_supported_depth_stencil_format(f)) before=NULL;
 CHECK(hr==wanted&&actual==before&&old_storage_calls==new_storage_calls);CHECK(!memcmp(expected,observed,sizeof(expected)));CHECK(surface.canary==(!hr?0x1234abcdu:9u));cases++;
}
for(UINT bad=0;bad<3;bad++){Surface*out=(void*)(uintptr_t)99;UINT calls=new_storage_calls;CHECK(d9wg_d3d9_core_depth_surface8(bad==0?NULL:bad==1?&wrong:&device,4,4,D3DFMT_D24S8,0,bad==2?NULL:&out)==-1);CHECK(calls==new_storage_calls&&(bad==2||!out));cases++;}
printf("PASS actual backend8 depth: %u checks,%u cases; SDK8/9 depth enums,frozen validation/results plus Wine invalid-format NULL output,six formats,sample and8192 storage bounds,zero quality/discard/shared,provider errors andprofile guards\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/depth.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/depth.c","-o",dir+"/depth"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/depth",[],{encoding:"utf8"});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
