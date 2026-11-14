'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const file=process.env.DDRAW_DEPTH_LAYOUT_SOURCE||path.resolve(__dirname,'../ddrawproxy/ddraw_proxy.c'),source=fs.readFileSync(file,'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-depth-layout-'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
const prefix=String.raw`
/* Existing fixtures model undetached subresources. Detached mappings use
 * actual helpers in ddraw_memory_graph_test and real XP/GPU graph probes. */
#define surface_wire_mip_level(s) ((s)->mip_level)
#define surface_wire_cube_face(s) ((s)->cube_face)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "DEPTH_HEADER"
#include "FORMAT_HEADER"
typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef int BOOL;typedef uint8_t BYTE;typedef size_t SIZE_T;
typedef struct {LONG left,top,right,bottom;}RECT;
typedef struct {DWORD dwSize,dwFlags,dwFourCC;union{DWORD dwRGBBitCount,dwZBufferBitDepth;};union{DWORD dwRBitMask,dwStencilBitDepth;};union{DWORD dwGBitMask,dwZBitMask;};union{DWORD dwBBitMask,dwStencilBitMask;};DWORD dwRGBAlphaBitMask;}DDPIXELFORMAT;
typedef struct {DWORD flags,fourcc,bit_count,red_mask,green_mask,blue_mask,alpha_mask,z_bit_depth,stencil_bit_depth;}DDrawPixelFormatDesc;
typedef struct{DWORD low,high;}DDCOLORKEY;
#define PREFIX DWORD dwSize,dwFlags,dwHeight,dwWidth;LONG lPitch;DWORD dwBackBufferCount;union{DWORD dwMipMapCount,dwZBufferBitDepth;};DWORD dwAlphaBitDepth,dwReserved;void*lpSurface;DDCOLORKEY ddckCKDestOverlay,ddckCKDestBlt,ddckCKSrcOverlay,ddckCKSrcBlt;DDPIXELFORMAT ddpfPixelFormat
typedef struct{PREFIX;struct{DWORD dwCaps;}ddsCaps;}DDSURFACEDESC;
typedef struct{PREFIX;struct{DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4;}ddsCaps;DWORD dwTextureStage;}DDSURFACEDESC2;
#define DDSD_ZBUFFERBITDEPTH 0x40u
#define DDSD_PIXELFORMAT 0x1000u

#define DDPF_ZBUFFER 0x400u
#define DDPF_STENCILBUFFER 0x4000u
typedef struct {BOOL gpu_dirty;}DDSurfaceMemory;
typedef struct {DWORD device_handle;}DDrawObject;
typedef struct {DDrawObject*owner;DDSurfaceMemory*memory;BYTE*shadow;UINT width,height,pitch,bytes_per_pixel,format,handle,mip_level,cube_face;BOOL upload_pending;struct{DDPIXELFORMAT ddpfPixelFormat;}desc;}DDSurface;
typedef struct {UINT resource_handle,level,x,y,z,width,height,block_height,row_bytes,row_count,source_pitch;const BYTE*source;}D9WGTextureRows;
typedef struct {UINT device_handle,texture_handle,level,face,format,width,height,first_row,row_count,destination_pitch,destination_bytes,response_offset,request_id;}D9WGReadbackSurface;
typedef struct {UINT request_id,status,byte_count,reserved;}D9WGReadbackResponse;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define ZeroMemory(p,n) memset(p,0,n)
#define MemoryBarrier() ((void)0)
#define DDWG_TRACE_CHECKPOINT(...)
#define HOSTLOG_FAILED(...)
#define DDWG_FMT_D16_LOCKABLE 70u
#define DDWG_FMT_D32 71u
#define DDWG_FMT_D15S1 73u
#define DDWG_FMT_D24S8 75u
#define DDWG_FMT_D24X8 77u
#define DDWG_FMT_D24X4S4 79u
#define DDWG_FMT_D16 80u
#define DDWG_FMT_YUY2 0x32595559u
#define DDWG_FMT_UYVY 0x59565955u
#define D9WG_RESPONSE_REGION_BYTES 128u
#define D9WG_READBACK_REGION_OFFSET 0u
#define D9WG_HEARTBEAT_BYTES 4u
#define D9WG_HEARTBEAT_OFFSET 124u
#define D9WG_RESPONSE_PENDING 0u
#define D9WG_RESPONSE_OK 1u
#define D9WG_OP_READBACK_SURFACE 1u
static unsigned checks,locks,allocations,uploads,readbacks;static int g_readback_lock,g_transport_lock;static DWORD g_next_request_id;
static BOOL fail_allocate,fail_read,fail_upload;static BYTE response_bytes[128],command_bytes[128],gpu[140];static D9WGTextureRows captured;static BYTE uploaded[140];
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"depth transfer line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
#define EnterCriticalSection(p) ((void)(p),++locks)
#define LeaveCriticalSection(p) do{(void)(p);MUST(locks);--locks;}while(0)
#define InterlockedIncrement(p) (++*(p))
static void*heap_alloc_zero(SIZE_T n){if(fail_allocate)return NULL;void*p=calloc(1,n);if(p)allocations++;return p;}
static void heap_free(void*p){if(p){MUST(allocations);allocations--;free(p);}}
static BOOL ensure_surface_storage(DDSurface*s){return !!s->handle;}
static void surface_memory_upload_aliases(DDSurface*s,BOOL pending){s->upload_pending=pending;}
static BOOL emit_texture_rows(const D9WGTextureRows*u){uploads++;captured=*u;MUST(u->row_count*u->row_bytes<=sizeof(uploaded));for(UINT y=0;y<u->row_count;y++)memcpy(uploaded+y*u->row_bytes,u->source+y*u->source_pitch,u->row_bytes);return !fail_upload;}
static BOOL open_transport_locked(void){return TRUE;}
static BYTE*response_region(void){return response_bytes;}
static BOOL reserve_command_locked(UINT op,UINT n,UINT extra,BYTE**p,void*unused){(void)extra;(void)unused;MUST(op==D9WG_OP_READBACK_SURFACE&&n==sizeof(D9WGReadbackSurface));*p=command_bytes;return !fail_read;}
static BOOL submit_batch_locked(BOOL present){const D9WGReadbackSurface*c=(const void*)command_bytes;D9WGReadbackResponse*r=(void*)response_bytes;MUST(!present&&c->width==3&&c->height==7&&c->destination_pitch==20);MUST(c->destination_bytes==c->row_count*20&&c->first_row+c->row_count<=7);readbacks++;memcpy(r+1,gpu+c->first_row*20,c->destination_bytes);r->request_id=c->request_id;r->status=D9WG_RESPONSE_OK;r->byte_count=c->destination_bytes;return TRUE;}
static DWORD GetTickCount(void){return 0;}static void Sleep(DWORD ms){(void)ms;}
`;
const tests=String.raw`
static DWORD get(const BYTE*p){DWORD v;memcpy(&v,p,4);return v;}
static void put(BYTE*p,DWORD v){memcpy(p,&v,4);}
static void transfer(unsigned format,DWORD z,DWORD stencil){DDSurfaceMemory memory={.gpu_dirty=TRUE};DDrawObject owner={7};BYTE shadow[140],before[140];DDSurface s={.owner=&owner,.memory=&memory,.shadow=shadow,.width=3,.height=7,.pitch=20,.bytes_per_pixel=4,.format=format,.handle=9,.desc={.ddpfPixelFormat={.dwZBitMask=z,.dwStencilBitMask=stencil}}};if(!z){DDSURFACEDESC old={.dwSize=sizeof(old),.dwFlags=DDSD_ZBUFFERBITDEPTH,.dwZBufferBitDepth=24};DDSURFACEDESC2 modern;desc1_to_desc2(&old,&modern);s.desc.ddpfPixelFormat=modern.ddpfPixelFormat;MUST(s.desc.ddpfPixelFormat.dwZBitMask==0xffffffu);}memset(shadow,0x5a,140);for(unsigned i=0;i<140;i++)gpu[i]=(BYTE)(0x80+i);for(unsigned y=0;y<7;y++)for(unsigned x=0;x<3;x++)put(gpu+y*20+x*4,((0x120000u+y*0x1100u+x*0x22u)<<8)|(format==75?0xa0u+y:format==79?(0xb0u|(y&15)):0));
 unsigned old_reads=readbacks;MUST(surface_readback(&s)&&!memory.gpu_dirty&&!locks&&readbacks==old_reads+2);
 for(unsigned y=0;y<7;y++){for(unsigned x=0;x<3;x++){DWORD original=get(gpu+y*20+x*4),expected=(original>>8)|((original&255)<<24);if(format==79&&stencil==0xf0000000u)expected=(original>>8)|((original&15)<<28)|((original&0xf0)<<20);MUST(get(shadow+y*20+x*4)==expected);}for(unsigned x=12;x<20;x++)MUST(shadow[y*20+x]==gpu[y*20+x]);}
 memcpy(before,shadow,140);MUST(surface_client_depth_mask(&s)==0xffffffu);MUST(surface_depth_fill_value(&s,0)==0.0f&&surface_depth_fill_value(&s,0xffffffffu)==1.0f);MUST(fabs((double)surface_depth_fill_value(&s,0xaa345678u)-0x345678/(double)0xffffff)<0.0000001);MUST(emit_surface_upload(&s,NULL)&&!allocations&&!locks);MUST(captured.source_pitch==12&&captured.row_bytes==12&&captured.row_count==7);for(unsigned y=0;y<7;y++)MUST(!memcmp(uploaded+y*12,gpu+y*20,12));MUST(!memcmp(before,shadow,140));
 RECT area={1,1,3,6};MUST(emit_surface_upload(&s,&area)&&captured.x==1&&captured.y==1&&captured.width==2&&captured.row_count==5&&captured.source_pitch==8);for(unsigned y=0;y<5;y++)MUST(!memcmp(uploaded+y*8,gpu+(y+1)*20+4,8));MUST(!memcmp(before,shadow,140));
 unsigned old_uploads=uploads;fail_allocate=TRUE;MUST(!emit_surface_upload(&s,&area)&&uploads==old_uploads&&s.upload_pending&&!allocations&&!memcmp(before,shadow,140));fail_allocate=FALSE;s.upload_pending=FALSE;fail_upload=TRUE;MUST(!emit_surface_upload(&s,&area)&&s.upload_pending&&!allocations&&!memcmp(before,shadow,140));fail_upload=FALSE;
 memory.gpu_dirty=TRUE;fail_read=TRUE;MUST(!surface_readback(&s)&&memory.gpu_dirty&&!locks&&!memcmp(before,shadow,140));fail_read=FALSE;
 memory.gpu_dirty=FALSE;surface_mirror_depth_fill(&s,&area,0x345678,FALSE);for(unsigned y=0;y<7;y++)for(unsigned x=0;x<3;x++){DWORD expected=get(before+y*20+x*4);if(y>=1&&y<6&&x>=1)expected=(expected&0xff000000u)|0x345678u;MUST(get(shadow+y*20+x*4)==expected);}MUST(!memory.gpu_dirty);
}
static uint32_t reference(uint32_t word,uint32_t source,uint32_t destination){uint32_t output=0;unsigned source_bits[32],destination_bits[32],ns=0,nd=0;for(unsigned b=0;b<32;b++){if(source&(1u<<b))source_bits[ns++]=b;if(destination&(1u<<b))destination_bits[nd++]=b;}for(unsigned b=0;b<ns&&b<nd;b++)if(word&(1u<<source_bits[b]))output|=1u<<destination_bits[b];return output;}
static void packing(void){const uint32_t masks[][4]={{0xffffff00,0,0x00ffffff,0},{0xffffff00,255,0x00ffffff,0xff000000},{0xffffff00,15,0x00ffffff,0x0f000000},{0xfffe,1,0x7fff,0x8000},{0xffffffff,0,0xffffffff,0},{0x00ff00ff,0xff00ff00,0x0f0f0f0f,0xf0f0f0f0}};uint32_t state=0x23ab7891;for(unsigned m=0;m<6;m++)for(unsigned i=0;i<4096;i++){state=state*1664525u+1013904223u;const uint32_t*p=masks[m];uint32_t expected=reference(state,p[0],p[2])|reference(state,p[1],p[3])|reference(state,~(p[0]|p[1]),~(p[2]|p[3]));uint32_t converted=ddraw_depth_repack_word(state,p[0],p[1],p[2],p[3]);MUST(converted==expected);MUST(ddraw_depth_repack_word(converted,p[2],p[3],p[0],p[1])==state);}MUST(ddraw_depth_repack_word(0x599999ab,0xffffff00,255,0xffffff,0xff000000)==0xab599999);}
static void precision(void){const DWORD masks[]={0,0xffffu,0xffffffu,0xffffff00u,0xffffffffu};for(unsigned stencil=0;stencil<2;stencil++)for(unsigned i=0;i<5;i++){DDPIXELFORMAT pf={.dwSize=sizeof(pf),.dwFlags=DDPF_ZBUFFER|(stencil?DDPF_STENCILBUFFER:0),.dwZBufferBitDepth=32,.dwZBitMask=masks[i]},before=pf;DDrawPixelFormatDesc out;describe_pixel_format(&pf,&out);MUST(out.z_bit_depth==(!stencil&&masks[i]==0xffffff?24u:32u));MUST(out.green_mask==masks[i]&&out.bit_count==32&&!memcmp(&before,&pf,sizeof(pf)));}}
int main(void){(void)&heap_free;packing();precision();transfer(77,0xffffff,0);transfer(77,0,0);transfer(75,0xffffff,0xff000000);transfer(79,0xffffff,0x0f000000);transfer(79,0xffffff,0xf0000000);printf("PASS actual DDraw depth transfer C: %u checks; chunked readback, full/partial uploads, client shadow/stencil/padding preservation, exact depth fills and failure cleanup\n",checks);return 0;}
`;
try{const c=prefix.replace('DEPTH_HEADER',(process.env.DDRAW_DEPTH_LAYOUT_INCLUDE_ROOT||path.dirname(file))+'/ddraw_depth_layout.h').replace('FORMAT_HEADER',(process.env.DDRAW_DEPTH_LAYOUT_INCLUDE_ROOT||path.dirname(file))+'/../runtime/d9wg_format_layouts.h')+['desc1_to_desc2','describe_pixel_format','format_compressed_block_bytes','format_is_compressed','format_is_yuv','surface_depth_mask','surface_depth_transfer_masks','surface_client_depth_mask','surface_depth_fill_value','surface_repack_depth_rows','emit_surface_upload','surface_readback','surface_mirror_depth_fill'].map(extract).join('\n')+tests;fs.writeFileSync(dir+'/test.c',c);const built=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(built.status,0,built.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
