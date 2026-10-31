"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process"), { body } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), code = `
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <pthread.h>
#include <unistd.h>
typedef uint32_t DWORD;typedef int BOOL;typedef int32_t LONG;typedef void* HANDLE;typedef void* HMODULE;typedef void* LPVOID;typedef const wchar_t* LPCWSTR;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
static pthread_mutex_t g_api_lock=PTHREAD_MUTEX_INITIALIZER;static __thread int held;static LONG g_window_dirty;static int g_wgl_context_count=1;static void* g_current_hwnd=(void*)(uintptr_t)99;
typedef struct {BOOL used;} Context;static Context context={TRUE};static Context* g_loaded_context=&context;
static HANDLE g_window_dispatch_thread;static DWORD g_window_dispatch_thread_id;static unsigned updates,surfaces,processed,closed;static volatile int module_refs;static int fail_module,fail_thread;
typedef struct {pthread_t thread;DWORD(*entry)(LPVOID);LPVOID parameter;} Thread;static Thread threads[4];static unsigned thread_count;
static void EnterCriticalSection(pthread_mutex_t* lock){assert(!pthread_mutex_lock(lock));held++;}
static void LeaveCriticalSection(pthread_mutex_t* lock){assert(held>0);held--;assert(!pthread_mutex_unlock(lock));}
static void Sleep(DWORD ms){assert(ms==50);usleep(1000);}
static LONG InterlockedExchange(volatile LONG* p,LONG value){return __sync_lock_test_and_set(p,value);}
static void process_pending_window_changes(void){assert(held);processed++;}
static void emit_current_surface(void* hwnd){assert(held&&hwnd==g_current_hwnd);surfaces++;}
static void emit_window_state(void* hwnd,BOOL flush){assert(held&&hwnd==g_current_hwnd&&flush);updates++;}
static BOOL CloseHandle(HANDLE handle){assert(held&&handle);closed++;return TRUE;}
static BOOL GetModuleHandleExW(DWORD flags,LPCWSTR address,HMODULE* module){assert(held&&flags==4&&address);if(fail_module)return FALSE;*module=(HMODULE)(uintptr_t)1;__sync_add_and_fetch(&module_refs,1);return TRUE;}
static BOOL FreeLibrary(HMODULE module){assert(module==(HMODULE)(uintptr_t)1);__sync_sub_and_fetch(&module_refs,1);return TRUE;}
static void __attribute__((noreturn)) FreeLibraryAndExitThread(HMODULE module,DWORD code){assert(!held&&!code);FreeLibrary(module);pthread_exit(NULL);}
static void* launch(void* value){Thread* thread=value;return (void*)(uintptr_t)thread->entry(thread->parameter);}
static HANDLE CreateThread(void* security,size_t stack,DWORD(*entry)(LPVOID),LPVOID parameter,DWORD flags,DWORD* id){
 assert(held&&!security&&!stack&&!flags);if(fail_thread)return NULL;Thread* thread=&threads[thread_count++];thread->entry=entry;thread->parameter=parameter;*id=42+thread_count;assert(!pthread_create(&thread->thread,NULL,launch,thread));return thread;
}
${body(source, "window_dispatch_thread", "static DWORD WINAPI ")}
${body(source, "start_window_dispatcher", "static void ")}
static void wait_for_exit(void){for(int i=0;i<3000&&__sync_fetch_and_add(&module_refs,0);i++)usleep(1000);assert(!module_refs);}
int main(void){
 EnterCriticalSection(&g_api_lock);fail_module=1;start_window_dispatcher();assert(!g_window_dispatch_thread&&!module_refs);fail_module=0;fail_thread=1;start_window_dispatcher();assert(!g_window_dispatch_thread&&!module_refs&&!g_window_dispatch_thread_id);fail_thread=0;LeaveCriticalSection(&g_api_lock);
 for(int pass=0;pass<2;pass++){
  EnterCriticalSection(&g_api_lock);g_wgl_context_count=1;start_window_dispatcher();assert(g_window_dispatch_thread&&module_refs==1);start_window_dispatcher();assert(module_refs==1);Thread* active=g_window_dispatch_thread;unsigned before=updates;LeaveCriticalSection(&g_api_lock);
  BOOL progressed=FALSE;for(int i=0;i<3000;i++){usleep(1000);EnterCriticalSection(&g_api_lock);progressed=updates>before;LeaveCriticalSection(&g_api_lock);if(progressed)break;}assert(progressed);
  EnterCriticalSection(&g_api_lock);g_wgl_context_count=0;LeaveCriticalSection(&g_api_lock);wait_for_exit();assert(!pthread_join(active->thread,NULL));assert(!g_window_dispatch_thread&&!g_window_dispatch_thread_id);
 }
 assert(closed==2&&updates>=2&&surfaces==updates&&processed>=updates);return 0;
}`;
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-window-dispatcher-"));
try {
    const file = path.join(directory, "test.c"), binary = path.join(directory, "test");fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C window dispatcher: serialized idle updates, DLL reference, start failures, final-context exit and sequential restart");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
