"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process"), { body } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), code = `
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <pthread.h>
typedef int BOOL;typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef void* HWND;typedef void* HANDLE;typedef void* HWINEVENTHOOK;typedef uintptr_t WPARAM;typedef intptr_t LPARAM,LRESULT;typedef LRESULT(*WNDPROC)(HWND,UINT,WPARAM,LPARAM);
#define CALLBACK
#define FALSE 0
#define TRUE 1
#define V86GL_DESTROY_QUEUE_SIZE 64
#define EVENT_SYSTEM_FOREGROUND 3
#define EVENT_OBJECT_SHOW 0x8002
#define EVENT_OBJECT_HIDE 0x8003
#define EVENT_OBJECT_DESTROY 0x8001
#define EVENT_OBJECT_REORDER 0x8004
#define EVENT_OBJECT_LOCATIONCHANGE 0x800B
#define OBJID_WINDOW 0
#define CHILDID_SELF 0
#define WM_MOVE 3
#define WM_SIZE 5
#define WM_WINDOWPOSCHANGED 0x47
#define WM_SHOWWINDOW 0x18
#define WM_NCDESTROY 0x82
static volatile LONG g_window_dirty,g_destroyed_windows[64];static unsigned original_calls,removed;
static LONG InterlockedExchange(volatile LONG* p,LONG v){return __sync_lock_test_and_set(p,v);}
static LONG InterlockedCompareExchange(volatile LONG* p,LONG v,LONG expected){return __sync_val_compare_and_swap(p,expected,v);}
static LRESULT original(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){(void)hwnd;(void)wp;(void)lp;original_calls++;return 100+msg;}
static HANDLE GetPropW(HWND hwnd,const wchar_t* key){(void)hwnd;assert(!wcscmp(key,L"V86GL.OriginalWndProc"));return (HANDLE)original;}
static HANDLE RemovePropW(HWND hwnd,const wchar_t* key){(void)hwnd;(void)key;removed++;return NULL;}
static LRESULT CallWindowProcW(WNDPROC proc,HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){return proc(hwnd,msg,wp,lp);}
static LRESULT DefWindowProcW(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){(void)hwnd;(void)msg;(void)wp;(void)lp;return 0;}
${body(source, "mark_window_change", "static void ")}
${body(source, "window_event", "static void CALLBACK ")}
${body(source, "vgl_window_proc", "static LRESULT CALLBACK ")}
static void* events(void* value){for(int i=0;i<1000;i++)mark_window_change((HWND)value,TRUE);return NULL;}
int main(void){
 HWND hwnd=(HWND)(uintptr_t)99;assert(vgl_window_proc(hwnd,WM_MOVE,0,0)==103&&g_window_dirty&&original_calls==1);g_window_dirty=0;
 assert(vgl_window_proc(hwnd,0x400,0,0)==1124&&!g_window_dirty&&original_calls==2);
 assert(vgl_window_proc(hwnd,WM_NCDESTROY,0,0)==230&&removed==1&&g_window_dirty);g_window_dirty=0;
 window_event(NULL,EVENT_OBJECT_SHOW,hwnd,1,0,0,0);assert(!g_window_dirty);
 window_event(NULL,EVENT_OBJECT_DESTROY,hwnd,0,0,0,0);assert(g_window_dirty);g_window_dirty=0;
 window_event(NULL,EVENT_SYSTEM_FOREGROUND,NULL,1,1,0,0);assert(g_window_dirty);
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,events,(void*)(i+100)));for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 for(int hwnd_id=99;hwnd_id<=103;hwnd_id++){BOOL found=FALSE;for(int i=0;i<64;i++)if(g_destroyed_windows[i]==hwnd_id)found=TRUE;assert(found);}
 return 0;
}`;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-window-callbacks-"));
try {
    const file = path.join(dir, "test.c"), binary = path.join(dir, "test");fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C window callbacks: Unicode predecessor, concurrent deferred destruction and visibility flags, no transport or context mutations");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
