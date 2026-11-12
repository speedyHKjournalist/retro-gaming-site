"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_MULTIPLE_DEVICE_SOURCE||root+"/ddrawproxy/d3d7_proxy.inc","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-multiple-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/ddrawproxy/ddraw_protocol.h"
typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef unsigned char BYTE;
typedef struct{DWORD value;}GUID;typedef const GUID*REFCLSID;
static const GUID IID_IDirect3DHALDevice={1},IID_IDirect3DTnLHalDevice={2},IID_IDirect3DRGBDevice={3},invalid={4};
typedef struct{const int*lpVtbl;}IDirect3D7,IDirectDraw7,IDirect3DDevice7,IDirectDrawSurface7;
typedef struct D3D7Device D3D7Device;
typedef struct DDrawObject{const int*vtbl7,*d3d7_vtbl;LONG ref;DWORD device_handle;D3D7Device*d3d7_device;}DDrawObject;
typedef struct DDSurface{const int*vtbl7;DDrawObject*owner;DWORD handle,width,height,format;LONG ref,public_refs[8];BOOL indexed;void*palette;struct{struct{DWORD dwCaps;}ddsCaps;}desc;}DDSurface;
typedef struct D3D7StateBlock{struct D3D7StateBlock*next;}D3D7StateBlock;
struct D3D7Device{const int*vtbl,*device3_vtbl,*device2_vtbl,*device1_vtbl;LONG ref;DDrawObject*owner;D3D7Device*next_in_owner;DWORD wire_handle;BOOL wire_created,render_target_invalid,render_target_pending,legacy_failed_rt;DDSurface*render_target;GUID device_guid;UINT creation_version;DDSurface*textures[8];D3D7StateBlock*state_blocks,*recording_state_block;struct{void*positions;}screen_clip_cache;void*extra_lights;};
static const int g_surface_vtbl=7,g_d3d7_device_vtbl=70,g_d3d3_device_vtbl=30,g_d3d2_device_vtbl=20,g_d3d1_device_vtbl=10;
#define WINAPI
#define FALSE 0
#define TRUE 1
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define D3DERR_INVALID_DEVICE (-2)
#define D3DERR_INITFAILED (-3)
#define DDERR_OUTOFMEMORY (-4)
#define E_FAIL (-5)
#define DDERR_INVALIDCAPS (-6)
#define DDERR_NOPALETTEATTACHED (-7)
#define D3DERR_SURFACENOTINVIDMEM (-8)
#define DDERR_INVALIDPIXELFORMAT (-9)
#define DDSCAPS_3DDEVICE 0x2000u
#define DDSCAPS_ZBUFFER 0x20000u
#define DDSCAPS_VIDEOMEMORY 0x4000u
#define FAILED(h) ((h)<0)
#define DD_VERSION_1 1u
#define D3D7_MAX_TEXTURE_STAGES 8u
#define DDWG_TRACE_ERROR(h) (h)
#define ZeroMemory(p,n) memset(p,0,n)
static unsigned checks,stage,allocs,creates,destroys,hidden,rebinds,texture_binds,cleanups,next_handle=200;static BYTE live[10000];
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"multiple device line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static BOOL guid_equal(REFCLSID a,REFCLSID b){return a&&b&&a->value==b->value;}
static DDrawObject*d3d7_object_from_iface(IDirect3D7*p){return (void*)((BYTE*)p-offseTof(DDrawObject,d3d7_vtbl));}
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return (void*)p;}
static DDSurface*surface_from_any(void*p,void*v){(void)v;return p;}
static BOOL format_is_depth(UINT f){return f==75;}static BOOL format_is_yuv(UINT f){return f==100;}static BOOL format_is_compressed(UINT f){return f==101;}
static BOOL ensure_device(DDrawObject*d){MUST(d->device_handle==100);return stage!=1;}
static void*heap_alloc_zero(size_t n){if(stage==2)return NULL;allocs++;return calloc(1,n);}static void heap_free(void*p){if(p){MUST(allocs>0);allocs--;free(p);}}
static DWORD allocate_handle(void){MUST(next_handle<10000);return next_handle++;}
static LONG InterlockedDecrement(LONG*p){return --*p;}
static ULONG ddraw_AddRef(IDirectDraw7*p){return ++((DDrawObject*)p)->ref;}static ULONG ddraw_Release(IDirectDraw7*p){DDrawObject*d=(void*)p;MUST(d->ref>1);return --d->ref;}
static void ddraw_child_detach(DDrawObject*d,BOOL public_ref){(void)d;(void)public_ref;MUST(0);}
static ULONG surface_public_release(DDSurface*s,unsigned version){(void)s;(void)version;MUST(0);return 0;}
static ULONG surface_AddRef(IDirectDrawSurface7*p){return ++((DDSurface*)p)->ref;}static ULONG surface_Release(IDirectDrawSurface7*p){DDSurface*s=(void*)p;MUST(s->ref>1);return --s->ref;}
static BOOL emit_command(UINT op,const void*p,UINT bytes){if(op==D9WG_OP_CREATE_DEVICE){const D9WGCreateDevice*q=p;MUST(bytes==sizeof(*q)&&q->device_handle>100&&!live[q->device_handle]&&q->width==8&&q->height==8&&q->windowed&&!q->enable_auto_depth_stencil);if(stage==3)return FALSE;live[q->device_handle]=1;creates++;}else if(op==D9WG_OP_WINDOW_STATE){const D9WGWindowState*q=p;MUST(bytes==sizeof(*q)&&live[q->device_handle]&&q->flags==D9WG_WINDOW_NO_SURFACE&&!q->hwnd);if(stage==4)return FALSE;hidden++;}else if(op==D9WG_OP_DESTROY_RESOURCE){const D9WGDestroyResource*q=p;MUST(bytes==sizeof(*q)&&!q->resource_kind&&q->resource_handle>100&&live[q->resource_handle]);live[q->resource_handle]=0;destroys++;}else if(op==D9WG_OP_SET_TEXTURE){const D9WGSetTexture*q=p;MUST(bytes==sizeof(*q)&&q->device_handle>100&&live[q->device_handle]&&q->texture_handle==400&&(q->stage==2||q->stage==5));texture_binds++;}else MUST(0);return TRUE;}
static void d3d7_device_init_state(D3D7Device*d){MUST(d->render_target&&d->owner&&d->creation_version);}
static BOOL d3d7_emit_render_target(D3D7Device*d){MUST(d->wire_handle!=d->owner->device_handle&&live[d->wire_handle]&&d->render_target->handle==300);rebinds++;return stage!=5;}
static BOOL prepare_surface_for_host(DDSurface*s){MUST(s->handle==400);return TRUE;}
static BOOL d3d7_sync_initial_state(D3D7Device*d){MUST(live[d->wire_handle]);return stage!=6;}
static void d3d7_state_block_destroy(D3D7StateBlock*b){MUST(!b);}static void d3d_legacy_device_cleanup(D3D7Device*d){MUST(d->owner);cleanups++;}static void d9wg_light_free(void**p){MUST(!*p);}
${["d3d7_destroy_host_context","d3d7_create_host_context","d3d7_validate_device_target","d3d7_create_device_for_version","d3d7_release_target","d3d7_device_Release","d3d7_refresh_render_target","d3d7_rebind_after_flip"].map(extract).join("\n")}
static HRESULT make(DDrawObject*o,DDSurface*s,unsigned version,D3D7Device**out){return d3d7_create_device_for_version((void*)&o->d3d7_vtbl,&IID_IDirect3DHALDevice,(void*)s,(void*)out,version);}
static void profiles(unsigned version){DDrawObject o={.ref=1,.device_handle=100};DDSurface target={.vtbl7=&g_surface_vtbl,.owner=&o,.handle=300,.width=8,.height=8,.format=22,.ref=1,.desc={.ddsCaps={.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_VIDEOMEMORY}}};D3D7Device*a=(void*)1,*b=NULL,*c=NULL;stage=0;MUST(d3d7_create_device_for_version((void*)&o.d3d7_vtbl,&invalid,(void*)&target,(void*)&a,version)==D3DERR_INVALID_DEVICE&&!a&&o.ref==1&&target.ref==1&&!o.d3d7_device);for(unsigned bad=0;bad<5;bad++){DDSurface rejected=target;HRESULT expected=DDERR_INVALIDCAPS;switch(bad){case 0:rejected.desc.ddsCaps.dwCaps=0;break;case 1:rejected.desc.ddsCaps.dwCaps|=DDSCAPS_ZBUFFER;break;case 2:rejected.indexed=TRUE;expected=DDERR_NOPALETTEATTACHED;break;case 3:rejected.desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE;expected=D3DERR_SURFACENOTINVIDMEM;break;case 4:rejected.format=75;expected=DDERR_INVALIDPIXELFORMAT;break;}unsigned previous_creates=creates,previous_allocs=allocs;stage=1;a=(void*)1;MUST(make(&o,&rejected,version,&a)==expected&&!a&&o.ref==1&&target.ref==1&&!o.d3d7_device&&creates==previous_creates&&allocs==previous_allocs);stage=0;}MUST(make(&o,&target,version,&a)==D3D_OK&&a&&o.ref==2&&target.ref==2&&o.d3d7_device==a);DWORD ah=a->wire_handle;for(stage=1;stage<=6;stage++){b=(void*)1;unsigned old_allocs=allocs,old_live=creates-destroys;MUST(make(&o,&target,version,&b)==(stage==2?DDERR_OUTOFMEMORY:D3DERR_INITFAILED)&&!b);MUST(o.d3d7_device==a&&!a->next_in_owner&&live[ah]&&o.ref==2&&target.ref==2&&allocs==old_allocs&&creates-destroys==old_live);}stage=0;MUST(make(&o,&target,version,&b)==D3D_OK&&make(&o,&target,version,&c)==D3D_OK);DWORD bh=b->wire_handle,ch=c->wire_handle;DDSurface texture={.owner=&o,.handle=400,.ref=1},other=target;unsigned r=rebinds,t=texture_binds;d3d7_refresh_render_target(&o,&other);MUST(rebinds==r);d3d7_refresh_render_target(&o,&target);MUST(rebinds==r+3);a->textures[2]=&texture;c->textures[5]=&texture;surface_AddRef((void*)&texture);surface_AddRef((void*)&texture);r=rebinds;d3d7_rebind_after_flip(&o);MUST(rebinds==r+3&&texture_binds==t+2);MUST(ah!=bh&&ah!=ch&&bh!=ch&&o.ref==4&&target.ref==4&&o.d3d7_device==c&&c->next_in_owner==b&&b->next_in_owner==a);MUST(!d3d7_device_Release((void*)b)&&!live[bh]&&live[ah]&&live[ch]&&o.d3d7_device==c&&c->next_in_owner==a&&o.ref==3&&target.ref==3);MUST(!d3d7_device_Release((void*)a)&&!live[ah]&&live[ch]&&o.d3d7_device==c&&!c->next_in_owner);MUST(!d3d7_device_Release((void*)c)&&!live[ch]&&!o.d3d7_device&&o.ref==1&&target.ref==1);MUST(make(&o,&target,version,&a)==D3D_OK&&a->wire_handle!=ah);MUST(!d3d7_device_Release((void*)a)&&!o.d3d7_device&&o.ref==1&&target.ref==1&&!allocs&&creates==destroys&&texture.ref==1);}
int main(void){for(unsigned n=0;n<32;n++){profiles(2);profiles(3);profiles(7);}MUST(hidden&&rebinds&&cleanups);printf("PASS actual multiple legacy device C: %u checks, three live contexts per owner, independent nonpresenting handles, shared target pins, all constructor failures with existing siblings, depth-attachment/flip rebinding across all contexts, middle/head/tail destruction, fresh recreation and complete wire/object cleanup\\n",checks);return 0;}
`.replace("offseTof","offsetof");
try{const file=dir+"/multiple.c",binary=dir+"/multiple";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
