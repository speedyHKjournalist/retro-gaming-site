'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-depth-stretch-'));
try {
 const source=fs.readFileSync(process.env.D9WG_DEPTH_STRETCH_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8'),start=source.indexOf('static HRESULT WINAPI device_stretch_rect('),end=source.indexOf('static HRESULT WINAPI device_color_fill(',start);assert(start>0&&end>start);
 const code=`#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef int HRESULT,D3DTEXTUREFILTERTYPE;typedef int32_t LONG;typedef uint32_t DWORD;typedef struct {int left,top,right,bottom;}RECT;
typedef struct {uint32_t handle;int in_scene;}D9Device,IDirect3DDevice9;
typedef struct {uint32_t usage;}Texture;
typedef struct {D9Device*device;uint32_t pool,width,height,format,multisample;Texture*texture,*cube_texture;int auto_depth_stencil,swap_chain,texture_child;void*shadow;}D9Surface,IDirect3DSurface9;
#define WINAPI
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DERR_INVALIDCALL -1
#define D3DERR_DRIVERINTERNALERROR -2
#define D3D_OK 0
#define D3DTEXF_NONE 0
#define D3DTEXF_POINT 1
#define D3DTEXF_LINEAR 2
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define ZeroMemory(p,n) memset(p,0,n)
static unsigned emitted,mirrored,wanted_source,wanted_destination;static int fail;
static D9Device*device_from_iface(IDirect3DDevice9*p){return p;}
static D9Surface*surface_from_iface(IDirect3DSurface9*p){return p;}
static unsigned device_compatibility_api(D9Device*d){(void)d;return 9;}
static int surface_target_handle(D9Surface*s,uint32_t*h,uint32_t*l,uint32_t*f){(void)s;*h=42;*l=0;*f=0;return 1;}
static void normalize_rect(const RECT*r,int w,int h,RECT*out){*out=r?*r:(RECT){0,0,w,h};}
static int emit_command(uint16_t op,const void*p,uint32_t n){const D9WGStretchRect*q=p;assert(op==D9WG_OP_STRETCH_RECT&&n==sizeof(*q)&&q->device_handle==7&&q->source_texture_handle==wanted_source&&q->destination_texture_handle==wanted_destination&&q->source_left==0&&q->source_top==0&&q->source_right==64&&q->source_bottom==48&&q->destination_left==0&&q->destination_top==0&&q->destination_right==64&&q->destination_bottom==48);emitted++;return !fail;}
static void mirror_target_copy(D9Surface*s,const RECT*r,D9Surface*d,const RECT*t){(void)s;(void)r;(void)d;(void)t;mirrored++;}
${source.slice(start,end)}
int main(void){unsigned checks=0;D9Device d={7,0};RECT areas[]={{0,0,64,48},{0,0,32,48},{0,48,64,0},{1,0,64,48},{0,0,65,48}};
for(unsigned src_kind=0;src_kind<3;src_kind++)for(unsigned dst_kind=0;dst_kind<3;dst_kind++)for(unsigned src_rect=0;src_rect<6;src_rect++)for(unsigned dst_rect=0;dst_rect<6;dst_rect++)for(unsigned bad=0;bad<5;bad++)for(unsigned filter=0;filter<4;filter++)for(unsigned scene=0;scene<2;scene++)for(unsigned broken=0;broken<2;broken++){
 Texture a={src_kind==2?1:2},b={dst_kind==2?1:2};D9Surface s={.device=&d,.width=64,.height=48,.format=75,.texture=src_kind?&a:NULL,.auto_depth_stencil=!src_kind},t={.device=&d,.width=64,.height=48,.format=75,.texture=dst_kind?&b:NULL,.auto_depth_stencil=!dst_kind};
 if(src_kind==2&&dst_kind==2)continue;
 if(bad==1)t.width=32;if(bad==2)t.height=24;if(bad==3)t.format=80;if(bad==4)t.multisample=4;
 int accepted=src_kind<2&&dst_kind<2&&src_rect<2&&dst_rect<2&&!bad&&filter<3&&!scene;
 emitted=mirrored=0;wanted_source=src_kind?42:D9WG_AUTO_DEPTH_STENCIL_HANDLE;wanted_destination=dst_kind?42:D9WG_AUTO_DEPTH_STENCIL_HANDLE;fail=broken;d.in_scene=scene;
 int result=device_stretch_rect(&d,&s,src_rect?&areas[src_rect-1]:NULL,&t,dst_rect?&areas[dst_rect-1]:NULL,filter);
 assert(result==(accepted?(broken?-2:0):-1));assert(emitted==(unsigned)accepted&&mirrored==(unsigned)(accepted&&!broken));checks+=2;
}
printf("PASS depth StretchRect native contract: %u checks; implicit/explicit depth, whole/partial/flipped rectangles, color conversion, extent/format/sample mismatch, filters, scene and atomic transport failure\\n",checks);}
`;
 fs.writeFileSync(dir+'/test.c',code);const cc=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
 const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
