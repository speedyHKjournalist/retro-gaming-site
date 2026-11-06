"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-cube-lock-")),source=fs.readFileSync(process.env.D8WG_BACKEND_CUBE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,n);let b=source.indexOf("{",m.index)+1,d=1;for(;d;b++){if(source[b]==="{")d++;if(source[b]==="}")d--;}return source.slice(m.index,b);}
try{const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/runtime/d9wg_cpu_texture_layout.h"
#include "${root}/runtime/d9wg_runtime.h"
typedef uint32_t UINT,DWORD,D3DFORMAT;typedef int32_t BOOL,HRESULT,INT,LONG;typedef uint8_t BYTE;
typedef struct{LONG left,top,right,bottom;}RECT;
typedef struct{INT Pitch;void*pBits;}D3DLOCKED_RECT;
typedef struct{UINT width,height,row_pitch,row_count,byte_count;BYTE*shadow;BOOL locked,dc_active;DWORD lock_flags;RECT lock_rect;}D9TextureLevel;
typedef struct{UINT api;}Device;
typedef struct{Device*device;UINT handle,format,usage,level_count;D9TextureLevel*levels;UINT pool;}D9CubeTexture,IDirect3DCubeTexture9;
typedef UINT D3DCUBEMAP_FACES;
#define WINAPI
static D9CubeTexture*cube_from_iface(IDirect3DCubeTexture9*t){return t;}
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_DYNAMIC 512
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DLOCK_READONLY 16
#define TRACE_REFUSE(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
static void SetRect(RECT*r,INT l,INT t,INT rr,INT b){*r=(RECT){l,t,rr,b};}
static UINT device_compatibility_api(Device*d){return d->api;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){return d9wg_format_layout(9,f,w,h,b);}
static BOOL multiply_u32(UINT a,UINT b,UINT*out){if(b&&a>UINT32_MAX/b)return FALSE;*out=a*b;return TRUE;}
static D9WGTextureRows observed;static UINT uploads;static BOOL fail_upload;
static BOOL emit_texture_rows(const D9WGTextureRows*r){observed=*r;uploads++;return !fail_upload;}
${['texture_levels_mapped','emit_level_update_api','emit_level_update','emit_cube_texture_update','cube_lock_rect','cube_unlock_rect'].map(extract).join('\n')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 cube lock case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&texture_format_layout;(void)&emit_level_update;BYTE storage[4096];const UINT sizes[]={1,3,7},faces[]={0,1,2,3,4,5,6,UINT32_MAX};
for(UINT api=8;api<=9;api++)for(UINT pool=0;pool<4;pool++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);f++)for(UINT dim=0;dim<3;dim++)for(UINT rectmode=0;rectmode<7;rectmode++)for(UINT fi=0;fi<8;fi++)for(UINT mip=0;mip<4;mip++)for(UINT ro=0;ro<2;ro++)for(UINT error=0;error<3;error++){
 const D9WGFormatLayout*q=d9wg_format_layouts+f;UINT w=sizes[dim],h=w,bw=q->block_width,bh=q->block_height,bytes=q->block_bytes,pitch=((w+bw-1)/bw)*bytes,rows=(h+bh-1)/bh,face=faces[fi],index=face<6&&mip<3?face*3+mip:0;Device d={api};D9TextureLevel levels[18]={0};levels[index]=(D9TextureLevel){.width=w,.height=h,.row_pitch=pitch,.row_count=rows,.byte_count=pitch*rows,.shadow=storage,.locked=error==1};D9TextureLevel*level=levels+index;D9CubeTexture t={&d,77,q->format,0,3,levels,pool};D3DLOCKED_RECT out={123,(void*)(uintptr_t)99};RECT r={0,0,(LONG)w,(LONG)h};if(rectmode==1)r.left=bw;if(rectmode==2)r.left=-1;if(rectmode==3)r.right=w+1;if(rectmode==4)r.right=0;if(rectmode==5)r.left=1;if(rectmode==6)r.right=w-1;UINT flags=ro?16:0;BOOL broken=api==9&&(q->format==0x31495441u||q->format==0x32495441u);UINT mapped_pitch=broken?w:pitch;
 BOOL bounds=r.left>=0&&r.top>=0&&r.right>r.left&&r.bottom>r.top&&(UINT)r.right<=w&&(UINT)r.bottom<=h;
 BOOL aligned=!((UINT)r.left%bw)&&!((UINT)r.top%bh)&&((UINT)r.right==w||!((UINT)r.right%bw))&&((UINT)r.bottom==h||!((UINT)r.bottom%bh));
 BOOL eligible=(q->apis&(1u<<(api-7)))!=0,valid=eligible&&!(pool==0&&q->format<0x10000u)&&!error&&face<6&&mip<3&&((bounds&&(aligned||broken))||(api==9&&pool!=0));
 D9TextureLevel saved=*level;uploads=0;HRESULT hr=cube_lock_rect(&t,face,mip,error==2?NULL:&out,rectmode?&r:NULL,flags);
 CHECK(hr==(valid?0:-1));if(!valid){CHECK(out.Pitch==(api==8&&error!=2?0:123)&&out.pBits==(api==8&&error!=2?NULL:(void*)(uintptr_t)99)&&!memcmp(level,&saved,sizeof(saved))&&!uploads);}else{CHECK(level->locked&&level->lock_flags==flags&&out.Pitch==(INT)mapped_pitch&&(uintptr_t)out.pBits==(uintptr_t)storage+(uintptr_t)(broken?(int64_t)r.top*w+r.left:(int64_t)r.top/bh*pitch+(int64_t)r.left/bw*bytes));D3DLOCKED_RECT again={456,(void*)(uintptr_t)98};CHECK(cube_lock_rect(&t,face,mip,&again,NULL,flags)==-1&&again.Pitch==(api==8?0:456)&&again.pBits==(api==8?NULL:(void*)(uintptr_t)98));CHECK(cube_lock_rect(&t,face,mip,NULL,NULL,0)==-1);fail_upload=dim==2;hr=cube_unlock_rect(&t,face,mip);CHECK(hr==(!ro&&fail_upload?-2:0)&&!level->locked&&!level->lock_flags&&!memcmp(&level->lock_rect,&(RECT){0},sizeof(RECT)));CHECK(uploads==!ro);if(!ro){RECT uploaded=bounds&&aligned?r:(RECT){0,0,(LONG)w,(LONG)h};UINT width=uploaded.right-uploaded.left;CHECK(observed.resource_handle==77&&observed.level==mip&&observed.z==face&&observed.x==(UINT)uploaded.left&&!observed.y&&observed.width==width&&observed.height==h&&observed.block_height==bh&&observed.row_count==rows&&observed.row_bytes==((width+bw-1)/bw)*bytes&&observed.source_pitch==pitch&&observed.source==storage+(UINT)uploaded.top/bh*pitch+(UINT)uploaded.left/bw*bytes);}CHECK(cube_unlock_rect(&t,face,mip)==(api==9?0:-1));CHECK(cube_unlock_rect(&t,6,mip)==-1);CHECK(cube_unlock_rect(&t,face,3)==-1);for(UINT other=0;other<18;other++)if(other!=index)CHECK(!memcmp(levels+other,&(D9TextureLevel){0},sizeof(*level)));}cases++;
}
/* A DC on the last face/mip owns the complete cube until ReleaseDC. */
{Device d={9};D9TextureLevel levels[18]={0};levels[0]=(D9TextureLevel){.width=4,.height=4,.row_pitch=16,.row_count=4,.byte_count=64,.shadow=storage};levels[17].dc_active=TRUE;levels[17].locked=TRUE;D9CubeTexture t={&d,77,21,0,3,levels,1};D3DLOCKED_RECT out={123,(void*)(uintptr_t)99};uploads=0;CHECK(cube_lock_rect(&t,0,0,&out,NULL,0)==-1&&out.Pitch==123&&out.pBits==(void*)(uintptr_t)99);CHECK(cube_unlock_rect(&t,0,0)==0&&levels[17].dc_active&&levels[17].locked&&!uploads);levels[17].dc_active=FALSE;CHECK(cube_lock_rect(&t,0,0,&out,NULL,16)==0);CHECK(cube_unlock_rect(&t,0,0)==0&&!uploads);}
printf("PASS actual backend8 cube lock/upload: %u checks,%u cases;six faces/three mips,both APIs/all registry formats,native CPU-pool raw offsets and bounded GPU upload ROIs,subrect offset/pitch,read-only/refusal/upload-failure state andface/mip isolation\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/lock.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/lock.c","-o",dir+"/lock"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/lock",[],{encoding:"utf8",timeout:60000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
