"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_MODE_SHAPE_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-mode-shape-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t LONG;
typedef struct{DWORD low,high;}DDCOLORKEY;typedef struct{DWORD words[8];}DDPIXELFORMAT;
#define PREFIX DWORD dwSize,dwFlags,dwHeight,dwWidth;LONG lPitch;DWORD dwBackBufferCount,dwMipMapCount,dwAlphaBitDepth,dwReserved;void*lpSurface;DDCOLORKEY ddckCKDestOverlay,ddckCKDestBlt,ddckCKSrcOverlay,ddckCKSrcBlt;DDPIXELFORMAT ddpfPixelFormat
typedef struct{PREFIX;struct{DWORD dwCaps;}ddsCaps;}DDSURFACEDESC;
typedef struct{PREFIX;struct{DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4;}ddsCaps;DWORD dwTextureStage;}DDSURFACEDESC2;
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define DD_VERSION_3 3u
static unsigned checks;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"mode shape line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
${extract("desc2_to_desc1")}
${extract("ddraw_write_display_mode")}
int main(void){for(unsigned n=0;n<256;n++)for(unsigned version=1;version<=7;version++){if(version==5||version==6)continue;for(unsigned shape=0;shape<2;shape++){DDSURFACEDESC2 from;memset(&from,0,sizeof(from));from.dwSize=sizeof(from);from.dwWidth=640+n;from.dwHeight=480+n;from.lPitch=2560+n;from.ddpfPixelFormat.words[0]=32;from.ddsCaps.dwCaps=0x1234;DDSURFACEDESC2 original=from;unsigned size=shape?sizeof(DDSURFACEDESC2):sizeof(DDSURFACEDESC);unsigned char*bytes=malloc(size+64);MUST(bytes);memset(bytes,0xcc,size+64);((DDSURFACEDESC*)bytes)->dwSize=size;ddraw_write_display_mode(&from,bytes,size,version);DDSURFACEDESC*got=(void*)bytes;MUST(got->dwSize==(version<=3?sizeof(DDSURFACEDESC):sizeof(DDSURFACEDESC2)));MUST(got->dwWidth==640+n&&got->dwHeight==480+n&&got->lPitch==(LONG)(2560+n)&&got->ddsCaps.dwCaps==0x1234);MUST(got->dwFlags==0&&!got->lpSurface&&got->ddpfPixelFormat.words[0]==32);for(unsigned i=0;i<64;i++)MUST(bytes[size+i]==0xcc);MUST(bytes[size-1]!=0xcc&&!memcmp(&from,&original,sizeof(from)));free(bytes);}}printf("PASS actual display-mode descriptor C: %u checks, five interface versions, both caller shapes, native reported size, cleared tail, guarded output and immutable source\\n",checks);return 0;}
`;
try{const file=dir+"/shape.c",binary=dir+"/shape";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
