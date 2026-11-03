"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-surfaces-")),source=fs.readFileSync(process.env.D8WG_SURFACE_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8");
function extract(name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{fs.writeFileSync(dir+"/surface.c",`#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "runtime/d9wg_surface_readback.h"
#include "runtime/d9wg_runtime.h"
typedef int HRESULT,D3DFORMAT,D3DPOOL;typedef uint8_t BYTE;typedef int32_t INT;typedef UINT D3DCUBEMAP_FACES;
typedef struct{int32_t left,top,right,bottom;}RECT;typedef struct{int32_t x,y;}POINT;
typedef struct{INT Pitch;void*pBits;}D3DLOCKED_RECT;
typedef struct{UINT Format,Type,Usage,Pool,Size,MultiSampleType,Width,Height;}D3DSURFACE_DESC;
typedef struct{const void*lpVtbl;}Iface;typedef Iface IDirect3DSurface8;
typedef struct D8Device{UINT handle;BYTE*front_shadow;UINT front_shadow_pitch;D3DSURFACE_DESC display_mode;UINT present;}D8Device,IDirect3DDevice8;
typedef struct{BYTE*shadow;UINT width,height,depth,row_pitch,row_count,slice_pitch,byte_count;RECT lock_rect;UINT lock_z,lock_depth;DWORD lock_flags;BOOL locked,gpu_dirty;}D8TextureLevel;
typedef struct{D8Device*device;UINT handle,level_count,format,pool,usage,multisample_type;D8TextureLevel*levels;BOOL lockable_render_target;}D8Texture,D8CubeTexture;
typedef struct{Iface iface;D8Texture*texture;D8CubeTexture*cube;UINT face;D8Device*device;UINT level;D3DSURFACE_DESC desc;BYTE*shadow;UINT row_pitch;RECT lock_rect;DWORD lock_flags;BOOL locked,backbuffer,depth_stencil,reset_blocker;}D8Surface;
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_MANAGED 1
#define D3DPOOL_SYSTEMMEM 2
#define D3DPOOL_SCRATCH 3
#define D3DMULTISAMPLE_NONE 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DLOCK_READONLY 16
#define D8WG_TRACE_ERROR(x) (x)
#define D8WG_TRACE(...) ((void)0)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define CopyMemory memcpy
#define GetProcessHeap() NULL
static unsigned g_surface_vtbl,emissions,updates,blits,reads,creates,destroys,allocations,frees,fail_alloc,fail_emit,read_error;
static D9WGProcess process,*g_process=&process;static D8Device*active;
static unsigned next_handle=1000;
static BOOL SetRect(RECT*r,int l,int t,int rr,int b){*r=(RECT){l,t,rr,b};return TRUE;}
static BOOL multiply_u32(UINT a,UINT b,UINT*out){if(a&&b>UINT32_MAX/a)return FALSE;*out=a*b;return TRUE;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){if(f!=21&&f!=22)return FALSE;*w=*h=1;*b=4;return TRUE;}
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;if(fail_alloc){fail_alloc=0;return NULL;}void*p=flags?calloc(1,n):malloc(n);if(p)allocations++;return p;}
static void HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;frees++;free(p);}
static UINT allocate_handle(void){return next_handle++;}
static D8Device*device_from_iface(D8Device*d){return d;}
static D8Surface*surface_from_iface(Iface*i){return (D8Surface*)i;}
static HRESULT surface_get_desc(Iface*i,D3DSURFACE_DESC*d){D8Surface*s=(D8Surface*)i;if(s->texture||s->cube){D8Texture*t=s->texture?s->texture:s->cube;D8TextureLevel*l=t->levels+(s->cube?s->face*t->level_count:0)+s->level;*d=(D3DSURFACE_DESC){.Format=t->format,.Usage=t->usage,.Pool=t->pool,.Size=l->byte_count,.Width=l->width,.Height=l->height};}else*d=s->desc;return 0;}
static HRESULT refresh_surface_pixels(D8Surface*surface);
static BOOL emit_level_update(uint32_t,D3DFORMAT,const D8TextureLevel*,UINT,UINT,const RECT*);
static HRESULT create_backbuffer_surface(D8Device*d,const UINT*present,BOOL blocker,Iface**out){assert(d==active&&present==&d->present&&!blocker);D8Surface*s=HeapAlloc(NULL,8,sizeof(*s));if(!s)return -3;s->iface.lpVtbl=&g_surface_vtbl;s->device=d;s->backbuffer=TRUE;s->desc=d->display_mode;*out=&s->iface;return 0;}
static unsigned IDirect3DSurface8_Release(Iface*i){HeapFree(NULL,0,i);return 0;}
#define MAX_RES 32
static struct{UINT handle,levels,faces,width,height;BYTE*data;}gpu[MAX_RES];
static unsigned gpu_count;
static void gpu_add(UINT handle,UINT levels,UINT faces,UINT w,UINT h){assert(gpu_count<MAX_RES);gpu[gpu_count].handle=handle;gpu[gpu_count].levels=levels;gpu[gpu_count].faces=faces;gpu[gpu_count].width=w;gpu[gpu_count].height=h;gpu[gpu_count].data=malloc(w*h*4*levels*faces);assert(gpu[gpu_count].data);memset(gpu[gpu_count].data,0x3c,w*h*4*levels*faces);gpu_count++;}
static BYTE*gpu_pixels(UINT handle,UINT level,UINT face,UINT*w,UINT*h){for(unsigned i=0;i<gpu_count;i++)if(gpu[i].handle==handle){assert(level<gpu[i].levels&&face<gpu[i].faces);*w=gpu[i].width;*h=gpu[i].height;return gpu[i].data+(face*gpu[i].levels+level)*(*w)*(*h)*4;}assert(0);return NULL;}
static BOOL step(void){emissions++;return !fail_emit||emissions!=fail_emit;}
static BOOL emit_command(uint16_t op,const void*bytes,UINT size){if(!step())return FALSE;
 if(op==D9WG_OP_CREATE_TEXTURE_2D){const D9WGCreateTexture2D*c=bytes;assert(size==sizeof(*c)&&c->device_handle==active->handle&&c->level_count==1&&c->pool==0&&c->width&&c->height);gpu_add(c->resource_handle,1,1,c->width,c->height);creates++;return TRUE;}
 if(op==D9WG_OP_DESTROY_RESOURCE){const D9WGDestroyResource*c=bytes;assert(size==sizeof(*c)&&c->resource_kind==D9WG_RESOURCE_TEXTURE_2D);for(unsigned i=0;i<gpu_count;i++)if(gpu[i].handle==c->resource_handle){free(gpu[i].data);gpu[i]=gpu[--gpu_count];destroys++;return TRUE;}assert(0);}
 assert(op==D9WG_OP_STRETCH_RECT);const D9WGStretchRect*c=bytes;assert(size==sizeof(*c)&&c->device_handle==active->handle&&c->filter_point==1);UINT sw,sh,dw,dh;BYTE*src=gpu_pixels(c->source_texture_handle,c->source_level,c->source_face,&sw,&sh),*dst=gpu_pixels(c->destination_texture_handle,c->destination_level,c->destination_face,&dw,&dh);UINT w=c->source_right-c->source_left,h=c->source_bottom-c->source_top;assert(c->destination_right-c->destination_left==(int)w&&c->destination_bottom-c->destination_top==(int)h);assert(c->source_left>=0&&c->source_top>=0&&c->destination_left>=0&&c->destination_top>=0&&(UINT)c->source_right<=sw&&(UINT)c->source_bottom<=sh&&(UINT)c->destination_right<=dw&&(UINT)c->destination_bottom<=dh);BYTE*snapshot=malloc(w*h*4);assert(snapshot);for(UINT y=0;y<h;y++)memcpy(snapshot+y*w*4,src+((c->source_top+y)*sw+c->source_left)*4,w*4);for(UINT y=0;y<h;y++)memcpy(dst+((c->destination_top+y)*dw+c->destination_left)*4,snapshot+y*w*4,w*4);free(snapshot);blits++;return TRUE;
}
static BOOL emit_texture_rows(const D9WGTextureRows*r){if(!step())return FALSE;UINT w,h;BYTE*dst=gpu_pixels(r->resource_handle,r->level,r->z,&w,&h);assert(r->x+r->width<=w&&r->y+r->height<=h&&r->block_height==1&&r->row_count==r->height&&r->row_bytes==r->width*4);for(UINT y=0;y<r->height;y++)memcpy(dst+((r->y+y)*w+r->x)*4,r->source+y*r->source_pitch,r->row_bytes);updates++;return TRUE;}
BOOL d9wg_transport_submit(D9WGProcess*p,BOOL present){assert(p==g_process&&!present);return !read_error;}
int d9wg_surface_readback(D9WGProcess*p,const D9WGReadbackSurface*q,uint32_t row_bytes,uint32_t rows,uint8_t*destination,uint32_t bytes,BOOL(*flush)(BOOL),D9WGReadbackDiagnostic*detail){assert(p==g_process&&q->device_handle==active->handle&&!q->first_row&&!q->row_count&&!q->request_id&&!q->response_offset);assert(q->format==21||q->format==22);UINT w,h;BYTE*src=gpu_pixels(q->texture_handle,q->level,q->face,&w,&h);assert(q->width==w&&q->height==h&&row_bytes==w*4&&rows==h&&q->destination_pitch>=row_bytes&&bytes>=rows*q->destination_pitch);reads++;memset(detail,0,sizeof(*detail));if(read_error)return read_error;assert(flush(FALSE));for(UINT y=0;y<rows;y++)memcpy(destination+y*q->destination_pitch,src+y*w*4,row_bytes);return 0;}
${["cube_level","emit_level_update","surface_texture_level","surface_device","surface_gpu_location","surface_pixels","submit_readback_batch_locked","readback_surface_pixels","refresh_surface_pixels","lock_texture_level","unlock_texture_level","lock_surface_texture_level","texture_lock_level","texture_unlock_level","cube_lock_face","cube_unlock_face","upload_backbuffer_rect","upload_surface_rect","copy_surface_rect","device_copy_rects","device_get_front_buffer"].map(extract).join("\n")}
static HRESULT call_copy(D8Device*d,D8Surface*s,const RECT*r,UINT n,D8Surface*t,const POINT*p){return device_copy_rects(d,s?&s->iface:NULL,r,n,t?&t->iface:NULL,p);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 surface line %u: %s reads=%u emits=%u\\n",__LINE__,#v,reads,emissions);return 1;}}while(0)
static void reset_events(void){emissions=updates=blits=reads=creates=destroys=0;read_error=fail_emit=fail_alloc=0;}
static void pattern(BYTE*p,unsigned n,unsigned seed){for(unsigned i=0;i<n;i++)p[i]=(BYTE)(seed+i*7);}
static void expected_copy(BYTE*out,const BYTE*before,UINT dw,const BYTE*input,UINT sw,const RECT*r,const POINT*p){memcpy(out,before,dw*4*4);for(int y=0;y<r->bottom-r->top;y++)for(int x=0;x<r->right-r->left;x++)for(int b=0;b<4;b++)out[((p->y+y)*dw+p->x+x)*4+b]=input[((r->top+y)*sw+r->left+x)*4+b];}
int main(void){D8Device device={.handle=7,.front_shadow_pitch=16,.display_mode={.Format=21,.Usage=1,.Pool=0,.Size=64,.Width=4,.Height=4}},foreign={.handle=8};BYTE front[64],cpu_a[64],cpu_b[64];device.front_shadow=front;active=&device;D8TextureLevel la[2]={0},lb[2]={0},lc[12]={0};for(unsigned i=0;i<2;i++){la[i]=(D8TextureLevel){.shadow=cpu_a,.width=4,.height=4,.depth=1,.row_pitch=16,.row_count=4,.byte_count=64};lb[i]=(D8TextureLevel){.shadow=cpu_b,.width=4,.height=4,.depth=1,.row_pitch=16,.row_count=4,.byte_count=64};}for(unsigned i=0;i<12;i++){lc[i]=la[0];lc[i].shadow=calloc(1,64);CHECK(lc[i].shadow);}D8Texture ta={.device=&device,.handle=11,.level_count=2,.format=21,.pool=0,.levels=la,.lockable_render_target=TRUE},tb={.device=&device,.handle=12,.level_count=2,.format=21,.pool=0,.levels=lb},cube={.device=&device,.handle=13,.level_count=2,.format=21,.pool=0,.levels=lc};D8Surface a={.iface={&g_surface_vtbl},.texture=&ta,.device=&device},b={.iface={&g_surface_vtbl},.texture=&tb,.device=&device},c={.iface={&g_surface_vtbl},.cube=&cube,.face=4,.level=1,.device=&device},back={.iface={&g_surface_vtbl},.device=&device,.backbuffer=TRUE,.desc=device.display_mode},image={.iface={&g_surface_vtbl},.device=&device,.desc={.Format=21,.Pool=2,.Size=64,.Width=4,.Height=4},.shadow=cpu_a,.row_pitch=16};gpu_add(0,1,1,4,4);gpu_add(11,2,1,4,4);gpu_add(12,2,1,4,4);gpu_add(13,2,6,4,4);RECT r={1,0,3,3};POINT pt={1,1};BYTE expected[64],before[64],source_before[64];UINT w,h;unsigned cases=0;
 for(unsigned from=0;from<4;from++)for(unsigned to=0;to<4;to++)if(from!=to){D8Surface*views[]={&a,&b,&c,&back};D8Surface*s=views[from],*t=views[to];uint32_t hs,ls,fs,ht,lt,ft;surface_gpu_location(s,&hs,&ls,&fs);surface_gpu_location(t,&ht,&lt,&ft);BYTE*sg=gpu_pixels(hs,ls,fs,&w,&h),*tg=gpu_pixels(ht,lt,ft,&w,&h);pattern(sg,64,31+from);pattern(tg,64,90+to);memcpy(source_before,sg,64);memcpy(before,tg,64);expected_copy(expected,before,4,source_before,4,&r,&pt);reset_events();CHECK(!call_copy(&device,s,&r,1,t,&pt)&&reads==0&&blits==1&&!updates);CHECK(!memcmp(tg,expected,64));D8TextureLevel*l=surface_texture_level(t);if(l)CHECK(l->gpu_dirty);cases++;}
 /* GPU-written ordinary textures refresh once; RT locks always refresh. */
 reset_events();lb[0].gpu_dirty=TRUE;BYTE*bg=gpu_pixels(12,0,0,&w,&h);pattern(bg,64,70);memset(cpu_b,0xcc,64);D3DLOCKED_RECT lock={123,(void*)99};CHECK(!texture_lock_level(&tb,0,&lock,NULL,16)&&reads==1&&lock.pBits==cpu_b&&!memcmp(cpu_b,bg,64)&&!lb[0].gpu_dirty);CHECK(!texture_unlock_level(&tb,0)&&!updates);CHECK(!texture_lock_level(&tb,0,&lock,NULL,16)&&reads==1);CHECK(!texture_unlock_level(&tb,0));cases++;
 ta.usage=1;for(unsigned pass=0;pass<2;pass++){reset_events();BYTE*ag=gpu_pixels(11,0,0,&w,&h);pattern(ag,64,100+pass);memcpy(before,ag,64);CHECK(!texture_lock_level(&ta,0,&lock,&r,pass?0:16)&&reads==1);CHECK(lock.pBits==cpu_a+4);if(pass)memset(lock.pBits,0xa6,4);CHECK(!texture_unlock_level(&ta,0)&&updates==pass);if(pass)memset(before+4,0xa6,4);CHECK(!memcmp(ag,before,64));cases++;}
 reset_events();lc[9].gpu_dirty=TRUE;BYTE*cg=gpu_pixels(13,1,4,&w,&h);pattern(cg,64,211);CHECK(!cube_lock_face(&cube,4,1,&lock,NULL,16)&&reads==1&&lock.pBits==lc[9].shadow&&!memcmp(lock.pBits,cg,64));CHECK(!cube_unlock_face(&cube,4,1)&&!updates);cube.usage=1;pattern(cg,64,213);CHECK(!cube_lock_face(&cube,4,1,&lock,NULL,16)&&reads==2&&!memcmp(lock.pBits,cg,64));CHECK(!cube_unlock_face(&cube,4,1)&&!updates);cube.usage=0;cases++;
 /* Readback failures roll locks back and preserve output fields. */
 const UINT samples[]={1,2,4,16,0xffffffffu};for(unsigned i=0;i<5;i++)for(unsigned readonly=0;readonly<2;readonly++){reset_events();ta.multisample_type=samples[i];lock=(D3DLOCKED_RECT){123,(void*)99};D8TextureLevel saved=la[0];memcpy(before,cpu_a,64);CHECK(texture_lock_level(&ta,0,&lock,NULL,readonly?16:0)==-1&&lock.Pitch==123&&lock.pBits==(void*)99&&!memcmp(&la[0],&saved,sizeof(saved))&&!memcmp(cpu_a,before,64)&&!reads&&!updates&&!emissions);cases++;}ta.multisample_type=0;
 for(unsigned error=1;error<=3;error++){reset_events();read_error=error;lock=(D3DLOCKED_RECT){123,(void*)99};CHECK(texture_lock_level(&ta,0,&lock,NULL,16)==(error==1?-1:-2)&&!la[0].locked&&lock.Pitch==123&&lock.pBits==(void*)99&&!updates);cases++;}ta.usage=0;
 /* GPU to CPU and CPU to GPU, including cube face stride and back-buffer staging. */
 for(unsigned kind=0;kind<3;kind++){D8Surface*view=kind==0?&a:kind==1?&c:&back;D8TextureLevel*l=surface_texture_level(view);if(l)l->gpu_dirty=TRUE;uint32_t handle,level,face;surface_gpu_location(view,&handle,&level,&face);BYTE*g=gpu_pixels(handle,level,face,&w,&h);pattern(g,64,50+kind);memcpy(source_before,g,64);memset(cpu_a,0x3c,64);/* image and a share cpu_a: use independent destination. */image.shadow=cpu_b;memset(cpu_b,0x3c,64);memcpy(before,cpu_b,64);expected_copy(expected,before,4,source_before,4,&r,&pt);reset_events();CHECK(!call_copy(&device,view,&r,1,&image,&pt)&&reads==1&&!blits&&!updates);CHECK(!memcmp(cpu_b,expected,64));cases++;
 image.shadow=cpu_a;pattern(cpu_a,64,130+kind);memcpy(source_before,cpu_a,64);pattern(g,64,80+kind);memcpy(before,g,64);expected_copy(expected,before,4,source_before,4,&r,&pt);reset_events();CHECK(!call_copy(&device,&image,&r,1,view,&pt));CHECK(!memcmp(g,expected,64));if(kind==2)CHECK(creates==1&&destroys==1&&blits==1&&updates==1);else CHECK(updates==1&&!blits);cases++;}
 /* Overlap snapshots must preserve the original source, including vertical moves. */
 image.shadow=cpu_a;pattern(cpu_a,64,29);memcpy(source_before,cpu_a,64);memcpy(before,cpu_a,64);expected_copy(expected,before,4,source_before,4,&r,&pt);reset_events();CHECK(!call_copy(&device,&image,&r,1,&image,&pt)&&!memcmp(cpu_a,expected,64));cases++;pattern(cpu_a,64,29);memcpy(before,cpu_a,64);reset_events();fail_alloc=1;CHECK(call_copy(&device,&image,&r,1,&image,&pt)==-3&&!memcmp(cpu_a,before,64)&&!emissions);cases++;
 /* An omitted point means origin; zero count with unused pointers is a no-op. */
 reset_events();POINT zero={0,0};memcpy(before,cpu_b,64);expected_copy(expected,before,4,cpu_a,4,&r,&zero);D8Surface image2=image;image2.shadow=cpu_b;CHECK(!call_copy(&device,&image,&r,1,&image2,NULL)&&!memcmp(cpu_b,expected,64));CHECK(!call_copy(&device,&image,&r,0,&image2,&pt)&&!emissions);cases++;
 for(unsigned bad=0;bad<12;bad++){reset_events();RECT q=r;POINT p=pt;UINT format=image2.desc.Format,usage=image2.desc.Usage;D8Device*owner=image2.device;if(bad==0)q.left=-1;if(bad==1)q.right=5;if(bad==2)q.bottom=q.top;if(bad==3)p.x=-1;if(bad==4)p.y=INT32_MAX;if(bad==5)p.x=INT32_MAX;if(bad==6)image2.desc.Format=22;if(bad==7)image2.desc.Usage=2;if(bad==8)image.locked=TRUE;if(bad==9)image2.locked=TRUE;if(bad==10)image2.device=&foreign;if(bad==11)image2.iface.lpVtbl=NULL;CHECK(call_copy(&device,&image,&q,1,&image2,&p)==-1&&!reads&&!emissions);image2.desc.Format=format;image2.desc.Usage=usage;image2.device=owner;image.locked=image2.locked=FALSE;image2.iface.lpVtbl=&g_surface_vtbl;cases++;}
 /* CPU-accessible pools retain authoritative bytes after failed GPU uploads. */
 reset_events();ta.pool=2;la[0].gpu_dirty=TRUE;pattern(cpu_a,64,193);memcpy(before,cpu_a,64);CHECK(!texture_lock_level(&ta,0,&lock,NULL,16)&&!reads&&!memcmp(cpu_a,before,64));CHECK(!texture_unlock_level(&ta,0));ta.pool=0;cases++;
 /* GetFrontBuffer uses real bytes and forces opaque ARGB, never Clear's mirror. */
 reset_events();BYTE*fg=gpu_pixels(0,0,0,&w,&h);pattern(fg,64,17);memset(front,0x77,64);memset(cpu_b,0xcc,64);image2.shadow=cpu_b;CHECK(!device_get_front_buffer(&device,&image2.iface)&&reads==1);for(unsigned i=0;i<64;i++)CHECK(cpu_b[i]==(i%4==3?255:fg[i]));cases++;
 reset_events();read_error=3;memset(cpu_b,0xcc,64);memcpy(before,cpu_b,64);image2.shadow=cpu_b;CHECK(call_copy(&device,&back,&r,1,&image2,&pt)==-2&&reads==1&&!emissions&&!memcmp(cpu_b,before,64));cases++;
 reset_events();read_error=2;CHECK(!submit_readback_batch_locked(FALSE));read_error=0;CHECK(submit_readback_batch_locked(FALSE));cases++;
 reset_events();lb[0].gpu_dirty=FALSE;memcpy(before,bg,64);fail_emit=1;CHECK(call_copy(&device,&a,&r,1,&b,&pt)==-2&&!lb[0].gpu_dirty&&!memcmp(bg,before,64));cases++;
 reset_events();image.shadow=cpu_a;pattern(cpu_a,64,81);lb[0].gpu_dirty=FALSE;memcpy(before,bg,64);fail_emit=1;CHECK(call_copy(&device,&image,&r,1,&b,&pt)==-2&&lb[0].gpu_dirty&&!memcmp(bg,before,64));fail_emit=0;CHECK(!texture_lock_level(&tb,0,&lock,NULL,16)&&reads==1&&!memcmp(cpu_b,bg,64));CHECK(!texture_unlock_level(&tb,0));cases++;
 reset_events();lb[0].gpu_dirty=TRUE;memset(cpu_b,0xcc,64);pattern(bg,64,91);memcpy(before,bg,64);expected_copy(expected,before,4,cpu_a,4,&r,&pt);CHECK(!call_copy(&device,&image,&r,1,&b,&pt)&&lb[0].gpu_dirty&&!reads&&!memcmp(bg,expected,64));CHECK(!texture_lock_level(&tb,0,&lock,NULL,16)&&reads==1&&!memcmp(cpu_b,expected,64));CHECK(!texture_unlock_level(&tb,0));cases++;
 BYTE padded[144],saved_padded[144];D8Surface large=image2;large.shadow=padded;large.row_pitch=24;large.desc.Width=large.desc.Height=6;large.desc.Size=144;memset(padded,0xcc,sizeof(padded));reset_events();CHECK(!device_get_front_buffer(&device,&large.iface)&&reads==1);for(unsigned y=0;y<6;y++)for(unsigned x=0;x<24;x++)CHECK(padded[y*24+x]==(y<4&&x<16?(x%4==3?255:fg[y*16+x]):0xcc));cases++;
 memset(padded,0xcc,sizeof(padded));memcpy(saved_padded,padded,sizeof(padded));reset_events();read_error=3;CHECK(device_get_front_buffer(&device,&large.iface)==-2&&!memcmp(padded,saved_padded,sizeof(padded)));cases++;
 reset_events();image2.desc.Format=22;CHECK(call_copy(&device,&image,&r,0,&image2,&pt)==-1&&!reads&&!emissions);image2.desc.Format=21;cases++;
 reset_events();RECT outside={3,0,5,2};POINT origin={0,0};CHECK(call_copy(&device,&image,&outside,1,&large,&origin)==-1&&!reads&&!emissions);cases++;
 for(unsigned i=0;i<12;i++)free(lc[i].shadow);for(unsigned i=0;i<gpu_count;i++)free(gpu[i].data);CHECK(allocations==frees);printf("PASS actual D3D8 surface paths: %u GPU/cube/back-buffer copies, stale mirrors and RT locks, exact rows/preservation, CPU staging lifecycle, overlap/OOM, bounds, locks, CPU pools and opaque front readback\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root+"/tests/native","-I",root,dir+"/surface.c","-o",dir+"/surface"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/surface",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
