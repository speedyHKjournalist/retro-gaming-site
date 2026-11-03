"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.DDRAW_OVERLAY_FLIP_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-overlay-flip-"));
function extract(name) {const re = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{"); let input = source, m = re.exec(input); if (!m && name !== "surface_Flip") {input = fs.readFileSync(root + "/ddrawproxy/ddraw_proxy.c", "utf8"); m = re.exec(input);} assert(m, name); let end = input.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (input[end] === "{") depth++; if (input[end] === "}") depth--;} return input.slice(m.index, end);}
const code = `#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL,HRESULT;typedef uint32_t DWORD;typedef unsigned char BYTE;
typedef struct{unsigned index;} DDTextureStorage;typedef struct{BYTE bits[4];} DDSurfaceMemory;
typedef struct DDSurface DDSurface;typedef struct{DDSurface*surfaces;} DDrawObject;
struct DDSurface{DDrawObject*owner;DDSurface*next,*flip_next,*overlay_destination;struct{struct{DWORD dwCaps;}ddsCaps;}desc;BOOL primary,overlay_visible,external_memory,upload_pending;DWORD handle,lock_depth,overlay_id;DDTextureStorage*storage;DDSurfaceMemory*memory;BYTE*shadow;};
typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define DDERR_GENERIC (-1)
#define DDERR_NOTFLIPPABLE (-2)
#define DDERR_SURFACEBUSY (-3)
#define DDSCAPS_FLIP 0x200u
#define DDSCAPS_OVERLAY 0x80u
#define DDWG_TRACE_CHECKPOINT(...)
#define DDWG_TRACE_RESULT(x) (x)
#define DDWG_TRACE_ERROR(x) (x)
static unsigned checks,screens,presents,overlays,rebinds,device_rebinds,fail_screen,fail_overlay,fail_present;static DWORD screen_handle,overlay_handle,overlay_destination,overlay_id;
static DDSurface*surface_from_iface(IDirectDrawSurface7*p){return p;}
static DDSurface*surface_from_any(void*p,void*q){(void)q;return p;}
static void rebind_host_surface_state(DDSurface*p){(void)p;rebinds++;}
static void d3d7_rebind_after_flip(DDrawObject*p){(void)p;device_rebinds++;}
static BOOL emit_blt_to_screen(DDSurface*p,const void*r){(void)r;screens++;screen_handle=p->handle;if(fail_screen){fail_screen=0;return FALSE;}return TRUE;}
static BOOL emit_overlay_state(DDSurface*p){overlays++;overlay_handle=p->handle;overlay_destination=p->overlay_destination?p->overlay_destination->handle:0;overlay_id=p->overlay_id;if(fail_overlay){fail_overlay=0;return FALSE;}return TRUE;}
static BOOL emit_present_and_flush(DDrawObject*p){(void)p;presents++;if(fail_present){fail_present=0;return FALSE;}return TRUE;}
${["surface_flip_contains", "surface_swap_flip_storage", "surface_rebind_flip_overlays", "surface_Flip"].map(extract).join("\n")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw overlay flip line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static void seed(DDrawObject*o,DDSurface*s,DDTextureStorage*t,DDSurfaceMemory*m,unsigned count){memset(s,0,sizeof(*s)*count);o->surfaces=s;for(unsigned i=0;i<count;i++){t[i].index=i;m[i].bits[0]=(BYTE)i;s[i].owner=o;s[i].next=i+1<count?&s[i+1]:NULL;s[i].flip_next=&s[(i+1)%count];s[i].desc.ddsCaps.dwCaps=DDSCAPS_FLIP;s[i].handle=100+i;s[i].storage=&t[i];s[i].memory=&m[i];s[i].shadow=m[i].bits;s[i].external_memory=i==1;s[i].upload_pending=i!=1;s[i].overlay_id=200+i;}}
static int verify(DDSurface*s,DDTextureStorage*t,DDSurfaceMemory*m,unsigned*order,unsigned count){for(unsigned i=0;i<count;i++){unsigned n=order[i];CHECK(s[i].handle==100+n&&s[i].storage==&t[n]&&s[i].memory==&m[n]&&s[i].shadow==m[n].bits&&s[i].external_memory==(n==1)&&s[i].upload_pending==(n!=1)&&s[i].overlay_id==200+i);}return 0;}
static int rotations(void){
 for(unsigned count=2;count<=3;count++){
  DDrawObject owner={0};DDSurface s[3],destination={.handle=9};DDTextureStorage t[3];DDSurfaceMemory m[3];seed(&owner,s,t,m,count);s[0].desc.ddsCaps.dwCaps|=DDSCAPS_OVERLAY;s[0].overlay_visible=TRUE;s[0].overlay_destination=&destination;unsigned order[]={0,1,2};
  for(unsigned round=0;round<128;round++){
   unsigned a=order[0];for(unsigned i=0;i+1<count;i++)order[i]=order[i+1];order[count-1]=a;unsigned b=screens,p=presents,o=overlays,r=rebinds,d=device_rebinds;CHECK(!surface_Flip(s,NULL,0));CHECK(screens==b&&presents==p+1&&overlays==o+1&&rebinds==r+count&&device_rebinds==d+1);CHECK(overlay_handle==s[0].handle&&overlay_destination==9&&overlay_id==200);CHECK(!verify(s,t,m,order,count));
   a=order[0];order[0]=order[count-1];order[count-1]=a;b=screens;p=presents;o=overlays;CHECK(!surface_Flip(s,&s[count-1],0));CHECK(screens==b&&presents==p+1&&overlays==o+1&&!verify(s,t,m,order,count));
  }
  s[0].overlay_visible=FALSE;unsigned p=presents,o=overlays,b=screens;CHECK(!surface_Flip(s,NULL,0)&&presents==p&&overlays==o&&screens==b);
 }
 return 0;
}
static int other_cases(void){
 DDrawObject owner={0},foreign_owner={0};DDSurface s[3],overlay={0},other={.owner=&owner,.handle=900},foreign={.owner=&foreign_owner};DDTextureStorage t[3];DDSurfaceMemory m[3];seed(&owner,s,t,m,3);s[2].next=&overlay;overlay.owner=&owner;overlay.desc.ddsCaps.dwCaps=DDSCAPS_OVERLAY;overlay.overlay_visible=TRUE;overlay.handle=800;overlay.overlay_destination=s;overlay.overlay_id=777;s[0].primary=TRUE;
 unsigned b=screens,p=presents,o=overlays;CHECK(!surface_Flip(s,NULL,0)&&screens==b+1&&presents==p+1&&overlays==o+1&&screen_handle==101&&overlay_handle==800&&overlay_destination==101&&overlay_id==777);
 DWORD before=s[0].handle;b=screens;p=presents;o=overlays;CHECK(surface_Flip(s,s,0)==DDERR_NOTFLIPPABLE&&surface_Flip(s,&other,0)==DDERR_NOTFLIPPABLE&&surface_Flip(s,&foreign,0)==DDERR_NOTFLIPPABLE&&s[0].handle==before&&screens==b&&presents==p&&overlays==o);s[0].lock_depth=1;CHECK(surface_Flip(s,NULL,0)==DDERR_SURFACEBUSY&&s[0].handle==before);s[0].lock_depth=0;
 s[0].primary=FALSE;overlay.overlay_destination=NULL;b=screens;p=presents;o=overlays;CHECK(!surface_Flip(s,NULL,0)&&screens==b&&presents==p&&overlays==o);overlay.overlay_destination=s;b=screens;p=presents;o=overlays;CHECK(!surface_Flip(s,NULL,0)&&screens==b&&presents==p+1&&overlays==o+1&&overlay_destination==s[0].handle);
 s[0].primary=TRUE;before=s[0].handle;fail_screen=1;p=presents;o=overlays;CHECK(surface_Flip(s,NULL,0)==DDERR_GENERIC&&s[0].handle!=before&&presents==p&&overlays==o);fail_overlay=1;p=presents;CHECK(surface_Flip(s,NULL,0)==DDERR_GENERIC&&presents==p);fail_present=1;CHECK(surface_Flip(s,NULL,0)==DDERR_GENERIC);CHECK(!surface_Flip(s,NULL,0));s[0].primary=FALSE;s[0].desc.ddsCaps.dwCaps=0;before=s[0].handle;CHECK(surface_Flip(s,NULL,0)==DDERR_NOTFLIPPABLE&&s[0].handle==before);return 0;
}
int main(void){(void)&surface_from_any;(void)&surface_flip_contains;(void)&surface_swap_flip_storage;(void)&surface_rebind_flip_overlays;CHECK(!rotations());CHECK(!other_cases());printf("PASS actual DDraw overlay Flip: %u checks, 512 two/three-buffer default/named flips, coupled storage/pending state, stable overlay identity, source/destination rebinding, primary base preservation, silent hidden/offscreen flips and invalid-target isolation\\n",checks);return 0;}
`;
try {const file = dir + "/flip.c", binary = dir + "/flip"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
