"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-texture-copy-"));
function extract(source,name){const m=new RegExp("static (?:BOOL|HRESULT)(?: WINAPI)? "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
try{for(const api of [8,9]){
 const source=fs.readFileSync(process.env.D9WG_TEXTURE_COPY_API===String(api)?process.env.D9WG_TEXTURE_COPY_FRONTEND_SOURCE:root+"/d3d"+api+"proxy/d3d"+api+"_proxy.c","utf8"),adapter=source.match(new RegExp("#define D9WG_COPY_LEVEL D"+api+"TextureLevel[\\s\\S]*?#include \\\"\\.\\./runtime/d9wg_texture_copy_bridge.h\\\""));assert(adapter,"actual SDK adapter");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/d9wg_runtime.h"
typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;typedef int BOOL,HRESULT;
#define WINAPI
#define API ${api}
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D8WG_TRACE_ERROR(x) (x)
#define TRACE_REFUSE(x) (x)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SYSTEMMEM 2
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct{int32_t left,top,right,bottom;}RECT;
typedef struct{UINT Left,Top,Right,Bottom,Front,Back;}D3DBOX;
static int SetRect(RECT*r,int l,int t,int rr,int b){*r=(RECT){l,t,rr,b};return 1;}
static unsigned allocations,frees,attempts,fail_alloc;
static void *HeapAlloc(void*h,UINT flags,size_t n){(void)h;if(++attempts==fail_alloc)return NULL;void*p=flags?calloc(1,n):malloc(n);if(p)allocations++;return p;}
typedef struct{const void *lpVtbl;}Iface;
typedef struct{unsigned marker;}Device;
typedef struct Level{UINT guard_a;BOOL locked;UINT depth;BYTE *shadow;UINT byte_count,width,height,row_pitch,row_count,slice_pitch;BOOL shadow_valid,gpu_dirty;UINT guard_b;}Level;
typedef struct Texture{Iface iface;Device *device;UINT handle,width,height,depth,edge_length,level_count,format,pool;Level *levels;}Texture;
typedef Device D${api}Device,IDirect3DDevice${api};typedef Level D${api}TextureLevel;
typedef Texture D${api}Texture,D${api}CubeTexture,D${api}VolumeTexture,IDirect3DCubeTexture${api},IDirect3DVolumeTexture${api};typedef Iface IDirect3DBaseTexture${api},IDirect3DBaseTexture${api}Vtbl;
static const int g_texture_vtbl=1,${api===8?"g_cube_texture_vtbl":"g_cube_vtbl"}=2,g_volume_texture_vtbl=3;
#define G_CUBE_VTBL ${api===8?"g_cube_texture_vtbl":"g_cube_vtbl"}
static Device *device_from_iface(Device*p){return p;}
static Texture *current_source,*current_destination;static UINT current_kind,skip,copied;static unsigned emits,fail_emit,event_error;
static struct{UINT layer,level,size;BYTE *data;}events[192];
static int emit_level(Texture*t,UINT kind,UINT layer,UINT level,const RECT*rect,UINT front,UINT depth){
 if(t!=current_destination||kind!=current_kind||!copied||layer!=emits/copied||level!=emits%copied||emits>=192){event_error=__LINE__;return 0;}
 Level *to=t->levels+layer*t->level_count+level;if(rect->left||rect->top||rect->right!=(int)to->width||rect->bottom!=(int)to->height||front||depth!=(kind==2?to->depth:1)){event_error=__LINE__;return 0;}
 events[emits].layer=layer;events[emits].level=level;events[emits].size=to->byte_count;events[emits].data=malloc(to->byte_count);if(!events[emits].data)abort();memcpy(events[emits].data,to->shadow,to->byte_count);++emits;return emits!=fail_emit;
}
static BOOL emit_texture_update(Texture*t,UINT level,const RECT*r){return emit_level(t,0,0,level,r,0,1);}
${api===8?`static BOOL emit_level_update(UINT handle,UINT format,Level*level,UINT mip,UINT face,const RECT*r){if(handle!=current_destination->handle||format!=current_destination->format||level!=current_destination->levels+face*current_destination->level_count+mip){event_error=__LINE__;return 0;}return emit_level(current_destination,1,face,mip,r,0,1);}
static BOOL emit_volume_level_update(Texture*t,UINT level,const RECT*r,UINT front,UINT depth){return emit_level(t,2,0,level,r,front,depth);}
${extract(source,"d8_copy_volume_upload")}
${extract(source,"d8_copy_upload_result")}`:`static BOOL emit_cube_texture_update(Texture*t,UINT face,UINT level,const RECT*r){return emit_level(t,1,face,level,r,0,1);}
static BOOL emit_volume_texture_update(Texture*t,UINT level,const D3DBOX*b){RECT r={(int)b->Left,(int)b->Top,(int)b->Right,(int)b->Bottom};return emit_level(t,2,0,level,&r,b->Front,b->Back-b->Front);}`}
${adapter[0].replace('../runtime/d9wg_texture_copy_bridge.h',process.env.D9WG_TEXTURE_COPY_BRIDGE_HEADER||'runtime/d9wg_texture_copy_bridge.h')}
${(api===8?["update_cube_texture","update_volume_texture","device_update_texture"]:["device_update_texture"]).map(n=>extract(source,n)).join("\n")}
static HRESULT call_update(Device*d,Texture*s,Texture*t){return device_update_texture(d,s?&s->iface:NULL,t?&t->iface:NULL);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"texture copy SDK line %u API=${api}: %s event=%u\\n",__LINE__,#v,event_error);return 1;}}while(0)
#include "tests/native/d9wg_texture_copy_cases.h"
`;
 fs.writeFileSync(dir+"/copy.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,"-I",root+"/runtime",dir+"/copy.c",process.env.D9WG_TEXTURE_COPY_RUNTIME_SOURCE||root+"/runtime/d9wg_runtime.c","-o",dir+"/copy"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/copy",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}}finally{fs.rmSync(dir,{recursive:true,force:true});}
