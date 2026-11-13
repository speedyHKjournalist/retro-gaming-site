"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),{createHash}=require("node:crypto");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_DESCRIPTOR_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8");
const native=JSON.parse(fs.readFileSync(root+"/../../v86/tests/glbridge/fixtures/ddraw_native_descriptors.json"));
assert.equal(createHash("sha256").update(fs.readFileSync(root+"/../"+native.source)).digest("hex"),native.sourceSha256);
const dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-descriptor-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let e=source.indexOf("{",m.index)+1,d=1;for(;d;e++){if(source[e]==="{")d++;if(source[e]==="}")d--;}return source.slice(m.index,e);}
const code=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD;typedef int32_t LONG;typedef int BOOL;
typedef struct{DWORD low,high;}DDCOLORKEY;
typedef struct{DWORD dwSize,dwFlags,dwFourCC;union{DWORD dwRGBBitCount,dwZBufferBitDepth;};union{DWORD dwRBitMask,dwStencilBitDepth;};union{DWORD dwGBitMask,dwZBitMask;};union{DWORD dwBBitMask,dwStencilBitMask;};DWORD dwRGBAlphaBitMask;}DDPIXELFORMAT;
#define PREFIX DWORD dwSize,dwFlags,dwHeight,dwWidth;LONG lPitch;DWORD dwBackBufferCount;union{DWORD dwMipMapCount,dwZBufferBitDepth;};DWORD dwAlphaBitDepth,dwReserved;void*lpSurface;DDCOLORKEY ddckCKDestOverlay,ddckCKDestBlt,ddckCKSrcOverlay,ddckCKSrcBlt;DDPIXELFORMAT ddpfPixelFormat
typedef struct{PREFIX;struct{DWORD dwCaps;}ddsCaps;}DDSURFACEDESC;
typedef struct{PREFIX;struct{DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4;}ddsCaps;DWORD dwTextureStage;}DDSURFACEDESC2;
typedef struct{DDPIXELFORMAT live;}DDrawObject;
typedef struct{DDSURFACEDESC2 desc;BOOL primary;DDrawObject*owner;}DDSurface;
#define ZeroMemory(p,n) memset(p,0,n)
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_ZBUFFERBITDEPTH 0x40u
#define DDPF_ZBUFFER 0x400u
static unsigned checks;
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"descriptor line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static BOOL ddraw_platform_primary_format(DDrawObject*o,DDPIXELFORMAT*p){*p=o->live;return 1;}
METHODS
static const struct Row{const char*name;unsigned version,flags,w,h,pitch,bits,u,pf,caps;}rows[]={NATIVE_ROWS};
int main(void){
 for(unsigned bits=16;bits<=32;bits+=8){
  DDSURFACEDESC old={.dwSize=sizeof(old),.dwFlags=DDSD_ZBUFFERBITDEPTH,.dwZBufferBitDepth=bits};
  memset(&old.ddpfPixelFormat,0xa5,sizeof(old.ddpfPixelFormat));DDSURFACEDESC before=old;DDSURFACEDESC2 modern;
  desc1_to_desc2(&old,&modern);
  MUST(sizeof(DDPIXELFORMAT)==32&&modern.dwFlags==DDSD_PIXELFORMAT&&!modern.dwMipMapCount);
  MUST(modern.ddpfPixelFormat.dwZBufferBitDepth==bits&&modern.ddpfPixelFormat.dwZBitMask==(bits==24?0xffffffu:0u));
  MUST(modern.ddpfPixelFormat.dwFlags==DDPF_ZBUFFER&&!modern.ddpfPixelFormat.dwFourCC&&!modern.ddpfPixelFormat.dwStencilBitDepth&&!modern.ddpfPixelFormat.dwStencilBitMask&&!modern.ddpfPixelFormat.dwRGBAlphaBitMask);
  MUST(!memcmp(&old,&before,sizeof(old)));
  old.dwFlags|=DDSD_PIXELFORMAT;old.ddpfPixelFormat.dwZBitMask=0xffffff00;desc1_to_desc2(&old,&modern);
  MUST(!memcmp(&old.ddpfPixelFormat,&modern.ddpfPixelFormat,sizeof(old.ddpfPixelFormat)));
 }
 for(unsigned repeat=0;repeat<32;repeat++)for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){
  const struct Row*r=&rows[i];DDrawObject owner={.live={.dwSize=sizeof(DDPIXELFORMAT),.dwFlags=r->pf,.dwRGBBitCount=r->bits}};
  DDSurface s={.primary=!strncmp(r->name,"primary_",8),.owner=&owner};
  s.desc.dwSize=sizeof(s.desc);s.desc.dwFlags=0x100f;s.desc.dwWidth=r->w;s.desc.dwHeight=r->h;s.desc.lPitch=r->pitch;
  s.desc.ddpfPixelFormat=owner.live;s.desc.ddsCaps.dwCaps=r->caps;
  if(s.primary)s.desc.ddpfPixelFormat.dwRGBBitCount=32;
  DDSurface before=s;
  unsigned size=r->version<4?sizeof(DDSURFACEDESC):sizeof(DDSURFACEDESC2);
  unsigned char*bytes=malloc(size+64);MUST(bytes);memset(bytes,0xa5,size+64);
  ((DDSURFACEDESC*)bytes)->dwSize=size;legacy_describe(&s,bytes);
  DDSURFACEDESC*got=(void*)bytes;
  MUST(got->dwSize==size&&got->dwFlags==r->flags&&got->dwWidth==r->w&&got->dwHeight==r->h&&got->lPitch==(LONG)r->pitch);
  MUST(got->ddpfPixelFormat.dwRGBBitCount==r->bits&&got->ddpfPixelFormat.dwFlags==r->pf&&got->dwMipMapCount==r->u);
  MUST(got->ddsCaps.dwCaps==r->caps&&!memcmp(&s,&before,sizeof(s)));
  for(unsigned n=0;n<64;n++)MUST(bytes[size+n]==0xa5);
  free(bytes);
  if(r->version<4&&(r->pf&DDPF_ZBUFFER)){
   DDSURFACEDESC old={.dwSize=sizeof(old),.dwFlags=r->flags,.dwWidth=r->w,.dwHeight=r->h,.lPitch=r->pitch,.dwZBufferBitDepth=r->bits};
   DDSURFACEDESC2 modern;desc1_to_desc2(&old,&modern);
   MUST(modern.dwSize==sizeof(modern)&&modern.dwFlags==0x100f&&!modern.dwMipMapCount);
   MUST(modern.ddpfPixelFormat.dwFlags==DDPF_ZBUFFER&&modern.ddpfPixelFormat.dwRGBBitCount==r->bits);
  }
 }
 printf("PASS actual descriptor bridge C: %u checks, 35 native records, live primary depth, both descriptor shapes, legacy depth flag/union, guarded outputs and immutable allocation metadata\n",checks);return 0;
}
`.replace("METHODS",["desc1_to_desc2","desc2_to_desc1","legacy_describe"].map(extract).join("\n"))
 .replace("NATIVE_ROWS",native.rows.map(r=>"{"+JSON.stringify(r.name)+","+[r.view,r.flags,r.width,r.height,r.pitch,r.bits,r.union,r.pf,r.caps].join(",")+"}").join(","));
try{fs.writeFileSync(dir+"/test.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
