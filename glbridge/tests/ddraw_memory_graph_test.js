'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=process.env.DDRAW_GRAPH_ROOT||path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_GRAPH_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-memory-graph-'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return source.slice(m.index,end);}
const definitions=fs.readFileSync(root+'/tests/ddraw_color_key_test.js','utf8').split('typedef uint32_t DWORD,UINT;')[1].split('typedef struct DDSurface DDSurface;')[0].replace('dwCaps,dwCaps2;', 'dwCaps,dwCaps2,dwCaps3,dwCaps4;');
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT;${definitions}
typedef size_t SIZE_T;typedef int32_t LONG;typedef unsigned char BYTE;
#define DDERR_INVALIDSURFACETYPE (-28)
#define DDERR_SURFACEBUSY (-29)
#define DDERR_OUTOFMEMORY (-30)
typedef struct DDSurfaceMemory DDSurfaceMemory;typedef struct DDTextureStorage DDTextureStorage;
${/struct DDSurfaceMemory \{[\s\S]*?\n\};/.exec(source)[0]}
${/struct DDTextureStorage \{[\s\S]*?\n\};/.exec(source)[0]}
typedef struct DDSurface{DDSURFACEDESC2 desc;UINT lock_depth,width,height,format,bytes_per_pixel,pitch,mip_level,cube_face;void*dc;BOOL implicit,indexed,external_memory,upload_pending;struct DDSurface*mip_next,*cube_faces[6];DDSurfaceMemory*memory;DDTextureStorage*storage;BYTE*shadow;DWORD handle;}DDSurface,IDirectDrawSurface7;
typedef struct{unsigned format;}DDrawPixelFormatDesc;
static UINT checks,live,allocations,fail_alloc,creates,uploads,rebinds,next_handle=100;static BOOL fail_gpu;
static BYTE client[6][4096];
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"memory graph line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
#define ZeroMemory(p,n) memset(p,0,n)
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static void*heap_alloc_zero(SIZE_T n){if(++allocations==fail_alloc)return NULL;void*p=calloc(1,n);MUST(p);live++;return p;}
static void heap_free(void*p){if(!p)return;MUST((uintptr_t)p<(uintptr_t)client||(uintptr_t)p>=(uintptr_t)(client+6));MUST(live);live--;free(p);}
static UINT allocate_handle(void){return ++next_handle;}
static BOOL emit_command(UINT op,const void*p,UINT n){MUST(op==D9WG_OP_DESTROY_RESOURCE&&p&&n==sizeof(D9WGDestroyResource));return TRUE;}
static DDSurface*surface_from_iface(DDSurface*p){return p;}
static void describe_pixel_format(DDPIXELFORMAT*p,DDrawPixelFormatDesc*d){d->format=p->dwRGBBitCount==32?22:0;}
static unsigned ddraw_pixel_format_to_d3dformat(DDrawPixelFormatDesc*d){return d->format;}
static UINT format_bytes_per_pixel(unsigned f){return f==22?4:0;}
static UINT format_compressed_block_bytes(unsigned f){(void)f;return 0;}
static UINT surface_pitch_for(UINT w,UINT p){return w*p;}
static BOOL ddraw_format_is_palettised(unsigned f){(void)f;return FALSE;}
${['surface_memory_create','surface_memory_addref','surface_memory_release','surface_storage_allocate','surface_storage_release','surface_wire_mip_level','surface_wire_cube_face'].map(extract).join('\n')}
static BOOL emit_surface_create(DDSurface*s){MUST(s&&s->storage->ref==1&&surface_wire_mip_level(s)==0&&surface_wire_cube_face(s)==0);creates++;return !fail_gpu;}
static BOOL emit_surface_upload(DDSurface*s,void*r){MUST(s&&!r&&s->memory->bits==s->shadow);uploads++;s->upload_pending=fail_gpu;return !fail_gpu;}
static void rebind_host_surface_state(DDSurface*s){MUST(s);rebinds++;}
${extract('surface_SetSurfaceDesc')}
static void init(DDSurface*s,DDTextureStorage*t,UINT mip,UINT face,UINT width){memset(s,0,sizeof(*s));s->desc.dwSize=sizeof(s->desc);s->desc.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE|DDSCAPS_COMPLEX;s->desc.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);s->desc.ddpfPixelFormat.dwRGBBitCount=32;s->width=s->height=width;s->pitch=width*4;s->format=22;s->bytes_per_pixel=4;s->mip_level=mip;s->cube_face=face;s->implicit=mip||face;s->storage=t;s->handle=t->handle;s->memory=surface_memory_create(NULL,width*width*4,TRUE);MUST(s->memory);s->shadow=s->memory->bits;memset(s->shadow,0x70+mip+face,width*width*4);}
static void release(DDSurface*s){surface_memory_release(s->memory);surface_storage_release(s->storage);}
static DDSURFACEDESC2 request(UINT i,UINT w){DDSURFACEDESC2 r={0};r.dwSize=sizeof(r);r.dwFlags=DDSD_LPSURFACE;r.lpSurface=client[i];if(w){r.dwFlags|=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH;r.dwWidth=r.dwHeight=w;r.lPitch=w*4;}return r;}
static void graph(BOOL cube){UINT n=cube?6:5;DDSurface s[6];DDTextureStorage*t=surface_storage_allocate();t->ref=n;t->resource_kind=cube?D9WG_RESOURCE_TEXTURE_CUBE:D9WG_RESOURCE_TEXTURE_2D;
 for(UINT i=0;i<n;i++)init(s+i,t,cube?0:i,cube?i:0,cube?8:16>>i);
 if(cube)for(UINT i=1;i<n;i++)s[0].cube_faces[i]=s+i;else for(UINT i=0;i+1<n;i++)s[i].mip_next=s+i+1;
 for(UINT i=0;i<n;i++){DDSurface saved[6];memcpy(saved,s,sizeof(s));DDSURFACEDESC2 r=request(i,0),input=r;UINT c=creates;memset(client[i],0x20+i,sizeof(client[i]));MUST(!surface_SetSurfaceDesc(s+i,&r,0)&&!memcmp(&r,&input,sizeof(r)));MUST(s[i].storage==t&&t->ref==(LONG)n&&creates==c&&s[i].memory->bits==client[i]&&!s[i].memory->owned&&s[i].memory->ref==1);MUST(surface_wire_mip_level(s+i)==(cube?0:i)&&surface_wire_cube_face(s+i)==(cube?i:0));MUST(s[i].mip_level==saved[i].mip_level&&s[i].cube_face==saved[i].cube_face&&s[i].implicit==saved[i].implicit&&s[i].mip_next==saved[i].mip_next&&!memcmp(s[i].cube_faces,saved[i].cube_faces,sizeof(s[i].cube_faces)));for(UINT j=0;j<n;j++)if(i!=j)MUST(!memcmp(s+j,saved+j,sizeof(s[j])));}
 for(UINT i=0;i<n;i++){DDSurface saved[6];memcpy(saved,s,sizeof(s));DDSURFACEDESC2 r=request(i,32),input=r;UINT c=creates;MUST(!surface_SetSurfaceDesc(s+i,&r,0)&&!memcmp(&r,&input,sizeof(r)));MUST(s[i].storage!=t&&s[i].storage->ref==1&&s[i].storage->mip_base==saved[i].mip_level&&s[i].storage->resource_kind==D9WG_RESOURCE_TEXTURE_2D&&s[i].handle!=saved[i].handle&&creates==c+1);MUST(surface_wire_mip_level(s+i)==0&&surface_wire_cube_face(s+i)==0&&s[i].width==32&&s[i].height==32&&s[i].pitch==128&&s[i].memory->bytes==4096);MUST(s[i].mip_level==saved[i].mip_level&&s[i].cube_face==saved[i].cube_face&&s[i].implicit==saved[i].implicit&&s[i].mip_next==saved[i].mip_next&&!memcmp(s[i].cube_faces,saved[i].cube_faces,sizeof(s[i].cube_faces)));for(UINT j=0;j<n;j++)if(i!=j)MUST(!memcmp(s+j,saved+j,sizeof(s[j])));}
 for(UINT i=n;i--;)release(s+i);MUST(!live);
}
static void aliases(void){DDSurface a,b;DDTextureStorage*t=surface_storage_allocate();init(&a,t,0,0,8);b=a;surface_memory_addref(a.memory);InterlockedIncrement(&t->ref);DDSurfaceMemory*old=a.memory;DDSURFACEDESC2 r=request(0,0);MUST(!surface_SetSurfaceDesc(&a,&r,0)&&a.storage!=b.storage&&a.memory!=b.memory&&a.memory->bits==client[0]&&b.memory==old&&old->ref==1&&b.shadow==old->bits&&b.shadow[0]==0x70&&t->ref==1);release(&a);release(&b);MUST(!live);}
static void failures(void){for(UINT kind=0;kind<2;kind++){DDSurface s;DDTextureStorage*t=surface_storage_allocate();init(&s,t,2,0,4);DDSurface saved=s;DDSURFACEDESC2 r=request(0,32),input=r;UINT before=live,c=creates,u=uploads,b=rebinds;fail_alloc=allocations+1+kind;MUST(surface_SetSurfaceDesc(&s,&r,0)==DDERR_OUTOFMEMORY&&!memcmp(&s,&saved,sizeof(s))&&!memcmp(&r,&input,sizeof(r))&&live==before&&creates==c&&uploads==u&&rebinds==b);fail_alloc=0;release(&s);MUST(!live);}
 for(UINT kind=0;kind<5;kind++){DDSurface s;DDTextureStorage*t=surface_storage_allocate();init(&s,t,0,0,8);if(kind==0)s.desc.ddsCaps.dwCaps2=DDSCAPS2_TEXTUREMANAGE;if(kind==1)s.desc.ddsCaps.dwCaps2=DDSCAPS2_D3DTEXTUREMANAGE;if(kind==2)s.desc.ddsCaps.dwCaps=DDSCAPS_VIDEOMEMORY|DDSCAPS_TEXTURE;if(kind==3)s.desc.ddsCaps.dwCaps|=DDSCAPS_PRIMARYSURFACE;if(kind==4){s.implicit=TRUE;s.desc.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN;}DDSurface saved=s;DDSURFACEDESC2 r=request(0,32);UINT a=allocations;MUST(surface_SetSurfaceDesc(&s,&r,0)==DDERR_INVALIDSURFACETYPE&&!memcmp(&s,&saved,sizeof(s))&&allocations==a);release(&s);MUST(!live);}
 DDSurface s;DDTextureStorage*t=surface_storage_allocate();init(&s,t,2,4,4);DDSURFACEDESC2 r=request(0,32);fail_gpu=TRUE;MUST(!surface_SetSurfaceDesc(&s,&r,0)&&s.upload_pending&&s.shadow==client[0]&&s.width==32&&surface_wire_mip_level(&s)==0&&surface_wire_cube_face(&s)==0&&!s.memory->gpu_dirty);fail_gpu=FALSE;release(&s);MUST(!live);
}
int main(void){graph(FALSE);graph(TRUE);aliases();failures();MUST(creates&&uploads&&rebinds&&!live);printf("PASS actual memory graph C: %u checks; real memory/storage refcounts, mip/cube pointer replacement, layout detach with logical roles retained, physical indices, aliases, allocation atomicity, deferred upload and forbidden client free\\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
