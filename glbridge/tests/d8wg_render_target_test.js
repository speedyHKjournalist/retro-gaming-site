"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-target-")),source=fs.readFileSync(process.env.D8WG_TARGET_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8");
function extract(name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{fs.writeFileSync(dir+"/target.c",`#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD;typedef int BOOL,HRESULT,D3DCUBEMAP_FACES,D3DFORMAT,D3DPOOL;typedef UINT D3DMULTISAMPLE_TYPE;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DMULTISAMPLE_NONE 0
#define SUCCEEDED(x) ((x)>=0)
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DERR_NOTFOUND (-4)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SYSTEMMEM 2
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D8WG_TRACE_ERROR(x) (x)
#define D8WG_TRACE(...) ((void)0)
#define FAILED(x) ((x)<0)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct{const void*lpVtbl;}Iface;typedef Iface IDirect3DSurface8,IDirect3DTexture8,IDirect3DCubeTexture8;
typedef struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}D3DVIEWPORT8;
typedef struct{UINT Format,Type,Usage,Pool,Size,MultiSampleType,Width,Height;}D3DSURFACE_DESC;
typedef struct{UINT width,height;}D8TextureLevel;
typedef struct D8Device D8Device;typedef D8Device IDirect3DDevice8;
typedef struct Texture{Iface iface;unsigned refs,freed;D8Device*device;UINT handle,level_count,usage,pool,format;D3DMULTISAMPLE_TYPE multisample_type;D8TextureLevel*levels;}D8Texture,D8CubeTexture;
typedef struct Surface{Iface iface;unsigned refs,heap;D8Texture*texture;D8CubeTexture*cube;D8Device*device;UINT level,face;BOOL backbuffer,depth_stencil;D3DSURFACE_DESC desc;}D8Surface;
typedef struct D8StateSnapshot{BOOL viewport_mask;D3DVIEWPORT8 viewport;}D8StateSnapshot;
typedef struct D8StateBlock{D8StateSnapshot state;}D8StateBlock;
struct D8Device{UINT handle;D8Texture*render_target_texture;D8CubeTexture*render_target_cube;UINT render_target_level,render_target_face;D8Surface*depth_surface;BOOL depth_surface_enabled;struct{UINT BackBufferWidth,BackBufferHeight,AutoDepthStencilFormat;D3DMULTISAMPLE_TYPE MultiSampleType;}present;struct{UINT Width,Height;}display_mode;D3DVIEWPORT8 viewport;D8StateBlock*recording_state_block;};
static unsigned g_surface_vtbl,count,fail_at,live_views,allocated_views;
static D8Device*active;
static struct{UINT opcode;BYTE bytes[128];}commands[32];
static D8Device*device_from_iface(D8Device*d){return d;}
static D8Surface*surface_from_iface(Iface*i){return (D8Surface*)i;}
static void add_texture(Iface*i){D8Texture*t=(D8Texture*)i;assert(t->refs&&!t->freed);t->refs++;}
static void release_texture(Iface*i){D8Texture*t=(D8Texture*)i;assert(t->refs&&!t->freed);if(!--t->refs){assert(active->render_target_texture!=t&&active->render_target_cube!=t);t->freed=1;}}
#define IDirect3DTexture8_AddRef add_texture
#define IDirect3DCubeTexture8_AddRef add_texture
#define IDirect3DTexture8_Release release_texture
#define IDirect3DCubeTexture8_Release release_texture
static unsigned IDirect3DSurface8_AddRef(Iface*i){D8Surface*s=(D8Surface*)i;assert(s->refs);return ++s->refs;}
static unsigned IDirect3DSurface8_Release(Iface*i){D8Surface*s=(D8Surface*)i;assert(s->refs);unsigned refs=--s->refs;if(!refs&&s->heap){if(s->texture)release_texture(&s->texture->iface);if(s->cube)release_texture(&s->cube->iface);free(s);live_views--;}return refs;}
static HRESULT surface_get_desc(Iface*i,D3DSURFACE_DESC*d){D8Surface*s=(D8Surface*)i;if(s->texture||s->cube){D8Texture*t=s->texture?s->texture:s->cube;D8TextureLevel*l=t->levels+(s->cube?s->face*t->level_count:0)+s->level;*d=(D3DSURFACE_DESC){.Format=t->format,.Pool=t->pool,.Usage=t->usage,.MultiSampleType=t->multisample_type,.Width=l->width,.Height=l->height};}else*d=s->desc;return 0;}
static HRESULT make_view(D8Texture*t,BOOL cube,UINT face,UINT level,Iface**out){D8Surface*s=calloc(1,sizeof(*s));assert(s);s->iface.lpVtbl=&g_surface_vtbl;s->refs=s->heap=1;s->device=t?t->device:active;s->level=level;s->face=face;if(t){if(cube)s->cube=t;else s->texture=t;add_texture(&t->iface);}else{s->backbuffer=TRUE;s->desc=(D3DSURFACE_DESC){.Usage=1,.Pool=0,.Width=64,.Height=64};}*out=&s->iface;live_views++;allocated_views++;return 0;}
static HRESULT texture_get_surface_level(Iface*t,UINT level,Iface**out){return make_view((D8Texture*)t,FALSE,0,level,out);}
static HRESULT cube_get_surface(Iface*t,D3DCUBEMAP_FACES face,UINT level,Iface**out){return make_view((D8Texture*)t,TRUE,face,level,out);}
static HRESULT create_backbuffer_surface(D8Device*d,const void*p,BOOL blocker,Iface**out){assert(d==active&&p==&d->present&&blocker);return make_view(NULL,FALSE,0,0,out);}
static HRESULT create_standalone_surface(D8Device*d,UINT w,UINT h,D3DFORMAT f,DWORD u,D3DPOOL p,BOOL l,BOOL b,BOOL depth,BOOL blocker,Iface**out){(void)d;(void)w;(void)h;(void)f;(void)u;(void)p;(void)l;(void)b;(void)depth;(void)blocker;(void)out;assert(0);return -1;}
static BOOL emit_command(uint16_t op,const void*p,UINT n){assert(count<32&&n<=128);commands[count].opcode=op;memcpy(commands[count].bytes,p,n);count++;return !fail_at||count!=fail_at;}
${["cube_level","surface_device","surface_gpu_location","bound_render_target_level","current_render_target_size","set_live_viewport","device_set_viewport","device_get_viewport","clear_render_target_binding","device_set_render_target","device_get_render_target","device_get_depth_surface"].map(extract).join("\n")}
static HRESULT bind(D8Device*d,D8Surface*c,D8Surface*z){return device_set_render_target(d,c?&c->iface:NULL,z?&z->iface:NULL);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 target line %u: %s count=%u\\n",__LINE__,#v,count);return 1;}}while(0)
static void reset_events(void){count=fail_at=0;memset(commands,0,sizeof(commands));}
int main(void){D8Device d={.handle=7,.present={64,64,75,0},.display_mode={64,64},.viewport={0,0,64,64,0,1}},foreign={.handle=8};active=&d;D8TextureLevel levels[12],tl[1]={{128,96}},dl[1]={{128,128}};for(unsigned f=0;f<6;f++)for(unsigned l=0;l<2;l++)levels[f*2+l]=(D8TextureLevel){64u>>l,64u>>l};D8CubeTexture cube={.refs=1,.device=&d,.handle=21,.level_count=2,.usage=1,.pool=0,.format=22,.levels=levels};D8Texture tex={.refs=1,.device=&d,.handle=22,.level_count=1,.usage=1,.pool=0,.format=22,.levels=tl},deptex={.refs=1,.device=&d,.handle=23,.level_count=1,.usage=2,.pool=0,.format=75,.levels=dl};D8Surface color={.iface={&g_surface_vtbl},.refs=1,.cube=&cube,.device=&d},target={.iface={&g_surface_vtbl},.refs=1,.texture=&tex,.device=&d},depth={.iface={&g_surface_vtbl},.refs=1,.texture=&deptex,.device=&d,.depth_stencil=TRUE},small=depth,back={.iface={&g_surface_vtbl},.refs=1,.device=&d,.backbuffer=TRUE,.desc={.Usage=1,.Pool=0,.Width=64,.Height=64}};unsigned cases=0;
 reset_events();CHECK(!bind(&d,&target,&depth)&&count==3&&tex.refs==2&&depth.refs==2);CHECK(d.viewport.Width==128&&d.viewport.Height==96);CHECK(((D9WGSetDepthStencilSurfaceLevel*)commands[1].bytes)->width==128&&((D9WGSetDepthStencilSurfaceLevel*)commands[1].bytes)->height==128&&((D9WGSetDepthStencilSurfaceLevel*)commands[1].bytes)->depth_texture_handle==23);cases++;
 for(unsigned face=0;face<6;face++)for(unsigned mip=0;mip<2;mip++){color.face=face;color.level=mip;reset_events();CHECK(!bind(&d,&color,&depth));CHECK(d.render_target_cube==&cube&&!d.render_target_texture&&d.render_target_level==mip&&d.render_target_face==face&&cube.refs==2&&tex.refs==1);CHECK(commands[0].opcode==D9WG_OP_SET_RENDER_TARGET&&commands[1].opcode==D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL);D9WGSetRenderTarget*c=(void*)commands[0].bytes;CHECK(c->device_handle==7&&c->color_texture_handle==21&&c->color_level==mip&&c->color_face==face);UINT width,height;current_render_target_size(&d,&width,&height);CHECK(width==(64u>>mip)&&height==width&&bound_render_target_level(&d)==levels+face*2+mip&&d.viewport.Width==width&&d.viewport.Height==height);Iface*out=NULL;CHECK(!device_get_render_target(&d,&out));D8Surface*q=(D8Surface*)out;CHECK(q->cube==&cube&&q->face==face&&q->level==mip);IDirect3DSurface8_Release(out);cases++;}
 reset_events();D3DVIEWPORT8 too_wide={0,0,33,32,0,1},before_vp=d.viewport;CHECK(device_set_viewport(&d,&too_wide)==-1&&!count&&!memcmp(&d.viewport,&before_vp,sizeof(before_vp)));cases++;
 D3DVIEWPORT8 custom={1,2,30,28,.1f,.9f};CHECK(!device_set_viewport(&d,&custom));reset_events();CHECK(!bind(&d,NULL,&depth)&&count==1&&commands[0].opcode==D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL&&!memcmp(&d.viewport,&custom,sizeof(custom))&&cube.refs==2&&depth.refs==2);cases++;
 D8TextureLevel valid_size={32,48};D8Texture valid_texture=deptex;valid_texture.levels=&valid_size;valid_texture.handle=24;D8Surface valid_depth=depth;valid_depth.texture=&valid_texture;valid_depth.refs=1;reset_events();CHECK(!bind(&d,NULL,&valid_depth)&&count==1&&((D9WGSetDepthStencilSurfaceLevel*)commands[0].bytes)->width==32&&((D9WGSetDepthStencilSurfaceLevel*)commands[0].bytes)->height==48&&((D9WGSetDepthStencilSurfaceLevel*)commands[0].bytes)->depth_texture_handle==24&&!memcmp(&d.viewport,&custom,sizeof(custom)));Iface*queried_depth=NULL;CHECK(!device_get_depth_surface(&d,&queried_depth)&&queried_depth==&valid_depth.iface);IDirect3DSurface8_Release(queried_depth);cases++;
 reset_events();CHECK(!bind(&d,NULL,NULL)&&count==1&&d.render_target_cube==&cube&&d.render_target_face==5&&d.render_target_level==1&&!memcmp(&d.viewport,&custom,sizeof(custom))&&depth.refs==1&&!d.depth_surface_enabled);Iface*out=(void*)123;CHECK(device_get_depth_surface(&d,&out)==D3DERR_NOTFOUND&&!out);cases++;
 for(unsigned bad=0;bad<9;bad++){reset_events();D8Device before=d;unsigned cr=cube.refs,dr=depth.refs;D8Surface c=color,z=depth;D8TextureLevel tiny={16,16};D8Texture small_tex=deptex;small_tex.levels=&tiny;small.texture=&small_tex;if(bad==0)z=small;if(bad==1)c.device=&foreign,c.cube=NULL,c.backbuffer=TRUE,c.desc=back.desc;if(bad==2)z.device=&foreign,z.texture=NULL;if(bad==3)c.iface.lpVtbl=NULL;if(bad==4)z.iface.lpVtbl=NULL;if(bad==5)c.cube=NULL,c.texture=NULL,c.backbuffer=FALSE,c.desc=(D3DSURFACE_DESC){.Width=32,.Height=32,.Pool=2};if(bad==6)cube.usage=0;if(bad==7)cube.pool=2;if(bad==8)c.depth_stencil=TRUE;CHECK(bind(&d,bad==0?NULL:&c,&z)==-1&&!count&&!memcmp(&before,&d,sizeof(d))&&cube.refs==cr&&depth.refs==dr);cube.usage=1;cube.pool=0;cases++;}
 /* Target side effects stay live while explicit SetViewport remains deferred. */
 D8StateBlock recorded={0};recorded.state.viewport=(D3DVIEWPORT8){9,9,9,9,0,1};d.recording_state_block=&recorded;reset_events();color.face=4;color.level=0;CHECK(!bind(&d,&color,NULL)&&d.viewport.Width==64&&d.viewport.Height==64&&!recorded.state.viewport_mask&&recorded.state.viewport.X==9);custom=(D3DVIEWPORT8){2,3,20,21,0,1};CHECK(!device_set_viewport(&d,&custom)&&recorded.state.viewport_mask&&recorded.state.viewport.X==2&&d.viewport.X==0&&d.viewport.Width==64);d.recording_state_block=NULL;cases++;
 /* Matching sample types, including depth-only validation against the live target. */
 D8Texture mstex=tex,msdepth=deptex;mstex.multisample_type=4;msdepth.multisample_type=4;D8Surface mscolor=target,msz=depth,implicit0=depth;implicit0.texture=NULL;implicit0.desc=(D3DSURFACE_DESC){.Width=128,.Height=128,.MultiSampleType=0};mscolor.texture=&mstex;msz.texture=&msdepth;D8TextureLevel mssize={128,128};msdepth.levels=&mssize;
 reset_events();CHECK(!bind(&d,&mscolor,&msz)&&mstex.refs==2&&msz.refs==2);cases++;
 for(unsigned mode=0;mode<4;mode++){reset_events();D8Device before=d;CHECK(bind(&d,mode==0?&mscolor:mode==1?&target:NULL,mode==1?&msz:mode==3?&implicit0:&depth)==-1&&!count&&!memcmp(&d,&before,sizeof(d)));cases++;}
 reset_events();CHECK(!bind(&d,NULL,NULL)&&!bind(&d,NULL,&msz));cases++;
 reset_events();back.desc.MultiSampleType=4;d.present.MultiSampleType=4;CHECK(!bind(&d,&back,&msz)&&mstex.refs==1);D8Device before_ms=d;reset_events();CHECK(bind(&d,NULL,&depth)==-1&&!count&&!memcmp(&d,&before_ms,sizeof(d)));cases++;
 reset_events();back.desc.MultiSampleType=0;d.present.MultiSampleType=0;CHECK(!bind(&d,&color,NULL)&&msz.refs==1);cases++;
 /* Rebinding the only retained parent ref must never free before AddRef. */
 release_texture(&cube.iface);CHECK(cube.refs==1);color.face=3;color.level=1;reset_events();CHECK(!bind(&d,&color,NULL)&&cube.refs==1&&!cube.freed);cases++;
 reset_events();CHECK(!bind(&d,&back,NULL)&&!d.render_target_cube&&!d.render_target_texture&&!d.render_target_level&&!d.render_target_face&&cube.freed&&commands[0].opcode==D9WG_OP_SET_RENDER_TARGET&&((D9WGSetRenderTarget*)commands[0].bytes)->color_texture_handle==0&&d.viewport.Width==64);cases++;
 /* Queue failures retain the accepted call's binding refs, then cleanup releases them. */
 cube.refs=1;cube.freed=0;reset_events();fail_at=1;CHECK(bind(&d,&color,NULL)==-2&&cube.refs==2&&d.render_target_cube==&cube);fail_at=0;clear_render_target_binding(&d);CHECK(cube.refs==1&&!d.render_target_cube&&!d.render_target_texture&&!d.render_target_level&&!d.render_target_face);cases++;
 reset_events();CHECK(!device_get_render_target(&d,&out)&&((D8Surface*)out)->backbuffer);IDirect3DSurface8_Release(out);CHECK(device_get_render_target(&d,NULL)==-1);D3DVIEWPORT8 queried;CHECK(!device_get_viewport(&d,&queried)&&!memcmp(&queried,&d.viewport,sizeof(queried)));CHECK(!live_views&&allocated_views==13);cases++;
 printf("PASS actual D3D8 render targets: %u cube face/mip, size/viewport/query/ref, NULL color/depth, recorded/live viewport, validation atomicity, sole-ref rebind and accepted-prefix cleanup cases\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/target.c","-o",dir+"/target"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/target",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
