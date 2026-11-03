"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-msaa-")),source=fs.readFileSync(process.env.D8WG_MSAA_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8");
function extract(name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{fs.writeFileSync(dir+"/msaa.c",`#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,D3DMULTISAMPLE_TYPE;typedef int BOOL,WINBOOL,HRESULT,D3DFORMAT,D3DPOOL;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DERR_NOTFOUND (-4)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SCRATCH 3
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DMULTISAMPLE_NONE 0
#define D3DMULTISAMPLE_4_SAMPLES 4
#define D3DFMT_UNKNOWN 0
#define D3DRTYPE_SURFACE 1
#define HEAP_ZERO_MEMORY 8
#define D8WG_TRACE_ERROR(x) (x)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct{const void*lpVtbl;}Iface;typedef Iface IDirect3DTexture8,IDirect3DSurface8;
typedef struct{UINT Format,Type,Usage,Pool,Size;D3DMULTISAMPLE_TYPE MultiSampleType;UINT Width,Height;}D3DSURFACE_DESC;
typedef struct{UINT BackBufferWidth,BackBufferHeight,BackBufferFormat;D3DMULTISAMPLE_TYPE MultiSampleType;UINT AutoDepthStencilFormat;}D3DPRESENT_PARAMETERS;
typedef struct{UINT width,height,depth,row_pitch,row_count,byte_count;BYTE*shadow;}D8TextureLevel;
typedef struct D8Device D8Device;typedef D8Device IDirect3DDevice8;
typedef struct Texture{Iface iface;UINT refcount;D8Device*device;UINT handle,width,height,level_count,usage;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample_type;BOOL lockable_render_target;D8TextureLevel*levels;struct Texture*next_device_resource;}D8Texture;
typedef struct{Iface iface;UINT refcount;D8Texture*texture;D8Device*device;BOOL standalone;void*private_data_owner;D3DSURFACE_DESC desc;}D8Surface;
struct D8Device{UINT handle,child_refs,back_buffer_private_data,depth_surface_private_data;D8Texture*texture_resources;D3DPRESENT_PARAMETERS present;struct{UINT Width,Height;D3DFORMAT Format;}display_mode;BOOL depth_surface_enabled;D8Surface*depth_surface;};
static unsigned g_texture_vtbl,allocations,allocation_call,fail_allocation,creates,fail_emit,fail_view,handles,cases;
static D9WGCreateTexture2D commands[128];
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,unsigned flags,size_t n){(void)h;assert(flags==8);if(++allocation_call==fail_allocation)return NULL;void*p=calloc(1,n);if(p)allocations++;return p;}
static void HeapFree(void*h,unsigned f,void*p){(void)h;(void)f;assert(p&&allocations);allocations--;free(p);}
static D8Device*device_from_iface(D8Device*d){return d;}
static D8Texture*texture_from_iface(Iface*i){return (D8Texture*)i;}
static D8Surface*surface_from_iface(Iface*i){return (D8Surface*)i;}
static UINT allocate_handle(void){return ++handles;}
static void device_child_add_ref(D8Device*d){d->child_refs++;}
static void device_child_release(D8Device*d){assert(d->child_refs);d->child_refs--;}
static BOOL supported_texture_format(D3DFORMAT f){return f==22;}
static BOOL supported_render_target_format(D3DFORMAT f){return f==22;}
static BOOL allocate_texture_level(D8TextureLevel*l,D3DFORMAT f,UINT w,UINT h,UINT depth){assert(f==22&&depth==1);l->width=w;l->height=h;l->byte_count=w*h*4;l->shadow=HeapAlloc(NULL,8,l->byte_count);return !!l->shadow;}
static BOOL emit_command(UINT op,const void*p,UINT n){assert(op==D9WG_OP_CREATE_TEXTURE_2D&&n==sizeof(commands[0])&&creates<128);memcpy(commands+creates++,p,n);return !fail_emit;}
static void IDirect3DTexture8_Release(Iface*i){D8Texture*t=(D8Texture*)i;assert(t->refcount);if(--t->refcount)return;D8Device*d=t->device;D8Texture**next=&d->texture_resources;while(*next&&*next!=t)next=&(*next)->next_device_resource;if(*next)*next=t->next_device_resource;for(UINT n=0;n<t->level_count;n++)if(t->levels[n].shadow)HeapFree(NULL,0,t->levels[n].shadow);HeapFree(NULL,0,t->levels);device_child_release(d);HeapFree(NULL,0,t);}
static HRESULT IDirect3DTexture8_GetSurfaceLevel(Iface*i,UINT level,Iface**out){D8Texture*t=(D8Texture*)i;assert(level==0);if(fail_view)return E_OUTOFMEMORY;D8Surface*s=HeapAlloc(NULL,8,sizeof(*s));if(!s)return E_OUTOFMEMORY;s->texture=t;s->device=t->device;s->refcount=1;t->refcount++;*out=&s->iface;return 0;}
static void IDirect3DSurface8_AddRef(Iface*i){((D8Surface*)i)->refcount++;}
static void release_surface(Iface*i){D8Surface*s=(D8Surface*)i;assert(s->refcount);if(--s->refcount)return;if(s->texture)IDirect3DTexture8_Release(&s->texture->iface);else device_child_release(s->device);HeapFree(NULL,0,s);}
static HRESULT create_standalone_surface(D8Device*d,UINT w,UINT h,D3DFORMAT f,DWORD usage,D3DPOOL pool,BOOL lock,BOOL back,BOOL depth,BOOL blocker,Iface**out){(void)lock;(void)back;(void)depth;(void)blocker;D8Surface*s=HeapAlloc(NULL,8,sizeof(*s));if(!s)return E_OUTOFMEMORY;s->device=d;s->refcount=1;s->standalone=1;s->desc=(D3DSURFACE_DESC){.Width=w,.Height=h,.Format=f,.Usage=usage,.Pool=pool};device_child_add_ref(d);*out=&s->iface;return 0;}
${["supported_multisample_type","emit_texture_create","full_mip_level_count","create_texture_internal","device_create_texture","texture_get_level_desc","create_backbuffer_surface","device_create_render_target","device_get_depth_surface"].map(extract).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 MSAA line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){D8Device d={.handle=7,.present={80,48,22,4,75},.display_mode={640,480,22},.depth_surface_enabled=1};
 for(UINT n=0;n<64;n++){UINT ms=n%2?4:0;Iface*out=NULL;UINT before=creates;CHECK(!device_create_render_target(&d,32,24,22,ms,0,&out)&&out);D8Surface*s=(D8Surface*)out;CHECK(creates==before+1&&commands[before].multisample_type==ms&&!commands[before].multisample_quality&&commands[before].width==32&&commands[before].height==24&&commands[before].resource_handle==s->texture->handle);D3DSURFACE_DESC desc;CHECK(!texture_get_level_desc(&s->texture->iface,0,&desc)&&desc.MultiSampleType==ms&&desc.Width==32&&desc.Height==24&&!s->texture->lockable_render_target);release_surface(out);CHECK(!allocations&&!d.child_refs&&!d.texture_resources);cases++;}
 Iface*out=NULL;CHECK(!device_create_render_target(&d,8,8,22,0,1,&out)&&((D8Surface*)out)->texture->lockable_render_target);release_surface(out);cases++;
 for(UINT n=0;n<4;n++){UINT before=creates;out=(void*)123;HRESULT hr=n==0?device_create_render_target(&d,32,32,22,4,1,&out):n==1?device_create_render_target(&d,32,32,22,2,0,&out):n==2?device_create_render_target(&d,0,32,22,0,0,&out):device_create_render_target(&d,32,32,99,0,0,&out);CHECK(hr==-1&&creates==before&&!allocations&&!d.child_refs);cases++;}
 CHECK(device_create_render_target(&d,32,32,22,0,0,NULL)==-1);cases++;
 for(UINT n=1;n<=4;n++){allocation_call=0;fail_allocation=n;out=NULL;CHECK(device_create_render_target(&d,32,32,22,4,0,&out)==-3&&!out&&!allocations&&!d.child_refs&&!d.texture_resources);cases++;}fail_allocation=0;
 out=NULL;fail_emit=1;CHECK(device_create_render_target(&d,32,32,22,4,0,&out)==-2&&!out&&!allocations&&!d.child_refs&&!d.texture_resources);fail_emit=0;cases++;
 out=NULL;fail_view=1;CHECK(device_create_render_target(&d,32,32,22,4,0,&out)==-3&&!out&&!allocations&&!d.child_refs&&!d.texture_resources);fail_view=0;cases++;
 CHECK(!device_create_texture(&d,32,16,0,0,22,2,&out));D8Texture*t=(D8Texture*)out;CHECK(t->level_count==6&&!t->multisample_type&&commands[creates-1].multisample_type==0);IDirect3DTexture8_Release(out);cases++;
 CHECK(!create_backbuffer_surface(&d,&d.present,1,&out));D8Surface*s=(D8Surface*)out;CHECK(s->desc.MultiSampleType==4&&s->desc.Width==80&&s->desc.Height==48&&s->private_data_owner==&d.back_buffer_private_data);release_surface(out);cases++;
 CHECK(!device_get_depth_surface(&d,&out));s=(D8Surface*)out;CHECK(s->desc.MultiSampleType==4&&s->desc.Width==80&&s->desc.Height==48&&s->desc.Format==75&&s->private_data_owner==&d.depth_surface_private_data);release_surface(out);cases++;
 d.present.BackBufferWidth=d.present.BackBufferHeight=d.present.MultiSampleType=0;CHECK(!device_get_depth_surface(&d,&out));s=(D8Surface*)out;CHECK(!s->desc.MultiSampleType&&s->desc.Width==640&&s->desc.Height==480);release_surface(out);cases++;
 allocation_call=0;fail_allocation=1;out=NULL;CHECK(create_backbuffer_surface(&d,&d.present,1,&out)==-3&&!out);allocation_call=0;CHECK(device_get_depth_surface(&d,&out)==-3&&!out);fail_allocation=0;CHECK(!allocations&&!d.child_refs&&!d.texture_resources);cases++;
 printf("PASS actual D3D8 MSAA construction: %u one-create, descriptor, allocation/transport/view failure and implicit extent cases\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/msaa.c","-o",dir+"/msaa"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/msaa",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
