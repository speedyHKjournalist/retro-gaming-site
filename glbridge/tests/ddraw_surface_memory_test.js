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
#define DDSCAPS2_MIPMAPSUBLEVEL 0x10000u
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define InterlockedIncrement(p) (++*(p))
#define InterlockedDecrement(p) (--*(p))
typedef struct DDSurface DDSurface;
typedef struct{DDSurface*surfaces;unsigned children;}DDrawObject;
typedef struct{LONG ref;DWORD handle;}DDTextureStorage;
typedef struct{LONG ref;BYTE*bits;SIZE_T bytes;BOOL owned,gpu_dirty;DWORD content_version;}DDSurfaceMemory;
struct DDSurface{const int*vtbl7,*vtbl4,*vtbl3,*vtbl2,*vtbl1,*texture2_vtbl,*texture1_vtbl,*gamma_vtbl;LONG ref,public_refs[8];unsigned creation_version;DDrawObject*owner;struct{uint16_t red[256],green[256],blue[256];}gamma_ramp;DDTextureStorage*storage;DDSurfaceMemory*memory;BYTE*shadow;BOOL external_memory,indexed,primary,implicit;DWORD handle,overlay_id,mip_level,cube_face,format,width,height,bytes_per_pixel,pitch;DDSURFACEDESC2 desc;DDCOLORKEY color_key[4];DWORD color_key_present;DDSurface*next,*mip_next;};
typedef DDSurface IDirectDrawSurface7;
static const int g_surface_vtbl=7,g_surface_vtbl4=4,g_surface_vtbl3=3,g_surface_vtbl2=2,g_surface_vtbl1=1,g_d3d_texture2_vtbl=12,g_d3d_texture1_vtbl=11,g_gamma_vtbl=20;
static int g_object_lock;static unsigned checks,allocs,calls,fail_call,serial=100;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"surface memory line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static void*heap_alloc_zero(size_t n){calls++;if(calls==fail_call)return NULL;void*p=calloc(1,n);MUST(p);allocs++;return p;}
static void heap_free(void*p){if(p){MUST(allocs);allocs--;free(p);}}
static LONG InterlockedCompareExchange(LONG*p,LONG n,LONG expected){LONG old=*p;if(old==expected)*p=n;return old;}
static void EnterCriticalSection(int*p){MUST(!(*p)++);}static void LeaveCriticalSection(int*p){MUST((*p)--==1);}
static void ddraw_child_attach(DDrawObject*d,BOOL public_ref){MUST(!public_ref);d->children++;}
static UINT format_compressed_block_bytes(UINT f){return f==0x31545844u?8:f==0x35545844u?16:0;}
static UINT format_bytes_per_pixel(UINT f){return format_compressed_block_bytes(f)?0:4;}
static BOOL ddraw_format_is_palettised(UINT f){(void)f;return FALSE;}
static UINT surface_pitch_for(UINT w,UINT bytes){return(w*bytes+7u)&~7u;}
static void mode_pixel_format(UINT bpp,DDPIXELFORMAT*p){*p=(DDPIXELFORMAT){.dwSize=sizeof(*p),.dwRGBBitCount=bpp};}
static DWORD allocate_handle(void){return ++serial;}
static DDTextureStorage*surface_storage_allocate(void){DDTextureStorage*p=heap_alloc_zero(sizeof(*p));if(p){p->ref=1;p->handle=allocate_handle();}return p;}
static DDSurface*surface_from_iface(DDSurface*p){return p;}
${["surface_memory_create","surface_memory_release","surface_allocate","surface_build_mip_chain","surface_GetSurfaceDesc"].map(extract).join("\n")}
static void destroy(DDSurface*s){MUST(s->owner->surfaces==s&&s->owner->children);s->owner->surfaces=s->next;s->owner->children--;surface_memory_release(s->memory);if(!--s->storage->ref)heap_free(s->storage);heap_free(s);}
static void cases(void){for(unsigned round=0;round<64;round++)for(unsigned external=0;external<2;external++)for(unsigned compressed=0;compressed<2;compressed++){BYTE supplied[256];memset(supplied,0x72,sizeof(supplied));DDrawObject owner={0};unsigned fmt=compressed?0x31545844u:22;DDSURFACEDESC2 r={.dwSize=sizeof(r),.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_CAPS|DDSD_PIXELFORMAT|DDSD_LINEARSIZE|(external?DDSD_LPSURFACE|DDSD_PITCH:0),.dwWidth=3,.dwHeight=4,.dwBackBufferCount=0xdead,.lPitch=external?20:100,.lpSurface=supplied,.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT),.dwRGBBitCount=32}};DDSURFACEDESC2 before=r;calls=0;fail_call=0;DDSurface*s=surface_allocate(&owner,&r,fmt,3,4,DDSCAPS_SYSTEMMEMORY,NULL,0,0);MUST(s&&s->external_memory==(BOOL)external&&s->memory->owned==!(BOOL)external&&s->memory->ref==1&&s->storage->ref==1&&s->owner==&owner&&owner.children==1);UINT pitch=compressed?8:external?20:16,rows=compressed?1:4;MUST(s->pitch==pitch&&s->memory->bytes==pitch*rows&&s->shadow==s->memory->bits);MUST(!memcmp(&r,&before,sizeof(r))&&s->desc.lpSurface==NULL&&!(s->desc.dwFlags&DDSD_LPSURFACE)&&!s->desc.dwBackBufferCount);MUST((s->desc.dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))==(compressed?DDSD_LINEARSIZE:DDSD_PITCH));if(compressed)MUST(s->desc.dwLinearSize==(external?~0u:pitch*rows));else MUST(s->desc.lPitch==(int32_t)pitch);if(external){MUST(s->shadow==supplied);for(unsigned i=0;i<sizeof(supplied);i++)MUST(supplied[i]==0x72);s->shadow[pitch-1]=0xa5;MUST(supplied[pitch-1]==0xa5);}else{MUST(s->shadow!=supplied);for(unsigned i=0;i<pitch*rows;i++)MUST(!s->shadow[i]);}DDSURFACEDESC2 got={.dwSize=sizeof(got)};MUST(!surface_GetSurfaceDesc(s,&got)&&got.lpSurface==NULL&&!(got.dwFlags&DDSD_LPSURFACE));destroy(s);MUST(!owner.surfaces&&!owner.children&&!allocs);}}
static void failures(void){for(unsigned external=0;external<2;external++){DDrawObject owner={0};BYTE bits[64];memset(bits,0x39,sizeof(bits));DDSURFACEDESC2 r={.dwFlags=DDSD_PIXELFORMAT|(external?DDSD_LPSURFACE|DDSD_PITCH:0),.lPitch=16,.lpSurface=bits,.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT)}};for(unsigned step=1;step<=(external?3:4);step++){calls=0;fail_call=step;MUST(!surface_allocate(&owner,&r,22,4,4,DDSCAPS_SYSTEMMEMORY,NULL,0,0));MUST(!owner.surfaces&&!owner.children&&!allocs);for(unsigned i=0;i<64;i++)MUST(bits[i]==0x39);}}}
// Exercise physical compressed storage independently of requested-root validation:
// all sub-block mip tails are legal; input pitch never changes block addresses.
static void block_layouts(void){
 fail_call=0;
 for(unsigned f=0;f<2;f++)for(unsigned ext=0;ext<2;ext++)for(unsigned value=0;value<4;value++)for(unsigned w=1;w<=8;w++)for(unsigned h=1;h<=8;h++){
  BYTE bits[288];memset(bits,0x73,sizeof(bits));DDrawObject owner={0};unsigned fmt=f?0x35545844u:0x31545844u,b=f?16:8,pitch=((w+3)/4)*b,rows=(h+3)/4,n=pitch*rows;
  DDSURFACEDESC2 r={.dwSize=sizeof(r),.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|(ext?DDSD_LPSURFACE|DDSD_PITCH:0),.dwWidth=w,.dwHeight=h,.lPitch=(int32_t)(value==0?0:value==1?1:value==2?0xffffffffu:0x7fffffffu),.lpSurface=bits+16};
  DDSurface*s=surface_allocate(&owner,&r,fmt,w,h,DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,NULL,0,0);MUST(s&&s->pitch==pitch&&s->memory->bytes==n);MUST(s->desc.dwLinearSize==(ext?~0u:n));
  if(ext){MUST(s->shadow==bits+16);for(unsigned i=0;i<sizeof(bits);i++)MUST(bits[i]==0x73);memset(s->shadow,0xa5,n);for(unsigned i=0;i<sizeof(bits);i++)MUST(bits[i]==(i>=16&&i<16+n?0xa5:0x73));}
  destroy(s);MUST(!allocs&&!owner.children);
 }
 for(unsigned ext=0;ext<2;ext++){
  BYTE bits[64];DDrawObject owner={0};DDSURFACEDESC2 r={.dwFlags=ext?DDSD_LPSURFACE:0,.lpSurface=bits};calls=0;
  MUST(!surface_allocate(&owner,&r,0x35545844u,0xffffffffu,4,DDSCAPS_TEXTURE,NULL,0,0));MUST(!calls&&!allocs&&!owner.surfaces);
 }
 for(unsigned f=0;f<2;f++){
  DDrawObject owner={0};unsigned fmt=f?0x35545844u:0x31545844u,b=f?16:8;DWORD caps=DDSCAPS_TEXTURE|DDSCAPS_MIPMAP|DDSCAPS_COMPLEX;
  DDSURFACEDESC2 r={.dwSize=sizeof(r),.dwFlags=DDSD_PIXELFORMAT,.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT)}};
  DDSurface*root=surface_allocate(&owner,&r,fmt,8,4,caps,NULL,0,0);MUST(root&&surface_build_mip_chain(root,&r,fmt,4,caps,0));
  DDSurface*level=root;for(unsigned i=0;i<4;i++,level=level->mip_next){unsigned w=8>>i,h=4>>i;if(!w)w=1;if(!h)h=1;MUST(level&&level->width==w&&level->height==h&&level->mip_level==i&&level->storage==root->storage);MUST(level->pitch==((w+3)/4)*b&&level->memory->bytes==((w+3)/4)*b*((h+3)/4));MUST(level->desc.dwLinearSize==level->memory->bytes);}
  MUST(!level&&root->storage->ref==4);while(owner.surfaces)destroy(owner.surfaces);MUST(!allocs&&!owner.children);
 }
}
int main(void){(void)&InterlockedCompareExchange;cases();failures();block_layouts();MUST(!g_object_lock);printf("PASS actual surface allocation C: %u checks, owned and borrowed RGB/compressed storage, caller pitch/bytes/aliasing, untouched caller memory, hidden input-only metadata, and all owned/borrowed allocation failure unwinds\\n",checks);return 0;}
`;
const program=code;
try{const file=dir+"/memory.c",binary=dir+"/memory";fs.writeFileSync(file,program);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
