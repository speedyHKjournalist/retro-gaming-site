"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"rect8-core-")),source=fs.readFileSync(process.env.D8WG_RECT_CORE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
function desc(api,name){const sdk=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",['-E','-P','-x','c','-'],{input:'#include <d3d'+api+'.h>\n',encoding:'utf8',maxBuffer:16<<20});assert.equal(sdk.status,0,sdk.stderr);const m=/typedef struct _D3DSURFACE_DESC\s*\{[\s\S]*?\}\s*D3DSURFACE_DESC\s*;/.exec(sdk.stdout);assert(m);return m[0].replace('struct _D3DSURFACE_DESC','struct').replace(/\bD3DSURFACE_DESC\b/g,name);}
try{
const block=source.match(/#define D9WG_RECT_DEVICE D9Device[\s\S]*?#include "\.\.\/runtime\/d9wg_copy_rects_bridge8.h"/);assert(block);
let old=fs.readFileSync(root+'/tests/fixtures/d3d8_copy_rects_before_core.c','utf8');
const map={copy_surface_rect:'oracle_rect',device_copy_rects:'oracle_copy',D8Device:'D9Device',D8Surface:'D9Surface',D8TextureLevel:'D9TextureLevel',IDirect3DDevice8:'IDirect3DDevice9',IDirect3DSurface8:'IDirect3DSurface9',D3DSURFACE_DESC:'Desc8',D8WG_TRACE_ERROR:'TRACE_REFUSE',surface_device:'oracle_owner',surface_get_desc:'oracle_desc',texture_format_layout:'front_layout8',surface_gpu_location:'oracle_location',refresh_surface_pixels:'oracle_refresh',surface_pixels:'oracle_pixels',upload_surface_rect:'oracle_upload',surface_texture_level:'oracle_level'};
for(const[a,b]of Object.entries(map))old=old.replace(new RegExp('\\b'+a+'\\b','g'),b);
const c=`#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include "runtime/d9wg_surface_readback.h"
#include "runtime/d9wg_format_layouts.h"
#include "runtime/d9wg_runtime.h"
typedef int32_t HRESULT;typedef uint32_t D3DFORMAT,D3DRESOURCETYPE,D3DPOOL,D3DMULTISAMPLE_TYPE;typedef uint8_t BYTE;
${desc(8,'Desc8')}
${desc(9,'D3DSURFACE_DESC')}
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0u
#define D3DUSAGE_RENDERTARGET 1u
#define D3DUSAGE_DEPTHSTENCIL 2u
#define D3DRTYPE_SURFACE 1u
#define TRACE_REFUSE(v) (v)
#define FAILED(v) ((v)<0)
#define SUCCEEDED(v) ((v)>=0)
#define HEAP_ZERO_MEMORY 8u
#define GetProcessHeap() NULL
typedef struct{int32_t left,top,right,bottom;}RECT;typedef struct{int32_t x,y;}POINT;
static void SetRect(RECT*r,int l,int t,int rr,int b){*r=(RECT){l,t,rr,b};}
typedef struct{const void*lpVtbl;}Iface,IDirect3DSurface9,IDirect3DDevice9;
typedef struct D9Device{Iface iface;UINT api,handle;}D9Device;
typedef struct{BYTE*shadow;UINT row_pitch,row_count,byte_count,width,height;BOOL locked,shadow_valid,gpu_dirty;}D9TextureLevel;
typedef struct Texture{D9Device*device;UINT handle,level_count,usage,pool,format;D9TextureLevel*levels;}D9Texture,D9CubeTexture;
typedef struct D9Surface{Iface iface;D9Device*device;D9Texture*texture;D9CubeTexture*cube_texture;void*swap_chain;UINT cube_face,level,width,height,format,pool,multisample,multisample_quality;BYTE*shadow;UINT row_pitch,byte_count;BOOL locked,auto_depth_stencil,depth_stencil;}D9Surface;
static int g_surface_vtbl;static D9WGProcess state,*g_process=&state;static D9Device device,foreign;static UINT checks,cases,fail_alloc,read_error,fail_emit,emissions,reads,uploads,blits,allocations,frees;
static D9Surface *views[2];static BYTE cpus[2][256],gpus[2][256];
static D9Device*device_from_iface(Iface*i){return(D9Device*)i;}static D9Surface*surface_from_iface(Iface*i){return(D9Surface*)i;}static UINT device_compatibility_api(D9Device*d){return d->api;}
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;if(fail_alloc)return NULL;BYTE*p=malloc(n+32);assert(p);memset(p,0xa5,n+32);memset(p+16,flags&8?0:0xcd,n);allocations++;return p+16;}
static BOOL HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;free((BYTE*)p-16);frees++;return TRUE;}
static void checked_copy(void*dst,const void*src,size_t n){for(UINT i=0;i<2;i++){uintptr_t a=(uintptr_t)dst,b=(uintptr_t)cpus[i];if(a>=b&&a<b+sizeof(cpus[i])){assert(n<=sizeof(cpus[i])-(a-b));}}memcpy(dst,src,n);}
#define CopyMemory checked_copy
${['surface_texture_level','surface_get_desc','front_pixels8','front_layout8','copy_location8'].map(n=>extract(source,n)).join('\n')}
static UINT index_of(D9Surface*s){for(UINT i=0;i<2;i++)if(views[i]==s)return i;assert(0);return 0;}
static D9Surface*gpu_view(UINT handle,UINT level,UINT face){for(UINT i=0;i<2;i++){D9Surface*s=views[i];if(s->texture&&s->texture->handle==handle&&level==s->level&&!face)return s;if(s->cube_texture&&s->cube_texture->handle==handle&&s->cube_face==face&&s->level==level)return s;if(s->swap_chain&&!handle&&!level&&!face)return s;}assert(0);return NULL;}
static BOOL step(void){emissions++;return !fail_emit||emissions!=fail_emit;}
static BOOL emit_command(uint16_t op,const void*p,UINT bytes){assert(op==D9WG_OP_STRETCH_RECT&&bytes==sizeof(D9WGStretchRect));if(!step())return FALSE;const D9WGStretchRect*q=p;assert(q->device_handle==device.handle&&q->filter_point==1);D9Surface*s=gpu_view(q->source_texture_handle,q->source_level,q->source_face),*t=gpu_view(q->destination_texture_handle,q->destination_level,q->destination_face);UINT bw,bh,pb;assert(front_layout8(s->format,&bw,&bh,&pb));UINT w=q->source_right-q->source_left,h=q->source_bottom-q->source_top;assert(q->source_left>=0&&q->source_top>=0&&q->destination_left>=0&&q->destination_top>=0&&(UINT)q->source_right<=s->width&&(UINT)q->source_bottom<=s->height&&(UINT)q->destination_right<=t->width&&(UINT)q->destination_bottom<=t->height&&q->destination_right-q->destination_left==(int)w&&q->destination_bottom-q->destination_top==(int)h);BYTE snapshot[256];for(UINT y=0;y<h;y++)memcpy(snapshot+y*w*pb,gpus[index_of(s)]+((q->source_top+y)*s->width+q->source_left)*pb,w*pb);for(UINT y=0;y<h;y++)memcpy(gpus[index_of(t)]+((q->destination_top+y)*t->width+q->destination_left)*pb,snapshot+y*w*pb,w*pb);blits++;return TRUE;}
static BOOL submit_readback_batch_locked(BOOL p){assert(!p);return TRUE;}
int d9wg_surface_readback(D9WGProcess*p,const D9WGReadbackSurface*q,uint32_t row_bytes,uint32_t rows,uint8_t*out,uint32_t bytes,BOOL(*flush)(BOOL),D9WGReadbackDiagnostic*detail){assert(p==g_process&&!q->first_row&&!q->row_count&&!q->request_id&&!q->response_offset&&flush==submit_readback_batch_locked);D9Surface*s=gpu_view(q->texture_handle,q->level,q->face);UINT bw,bh,pb;assert(front_layout8(s->format,&bw,&bh,&pb));assert(q->device_handle==device.handle&&q->format==s->format&&q->width==s->width&&q->height==s->height&&rows==s->height&&row_bytes==s->width*pb&&q->destination_pitch>=row_bytes&&bytes>=q->destination_pitch*rows);reads++;memset(detail,0,sizeof(*detail));if(read_error)return read_error;for(UINT y=0;y<rows;y++)memcpy(out+y*q->destination_pitch,gpus[index_of(s)]+y*row_bytes,row_bytes);return 0;}
static BOOL observe_upload(D9Surface*s,const RECT*r){if(!s->texture&&!s->cube_texture&&!s->swap_chain)return TRUE;if(!step())return FALSE;UINT bw,bh,pb;assert(front_layout8(s->format,&bw,&bh,&pb));D9TextureLevel*l=surface_texture_level(s);BYTE*input=l?l->shadow:s->shadow;UINT pitch=l?l->row_pitch:s->row_pitch;for(int y=r->top;y<r->bottom;y++)memcpy(gpus[index_of(s)]+(y*s->width+r->left)*pb,input+y*pitch+r->left*pb,(r->right-r->left)*pb);uploads++;return TRUE;}
static BOOL emit_texture_update(D9Texture*t,UINT level,const RECT*r){D9Surface*s=gpu_view(t->handle,level,0);return observe_upload(s,r);}
static BOOL emit_cube_texture_update(D9CubeTexture*t,UINT face,UINT level,const RECT*r){D9Surface*s=gpu_view(t->handle,level,face);return observe_upload(s,r);}
typedef struct{int32_t left,top,right,bottom;}D9WGSurfaceLockRect;
static UINT allocate_handle(void){return 101;}
static BOOL emit_texture_rows(const D9WGTextureRows*r){(void)r;return TRUE;}
static BOOL observed_renderable(UINT dev,UINT handle,UINT fmt,UINT target,UINT level,UINT face,const D9WGSurfaceLockRect*r,const BYTE*p,UINT pitch,UINT pb,int(*emit)(uint16_t,const void*,UINT),int(*rows)(const D9WGTextureRows*)){D9Surface*s=gpu_view(0,0,0);assert(dev==device.handle&&handle==101&&fmt==s->format&&!target&&!level&&!face&&p==s->shadow+r->top*pitch+r->left*pb&&pitch==s->row_pitch&&emit==emit_command&&rows==emit_texture_rows);RECT a={r->left,r->top,r->right,r->bottom};return observe_upload(s,&a);}
#define d9wg_renderable_upload observed_renderable
${['upload_front8','refresh_copy8'].map(n=>extract(source,n)).join('\n')}
${block[0].replace('../runtime/d9wg_copy_rects_bridge8.h',process.env.D8WG_RECT_BRIDGE_HEADER||root+'/runtime/d9wg_copy_rects_bridge8.h')}
${extract(source,'d9wg_d3d9_core_copy_rects8')}
/* Old8 bodies use independent metadata and source-authority adapters. */
static D9Device*oracle_owner(D9Surface*s){return s->device;}
static D9TextureLevel*oracle_level(D9Surface*s){if(s->texture)return s->texture->levels+s->level;if(s->cube_texture)return s->cube_texture->levels+s->cube_face*s->cube_texture->level_count+s->level;return NULL;}
static HRESULT oracle_desc(Iface*i,Desc8*d){D9Surface*s=(D9Surface*)i;memset(d,0,sizeof(*d));d->Format=s->format;d->Width=s->width;d->Height=s->height;d->Pool=s->pool;d->Usage=s->texture?s->texture->usage:s->cube_texture?s->cube_texture->usage:s->swap_chain?1:s->auto_depth_stencil?2:0;return 0;}
static BOOL oracle_location(D9Surface*s,UINT*h,UINT*l,UINT*f){*h=*l=*f=0;if(s->texture){*h=s->texture->handle;*l=s->level;return TRUE;}if(s->cube_texture){*h=s->cube_texture->handle;*l=s->level;*f=s->cube_face;return TRUE;}return s->swap_chain!=NULL;}
static BYTE*oracle_pixels(D9Surface*s,UINT*pitch){D9TextureLevel*l=oracle_level(s);*pitch=l?l->row_pitch:s->row_pitch;return l?l->shadow:s->shadow;}
static HRESULT oracle_refresh(D9Surface*s){D9TextureLevel*l=oracle_level(s);Desc8 d;oracle_desc(&s->iface,&d);if(!s->swap_chain&&(d.Pool!=0||(!(d.Usage&1)&&!(l&&l->gpu_dirty))))return 0;reads++;if(read_error)return read_error==1?-1:-2;UINT bw,bh,pb;assert(front_layout8(d.Format,&bw,&bh,&pb));UINT pitch;BYTE*out=oracle_pixels(s,&pitch);for(UINT y=0;y<d.Height;y++)memcpy(out+y*pitch,gpus[index_of(s)]+y*d.Width*pb,d.Width*pb);if(l)l->gpu_dirty=FALSE;return 0;}
static BOOL oracle_upload(D9Surface*s,UINT fmt,const RECT*r,const BYTE*p,UINT pitch,UINT pb){(void)p;(void)pitch;(void)pb;assert(fmt==s->format);return observe_upload(s,r);}
${old}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"CopyRects8 core case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static void reset_data(void){for(UINT i=0;i<2;i++)for(UINT j=0;j<256;j++){cpus[i][j]=(BYTE)(i*43+j*7+11);gpus[i][j]=(BYTE)(i*29+j*3+97);}emissions=reads=uploads=blits=allocations=frees=0;}
int main(void){_Static_assert(sizeof(Desc8)==32&&sizeof(D3DSURFACE_DESC)==32,"SDK descriptor sizes");device=(D9Device){.handle=91,.api=8};foreign=(D9Device){.handle=92,.api=8};const UINT formats[]={21,23,65};
for(UINT fmt=0;fmt<3;fmt++)for(UINT pool_s=0;pool_s<4;pool_s++)for(UINT pool_t=0;pool_t<4;pool_t++)for(UINT kind_s=0;kind_s<4;kind_s++)for(UINT kind_t=0;kind_t<4;kind_t++)for(UINT shape=0;shape<9;shape++)for(UINT fault=0;fault<12;fault++){
 /* Two implicit views represent the same GPU identity and use the overlap path. */
 if(kind_s==3&&kind_t==3)continue;
 UINT pb=fmt==1?2:4,w=4,h=3,pitch=w*pb+4;D9TextureLevel ls[12]={0},lt[12]={0};UINT si=kind_s==2?9:0,ti=kind_t==2?7:0;
 ls[si]=(D9TextureLevel){.shadow=cpus[0],.width=w,.height=h,.row_pitch=pitch,.byte_count=pitch*h,.row_count=h};lt[ti]=ls[si];lt[ti].shadow=cpus[1];
 D9Texture ts={.device=&device,.handle=11,.level_count=2,.format=formats[fmt],.pool=pool_s,.levels=ls},tt=ts;tt.handle=12;tt.pool=pool_t;tt.levels=lt;
 D9Surface s={.iface={&g_surface_vtbl},.device=&device,.width=w,.height=h,.format=formats[fmt],.pool=pool_s,.shadow=cpus[0],.row_pitch=pitch,.byte_count=pitch*h,.texture=kind_s==1?&ts:NULL,.cube_texture=kind_s==2?&ts:NULL,.cube_face=4,.level=kind_s==2?1:0,.swap_chain=kind_s==3?&state:NULL},t=s;t.pool=pool_t;t.shadow=cpus[1];t.texture=kind_t==1?&tt:NULL;t.cube_texture=kind_t==2?&tt:NULL;t.cube_face=3;t.level=kind_t==2?1:0;t.swap_chain=kind_t==3?&state:NULL;
 views[0]=&s;views[1]=&t;RECT rects[3]={{1,0,3,2},{0,1,2,3},{-1,0,1,1}};POINT points[3]={{0,1},{2,0},{0,0}};UINT count=shape==0?0:shape==3?3:shape==4?2:1;const RECT*rp=shape==0?NULL:rects;const POINT*pp=shape==0||shape==2?NULL:points;
 if(shape==5)rects[0].right=5;if(shape==6)points[0].x=INT32_MAX;if(shape==7){count=0;}if(shape==8){count=1;rp=NULL;}
 read_error=fault==1?1:fault==2?3:0;fail_emit=fault==3?1:0;fail_alloc=fault==4?1:0;ts.usage=fault==5?1:0;tt.usage=fault==6?2:0;s.locked=fault==7;lt[ti].locked=fault==8;t.device=fault==9?&foreign:&device;if(fault==10)t.format=22;if(fault==11)t.iface.lpVtbl=NULL;
 BOOL dirty=(shape&1)!=0;ls[si].gpu_dirty=dirty;lt[ti].gpu_dirty=dirty;reset_data();HRESULT expected=oracle_copy(&device.iface,&s.iface,rp,count,&t.iface,pp);BYTE expected_cpu[2][256],expected_gpu[2][256];memcpy(expected_cpu,cpus,sizeof(cpus));memcpy(expected_gpu,gpus,sizeof(gpus));UINT er=reads,eu=uploads,eb=blits,ee=emissions;BOOL ds=ls[si].gpu_dirty,dt=lt[ti].gpu_dirty;CHECK(allocations==frees);
 reset_data();ls[si].shadow_valid=!dirty;lt[ti].shadow_valid=!dirty;HRESULT actual=d9wg_d3d9_core_copy_rects8(&device.iface,&s.iface,rp,count,&t.iface,pp);CHECK(actual==expected&&reads==er&&uploads==eu&&blits==eb&&emissions==ee&&allocations==frees);CHECK(!memcmp(cpus,expected_cpu,sizeof(cpus))&&!memcmp(gpus,expected_gpu,sizeof(gpus)));if(kind_s==1||kind_s==2)CHECK(ls[si].shadow_valid==!ds);if(kind_t==1||kind_t==2)CHECK(lt[ti].shadow_valid==!dt);cases++;
}
/* Aliased CPU images must snapshot each rectangle before ordered writes. */
for(UINT fault=0;fault<2;fault++)for(UINT axis=0;axis<2;axis++){
 D9Surface s={.iface={&g_surface_vtbl},.device=&device,.width=4,.height=3,.format=21,.pool=2,.shadow=cpus[0],.row_pitch=20,.byte_count=60};views[0]=views[1]=&s;RECT r={0,0,3,2};POINT p={axis?1:0,axis?0:1};reset_data();BYTE before[256],expected[256];memcpy(before,cpus[0],256);memcpy(expected,before,256);for(UINT y=0;y<2;y++)memcpy(expected+(y+p.y)*20+p.x*4,before+y*20,12);fail_alloc=fault;read_error=fail_emit=0;HRESULT hr=d9wg_d3d9_core_copy_rects8(&device.iface,&s.iface,&r,1,&s.iface,&p);CHECK(hr==(fault?-3:0)&&!memcmp(cpus[0],fault?before:expected,256)&&allocations==frees);cases++;
}
CHECK(d9wg_d3d9_core_copy_rects8(NULL,NULL,NULL,0,NULL,NULL)==-1);device.api=9;CHECK(d9wg_d3d9_core_copy_rects8(&device.iface,NULL,NULL,0,NULL,NULL)==-1);cases+=2;
printf("PASS CopyRects8 actual core: %u checks / %u cases; frozen8 body, SDK8/9 metadata, four pools, texture/cube/implicit/CPU, ordered partial failure, overlap/OOM, padding, bounds, read/upload faults and profile isolation\\n",checks,cases);return 0;}
`;
fs.writeFileSync(dir+'/rect.c',c);
const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root+'/tests/native','-I',root,dir+'/rect.c','-o',dir+'/rect'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);
const run=spawnSync(dir+'/rect',[],{encoding:'utf8',timeout:60000});assert(!run.error,String(run.error));assert.equal(run.status,0,run.stdout+run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
