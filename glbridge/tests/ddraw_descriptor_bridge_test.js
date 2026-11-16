"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),{createHash}=require("node:crypto");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_DESCRIPTOR_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8");
const native=JSON.parse(fs.readFileSync(root+"/../../v86/tests/glbridge/fixtures/ddraw_native_descriptors.json"));
assert.equal(createHash("sha256").update(fs.readFileSync(root+"/../"+native.source)).digest("hex"),native.sourceSha256);
const memoryNative=JSON.parse(fs.readFileSync(root+"/../../v86/tests/glbridge/fixtures/ddraw_native_memory_descriptors.json"));
assert.equal(createHash("sha256").update(fs.readFileSync(root+"/../"+memoryNative.source)).digest("hex"),memoryNative.sourceSha256);
const getterThunks=[1,2,3,4].map(view=>{const start=source.indexOf('static HRESULT WINAPI tag##_GetSurfaceDesc('),end=source.indexOf('static HRESULT WINAPI tag##_Initialize(',start);assert(start>=0&&end>start);return source.slice(start,end).replace(/\\\r?\n/g,'\n').replaceAll('tag##','view'+view).replaceAll('IFACE','DDSurface').replace(/\bDESC\b/g,view<4?'DDSURFACEDESC':'DDSURFACEDESC2');}).join('\n');
const dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-descriptor-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let e=source.indexOf("{",m.index)+1,d=1;for(;d;e++){if(source[e]==="{")d++;if(source[e]==="}")d--;}return source.slice(m.index,e);}
const code=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD;typedef int32_t LONG,HRESULT;typedef int BOOL;
typedef struct{DWORD low,high;}DDCOLORKEY;
typedef struct{DWORD dwSize,dwFlags,dwFourCC;union{DWORD dwRGBBitCount,dwZBufferBitDepth;};union{DWORD dwRBitMask,dwStencilBitDepth;};union{DWORD dwGBitMask,dwZBitMask;};union{DWORD dwBBitMask,dwStencilBitMask;};DWORD dwRGBAlphaBitMask;}DDPIXELFORMAT;
#define PREFIX DWORD dwSize,dwFlags,dwHeight,dwWidth;LONG lPitch;DWORD dwBackBufferCount;union{DWORD dwMipMapCount,dwZBufferBitDepth;};DWORD dwAlphaBitDepth,dwReserved;void*lpSurface;DDCOLORKEY ddckCKDestOverlay,ddckCKDestBlt,ddckCKSrcOverlay,ddckCKSrcBlt;DDPIXELFORMAT ddpfPixelFormat
typedef struct{PREFIX;struct{DWORD dwCaps;}ddsCaps;}DDSURFACEDESC;
typedef struct{PREFIX;struct{DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4;}ddsCaps;DWORD dwTextureStage;}DDSURFACEDESC2,*LPDDSURFACEDESC2;
typedef struct{DDPIXELFORMAT live;}DDrawObject;
typedef struct{DDSURFACEDESC2 desc;BOOL primary;DDrawObject*owner;}DDSurface;
typedef DDSurface IDirectDrawSurface7;
#define WINAPI
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057u)
#define DDWG_TRACE_ERROR(hr) (hr)
static DDSurface*surface_from_iface(DDSurface*p){return p;}
static DDSurface*surface_from_any(void*p,void*unused){(void)unused;return p;}
#define ZeroMemory(p,n) memset(p,0,n)
#define DD_OK 0
#define DDSD_LPSURFACE 0x800u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_ZBUFFERBITDEPTH 0x40u
#define DDPF_ZBUFFER 0x400u
static unsigned checks;
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"descriptor line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static BOOL ddraw_platform_primary_format(DDrawObject*o,DDPIXELFORMAT*p){*p=o->live;return 1;}
METHODS
GETTERS
static const struct Row{const char*name;unsigned version,flags,w,h,pitch,bits,u,pf,caps;}rows[]={NATIVE_ROWS};
static const struct{unsigned view,flags,w,h,pitch,bits,pf,caps;}memory_rows[]={MEMORY_ROWS};
static HRESULT get_description(unsigned view,DDSurface*s,void*p){switch(view){case 1:return view1_GetSurfaceDesc(s,p);case 2:return view2_GetSurfaceDesc(s,p);case 3:return view3_GetSurfaceDesc(s,p);case 4:return view4_GetSurfaceDesc(s,p);default:return surface_GetSurfaceDesc(s,p);}}
static void client_descriptor_views(void){
 for(unsigned i=0;i<sizeof(memory_rows)/sizeof(*memory_rows);i++){
  unsigned v=memory_rows[i].view,size=v<4?sizeof(DDSURFACEDESC):sizeof(DDSURFACEDESC2);DDrawObject owner={0};DDSurface s={.owner=&owner};
  s.desc=(DDSURFACEDESC2){.dwSize=sizeof(s.desc),.dwFlags=memory_rows[i].flags|0x800,.dwWidth=memory_rows[i].w,.dwHeight=memory_rows[i].h,.lPitch=memory_rows[i].pitch,.lpSurface=(void*)(uintptr_t)0x12345678,.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT),.dwFlags=memory_rows[i].pf,.dwRGBBitCount=memory_rows[i].bits},.ddsCaps={.dwCaps=memory_rows[i].caps}};DDSurface before=s;
  unsigned char bytes[sizeof(DDSURFACEDESC2)+32],saved[sizeof(bytes)];memset(bytes,0xa5,sizeof(bytes));((DDSURFACEDESC*)bytes)->dwSize=size;
  MUST(!get_description(v,&s,bytes));DDSURFACEDESC*old=(void*)bytes;DDSURFACEDESC2*modern=(void*)bytes;
  MUST(old->dwSize==size&&old->dwFlags==memory_rows[i].flags&&!old->lpSurface&&old->dwWidth==memory_rows[i].w&&old->dwHeight==memory_rows[i].h&&old->lPitch==(LONG)memory_rows[i].pitch&&old->ddpfPixelFormat.dwRGBBitCount==memory_rows[i].bits);
  MUST((v<4?old->ddsCaps.dwCaps:modern->ddsCaps.dwCaps)==memory_rows[i].caps&&!memcmp(&s,&before,sizeof(s)));
  for(unsigned j=size;j<sizeof(bytes);j++)MUST(bytes[j]==0xa5);
  for(int delta=-1;delta<=1;delta+=2){memset(bytes,0xa5,sizeof(bytes));((DDSURFACEDESC*)bytes)->dwSize=size+delta;memcpy(saved,bytes,sizeof(bytes));MUST(get_description(v,&s,bytes)==DDERR_INVALIDPARAMS&&!memcmp(bytes,saved,sizeof(bytes))&&!memcmp(&s,&before,sizeof(s)));}
  MUST(get_description(v,&s,NULL)==DDERR_INVALIDPARAMS&&!memcmp(&s,&before,sizeof(s)));
 }
}
int main(void){client_descriptor_views();
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
 printf("PASS actual descriptor bridge C: %u checks, 35 original plus45 client-memory native records, real legacy getters, live primary depth, both descriptor shapes, legacy depth flag/union, guarded outputs and immutable allocation metadata\n",checks);return 0;
}
`.replace("METHODS",["desc1_to_desc2","desc2_to_desc1","legacy_describe","surface_GetSurfaceDesc"].map(extract).join("\n"))
 .replace("GETTERS",getterThunks).replace("MEMORY_ROWS",memoryNative.rows.map(r=>"{"+[r.view,r.flags,r.width,r.height,r.pitch,r.bits,r.pf,r.caps].join(",")+"}").join(","))
 .replace("NATIVE_ROWS",native.rows.map(r=>"{"+JSON.stringify(r.name)+","+[r.view,r.flags,r.width,r.height,r.pitch,r.bits,r.union,r.pf,r.caps].join(",")+"}").join(","));
try{fs.writeFileSync(dir+"/test.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
