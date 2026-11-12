"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_RT_SOURCE||root+"/ddrawproxy/d3d7_proxy.inc","utf8"),surface=fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8"),legacy=fs.readFileSync(root+"/ddrawproxy/d3d_legacy_proxy.inc","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-render-target-"));
function extract(input,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(input);assert(m,name);let end=input.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(input[end]==="{")depth++;if(input[end]==="}")depth--;}return input.slice(m.index,end);}
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;
typedef struct{const int*lpVtbl;}IDirectDrawSurface7,IDirectDrawSurface;
typedef uint32_t D3DCOLOR;typedef float D3DVALUE;typedef struct{int32_t x1,y1,x2,y2;}D3DRECT;
typedef struct{unsigned id;}DDrawObject;
typedef struct{BOOL gpu_dirty;DWORD content_version;}Memory;
typedef Memory DDSurfaceMemory;
static LONG InterlockedIncrement(LONG*);
${fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8").match(/static void surface_memory_changed\([^)]*\)\s*\{[\s\S]*?\n}/)[0]}
static Memory clear_memory;
typedef struct DDSurface{const int*vtbl7,*vtbl1;DDrawObject*owner;LONG ref,public_refs[8];BOOL implicit,indexed,dead;Memory*memory;struct{struct{DWORD dwCaps;}ddsCaps;}desc;UINT format,handle,mip_level,cube_face,width,height;struct DDSurface*attached_depth;}DDSurface;
typedef struct D3D7Device{DDSurface*render_target;DDrawObject*owner;UINT creation_version,wire_handle;BOOL render_target_invalid,render_target_pending,legacy_failed_rt;}D3D7Device,IDirect3DDevice7,IDirect3DDevice2;
#define D3DERR_ZBUFFER_NOTPRESENT (-6)
#define D3DERR_STENCILBUFFER_NOTPRESENT (-7)
#define DDWG_FMT_D24S8 75u
#define D3DCLEAR_TARGET 1u
#define D3DCLEAR_ZBUFFER 2u
#define D3DCLEAR_STENCIL 4u
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_INVALIDCAPS (-2)
#define DDERR_INVALIDPIXELFORMAT (-3)
#define D3DERR_INITFAILED (-4)
#define DDERR_NOTFOUND (-5)
#define DD_VERSION_1 1u
#define DD_VERSION_7 7u
#define DDSCAPS_3DDEVICE 0x2000u
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define DDSCAPS_ZBUFFER 0x20000u
#define D3DFVF_NORMAL 0x10u
#define D3DDP_DONOTLIGHT 1u
#define D3DRENDERSTATE_LIGHTING 137u
#define DDWG_TRACE_ERROR(h) (h)
#define ZeroMemory(p,n) memset(p,0,n)
static const int g_surface_vtbl=7,g_surface_vtbl1=1;
static unsigned checks,destroys,emits,fail_mask,wire_color,wire_depth,states,fail_prepare,clears;static int g_transport_lock;static uint8_t clear_payload[1024];static D3D7Device*active_clear;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"render target line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static DDSurface*surface_from_any(void*p,void*v){(void)v;MUST(p);const int*vtbl=*(const int**)p;return(void*)((char*)p-(vtbl==&g_surface_vtbl1?offsetof(DDSurface,vtbl1):0));}
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}static D3D7Device*d3d2_device_from_iface(IDirect3DDevice2*p){return p;}
static void surface_destroy(DDSurface*s){MUST(!s->dead&&!s->ref);s->dead=TRUE;destroys++;}
${["surface_private_addref","surface_private_release","surface_public_addref","surface_public_release","surface_AddRef","surface_Release"].map(n=>extract(surface,n)).join("\n")}
static BOOL format_is_depth(UINT f){return f==75;}static BOOL format_is_yuv(UINT f){return f==100;}static BOOL format_is_compressed(UINT f){return f==101;}
static BOOL prepare_surface_for_host(DDSurface*s){MUST(s&&!s->dead&&!format_is_depth(s->format)&&!s->indexed);return !fail_prepare;}
static BOOL ensure_surface_storage(DDSurface*s){MUST(s&&!s->dead);return TRUE;}
static BOOL emit_command(UINT op,const void*p,UINT bytes){emits++;if(fail_mask&(1u<<emits))return FALSE;if(op==D9WG_OP_SET_RENDER_TARGET){const D9WGSetRenderTarget*q=p;MUST(bytes==sizeof(*q)&&q->device_handle==33);wire_color=q->color_texture_handle;}else{const D9WGSetDepthStencilSurfaceLevel*q=p;MUST(op==D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL&&bytes==sizeof(*q)&&q->device_handle==33);wire_depth=q->depth_texture_handle;}return TRUE;}
static HRESULT d3d7_device_SetRenderState(IDirect3DDevice7*d,UINT state,DWORD value){MUST(!d->render_target_pending&&!d->render_target_invalid&&wire_color==d->render_target->handle&&wire_depth==(d->render_target->attached_depth?d->render_target->attached_depth->handle:0)&&state==D3DRENDERSTATE_LIGHTING&&value==1);states++;return D3D_OK;}
static void EnterCriticalSection(int*p){MUST(!(*p)++);}static void LeaveCriticalSection(int*p){MUST((*p)--==1);}
static BOOL reserve_command_locked(UINT op,UINT bytes,UINT extra,uint8_t**payload,uint8_t**blob){MUST(op==D9WG_OP_CLEAR&&bytes==sizeof(D9WGClear)&&bytes+extra<=sizeof(clear_payload)&&active_clear&&!active_clear->render_target_pending&&!active_clear->render_target_invalid&&wire_color==active_clear->render_target->handle);clears++;*payload=clear_payload;*blob=clear_payload+bytes;return TRUE;}
${["d3d7_emit_render_target","d3d7_release_target","d3d7_device_SetRenderTarget","d3d7_device_GetRenderTarget","d3d7_prepare_legacy_draw","d3d7_multiply_u32","d3d7_device_Clear"].map(n=>extract(source,n)).join("\n")}
${extract(legacy,"d3d2_device_GetRenderTarget")}
static DDSurface make(DDrawObject*o,UINT handle,UINT format,DWORD caps){return(DDSurface){.vtbl7=&g_surface_vtbl,.vtbl1=&g_surface_vtbl1,.owner=o,.ref=1,.memory=&clear_memory,.public_refs={[1]=1},.handle=handle,.format=format,.width=8,.height=8,.desc={.ddsCaps={.dwCaps=caps}}};}
static D3D7Device device(DDrawObject*o,DDSurface*s,unsigned version){D3D7Device d={.render_target=s,.owner=o,.creation_version=version,.wire_handle=33};surface_AddRef((void*)s);emits=fail_mask=fail_prepare=0;MUST(d3d7_emit_render_target(&d));return d;}
static void finish(D3D7Device*d){d3d7_release_target(d->render_target,d->legacy_failed_rt);d->render_target=NULL;}
static void parameters(void){for(unsigned version=2;version<=7;version++){DDrawObject o={1},foreign={2};DDSurface old=make(&o,10,22,DDSCAPS_3DDEVICE),bad=make(&o,20,22,0),other=make(&foreign,30,22,DDSCAPS_3DDEVICE);D3D7Device d=device(&o,&old,version);unsigned count=emits;MUST(d3d7_device_SetRenderTarget(&d,NULL,0)==DDERR_INVALIDPARAMS);MUST(d3d7_device_SetRenderTarget(&d,(void*)&other,0)==DDERR_INVALIDPARAMS);MUST(d3d7_device_SetRenderTarget(&d,(void*)&bad,0)==DDERR_INVALIDCAPS);bad.desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE;bad.indexed=TRUE;MUST(d3d7_device_SetRenderTarget(&d,(void*)&bad,0)==DDERR_INVALIDCAPS);MUST(d.render_target==&old&&old.ref==2&&bad.ref==1&&other.ref==1&&emits==count);bad.indexed=FALSE;bad.desc.ddsCaps.dwCaps|=DDSCAPS_SYSTEMMEMORY;MUST(d3d7_device_SetRenderTarget(&d,(void*)&bad,0)==(version>=7?DDERR_INVALIDPARAMS:0));MUST(d.render_target==(version>=7?&old:&bad));finish(&d);MUST(!surface_public_release(&old,1)&&old.dead);MUST(!surface_public_release(&bad,1)&&bad.dead);MUST(!surface_public_release(&other,1)&&other.dead);}}
static void invalid_targets(void){for(unsigned version=2;version<=7;version++)for(unsigned format=75;format<=101;format+=(format==75?25:1)){DDrawObject o={1};DDSurface old=make(&o,10,22,DDSCAPS_3DDEVICE),bad=make(&o,20,format,DDSCAPS_3DDEVICE);D3D7Device d=device(&o,&old,version);unsigned count=emits;MUST(d3d7_device_SetRenderTarget(&d,(void*)&bad,0)==DDERR_INVALIDPIXELFORMAT&&d.render_target==&bad&&d.render_target_invalid&&d.render_target_pending&&d.legacy_failed_rt==(version==2)&&emits==count&&old.ref==1&&bad.ref==2&&bad.public_refs[1]==1&&bad.public_refs[7]==1);MUST(d3d7_device_SetRenderTarget(&d,(void*)&bad,0)==DDERR_INVALIDPIXELFORMAT&&bad.ref==2);MUST(!d3d7_emit_render_target(&d)&&emits==count);MUST(d3d7_prepare_legacy_draw(&d,D3DFVF_NORMAL,0)==DDERR_INVALIDPIXELFORMAT&&emits==count);active_clear=&d;unsigned before_clear=clears;MUST(d3d7_device_Clear(&d,0,NULL,D3DCLEAR_TARGET,0,1.0f,0)==DDERR_INVALIDPIXELFORMAT&&emits==count&&clears==before_clear);IDirectDrawSurface7*view7=NULL;MUST(!d3d7_device_GetRenderTarget(&d,&view7)&&view7==(void*)&bad&&bad.ref==3);MUST(surface_Release(view7)==1);if(version==2){MUST(surface_public_addref(&bad,1)==2);IDirectDrawSurface*view1=NULL;MUST(!d3d2_device_GetRenderTarget(&d,&view1)&&view1==(void*)&bad.vtbl1&&bad.public_refs[1]==3);MUST(surface_public_release(&bad,1)==2&&surface_public_release(&bad,1)==1);}else MUST(!surface_public_release(&bad,1));finish(&d);MUST(bad.dead&&!bad.ref&&!bad.public_refs[1]&&!bad.public_refs[7]);MUST(!surface_public_release(&old,1)&&old.dead);}}
static void failures(void){for(unsigned version=2;version<=7;version++)for(unsigned mask=0;mask<32;mask++){DDrawObject o={1};DDSurface old=make(&o,10,22,DDSCAPS_3DDEVICE),next=make(&o,20,22,DDSCAPS_3DDEVICE),depth=make(&o,200,75,0);next.attached_depth=&depth;D3D7Device d=device(&o,&old,version);emits=0;fail_mask=mask;HRESULT hr=d3d7_device_SetRenderTarget(&d,(void*)&next,0);BOOL success=!(mask&6u);MUST(hr==(success?D3D_OK:D3DERR_INITFAILED)&&d.render_target==(success?&next:&old)&&!d.render_target_invalid&&!d.legacy_failed_rt);MUST(old.ref==(success?1:2)&&next.ref==(success?2:1));if(!success){unsigned first=(mask&2u)?2:3;BOOL debt=!!(mask&((1u<<first)|(1u<<(first+1))));MUST(d.render_target_pending==debt);if(!debt)MUST(wire_color==10&&!wire_depth);}fail_mask=0;active_clear=&d;if(mask%3==0)MUST(!d3d7_device_SetRenderTarget(&d,(void*)d.render_target,0));else if(mask%3==1){unsigned c=clears;MUST(!d3d7_device_Clear(&d,0,NULL,D3DCLEAR_TARGET,0,1.0f,0)&&clears==c+1);}MUST(!d3d7_prepare_legacy_draw(&d,D3DFVF_NORMAL,0)&&!d.render_target_pending&&wire_color==(success?20u:10u)&&wire_depth==(success?200u:0u));d.render_target_pending=TRUE;fail_prepare=1;unsigned previous_clears=clears;MUST(d3d7_device_Clear(&d,0,NULL,D3DCLEAR_TARGET,0,1.0f,0)==D3DERR_INITFAILED&&d.render_target_pending&&clears==previous_clears);MUST(d3d7_prepare_legacy_draw(&d,D3DFVF_NORMAL,0)==D3DERR_INITFAILED&&d.render_target_pending);MUST(d3d7_device_SetRenderTarget(&d,(void*)d.render_target,0)==D3DERR_INITFAILED&&d.render_target_pending);fail_prepare=0;clear_memory.gpu_dirty=FALSE;MUST(!d3d7_device_Clear(&d,0,NULL,D3DCLEAR_TARGET,0,1.0f,0)&&!d.render_target_pending&&clear_memory.gpu_dirty&&clears==previous_clears+1);unsigned e=emits;MUST(!d3d7_device_SetRenderTarget(&d,(void*)d.render_target,0)&&emits==e);finish(&d);MUST(!surface_public_release(&old,1)&&!surface_public_release(&next,1)&&!surface_public_release(&depth,1));}}
static void invalid_rollback(void){for(unsigned version=2;version<=7;version++){DDrawObject o={1};DDSurface old=make(&o,10,22,DDSCAPS_3DDEVICE),bad=make(&o,20,75,DDSCAPS_3DDEVICE),next=make(&o,30,22,DDSCAPS_3DDEVICE);D3D7Device d=device(&o,&old,version);MUST(d3d7_device_SetRenderTarget(&d,(void*)&bad,0)==DDERR_INVALIDPIXELFORMAT);if(version==2)MUST(surface_public_addref(&bad,1)==2);MUST(surface_public_release(&bad,1)==(version==2?1u:0u));emits=0;fail_mask=2;MUST(d3d7_device_SetRenderTarget(&d,(void*)&next,0)==D3DERR_INITFAILED&&d.render_target==&bad&&d.render_target_invalid&&d.legacy_failed_rt==(version==2)&&bad.ref==(version==2?2:1)&&next.ref==1);fail_mask=0;MUST(d3d7_prepare_legacy_draw(&d,D3DFVF_NORMAL,0)==DDERR_INVALIDPIXELFORMAT);MUST(!d3d7_device_SetRenderTarget(&d,(void*)&next,0)&&!d.render_target_invalid&&!d.render_target_pending&&!d.legacy_failed_rt&&bad.dead);finish(&d);MUST(!surface_public_release(&old,1)&&!surface_public_release(&next,1));}}
int main(void){invalid_rollback();parameters();invalid_targets();failures();MUST(states&&destroys);printf("PASS actual render-target C: %u checks, native caps/palette/system-memory validation, invalid-format target observability and Device2 compensation counts, all color/depth failure prefixes, atomic logical rollback, immediate/deferred binding repair, same-target retries and complete lifetime cleanup\\n",checks);return 0;}
`;
try{const file=dir+"/target.c",binary=dir+"/target";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
