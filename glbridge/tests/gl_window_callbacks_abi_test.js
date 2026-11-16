"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process"), { body } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), code = `
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef int BOOL;typedef uint32_t DWORD;typedef int32_t LONG;typedef void* HWND;typedef void* HWINEVENTHOOK;
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
static volatile LONG g_window_dirty,g_destroyed_windows[64];static HWND g_current_hwnd;static BOOL g_have_last_surface;
static unsigned watches,unwatches,starts;
static LONG InterlockedExchange(volatile LONG* p,LONG v){return __sync_lock_test_and_set(p,v);}
static LONG InterlockedCompareExchange(volatile LONG* p,LONG v,LONG expected){return __sync_val_compare_and_swap(p,expected,v);}
static void watch_window_state(void){watches++;}
static void unwatch_window_state(void){unwatches++;}
static void start_window_dispatcher(void){starts++;}
${body(source, "mark_window_change", "static void ")}
${body(source, "window_event", "static void CALLBACK ")}
${body(source, "unwatch_current_window", "static void ")}
${body(source, "watch_current_window", "static void ")}
static void* events(void* value){for(int i=0;i<1000;i++)mark_window_change((HWND)value,TRUE);return NULL;}
int main(void){
 HWND hwnd=(HWND)(uintptr_t)99;watch_current_window(hwnd);assert(g_current_hwnd==hwnd&&g_window_dirty&&watches==1&&unwatches==1&&starts==1);g_window_dirty=0;
 watch_current_window(hwnd);assert(!g_window_dirty&&watches==1&&unwatches==1&&starts==1);
 watch_current_window((HWND)(uintptr_t)100);assert(g_current_hwnd==(HWND)(uintptr_t)100&&g_window_dirty&&watches==2&&unwatches==2&&starts==2);g_window_dirty=0;
 g_have_last_surface=TRUE;unwatch_current_window();assert(!g_current_hwnd&&!g_have_last_surface&&unwatches==3);
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
    // No window-procedure or transport API is supplied: calling one is an error.
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C window monitoring: concurrent deferred events, window switches and unwatch without touching application Unicode/ANSI procedure chains or transport");
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
