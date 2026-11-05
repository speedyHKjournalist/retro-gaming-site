"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'target8-pair-')),source=fs.readFileSync(process.env.D8WG_TARGET_PAIR_CORE_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8');
function extract(name){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
try{fs.writeFileSync(dir+'/pair.c',`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,ULONG,D3DMULTISAMPLE_TYPE;typedef int HRESULT,BOOL;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DRTYPE_SURFACE 1
#define FAILED(hr) ((hr)<0)
#define TRACE(...) ((void)0)
#define TRACE_REFUSE(x) (x)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct Iface{const void*lpVtbl;UINT refs;}Iface;typedef Iface IUnknown,IDirect3DDevice9,IDirect3DSurface9;
typedef struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}D3DVIEWPORT9;
typedef struct{UINT Format,Type,Usage,Pool,MultiSampleType,MultiSampleQuality,Width,Height;}D3DSURFACE_DESC;
typedef struct D9Device D9Device;
typedef struct Texture{Iface iface;UINT handle,usage,pool;}D9Texture,D9CubeTexture;
typedef struct D9Surface{Iface iface;D9Device*device;D9Texture*texture;D9CubeTexture*cube_texture;UINT cube_face,level,width,height,format,pool,multisample,multisample_quality;BOOL texture_child,auto_depth_stencil;void*shadow,*swap_chain;UINT private_refs,freed;}D9Surface;
struct D9Device{Iface iface;UINT profile,handle;D9Surface*render_target_surfaces[4],*depth_stencil_surface;BOOL depth_stencil_unbound;struct{UINT Width,Height;}display_mode;struct{UINT MultiSampleType;}present;D3DVIEWPORT9 viewport;BOOL recording;};
static int g_device_vtbl,g_surface_vtbl,bad_vtbl;
static D9Device*device_from_iface(Iface*i){return(D9Device*)i;}
static D9Surface*surface_from_iface(Iface*i){return(D9Surface*)i;}
static UINT device_compatibility_api(const D9Device*d){return d->profile;}
/* Only the already-tested surface arm of the weak registry is needed here. */
static D9Device*core_view_device8(IUnknown*i){if(!i||i->lpVtbl!=&g_surface_vtbl)return NULL;D9Device*d=((D9Surface*)i)->device;return d&&d->profile==8?d:NULL;}
static UINT count,fail_at,ordering_error,ref_error;static D9Device*active;static D9Surface*expected_color,*expected_depth;
static struct{UINT op,bytes;unsigned char data[128];}events[4];
static void surface_binding_add_ref(D9Surface*s){if(s->freed)ref_error++;s->private_refs++;}
static void surface_binding_release(D9Surface*s){if(!s->private_refs||s->freed){ref_error++;return;}if(!--s->private_refs)s->freed=1;}
static BOOL emit_command(UINT op,const void*data,UINT bytes){if(!active||count>=4||bytes>128)abort();if(active->render_target_surfaces[0]!=expected_color||active->depth_stencil_surface!=expected_depth)ordering_error++;events[count].op=op;events[count].bytes=bytes;memcpy(events[count].data,data,bytes);count++;return !fail_at||count!=fail_at;}
${['surface_target_handle','surface_get_desc','device_replace_render_target_surface','device_replace_depth_stencil_surface','device_set_live_viewport','d9wg_d3d9_core_set_render_target8','d9wg_d3d9_core_surface_owner8'].map(extract).join('\n')}
static UINT checks,cases;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Target pair line %u: %s\\n",__LINE__,#x);return 1;}checks++;}while(0)
static void reset_events(D9Device*d,D9Surface*c,D9Surface*z,UINT failure){active=d;expected_color=c;expected_depth=z;count=ordering_error=ref_error=0;fail_at=failure;memset(events,0,sizeof(events));}
static D9Device make_device(void){D9Device d={.iface={&g_device_vtbl,17},.profile=8,.handle=7,.display_mode={128,96},.viewport={1,2,17,19,.25f,.75f}};return d;}
int main(void){
 for(UINT kind=0;kind<4;kind++)for(UINT seed=0;seed<128;seed++)for(UINT null_color=0;null_color<2;null_color++)for(UINT failure=0;failure<4;failure++){
  D9Device d=make_device();d.recording=seed&1u;UINT width=1+seed%127,height=1+(seed*13)%95,samples=(seed%3)*2;
  D9Texture texture={.iface={NULL,31},.handle=21,.usage=1},depth_texture={.iface={NULL,37},.handle=22,.usage=2};D9CubeTexture cube={.iface={NULL,41},.handle=23,.usage=1};
  D9Surface color={.iface={&g_surface_vtbl,11},.device=&d,.width=width,.height=height,.multisample=samples,.private_refs=1},depth={.iface={&g_surface_vtbl,13},.device=&d,.texture=&depth_texture,.width=width+seed%2,.height=height+seed%3,.multisample=samples,.private_refs=1};
  if(kind==0){color.texture=&texture;color.texture_child=TRUE;color.level=seed%4;}else if(kind==1){color.cube_texture=&cube;color.cube_face=seed%6;color.level=seed%4;}else if(kind==2)color.texture=&texture;else color.swap_chain=&d;
  if(seed&2u){depth.texture=NULL;depth.auto_depth_stencil=TRUE;}else if(seed&8u){depth.texture_child=TRUE;depth.level=seed%3;}
  d.render_target_surfaces[0]=&color;d.depth_stencil_surface=&depth;
  if(seed&4u)d.viewport=(D3DVIEWPORT9){0,0,width,height,0,1};const D3DVIEWPORT9 prior=d.viewport;
  D9Device before=d;D9Surface before_color=color,before_depth=depth;UINT depth_handle=depth.auto_depth_stencil?0xffffffffu:22u;
  reset_events(&d,&color,&depth,failure);HRESULT hr=d9wg_d3d9_core_set_render_target8(&d.iface,null_color?NULL:&color.iface,&depth.iface);UINT total=null_color?1u:((seed&4u)?2u:3u),emitted=failure&&failure<=total?failure:total;
  CHECK(hr==(failure&&failure<=total?D3DERR_DRIVERINTERNALERROR:D3D_OK)&&count==emitted&&!ordering_error&&!ref_error);CHECK(d.render_target_surfaces[0]==&color&&d.depth_stencil_surface==&depth&&!d.depth_stencil_unbound&&color.private_refs==before_color.private_refs&&depth.private_refs==before_depth.private_refs&&!color.freed&&!depth.freed);CHECK(color.iface.refs==11&&depth.iface.refs==13&&texture.iface.refs==31&&depth_texture.iface.refs==37&&cube.iface.refs==41&&d.iface.refs==17&&d.recording==before.recording);
  if(!null_color){CHECK(events[0].op==D9WG_OP_SET_RENDER_TARGET);D9WGSetRenderTarget*q=(void*)events[0].data;CHECK(q->device_handle==7&&q->target_index==0&&q->color_texture_handle==(kind==1?23u:kind==3?0u:21u)&&q->color_level==(kind<2?seed%4:0u)&&q->color_face==(kind==1?seed%6:0u));}
  if(null_color||count>=2){UINT index=null_color?0:1;CHECK(events[index].op==D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL);D9WGSetDepthStencilSurfaceLevel*q=(void*)events[index].data;CHECK(q->device_handle==7&&q->depth_texture_handle==depth_handle&&q->depth_level==(depth.auto_depth_stencil?0u:depth.level)&&q->width==depth.width&&q->height==depth.height);}
  if(null_color||failure==1||failure==2)CHECK(!memcmp(&d.viewport,&prior,sizeof(prior)));else CHECK(!d.viewport.X&&!d.viewport.Y&&d.viewport.Width==width&&d.viewport.Height==height&&d.viewport.MinZ==0&&d.viewport.MaxZ==1);
  if(count==3){D9WGSetViewport*q=(void*)events[2].data;CHECK(events[2].op==D9WG_OP_SET_VIEWPORT&&q->device_handle==7&&!q->x&&!q->y&&q->width==width&&q->height==height&&q->min_z==0&&q->max_z==1&&!q->reserved);}
  IUnknown*owner=(void*)99;UINT owner_kind=99;CHECK(!d9wg_d3d9_core_surface_owner8(&color.iface,&owner,&owner_kind)&&owner==(kind==0?&texture.iface:kind==1?&cube.iface:&d.iface)&&owner_kind==(kind==0?3u:kind==1?4u:0u));CHECK(!d9wg_d3d9_core_surface_owner8(&depth.iface,&owner,&owner_kind)&&owner==(depth.texture_child?&depth_texture.iface:&d.iface)&&owner_kind==(depth.texture_child?3u:0u)&&d.iface.refs==17&&color.iface.refs==11&&depth.iface.refs==13);cases++;
 }
 for(UINT bad=0;bad<17;bad++)for(UINT null_color=0;null_color<2;null_color++){
  D9Device d=make_device(),foreign=make_device();D9Texture t={.usage=1},z={.usage=2};D9Surface color={.iface={&g_surface_vtbl,1},.device=&d,.texture=&t,.width=128,.height=96,.private_refs=1},depth={.iface={&g_surface_vtbl,1},.device=&d,.texture=&z,.width=128,.height=96,.private_refs=1};d.render_target_surfaces[0]=&color;d.depth_stencil_surface=&depth;
  if(bad==0)d.profile=9;if(bad==1)d.iface.lpVtbl=&bad_vtbl;if(bad==2)depth.iface.lpVtbl=&bad_vtbl;if(bad==3)depth.device=&foreign;if(bad==4)z.usage=0;if(bad==5)depth.width--;if(bad==6)depth.height--;if(bad==7)depth.multisample=2;if(bad==8)depth.cube_texture=&z;if(bad==9)depth.shadow=&z;if(bad==10)depth.texture=NULL;
  if(bad>=11){if(null_color)continue;if(bad==11)color.iface.lpVtbl=&bad_vtbl;if(bad==12)color.device=&foreign;if(bad==13)t.pool=2;if(bad==14)t.usage=2;if(bad==15)t.usage=0;if(bad==16)t.usage=3;}
  D9Device before=d;D9Surface c=color,p=depth;reset_events(&d,&color,&depth,0);CHECK(d9wg_d3d9_core_set_render_target8(&d.iface,null_color?NULL:&color.iface,&depth.iface)==D3DERR_INVALIDCALL&&!count&&!memcmp(&d,&before,sizeof(d))&&!memcmp(&color,&c,sizeof(color))&&!memcmp(&depth,&p,sizeof(depth)));cases++;
 }
 for(UINT mode=0;mode<4;mode++){
  D9Device d=make_device();D9Texture t={.usage=1};D9Surface old={.iface={&g_surface_vtbl,0},.device=&d,.texture=&t,.width=128,.height=96,.private_refs=1},new_color=old,new_depth={.iface={&g_surface_vtbl,0},.device=&d,.auto_depth_stencil=TRUE,.width=128,.height=96,.private_refs=1};old.private_refs=2;new_color.private_refs=new_depth.private_refs=0;d.render_target_surfaces[0]=&old;d.depth_stencil_surface=&old;
  reset_events(&d,mode&1u?&old:&new_color,mode&2u?&new_depth:NULL,1);D3DVIEWPORT9 viewport=d.viewport;CHECK(d9wg_d3d9_core_set_render_target8(&d.iface,mode&1u?NULL:&new_color.iface,mode&2u?&new_depth.iface:NULL)==D3DERR_DRIVERINTERNALERROR&&!ordering_error&&!ref_error&&!memcmp(&d.viewport,&viewport,sizeof(viewport)));CHECK(d.depth_stencil_unbound==!(mode&2u));cases++;
 }
 for(UINT samples=0;samples<3;samples++)for(UINT depth_null=0;depth_null<2;depth_null++){D9Device d=make_device();d.present.MultiSampleType=samples*2;D9Surface depth={.iface={&g_surface_vtbl,1},.device=&d,.auto_depth_stencil=TRUE,.width=128,.height=96,.multisample=samples*2};D3DVIEWPORT9 previous=d.viewport;reset_events(&d,NULL,depth_null?NULL:&depth,0);CHECK(!d9wg_d3d9_core_set_render_target8(&d.iface,NULL,depth_null?NULL:&depth.iface)&&count==1&&!ordering_error&&!ref_error&&!memcmp(&d.viewport,&previous,sizeof(previous))&&d.depth_stencil_unbound==!!depth_null&&depth.private_refs==!depth_null);cases++;}
 {D9Device d=make_device();D9Surface plain={.iface={&g_surface_vtbl,1},.device=&d,.auto_depth_stencil=TRUE};IUnknown*owner=(void*)99;UINT kind=99;CHECK(d9wg_d3d9_core_surface_owner8(NULL,&owner,&kind)==D3DERR_INVALIDCALL&&!owner&&!kind);CHECK(d9wg_d3d9_core_surface_owner8(&plain.iface,NULL,&kind)==D3DERR_INVALIDCALL);CHECK(d9wg_d3d9_core_surface_owner8(&plain.iface,&owner,NULL)==D3DERR_INVALIDCALL);d.profile=9;owner=(void*)99;kind=99;CHECK(d9wg_d3d9_core_surface_owner8(&plain.iface,&owner,&kind)==D3DERR_INVALIDCALL&&!owner&&!kind);Iface bare={&bad_vtbl,1};CHECK(d9wg_d3d9_core_surface_owner8(&bare,&owner,&kind)==D3DERR_INVALIDCALL&&!owner&&!kind);CHECK(d9wg_d3d9_core_set_render_target8(NULL,NULL,NULL)==D3DERR_INVALIDCALL);cases++;}
 printf("PASS actual core8 target pair/owner: %u checks / %u cases;preflight dimensions/samples/owner/class,shared privatebinding replacement before first emit,NULLcolor/depth,all failureprefixes,live viewport during recording/equality suppression,texture/cube versusstandalone/device borrowedcontainers;explicit native storage/class shims,realSDK/XP lifetimes separate\\n",checks,cases);return 0;}
`);const build=spawnSync(process.env.CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/pair.c','-o',dir+'/pair'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/pair',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
