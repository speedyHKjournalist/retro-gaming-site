"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-reset-mode-"));
function extract(source, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) { if (source[end] === "{") depth++; if (source[end] === "}") depth--; }
    return source.slice(m.index, end);
}
try {
    for (const api of [8]) {
    const source = readComparisonSource(process.env["D" + api + "WG_RESET_MODE_FRONTEND"] || root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
    fs.writeFileSync(dir + "/mode.c", `#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef int BOOL;typedef uint32_t UINT,DWORD,LONG;typedef struct Window*HWND;
struct Window{BOOL live,iconic;LONG style,exstyle;unsigned guarded,x,y,width,height,thread;};
typedef struct{UINT dmSize,dmPelsWidth,dmPelsHeight,dmBitsPerPel,dmFields;}DEVMODEA;
typedef struct{struct{BOOL Windowed;}present;struct{UINT Width,Height;}display_mode;struct{HWND hFocusWindow;}creation;BOOL display_mode_changed;HWND styled_window;LONG saved_window_style,saved_window_exstyle;DWORD last_foreground_claim;void*exclusive_window;int reset_state;}D9Device;typedef D9Device D8Device;
typedef struct{HWND window;}D9WGWindowResetScope;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define DM_PELSWIDTH 1
#define DM_PELSHEIGHT 2
#define DM_BITSPERPEL 4
#define CDS_FULLSCREEN 4
#define DISP_CHANGE_SUCCESSFUL 0
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define WS_CAPTION 0x00c00000u
#define WS_THICKFRAME 0x00040000u
#define WS_BORDER 0x00800000u
#define WS_DLGFRAME 0x00400000u
#define WS_MINIMIZEBOX 0x00020000u
#define WS_MAXIMIZEBOX 0x00010000u
#define WS_SYSMENU 0x00080000u
#define WS_POPUP 0x80000000u
#define WS_EX_CLIENTEDGE 0x200u
#define WS_EX_WINDOWEDGE 0x100u
#define WS_EX_DLGMODALFRAME 1u
#define WS_EX_STATICEDGE 0x20000u
#define SW_RESTORE 9
#define SWP_SHOWWINDOW 0x40u
#define SWP_FRAMECHANGED 0x20u
#define SWP_NOMOVE 2u
#define SWP_NOSIZE 1u
#define SWP_NOZORDER 4u
#define SWP_NOACTIVATE 0x10u
#define HWND_TOP ((HWND)(uintptr_t)1)
static unsigned guard_calls,end_calls,fail_guard_at,mode_calls,restore_calls,raises,sizes,fail32,fail_all,fail_restore;
static HWND required_focus,foreground,active_window,focus_window;static unsigned attach_attempts,detaches,input_attached,fail_attach;
static UINT mode_width=1024,mode_height=768,mode_bits=32;
static BOOL IsWindow(HWND w){return w&&w->live;}
static BOOL IsIconic(HWND w){assert(IsWindow(w));return w->iconic;}
static void ShowWindow(HWND w,UINT how){assert(IsWindow(w)&&how==SW_RESTORE&&w->guarded);w->iconic=0;}
static LONG GetWindowLongA(HWND w,int i){assert(IsWindow(w));return i==GWL_STYLE?w->style:w->exstyle;}
static LONG SetWindowLongA(HWND w,int i,LONG value){LONG old=GetWindowLongA(w,i);assert(w->guarded);if(i==GWL_STYLE)w->style=value;else w->exstyle=value;return old;}
static void SetWindowPos(HWND w,HWND order,int x,int y,int width,int height,UINT flags){assert(IsWindow(w));if(!(flags&SWP_NOMOVE)){w->x=x;w->y=y;}if(!(flags&SWP_NOSIZE)){assert(order==HWND_TOP);w->width=width;w->height=height;}if(!w->guarded)sizes++;}
static LONG ChangeDisplaySettingsA(DEVMODEA*m,DWORD flags){
 if(required_focus)assert(required_focus->guarded);
 if(!m){assert(!flags);restore_calls++;if(fail_restore)return 1;mode_width=1024;mode_height=768;mode_bits=32;return 0;}
 assert(flags==CDS_FULLSCREEN&&m->dmSize==sizeof(*m)&&m->dmFields==7);mode_calls++;
 if(fail_all||(fail32&&m->dmBitsPerPel==32))return 1;mode_width=m->dmPelsWidth;mode_height=m->dmPelsHeight;mode_bits=m->dmBitsPerPel;return 0;
}
static HWND GetForegroundWindow(void){return foreground;}
static DWORD GetCurrentThreadId(void){return 13;}
static DWORD GetWindowThreadProcessId(HWND w,DWORD*p){assert(!p);return IsWindow(w)?w->thread:0;}
static BOOL AttachThreadInput(DWORD a,DWORD b,BOOL attach){assert(a==13&&b==99);if(attach){attach_attempts++;if(fail_attach)return 0;assert(!input_attached);input_attached=1;}else{assert(input_attached);input_attached=0;detaches++;}return 1;}
static void BringWindowToTop(HWND w){assert(IsWindow(w)&&w->guarded);raises++;}
static void SetForegroundWindow(HWND w){assert(IsWindow(w)&&w->guarded);foreground=w;}
static void SetActiveWindow(HWND w){assert(IsWindow(w)&&w->guarded);active_window=w;}
static void SetFocus(HWND w){assert(IsWindow(w)&&w->guarded);focus_window=w;}
static DWORD GetTickCount(void){return 1234;}
static BOOL d9wg_window_reset_begin(D9WGWindowResetScope*s,HWND w){*s=(D9WGWindowResetScope){0};guard_calls++;if(guard_calls==fail_guard_at)return 0;if(w){if(!IsWindow(w))return 0;assert(!w->guarded);w->guarded++;s->window=w;}return 1;}
static void d9wg_window_reset_end(D9WGWindowResetScope*s){end_calls++;if(s->window){assert(s->window->guarded==1);s->window->guarded--;s->window=NULL;}}
#include "${process.env.D9WG_WINDOW_MODE_HEADER || root + "/runtime/d9wg_window_mode.h"}"
/* Ordinary9 retains its existing mode policy; API8 exclusive routing is
 * exercised by d9wg_exclusive_window_test and live XP window-runtime. */
static UINT device_compatibility_api(const D9Device*d){(void)d;return 9;}
static void d9wg_exclusive_window_leave(void **s){(void)s;assert(0);}
static BOOL d9wg_exclusive_window_enter(void**s,HWND f,HWND w,UINT x,UINT y,void*state){(void)s;(void)f;(void)w;(void)x;(void)y;(void)state;assert(0);return 0;}
${["window_mode_bindings", "restore_display_mode", "reset_window_mode"].map(n => extract(source, n)).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Reset mode entry line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;LONG style=WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU,exstyle=WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME|WS_EX_STATICEDGE;
 struct Window a={.live=1,.style=style,.exstyle=exstyle,.iconic=1,.thread=13},b=a,c=a,foreign=a;foreign.thread=99;foreground=&foreign;required_focus=&a;D9Device d={.present={1},.creation={&a},.display_mode={640,480}};
 CHECK(reset_window_mode(&d,&a)&&!guard_calls&&!mode_calls&&!raises);
 d.present.Windowed=0;CHECK(reset_window_mode(&d,&a));CHECK(mode_width==640&&mode_height==480&&mode_bits==32&&d.display_mode_changed&&d.styled_window==&a);
 CHECK(d.saved_window_style==style&&d.saved_window_exstyle==exstyle&&a.style==WS_POPUP&&!a.exstyle&&!a.iconic&&a.width==640&&a.height==480&&!a.x&&!a.y);
 CHECK(raises==1&&d.last_foreground_claim==1234&&!a.guarded&&!b.guarded&&!sizes);
 CHECK(attach_attempts==1&&detaches==1&&!input_attached&&foreground==&a&&active_window==&a&&focus_window==&a);
 d.display_mode.Width=800;d.display_mode.Height=600;CHECK(reset_window_mode(&d,&a));CHECK(mode_width==800&&mode_height==600&&a.width==800&&a.height==600&&d.saved_window_style==style&&d.saved_window_exstyle==exstyle&&!a.guarded&&!sizes);
 d.display_mode.Width=640;d.display_mode.Height=480;CHECK(reset_window_mode(&d,&b));CHECK(a.style==style&&a.exstyle==exstyle&&b.style==WS_POPUP&&!b.exstyle&&d.styled_window==&b&&d.saved_window_style==style&&mode_width==640&&!a.guarded&&!b.guarded&&!sizes);
 d.present.Windowed=1;CHECK(reset_window_mode(&d,&b));CHECK(!d.styled_window&&!d.display_mode_changed&&b.style==style&&b.exstyle==exstyle&&mode_width==1024&&mode_height==768&&!a.guarded&&!b.guarded&&!sizes&&restore_calls==1);
 unsigned before_guards=guard_calls;CHECK(reset_window_mode(&d,&b)&&guard_calls==before_guards);
 d.present.Windowed=0;fail32=1;unsigned before_modes=mode_calls;CHECK(reset_window_mode(&d,&a));CHECK(mode_calls==before_modes+2&&mode_bits==16&&d.display_mode_changed&&!sizes);fail32=0;
 fail_all=1;CHECK(!reset_window_mode(&d,&b));CHECK(d.display_mode_changed&&mode_bits==16&&mode_width==640&&!a.guarded&&!b.guarded&&!sizes);fail_all=0;
 d.present.Windowed=1;fail_restore=1;CHECK(!reset_window_mode(&d,&b)&&d.display_mode_changed&&!a.guarded&&!b.guarded);fail_restore=0;CHECK(reset_window_mode(&d,&b)&&!d.display_mode_changed&&mode_width==1024);
 for(unsigned i=1;i<=3;i++){
  d=(D9Device){.present={0},.creation={&c},.display_mode={800,600},.display_mode_changed=1,.styled_window=&a,.saved_window_style=style,.saved_window_exstyle=exstyle};D9Device before=d;
  fail_guard_at=guard_calls+i;unsigned old_modes=mode_calls,old_ends=end_calls;CHECK(!reset_window_mode(&d,&b));CHECK(!memcmp(&d,&before,sizeof(d))&&mode_calls==old_modes&&end_calls==old_ends+i-1&&!a.guarded&&!b.guarded&&!c.guarded);fail_guard_at=0;
 }
 d=(D9Device){.creation={&a},.display_mode={800,600}};CHECK(reset_window_mode(&d,NULL)&&d.display_mode_changed&&!d.styled_window&&!a.guarded);
 unsigned before_attaches=attach_attempts,before_detaches=detaches;foreground=NULL;
 CHECK(reset_window_mode(&d,&b)&&attach_attempts==before_attaches&&!input_attached&&foreground==&b&&active_window==&b&&focus_window==&b);
 foreground=&foreign;fail_attach=1;
 CHECK(reset_window_mode(&d,&a)&&attach_attempts==before_attaches+1&&detaches==before_detaches&&!input_attached&&foreground==&a);
 foreground=&foreign;fail_attach=0;before_attaches=attach_attempts;before_detaches=detaches;
 CHECK(reset_window_mode(&d,&b)&&attach_attempts==before_attaches+1&&detaches==before_detaches+1&&!input_attached&&foreground==&b);
 printf("PASS actual API ${api} Reset mode entry: %u checks; window/fullscreen/resize/replacement/restore, saved styles, fallback/failure, foreground, three-window guards and complete unwind\\n",checks);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers", "-Wno-unused-function", dir + "/mode.c", "-o", dir + "/mode"], {encoding: "utf8"});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/mode", [], {encoding: "utf8"});
    assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
    }
} finally { fs.rmSync(dir, {recursive: true, force: true}); }

// The live core now uses native focus subscriptions for both APIs.
require("./d9wg_exclusive_mode_entry_test.js");
