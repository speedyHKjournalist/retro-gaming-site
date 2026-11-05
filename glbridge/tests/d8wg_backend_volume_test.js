"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-volume-")),source=fs.readFileSync(process.env.D8WG_BACKEND_VOLUME_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
try{const old8=fs.readFileSync(root+"/tests/fixtures/d3d8_volume_before_core.c","utf8"),old9=fs.readFileSync(root+"/tests/fixtures/d3d9_volume_before_core.c","utf8");const code=`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
#include "${root}/runtime/d9wg_format_layouts.h"
#include "${root}/runtime/d9wg_format_queries.h"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DPOOL,D3DMULTISAMPLE_TYPE,D3DTEXTUREFILTERTYPE;typedef int32_t BOOL,LONG,HRESULT;typedef uint8_t BYTE;typedef void*HANDLE;
typedef struct{const void*lpVtbl;}Iface,IDirect3DVolumeTexture8,IDirect3DVolumeTexture9;
typedef struct Volume D9Volume;
typedef struct Level{UINT width,height,depth,row_pitch,row_count,slice_pitch,byte_count;BYTE*shadow;BOOL shadow_valid;D9Volume*level_volume;}Level,D8TextureLevel,D9TextureLevel;
typedef struct Device Device,D8Device,D9Device,IDirect3DDevice8,IDirect3DDevice9;
typedef struct Texture Texture,D8VolumeTexture,D9VolumeTexture;
struct Device{UINT api,pins;union{Texture*volume_resources;Texture*volume_textures;};UINT handle;};
struct Texture{Iface iface;LONG refcount;Device*device;UINT handle,width,height,depth,level_count,usage,format,pool,multisample_type,multisample,multisample_quality,autogen_filter;Level*levels;Texture*next_device_resource;};
#define WINAPI
#define D3D_OK 0
#define D3DFMT_UNKNOWN 0
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
#define D3DUSAGE_DYNAMIC 0x200
#define D3DUSAGE_AUTOGENMIPMAP 0x400
#define D3DMULTISAMPLE_NONE 0
#define D3DRTYPE_VOLUMETEXTURE 4
#define D3DFMT_DXT1 827611204u
#define D3DFMT_DXT2 844388420u
#define D3DFMT_DXT3 861165636u
#define D3DFMT_DXT4 877942852u
#define D3DFMT_DXT5 894720068u
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define TRACE_REGISTER_RANGE(...)
#define GetProcessHeap() NULL
struct Volume{Iface iface;Texture*texture;UINT level;};
static int g_volume_texture_vtbl,g_volume_vtbl;
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
${extract(source,'supported_format8')}
${extract(source,'supported_volume_texture_format8')}
static BOOL texture_create_supported(UINT type,UINT usage,UINT f,UINT pool){(void)type;UINT w,h,b;if(pool>3||(usage&3)==3)return FALSE;if(usage&2)return pool==0&&(f==71||f==73||f==75||f==77||f==79||f==80);if(usage&1)return pool==0&&(f==21||f==22||f==23);return d9wg_format_layout(9,f,&w,&h,&b);}
static BOOL emit_command(UINT op,const void*payload,UINT bytes){assert(op==D9WG_OP_CREATE_TEXTURE_VOLUME&&bytes==sizeof(D9WGCreateTextureVolume));const D9WGCreateTextureVolume*q=payload;creates++;UINT a[]={q->width,q->height,q->level_count,q->usage,q->format,q->pool,q->depth,0,q->resource_handle,0};memcpy(emitted,a,sizeof(a));return !transport_fail;}
#define ZeroMemory(p,n) memset(p,0,n)
${extract(source,'emit_volume_texture_create')}
${extract(source,'full_mip_level_count')}
${extract(source,'full_volume_mip_level_count')}
${old8.replace(/\bdevice_create_volume_texture\b/g,'old8_create').replace(/\bemit_volume_texture_create\b/g,'old8_emit')}
${old9.replace(/\bdevice_create_volume_texture\b/g,'old9_create').replace(/\btexture_level_layout\b/g,'old9_layout').replace(/\bemit_volume_texture_create\b/g,'old9_emit')}
${['create_volume_texture_object','device_create_volume_texture','d9wg_d3d9_core_volume_texture8'].map(n=>extract(source,n)).join('\n')}
static UINT checks,cases,api8cases,api9cases,levelchecks;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 volume case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static void cleanup(Iface*i){Texture*t=(Texture*)i;for(UINT l=0;l<t->level_count;l++){if(t->levels[l].level_volume)HeapFree(NULL,0,t->levels[l].level_volume);if(t->levels[l].shadow)HeapFree(NULL,0,t->levels[l].shadow);}t->device->pins--;t->device->volume_textures=NULL;HeapFree(NULL,0,t->levels);HeapFree(NULL,0,t);}
static BOOL zeroes(const BYTE*p,UINT n){for(UINT i=0;i<n;i++)if(p[i])return FALSE;return TRUE;}
int main(void){(void)&device_compatibility_api;const UINT dims[][3]={{0,2,3},{3,0,3},{3,2,0},{3,2,3},{4,4,4},{2048,1,1},{2049,1,1},{1,2048,1},{1,2049,1},{1,1,2048},{1,1,2049},{2048,2048,2048},{UINT32_MAX,UINT32_MAX,UINT32_MAX}},levels[]={0,1,2,32},usages[]={0,1,2,3,0x400,0x20000,8,0x200},pools[]={0,1,2,3,4};
for(UINT api=8;api<=9;api++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)+1;f++)for(UINT x=0;x<sizeof(dims)/sizeof(*dims);x++)for(UINT l=0;l<4;l++)for(UINT u=0;u<8;u++)for(UINT p=0;p<5;p++)for(UINT fail=0;fail<8;fail++)for(UINT nullout=0;nullout<2;nullout++){
 if(api==8&&pools[p]==3)continue; /* CPU scratch expectations below. */
 UINT format=f==sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)?0:d9wg_format_layouts[f].format,w=dims[x][0],h=dims[x][1],depth=dims[x][2];Device before_device={.api=api,.handle=77},actual_device={.api=api,.handle=77};Iface*before=(void*)(uintptr_t)99,*actual=(void*)(uintptr_t)99;assert(!live);allocations=frees=creates=handles=0;memset(emitted,0xcc,sizeof(emitted));fail_at=fail==7?0:fail;transport_fail=fail==7;HRESULT wanted=api==8?old8_create(&before_device,w,h,depth,levels[l],usages[u],format,pools[p],nullout?NULL:&before):old9_create(&before_device,w,h,depth,levels[l],usages[u],format,pools[p],nullout?NULL:&before,NULL);
 if(api==8&&((usages[u]&8)||((usages[u]&0x200)&&pools[p]==1)||format==0x31495441u||format==0x32495441u)){if(!wanted)cleanup(before);before=nullout?(void*)(uintptr_t)99:NULL;wanted=D3DERR_INVALIDCALL;allocations=frees=creates=handles=0;memset(emitted,0xcc,sizeof(emitted));}
 if(api==8&&!format)before=(void*)(uintptr_t)99;
 UINT old_alloc=allocations,old_free=frees,old_creates=creates,old_handles=handles,args[10];memcpy(args,emitted,sizeof(args));
 allocations=frees=creates=handles=0;memset(emitted,0xcc,sizeof(emitted));HRESULT hr=api==8?d9wg_d3d9_core_volume_texture8(&actual_device,w,h,depth,levels[l],usages[u],format,pools[p],nullout?NULL:&actual):device_create_volume_texture(&actual_device,w,h,depth,levels[l],usages[u],format,pools[p],nullout?NULL:&actual,NULL);
 if(allocations!=old_alloc||frees!=old_free||creates!=old_creates||handles!=old_handles)fprintf(stderr,"volume api%u fmt%u %ux%ux%u levels%u usage%x pool%u fail%u null%u allocations%u/%u frees%u/%u creates%u/%u handles%u/%u hr%d/%d\\n",api,format,w,h,depth,levels[l],usages[u],pools[p],fail,nullout,allocations,old_alloc,frees,old_free,creates,old_creates,handles,old_handles,hr,wanted);
 CHECK(hr==wanted);CHECK(allocations==old_alloc&&frees==old_free&&creates==old_creates&&handles==old_handles&&!memcmp(args,emitted,sizeof(args)));CHECK(actual_device.pins==before_device.pins&&actual_device.pins==(!hr));
 if(!hr){Texture*a=(Texture*)actual,*b=(Texture*)before;CHECK(a->refcount==1&&a->device==&actual_device&&actual_device.volume_textures==a&&b->level_count==a->level_count&&a->width==w&&a->height==h&&a->depth==depth&&a->usage==usages[u]&&a->format==format&&a->pool==pools[p]&&!a->multisample_type);for(UINT n=0;n<a->level_count;n++){Level*aa=a->levels+n,*bb=b->levels+n;CHECK(aa->width==bb->width&&aa->height==bb->height&&aa->depth==bb->depth&&aa->slice_pitch==bb->slice_pitch&&!!aa->level_volume==!!bb->level_volume&&aa->row_pitch==bb->row_pitch&&aa->row_count==bb->row_count&&aa->byte_count==bb->byte_count&&!!aa->shadow==!!bb->shadow);if(aa->shadow)CHECK(!memcmp(aa->shadow,bb->shadow,aa->byte_count)&&zeroes(aa->shadow,aa->byte_count));levelchecks++;}cleanup(before);cleanup(actual);}else CHECK(before==actual&&(!actual||actual==(void*)(uintptr_t)99)&&!before_device.volume_textures&&!actual_device.volume_textures);CHECK(!live&&!before_device.pins&&!actual_device.pins);cases++;if(api==8)api8cases++;else api9cases++;
}
/* Scratch volumes use runtime storage even when the driver cannot create
 * that format. Compressed scratch stays CPU-only; uncompressed scratch must
 * create GPU storage and propagate transport errors. */
for(UINT k=0;k<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);k++)for(UINT w=1;w<=8;w++)for(UINT h=1;h<=8;h++)for(UINT depth=1;depth<=3;depth++)for(UINT failure=0;failure<5;failure++)for(UINT transport=0;transport<2;transport++){
 const D9WGFormatLayout*q=d9wg_format_layouts+k;Device d={.api=8};Iface*out=(void*)(uintptr_t)99;allocations=frees=creates=handles=0;fail_at=failure;transport_fail=transport;
 BOOL dxt=(q->format&0xffffffu)==0x545844u,valid=(q->apis&2)&&(!dxt||(!(w%4)&&!(h%4)));UINT levels=1,maximum=w>h?w:h;if(depth>maximum)maximum=depth;while(maximum>1){levels++;maximum>>=1;}BOOL gpu=!dxt&&q->format!=0x31495441u&&q->format!=0x32495441u;BOOL emit=valid&&(!failure||failure>levels+2)&&gpu;HRESULT expected=!valid?-1:failure&&failure<=levels+2?-3:emit&&transport?-2:0;
 HRESULT hr=d9wg_d3d9_core_volume_texture8(&d,w,h,depth,0,0,q->format,3,&out);CHECK(hr==expected&&creates==(UINT)emit);
 if(!hr){Texture*t=(Texture*)out;CHECK(t->level_count==levels&&t->pool==3&&d.pins==1);UINT mw=w,mh=h,md=depth;for(UINT n=0;n<levels;n++){Level*l=t->levels+n;UINT pitch=((mw+q->block_width-1)/q->block_width)*q->block_bytes,rows=(mh+q->block_height-1)/q->block_height;CHECK(l->width==mw&&l->height==mh&&l->depth==md&&l->row_pitch==pitch&&l->slice_pitch==pitch*rows&&l->byte_count==pitch*rows*md&&!l->level_volume&&zeroes(l->shadow,l->byte_count));mw=mw>1?mw/2:1;mh=mh>1?mh/2:1;md=md>1?md/2:1;}cleanup(out);}else CHECK(!out);CHECK(!live&&!d.pins);cases++;
}
Device good={.api=8},wrong={.api=9};for(UINT n=0;n<3;n++){Iface*out=(void*)(uintptr_t)99;allocations=0;CHECK(d9wg_d3d9_core_volume_texture8(n==0?NULL:n==1?&wrong:&good,3,2,3,1,0,65,1,n==2?NULL:&out)==-1&&!allocations&&(n==2||!out));cases++;}
printf("PASS actual backend8 volume creation: %u checks,%u cases (%u API8/%u ordinary9),%u mip comparisons; frozen8/9 methods,all registry formats,2048 bounds/lazy8-eager9 views,levels/pools/usage/autogen,zero CPU storage,allocation/transport unwind/output/list/parent pins\\n",checks,cases,api8cases,api9cases,levelchecks);return 0;}
`;const compiled=code.replace('static HRESULT WINAPI old9_create(', 'static BOOL old9_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(9,f,w,h,p,r,b);}\nstatic HRESULT WINAPI old9_create(');fs.writeFileSync(dir+"/texture.c",compiled);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),dir+"/texture.c","-o",dir+"/texture"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/texture",[],{encoding:"utf8",timeout:180000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
