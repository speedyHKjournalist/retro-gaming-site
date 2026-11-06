"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-texture-lock-")),source=fs.readFileSync(process.env.D8WG_BACKEND_TEXTURE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
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
typedef struct{Device*device;UINT handle,format,usage,level_count;D9TextureLevel*levels;UINT pool;BOOL private_back_buffer;}D9Texture;
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DUSAGE_DYNAMIC 512
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DLOCK_READONLY 16
#define D3DPOOL_DEFAULT 0
#define TRACE_REFUSE(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
static void SetRect(RECT*r,INT l,INT t,INT rr,INT b){*r=(RECT){l,t,rr,b};}
static UINT device_compatibility_api(Device*d){return d->api;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){return d9wg_format_layout(9,f,w,h,b);}
static BOOL multiply_u32(UINT a,UINT b,UINT*out){if(b&&a>UINT32_MAX/b)return FALSE;*out=a*b;return TRUE;}
static D9WGTextureRows observed;static UINT uploads;static BOOL fail_upload;
static BOOL emit_texture_rows(const D9WGTextureRows*r){observed=*r;uploads++;return !fail_upload;}
${['texture_levels_mapped','emit_level_update_api','emit_level_update','emit_texture_update','texture_lock_level','texture_unlock_level'].map(extract).join('\n')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 texture lock case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&texture_format_layout;(void)&emit_level_update;BYTE storage[4096];const UINT sizes[]={1,3,7};
for(UINT api=8;api<=9;api++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);f++)for(UINT dim=0;dim<3;dim++)for(UINT rectmode=0;rectmode<5;rectmode++)for(UINT usage=0;usage<3;usage++)for(UINT ro=0;ro<2;ro++)for(UINT error=0;error<4;error++){
 const D9WGFormatLayout*q=d9wg_format_layouts+f;UINT w=sizes[dim],h=w+1,bw=q->block_width,bh=q->block_height,bytes=q->block_bytes,pitch=((w+bw-1)/bw)*bytes,rows=(h+bh-1)/bh;Device d={api};D9TextureLevel level={.width=w,.height=h,.row_pitch=pitch,.row_count=rows,.byte_count=pitch*rows,.shadow=error==1?NULL:storage,.locked=error==2};D9Texture t={&d,77,q->format,usage,1,&level,0,0};for(UINT n=0;n<sizeof(storage);n++)storage[n]=(BYTE)(n*31+7);D3DLOCKED_RECT out={123,(void*)(uintptr_t)99};RECT r={0,0,(LONG)w,(LONG)h};if(rectmode==1)r.left=bw;if(rectmode==2)r.left=-1;if(rectmode==3)r.right=w+1;if(rectmode==4)r.right=0;UINT index=error==3?1:0,flags=ro?16:0;BOOL broken=api==9&&(q->format==0x31495441u||q->format==0x32495441u);UINT mapped_pitch=broken?w:pitch;BOOL eligible=(q->apis&(1u<<(api-7)))!=0,valid=eligible&&!(q->format<0x10000u)&&!usage&&!error&&rectmode<2&&r.left<r.right;D9TextureLevel saved=level;uploads=0;HRESULT hr=texture_lock_level(&t,index,&out,rectmode?&r:NULL,flags);
 CHECK(hr==(valid?0:-1));if(!valid){CHECK(out.Pitch==123&&out.pBits==(void*)(uintptr_t)99&&!memcmp(&level,&saved,sizeof(level))&&!uploads);}else{CHECK(level.locked&&level.lock_flags==flags&&out.Pitch==(INT)mapped_pitch&&(uintptr_t)out.pBits==(uintptr_t)storage+(broken?(UINT)r.top*w+(UINT)r.left:(UINT)r.top/bh*pitch+(UINT)r.left/bw*bytes));D3DLOCKED_RECT again={456,(void*)(uintptr_t)98};CHECK(texture_lock_level(&t,0,&again,NULL,flags)==-1&&again.Pitch==456&&again.pBits==(void*)(uintptr_t)98);CHECK(texture_lock_level(&t,0,NULL,NULL,0)==-1);fail_upload=dim==2;hr=texture_unlock_level(&t,0);CHECK(hr==(!ro&&fail_upload?-2:0)&&!level.locked&&!level.lock_flags&&!memcmp(&level.lock_rect,&(RECT){0},sizeof(RECT)));CHECK(uploads==!ro);if(!ro){UINT width=w-r.left;CHECK(observed.resource_handle==77&&!observed.level&&!observed.z&&observed.x==(UINT)r.left&&!observed.y&&observed.width==width&&observed.height==h&&observed.block_height==bh&&observed.row_count==rows&&observed.row_bytes==((width+bw-1)/bw)*bytes&&observed.source_pitch==pitch&&observed.source==storage+(UINT)r.top/bh*pitch+(UINT)r.left/bw*bytes);}CHECK(texture_unlock_level(&t,0)==0);CHECK(texture_unlock_level(&t,1)==-1);}cases++;
}
/* Wine D3D8/9 CPU-pool textures accept invalid and partial-block ROIs.
 * Assert raw API offsets without dereferencing them, then read every emitted
 * byte under ASan: upload storage must remain within the allocated level. */
const RECT areas[]={{0,0,8,8},{1,1,3,3},{-4,0,0,8},{0,-4,8,0},{20,20,24,24},{0,0,0,8},{7,7,3,3},{0,0,16,16},{INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX}};
for(UINT api=8;api<=9;api++)for(UINT pool=0;pool<4;pool++)for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);f++)for(UINT r=0;r<sizeof(areas)/sizeof(*areas);r++)for(UINT ro=0;ro<2;ro++){
 const D9WGFormatLayout*q=d9wg_format_layouts+f;UINT bw=q->block_width,bh=q->block_height,bytes=q->block_bytes,pitch=(8/bw)*bytes,rows=8/bh;Device d={api};D9TextureLevel level={.width=8,.height=8,.row_pitch=pitch,.row_count=rows,.byte_count=pitch*rows,.shadow=storage};D9Texture t={&d,77,q->format,0,1,&level,pool,0};D3DLOCKED_RECT out={123,(void*)(uintptr_t)99};const RECT*area=areas+r;BOOL broken=api==9&&(q->format==0x31495441u||q->format==0x32495441u);UINT mapped_pitch=broken?8:pitch;BOOL eligible=(q->apis&(1u<<(api-7)))!=0,aligned=r==0||(r==1&&bw==1&&bh==1),valid=eligible&&!(pool==0&&q->format<0x10000u)&&(aligned||(broken&&r==1)||pool!=0);uploads=0;fail_upload=FALSE;
 HRESULT hr=texture_lock_level(&t,0,&out,area,ro?16:0);CHECK(hr==(valid?0:-1));
 if(valid){uintptr_t offset=(uintptr_t)(broken?(int64_t)area->top*8+area->left:(int64_t)area->top/bh*pitch+(int64_t)area->left/bw*bytes);CHECK((uintptr_t)out.pBits==(uintptr_t)storage+offset&&out.Pitch==(INT)mapped_pitch);CHECK(texture_unlock_level(&t,0)==0&&uploads==!ro);if(!ro){CHECK((uintptr_t)observed.source>=(uintptr_t)storage&&(uintptr_t)observed.source+(observed.row_count-1)*observed.source_pitch+observed.row_bytes<=(uintptr_t)storage+level.byte_count);volatile BYTE checksum=0;for(UINT y=0;y<observed.row_count;y++)for(UINT x=0;x<observed.row_bytes;x++)checksum^=observed.source[y*observed.source_pitch+x];(void)checksum;if(!aligned)CHECK(observed.source==storage&&observed.width==8&&observed.height==8);}}
 else CHECK(!level.locked&&!uploads&&out.Pitch==123&&out.pBits==(void*)(uintptr_t)99);cases++;
}
/* Retail pool/usage permission is independent of ROI and lock flags. */
for(UINT api=8;api<=9;api++)for(UINT pool=0;pool<4;pool++)for(UINT dynamic=0;dynamic<2;dynamic++) {
 Device d={api};D9TextureLevel level={.width=4,.height=4,.row_pitch=16,.row_count=4,.byte_count=64,.shadow=storage};
 D9Texture t={&d,77,21,dynamic?512:0,1,&level,pool,0};D3DLOCKED_RECT out={0};
 BOOL permitted=pool!=0||dynamic;fail_upload=FALSE;uploads=0;
 CHECK(texture_lock_level(&t,0,&out,NULL,16)==(permitted?0:-1));
 CHECK(texture_unlock_level(&t,0)==(permitted?0:-1));CHECK(!uploads);cases++;
}
/* GDI ownership on another mip excludes maps without consuming the lock. */
{Device d={9};D9TextureLevel levels[2]={{.width=4,.height=4,.row_pitch=16,.row_count=4,.byte_count=64,.shadow=storage},{.dc_active=TRUE,.locked=TRUE}};D9Texture t={&d,77,21,0,2,levels,1,0};D3DLOCKED_RECT out={123,(void*)(uintptr_t)99};uploads=0;CHECK(texture_lock_level(&t,0,&out,NULL,0)==-1&&out.Pitch==123&&out.pBits==(void*)(uintptr_t)99);CHECK(texture_unlock_level(&t,0)==0&&levels[1].dc_active&&levels[1].locked&&!uploads);}
printf("PASS actual backend8 texture lock/upload: %u checks,%u cases;both APIs/all registry formats/odd andBC extents,subrect source offset andwhole pitch,read-only/duplicate/null/level/target refusals,upload failure unlock cleanup\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/lock.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),dir+"/lock.c","-o",dir+"/lock"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/lock",[],{encoding:"utf8",timeout:60000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
