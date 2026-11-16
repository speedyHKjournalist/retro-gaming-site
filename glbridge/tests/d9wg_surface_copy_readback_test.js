"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-copy-readback-")),source=fs.readFileSync(process.env.D9WG_READBACK_FRONTEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(name){const m=new RegExp("static (?:HRESULT|BOOL) "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{fs.writeFileSync(dir+"/copy.c",`#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "runtime/d9wg_surface_readback.h"
typedef int HRESULT,D3DFORMAT;typedef uint8_t BYTE;typedef struct{unsigned handle;}D9Device;
typedef struct{const void *lpVtbl;}Iface;
typedef struct{BYTE*shadow;UINT row_pitch,byte_count;BOOL shadow_valid,locked;}D9TextureLevel;
typedef struct{DWORD usage;UINT handle;}D9Texture;
typedef struct{Iface iface;D9Device*device;UINT width,height,format,pool,level;D9Texture*texture;void*swap_chain;BYTE*shadow;UINT row_pitch,byte_count;D9TextureLevel*data;BOOL locked;}D9Surface;typedef Iface IDirect3DSurface9;
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SYSTEMMEM 2
#define D3DPOOL_SCRATCH 3
#define D3DUSAGE_RENDERTARGET 1
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define TRACE_REFUSE(x) (x)
#define HOSTLOG_FAILED(...) ((void)0)
#define CopyMemory memcpy
static unsigned g_surface_vtbl,reads,mode;
static D9WGProcess state,*g_process=&state;static D9Device device={19};
static D9Surface *current_rt,*current_dst;
static D9Surface*surface_from_iface(IDirect3DSurface9*s){return (D9Surface*)s;}
static D9TextureLevel*surface_texture_level(D9Surface*s){return s->data;}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*bytes,UINT*rows,UINT*total){if(f!=21&&f!=22)return FALSE;*bytes=w*4;*rows=h;*total=*bytes*h;return TRUE;}
BOOL d9wg_transport_submit(D9WGProcess*p,BOOL present){assert(p==g_process&&!present);return mode!=2;}
${extract("submit_readback_batch_locked")}
int d9wg_surface_readback(D9WGProcess*p,const D9WGReadbackSurface*q,uint32_t row_bytes,uint32_t rows,uint8_t*out,uint32_t bytes,BOOL(*flush)(BOOL),D9WGReadbackDiagnostic*d){
 reads++;assert(p==g_process&&row_bytes==12&&rows==2&&q->device_handle==19&&q->texture_handle==(current_rt->texture?73u:0u)&&q->level==3&&q->format==current_dst->format&&q->width==3&&q->height==2&&q->destination_pitch==16&&q->face==0&&bytes==32&&flush==submit_readback_batch_locked);memset(d,0,sizeof(*d));if(!flush(FALSE))return 2;if(mode)return mode;for(unsigned y=0;y<2;y++)for(unsigned x=0;x<16;x++)out[y*16+x]=(BYTE)(y*31+x);return 0;}
${extract("copy_surface_to_system_memory")}
static HRESULT call_copy(D9Device*d,D9Surface*s,D9Surface*t,BOOL f){return copy_surface_to_system_memory(d,s?&s->iface:NULL,t?&t->iface:NULL,f);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"surface copy line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){BYTE source_bytes[24],destination[48];for(unsigned i=0;i<24;i++)source_bytes[i]=(BYTE)(i*7);unsigned cases=0;
 for(unsigned front=0;front<2;front++)for(unsigned fast=0;fast<2;fast++)for(unsigned child=0;child<2;child++)for(unsigned result=0;result<(fast?1u:4u);result++){
  memset(destination,0xcc,sizeof(destination));reads=0;mode=result;D9Texture texture={1,73};D9TextureLevel sl={source_bytes,12,24,fast,FALSE},dl={destination,16,32,FALSE,FALSE};D9Surface rt={{&g_surface_vtbl},&device,3,2,front?22:21,0,3,&texture,NULL,NULL,0,0,&sl,FALSE},dst={{&g_surface_vtbl},&device,3,2,21,2,0,NULL,NULL,child?NULL:destination,16,32,child?&dl:NULL,FALSE};current_rt=&rt;current_dst=&dst;if(front&&child)dst.pool=3;
  HRESULT hr=call_copy(&device,&rt,&dst,front);CHECK(hr==(result==1?-1:result?-2:0));CHECK(reads==!fast);CHECK(dl.shadow_valid==(child&&!result));
  for(unsigned y=0;y<2;y++)for(unsigned b=0;b<16;b++){BYTE wanted=result?0xcc:fast?(b<12?source_bytes[y*12+b]:0xcc):(BYTE)(y*31+b);if(!result&&front&&b<12&&b%4==3)wanted=255;CHECK(destination[y*16+b]==wanted);}for(unsigned b=32;b<48;b++)CHECK(destination[b]==0xcc);cases++;
 }
 for(unsigned bad=0;bad<19;bad++){
  reads=mode=0;D9Device foreign={20};D9Texture texture={1,73};D9TextureLevel sl={source_bytes,12,24,FALSE,FALSE},dl={destination,16,32,FALSE,FALSE};D9Surface rt={{&g_surface_vtbl},&device,3,2,21,0,3,&texture,NULL,NULL,0,0,&sl,FALSE},dst={{&g_surface_vtbl},&device,3,2,21,2,0,NULL,NULL,destination,16,32,NULL,FALSE};current_rt=&rt;current_dst=&dst;BOOL front=FALSE;
  if(bad==0)rt.iface.lpVtbl=NULL;if(bad==1)dst.iface.lpVtbl=NULL;if(bad==2)rt.device=&foreign;if(bad==3)dst.device=&foreign;if(bad==4)dst.width++;if(bad==5)dst.height++;if(bad==6)texture.usage=0;if(bad==7)dst.pool=3;if(bad==8)dst.format=22;if(bad==9){front=TRUE;dst.pool=0;}if(bad==10){front=TRUE;dst.format=22;}if(bad==11){front=TRUE;rt.format=23;}if(bad==12)dst.shadow=NULL;if(bad==13)rt.format=dst.format=23;
  if(bad==15)rt.locked=TRUE;if(bad==16)dst.locked=TRUE;if(bad==17)sl.locked=TRUE;if(bad==18){dst.data=&dl;dl.locked=TRUE;}
  if(bad!=14)CHECK(call_copy(&device,&rt,&dst,front)==-1&&!reads);else{rt.texture=NULL;rt.swap_chain=&state;sl.shadow_valid=FALSE;CHECK(!call_copy(&device,&rt,&dst,FALSE)&&reads==1);}cases++;
 }
 reads=0;CHECK(call_copy(&device,NULL,current_dst,FALSE)==-1&&!reads);CHECK(call_copy(&device,current_rt,NULL,FALSE)==-1&&!reads);cases+=2;
 mode=2;CHECK(!submit_readback_batch_locked(FALSE));mode=0;CHECK(submit_readback_batch_locked(FALSE));cases+=2;
 printf("PASS actual D3D9 copy/readback entry: %u shadow/transport/front/child/error cases, exact request identity and capacity, opaque front alpha, padding and guards\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root+"/tests/native","-I",root,dir+"/copy.c","-o",dir+"/copy"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/copy",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
