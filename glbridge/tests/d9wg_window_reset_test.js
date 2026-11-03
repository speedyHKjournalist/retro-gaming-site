"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-reset-window-"));
try {
    fs.writeFileSync(dir + "/window.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned UINT,DWORD;typedef int BOOL;typedef intptr_t LONG_PTR,LRESULT,LPARAM;typedef uintptr_t WPARAM;typedef void*HANDLE;
typedef struct Window*HWND;typedef LRESULT(*WNDPROC)(HWND,UINT,WPARAM,LPARAM);
struct Window{BOOL live,unicode;DWORD thread;WNDPROC proc;HANDLE property;};
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define WM_SIZE 5
#define GWLP_WNDPROC (-4)
static unsigned fail_prop,fail_proc,error,called,forwarded,defaults,sets,properties,removes;static UINT last_message;static WPARAM last_wparam;static LPARAM last_lparam;
static BOOL IsWindow(HWND w){return w&&w->live;}
static BOOL IsWindowUnicode(HWND w){assert(IsWindow(w));return w->unicode;}
static DWORD GetCurrentThreadId(void){return 13;}
static DWORD GetWindowThreadProcessId(HWND w,DWORD*p){assert(IsWindow(w)&&!p);return w->thread;}
static HANDLE GetPropA(HWND w,const char*n){assert(IsWindow(w)&&!strcmp(n,"D9WG.ResetWindowScope.v1"));return w->property;}
static BOOL SetPropA(HWND w,const char*n,HANDLE p){(void)GetPropA(w,n);properties++;if(fail_prop){fail_prop--;return 0;}w->property=p;return 1;}
static HANDLE RemovePropA(HWND w,const char*n){HANDLE p=GetPropA(w,n);removes++;w->property=NULL;return p;}
static LONG_PTR get_proc(HWND w,int i,BOOL unicode){assert(IsWindow(w)&&i==GWLP_WNDPROC&&unicode==w->unicode);return (LONG_PTR)w->proc;}
static LONG_PTR GetWindowLongPtrA(HWND w,int i){return get_proc(w,i,0);}static LONG_PTR GetWindowLongPtrW(HWND w,int i){return get_proc(w,i,1);}
static LONG_PTR set_proc(HWND w,int i,LONG_PTR p,BOOL unicode){LONG_PTR old=get_proc(w,i,unicode);sets++;if(fail_proc){fail_proc--;error=123;return 0;}w->proc=(WNDPROC)p;return old;}
static LONG_PTR SetWindowLongPtrA(HWND w,int i,LONG_PTR p){return set_proc(w,i,p,0);}static LONG_PTR SetWindowLongPtrW(HWND w,int i,LONG_PTR p){return set_proc(w,i,p,1);}
static void SetLastError(DWORD v){error=v;}static DWORD GetLastError(void){return error;}
static LRESULT user_proc(HWND w,UINT m,WPARAM p,LPARAM l){assert(IsWindow(w));called++;last_message=m;last_wparam=p;last_lparam=l;return 73;}
static LRESULT new_proc(HWND w,UINT m,WPARAM p,LPARAM l){return user_proc(w,m,p,l)+1;}
static LRESULT forward(WNDPROC fn,HWND w,UINT m,WPARAM p,LPARAM l,BOOL unicode){assert(unicode==w->unicode&&fn);forwarded++;return fn(w,m,p,l);}
static LRESULT CallWindowProcA(WNDPROC f,HWND w,UINT m,WPARAM p,LPARAM l){return forward(f,w,m,p,l,0);}static LRESULT CallWindowProcW(WNDPROC f,HWND w,UINT m,WPARAM p,LPARAM l){return forward(f,w,m,p,l,1);}
static LRESULT DefWindowProcA(HWND w,UINT m,WPARAM p,LPARAM l){assert(!w->unicode);(void)m;(void)p;(void)l;defaults++;return 91;}
static LRESULT DefWindowProcW(HWND w,UINT m,WPARAM p,LPARAM l){assert(w->unicode);(void)m;(void)p;(void)l;defaults++;return 92;}
#include "${process.env.D9WG_WINDOW_RESET_HEADER || root + "/runtime/d9wg_window_reset.h"}"
/* A second DLL has a distinct callback address but the same scope ABI. */
static LRESULT other_dll_proc(HWND w,UINT m,WPARAM p,LPARAM l){return d9wg_reset_window_proc(w,m,p,l);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Reset window guard line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;for(unsigned unicode=0;unicode<2;unicode++){
 struct Window w={1,(BOOL)unicode,13,user_proc,NULL},other=w;D9WGWindowResetScope a,b;
 CHECK(d9wg_window_reset_begin(&a,&w)&&w.proc==d9wg_reset_window_proc&&w.property==&a);
 unsigned calls=called;CHECK(w.proc(&w,WM_SIZE,0x123,0x456)==0&&called==calls);
 WPARAM wp=UINTPTR_MAX-7;LPARAM lp=INTPTR_MIN+17;CHECK(w.proc(&w,0x404,wp,lp)==73&&called==calls+1&&last_message==0x404&&last_wparam==wp&&last_lparam==lp);
 CHECK(d9wg_window_reset_begin(&b,&w)&&b.forward_proc==user_proc&&b.previous_scope==&a);
 calls=called;CHECK(w.proc(&w,WM_SIZE,1,2)==0&&called==calls);CHECK(w.proc(&w,0x405,3,4)==73&&called==calls+1);
 d9wg_window_reset_end(&b);CHECK(w.property==&a&&w.proc==d9wg_reset_window_proc);d9wg_window_reset_end(&a);CHECK(!w.property&&w.proc==user_proc);
 unsigned old_sets=sets,old_removes=removes;d9wg_window_reset_end(&a);CHECK(sets==old_sets&&removes==old_removes);
 CHECK(w.proc(&w,WM_SIZE,0,0)==73);CHECK(other.proc==user_proc&&!other.property);
 CHECK(d9wg_window_reset_begin(&a,&w));w.proc=other_dll_proc;a.installed_proc=other_dll_proc;
 CHECK(d9wg_window_reset_begin(&b,&w)&&b.forward_proc==user_proc);CHECK(w.proc(&w,0x406,0,0)==73);
 d9wg_window_reset_end(&b);CHECK(w.proc==other_dll_proc&&w.property==&a);CHECK(w.proc(&w,WM_SIZE,0,0)==0);
 d9wg_window_reset_end(&a);CHECK(w.proc==user_proc&&!w.property);
 CHECK(d9wg_window_reset_begin(&a,&w));fail_proc=1;CHECK(!d9wg_window_reset_begin(&b,&w)&&w.property==&a&&w.proc==d9wg_reset_window_proc);
 d9wg_window_reset_end(&a);CHECK(!w.property&&w.proc==user_proc);
 fail_prop=1;CHECK(!d9wg_window_reset_begin(&a,&w)&&!w.property&&w.proc==user_proc);d9wg_window_reset_end(&a);
 fail_proc=1;CHECK(!d9wg_window_reset_begin(&a,&w)&&!w.property&&w.proc==user_proc);d9wg_window_reset_end(&a);
 w.thread=99;old_sets=sets;CHECK(!d9wg_window_reset_begin(&a,&w)&&sets==old_sets&&!w.property);w.thread=13;
 w.live=0;CHECK(!d9wg_window_reset_begin(&a,&w));w.live=1;w.proc=NULL;CHECK(!d9wg_window_reset_begin(&a,&w)&&!w.property);w.proc=user_proc;
 CHECK(d9wg_window_reset_begin(&a,&w));w.proc=new_proc;old_sets=sets;CHECK(!d9wg_window_reset_begin(&b,&w)&&sets==old_sets&&w.property==&a);
 d9wg_window_reset_end(&a);CHECK(w.proc==new_proc&&!w.property);w.proc=user_proc;
 CHECK(d9wg_window_reset_begin(&a,&w));w.live=0;d9wg_window_reset_end(&a);CHECK(!a.window);w.live=1;w.property=NULL;w.proc=user_proc;
 CHECK(d9wg_window_reset_begin(&a,NULL)&&!a.window);d9wg_window_reset_end(&a);
 CHECK(d9wg_reset_window_proc(&w,0x407,0,0)==(unicode?92:91));
}
printf("PASS Reset window guard: %u ANSI/Unicode checks; WM_SIZE suppression, exact forwarding, nested DLL scopes, rollback, changed/destroyed windows and owning thread\\n",checks);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/window.c", "-o", dir + "/window"], {encoding: "utf8"});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/window", [], {encoding: "utf8"});
    assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal);
    process.stdout.write(run.stdout);
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
