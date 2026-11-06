'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-update-surface-'));
try{
const source=fs.readFileSync(process.env.D9WG_UPDATE_SURFACE_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8'),start=source.indexOf('static HRESULT WINAPI device_update_surface('),end=source.indexOf('\n/*',start);
assert(start>0&&end>start);
fs.writeFileSync(dir+'/test.c',`#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int HRESULT,BOOL;typedef int32_t LONG;typedef unsigned UINT;typedef unsigned char BYTE;
typedef struct{LONG left,top,right,bottom;}RECT;typedef struct{LONG x,y;}POINT;
typedef struct{UINT width,height,row_pitch,row_count,byte_count;BYTE*shadow;BOOL locked,shadow_valid;}D9TextureLevel;
typedef struct{UINT pool;D9TextureLevel data;}Texture;
typedef struct{UINT id;}D9Device,IDirect3DDevice9;
typedef struct{void*lpVtbl;D9Device*device;Texture*texture,*cube_texture;UINT format,pool,width,height,row_pitch,level,cube_face;BYTE*shadow;BOOL locked;}D9Surface,IDirect3DSurface9;
static int g_surface_vtbl;
#define WINAPI
#define TRACE_REFUSE(v) (v)
#define D3DERR_INVALIDCALL -1
#define D3DERR_DRIVERINTERNALERROR -2
#define E_OUTOFMEMORY -3
#define D3D_OK 0
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SYSTEMMEM 2
#define HEAP_ZERO_MEMORY 1
#define FALSE 0
#define CopyMemory memcpy
#define SetRect(r,l,t,x,b) (*(r)=(RECT){l,t,x,b})
static unsigned allocations,uploads;static int oom,transport_failure;static BYTE gpu[64];
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*p,UINT flags,UINT n){(void)p;(void)flags;allocations++;return oom?NULL:calloc(1,n);}
static D9Device*device_from_iface(IDirect3DDevice9*p){return p;}
static D9Surface*surface_from_iface(IDirect3DSurface9*p){return p;}
static D9TextureLevel*surface_texture_level(D9Surface*s){return s->texture?&s->texture->data:s->cube_texture?&s->cube_texture->data:NULL;}
static int texture_format_layout(UINT f,UINT*w,UINT*h,UINT*b){*w=*h=f==0x31545844?4:1;*b=f==0x31545844?8:4;return 1;}
static int multiply_u32(UINT a,UINT b,UINT*out){*out=a*b;return b==0||*out/b==a;}
static int emit_texture_update(Texture*t,UINT level,const RECT*r){assert(!level && t->data.row_pitch==16);uploads++;if(transport_failure)return 0;
 for(int y=r->top;y<r->bottom;y++)memcpy(gpu+y*16+r->left*4,t->data.shadow+y*16+r->left*4,(r->right-r->left)*4);return 1;}
static int emit_cube_texture_update(Texture*t,UINT face,UINT level,const RECT*r){assert(face==3);return emit_texture_update(t,level,r);}
${source.slice(start,end)}
int main(void){unsigned checks=0;BYTE pixels[64];for(unsigned i=0;i<64;i++)pixels[i]=(BYTE)(i+1);
D9Device d={1},other={2};RECT regions[]={{0,0,4,4},{1,1,3,3},{-1,0,2,2},{0,0,5,4},{2,2,1,1},{0,0,0,1}};
for(unsigned cube=0;cube<2;cube++)for(unsigned existing=0;existing<2;existing++)for(unsigned r=0;r<6;r++)for(unsigned bad=0;bad<7;bad++){
 Texture t={.pool=0,.data={4,4,16,4,64,NULL,0,0}};
 if(existing){t.data.shadow=malloc(64);memset(t.data.shadow,0x77,64);t.data.shadow_valid=1;}
 D9Surface src={.lpVtbl=&g_surface_vtbl,.device=&d,.format=21,.pool=2,.width=4,.height=4,.row_pitch=16,.shadow=pixels};
 D9Surface dst={.lpVtbl=&g_surface_vtbl,.device=&d,.format=21,.pool=0,.width=4,.height=4,.cube_face=3};
 if(cube)dst.cube_texture=&t;else dst.texture=&t;
 if(bad==1)src.locked=1;if(bad==2)t.data.locked=1;if(bad==3)src.device=&other;if(bad==4)dst.format=22;
 oom=bad==5;transport_failure=bad==6;allocations=uploads=0;memset(gpu,0x77,64);POINT at={regions[r].left,regions[r].top};
 int valid=r<2&&bad!=1&&bad!=2&&bad!=3&&bad!=4;
 int expected=!valid?-1:oom&&!existing?-3:transport_failure?-2:0;
 int hr=device_update_surface(&d,&src,&regions[r],&dst,&at);assert(hr==expected);
 assert(allocations==(unsigned)(valid&&!existing));assert(uploads==(unsigned)(valid&&(!oom||existing)));
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<16;x++){
   int inside=y>=(UINT)regions[r].top&&y<(UINT)regions[r].bottom&&x/4>=(UINT)regions[r].left&&x/4<(UINT)regions[r].right;
   assert(gpu[y*16+x]==(hr==0&&inside?pixels[y*16+x]:0x77));
 }
 if(hr==-2)assert(!t.data.shadow_valid);
 if(hr==0)assert(t.data.shadow_valid==(existing||r==0));
 free(t.data.shadow);checks++;
}
/* A sub-block tail can be copied only when it reaches both image edges. */
for(unsigned source_size=2;source_size<=4;source_size+=2)for(unsigned dest_size=2;dest_size<=4;dest_size+=2){
 Texture t={.pool=0,.data={dest_size,dest_size,8,1,8,NULL,0,0}};
 D9Surface src={.lpVtbl=&g_surface_vtbl,.device=&d,.format=0x31545844,.pool=2,.width=source_size,.height=source_size,.row_pitch=8,.shadow=pixels};
 D9Surface dst={.lpVtbl=&g_surface_vtbl,.device=&d,.format=0x31545844,.pool=0,.width=dest_size,.height=dest_size,.texture=&t};
 RECT area={0,0,2,2};oom=transport_failure=0;allocations=uploads=0;
 if(source_size==2&&dest_size==2)continue; /* Successful BC uploads use a different row layout. */
 assert(device_update_surface(&d,&src,&area,&dst,NULL)==-1);assert(!allocations&&!uploads);checks++;
}
printf("PASS UpdateSurface native transactions: %u combinations; lazy allocation, ROI preservation, cube identity, invalid/locked/foreign input, OOM and failed transport\\n",checks);}
`);
const cc=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
