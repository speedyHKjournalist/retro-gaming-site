"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.DDRAW_PRIMARY_WRITE_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-primary-write-"));
function extract(name) {const pattern = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{"); let input = source, m = pattern.exec(input); if (!m && ["primary_present_area", "present_changed_primary"].includes(name)) {input = fs.readFileSync(root + "/ddrawproxy/ddraw_proxy.c", "utf8"); m = pattern.exec(input);} assert(m, name); let end = input.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (input[end] === "{") depth++; if (input[end] === "}") depth--;} return input.slice(m.index, end);}
const code = `#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "${root}/ddrawproxy/ddraw_protocol.h"
typedef uint32_t UINT,DWORD;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef unsigned char BYTE;typedef void*HWND;
typedef struct {LONG left,top,right,bottom;} RECT,*LPRECT;
typedef struct {BOOL client_succeeded,origin_succeeded;struct{LONG window_x,window_y;UINT client_width,client_height;}state;} D9WGWindowProbe;
typedef struct{HWND window;} DDClipper;typedef struct{HWND window;UINT cooperative_flags;uint32_t device_handle;} DDrawObject;
typedef struct{BOOL gpu_dirty;} Memory;
typedef struct DDSurface{DDrawObject*owner;DDClipper*clipper;UINT width,height,pitch,bytes_per_pixel,handle,mip_level,cube_face;BOOL primary;void*flip_next;Memory*memory;BYTE*shadow,*dib_bits;void*dc,*dib,*old_bitmap;DWORD lock_flags,lock_depth;RECT locked_rect;} DDSurface;
typedef DDSurface IDirectDrawSurface7;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define DDERR_GENERIC (-1)
#define DDERR_NOTLOCKED (-2)
#define DDLOCK_READONLY 0x10u
#define DDSCL_NORMAL 8u
#define D9WG_WINDOW_PROBE_STATE 1u
#define DDWG_TRACE_CHECKPOINT(...)
#define DDWG_TRACE_RESULT(x) (x)
#define DDWG_TRACE_ERROR(x) (x)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
static unsigned checks,probes,blits,presents,uploads,fail_blit,fail_present,fail_prepare;static uintptr_t probed_window;static D9WGWindowProbe next_probe;static D9WGDDBlt last_blit;static RECT last_upload;
static void d9wg_window_probe_capture(uintptr_t w,UINT flags,D9WGWindowProbe*p){(void)flags;probes++;probed_window=w;*p=next_probe;}
static BOOL sync_external_surface_for_read(DDSurface*s){(void)s;return TRUE;}
static BOOL ensure_device(DDrawObject*o){(void)o;return TRUE;}
static BOOL prepare_surface_for_host(DDSurface*s){(void)s;return !fail_prepare;}
static BOOL emit_command(UINT op,const void*p,UINT size){if(op!=D9WG_OP_DD_BLT||size!=sizeof(last_blit))return FALSE;memcpy(&last_blit,p,size);blits++;if(fail_blit){fail_blit=0;return FALSE;}return TRUE;}
static BOOL emit_present_and_flush(DDrawObject*o){(void)o;presents++;if(fail_present){fail_present=0;return FALSE;}return TRUE;}
static DDSurface *surface_from_iface(IDirectDrawSurface7*p){return p;}
static void emit_surface_upload(DDSurface*s,RECT*r){(void)s;uploads++;last_upload=r?*r:(RECT){0,0,(LONG)s->width,(LONG)s->height};}
static void GdiFlush(void){}
static void SelectObject(void*a,void*b){(void)a;(void)b;}
static void DeleteObject(void*p){(void)p;}
static void DeleteDC(void*p){(void)p;}
${extract("primary_present_area")}
${extract("emit_blt_to_screen")}
${extract("present_changed_primary")}
${extract("surface_Unlock")}
${extract("surface_finish_dc")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw primary line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static LONG maxl(LONG a,LONG b){return a>b?a:b;}
static LONG minl(LONG a,LONG b){return a<b?a:b;}
static int areas(void){
 DDrawObject owner={.window=(void*)1,.cooperative_flags=DDSCL_NORMAL,.device_handle=9};DDSurface s={.owner=&owner,.primary=TRUE,.width=100,.height=80,.handle=11};
 const LONG positions[]={-160,-20,-1,0,1,20,79,99,100,160};const UINT extents[]={0,1,2,20,100,300};
 for(unsigned xi=0;xi<10;xi++)for(unsigned yi=0;yi<10;yi++)for(unsigned wi=0;wi<6;wi++)for(unsigned hi=0;hi<6;hi++)for(unsigned dirty=0;dirty<3;dirty++){
  LONG x=positions[xi],y=positions[yi];UINT w=extents[wi],h=extents[hi];next_probe=(D9WGWindowProbe){.client_succeeded=TRUE,.origin_succeeded=TRUE,.state={x,y,w,h}};RECT input=dirty==1?(RECT){10,12,70,60}:(RECT){-10,-20,110,90},got;BOOL ok=primary_present_area(&s,dirty?&input:NULL,&got);RECT wanted={maxl(0,x),maxl(0,y),minl(100,x+(LONG)w),minl(80,y+(LONG)h)};if(dirty){wanted.left=maxl(wanted.left,input.left);wanted.top=maxl(wanted.top,input.top);wanted.right=minl(wanted.right,input.right);wanted.bottom=minl(wanted.bottom,input.bottom);}CHECK(ok==(wanted.right>wanted.left&&wanted.bottom>wanted.top));if(ok)CHECK(!memcmp(&got,&wanted,sizeof(got)));CHECK(probed_window==1);
 }
 RECT area;next_probe=(D9WGWindowProbe){.client_succeeded=TRUE,.origin_succeeded=TRUE,.state={30,40,32,24}};CHECK(primary_present_area(&s,NULL,&area)&&area.left==30&&area.top==40&&area.right==62&&area.bottom==64);
 CHECK(emit_blt_to_screen(&s,NULL)&&last_blit.source_rect[0]==30&&last_blit.source_rect[1]==40&&last_blit.source_rect[2]==62&&last_blit.destination_rect[3]==64);
 DDClipper clipper={(void*)2};s.clipper=&clipper;CHECK(primary_present_area(&s,NULL,&area)&&probed_window==2);s.clipper=NULL;unsigned old=probes;s.primary=FALSE;CHECK(primary_present_area(&s,NULL,&area)&&area.right==100&&area.bottom==80&&probes==old);s.primary=TRUE;owner.cooperative_flags=0;CHECK(primary_present_area(&s,NULL,&area)&&area.right==100&&probes==old);owner.cooperative_flags=DDSCL_NORMAL;owner.window=NULL;CHECK(primary_present_area(&s,NULL,&area)&&area.right==100&&probes==old);owner.window=(void*)1;
 next_probe.client_succeeded=FALSE;CHECK(!primary_present_area(&s,NULL,&area));next_probe.client_succeeded=TRUE;next_probe.origin_succeeded=FALSE;CHECK(!primary_present_area(&s,NULL,&area));next_probe.origin_succeeded=TRUE;next_probe.state.window_x=INT32_MAX;next_probe.state.client_width=UINT32_MAX;CHECK(!primary_present_area(&s,NULL,&area));next_probe.state.window_x=INT32_MIN;next_probe.state.client_width=UINT32_MAX;CHECK(primary_present_area(&s,NULL,&area)&&area.left==0&&area.right==100);next_probe.state.window_x=20;CHECK(primary_present_area(&s,NULL,&area)&&area.left==20&&area.right==100);return 0;
}
static int routes(void){
 DDrawObject owner={.window=(void*)1,.cooperative_flags=DDSCL_NORMAL,.device_handle=9};Memory memory={0};BYTE shadow[64]={0},dib[64];memset(dib,0x42,sizeof(dib));DDSurface s={.owner=&owner,.primary=TRUE,.width=4,.height=4,.pitch=16,.bytes_per_pixel=4,.handle=11,.memory=&memory,.shadow=shadow,.flip_next=(void*)1};next_probe=(D9WGWindowProbe){.client_succeeded=TRUE,.origin_succeeded=TRUE,.state={0,0,4,4}};
 for(unsigned chain=0;chain<2;chain++)for(unsigned primary=0;primary<2;primary++){
  s.primary=primary;s.flip_next=chain?(void*)1:NULL;s.lock_depth=1;s.lock_flags=0;s.locked_rect=(RECT){1,1,3,3};unsigned b=blits,p=presents,u=uploads;CHECK(!surface_Unlock(&s,NULL)&&uploads==u+1&&blits==b+primary&&presents==p+primary);if(primary)CHECK(last_blit.source_handle==11&&last_blit.destination_handle==0&&last_blit.source_rect[0]==1&&last_blit.destination_rect[3]==3);CHECK(last_upload.left==1&&last_upload.right==3);
  s.lock_depth=1;s.lock_flags=DDLOCK_READONLY;b=blits;p=presents;u=uploads;CHECK(!surface_Unlock(&s,NULL)&&blits==b&&presents==p&&uploads==u);
  s.dc=(void*)1;s.dib=(void*)2;s.old_bitmap=(void*)3;s.dib_bits=dib;b=blits;p=presents;u=uploads;CHECK(!surface_finish_dc(&s,TRUE)&&uploads==u+1&&blits==b+primary&&presents==p+primary&&!s.dc&&!s.dib&&!s.dib_bits&&!s.old_bitmap&&!memcmp(shadow,dib,64));
  s.dc=(void*)1;s.dib_bits=dib;b=blits;p=presents;CHECK(!surface_finish_dc(&s,FALSE)&&blits==b&&presents==p);
 }
 s.primary=TRUE;s.flip_next=(void*)1;unsigned p=presents;fail_blit=1;CHECK(!present_changed_primary(&s,NULL)&&presents==p);fail_present=1;CHECK(!present_changed_primary(&s,NULL)&&presents==p+1);CHECK(present_changed_primary(&s,NULL));fail_prepare=1;CHECK(!present_changed_primary(&s,NULL));fail_prepare=0;
 s.lock_depth=1;s.lock_flags=0;s.locked_rect=(RECT){0,0,4,4};fail_present=1;CHECK(surface_Unlock(&s,NULL)==DDERR_GENERIC&&!s.lock_depth);s.dc=(void*)1;s.dib_bits=dib;fail_blit=1;CHECK(surface_finish_dc(&s,TRUE)==DDERR_GENERIC&&!s.dc);return 0;
}
int main(void){CHECK(!areas());CHECK(!routes());printf("PASS actual DDraw primary writes: %u checks, 10800 window/dirty intersections, overflow and probe failures, primary/offscreen with/without flip chain, actual Unlock/DC routing and accepted-prefix presentation errors\\n",checks);return 0;}
`;
try {const file = dir + "/primary.c", binary = dir + "/primary"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
