'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-color-fill-'));
try {
 const source=fs.readFileSync(process.env.D9WG_COLOR_FILL_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8'),start=source.indexOf('static HRESULT WINAPI device_color_fill('),end=source.indexOf('static HRESULT WINAPI device_set_scissor_rect(',start);assert(start>0&&end>start);
 const code=`#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef int HRESULT;typedef uint32_t DWORD,D3DCOLOR;typedef struct {int left,top,right,bottom;}RECT;
typedef struct {uint32_t handle;}D9Device,IDirect3DDevice9;
typedef struct {uint32_t usage;}Texture;
typedef struct {D9Device*device;uint32_t pool,width,height;Texture*texture,*cube_texture;int auto_depth_stencil,swap_chain,texture_child;}D9Surface,IDirect3DSurface9;
#define WINAPI
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DERR_INVALIDCALL -1
#define D3DERR_DRIVERINTERNALERROR -2
#define D3D_OK 0
#define TRACE_REFUSE(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
static unsigned emitted,mirrored;static int fail;
static D9Device*device_from_iface(IDirect3DDevice9*p){return p;}
static D9Surface*surface_from_iface(IDirect3DSurface9*p){return p;}
static int surface_target_handle(D9Surface*s,uint32_t*h,uint32_t*l,uint32_t*f){(void)s;*h=42;*l=3;*f=5;return 1;}
static void normalize_rect(const RECT*r,int w,int h,RECT*out){*out=r?*r:(RECT){0,0,w,h};}
static int emit_command(uint16_t op,const void*p,uint32_t n){const D9WGColorFill*q=p;assert(op==D9WG_OP_COLOR_FILL&&n==sizeof(*q)&&q->texture_handle==42&&q->level==3&&q->face==5&&q->device_handle==7&&q->color==0xdeadbeef&&q->left==2&&q->top==3&&q->right==5&&q->bottom==7);emitted++;return !fail;}
static void mirror_target_color_fill(D9Surface*s,const RECT*r,D3DCOLOR c){(void)s;(void)r;(void)c;mirrored++;}
${source.slice(start,end)}
int main(void){unsigned checks=0;D9Device d={7},other={8};RECT area={2,3,5,7};
for(unsigned pool=0;pool<4;pool++)for(unsigned kind=0;kind<5;kind++)for(unsigned usage=0;usage<4;usage++)for(unsigned foreign=0;foreign<2;foreign++)for(unsigned broken=0;broken<2;broken++){
 Texture t={usage};D9Surface s={.device=foreign?&other:&d,.pool=pool,.width=16,.height=16,.texture=kind==0?&t:NULL,.cube_texture=kind==1?&t:NULL,.auto_depth_stencil=kind==2,.swap_chain=kind==3,.texture_child=kind<2};
 /* Native ColorFill accepts default-pool render-target texture levels and
  * backbuffers; rejects depth, ordinary texture levels and nondefault pools. */
 int accepted=!foreign&&!pool&&(kind==3||kind==4||((kind<2)&&(usage==1)));
 emitted=mirrored=0;fail=broken;int result=device_color_fill(&d,&s,&area,0xdeadbeef);
 assert(result==(accepted?(broken?-2:0):-1));assert(emitted==(unsigned)accepted&&mirrored==(unsigned)(accepted&&!broken));checks+=2;
}
emitted=0;assert(device_color_fill(&d,NULL,NULL,0)==-1&&!emitted);checks++;
printf("PASS ColorFill native contracts: %u checks; depth/texture/pool/device rejection before wire emission, cube/mip identity and transport failure\\n",checks);}
`;
 fs.writeFileSync(dir+'/test.c',code);const cc=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
 const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
