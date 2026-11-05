"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-image-")),source=fs.readFileSync(process.env.D8WG_BACKEND_IMAGE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
try{const old8=fs.readFileSync(root+"/tests/fixtures/d3d8_image_surface_before_core.c","utf8"),old9=fs.readFileSync(root+"/tests/fixtures/d3d9_offscreen_before_cpu_adapter.c","utf8");const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_format_layouts.h"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DPOOL;typedef int32_t BOOL,LONG,HRESULT;typedef uint8_t BYTE;typedef void*HANDLE;
typedef struct{const void*lpVtbl;}Iface,IDirect3DSurface8,IDirect3DSurface9;typedef struct{UINT api,pins,implicit_surface_count;}Device,D8Device,D9Device,IDirect3DDevice8,IDirect3DDevice9;
typedef struct{UINT Format,Type,Usage,Pool,Size,MultiSampleType,Width,Height;}Desc8;
typedef struct{Iface iface;LONG refcount;Device*device;BYTE*shadow;UINT row_pitch;BOOL backbuffer,depth_stencil,reset_blocker;Desc8 desc;}D8Surface;
typedef struct{Iface iface;LONG refcount;Device*device;BYTE*shadow;UINT row_pitch,byte_count,width,height,format,pool;}D9Surface;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_MANAGED 1
#define D3DPOOL_SYSTEMMEM 2
#define D3DPOOL_SCRATCH 3
#define D3DMULTISAMPLE_NONE 0
#define D3DRTYPE_SURFACE 1
#define TRUE 1
#define FALSE 0
#define HEAP_ZERO_MEMORY 8
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_REGISTER_RANGE(...)
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() NULL
static int g_surface_vtbl,g_old_surface_vtbl;
static UINT allocations,frees,live,fail_at,target_calls;static UINT target_args[6];
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;allocations++;if(allocations==fail_at||n>131072)return NULL;BYTE*p=malloc(n);assert(p);memset(p,flags&8?0:0xcd,n);live++;return p;}
static BOOL HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;assert(p&&live);frees++;live--;free(p);return TRUE;}
static Device*device_from_iface(Device*d){return d;}static UINT device_compatibility_api(Device*d){return d->api;}
static void device_child_add_ref(Device*d){d->pins++;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){return d9wg_format_layout(8,f,w,h,b);}
static BOOL old_level_layout8(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(8,f,w,h,p,r,b);}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(9,f,w,h,p,r,b);}
static BOOL supported_texture_format(D3DFORMAT f){UINT w,h,b;return d9wg_format_layout(9,f,&w,&h,&b);}
${extract(source,'supported_format8')}
static HRESULT create_target_texture(Device*d,UINT w,UINT h,D3DFORMAT f,UINT usage,UINT ms,UINT quality,Iface**out){(void)d;target_calls++;UINT a[]={w,h,f,usage,ms,quality};memcpy(target_args,a,sizeof(a));*out=NULL;return -7;}
${old8.replace(/\bg_surface_vtbl\b/g,'g_old_surface_vtbl').replace(/\btexture_level_layout\b/g,'old_level_layout8')}
${old9.replace(/\bdevice_create_offscreen_plain_surface\b/g,'offscreen_before')}
${['create_cpu_surface','device_create_offscreen_plain_surface','d9wg_d3d9_core_image_surface8'].map(n=>extract(source,n)).join('\n')}
static UINT checks,cases,api8cases,api9cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 image line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static void cleanup8(Iface*i){D8Surface*s=(D8Surface*)i;HeapFree(NULL,0,s->shadow);s->device->pins--;HeapFree(NULL,0,s);}
static void cleanup9(Iface*i){D9Surface*s=(D9Surface*)i;HeapFree(NULL,0,s->shadow);s->device->pins--;HeapFree(NULL,0,s);}
int main(void){const UINT dims[]={0,1,4,8192,8193,0xffffffffu},heights[]={0,3,8192,8193};const UINT pools[]={0,1,2,3,4,0xffffffffu};
for(UINT api=8;api<=9;api++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)+1;f++)for(UINT x=0;x<6;x++)for(UINT y=0;y<4;y++)for(UINT pool_index=0;pool_index<(api==8?1u:6u);pool_index++)for(UINT failure=0;failure<3;failure++)for(UINT null_out=0;null_out<2;null_out++){
 if(api==8&&f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)&&(d9wg_format_layouts[f].block_width>1||d9wg_format_layouts[f].block_height>1))continue; /* New Wine block policy tested below, not against frozen rejection. */
 const UINT format=f==sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)?0:d9wg_format_layouts[f].format,w=dims[x],h=heights[y],pool=api==8?2:pools[pool_index];Device old_device={api,0,0},new_device={api,0,0};Iface*before=(void*)(uintptr_t)99,*actual=(void*)(uintptr_t)99;assert(!live);allocations=frees=target_calls=0;memset(target_args,0xcc,sizeof(target_args));fail_at=failure;HRESULT expected=api==8?device_create_image_surface(&old_device,w,h,format,null_out?NULL:&before):offscreen_before(&old_device,w,h,format,pool,null_out?NULL:&before,NULL);UINT wanted_alloc=allocations,wanted_free=frees,wanted_targets=target_calls,args[6];memcpy(args,target_args,sizeof(args));
 allocations=frees=target_calls=0;memset(target_args,0xcc,sizeof(target_args));HRESULT hr=api==8?d9wg_d3d9_core_image_surface8(&new_device,w,h,format,null_out?NULL:&actual):device_create_offscreen_plain_surface(&new_device,w,h,format,pool,null_out?NULL:&actual,NULL);
 CHECK(hr==expected&&allocations==wanted_alloc&&frees==wanted_free&&target_calls==wanted_targets&&!memcmp(args,target_args,sizeof(args)));CHECK(new_device.pins==old_device.pins&&new_device.pins==(!hr));
 if(!hr){D9Surface*n=(D9Surface*)actual;UINT pitch,count,bytes;CHECK(d9wg_texture_level_layout(api,format,w,h,&pitch,&count,&bytes)&&n->row_pitch==pitch&&n->byte_count==bytes&&n->width==w&&n->height==h&&n->format==format&&n->pool==pool&&n->refcount==1&&n->device==&new_device);BYTE*old_shadow=api==8?((D8Surface*)before)->shadow:((D9Surface*)before)->shadow;CHECK(!memcmp(old_shadow,n->shadow,bytes));for(UINT b=0;b<bytes;b++)CHECK(n->shadow[b]==0);if(api==8){D8Surface*o=(D8Surface*)before;CHECK(o->desc.Size==bytes&&o->desc.Format==format&&o->desc.Pool==2&&o->desc.Width==w&&o->desc.Height==h);cleanup8(before);}else cleanup9(before);cleanup9(actual);}else CHECK(actual==before&&(!actual||actual==(void*)(uintptr_t)99));CHECK(!live&&!new_device.pins&&!old_device.pins);cases++;if(api==8)api8cases++;else api9cases++;
}
/* Independently check runtime-owned compressed images, including allocation
 * failures. No driver capability or legacy constructor supplies this oracle. */
