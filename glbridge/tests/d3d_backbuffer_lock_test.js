"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d3d-bblock-"));
const eight=readComparisonSource(process.env.D8WG_BACKBUFFER_LOCK_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=readComparisonSource(process.env.D9WG_BACKBUFFER_LOCK_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_surface_readback.h"
#include "${root}/runtime/d9wg_cpu_texture_layout.h"
#include "${process.env.D9WG_SURFACE_LOCK_HEADER||root+'/runtime/d9wg_surface_lock.h'}"
#include "${process.env.D9WG_BACKBUFFER_UPLOAD_HEADER||root+'/runtime/d9wg_backbuffer_upload.h'}"
typedef int HRESULT,D3DFORMAT;typedef uint8_t BYTE;typedef int32_t LONG,INT;typedef struct {LONG left,top,right,bottom;}RECT;
typedef struct{INT Pitch;void*pBits;}D3DLOCKED_RECT;typedef struct{const void*lpVtbl;}Iface;typedef struct {BOOL locked,shadow_valid,dc_active;}D9TextureLevel;typedef struct{Iface iface;UINT handle,usage,level_count;D9TextureLevel*levels;}Texture,D9CubeTexture;
typedef struct Device{UINT handle;BYTE*front_shadow;UINT front_shadow_pitch;struct{DWORD Flags;}present;UINT compatibility_api;}Device,D8Device,D9Device;
typedef struct Surface{Iface iface;LONG refcount;Device*device;Texture*texture,*cube,*cube_texture;UINT face,cube_face,level;void*swap_chain;struct{UINT Format,Pool,Usage,Size,MultiSampleType,Width,Height;}desc;UINT width,height,format,multisample,row_pitch,byte_count,pool;BYTE*shadow;BOOL locked,backbuffer,depth_stencil,lockable_render_target,texture_child;RECT lock_rect;DWORD lock_flags;}Surface,D8Surface,D9Surface;
typedef struct Device Device;typedef struct Surface Surface;typedef Iface IDirect3DSurface8,IDirect3DSurface9;
#define WINAPI
#define D9FMT_NULL 0x4c4c554eu
#define D3DPOOL_DEFAULT 0
#define D3D_OK 0
#define D3DFMT_A8R8G8B8 21
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER 1
#define D3DUSAGE_RENDERTARGET 1
#define D3DLOCK_READONLY 16
#define D3DLOCK_DISCARD 8192
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE(...)
#define FAILED(v) ((v)<0)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() NULL
#define IDirect3DCubeTexture9_LockRect(...) (-12)
#define IDirect3DCubeTexture9_UnlockRect(...) (-22)
#define D3DCUBEMAP_FACES UINT
static UINT allocations,frees,fail_alloc,read_error,reads,emits,creates,uploads,blits,destroys,fail_emit,live,next_handle=1000;static BYTE gpu[4096],transient[4096];static UINT width,height,pixel_bytes,padding,tw,th,active_target;static Device*active;
static D9WGProcess process,*g_process=&process;
static BOOL SetRect(RECT*r,int l,int t,int rr,int b){*r=(RECT){l,t,rr,b};return TRUE;}
static Surface*surface_from_iface(Iface*p){return (Surface*)p;}
static UINT device_compatibility_api(Device*d){return d->compatibility_api?d->compatibility_api:9;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*bw,UINT*bh,UINT*b){if(f<21||f>26)return FALSE;*bw=*bh=1;*b=f<=22?4:2;return TRUE;}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*pitch,UINT*rows,UINT*bytes){UINT bw,bh,b;if(!texture_format_layout(f,&bw,&bh,&b))return FALSE;*pitch=w*b+padding;*rows=h;*bytes=*pitch*h;return TRUE;}
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;(void)flags;if(fail_alloc){fail_alloc=0;return NULL;}void*p=malloc(n);assert(p);memset(p,0xcd,n);allocations++;return p;}
static void HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;if(p){frees++;free(p);}}
static UINT allocate_handle(void){return ++next_handle;}
static BOOL submit_readback_batch_locked(BOOL p){assert(!p);return TRUE;}
static int read_pixels(BYTE*dst,UINT pitch,UINT bytes){assert(bytes>=pitch*height&&pitch>=width*pixel_bytes);reads++;if(read_error)return read_error;for(UINT y=0;y<height;y++)memcpy(dst+y*pitch,gpu+y*width*pixel_bytes,width*pixel_bytes);return 0;}
static HRESULT readback_surface_pixels(Surface*s,D3DFORMAT f,BYTE*dst,UINT pitch,UINT bytes){assert(s->device==active&&f==(D3DFORMAT)s->desc.Format&&dst==active->front_shadow&&pitch==active->front_shadow_pitch&&bytes==s->desc.Size);int r=read_pixels(dst,pitch,bytes);return !r?0:r==D9WG_READBACK_INVALID?-1:-2;}
int d9wg_surface_readback(D9WGProcess*p,const D9WGReadbackSurface*q,uint32_t row_bytes,uint32_t rows,uint8_t*dst,uint32_t bytes,BOOL(*flush)(BOOL),D9WGReadbackDiagnostic*detail){assert(p==g_process&&q->device_handle==active->handle&&q->texture_handle==active_target&&!q->level&&!q->face&&q->width==width&&q->height==height&&row_bytes==width*pixel_bytes&&rows==height);assert(flush(FALSE));memset(detail,0,sizeof(*detail));return read_pixels(dst,q->destination_pitch,bytes);}
static BOOL step(void){return ++emits!=fail_emit;}
static BOOL emit_command(uint16_t op,const void*p,uint32_t size){(void)size;if(!step())return FALSE;if(op==D9WG_OP_CREATE_TEXTURE_2D){const D9WGCreateTexture2D*q=p;assert(!live&&q->device_handle==active->handle&&q->resource_handle==next_handle&&q->level_count==1&&!q->pool&&!q->usage);live=1;tw=q->width;th=q->height;assert(tw*th*pixel_bytes<=sizeof(transient));memset(transient,0,tw*th*pixel_bytes);creates++;}else if(op==D9WG_OP_STRETCH_RECT){const D9WGStretchRect*q=p;assert(live&&q->device_handle==active->handle&&q->source_texture_handle==next_handle&&q->destination_texture_handle==active_target&&!q->source_left&&!q->source_top&&q->source_right==(INT)tw&&q->source_bottom==(INT)th&&q->filter_point==1&&q->destination_right-q->destination_left==(INT)tw&&q->destination_bottom-q->destination_top==(INT)th);for(UINT y=0;y<th;y++)memcpy(gpu+((y+q->destination_top)*width+q->destination_left)*pixel_bytes,transient+y*tw*pixel_bytes,tw*pixel_bytes);blits++;}else{const D9WGDestroyResource*q=p;assert(op==D9WG_OP_DESTROY_RESOURCE&&q->resource_kind==D9WG_RESOURCE_TEXTURE_2D&&q->resource_handle==next_handle&&live);live=0;destroys++;}return TRUE;}
static BOOL emit_texture_rows(const D9WGTextureRows*q){if(!step())return FALSE;assert(live&&q->resource_handle==next_handle&&!q->level&&!q->x&&!q->y&&!q->z&&q->width==tw&&q->height==th&&q->block_height==1&&q->row_count==th&&q->row_bytes==tw*pixel_bytes&&q->source_pitch>=q->row_bytes);for(UINT y=0;y<th;y++)memcpy(transient+y*tw*pixel_bytes,q->source+y*q->source_pitch,tw*pixel_bytes);uploads++;return TRUE;}
static BOOL upload_backbuffer_rect(Surface*s,D3DFORMAT f,const RECT*r,const BYTE*p,UINT pitch,UINT b){D9WGSurfaceLockRect area={r->left,r->top,r->right,r->bottom};return d9wg_backbuffer_upload(s->device->handle,allocate_handle(),f,&area,p,pitch,b,emit_command,emit_texture_rows);}
static HRESULT texture_lock_level(Texture*t,UINT l,D3DLOCKED_RECT*out,const RECT*r,DWORD f){(void)t;(void)l;(void)out;(void)r;(void)f;return -11;}
static HRESULT texture_unlock_level(Texture*t,UINT l){(void)t;(void)l;return -21;}
static HRESULT cube_lock_face(Texture*t,UINT f,UINT l,D3DLOCKED_RECT*out,const RECT*r,DWORD flags){(void)t;(void)f;(void)l;(void)out;(void)r;(void)flags;return -12;}
static HRESULT cube_unlock_face(Texture*t,UINT f,UINT l){(void)t;(void)f;(void)l;return -22;}
${extract(nine,"texture_levels_mapped")}
${["surface_lock_rect","surface_unlock_rect"].map(n=>extract(eight,n).replace(new RegExp('\\b'+n+'\\b','g'),n+'8')).join('\n')}
${["surface_lock_renderable","surface_unlock_renderable","surface_lock_rect","surface_unlock_rect"].map(n=>extract(nine,n).replace(/\bsurface_lock_rect\b/g,'surface_lock_rect9').replace(/\bsurface_unlock_rect\b/g,'surface_unlock_rect9')).join('\n')}
static UINT checks,cases,plans;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"backbuffer lock line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static HRESULT lock(UINT api,Surface*s,D3DLOCKED_RECT*out,const RECT*r,UINT f){return api==8?surface_lock_rect8(&s->iface,out,r,f):surface_lock_rect9(&s->iface,out,r,f);}
static HRESULT unlock(UINT api,Surface*s){return api==8?surface_unlock_rect8(&s->iface):surface_unlock_rect9(&s->iface);}
static void events(void){reads=emits=creates=uploads=blits=destroys=fail_emit=read_error=fail_alloc=0;assert(!live);}
int main(void){(void)&device_compatibility_api;(void)&surface_lock_renderable;(void)&surface_unlock_renderable;(void)&readback_surface_pixels;
 const UINT dims[]={1,2,7,257},pads[]={0,4,31},bpp[]={1,2,4,8},flags[]={0,16,8192,0xffffffff};
 for(UINT wi=0;wi<4;wi++)for(UINT hi=0;hi<4;hi++)for(UINT pi=0;pi<3;pi++)for(UINT bi=0;bi<4;bi++)for(UINT allowed=0;allowed<2;allowed++)for(UINT locked=0;locked<2;locked++)for(UINT ms=0;ms<2;ms++)for(UINT fi=0;fi<4;fi++)for(UINT rect=0;rect<7;rect++){
 UINT w=dims[wi],h=dims[hi],pitch=w*bpp[bi]+pads[pi];D9WGSurfaceLockRect r={0,0,(int32_t)w,(int32_t)h};if(rect==2)r.left=-1;if(rect==3)r.right++;if(rect==4)r.bottom++;if(rect==5)r.right=0;if(rect==6)r.top=-1;D9WGSurfaceLockPlan plan,saved;memset(&plan,0xa5,sizeof(plan));saved=plan;BOOL good=allowed&&!locked&&!ms&&rect<2;CHECK(d9wg_surface_lock_plan(w,h,pitch,bpp[bi],ms?4:0,allowed,locked,rect?&r:NULL,flags[fi],&plan)==good);if(good)CHECK(!plan.offset&&plan.readonly==!!(flags[fi]&16)&&plan.area.right==(int32_t)w&&plan.area.bottom==(int32_t)h);else CHECK(!memcmp(&plan,&saved,sizeof(plan)));plans++;
 }
 D9WGSurfaceLockPlan p;CHECK(!d9wg_surface_lock_plan(0,1,4,4,0,1,0,NULL,0,&p)&&!d9wg_surface_lock_plan(1,0,4,4,0,1,0,NULL,0,&p)&&!d9wg_surface_lock_plan(2,1,4,4,0,1,0,NULL,0,&p)&&!d9wg_surface_lock_plan(1,2,UINT32_MAX,4,0,1,0,NULL,0,&p)&&!d9wg_surface_lock_plan(1,1,4,4,0,1,0,NULL,0,NULL));
 width=8;height=6;BYTE front[4096],before[4096],expected[4096];
 for(UINT api=8;api<=9;api++)for(UINT format=21;format<=26;format++)for(UINT pad=0;pad<2;pad++)for(UINT ro=0;ro<2;ro++){
 pixel_bytes=format<=22?4:2;padding=pad?12:0;UINT pitch=width*pixel_bytes+padding,bytes=pitch*height;Device d={.handle=7,.front_shadow=front,.front_shadow_pitch=pitch,.present={1}};active=&d;Surface s={.device=&d,.swap_chain=api==9?(void*)1:NULL,.backbuffer=api==8,.width=width,.height=height,.format=format,.desc={.Format=format,.Size=bytes,.Width=width,.Height=height}};for(UINT i=0;i<width*height*pixel_bytes;i++)gpu[i]=(BYTE)(i*7+api);memcpy(before,gpu,width*height*pixel_bytes);memcpy(expected,before,width*height*pixel_bytes);memset(front,0xcc,sizeof(front));RECT area={1,2,6,5};D3DLOCKED_RECT out={123,(void*)99};events();CHECK(!lock(api,&s,&out,&area,ro?16:0)&&s.locked&&reads==1&&out.Pitch==(INT)pitch);BYTE*base=api==8?front:s.shadow;CHECK(out.pBits==base+area.top*pitch+area.left*pixel_bytes);for(UINT y=0;y<height;y++)CHECK(!memcmp(base+y*pitch,before+y*width*pixel_bytes,width*pixel_bytes));for(UINT y=0;y<3;y++){memset((BYTE*)out.pBits+y*pitch,0xa6,5*pixel_bytes);if(!ro)memset(expected+((y+2)*width+1)*pixel_bytes,0xa6,5*pixel_bytes);}D3DLOCKED_RECT rejected={123,(void*)99};CHECK(lock(api,&s,&rejected,NULL,16)==-1&&rejected.Pitch==123&&rejected.pBits==(void*)99&&reads==1);CHECK(!unlock(api,&s)&&!s.locked&&!s.lock_flags&&!memcmp(gpu,expected,width*height*pixel_bytes)&&creates==!ro&&uploads==!ro&&blits==!ro&&destroys==!ro&&!live);CHECK(unlock(api,&s)==-1);for(UINT i=0;i<width*height*pixel_bytes;i++)gpu[i]^=0x45;CHECK(!lock(api,&s,&out,NULL,16)&&reads==2&&out.pBits==base&&!unlock(api,&s));
 for(UINT bad=0;bad<8;bad++){events();RECT invalid={-1,0,2,2};out=(D3DLOCKED_RECT){123,(void*)99};d.present.Flags=bad==0?0:1;s.multisample=s.desc.MultiSampleType=bad==1?4:0;s.locked=bad==2;s.depth_stencil=0;UINT start_alloc=allocations;CHECK(lock(api,&s,bad==3?NULL:&out,bad>=4?&invalid:NULL,16)==-1&&out.Pitch==123&&out.pBits==(void*)99&&!reads&&!emits&&allocations==start_alloc);s.locked=FALSE;s.multisample=s.desc.MultiSampleType=0;d.present.Flags=1;}
 for(UINT failure=1;failure<=3;failure++){events();read_error=failure;out=(D3DLOCKED_RECT){123,(void*)99};CHECK(lock(api,&s,&out,NULL,16)==(failure==1?-1:-2)&&!s.locked&&!s.lock_flags&&out.Pitch==123&&out.pBits==(void*)99&&reads==1&&!emits);}
 for(UINT failure=1;failure<=3;failure++){events();CHECK(!lock(api,&s,&out,&area,0));fail_emit=failure;CHECK(unlock(api,&s)==-2&&!s.locked&&!s.lock_flags&&!live&&destroys==(failure>1));}
 if(api==9)HeapFree(NULL,0,s.shadow);cases++;
 }

 for(UINT kind=0;kind<2;kind++)for(UINT format=21;format<=26;format++)for(UINT pad=0;pad<2;pad++)for(UINT ro=0;ro<2;ro++){
 pixel_bytes=format<=22?4:2;padding=pad?12:0;active_target=77;UINT pitch=width*pixel_bytes+padding;Device d={.handle=7,.present={0}};active=&d;D9TextureLevel level={.shadow_valid=TRUE};Texture backing={.handle=77,.usage=1,.levels=&level};Surface s={.device=&d,.texture=&backing,.swap_chain=kind?(void*)1:NULL,.width=width,.height=height,.format=format,.lockable_render_target=TRUE};RECT area={1,2,6,5};D3DLOCKED_RECT out={123,(void*)99};for(UINT i=0;i<width*height*pixel_bytes;i++)gpu[i]=(BYTE)(i*13+format);memcpy(expected,gpu,width*height*pixel_bytes);events();CHECK(!lock(9,&s,&out,&area,ro?16:0)&&s.locked&&level.locked&&level.shadow_valid&&reads==1&&out.Pitch==(INT)pitch&&out.pBits==s.shadow+area.top*pitch+area.left*pixel_bytes);for(UINT y=0;y<3;y++){memset((BYTE*)out.pBits+y*pitch,0xa6,5*pixel_bytes);if(!ro)memset(expected+((y+2)*width+1)*pixel_bytes,0xa6,5*pixel_bytes);}CHECK(!unlock(9,&s)&&!s.locked&&!level.locked&&level.shadow_valid==(BOOL)ro&&!memcmp(gpu,expected,width*height*pixel_bytes)&&creates==!ro&&uploads==!ro&&blits==!ro&&destroys==!ro&&!live);
 events();level.locked=TRUE;out=(D3DLOCKED_RECT){123,(void*)99};CHECK(lock(9,&s,&out,NULL,16)==-1&&!s.locked&&out.Pitch==123&&out.pBits==(void*)99&&!reads);level.locked=FALSE;s.lockable_render_target=FALSE;CHECK(lock(9,&s,&out,NULL,16)==-1&&!reads&&!s.locked&&!level.locked);s.lockable_render_target=TRUE;s.multisample=4;CHECK(lock(9,&s,&out,NULL,16)==-1&&!reads&&!s.locked&&!level.locked);s.multisample=0;
 for(UINT fail=1;fail<=3;fail++){events();read_error=fail;out=(D3DLOCKED_RECT){123,(void*)99};CHECK(lock(9,&s,&out,NULL,16)==(fail==1?-1:-2)&&!s.locked&&!level.locked&&out.Pitch==123&&out.pBits==(void*)99&&reads==1&&!emits);}
 for(UINT fail=1;fail<=3;fail++){events();level.shadow_valid=TRUE;CHECK(!lock(9,&s,&out,&area,0)&&level.locked);fail_emit=fail;CHECK(unlock(9,&s)==-2&&!s.locked&&!level.locked&&!level.shadow_valid&&!live&&destroys==(fail>1));}
 events();for(UINT i=0;i<width*height*pixel_bytes;i++)gpu[i]^=0x35;level.shadow_valid=TRUE;CHECK(!lock(9,&s,&out,NULL,16)&&reads==1);for(UINT y=0;y<height;y++)CHECK(!memcmp(s.shadow+y*pitch,gpu+y*width*pixel_bytes,width*pixel_bytes));CHECK(!unlock(9,&s)&&!level.locked&&level.shadow_valid&&!emits);HeapFree(NULL,0,s.shadow);cases++;
 }
 active_target=0;
 events();pixel_bytes=4;padding=0;Device d={.handle=7,.present={1}};active=&d;Surface s={.device=&d,.swap_chain=(void*)1,.width=width,.height=height,.format=21};D3DLOCKED_RECT out={123,(void*)99};fail_alloc=1;CHECK(lock(9,&s,&out,NULL,16)==-3&&!s.shadow&&!s.locked&&!reads&&out.Pitch==123&&out.pBits==(void*)99);
 Surface cpu={.device=&d,.width=width,.height=height,.format=21,.shadow=front,.row_pitch=32,.desc={.Width=width,.Height=height},.locked=FALSE};CHECK(!lock(8,&cpu,&out,NULL,16)&&!unlock(8,&cpu)&&!lock(9,&cpu,&out,NULL,16)&&!unlock(9,&cpu)&&!reads&&!emits);Texture texture={0};cpu.shadow=NULL;cpu.texture=&texture;CHECK(lock(8,&cpu,&out,NULL,0)==-11&&unlock(8,&cpu)==-21&&lock(9,&cpu,&out,NULL,0)==-11&&unlock(9,&cpu)==-21);D9TextureLevel cube_levels[6]={{.locked=TRUE}};texture.level_count=1;texture.levels=cube_levels;cpu.texture=NULL;cpu.cube=cpu.cube_texture=&texture;CHECK(lock(8,&cpu,&out,NULL,0)==-12&&unlock(8,&cpu)==-22&&lock(9,&cpu,&out,NULL,0)==-12&&unlock(9,&cpu)==-22);cube_levels[0].locked=FALSE;CHECK(unlock(9,&cpu)==-1);cube_levels[5].dc_active=TRUE;CHECK(unlock(9,&cpu)==-22&&cube_levels[5].dc_active);CHECK(allocations==frees);
 for(UINT api=8;api<=9;api++){Device profile={.compatibility_api=api};Surface image={.device=&profile,.width=3,.height=2,.format=65,.row_pitch=16,.shadow=front};RECT part={1,0,3,2};memset(front,0,sizeof(front));out=(D3DLOCKED_RECT){123,(void*)99};events();HRESULT hr=lock(9,&image,&out,&part,0);if(api==8){CHECK(!hr&&out.Pitch==16&&out.pBits==front+4&&image.locked);memset(out.pBits,0x73,8);memset((BYTE*)out.pBits+16,0x45,8);CHECK(!unlock(9,&image)&&!image.locked&&!reads&&!emits);CHECK(!lock(9,&image,&out,NULL,16)&&out.pBits==front&&out.Pitch==16&&front[4]==0x73&&front[20]==0x45&&!unlock(9,&image));}else CHECK(hr==-1&&!image.locked&&out.Pitch==123&&out.pBits==(void*)99);CHECK(!reads&&!emits);}

 /* NULL targets expose a bounded scratch lock, with no readable color or GPU
  * transfer. Exercise actual dispatch, readonly/write, OOM and lock lifetime. */
 for(UINT ro=0;ro<2;ro++)for(UINT sub=0;sub<2;sub++){
  events();padding=0;D9TextureLevel level={0};Texture tex={.handle=777,.usage=1,.levels=&level};
  Surface target={.device=&d,.texture=&tex,.width=8,.height=6,.format=D9FMT_NULL,.lockable_render_target=TRUE};
  RECT area={2,1,6,4};out=(D3DLOCKED_RECT){123,(void*)99};fail_alloc=1;
  CHECK(lock(9,&target,&out,NULL,ro?16:0)==-3&&!target.shadow&&!target.locked&&!level.locked&&!reads&&!emits);
  CHECK(!lock(9,&target,&out,sub?&area:NULL,ro?16:0)&&out.Pitch==32&&out.pBits==target.shadow+(sub?40:0)&&target.locked&&level.locked&&!reads&&!emits);
  memset(out.pBits,0x57,sub?16:32);CHECK(lock(9,&target,&out,NULL,0)==-1&&!reads&&!emits);
  CHECK(!unlock(9,&target)&&!target.locked&&!level.locked&&!reads&&!emits);
  CHECK(unlock(9,&target)==-1);target.lockable_render_target=FALSE;CHECK(lock(9,&target,&out,NULL,0)==-1);
  target.lockable_render_target=TRUE;RECT bad={-1,0,8,6};CHECK(lock(9,&target,&out,&bad,0)==-1&&!reads&&!emits);
  HeapFree(NULL,0,target.shadow);cases++;
 }
 CHECK(allocations==frees);
 printf("PASS actual8/9 renderable locks: %u checks,%u pureplans,%u adapter scenarios;six formats/padded pitches/partial writes,real shared upload bytes,readonly GPU refresh,no-upload,output guards,MSAA/flags/doublelock,readback/OOM/emission failure cleanup andCPU/texture/cube dispatch;standalone/additional targets,target flags/identity,level lock Reset tracking,stale-mirror invalidation andNULL scratch mapping\\n",checks,plans,cases);return 0;
}
`;
fs.writeFileSync(dir+"/lock.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root+"/tests/native","-I",root,dir+"/lock.c","-o",dir+"/lock"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/lock",[],{encoding:"utf8",timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
