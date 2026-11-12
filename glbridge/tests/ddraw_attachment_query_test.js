"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(process.env.DDRAW_ATTACHMENT_SOURCE||path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8");
const start=source.indexOf("static HRESULT WINAPI surface_GetAttachedSurface("),end=source.indexOf("\nstatic HRESULT WINAPI surface_GetBltStatus",start);assert(start>=0&&end>start);
function extract(name){const re=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{");const match=re.exec(source);assert(match,name);let e=source.indexOf("{",match.index)+1,d=1;for(;d;e++){if(source[e]==="{")d++;if(source[e]==="}")d--;}return source.slice(match.index,e);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t HRESULT;typedef int BOOL;typedef struct{DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4;}DDSCAPS2,*LPDDSCAPS2;
typedef struct{DWORD dwSize,dwFlags,dwWidth,dwHeight,dwMipMapCount;DDSCAPS2 ddsCaps;}DDSURFACEDESC2;
typedef struct DDSurface{DDSURFACEDESC2 desc;void*owner,*storage;UINT width,height;struct DDSurface*mip_next,*attached_depth,*flip_next,*cube_faces[6];UINT mip_level,cube_face,refs;}DDSurface,IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
#define TRUE 1
#define FALSE 0
#define DDSD_CAPS 1u
#define DDSD_WIDTH 4u
#define DDSD_HEIGHT 2u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_MIPMAPCOUNT 0x20000u
#define DDSD_BACKBUFFERCOUNT 0x20u
#define DDSCAPS_PRIMARYSURFACE 0x200u
#define DDSCAPS_FRONTBUFFER 0x20u
#define DDSCAPS_BACKBUFFER 4u
#define DDSCAPS_FLIP 16u
#define DDSCAPS_VISIBLE 0x8000u
#define WINAPI
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_NOTFOUND (-2)
#define DDWG_TRACE_ERROR(h) (h)
#define DDSCAPS_COMPLEX 8u
#define DDSCAPS_TEXTURE 0x1000u
#define DDSCAPS_MIPMAP 0x400000u
#define DDSCAPS_ZBUFFER 0x20000u
#define DDSCAPS2_CUBEMAP 0x200u
#define DDSCAPS2_CUBEMAP_POSITIVEX 0x400u
#define DDSCAPS2_CUBEMAP_NEGATIVEX 0x800u
#define DDSCAPS2_CUBEMAP_POSITIVEY 0x1000u
#define DDSCAPS2_CUBEMAP_NEGATIVEY 0x2000u
#define DDSCAPS2_CUBEMAP_POSITIVEZ 0x4000u
#define DDSCAPS2_CUBEMAP_NEGATIVEZ 0x8000u
#define DDSCAPS2_CUBEMAP_ALLFACES 0xfc00u
#define DDSCAPS2_MIPMAPSUBLEVEL 0x10000u
static unsigned checks;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"attachment query line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static DDSurface*surface_from_iface(DDSurface*p){return p;}
static void IDirectDrawSurface7_AddRef(DDSurface*p){MUST(p);p->refs++;}
static DDSurface pool[16];static unsigned allocated;
static DDSurface*surface_allocate(void*owner,const DDSURFACEDESC2*desc,unsigned format,UINT width,UINT height,DWORD caps,void*storage,UINT level,UINT face){MUST(format==32&&allocated<16&&caps==desc->ddsCaps.dwCaps);DDSurface*p=&pool[allocated++];memset(p,0,sizeof(*p));p->owner=owner;p->storage=storage;p->desc=*desc;p->width=width;p->height=height;p->mip_level=level;p->cube_face=face;p->refs=1;return p;}
${extract("surface_build_mip_chain")}
${source.slice(start,end)}
static void constructed_mips(void){DDSurface root={.width=128,.height=64,.owner=(void*)1,.storage=(void*)2};DDSURFACEDESC2 desc={.dwFlags=DDSD_BACKBUFFERCOUNT,.ddsCaps={.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP}};allocated=0;MUST(surface_build_mip_chain(&root,&desc,32,8,desc.ddsCaps.dwCaps,0)&&allocated==7);DDSurface*p=&root;DDSCAPS2 caps={.dwCaps=DDSCAPS_COMPLEX};for(unsigned level=1;level<8;level++){DDSurface*next=NULL;MUST(surface_GetAttachedSurface(p,&caps,&next)==DD_OK&&next==&pool[level-1]);MUST(next->desc.ddsCaps.dwCaps&DDSCAPS_COMPLEX);MUST(next->mip_level==level&&next->width==(128u>>level)&&next->height==(level<7?64u>>level:1));MUST(!(next->desc.dwFlags&DDSD_BACKBUFFERCOUNT)&&next->desc.dwMipMapCount==8-level);p=next;}MUST(!p->mip_next);}

static void mips(void){DDSurface levels[4]={0};DWORD bits[]={DDSCAPS_TEXTURE,DDSCAPS_COMPLEX,DDSCAPS_MIPMAP};for(unsigned i=0;i<4;i++){levels[i].desc.ddsCaps.dwCaps=bits[0]|bits[1]|bits[2];levels[i].desc.ddsCaps.dwCaps2=i?DDSCAPS2_MIPMAPSUBLEVEL:0;levels[i].refs=1;levels[i].mip_next=i<3?&levels[i+1]:NULL;}
 for(unsigned i=0;i<4;i++)for(unsigned subset=0;subset<8;subset++)for(unsigned sublevel=0;sublevel<2;sublevel++){
  DDSCAPS2 caps={0};for(unsigned j=0;j<3;j++)if(subset&(1u<<j))caps.dwCaps|=bits[j];caps.dwCaps2=sublevel?DDSCAPS2_MIPMAPSUBLEVEL:DDSCAPS2_CUBEMAP_ALLFACES;DDSurface*out=(void*)1;
  HRESULT hr=surface_GetAttachedSurface(&levels[i],&caps,&out);MUST(hr==(i<3?DD_OK:DDERR_NOTFOUND));if(i<3){MUST(out==&levels[i+1]&&out->refs==2);--out->refs;}else MUST(!out);
  caps.dwCaps|=0x80000000u;out=(void*)1;MUST(surface_GetAttachedSurface(&levels[i],&caps,&out)==DDERR_NOTFOUND&&!out);for(unsigned j=0;j<4;j++)MUST(levels[j].refs==1);
 }
 DDSCAPS2 caps={0};DDSurface*out=(void*)1;MUST(surface_GetAttachedSurface(levels,NULL,&out)==DDERR_INVALIDPARAMS);MUST(surface_GetAttachedSurface(levels,&caps,NULL)==DDERR_INVALIDPARAMS);
}
static void other_attachments(void){DDSurface root={0},depth={.refs=1},back={.refs=1};DDSCAPS2 caps={.dwCaps=DDSCAPS_ZBUFFER};root.attached_depth=&depth;DDSurface*out=NULL;MUST(surface_GetAttachedSurface(&root,&caps,&out)==DD_OK&&out==&depth&&depth.refs==2);root.attached_depth=NULL;caps.dwCaps=4;root.flip_next=&back;back.flip_next=&root;back.desc.ddsCaps.dwCaps=4;MUST(surface_GetAttachedSurface(&root,&caps,&out)==DD_OK&&out==&back&&back.refs==2);}
int main(void){for(unsigned i=0;i<32;i++){constructed_mips();mips();other_attachments();}printf("PASS actual DirectDraw attachment query C: %u checks, actual mip construction including leaf COMPLEX, TEXTURE/COMPLEX/MIPMAP subsets, sublevel and non-cube caps2, endpoint/failure isolation and retained depth/flip paths\\n",checks);return 0;}
`;
const dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-query-"));
try{const file=dir+"/query.c",binary=dir+"/query";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