for(UINT k=0;k<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);k++)for(UINT w=1;w<=8;w++)for(UINT h=1;h<=8;h++)for(UINT failure=0;failure<3;failure++)for(UINT nullout=0;nullout<2;nullout++){
 const D9WGFormatLayout*q=d9wg_format_layouts+k;if(q->block_width==1&&q->block_height==1)continue;Device d={8,0,0};Iface*out=(void*)(uintptr_t)99;allocations=frees=target_calls=0;fail_at=failure;
 BOOL dxt=(q->format&0xffffffu)==0x545844u,valid=(q->apis&2)&&(!dxt||(!(w%4)&&!(h%4)));HRESULT expected=!valid||nullout?-1:failure?-3:0;
 HRESULT hr=d9wg_d3d9_core_image_surface8(&d,w,h,q->format,nullout?NULL:&out);CHECK(hr==expected&&!target_calls);
 if(!hr){D9Surface*n=(D9Surface*)out;UINT pitch=((w+q->block_width-1)/q->block_width)*q->block_bytes,rows=(h+q->block_height-1)/q->block_height;CHECK(n->row_pitch==pitch&&n->byte_count==pitch*rows&&n->pool==2&&d.pins==1);for(UINT b=0;b<n->byte_count;b++)CHECK(!n->shadow[b]);cleanup9(out);}else CHECK(out==(nullout?(void*)(uintptr_t)99:NULL));CHECK(!live&&!d.pins);cases++;
}
Device good={8,0,0},wrong={9,0,0};for(UINT bad=0;bad<3;bad++){Iface*out=(void*)(uintptr_t)99;allocations=0;CHECK(d9wg_d3d9_core_image_surface8(bad==0?NULL:bad==1?&wrong:&good,3,2,65,bad==2?NULL:&out)==-1&&!allocations&&(bad==2||!out));cases++;}
printf("PASS actual backend8 CPU image: %u checks,%u cases (%u API8/%u ordinary9); frozen8/9 constructors,all registry formats including8-only65,linear/bounds/pools/overflow/output/OOM,exact zeroed storage/pitch andparent pins;API9 allocation refactor unchanged\\n",checks,cases,api8cases,api9cases);return 0;}
`;fs.writeFileSync(dir+"/image.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),dir+"/image.c","-o",dir+"/image"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/image",[],{encoding:"utf8",timeout:60000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
