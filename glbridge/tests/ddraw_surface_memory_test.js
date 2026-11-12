"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_MEMORY_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-surface-memory-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
const definitions=fs.readFileSync(__dirname+"/ddraw_color_key_test.js","utf8").split("typedef uint32_t DWORD,UINT;")[1].split("typedef struct DDSurface DDSurface;")[0];
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT;${definitions}
typedef unsigned char BYTE;typedef uint16_t WORD;typedef int32_t LONG;typedef size_t SIZE_T;
#define DD_VERSION_7 7u
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define InterlockedIncrement(p) (++*(p))
#define InterlockedDecrement(p) (--*(p))
typedef struct DDSurface DDSurface;
typedef struct{DDSurface*surfaces;unsigned children;}DDrawObject;
typedef struct{LONG ref;DWORD handle;}DDTextureStorage;
typedef struct{LONG ref;BYTE*bits;SIZE_T bytes;BOOL owned,gpu_dirty;}DDSurfaceMemory;
struct DDSurface{const int*vtbl7,*vtbl4,*vtbl3,*vtbl2,*vtbl1,*texture2_vtbl,*texture1_vtbl,*gamma_vtbl;LONG ref,public_refs[8];unsigned creation_version;DDrawObject*owner;struct{uint16_t red[256],green[256],blue[256];}gamma_ramp;DDTextureStorage*storage;DDSurfaceMemory*memory;BYTE*shadow;BOOL external_memory,indexed,primary,implicit;DWORD handle,overlay_id,mip_level,cube_face,format,width,height,bytes_per_pixel,pitch;DDSURFACEDESC2 desc;DDCOLORKEY color_key[4];DWORD color_key_present;DDSurface*next;};
typedef DDSurface IDirectDrawSurface7;
static const int g_surface_vtbl=7,g_surface_vtbl4=4,g_surface_vtbl3=3,g_surface_vtbl2=2,g_surface_vtbl1=1,g_d3d_texture2_vtbl=12,g_d3d_texture1_vtbl=11,g_gamma_vtbl=20;
static int g_object_lock;static unsigned checks,allocs,calls,fail_call,serial=100;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"surface memory line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static void*heap_alloc_zero(size_t n){calls++;if(calls==fail_call)return NULL;void*p=calloc(1,n);MUST(p);allocs++;return p;}
static void heap_free(void*p){if(p){MUST(allocs);allocs--;free(p);}}
static LONG InterlockedCompareExchange(LONG*p,LONG n,LONG expected){LONG old=*p;if(old==expected)*p=n;return old;}
static void EnterCriticalSection(int*p){MUST(!(*p)++);}static void LeaveCriticalSection(int*p){MUST((*p)--==1);}
static void ddraw_child_attach(DDrawObject*d,BOOL public_ref){MUST(!public_ref);d->children++;}
static UINT format_bytes_per_pixel(UINT f){return f==0x31545844u?0:4;}
static UINT format_compressed_block_bytes(UINT f){return f==0x31545844u?8:0;}
static BOOL ddraw_format_is_palettised(UINT f){(void)f;return FALSE;}
static UINT surface_pitch_for(UINT w,UINT bytes){return(w*bytes+7u)&~7u;}
static void mode_pixel_format(UINT bpp,DDPIXELFORMAT*p){*p=(DDPIXELFORMAT){.dwSize=sizeof(*p),.dwRGBBitCount=bpp};}
static DWORD allocate_handle(void){return ++serial;}
static DDTextureStorage*surface_storage_allocate(void){DDTextureStorage*p=heap_alloc_zero(sizeof(*p));if(p){p->ref=1;p->handle=allocate_handle();}return p;}
static DDSurface*surface_from_iface(DDSurface*p){return p;}
${["surface_memory_create","surface_memory_release","surface_allocate","surface_GetSurfaceDesc"].map(extract).join("\n")}
static void destroy(DDSurface*s){MUST(s->owner->surfaces==s&&s->owner->children);s->owner->surfaces=s->next;s->owner->children--;surface_memory_release(s->memory);if(!--s->storage->ref)heap_free(s->storage);heap_free(s);}
static void cases(void){for(unsigned round=0;round<64;round++)for(unsigned external=0;external<2;external++)for(unsigned compressed=0;compressed<2;compressed++){BYTE supplied[256];memset(supplied,0x72,sizeof(supplied));DDrawObject owner={0};unsigned fmt=compressed?0x31545844u:22;DDSURFACEDESC2 r={.dwSize=sizeof(r),.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_CAPS|DDSD_PIXELFORMAT|DDSD_LINEARSIZE|(external?DDSD_LPSURFACE|DDSD_PITCH:0),.dwWidth=3,.dwHeight=4,.dwBackBufferCount=0xdead,.lPitch=external?20:100,.lpSurface=supplied,.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT),.dwRGBBitCount=32}};DDSURFACEDESC2 before=r;calls=0;fail_call=0;DDSurface*s=surface_allocate(&owner,&r,fmt,3,4,DDSCAPS_SYSTEMMEMORY,NULL,0,0);MUST(s&&s->external_memory==(BOOL)external&&s->memory->owned==!(BOOL)external&&s->memory->ref==1&&s->storage->ref==1&&s->owner==&owner&&owner.children==1);UINT pitch=external?20:compressed?8:16,rows=compressed?1:4;MUST(s->pitch==pitch&&s->memory->bytes==pitch*rows&&s->shadow==s->memory->bits);MUST(!memcmp(&r,&before,sizeof(r))&&s->desc.lpSurface==NULL&&!(s->desc.dwFlags&DDSD_LPSURFACE)&&!s->desc.dwBackBufferCount);MUST((s->desc.dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))==(compressed?DDSD_LINEARSIZE:DDSD_PITCH));if(compressed)MUST(s->desc.dwLinearSize==pitch*rows);else MUST(s->desc.lPitch==(int32_t)pitch);if(external){MUST(s->shadow==supplied);for(unsigned i=0;i<sizeof(supplied);i++)MUST(supplied[i]==0x72);s->shadow[pitch-1]=0xa5;MUST(supplied[pitch-1]==0xa5);}else{MUST(s->shadow!=supplied);for(unsigned i=0;i<pitch*rows;i++)MUST(!s->shadow[i]);}DDSURFACEDESC2 got={.dwSize=sizeof(got)};MUST(!surface_GetSurfaceDesc(s,&got)&&got.lpSurface==NULL&&!(got.dwFlags&DDSD_LPSURFACE));destroy(s);MUST(!owner.surfaces&&!owner.children&&!allocs);}}
static void failures(void){for(unsigned external=0;external<2;external++){DDrawObject owner={0};BYTE bits[64];memset(bits,0x39,sizeof(bits));DDSURFACEDESC2 r={.dwFlags=DDSD_PIXELFORMAT|(external?DDSD_LPSURFACE|DDSD_PITCH:0),.lPitch=16,.lpSurface=bits,.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT)}};for(unsigned step=1;step<=(external?3:4);step++){calls=0;fail_call=step;MUST(!surface_allocate(&owner,&r,22,4,4,DDSCAPS_SYSTEMMEMORY,NULL,0,0));MUST(!owner.surfaces&&!owner.children&&!allocs);for(unsigned i=0;i<64;i++)MUST(bits[i]==0x39);}}}
int main(void){(void)&InterlockedCompareExchange;cases();failures();MUST(!g_object_lock);printf("PASS actual surface allocation C: %u checks, owned and borrowed RGB/compressed storage, caller pitch/bytes/aliasing, untouched caller memory, hidden input-only metadata, and all owned/borrowed allocation failure unwinds\\n",checks);return 0;}
`;
// Model the DDSURFACEDESC pitch/linear-size union exactly for allocation checks.
const program=code.replace("int32_t lPitch;DWORD dwSize", "union{int32_t lPitch;DWORD dwLinearSize;};DWORD dwSize");
try{const file=dir+"/memory.c",binary=dir+"/memory";fs.writeFileSync(file,program);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
