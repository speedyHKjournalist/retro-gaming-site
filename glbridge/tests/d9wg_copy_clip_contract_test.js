"use strict";
// Native Wine contracts, exercised against the actual guest entry points.
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D9WG_COPY_CLIP_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-copy-clip-"));
function extract(name){const m=new RegExp("^static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;++end){if(source[end]==="{")++depth;if(source[end]==="}")--depth;}return source.slice(m.index,end);}
try {
fs.writeFileSync(dir+"/test.c",`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,D3DTEXTUREFILTERTYPE;typedef int BOOL,HRESULT;
typedef struct{int left,top,right,bottom;}RECT;
typedef struct{UINT api,handle;BOOL recording,pure,in_scene;float clip_planes[6][4],recorded[6][4];UINT recorded_mask;}D9Device,IDirect3DDevice9;
typedef struct{UINT usage;}Texture;
typedef struct{D9Device*device;Texture*texture,*cube_texture;void*swap_chain,*shadow;UINT width,height,format,pool;BOOL auto_depth_stencil,texture_child,gpu;}D9Surface,IDirect3DSurface9;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DTEXF_NONE 0
#define D3DTEXF_POINT 1
#define D3DTEXF_LINEAR 2
#define D9_MAX_CLIP_PLANES 6
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define PURE_DEVICE_REFUSE(d,label) do{if((d)->pure)return D3DERR_INVALIDCALL;}while(0)
static D9Device*device_from_iface(D9Device*d){return d;}
static D9Surface*surface_from_iface(D9Surface*s){return s;}
static UINT device_compatibility_api(D9Device*d){return d->api;}
static void SetRect(RECT*r,int l,int t,int right,int b){*r=(RECT){l,t,right,b};}
static unsigned emitted,mirrored,handles,checks;static BOOL transport_fail;static D9WGStretchRect copy_command;static D9WGSetClipPlane clip_command;
static BOOL surface_target_handle(D9Surface*s,uint32_t*h,uint32_t*l,uint32_t*f){++handles;*h=s->format;*l=2;*f=0;return s->gpu;}
static BOOL emit_command(UINT op,const void*p,UINT size){++emitted;if(op==D9WG_OP_STRETCH_RECT){assert(size==sizeof(copy_command));memcpy(&copy_command,p,size);}else{assert(op==D9WG_OP_SET_CLIP_PLANE&&size==sizeof(clip_command));memcpy(&clip_command,p,size);}return !transport_fail;}
static void mirror_target_copy(D9Surface*s,const RECT*a,D9Surface*d,const RECT*b){(void)s;(void)a;(void)d;(void)b;++mirrored;}
static BOOL state_block_record_clip_plane(D9Device*d,UINT i,const float*p){assert(i<6);if(!d->recording)return 0;d->recorded_mask|=1u<<i;memcpy(d->recorded[i],p,16);return 1;}
${['normalize_rect','device_stretch_rect','device_set_clip_plane','device_get_clip_plane'].map(extract).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Copy/clip contract line %u: %s\\n",__LINE__,#v);return 1;}++checks;}while(0)
static D9Surface make_surface(D9Device*d,Texture*t,UINT kind,UINT pool){D9Surface s={.device=d,.width=32,.height=24,.format=21,.pool=pool,.gpu=1};t->usage=kind==1||kind==3?1:0;if(kind<4){s.texture=t;s.texture_child=kind>=2;}else if(kind==4){s.swap_chain=(void*)1;}else{s.cube_texture=t;s.texture_child=1;}return s;}
int main(void){
/* 0 plain,1 standalone RT,2 texture,3 RT texture,4 backbuffer,5 cube.
 * Native Wine's complete color-copy table is independent of the proxy's
 * backing texture representation. Every rejection must precede transport. */
for(UINT api=8;api<=9;++api)for(UINT src=0;src<6;++src)for(UINT dst=0;dst<6;++dst)for(UINT sp=0;sp<4;++sp)for(UINT dp=0;dp<4;++dp)for(UINT filter=0;filter<4;++filter)for(UINT foreign=0;foreign<3;++foreign){
 D9Device d={.api=api,.handle=7},other={.api=api};Texture st,dt;D9Surface s=make_surface(foreign==1?&other:&d,&st,src,sp),t=make_surface(foreign==2?&other:&d,&dt,dst,dp);
 BOOL valid=api==8||(!sp&&!dp&&filter<3&&!foreign&&((dst==1||dst==3||dst==4)||(src==0&&dst==0)));
 emitted=mirrored=handles=0;HRESULT hr=device_stretch_rect(&d,&s,NULL,&t,NULL,filter);
 CHECK(hr==(valid?0:-1));CHECK(emitted==(UINT)valid&&mirrored==(UINT)valid&&handles==(valid?2u:0u));
 if(valid)CHECK(copy_command.device_handle==7&&copy_command.source_left==0&&copy_command.source_top==0&&copy_command.source_right==32&&copy_command.source_bottom==24&&copy_command.destination_right==32&&copy_command.destination_bottom==24&&copy_command.filter_point==(filter<2));
}
D9Device d={.api=9,.handle=7};Texture st,dt;D9Surface s=make_surface(&d,&st,0,0),t=make_surface(&d,&dt,1,0);RECT a={1,2,11,12},b={3,4,13,14};emitted=mirrored=handles=0;CHECK(!device_stretch_rect(&d,&s,&a,&t,&b,2)&&emitted==1&&mirrored==1&&copy_command.source_left==1&&copy_command.source_bottom==12&&copy_command.destination_left==3&&copy_command.destination_bottom==14);
for(UINT api=8;api<=9;++api)for(UINT depth=0;depth<2;++depth)for(UINT scene=0;scene<2;++scene){
 d.api=api;d.in_scene=scene;st.usage=dt.usage=depth?2:1;s.texture=&st;s.texture_child=0;
 emitted=mirrored=handles=0;BOOL valid=api==8||!depth||!scene;
 CHECK(device_stretch_rect(&d,&s,NULL,&t,NULL,0)==(valid?0:-1));
 CHECK(emitted==(UINT)valid&&mirrored==(UINT)valid&&handles==(valid?2u:0u));
}
d.api=9;d.in_scene=0;st.usage=dt.usage=1;
transport_fail=1;mirrored=0;CHECK(device_stretch_rect(&d,&s,NULL,&t,NULL,0)==-2&&!mirrored);transport_fail=0;
for(UINT api=8;api<=9;++api){d=(D9Device){.api=api,.handle=9};float plane[4]={2,8,5,0},out[4];
 for(UINT i=0;i<64;++i){plane[3]=(float)i;emitted=0;HRESULT hr=device_set_clip_plane(&d,i,plane);CHECK(hr==((api==9||i<6)?0:-1));if(!hr)CHECK(emitted==1&&clip_command.index<6&&!memcmp(clip_command.plane,plane,16));else CHECK(!emitted);}
 for(UINT i=0;i<64;++i){memset(out,0xff,16);HRESULT hr=device_get_clip_plane(&d,i,out);CHECK(hr==((api==9||i<6)?0:-1));if(!hr)CHECK(out[0]==2&&out[1]==8&&out[2]==5&&out[3]==(float)(i<5?i:api==9?63:5));}
 float before[6][4];memcpy(before,d.clip_planes,sizeof(before));d.recording=1;plane[3]=99;emitted=0;HRESULT hr=device_set_clip_plane(&d,UINT32_MAX,plane);CHECK(hr==(api==9?0:-1)&&!emitted&&!memcmp(before,d.clip_planes,sizeof(before)));CHECK(d.recorded_mask==(api==9?32u:0u));if(api==9)CHECK(!memcmp(d.recorded[5],plane,16));
 CHECK(device_set_clip_plane(&d,0,NULL)==-1&&device_get_clip_plane(&d,0,NULL)==-1);d.pure=1;CHECK(device_get_clip_plane(&d,0,out)==-1);
}
printf("PASS actual copy/clip contracts: %u checks; native color surface/pool/filter/owner matrix, normalized rectangles, no transport on refusal, failed-submit mirror safety, API8 limits, API9 clip aliases and recorded state isolation\\n",checks);return 0;}
`);
const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);
const r=spawnSync(dir+"/test",[],{encoding:"utf8",timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
