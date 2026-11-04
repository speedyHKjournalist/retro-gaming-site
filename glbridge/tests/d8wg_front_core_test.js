"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"front8-core-")),source=fs.readFileSync(process.env.D8WG_FRONT_CORE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
function desc(api,name){const sdk=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",['-E','-P','-x','c','-'],{input:'#include <d3d'+api+'.h>\n',encoding:'utf8',maxBuffer:16<<20});assert.equal(sdk.status,0,sdk.stderr);const m=/typedef struct _D3DSURFACE_DESC\s*\{[\s\S]*?\}\s*D3DSURFACE_DESC\s*;/.exec(sdk.stdout);assert(m);return m[0].replace('struct _D3DSURFACE_DESC','struct').replace(/\bD3DSURFACE_DESC\b/g,name);}
try{const block=source.match(/#define D9WG_FRONT_DEVICE D9Device[\s\S]*?#include "\.\.\/runtime\/d9wg_front_buffer_bridge8.h"/);assert(block);let old=fs.readFileSync(root+'/tests/fixtures/d3d8_front_before_core.c','utf8');old=extract(old,'device_get_front_buffer');for(const[from,to]of Object.entries({device_get_front_buffer:'oracle8_front',D8Device:'D9Device',D8Surface:'D9Surface',D8TextureLevel:'D9TextureLevel',IDirect3DDevice8:'IDirect3DDevice9',IDirect3DSurface8:'IDirect3DSurface9',D3DSURFACE_DESC:'Desc8',D8WG_TRACE_ERROR:'TRACE_REFUSE',surface_device:'oracle_owner',surface_get_desc:'oracle_desc',surface_pixels:'oracle_pixels',create_backbuffer_surface:'oracle_source',readback_surface_pixels:'oracle_read',upload_surface_rect:'oracle_upload',surface_texture_level:'oracle_level',IDirect3DSurface8_Release:'release_source'}))old=old.replace(new RegExp('\\b'+from+'\\b','g'),to);
const code=`#include <stdio.h>
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
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DPOOL_SYSTEMMEM 2u
#define D3DPOOL_SCRATCH 3u
#define HOSTLOG_FAILED(...) ((void)0)
#define D3DUSAGE_RENDERTARGET 1u
#define D3DUSAGE_DEPTHSTENCIL 2u
#define D3DRTYPE_SURFACE 1u
#define D3DBACKBUFFER_TYPE_MONO 0u
#define TRACE_REFUSE(v) (v)
#define FAILED(v) ((v)<0)
#define SUCCEEDED(v) ((v)>=0)
#define CopyMemory memcpy
#define HEAP_ZERO_MEMORY 8u
#define GetProcessHeap() NULL
typedef struct{int32_t left,top,right,bottom;}RECT;
static void SetRect(RECT*r,int l,int t,int rr,int b){*r=(RECT){l,t,rr,b};}
typedef struct{const void*lpVtbl;}Iface,IDirect3DSurface9,IDirect3DDevice9;
typedef struct D9Device D9Device;typedef struct D9Surface D9Surface;
typedef struct{BYTE*shadow;UINT row_pitch,row_count,byte_count,width,height;BOOL locked,shadow_valid,gpu_dirty;}D9TextureLevel;
typedef struct Texture{D9Device*device;UINT handle,level_count,usage,pool,format;D9TextureLevel*levels;}D9Texture,D9CubeTexture;
struct D9Device{Iface iface;UINT api,handle,present;D9Surface*source;};
struct D9Surface{Iface iface;D9Device*device;D9Texture*texture;D9CubeTexture*cube_texture;void*swap_chain;UINT cube_face,level,width,height,format,pool,multisample,multisample_quality;BYTE*shadow;UINT row_pitch,byte_count;BOOL locked,auto_depth_stencil,depth_stencil;UINT refs;};
static int g_surface_vtbl;static D9WGProcess state,*g_process=&state;static D9Device device;static D9Surface*current_destination;static UINT reads,source_gets,releases,upload_calls,observer_error,read_result,get_result,upload_failure,handle_counter,alloc_attempts,fail_at,allocations,frees,live;
static D9Device*device_from_iface(Iface*i){return(D9Device*)i;}static D9Surface*surface_from_iface(Iface*i){return(D9Surface*)i;}static UINT device_compatibility_api(D9Device*d){return d->api;}
static BYTE*HeapAlloc(void*h,UINT flags,size_t n){(void)h;if(++alloc_attempts==fail_at)return NULL;BYTE*p=malloc(n+32);assert(p);memset(p,0xa5,n+32);memset(p+16,flags&8?0:0xcd,n);allocations++;live++;return p+16;}
static void free_shadow(BYTE*p,UINT n){if(!p)return;for(UINT i=0;i<16;i++)assert(p[-16+(int)i]==0xa5&&p[n+i]==0xa5);free(p-16);frees++;assert(live);live--;}
static UINT allocate_handle(void){return ++handle_counter;}
${extract(source,'surface_texture_level')}
${extract(source,'surface_get_desc')}
static HRESULT get_source(Iface*i,UINT sc,UINT index,UINT type,Iface**out){D9Device*d=device_from_iface(i);source_gets++;assert(d==&device&&!sc&&!index&&!type);*out=NULL;if(get_result)return -(HRESULT)get_result;d->source->refs++;*out=&d->source->iface;return 0;}
static UINT release_source(Iface*i){D9Surface*s=surface_from_iface(i);assert(s==device.source&&s->refs);releases++;return --s->refs;}
#define IDirect3DDevice9_GetBackBuffer get_source
#define IDirect3DSurface9_Release release_source
static BOOL submit_readback_batch_locked(BOOL p){(void)p;return TRUE;}
static BYTE pattern(UINT y,UINT b,UINT fmt){return fmt==22&&b%4==3?255:(BYTE)(13+y*37+b*3);}
static HRESULT observe_read(D9Surface*s,UINT fmt,BYTE*p,UINT pitch,UINT bytes){UINT bw,bh,pb;if(!p||!d9wg_format_layout(8,fmt,&bw,&bh,&pb)||pitch<s->width*pb||bytes<(s->height-1)*pitch+s->width*pb)return 1;reads++;if(read_result)return read_result;for(UINT y=0;y<s->height;y++)for(UINT x=0;x<s->width*pb;x++)p[y*pitch+x]=pattern(y,x,fmt);return 0;}
int d9wg_surface_readback(D9WGProcess*p,const D9WGReadbackSurface*q,uint32_t row_bytes,uint32_t rows,uint8_t*out,uint32_t bytes,BOOL(*flush)(BOOL),D9WGReadbackDiagnostic*detail){D9Surface*s=device.source;UINT bw,bh,pb;if(!out)return 1;assert(p==g_process&&d9wg_format_layout(8,current_destination->format,&bw,&bh,&pb));if(q->device_handle!=device.handle||q->texture_handle||q->level||q->face||q->format!=current_destination->format||q->width!=s->width||q->height!=s->height||q->destination_pitch!=s->width*pb||row_bytes!=s->width*pb||rows!=s->height||bytes!=s->byte_count||flush!=submit_readback_batch_locked){observer_error=__LINE__;return 2;}memset(detail,0,sizeof(*detail));return observe_read(s,q->format,out,q->destination_pitch,bytes);}
static D9TextureLevel*oracle_level(D9Surface*s);
static BOOL observe_upload(D9Surface*s,UINT kind,UINT face,UINT level,const RECT*r,const BYTE*p,UINT pitch,UINT pb){D9Surface*src=device.source;if(s!=current_destination||r->left||r->top||r->right!=(int32_t)src->width||r->bottom!=(int32_t)src->height||pitch!= (oracle_level(s)?oracle_level(s)->row_pitch:s->row_pitch)||!p||kind!=(s->texture?1u:s->cube_texture?2u:s->swap_chain?3u:0u)||face!=(s->cube_texture?s->cube_face:0u)||level!=(s->texture||s->cube_texture?s->level:0u)){observer_error=__LINE__;return FALSE;}UINT bw,bh,expected;assert(d9wg_format_layout(8,s->format,&bw,&bh,&expected));if(pb!=expected){observer_error=__LINE__;return FALSE;}if(kind)upload_calls++;return !kind||!upload_failure;}
static BOOL emit_texture_update(D9Texture*t,UINT level,const RECT*r){D9Surface*s=current_destination;if(t!=s->texture){observer_error=__LINE__;return FALSE;}UINT bw,bh,pb;assert(d9wg_format_layout(8,s->format,&bw,&bh,&pb));return observe_upload(s,1,0,level,r,t->levels[level].shadow,t->levels[level].row_pitch,pb);}
static BOOL emit_cube_texture_update(D9CubeTexture*t,UINT face,UINT level,const RECT*r){D9Surface*s=current_destination;if(t!=s->cube_texture){observer_error=__LINE__;return FALSE;}UINT bw,bh,pb;assert(d9wg_format_layout(8,s->format,&bw,&bh,&pb));D9TextureLevel*l=&t->levels[face*t->level_count+level];return observe_upload(s,2,face,level,r,l->shadow,l->row_pitch,pb);}
typedef struct{int32_t left,top,right,bottom;}D9WGSurfaceLockRect;
static BOOL emit_command(uint16_t o,const void*p,uint32_t n){(void)o;(void)p;(void)n;return TRUE;}
static BOOL emit_texture_rows(const D9WGTextureRows*r){(void)r;return TRUE;}
static BOOL observe_renderable(UINT dev,UINT handle,UINT fmt,UINT target,UINT level,UINT face,const D9WGSurfaceLockRect*r,const BYTE*p,UINT pitch,UINT pb,int(*emit)(uint16_t,const void*,uint32_t),int(*emit_rows)(const D9WGTextureRows*)){if(dev!=device.handle||handle!=101||fmt!=current_destination->format||target||emit!=emit_command||emit_rows!=emit_texture_rows){observer_error=__LINE__;return FALSE;}RECT a={r->left,r->top,r->right,r->bottom};return observe_upload(current_destination,3,face,level,&a,p,pitch,pb);}
#define d9wg_renderable_upload observe_renderable
${['front_pixels8','front_layout8','readback_front8','upload_front8'].map(n=>extract(source,n)).join('\n')}
${block[0].replace('../runtime/d9wg_front_buffer_bridge8.h',process.env.D8WG_FRONT_BRIDGE_HEADER||root+'/runtime/d9wg_front_buffer_bridge8.h')}
static BOOL texture_level_layout(UINT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(9u,f,w,h,p,r,b);}
${extract(source,'copy_surface_to_system_memory')}
${extract(source,'d9wg_d3d9_core_get_front_buffer8')}
/* Frozen8 body is bound to independent metadata/read/upload observations. */
static D9Device*oracle_owner(D9Surface*s){return s->device;}
static D9TextureLevel*oracle_level(D9Surface*s){if(s->texture)return &s->texture->levels[s->level];if(s->cube_texture)return &s->cube_texture->levels[s->cube_face*s->cube_texture->level_count+s->level];return NULL;}
static HRESULT oracle_desc(Iface*i,Desc8*d){D9Surface*s=surface_from_iface(i);memset(d,0,sizeof(*d));d->Format=s->format;d->Type=1;d->Pool=s->pool;d->Width=s->width;d->Height=s->height;d->Size=s->byte_count;return 0;}
static BYTE*oracle_pixels(D9Surface*s,UINT*pitch){D9TextureLevel*l=oracle_level(s);*pitch=l?l->row_pitch:s->row_pitch;return l?l->shadow:s->shadow;}
static HRESULT oracle_source(D9Device*d,const void*p,BOOL reset,Iface**out){(void)p;assert(!reset);return get_source(&d->iface,0,0,0,out);}
static HRESULT oracle_read(D9Surface*s,UINT fmt,BYTE*p,UINT pitch,UINT bytes){HRESULT h=observe_read(s,fmt,p,pitch,bytes);return !h?0:h==1?-1:-2;}
static BOOL oracle_upload(D9Surface*s,UINT fmt,const RECT*r,const BYTE*p,UINT pitch,UINT pb){assert(fmt==s->format);return observe_upload(s,s->texture?1:s->cube_texture?2:s->swap_chain?3:0,s->cube_texture?s->cube_face:0,s->texture||s->cube_texture?s->level:0,r,p,pitch,pb);}
static BOOL texture_format_layout(UINT f,UINT*bw,UINT*bh,UINT*pb){return d9wg_format_layout(8,f,bw,bh,pb);}
${old}
static UINT cases,checks;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Front8 core case %u line %u: %s observer=%u\\n",cases,__LINE__,#v,observer_error);return 1;}checks++;}while(0)
static void counters(void){reads=source_gets=releases=upload_calls=observer_error=0;handle_counter=100;alloc_attempts=fail_at=0;}
int main(void){(void)&copy_surface_to_system_memory;(void)&d9wg_front_buffer8_common;_Static_assert(sizeof(Desc8)==32&&sizeof(D3DSURFACE_DESC)==32,"SDK descriptions");_Static_assert(offsetof(Desc8,Size)==16&&offsetof(D3DSURFACE_DESC,MultiSampleType)==16&&offsetof(D3DSURFACE_DESC,MultiSampleQuality)==20,"different SDK description fields");const UINT formats[]={21,22,23,0},widths[]={1,3,4};BYTE source_a[256],source_b[256],data_a[1024],data_b[1024];D9TextureLevel levels_a[12],levels_b[12];
for(UINT sf=0;sf<3;sf++)for(UINT df=0;df<4;df++)for(UINT wi=0;wi<3;wi++)for(UINT h=1;h<=2;h++)for(UINT extent=0;extent<5;extent++)for(UINT pad=0;pad<2;pad++)for(UINT kind=0;kind<5;kind++)for(read_result=0;read_result<4;read_result++)for(upload_failure=0;upload_failure<2;upload_failure++){
 UINT w=widths[wi],dw=w+(extent==1||extent==3?2:0),dh=h+(extent==2||extent==3?2:0);if(extent==4)dw=w-1;UINT spb=sf==2?2:4,dpb=df==2?2:4,source_pitch=w*spb,destination_pitch=dw*dpb+pad*4,source_bytes=source_pitch*h,destination_bytes=destination_pitch*dh;memset(source_a,0x77,sizeof(source_a));memset(source_b,0x77,sizeof(source_b));memset(data_a,0xcc,sizeof(data_a));memset(data_b,0xcc,sizeof(data_b));memset(levels_a,0,sizeof(levels_a));memset(levels_b,0,sizeof(levels_b));device=(D9Device){.api=8,.handle=91};D9Surface sa={.iface={&g_surface_vtbl},.device=&device,.width=w,.height=h,.format=formats[sf],.swap_chain=&state,.shadow=source_a,.row_pitch=source_pitch,.byte_count=source_bytes},sb=sa;sb.shadow=source_b;UINT li=kind==2?9:0;levels_a[li]=(D9TextureLevel){.shadow=data_a,.width=dw,.height=dh,.row_pitch=destination_pitch,.row_count=dh,.byte_count=destination_bytes,.shadow_valid=FALSE,.gpu_dirty=TRUE};levels_b[li]=levels_a[li];levels_b[li].shadow=data_b;D9Texture ta={.device=&device,.handle=73,.level_count=2,.usage=kind==4?1:0,.pool=kind==4?0:1,.format=formats[df],.levels=levels_a},tb=ta;tb.levels=levels_b;D9Surface a={.iface={&g_surface_vtbl},.device=&device,.width=dw,.height=dh,.format=formats[df],.pool=kind==0?2:0,.shadow=kind==0||kind==3?data_a:NULL,.row_pitch=destination_pitch,.byte_count=destination_bytes,.texture=kind==1||kind==4?&ta:NULL,.cube_texture=kind==2?&ta:NULL,.cube_face=4,.level=kind==2?1:0,.swap_chain=kind==3?&state:NULL},b=a;b.shadow=kind==0||kind==3?data_b:NULL;b.texture=kind==1||kind==4?&tb:NULL;b.cube_texture=kind==2?&tb:NULL;
 counters();device.source=&sa;current_destination=&a;HRESULT expected=oracle8_front(&device.iface,&a.iface);UINT er=reads,eg=source_gets,el=releases,eu=upload_calls;CHECK(!observer_error&&!sa.refs);counters();device.source=&sb;current_destination=&b;HRESULT actual=d9wg_d3d9_core_get_front_buffer8(&device.iface,&b.iface);CHECK(actual==expected&&reads==er&&source_gets==eg&&releases==el&&upload_calls==eu&&!observer_error&&!sb.refs);CHECK(!memcmp(data_a,data_b,sizeof(data_a)));if(kind==1||kind==2||kind==4)CHECK(levels_b[li].shadow_valid==!levels_a[li].gpu_dirty);CHECK(alloc_attempts==0);cases++;
}
/* Profile/null guards do not acquire a source view. */
counters();device.api=9;CHECK(d9wg_d3d9_core_get_front_buffer8(&device.iface,NULL)==-1&&!source_gets);CHECK(d9wg_d3d9_core_get_front_buffer8(NULL,NULL)==-1&&!source_gets);device.api=8;CHECK(d9wg_d3d9_core_get_front_buffer8(&device.iface,NULL)==-1&&!source_gets);cases+=3;
/* Frontend validation and source acquisition failures keep all target bytes. */
read_result=upload_failure=0;
for(UINT bad=0;bad<10;bad++){
 memset(data_a,0xcc,sizeof(data_a));memset(data_b,0xcc,sizeof(data_b));D9TextureLevel la={.shadow=data_a,.width=3,.height=2,.row_pitch=12,.row_count=2,.byte_count=24,.gpu_dirty=TRUE},lb=la;lb.shadow=data_b;D9Texture ta={.device=&device,.handle=73,.level_count=1,.format=21,.levels=&la},tb=ta;tb.levels=&lb;D9Device foreign={.handle=92};D9Surface sa={.iface={&g_surface_vtbl},.device=&device,.width=3,.height=2,.format=21,.shadow=source_a,.row_pitch=12,.byte_count=24,.swap_chain=&state},sb=sa;sb.shadow=source_b;D9Surface a={.iface={&g_surface_vtbl},.device=&device,.width=3,.height=2,.format=21,.texture=&ta},b=a;b.texture=&tb;
 if(bad==1){a.iface.lpVtbl=b.iface.lpVtbl=NULL;}if(bad==2){a.device=b.device=&foreign;ta.device=tb.device=&foreign;}if(bad==3)a.locked=b.locked=TRUE;if(bad==4)la.locked=lb.locked=TRUE;if(bad==5){a.depth_stencil=b.depth_stencil=a.auto_depth_stencil=b.auto_depth_stencil=TRUE;}if(bad==7)sa.format=sb.format=0;if(bad==8)a.width=b.width=2;if(bad==9)a.format=b.format=0;get_result=bad==6?3:0;
 counters();device.source=&sa;current_destination=&a;HRESULT wanted=oracle8_front(&device.iface,bad==0?NULL:&a.iface);UINT er=reads,eg=source_gets,el=releases,eu=upload_calls;counters();device.source=&sb;current_destination=&b;HRESULT actual=d9wg_d3d9_core_get_front_buffer8(&device.iface,bad==0?NULL:&b.iface);CHECK(actual==wanted&&actual==(bad==6?-3:-1)&&reads==er&&source_gets==eg&&releases==el&&upload_calls==eu&&!observer_error&&!sb.refs);CHECK(!memcmp(data_a,data_b,sizeof(data_a))&&!alloc_attempts);cases++;
}
get_result=0;
/* A cached backbuffer may be both source view and destination view. */
{memset(source_a,0x77,sizeof(source_a));memset(source_b,0x77,sizeof(source_b));D9Surface a={.iface={&g_surface_vtbl},.device=&device,.width=3,.height=2,.format=21,.shadow=source_a,.row_pitch=12,.byte_count=24,.swap_chain=&state,.refs=1},b=a;b.shadow=source_b;counters();device.source=&a;current_destination=&a;CHECK(!oracle8_front(&device.iface,&a.iface)&&a.refs==1);counters();device.source=&b;current_destination=&b;CHECK(!d9wg_d3d9_core_get_front_buffer8(&device.iface,&b.iface)&&b.refs==1&&source_gets==1&&releases==1&&reads==1&&upload_calls==1&&!observer_error);CHECK(!memcmp(source_a,source_b,sizeof(source_a)));cases++;}
/* Real core storage allocation, first/second allocation failures and guards. */
read_result=upload_failure=0;for(UINT fail=0;fail<3;fail++){D9Surface src={.iface={&g_surface_vtbl},.device=&device,.width=3,.height=2,.format=21,.swap_chain=&state},dst={.iface={&g_surface_vtbl},.device=&device,.width=5,.height=4,.format=21};device.source=&src;current_destination=&dst;counters();fail_at=fail;HRESULT hr=d9wg_d3d9_core_get_front_buffer8(&device.iface,&dst.iface);CHECK(hr==(fail?-1:0)&&!observer_error&&!src.refs&&source_gets==1&&releases==1&&alloc_attempts==2);CHECK(reads==!fail);if(!fail){for(UINT y=0;y<4;y++)for(UINT x=0;x<20;x++)CHECK(dst.shadow[y*20+x]==(y<2&&x<12?(x%4==3?255:pattern(y,x,21)):0));}free_shadow(src.shadow,src.byte_count);free_shadow(dst.shadow,dst.byte_count);CHECK(!live);cases++;}
/* Lazy level shadows use the actual parent layout, including2-byte RGB. */
const UINT lazy_formats[]={21,23,65}; /*65 is internal CPU metadata, not a backbuffer capability/GPU claim.*/
for(UINT kind=1;kind<=4;kind++)if(kind!=3)for(UINT fi=0;fi<3;fi++)for(UINT fail=0;fail<2;fail++){UINT fmt=lazy_formats[fi];
 UINT pb=fmt==23?2:4,dh=kind==2?5:4;D9TextureLevel levels[12]={0};UINT index=kind==2?9:0;levels[index].width=5;levels[index].height=dh;D9Texture tex={.device=&device,.handle=73,.level_count=2,.usage=kind==4?1:0,.format=fmt,.levels=levels};D9Surface src={.iface={&g_surface_vtbl},.device=&device,.width=3,.height=2,.format=fmt,.shadow=source_a,.row_pitch=3*pb,.byte_count=6*pb,.swap_chain=&state},dst={.iface={&g_surface_vtbl},.device=&device,.width=5,.height=dh,.format=fmt,.texture=kind==1||kind==4?&tex:NULL,.cube_texture=kind==2?&tex:NULL,.cube_face=4,.level=kind==2?1:0};device.source=&src;current_destination=&dst;counters();fail_at=fail;HRESULT hr=d9wg_d3d9_core_get_front_buffer8(&device.iface,&dst.iface);CHECK(hr==(fail?-1:0)&&!observer_error&&!src.refs&&source_gets==1&&releases==1&&alloc_attempts==1&&reads==!fail);CHECK(levels[index].row_pitch==5*pb&&levels[index].byte_count==5*pb*dh&&!levels[index].shadow_valid);if(!fail)for(UINT y=0;y<dh;y++)for(UINT x=0;x<5*pb;x++)CHECK(levels[index].shadow[y*5*pb+x]==(y<2&&x<3*pb?(fmt==21&&x%4==3?255:pattern(y,x,fmt)):0));free_shadow(levels[index].shadow,levels[index].byte_count);CHECK(!live);cases++;
}
CHECK(allocations==frees);printf("PASS private/shared API8 front: %u checks,%u cases;actual frozen8 body,SDK8/9 descriptor fields,21/22/23 formats,equal/larger/padded targets,CPU/texture/cube/backbuffer/RT,opaquealpha,read/upload failures,source refs,realallocation/OOM andguards\\n",checks,cases);return 0;}
`;
fs.writeFileSync(dir+'/front.c',code);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root+'/tests/native','-I',root,dir+'/front.c','-o',dir+'/front'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/front',[],{encoding:'utf8',timeout:180000});assert.equal(run.status,0,run.stdout+run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
