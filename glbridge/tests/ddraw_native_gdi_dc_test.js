"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_NATIVE_GDI_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-native-gdi-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
const structure=source.match(/typedef struct DDrawNativeDC \{[\s\S]*?\} DDrawNativeDC;/)[0];
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef uint16_t WORD;typedef unsigned char BYTE;typedef int BOOL;typedef uintptr_t FLATPTR;typedef void*HDC;typedef void*HMODULE;
typedef struct{BYTE r,g,b,f;}PALETTEENTRY;typedef struct{DWORD words[8];}DDPIXELFORMAT;
typedef struct{uintptr_t hDD;}DDRAWI_DIRECTDRAW_GBL;
typedef struct{DDRAWI_DIRECTDRAW_GBL*lpGbl;DWORD dwProcessId;}DDRAWI_DIRECTDRAW_LCL;
typedef struct{DWORD dwSize;}DDRAWI_DDRAWSURFACE_GBL_MORE;
typedef struct{DDRAWI_DIRECTDRAW_LCL*lpDD_lcl;DWORD dwSize;}DDRAWI_DDRAWSURFACE_MORE;
typedef struct{DDRAWI_DIRECTDRAW_GBL*lpDD;FLATPTR fpVidMem;LONG lPitch;WORD wHeight,wWidth;DWORD dwGlobalFlags;DDPIXELFORMAT ddpfSurface;}DDRAWI_DDRAWSURFACE_GBL;
typedef struct{DDRAWI_DDRAWSURFACE_MORE*lpSurfMore;DDRAWI_DDRAWSURFACE_GBL*lpGbl;uintptr_t hDDSurface;DWORD dwProcessId,dwFlags;struct{DWORD dwCaps;}ddsCaps;}DDRAWI_DDRAWSURFACE_LCL;
typedef struct{BYTE*shadow;UINT width,height,pitch;struct{DDPIXELFORMAT ddpfPixelFormat;}desc;}DDSurface;
#define WINAPI
#define FALSE 0
#define DDRAWISURFGBL_SYSMEMREQUESTED 2u
#define DDRAWISURFGBL_ISCLIENTMEM 0x100u
#define DDRAWISURF_HASPIXELFORMAT 0x2000u
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define DDSCAPS_OFFSCREENPLAIN 0x40u
${structure}
static unsigned checks,fail,missing,allocs,screens,draws,surfaces,dcs,flushes;static DDSurface*input;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"native GDI line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static void*heap_alloc_zero(size_t n){if(fail==1)return NULL;allocs++;return calloc(1,n);}static void heap_free(void*p){MUST(p&&allocs==1&&!screens&&!draws&&!surfaces&&!dcs);allocs--;free(p);}
static HMODULE GetModuleHandleA(const char*s){MUST(!strcmp(s,"gdi32.dll"));return fail==2?NULL:(void*)1;}
static HDC GetDC(void*p){MUST(!p);if(fail==4)return NULL;screens++;return (void*)2;}
static void ReleaseDC(void*p,HDC dc){MUST(!p&&dc==(void*)2&&screens==1&&!draws&&!surfaces&&!dcs);screens--;}
static DWORD GetCurrentProcessId(void){return 77;}
static BOOL create_draw(DDRAWI_DIRECTDRAW_GBL*d,HDC dc){MUST(dc==(void*)2&&screens==1&&!draws);if(fail==5)return 0;d->hDD=3;draws++;return fail!=8;}
static BOOL delete_draw(DDRAWI_DIRECTDRAW_GBL*d){MUST(d->hDD==3&&draws==1&&!surfaces&&!dcs);d->hDD=0;draws--;return 1;}
static BOOL create_surface(DDRAWI_DDRAWSURFACE_LCL*s,BOOL primary){MUST(!primary&&draws==1&&!surfaces&&s->lpSurfMore&&s->lpGbl);DDRAWI_DDRAWSURFACE_GBL*g=s->lpGbl;MUST(s->lpSurfMore->dwSize==sizeof(DDRAWI_DDRAWSURFACE_MORE));MUST(s->lpSurfMore->lpDD_lcl&&s->lpSurfMore->lpDD_lcl->lpGbl==g->lpDD&&g->lpDD->hDD==3);MUST(s->lpSurfMore->lpDD_lcl->dwProcessId==77&&s->dwProcessId==77);DDRAWI_DDRAWSURFACE_GBL_MORE*m=*((DDRAWI_DDRAWSURFACE_GBL_MORE**)g-1);MUST(m&&m->dwSize==sizeof(*m));MUST(g->fpVidMem==(uintptr_t)input->shadow&&g->lPitch==(LONG)input->pitch&&g->wWidth==input->width&&g->wHeight==input->height);MUST(g->dwGlobalFlags==(DDRAWISURFGBL_SYSMEMREQUESTED|DDRAWISURFGBL_ISCLIENTMEM));MUST(s->dwFlags==DDRAWISURF_HASPIXELFORMAT&&s->ddsCaps.dwCaps==(DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN));MUST(!memcmp(&g->ddpfSurface,&input->desc.ddpfPixelFormat,sizeof(DDPIXELFORMAT)));if(fail==6)return 0;s->hDDSurface=4;surfaces++;return fail!=9;}
static BOOL delete_surface(DDRAWI_DDRAWSURFACE_LCL*s){MUST(s->hDDSurface==4&&surfaces==1&&!dcs&&draws==1);s->hDDSurface=0;surfaces--;return 1;}
static HDC get_dc(DDRAWI_DDRAWSURFACE_LCL*s,PALETTEENTRY*p){MUST(s->hDDSurface==4&&surfaces==1&&!p&&!dcs);if(fail==7)return NULL;dcs++;return (void*)5;}
static BOOL release_dc(DDRAWI_DDRAWSURFACE_LCL*s){MUST(s->hDDSurface==4&&dcs==1&&flushes);dcs--;return 1;}
static void GdiFlush(void){flushes++;}
static void*GetProcAddress(HMODULE module,const char*name){MUST(module==(void*)1);unsigned entry=(unsigned)(name[8]-'0');MUST(!strncmp(name,"GdiEntry",8)&&name[9]==0);if(fail==3||entry==missing)return NULL;switch(entry){case 1:return (void*)create_draw;case 3:return (void*)delete_draw;case 4:return (void*)create_surface;case 5:return (void*)delete_surface;case 7:return (void*)get_dc;case 8:return (void*)release_dc;default:MUST(0);return NULL;}}
${extract("ddraw_gdi_dc_destroy")}
${extract("ddraw_gdi_dc_create")}
int main(void){BYTE bits[1024]={0};DDSurface s={.shadow=bits,.width=16,.height=16,.pitch=64};input=&s;for(unsigned n=0;n<64;n++){s.desc.ddpfPixelFormat.words[0]=n;DDSurface before=s;for(fail=0;fail<=9;fail++){for(missing=0;missing<=8;missing++){DDrawNativeDC*d=ddraw_gdi_dc_create(&s);BOOL good=!fail&&(missing==0||missing==2||missing==6);MUST(!!d==good&&!memcmp(&s,&before,sizeof(s)));if(d){MUST(d->dc==(void*)5&&allocs==1&&screens==1&&draws==1&&surfaces==1&&dcs==1);ddraw_gdi_dc_destroy(d);}MUST(!allocs&&!screens&&!draws&&!surfaces&&!dcs);}}fail=missing=0;DDSurface bad=s;bad.shadow=NULL;MUST(!ddraw_gdi_dc_create(&bad));bad=s;bad.width=0;MUST(!ddraw_gdi_dc_create(&bad));bad.width=65536;MUST(!ddraw_gdi_dc_create(&bad));bad=s;bad.height=65536;MUST(!ddraw_gdi_dc_create(&bad));bad=s;bad.pitch=0x80000000u;MUST(!ddraw_gdi_dc_create(&bad));MUST(!allocs&&!screens&&!draws&&!surfaces&&!dcs);ddraw_gdi_dc_destroy(NULL);}printf("PASS actual native GDI DC C: %u checks, SDK pointer graph and pixel layout, borrowed shadow identity, all export/constructor failures, partial kernel allocations, ordered complete teardown, invalid geometry and immutable caller\\n",checks);return 0;}
`;
try{const file=dir+"/gdi.c",binary=dir+"/gdi";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
