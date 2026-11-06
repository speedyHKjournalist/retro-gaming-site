"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.D9WG_DIRTY_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8');
function extract(n){const m=new RegExp('^static [^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,n);let i=source.indexOf('{',m.index)+1,d=1;for(;d;i++){if(source[i]==='{')d++;if(source[i]==='}')d--;}return source.slice(m.index,i);}
const header=process.env.D9WG_DIRTY_HEADER||root+'/runtime/d9wg_texture_dirty2d.h';
const copyHeader=process.env.D9WG_DIRTY_COPY_HEADER||root+'/runtime/d9wg_texture_dirty_copy2d.h';
const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "${root}/runtime/d9wg_cpu_texture_layout.h"
#include "${root}/runtime/d9wg_runtime.h"
typedef uint32_t UINT,DWORD,D3DFORMAT;typedef int32_t BOOL,HRESULT,INT,LONG;typedef uint8_t BYTE;
typedef struct RECT {LONG left,top,right,bottom;} RECT;
typedef struct {UINT Left,Top,Right,Bottom,Front,Back;} D3DBOX;
typedef struct {INT Pitch;void *pBits;} D3DLOCKED_RECT;
typedef struct D9TextureDirtyRect {RECT rect;struct D9TextureDirtyRect *next;} D9TextureDirtyRect;
typedef struct {UINT width,height,depth,row_pitch,row_count,slice_pitch,byte_count;BYTE *shadow;BOOL shadow_valid,locked,dc_active,dirty_full;DWORD lock_flags;RECT lock_rect;D9TextureDirtyRect *dirty_rects;} D9TextureLevel;
typedef struct D9Texture D9Texture;
typedef struct D9Buffer {UINT pool;BOOL full_upload_pending;struct D9Buffer *next_device_resource;} D9VertexBuffer,D9IndexBuffer;
typedef struct {UINT api;BOOL managed_texture_dirty;D9Texture *textures[20],*texture_resources;D9VertexBuffer *vertex_buffers;D9IndexBuffer *index_buffers;} D9Device;
struct D9Texture {D9Device *device;UINT handle,width,height,edge_length,format,usage,level_count,pool,autogen_filter;BOOL private_back_buffer,dirty_pending;D9TextureLevel *levels;D9Texture *next_device_resource;};
typedef D9Texture IDirect3DTexture9,D9CubeTexture,D9VolumeTexture;
typedef D9Device IDirect3DDevice9;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_MANAGED 1
#define D3DPOOL_SYSTEMMEM 2
#define D3DLOCK_READONLY 16
#define D3DLOCK_NO_DIRTY_UPDATE 32768
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DUSAGE_DYNAMIC 512
#define D3DUSAGE_AUTOGENMIPMAP 1024
#define D9_MAX_TEXTURE_BINDINGS 20
#define TRACE_REFUSE(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
static unsigned allocations,frees,allocation_attempts,fail_allocation;
static void *HeapAlloc(void *h,unsigned flags,size_t n){(void)h;if(++allocation_attempts==fail_allocation)return NULL;void *p=flags?calloc(1,n):malloc(n);if(p)allocations++;return p;}
static int HeapFree(void*h,unsigned f,void*p){(void)h;(void)f;if(p){frees++;free(p);}return 1;}
static void SetRect(RECT*r,INT l,INT t,INT rr,INT b){*r=(RECT){l,t,rr,b};}
static UINT device_compatibility_api(D9Device*d){return d->api;}
static D9Device *device_from_iface(D9Device*d){return d;}
static D9Texture *texture_from_iface(D9Texture*t){return t;}
static BYTE gpu[4][18][256];static unsigned uploads,fail_upload;
static BOOL emit_texture_update(D9Texture*t,UINT level,const RECT*r){if(++uploads==fail_upload)return FALSE;D9TextureLevel*l=t->levels+level;UINT bw,bh,bytes;if(!d9wg_format_layout(t->device->api,t->format,&bw,&bh,&bytes))return FALSE;UINT x=r->left/bw*bytes,n=((r->right+bw-1)/bw)*bytes-x;for(UINT y=r->top/bh;y<(r->bottom+bh-1)/bh;y++)memcpy(gpu[t->handle][level]+y*l->row_pitch+x,l->shadow+y*l->row_pitch+x,n);return TRUE;}
#include "${header}"
#define D9WG_COPY_LEVEL D9TextureLevel
#define D9WG_COPY_2D_TYPE D9Texture
#define D9WG_COPY_CUBE_TYPE D9CubeTexture
#define D9WG_COPY_VOLUME_TYPE D9VolumeTexture
#define D9WG_COPY_MARK_VALID(l) ((l)->shadow_valid=TRUE)
#define D9WG_COPY_UPLOAD_2D(t,l,r) emit_texture_update(t,l,r)
#define D9WG_COPY_UPLOAD_CUBE(t,f,l,r) emit_texture_update(t,l,r)
#define D9WG_COPY_UPLOAD_VOLUME(t,l,b) (TRUE)
#include "${root}/runtime/d9wg_texture_copy_bridge.h"
static BOOL emit_cube_texture_update(D9CubeTexture*t,UINT face,UINT level,const RECT*r){if(++uploads==fail_upload)return FALSE;D9TextureLevel*l=t->levels+face*t->level_count+level;UINT bw,bh,bytes;if(!d9wg_format_layout(t->device->api,t->format,&bw,&bh,&bytes))return FALSE;UINT x=r->left/bw*bytes,n=((r->right+bw-1)/bw)*bytes-x;for(UINT y=r->top/bh;y<(r->bottom+bh-1)/bh;y++)memcpy(gpu[t->handle][face*t->level_count+level]+y*l->row_pitch+x,l->shadow+y*l->row_pitch+x,n);return TRUE;}
#include "${process.env.D9WG_LEGACY_COLOR_HEADER||root+'/runtime/d9wg_texture_legacy_color.h'}"
#include "${copyHeader}"
static unsigned generations;static BYTE generated_pixel;
static BOOL emit_generate_mips(D9Device*d,UINT handle,UINT filter,BOOL deferred){(void)d;(void)filter;(void)deferred;generations++;generated_pixel=gpu[handle][0][0];return TRUE;}
${['texture_generate_mip_sublevels','texture_levels_mapped','texture_lock_level','texture_unlock_level','texture_add_dirty_rect','texture_preload','device_evict_managed_resources'].map(extract).join('\n')}
static UINT checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"dirty texture line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
static void init_texture(D9Texture*t,D9Device*d,D9TextureLevel*levels,BYTE*storage,UINT handle,UINT pool){memset(t,0,sizeof(*t));memset(levels,0,sizeof(*levels)*3);*t=(D9Texture){.device=d,.handle=handle,.width=8,.height=8,.format=21,.level_count=3,.pool=pool,.levels=levels};UINT offset=0;for(UINT i=0;i<3;i++){UINT w=8>>i;levels[i]=(D9TextureLevel){.width=w,.height=w,.row_pitch=w*4,.row_count=w,.byte_count=w*w*4,.shadow=storage+offset};offset+=w*w*4;}texture_dirty_all(t);}
static void init_color(D9Texture*t,D9Device*d,D9TextureLevel*l,BYTE*storage,UINT handle,UINT format,BOOL cube){memset(l,0,18*sizeof(*l));*t=(D9Texture){.device=d,.handle=handle,.width=8,.height=8,.edge_length=8,.format=format,.level_count=3,.pool=2,.levels=l};UINT off=0;for(UINT f=0;f<(cube?6u:1u);f++)for(UINT i=0;i<3;i++){UINT w=8>>i,n=format==23?2:4;l[f*3+i]=(D9TextureLevel){.width=w,.height=w,.row_pitch=w*n,.row_count=w,.byte_count=w*w*n,.shadow=storage+off};off+=w*w*n;}texture_dirty_all(t);}
int main(void){(void)&d9wg_update_texture_common;(void)&d9wg_commit_texture_copy_level;
for(UINT api=8;api<=9;api++){
 D9Device device={.api=api};D9Texture a,b;D9TextureLevel al[3],bl[3];BYTE as[336],bs[336];D3DLOCKED_RECT lock;
 init_texture(&a,&device,al,as,1,1);device.textures[0]=&a;device.texture_resources=&a;
 /* Initial dirty uploads every mip, including writes through READONLY. */
 CHECK(!texture_lock_level(&a,0,&lock,NULL,16));memset(as,0x11,256);memset(as+256,0x22,64);memset(as+320,0x33,16);uploads=0;
 CHECK(!texture_unlock_level(&a,0)&&!uploads);CHECK(flush_managed_textures(&device)&&uploads==3);
 CHECK(gpu[1][0][0]==0x11&&gpu[1][1][0]==0x22&&gpu[1][2][0]==0x33);
 CHECK(!device.managed_texture_dirty&&!a.dirty_pending);
 for(UINT flags=16;flags<=32768;flags*=2048){CHECK(!texture_lock_level(&a,0,&lock,NULL,flags));memset(as,0x44,336);CHECK(!texture_unlock_level(&a,0));CHECK(flush_managed_textures(&device)&&uploads==3&&gpu[1][0][0]==0x11&&gpu[1][1][0]==0x22);}
 /* Disjoint dirty rectangles must leave an intervening CPU write hidden. */
 RECT left={0,0,2,2},right={6,6,8,8};CHECK(!texture_add_dirty_rect(&a,&left));CHECK(!texture_add_dirty_rect(&a,&right));CHECK(flush_managed_textures(&device));
 CHECK(gpu[1][0][0]==0x44&&gpu[1][0][(7*8+7)*4]==0x44&&gpu[1][0][(3*8+3)*4]==0x11);
 CHECK(gpu[1][1][0]==0x44&&gpu[1][1][(2*4+2)*4]==0x22);
 /* Allocation failure does not partly mark the mip chain or acquire a lock. */
 RECT mid={2,2,4,4};UINT allocated=allocations-frees;D9TextureLevel before[3];memcpy(before,al,sizeof(al));
 fail_allocation=allocation_attempts+2;lock=(D3DLOCKED_RECT){123,(void*)99};CHECK(texture_lock_level(&a,0,&lock,&mid,0)==-3);CHECK(!memcmp(before,al,sizeof(al))&&lock.Pitch==123&&lock.pBits==(void*)99&&allocations-frees==allocated);fail_allocation=0;
 CHECK(!texture_add_dirty_rect(&a,&mid));fail_upload=uploads+1;CHECK(!flush_managed_textures(&device)&&a.dirty_pending&&device.managed_texture_dirty);fail_upload=0;CHECK(flush_managed_textures(&device)&&!a.dirty_pending);
 memset(as,0x55,336);CHECK(!device_evict_managed_resources(&device));CHECK(flush_managed_textures(&device)&&gpu[1][0][0]==0x55&&gpu[1][2][0]==0x55);
 memset(as,0x66,336);CHECK(!texture_add_dirty_rect(&a,NULL));texture_preload(&a);CHECK(!a.dirty_pending&&gpu[1][2][0]==0x66);
 /* Explicit generation observes the uploaded base before its GPU command. */
 a.usage=1024;memset(as,0xaa,336);CHECK(!texture_add_dirty_rect(&a,NULL));UINT prior_generations=generations;texture_generate_mip_sublevels(&a);CHECK(generations==prior_generations+1&&generated_pixel==0xaa&&!a.dirty_pending);
 memset(as,0xbb,336);CHECK(!texture_add_dirty_rect(&a,NULL));fail_upload=uploads+1;texture_generate_mip_sublevels(&a);CHECK(generations==prior_generations+1&&a.dirty_pending);fail_upload=0;texture_generate_mip_sublevels(&a);CHECK(generated_pixel==0xbb&&!a.dirty_pending);
 /* UpdateTexture consumes dirty state and copies exact CPU rectangles. */
 init_texture(&a,&device,al,as,1,2);init_texture(&b,&device,bl,bs,2,0);memset(as,0x77,336);memset(bs,0x88,336);CHECK(!texture_update_dirty(&a,&b));CHECK(bs[0]==0x77&&bs[335]==0x77);
 uploads=0;memset(as,0x99,336);CHECK(!texture_update_dirty(&a,&b)&&!uploads&&bs[0]==0x77);
 CHECK(!texture_add_dirty_rect(&a,&left));CHECK(!texture_add_dirty_rect(&a,&right));CHECK(!texture_update_dirty(&a,&b));CHECK(bs[0]==0x99&&bs[(7*8+7)*4]==0x99&&bs[(3*8+3)*4]==0x77);
 CHECK(!texture_lock_level(&a,0,&lock,&mid,0));CHECK(!texture_update_dirty(&a,&b)&&bs[(2*8+2)*4]==0x99);CHECK(!texture_unlock_level(&a,0));
 CHECK(!texture_add_dirty_rect(&a,&left));fail_upload=uploads+1;CHECK(texture_update_dirty(&a,&b)==-2&&a.dirty_pending);fail_upload=0;CHECK(!texture_update_dirty(&a,&b)&&!a.dirty_pending);
 /* Invalid destination layout is rejected even with no dirty regions. */
 bl[0].row_pitch++;CHECK(texture_update_dirty(&a,&b)==-1);bl[0].row_pitch--;
 RECT bad={0,0,9,8};CHECK(texture_add_dirty_rect(&a,&bad)==-1&&!a.dirty_pending);
 for(UINT i=0;i<3;i++){texture_dirty_clear(al+i);texture_dirty_clear(bl+i);}CHECK(allocations==frees);
}
/* Block alignment at every mip must stay within owned CPU bytes. */
for(UINT format_index=0;format_index<3;format_index++){
 const UINT formats[]={0x31545844u,0x35545844u,41u};D9Device d={.api=9};D9Texture t;D9TextureLevel levels[3];BYTE storage[336];init_texture(&t,&d,levels,storage,3,1);t.format=formats[format_index];UINT bw,bh,bytes,offset=0;CHECK(d9wg_format_layout(9,t.format,&bw,&bh,&bytes));
 for(UINT i=0;i<3;i++){D9TextureLevel*l=levels+i;l->row_pitch=((l->width+bw-1)/bw)*bytes;l->row_count=(l->height+bh-1)/bh;l->byte_count=l->row_pitch*l->row_count;l->shadow=storage+offset;offset+=l->byte_count;}
 memset(storage,0x11,sizeof(storage));CHECK(texture_flush_dirty(&t));memset(storage,0x88,sizeof(storage));RECT r={1,1,3,3};CHECK(!texture_add_dirty_rect(&t,&r));CHECK(texture_flush_dirty(&t));
 CHECK(gpu[3][0][bytes*((1/bh)*(8/bw)+1/bw)]==0x88);CHECK(gpu[3][0][levels[0].byte_count-1]==0x11);CHECK(gpu[3][2][0]==0x88);for(UINT i=0;i<3;i++)texture_dirty_clear(levels+i);CHECK(allocations==frees);
}
/* Independent byte oracles for every color pair, all six faces and mips. */
for(UINT api=8;api<=9;api++)for(UINT cube=0;cube<2;cube++)for(UINT sf=21;sf<=23;sf++)for(UINT df=21;df<=23;df++){
 D9Device dev={.api=api};D9Texture a,b;D9TextureLevel al[18],bl[18];BYTE as[2016],bs[2016];
 init_color(&a,&dev,al,as,1,sf,cube);init_color(&b,&dev,bl,bs,2,df,cube);memset(bs,0xbd,sizeof(bs));memset(gpu[2],0xbd,sizeof(gpu[2]));uploads=fail_upload=0;
 const BYTE encoded[3][4]={{0x33,0x66,0x99,0x40},{0x33,0x66,0x99,3},{0x00,0xf8,0,0}};
 const BYTE green[3][4]={{0,255,0,64},{0,255,0,3},{0xe0,7,0,0}};
 for(UINT f=0;f<(cube?6u:1u);f++)for(UINT i=0;i<3;i++){D9TextureLevel*l=al+f*3+i;UINT n=sf==23?2:4;for(UINT y=0;y<l->height;y++)for(UINT x=0;x<l->width;x++)memcpy(l->shadow+y*l->row_pitch+x*n,f&1?green[sf-21]:encoded[sf-21],n);}
 HRESULT hr=texture_update_legacy_color(&a,&b,cube);
 if(api==9){CHECK(hr==-1&&!uploads&&bs[0]==0xbd&&a.dirty_pending);continue;}
 CHECK(hr==0&&uploads==(cube?18u:3u));
 for(UINT face=0;face<(cube?6u:1u);face++)for(UINT i=0;i<3;i++){
  BYTE want[4];UINT n=df==23?2:4;
  if(face&1){memcpy(want,green[df-21],4);if((df==21&&sf!=21)||(df==22&&sf!=22))want[3]=255;}
  else if(sf==23){const BYTE red[3][4]={{0,0,255,255},{0,0,255,255},{0,248,0,0}};memcpy(want,red[df-21],4);}
  else if(df==23){want[0]=0x26;want[1]=0x9b;}
  else{memcpy(want,encoded[df-21],4);if((df==21&&sf==22)||(df==22&&sf!=22))want[3]=255;}
  D9TextureLevel*l=bl+face*3+i;for(UINT y=0;y<l->height;y++)for(UINT x=0;x<l->width;x++){
   CHECK(!memcmp(l->shadow+y*l->row_pitch+x*n,want,n));CHECK(!memcmp(gpu[2][face*3+i]+y*l->row_pitch+x*n,want,n));
  }
 }
 if(!cube){CHECK(!a.dirty_pending);UINT prior=uploads;memset(as,0,2016);CHECK(!texture_update_legacy_color(&a,&b,FALSE)&&uploads==prior);
  RECT r={2,2,4,4};CHECK(!texture_add_dirty_rect(&a,&r));fail_upload=uploads+1;CHECK(texture_update_legacy_color(&a,&b,FALSE)==-2&&a.dirty_pending);fail_upload=0;
  CHECK(!texture_update_legacy_color(&a,&b,FALSE)&&!a.dirty_pending);CHECK(bs[0]==(df==23?(sf==23?0:0x26):(sf==23?0:0x33)));
  UINT n=df==23?2:4;CHECK(bs[2*bl[0].row_pitch+2*n]==0&&bs[2*bl[0].row_pitch+2*n+1]==0);
 }
 /* Validate the last mip/face before touching even the first destination. */
 UINT last=(cube?18:3)-1;bl[last].byte_count=1;uploads=0;memset(bs,0xbd,sizeof(bs));CHECK(texture_update_legacy_color(&a,&b,cube)==-1&&!uploads&&bs[0]==0xbd);
 for(UINT i=0;i<(cube?18u:3u);i++)texture_dirty_clear(al+i);
 CHECK(allocations==frees);
}
/* Different aspect ratios: only the overlapping texels of corresponding
 * largest-dimension mips are copied. Lower destination rows stay untouched. */
for(UINT api=8;api<=9;api++){
 D9Device dev={.api=api};D9Texture a,b;D9TextureLevel al[18],bl[18];BYTE as[2016],bs[2016];init_color(&a,&dev,al,as,1,21,FALSE);init_color(&b,&dev,bl,bs,2,21,FALSE);
 a.height=4;b.width=2;b.height=4;b.level_count=2;
 for(UINT i=0;i<3;i++){al[i].height=4>>i;al[i].row_count=al[i].height;al[i].byte_count=al[i].row_pitch*al[i].height;}
 for(UINT i=0;i<2;i++){bl[i].width=2>>i;bl[i].height=4>>i;bl[i].row_pitch=bl[i].width*4;bl[i].row_count=bl[i].height;bl[i].byte_count=bl[i].row_pitch*bl[i].height;}
 memset(as,0x77,sizeof(as));memset(bs,0xbd,sizeof(bs));uploads=0;HRESULT hr=texture_update_dirty(&a,&b);
 if(api==9)CHECK(hr==-1&&!uploads&&bs[0]==0xbd);
 else{CHECK(!hr&&uploads==2);for(UINT i=0;i<2;i++)for(UINT y=0;y<bl[i].height;y++)for(UINT x=0;x<bl[i].row_pitch;x++)CHECK(bl[i].shadow[y*bl[i].row_pitch+x]==(y<al[i+1].height?0x77:0xbd));}
}
/* OOM is atomic across the mip chain, while a retry reuses reserved storage. */
{
 D9Device dev={.api=8};D9Texture a,b;D9TextureLevel al[18],bl[18];BYTE as[2016],bs[2016];init_color(&a,&dev,al,as,1,21,FALSE);init_color(&b,&dev,bl,bs,2,23,FALSE);memset(as,0xff,sizeof(as));for(UINT i=0;i<3;i++)bl[i].shadow=NULL;
 uploads=0;fail_allocation=allocation_attempts+2;CHECK(texture_update_legacy_color(&a,&b,FALSE)==-3&&!uploads&&a.dirty_pending);CHECK(bl[0].shadow&&!bl[1].shadow&&!bl[2].shadow&&bl[0].shadow[0]==0);
 fail_allocation=0;CHECK(!texture_update_legacy_color(&a,&b,FALSE)&&uploads==3);for(UINT i=0;i<3;i++){CHECK(bl[i].shadow[0]==255&&bl[i].shadow[1]==255);HeapFree(NULL,0,bl[i].shadow);}CHECK(allocations==frees);
}
printf("PASS actual texture dirty C: %u checks; both APIs, exact regions/mips, READONLY/NO_DIRTY_UPDATE, preload/eviction, locked UpdateTexture, OOM atomicity/upload retry, legacy color conversion across all cube faces/mips, exact dirty regions and mismatched aspect bounds\\n",checks);return 0;}
`;
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-dirty-'));try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c',root+'/runtime/d9wg_runtime.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
