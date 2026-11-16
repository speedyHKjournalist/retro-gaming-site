"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-volume-lock-")),source=fs.readFileSync(process.env.D8WG_BACKEND_VOLUME_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,n);let b=source.indexOf("{",m.index)+1,d=1;for(;d;b++){if(source[b]==="{")d++;if(source[b]==="}")d--;}return source.slice(m.index,b);}
try{const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_format_layouts.h"
#include "${root}/runtime/d9wg_runtime.h"
typedef uint32_t UINT,DWORD,D3DFORMAT;typedef int32_t BOOL,HRESULT,INT,LONG;typedef uint8_t BYTE;
typedef struct{LONG left,top,right,bottom;}RECT;
typedef struct{UINT Left,Top,Right,Bottom,Front,Back;}D3DBOX;
typedef struct{INT RowPitch,SlicePitch;void*pBits;}D3DLOCKED_BOX;
typedef struct{UINT width,height,depth,row_pitch,row_count,slice_pitch,byte_count;BYTE*shadow;BOOL locked;DWORD lock_flags;D3DBOX lock_box;}D9TextureLevel;
typedef struct{UINT api;}Device;
typedef struct{Device*device;UINT handle,format,level_count;D9TextureLevel*levels;UINT pool,usage;}D9VolumeTexture,IDirect3DVolumeTexture9;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SCRATCH 3
#define D3DUSAGE_DYNAMIC 0x200
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DLOCK_READONLY 16
#define TRACE_REFUSE(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
static void SetRect(RECT*r,INT l,INT t,INT rr,INT b){*r=(RECT){l,t,rr,b};}
static UINT device_compatibility_api(Device*d){return d->api;}static D9VolumeTexture*volume_texture_from_iface(IDirect3DVolumeTexture9*t){return t;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){return d9wg_format_layout(9,f,w,h,b);}
#define D3DFMT_DXT1 0x31545844u
#define D3DFMT_DXT2 0x32545844u
#define D3DFMT_DXT3 0x33545844u
#define D3DFMT_DXT4 0x34545844u
#define D3DFMT_DXT5 0x35545844u
${extract('supported_format8')}
${extract('supported_volume_texture_format8')}
static BOOL multiply_u32(UINT a,UINT b,UINT*out){if(b&&a>UINT32_MAX/b)return FALSE;*out=a*b;return TRUE;}
static int g_transport_lock;static UINT entries,uploads,fail_at;static D9WGTextureRows observed[8];
static void EnterCriticalSection(int*p){assert(!*p);*p=1;entries++;}static void LeaveCriticalSection(int*p){assert(*p==1);*p=0;}
static BOOL emit_texture_rows(const D9WGTextureRows*r){assert(g_transport_lock==1&&uploads<8);observed[uploads++]=*r;return uploads!=fail_at;}
${['emit_level_update_api','emit_level_update','emit_volume_texture_update','volume_texture_lock_box','volume_texture_unlock_box'].map(extract).join('\n')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 volume lock case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&texture_format_layout;BYTE storage[4096];const UINT dims[][3]={{1,1,1},{3,2,3},{7,7,4}};
for(UINT api=8;api<=9;api++)for(UINT pool=0;pool<4;pool++)for(UINT dynamic=0;dynamic<2;dynamic++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);f++)for(UINT dim=0;dim<3;dim++)for(UINT boxmode=0;boxmode<11;boxmode++)for(UINT mip=0;mip<3;mip++)for(UINT ro=0;ro<2;ro++)for(UINT error=0;error<3;error++)for(UINT failure=0;failure<3;failure++){
 const D9WGFormatLayout*q=d9wg_format_layouts+f;UINT w=dims[dim][0],h=dims[dim][1],depth=dims[dim][2],bw=q->block_width,bh=q->block_height,bytes=q->block_bytes,pitch=((w+bw-1)/bw)*bytes,rows=(h+bh-1)/bh,slice=pitch*rows,index=mip<2?mip:0;Device d={api};D9TextureLevel levels[2]={0};levels[index]=(D9TextureLevel){.width=w,.height=h,.depth=depth,.row_pitch=pitch,.row_count=rows,.slice_pitch=slice,.byte_count=slice*depth,.shadow=storage,.locked=error==1};D9TextureLevel*level=levels+index;D9VolumeTexture t={&d,77,q->format,2,levels,pool,dynamic?0x200:0};D3DLOCKED_BOX out={123,456,(void*)(uintptr_t)99};D3DBOX box={0,0,w,h,0,depth};if(boxmode==1){box.Left=bw;box.Front=1;box.Back=depth-1;}if(boxmode==2)box.Front=UINT32_MAX;if(boxmode==3)box.Right=w+1;if(boxmode==4)box.Right=0;if(boxmode==5)box.Left=1;if(boxmode==6)box.Right=w-1;if(boxmode==7)box.Back=1;if(boxmode==8)box.Front=depth-1;if(boxmode==9)box.Bottom=h-1;if(boxmode==10)box.Top=1;UINT flags=ro?16:0;BOOL eligible=(q->apis&(1u<<(api-7)))!=0,valid=eligible&&!(pool==0&&!dynamic)&&!error&&mip<2&&box.Right>box.Left&&box.Bottom>box.Top&&box.Back>box.Front&&box.Right<=w&&box.Bottom<=h&&box.Back<=depth&&!(box.Left%bw)&&!(box.Top%bh);if((box.Right!=w&&box.Right%bw)||(box.Bottom!=h&&box.Bottom%bh))valid=FALSE;D9TextureLevel saved=*level;uploads=entries=0;fail_at=failure;HRESULT hr=volume_texture_lock_box(&t,mip,error==2?NULL:&out,boxmode?&box:NULL,flags);
 CHECK(hr==(valid?0:-1));if(!valid){CHECK(out.RowPitch==123&&out.SlicePitch==456&&out.pBits==(pool==0&&!dynamic&&mip<2&&error!=2?NULL:(void*)(uintptr_t)99)&&!memcmp(level,&saved,sizeof(saved))&&!uploads&&!entries);}else{BYTE*source=storage+box.Front*slice+(box.Top/bh)*pitch+(box.Left/bw)*bytes;CHECK(level->locked&&level->lock_flags==flags&&out.RowPitch==(INT)pitch&&out.SlicePitch==(INT)slice&&out.pBits==source);D3DLOCKED_BOX again={789,987,(void*)(uintptr_t)98};CHECK(volume_texture_lock_box(&t,mip,&again,NULL,flags)==-1&&again.RowPitch==789&&again.SlicePitch==987&&again.pBits==(void*)(uintptr_t)98);CHECK(volume_texture_lock_box(&t,mip,NULL,NULL,0)==-1);BOOL cpu_only=pool==3&&(api==9||(q->format&0x00ffffffu)==0x00545844u||q->format==0x31495441u||q->format==0x32495441u);UINT count=box.Back-box.Front,wanted=ro||cpu_only?0:failure&&failure<=count?failure:count;BOOL failed=!ro&&!cpu_only&&failure&&failure<=count;CHECK(volume_texture_unlock_box(&t,mip)==(failed?-2:0));CHECK(!level->locked&&!level->lock_flags&&!memcmp(&level->lock_box,&(D3DBOX){0},sizeof(D3DBOX))&&uploads==wanted&&entries==(!ro&&!cpu_only)&&!g_transport_lock);for(UINT z=0;z<wanted;z++){D9WGTextureRows*v=observed+z;UINT width=box.Right-box.Left,height=box.Bottom-box.Top;CHECK(v->resource_handle==77&&v->level==mip&&v->z==box.Front+z&&v->x==box.Left&&v->y==box.Top&&v->width==width&&v->height==height&&v->block_height==bh&&v->row_count==(height+bh-1)/bh&&v->row_bytes==((width+bw-1)/bw)*bytes&&v->source_pitch==pitch&&v->source==source+z*slice);}CHECK(volume_texture_unlock_box(&t,mip)==-1);CHECK(volume_texture_unlock_box(&t,2)==-1);CHECK(!memcmp(levels+(index^1u),&(D9TextureLevel){0},sizeof(*level)));}cases++;
}
printf("PASS actual backend8 volume lock/upload: %u checks,%u cases;both APIs/all registry formats,two mips/first-last-partial slices,independent row/slice/box arithmetic,versioned alignment,readonly/invalid output/duplicate locks,one transport scope andfailed-upload cleanup\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/lock.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),dir+"/lock.c","-o",dir+"/lock"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/lock",[],{encoding:"utf8",timeout:60000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
