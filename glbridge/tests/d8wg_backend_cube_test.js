"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-cube-")),source=fs.readFileSync(process.env.D8WG_BACKEND_CUBE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
try{const old8=fs.readFileSync(root+"/tests/fixtures/d3d8_cube_before_core.c","utf8"),old9=fs.readFileSync(root+"/tests/fixtures/d3d9_cube_before_core.c","utf8");const code=`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
#include "${root}/runtime/d9wg_format_layouts.h"
#include "${root}/runtime/d9wg_format_queries.h"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DPOOL,D3DMULTISAMPLE_TYPE,D3DTEXTUREFILTERTYPE;typedef int32_t BOOL,LONG,HRESULT;typedef uint8_t BYTE;typedef void*HANDLE;
typedef struct{const void*lpVtbl;}Iface,IDirect3DCubeTexture8,IDirect3DCubeTexture9;
typedef struct Level{UINT width,height,depth,row_pitch,row_count,slice_pitch,byte_count;BYTE*shadow;BOOL shadow_valid;}Level,D8TextureLevel,D9TextureLevel;
typedef struct Device Device,D8Device,D9Device,IDirect3DDevice8,IDirect3DDevice9;
typedef struct Texture Texture,D8CubeTexture,D9CubeTexture;
struct Device{UINT api,pins;union{Texture*cube_resources;Texture*cube_textures;};UINT handle;};
struct Texture{Iface iface;LONG refcount;Device*device;UINT handle,edge_length,level_count,usage,format,pool,multisample_type,multisample,multisample_quality,autogen_filter;Level*levels;Texture*next_device_resource;};
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define TRUE 1
#define FALSE 0
#define HEAP_ZERO_MEMORY 8
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_MANAGED 1
#define D3DPOOL_SYSTEMMEM 2
#define D3DPOOL_SCRATCH 3
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DUSAGE_WRITEONLY 8
#define D3DUSAGE_AUTOGENMIPMAP 0x400
#define D3DMULTISAMPLE_NONE 0
#define D3DRTYPE_CUBETEXTURE 5
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define TRACE_REGISTER_RANGE(...)
#define GetProcessHeap() NULL
static int g_cube_vtbl;
static UINT allocations,frees,live,fail_at,transport_fail,creates,handles;
static UINT emitted[10];
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;allocations++;if(allocations==fail_at||n>32768)return NULL;BYTE*p=malloc(n?n:1);assert(p);memset(p,flags&8?0:0xcd,n);live++;return p;}
static BOOL HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;if(!p)return FALSE;assert(live);frees++;live--;free(p);return TRUE;}
static Device*device_from_iface(Device*d){return d;}static UINT device_compatibility_api(Device*d){return d->api;}
static void device_child_add_ref(Device*d){d->pins++;}static void device_child_release(Device*d){assert(d->pins);d->pins--;}
static UINT allocate_handle(void){return ++handles;}
static BOOL multiply_u32(UINT a,UINT b,UINT*out){if(b&&a>UINT32_MAX/b)return FALSE;*out=a*b;return TRUE;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){return d9wg_format_layout(8,f,w,h,b);}
static BOOL supported_texture_format(D3DFORMAT f){UINT w,h,b;return texture_format_layout(f,&w,&h,&b);}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(8,f,w,h,p,r,b);}
static BOOL supported_render_target_format(UINT f){return f==21||f==22||f==23||f==24||f==25||f==26;}
static BOOL supported_render_target_format8(UINT f){return supported_render_target_format(f);}
${extract(source,'supported_format8')}
static BOOL texture_create_supported(UINT type,UINT usage,UINT f,UINT pool){(void)type;UINT w,h,b;if(pool>3||(usage&3)==3)return FALSE;if(usage&2)return pool==0&&(f==71||f==73||f==75||f==77||f==79||f==80);if(usage&1)return pool==0&&(f==21||f==22||f==23);return d9wg_format_layout(9,f,&w,&h,&b);}
static UINT runtime_host_features(void){return 0;}
static BOOL emit_command(UINT op,const void*payload,UINT bytes){assert(op==D9WG_OP_CREATE_TEXTURE_CUBE&&bytes==sizeof(D9WGCreateTextureCube));const D9WGCreateTextureCube*q=payload;creates++;UINT a[]={q->edge_length,q->edge_length,q->level_count,q->usage,q->format,q->pool,q->reserved,0,q->resource_handle,0};memcpy(emitted,a,sizeof(a));return !transport_fail;}
static BOOL emit_generate_mips(Device*d,UINT handle,UINT filter,BOOL initial){(void)d;assert(handle&&initial);emitted[7]=filter;emitted[9]+=16;return TRUE;}
#define ZeroMemory(p,n) memset(p,0,n)
${extract(source,'emit_cube_texture_create')}
${extract(source,'full_mip_level_count')}
${old8.replace(/\bg_cube_texture_vtbl\b/g,'g_cube_vtbl').replace(/\bdevice_create_cube_texture\b/g,'old8_create').replace(/\bemit_cube_texture_create\b/g,'old8_emit')}
${old9.replace(/\bdevice_create_cube_texture\b/g,'old9_create').replace(/\btexture_level_layout\b/g,'old9_layout').replace(/\bemit_cube_texture_create\b/g,'old9_emit')}
${['create_cube_texture_object','device_create_cube_texture','d9wg_d3d9_core_cube_texture8'].map(n=>extract(source,n)).join('\n')}
static UINT checks,cases,api8cases,api9cases,levelchecks;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 cube case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static void cleanup(Iface*i){Texture*t=(Texture*)i;for(UINT l=0;l<t->level_count*6;l++)if(t->levels[l].shadow)HeapFree(NULL,0,t->levels[l].shadow);t->device->pins--;t->device->cube_textures=NULL;HeapFree(NULL,0,t->levels);HeapFree(NULL,0,t);}
static BOOL zeroes(const BYTE*p,UINT n){for(UINT i=0;i<n;i++)if(p[i])return FALSE;return TRUE;}
int main(void){const UINT dims[]={0,1,3,4,4096,4097,8192,8193,UINT32_MAX},levels[]={0,1,2,32},usages[]={0,1,2,3,0x400,0x20000,8,9},pools[]={0,1,2,3,4};
for(UINT api=8;api<=9;api++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)+1;f++)for(UINT x=0;x<sizeof(dims)/sizeof(*dims);x++)for(UINT l=0;l<4;l++)for(UINT u=0;u<8;u++)for(UINT p=0;p<5;p++)for(UINT fail=0;fail<10;fail++)for(UINT nullout=0;nullout<2;nullout++){
 UINT format=f==sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)?0:d9wg_format_layouts[f].format,w=dims[x];Device before_device={.api=api,.handle=77},actual_device={.api=api,.handle=77};Iface*before=(void*)(uintptr_t)99,*actual=(void*)(uintptr_t)99;assert(!live);allocations=frees=creates=handles=0;memset(emitted,0xcc,sizeof(emitted));fail_at=fail==9?0:fail;transport_fail=fail==9;HRESULT wanted=api==8?old8_create(&before_device,w,levels[l],usages[u],format,pools[p],nullout?NULL:&before):old9_create(&before_device,w,levels[l],usages[u],format,pools[p],nullout?NULL:&before,NULL);
 /* Wine top-level DXT dimension and texture WRITEONLY rules supersedes the frozen permissive constructor. */
 BOOL dxt=format==0x31545844u||format==0x32545844u||format==0x33545844u||format==0x34545844u||format==0x35545844u;
 if((dxt && (w%4)) || (api==8 && (usages[u]&8))){if(!wanted)cleanup(before);before=nullout?(void*)(uintptr_t)99:NULL;wanted=D3DERR_INVALIDCALL;allocations=frees=creates=handles=0;memset(emitted,0xcc,sizeof(emitted));}
 UINT old_alloc=allocations,old_free=frees,old_creates=creates,old_handles=handles,args[10];memcpy(args,emitted,sizeof(args));
 allocations=frees=creates=handles=0;memset(emitted,0xcc,sizeof(emitted));HRESULT hr=api==8?d9wg_d3d9_core_cube_texture8(&actual_device,w,levels[l],usages[u],format,pools[p],nullout?NULL:&actual):device_create_cube_texture(&actual_device,w,levels[l],usages[u],format,pools[p],nullout?NULL:&actual,NULL);
 CHECK(hr==wanted);CHECK(allocations==old_alloc&&frees==old_free&&creates==old_creates&&handles==old_handles&&!memcmp(args,emitted,sizeof(args)));CHECK(actual_device.pins==before_device.pins&&actual_device.pins==(!hr));
 if(!hr){Texture*a=(Texture*)actual,*b=(Texture*)before;CHECK(a->autogen_filter==b->autogen_filter&&a->refcount==1&&a->device==&actual_device&&actual_device.cube_textures==a&&b->level_count==a->level_count&&a->edge_length==w&&a->usage==usages[u]&&a->format==format&&a->pool==pools[p]&&!a->multisample_type);for(UINT n=0;n<a->level_count*6;n++){Level*aa=a->levels+n,*bb=b->levels+n;CHECK(aa->width==bb->width&&aa->height==bb->height&&aa->row_pitch==bb->row_pitch&&aa->row_count==bb->row_count&&aa->byte_count==bb->byte_count&&!!aa->shadow==!!bb->shadow);if(aa->shadow)CHECK(!memcmp(aa->shadow,bb->shadow,aa->byte_count)&&zeroes(aa->shadow,aa->byte_count));levelchecks++;}cleanup(before);cleanup(actual);}else CHECK(before==actual&&(!actual||actual==(void*)(uintptr_t)99)&&!before_device.cube_textures&&!actual_device.cube_textures);CHECK(!live&&!before_device.pins&&!actual_device.pins);cases++;if(api==8)api8cases++;else api9cases++;
}
Device good={.api=8},wrong={.api=9};for(UINT n=0;n<3;n++){Iface*out=(void*)(uintptr_t)99;allocations=0;CHECK(d9wg_d3d9_core_cube_texture8(n==0?NULL:n==1?&wrong:&good,3,1,0,65,1,n==2?NULL:&out)==-1&&!allocations&&(n==2||!out));cases++;}
printf("PASS actual backend8 cube creation: %u checks,%u cases (%u API8/%u ordinary9),%u face/mip comparisons; frozen8/9 methods plus Wine DXT creation alignment,all registry formats,4096/8192 bounds,levels/pools/usage/autogen,zero CPU storage,allocation/transport unwind/output/list/parent pins\\n",checks,cases,api8cases,api9cases,levelchecks);return 0;}
`;const compiled=code.replace('static HRESULT WINAPI old9_create(', 'static BOOL old9_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(9,f,w,h,p,r,b);}\nstatic HRESULT WINAPI old9_create(');fs.writeFileSync(dir+"/texture.c",compiled);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/texture.c","-o",dir+"/texture"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/texture",[],{encoding:"utf8",timeout:180000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
