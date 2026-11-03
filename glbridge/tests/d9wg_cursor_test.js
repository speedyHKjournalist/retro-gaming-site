"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-cursor-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_CURSOR_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),header=process.env.D9WG_CURSOR_HEADER||root+'/runtime/d9wg_cursor_bridge.h';fs.writeFileSync(dir+'/cursor.c',`#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_cursor.h"
typedef uint32_t UINT,DWORD;typedef int BOOL,WINBOOL,HRESULT;typedef uint8_t BYTE;typedef uintptr_t HWND;typedef struct{int32_t x,y;}POINT;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D9WG_READBACK_INVALID 1
#define D3DFMT_A8R8G8B8 21
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define FAILED(x) ((x)<0)
#define D8WG_TRACE_ERROR(x) (x)
#define TRACE_REFUSE(x) (x)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
typedef struct{const void*lpVtbl;}Iface;typedef Iface IDirect3DSurface${api};
typedef struct{UINT Width,Height;}D3DDISPLAYMODE;
typedef struct{UINT Format,Width,Height,Pool,Usage,Size;}D3DSURFACE_DESC;
typedef struct{Iface iface;}Parent;
typedef struct Device{UINT handle;Parent*parent;struct{UINT AdapterOrdinal;HWND hFocusWindow;}creation;struct{HWND hDeviceWindow;}present;D9WGCursorState app_cursor_state;BOOL cursor_ready,app_cursor,cursor_visible;}Device;
typedef Device D${api}Device,IDirect3DDevice${api};
typedef struct{BYTE*shadow;UINT row_pitch,byte_count;BOOL locked,gpu_dirty,shadow_valid;}Level;
typedef Level D${api}TextureLevel;
typedef struct{Device*device;UINT handle,pool,usage,level_count;Level*levels;}Texture;
typedef struct{Iface iface;UINT refs;Device*device;Texture*texture,*cube,*cube_texture;UINT level,face,cube_face,width,height,format,pool,row_pitch,byte_count;BYTE*shadow;BOOL locked,backbuffer;void*swap_chain;D3DSURFACE_DESC desc;}Surface;
typedef Surface D${api}Surface;
typedef struct{int held;}Lock;static Lock g_transport_lock;
static unsigned g_surface_vtbl,allocations,fail_alloc,fail_reserve,fail_emit,read_error,reads,uploads,shows,positions,warps,get_positions,fail_get,fail_warp,strict_flushes,last_extra;
static POINT os_position={100,120};static HWND converted_window;
static uint8_t batch[3<<20],pixels[3<<20],gpu_pixels[3<<20];
static D9WGSetCursorPosition position;static D9WGShowCursor visibility;static D9WGReadbackSurface read_request;
static void*g_process=(void*)3;
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,UINT flags,size_t size){(void)h;assert(!flags);if(fail_alloc)return NULL;void*p=malloc(size);if(p)allocations++;return p;}
static void HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;assert(p&&allocations);allocations--;free(p);}
static void EnterCriticalSection(Lock*l){l->held++;}static void LeaveCriticalSection(Lock*l){assert(l->held);l->held--;}
static uint8_t*batch_base(void){return batch;}
static BOOL reserve_command_locked(uint16_t op,uint32_t n,uint32_t extra,void*command,uint8_t**payload,uint8_t**blob){(void)command;assert(g_transport_lock.held&&op==D9WG_OP_SET_CURSOR_PROPERTIES&&n==sizeof(D9WGSetCursorProperties)&&extra+128<=sizeof(batch));if(fail_reserve)return FALSE;*payload=batch+16;*blob=batch+128;last_extra=extra;memset(batch+128+extra,0xd3,16);uploads++;return TRUE;}
static int guard_ok(void){for(unsigned i=0;i<16;i++)if(batch[128+last_extra+i]!=0xd3)return 0;return 1;}
static BOOL emit_command(uint16_t op,const void*p,uint32_t bytes){if(op==D9WG_OP_SET_CURSOR_POSITION){assert(bytes==sizeof(position));memcpy(&position,p,bytes);positions++;}else{assert(op==D9WG_OP_SHOW_CURSOR&&bytes==sizeof(visibility));memcpy(&visibility,p,bytes);shows++;}return !fail_emit;}
static BOOL GetCursorPos(POINT*p){get_positions++;if(fail_get)return FALSE;*p=os_position;return TRUE;}
static BOOL SetCursorPos(int32_t x,int32_t y){warps++;if(fail_warp)return FALSE;os_position=(POINT){x,y};return TRUE;}
static BOOL ScreenToClient(HWND window,POINT*p){converted_window=window;p->x-=window==1?30:70;p->y-=window==1?40:80;return TRUE;}
static BOOL submit_readback_batch_locked(BOOL present){assert(!present);strict_flushes++;return TRUE;}
typedef struct{UINT unused;}D9WGReadbackDiagnostic;
static int d9wg_surface_readback(void*process,D9WGReadbackSurface*r,uint32_t row_bytes,uint32_t rows,void*destination,uint32_t capacity,BOOL(*flush)(BOOL),D9WGReadbackDiagnostic*detail){(void)detail;assert(process==g_process&&flush==submit_readback_batch_locked&&!g_transport_lock.held&&row_bytes==r->width*4&&rows==r->height&&capacity==row_bytes*rows);reads++;read_request=*r;assert(flush(FALSE));if(read_error)return read_error;memcpy(destination,gpu_pixels,capacity);return 0;}
static Device*device_from_iface(Device*d){return d;}
static Surface*surface_from_iface(Iface*i){return (Surface*)i;}
static Level*surface_texture_level(Surface*s){Texture*t=s->texture?s->texture:s->cube?s->cube:s->cube_texture;UINT face=s->cube?s->face:s->cube_texture?s->cube_face:0;return t?t->levels+face*t->level_count+s->level:NULL;}
static HRESULT d3d_get_adapter_display_mode(Iface*parent,UINT adapter,D3DDISPLAYMODE*mode){assert(parent);if(adapter)return -1;*mode=(D3DDISPLAYMODE){${api===8?1024:800},${api===8?768:600}};return 0;}
${api===8?'static Device*surface_device(Surface*s){return s->texture?s->texture->device:s->cube?s->cube->device:s->device;}\nstatic HRESULT surface_get_desc(Iface*i,D3DSURFACE_DESC*out){*out=((Surface*)i)->desc;return 0;}\nstatic BYTE*surface_pixels(Surface*s,UINT*pitch){Level*l=surface_texture_level(s);*pitch=l?l->row_pitch:s->row_pitch;return l?l->shadow:s->shadow;}\nstatic BOOL surface_gpu_location(Surface*s,uint32_t*handle,uint32_t*level,uint32_t*face){*handle=s->texture?s->texture->handle:s->cube?s->cube->handle:0;*level=s->level;*face=s->cube?s->face:0;return TRUE;}':''}
#include "${header}"
${['device_set_cursor_properties','device_set_cursor_position','device_show_cursor'].map(n=>extract(source,n)).join('\n')}
static HRESULT image(Device*d,Surface*s,UINT x,UINT y){return device_set_cursor_properties(d,x,y,s?&s->iface:NULL);}
static void shape(Surface*s,UINT width,UINT height,UINT pitch,UINT size){s->width=width;s->height=height;s->row_pitch=pitch;s->byte_count=size;s->desc=(D3DSURFACE_DESC){21,width,height,s->pool,s->texture?s->texture->usage:0,size};Level*l=surface_texture_level(s);if(l){l->row_pitch=pitch;l->byte_count=size;}}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Cursor API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){Parent parent={0};Device d={.handle=7,.parent=&parent,.creation={0,1}},foreign={.handle=9,.parent=&parent,.creation={0,1}};Level levels[12];for(unsigned i=0;i<12;i++)levels[i]=(Level){pixels,140,4468,0,0,1};Texture t={.device=&foreign,.handle=41,.pool=1,.level_count=2,.levels=levels};Surface s={.iface={&g_surface_vtbl},.refs=19,.device=&foreign,.texture=&t,.format=21,.pool=1};shape(&s,32,32,140,4468);for(unsigned i=0;i<sizeof(pixels);i++){pixels[i]=(BYTE)(i*17+i/140);gpu_pixels[i]=(BYTE)(i*3+71);}unsigned cases=0;
 CHECK(!device_show_cursor(&d,TRUE)&&!device_show_cursor(&d,TRUE)&&!shows&&!d.app_cursor_state.visible);cases++;
 device_set_cursor_position(&d,30,40,0x55);CHECK(!warps&&position.device_handle==7&&position.x==0&&position.y==0&&position.flags==0x55);cases++;
 CHECK(!image(&d,&s,0xffffffffu,0x80000000u)&&uploads==1&&shows==1&&guard_ok()&&d.app_cursor_state.ready&&d.app_cursor_state.hardware&&!d.app_cursor_state.visible&&!allocations&&!g_transport_lock.held);D9WGSetCursorProperties*c=(void*)(batch+16);CHECK(c->device_handle==7&&c->hotspot_x==0xffffffffu&&c->hotspot_y==0x80000000u&&c->width==32&&c->height==32&&c->data_bytes==4096&&c->data_offset==128);for(UINT y=0;y<32;y++)CHECK(!memcmp(batch+128+y*128,pixels+y*140,128));CHECK(s.refs==19&&t.device==&foreign&&visibility.show==0&&visibility.device_handle==7);cases++;
 CHECK(!device_show_cursor(&d,TRUE)&&device_show_cursor(&d,TRUE)&&d.app_cursor_state.visible&&visibility.show==1&&position.x==70&&position.y==80);CHECK(device_show_cursor(&d,FALSE)&&!device_show_cursor(&d,FALSE)&&!d.app_cursor_state.visible);cases++;
 d.present.hDeviceWindow=2;device_set_cursor_position(&d,(UINT)-5,10,1);CHECK(warps==1&&os_position.x==-5&&os_position.y==10&&converted_window==2&&position.x==-75&&position.y==-70&&position.flags==1);cases++;
 CHECK(!device_show_cursor(&d,TRUE));os_position=(POINT){200,220};d9wg_cursor_present(d.handle,2,&d.app_cursor_state);CHECK(position.x==130&&position.y==140);cases++;
 fail_warp=1;device_set_cursor_position(&d,1,2,0);CHECK(!d.app_cursor_state.hardware&&position.x==-69&&position.y==-78);fail_warp=0;os_position=(POINT){400,420};unsigned calls=get_positions;d9wg_cursor_present(d.handle,2,&d.app_cursor_state);CHECK(get_positions==calls&&position.x==-69&&position.y==-78);cases++;
 shape(&s,2,4,12,44);CHECK(!image(&d,&s,0,0)&&!d.app_cursor_state.hardware&&d.app_cursor_state.visible);unsigned before=warps;device_set_cursor_position(&d,90,110,7);CHECK(warps==before&&position.x==20&&position.y==30&&position.flags==7);calls=get_positions;os_position=(POINT){800,820};d9wg_cursor_present(d.handle,2,&d.app_cursor_state);CHECK(calls==get_positions&&position.x==20);cases++;
 CHECK(device_show_cursor(&d,FALSE));fail_get=1;CHECK(!device_show_cursor(&d,TRUE)&&position.x==20&&position.y==30);fail_get=0;cases++;
 /* Every validation below precedes pixel/transport or visible-state effects. */
 for(unsigned n=0;n<12;n++){Surface bad=s;Level l=*levels;Texture tex=t;tex.levels=&l;bad.texture=&tex;unsigned old_uploads=uploads,old_shows=shows,old_reads=reads;D9WGCursorState old=d.app_cursor_state;if(n==0)bad.iface.lpVtbl=NULL;if(n==1)bad.format=bad.desc.Format=22;if(n==2)shape(&bad,3,4,12,48);if(n==3)shape(&bad,2,3,8,24);if(n==4)shape(&bad,0,4,0,0);if(n==5)shape(&bad,2048,4,8192,32768);if(n==6)shape(&bad,2,1024,8,8192);if(n==7)l.row_pitch=bad.row_pitch=4;if(n==8)l.byte_count=bad.byte_count=43,bad.desc.Size=43;if(n==9)l.shadow=NULL,bad.shadow=NULL;if(n==10)bad.locked=1;if(n==11)l.locked=1;CHECK(image(&d,&bad,0,0)==-1&&uploads==old_uploads&&shows==old_shows&&reads==old_reads&&!allocations&&!g_transport_lock.held&&!memcmp(&old,&d.app_cursor_state,sizeof(old)));cases++;}
 CHECK(image(&d,NULL,0,0)==-1);cases++;
 /* GPU-backed foreign cube face/level uses actual pixels and source device. */
 s.texture=NULL;${api===8?'s.cube=&t;s.face=4;':'s.cube_texture=&t;s.cube_face=4;'}s.level=1;s.pool=t.pool=0;t.usage=1;shape(&s,32,32,140,4468);levels[9].gpu_dirty=1;levels[9].shadow_valid=0;CHECK(!image(&d,&s,3,5)&&reads==1&&strict_flushes==1&&!allocations&&read_request.device_handle==9&&read_request.texture_handle==41&&read_request.level==1&&read_request.face==4&&read_request.width==32&&read_request.height==32&&!memcmp(batch+128,gpu_pixels,4096)&&guard_ok());cases++;
 before=uploads;fail_alloc=1;CHECK(image(&d,&s,0,0)==-3&&uploads==before&&!allocations);fail_alloc=0;cases++;
 for(unsigned error=1;error<=2;error++){read_error=error;CHECK(image(&d,&s,0,0)==(error==1?-1:-2)&&uploads==before&&!allocations&&!g_transport_lock.held);cases++;}read_error=0;
 fail_reserve=1;CHECK(image(&d,&s,0,0)==-2&&uploads==before&&!allocations&&!g_transport_lock.held);fail_reserve=0;cases++;
 /* A new image does not require retaining the app's source surface. */
 s.refs=0;CHECK(!image(&d,&s,0,0)&&!s.refs);cases++;
 /* Max accepted power-of-two cursor fits the adapter and one batch. */
 s.texture=&t;${api===8?'s.cube=NULL;':'s.cube_texture=NULL;'}s.level=0;s.pool=t.pool=1;t.usage=0;UINT max_width=${api===8?1024:512};shape(&s,max_width,512,max_width*4,max_width*512*4);CHECK(!image(&d,&s,0,0)&&!memcmp(batch+128,pixels,max_width*512*4)&&guard_ok());cases++;
 Device fresh={.handle=11,.parent=&parent,.creation={0,1}};shape(&s,32,32,128,4096);fail_reserve=1;CHECK(image(&fresh,&s,0,0)==-2&&!fresh.app_cursor_state.ready);fail_reserve=0;cases++;fail_emit=1;CHECK(image(&fresh,&s,0,0)==-2&&fresh.app_cursor_state.ready&&!fresh.app_cursor_state.visible);fail_emit=0;CHECK(!device_show_cursor(&fresh,TRUE)&&device_show_cursor(&fresh,FALSE));cases++;
 CHECK(!allocations&&!g_transport_lock.held);printf("PASS actual API ${api} cursor: %u image/shape/format/pool/lock, padded/large rows, source independence, real-source readback identity/failures, Show/position/warp/fallback and accepted-prefix cases\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',root,'-I',root+'/runtime',dir+'/cursor.c','-o',dir+'/cursor'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/cursor',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
