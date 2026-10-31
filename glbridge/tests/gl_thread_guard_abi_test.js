"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
function type(name) { const match = source.match(new RegExp("typedef struct \\{[^{}]*\\} " + name + ";"));assert.ok(match, name);return match[0]; }
const guard = source.match(/^#define V86GL_API_CALL\(\).*$/m)[0];
const code = `
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "gl_protocol.h"
typedef uint32_t GLenum,GLuint,GLbitfield,DWORD,UINT;typedef int32_t GLint,GLsizei,LONG;typedef int BOOL;typedef unsigned char GLboolean;typedef float GLfloat;typedef double GLdouble;typedef void GLvoid;typedef void* HANDLE;typedef void* HWND;typedef void* HDC;typedef void* HGLRC;typedef uintptr_t WPARAM;typedef intptr_t LPARAM,LRESULT;typedef void* HWINEVENTHOOK;typedef LRESULT(*WNDPROC)(HWND,UINT,WPARAM,LPARAM);
#define APIENTRY
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define TLS_OUT_OF_INDEXES UINT32_MAX
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_SUCCESS 0
#define ERROR_BUSY 170
#define SYNCHRONIZE 0x100000u
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define V86GL_MAX_WGL_CONTEXTS 32
#define V86GL_MAX_BUFFER_OBJECTS 4096
#define V86GL_MAX_TEXTURE_UNITS 8
#define V86GL_MAX_VERTEX_ATTRIBS 16
#define V86GL_PROGRAM_ERROR_STRING_MAX 4096
#define V86GL_ATTRIB_STACK_DEPTH 16
#define V86GL_DESTROY_QUEUE_SIZE 64
#define V86GL_DIAG(...) ((void)0)
#define V86GL_DIAG_RECORD(a,b) ((void)0)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() 0
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static __thread DWORD thread_id,last_error;static __thread void* tls_context;static __thread int api_held,tls_fail;
static DWORD GetCurrentThreadId(void){return thread_id;}
static DWORD GetLastError(void){return last_error;}
static void SetLastError(DWORD value){last_error=value;}
static void* TlsGetValue(DWORD index){(void)index;return tls_context;}
static BOOL TlsSetValue(DWORD index,void* value){(void)index;if(tls_fail){tls_fail=0;return FALSE;}tls_context=value;return TRUE;}
typedef struct {pthread_mutex_t mutex;int api;} CRITICAL_SECTION;
static void EnterCriticalSection(CRITICAL_SECTION* cs){assert(!pthread_mutex_lock(&cs->mutex));if(cs->api)api_held++;}
static void LeaveCriticalSection(CRITICAL_SECTION* cs){if(cs->api){assert(api_held>0);api_held--;}assert(!pthread_mutex_unlock(&cs->mutex));}
static HANDLE OpenThread(DWORD access,BOOL inherit,DWORD id){(void)access;(void)inherit;(void)id;return NULL;}
static BOOL CloseHandle(HANDLE handle){(void)handle;return TRUE;}
static DWORD WaitForSingleObject(HANDLE handle,DWORD time){(void)handle;(void)time;return WAIT_TIMEOUT;}
static HWND WindowFromDC(HDC dc){return (HWND)dc;}
static LONG InterlockedExchange(volatile LONG* p,LONG value){return __sync_lock_test_and_set(p,value);}
static LONG InterlockedCompareExchange(volatile LONG* p,LONG value,LONG expected){return __sync_val_compare_and_swap(p,expected,value);}
${type("V86GLWGLContext")}
static V86GLWGLContext g_wgl_contexts[32],*g_loaded_context;
static uint32_t g_wgl_context_count=2,g_api_depth;static DWORD g_context_tls,g_loaded_thread;
static CRITICAL_SECTION g_api_lock,g_wgl_context_lock;static BOOL g_api_lock_initialized=TRUE,g_wgl_context_lock_initialized=TRUE;
typedef struct {BOOL held;} V86GLAPIGuard;
static V86GLAPIGuard v86gl_enter_api(void);static void v86gl_leave_api(V86GLAPIGuard* guard);static V86GLWGLContext* v86gl_current_context(void);
${guard}
static volatile LONG g_window_dirty=1,g_destroyed_windows[64];static HWND g_current_hwnd;static BOOL g_have_last_surface,g_context_destroy_sent,g_renderer_lifecycle_started;
static void hook_window(HWND hwnd){g_current_hwnd=hwnd;}
static void restore_window_proc(void){g_current_hwnd=NULL;g_have_last_surface=FALSE;}
static void emit_window_state(HWND hwnd,BOOL flush){(void)hwnd;(void)flush;}
static V86GLWGLContext* find_wgl_context(HGLRC handle);static void force_release_window_contexts(HWND hwnd);
${transport.slice(0, transport.indexOf("static int reserve_pci_record"))}
static void flush_immediate_vertices(void);
static uint8_t arena[262144],*g_dma_buffer=arena;static uint32_t g_dma_capacity=sizeof(arena),g_dma_size,g_dma_command_count,g_frame_id;
typedef struct {uint32_t words[8];} V86GLDMADesc;
static int open_v86gl(void){assert(api_held);return 1;}
static void v86gl_error(const char* text,...){(void)text;}
static void v86gl_trace(const char* text,...){(void)text;}
static int v86gl_trace_call_allowed(void){return 0;}
static void write_u32le(uint8_t* p,uint32_t value){memcpy(p,&value,4);}
static int emit_pci_batch(BOOL present){
 (void)present;assert(api_held);if(!g_dma_size)return 1;
 wire_size=g_dma_size;memcpy(wire,g_dma_buffer+sizeof(V86GLDMADesc),wire_size);
 assert(fwrite(&wire_size,4,1,stdout)==1);assert(fwrite(wire,1,wire_size,stdout)==wire_size);fflush(stdout);assert(fread(wire,1,wire_size,stdin)==wire_size);memcpy(g_dma_buffer+sizeof(V86GLDMADesc),wire,wire_size);
 g_dma_size=g_dma_command_count=0;return 1;
}
${body(source, "reserve_pci_record", "static int ")}
${body(source, "emit_pci_record", "static int ")}
static BOOL g_immediate_active;static uint8_t g_immediate_bytes[V86GL_IMMEDIATE_BLOCK_BYTES];static uint32_t g_immediate_size,g_immediate_runs,g_immediate_run;
${body(source, "flush_immediate_vertices", "static void ")}
${body(source, "buffer_immediate_call", "static BOOL ")}
static void emit_gl_large_call(uint16_t fn,const void* data,uint32_t size){(void)fn;(void)data;(void)size;abort();}
${body(source, "emit_gl_call", "static void ")}
#undef release_locked_client_arrays
static void release_locked_client_arrays(void){}
${type("ClientArrayState")}
${type("GenericAttribState")}
${type("ClientAttribSnapshot")}
${type("GuestContextState")}
static GLenum g_client_active_texture=GL_TEXTURE0_ARB,g_render_mode=GL_RENDER;
static GLuint g_array_buffer_binding,g_element_array_buffer_binding,g_pixel_pack_buffer_binding,g_pixel_unpack_buffer_binding;
static GLint g_unpack_alignment=4,g_unpack_row_length,g_unpack_skip_rows,g_unpack_skip_pixels,g_unpack_image_height,g_unpack_skip_images,g_pack_alignment=4,g_pack_row_length,g_pack_skip_rows,g_pack_skip_pixels,g_pack_image_height,g_pack_skip_images,g_array_lock_first;
static GLsizei g_array_lock_count;
static ClientArrayState g_vertex_array,g_color_array,g_index_array,g_normal_array,g_edge_flag_array,g_secondary_color_array,g_fog_coord_array,g_texcoord_arrays[8];static GenericAttribState g_generic_attribs[16];
static ClientAttribSnapshot g_client_attrib_stack[16];static GLint g_client_attrib_stack_depth;static char g_program_error_string[4096];
static GuestContextState g_default_guest_context_state;static BOOL g_default_guest_context_state_valid;
${body(source, "capture_client_attrib", "static void ")}
${body(source, "restore_client_attrib", "static void ")}
${body(source, "capture_guest_context_state", "static void ")}
${body(source, "restore_guest_context_state", "static void ")}
${body(source, "ensure_guest_context_state", "static GuestContextState* ")}
${body(source, "save_guest_context_state", "static BOOL ")}
${body(source, "load_guest_context_state", "static BOOL ")}
${body(source, "find_wgl_context", "static V86GLWGLContext* ")}
${body(source, "v86gl_current_context", "static V86GLWGLContext* ")}
${body(source, "clear_wgl_context_binding", "static void ")}
${body(source, "reap_terminated_wgl_contexts", "static void ")}
${body(source, "find_thread_current_context", "static V86GLWGLContext* ")}
${body(source, "find_any_current_context", "static V86GLWGLContext* ")}
${body(source, "release_thread_current_context", "static BOOL ")}
${body(source, "force_release_window_contexts", "static void ")}
${body(source, "can_bind_wgl_context", "static BOOL ")}
${body(source, "validate_wgl_context", "static BOOL ")}
typedef struct {LONG x,y;} POINT;typedef struct {LONG left,top,right,bottom;} RECT;
static BOOL GetClientRect(HWND hwnd,RECT* r){(void)hwnd;*r=(RECT){0,0,16,16};return TRUE;}
static BOOL ClientToScreen(HWND hwnd,POINT* p){(void)hwnd;p->x=p->y=0;return TRUE;}
static uint32_t window_format_flags(HWND hwnd){(void)hwnd;return 0;}
static uint32_t current_caps_profile(void){return 5;}
static DWORD g_window_dispatch_thread_id;
static LONG g_last_surface_x,g_last_surface_y;static uint32_t g_last_surface_width,g_last_surface_height,g_last_surface_context_id,g_last_surface_share_group,g_last_surface_format;
${body(source, "emit_current_surface", "static void ")}
${body(source, "process_pending_window_changes", "static void ")}
${body(source, "v86gl_enter_api", "static V86GLAPIGuard ")}
${body(source, "v86gl_leave_api", "static void ")}
${type("BufferObjectState")}
static BufferObjectState g_buffer_objects[4096];
${body(source, "current_buffer_scope", "static uint32_t ")}
${body(source, "find_buffer_state", "static BufferObjectState* ")}
${body(source, "release_buffer_group", "static void ")}
${body(source, "wglMakeCurrent", "BOOL APIENTRY ")}
${body(source, "wglGetCurrentContext", "HGLRC APIENTRY ")}
${body(source, "wglGetCurrentDC", "HDC APIENTRY ")}
${body(source, "glBegin", "void APIENTRY ")}
${body(source, "glEnd", "void APIENTRY ")}
${body(source, "glColor4f", "void APIENTRY ")}
${body(source, "glVertex4f", "void APIENTRY ")}
static pthread_mutex_t turns=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;static int ready,turn=1;
static void query_color(int id){V86GL_API_CALL();V86GLRecordQueryState q={0};q.pname=GL_CURRENT_COLOR;q.value_kind=V86GL_STATE_DOUBLE;q.data_size=32;uint8_t* p;
 assert(reserve_pci_record(GLFN_QUERY_STATE,NULL,sizeof(q)+32,&p));memcpy(p,&q,sizeof(q));assert(emit_pci_batch(0));assert(read_u32le(p+offsetof(V86GLRecordQueryState,status))==1);double color[4];memcpy(color,p+sizeof(q),32);assert(color[0]==(id==1)&&color[1]==(id==2)&&color[2]==0&&color[3]==1);
}
static void vertex_step(int id){V86GL_API_CALL();assert(wglGetCurrentContext()==(HGLRC)&g_wgl_contexts[id-1]&&wglGetCurrentDC()==(HDC)(uintptr_t)99);assert(g_vertex_array.pointer==(const void*)(uintptr_t)(id*1024));assert(g_array_lock_count==id+10);BufferObjectState* b=find_buffer_state(7,FALSE);assert(b&&b->size==(uint32_t)(id*100));glVertex4f(id==1?-1:2,0,0,id==1?2:4);}
static void* draw_thread(void* value){int id=(int)(uintptr_t)value;thread_id=(DWORD)id;
 {V86GL_API_CALL();assert(wglMakeCurrent((HDC)(uintptr_t)99,(HGLRC)&g_wgl_contexts[id-1]));g_vertex_array.pointer=(const void*)(uintptr_t)(id*1024);g_array_lock_count=id+10;BufferObjectState* b=find_buffer_state(7,TRUE);assert(b);b->size=(uint32_t)id*100;glColor4f(id==1,id==2,0,1);glBegin(GL_POINTS);}
 pthread_mutex_lock(&turns);ready++;pthread_cond_broadcast(&changed);while(ready<2)pthread_cond_wait(&changed,&turns);pthread_mutex_unlock(&turns);
 for(int i=0;i<64;i++){pthread_mutex_lock(&turns);while(turn!=id)pthread_cond_wait(&changed,&turns);vertex_step(id);turn=3-id;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&turns);}
 glEnd();query_color(id);
 // Failed TLS binding must leave the old context current and owned.
 tls_fail=1;assert(!wglMakeCurrent((HDC)(uintptr_t)99,(HGLRC)&g_wgl_contexts[id-1]));assert(wglGetCurrentContext()==(HGLRC)&g_wgl_contexts[id-1]);
 assert(wglMakeCurrent(NULL,NULL));assert(!wglGetCurrentContext()&&!wglGetCurrentDC());return NULL;
}
int main(void){pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));assert(!pthread_mutex_init(&g_api_lock.mutex,&attr));assert(!pthread_mutex_init(&g_wgl_context_lock.mutex,&attr));g_api_lock.api=1;
 capture_guest_context_state(&g_default_guest_context_state);g_default_guest_context_state_valid=TRUE;for(int i=0;i<2;i++){g_wgl_contexts[i].used=TRUE;g_wgl_contexts[i].share_group=(uint32_t)i+1;assert(ensure_guest_context_state(&g_wgl_contexts[i]));}
 pthread_t a,b;assert(!pthread_create(&a,NULL,draw_thread,(void*)(uintptr_t)1));assert(!pthread_create(&b,NULL,draw_thread,(void*)(uintptr_t)2));assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(!g_api_depth&&!g_dma_size&&g_wgl_context_count==2);
 int mappings=0;for(int i=0;i<4096;i++)if(g_buffer_objects[i].used)mappings++;assert(mappings==2);release_buffer_group(2);mappings=0;for(int i=0;i<4096;i++)if(g_buffer_objects[i].used)mappings++;assert(mappings==2);g_wgl_contexts[0].used=FALSE;release_buffer_group(1);assert(!g_buffer_objects[0].used||g_buffer_objects[0].share_group==2);mappings=0;for(int i=0;i<4096;i++)if(g_buffer_objects[i].used){assert(g_buffer_objects[i].share_group==2);mappings++;}assert(mappings==1);return 0;
}`;
run(code, (cpu, gpu) => {
    assert.equal(gpu.threadVertices.length, 128);
    for (const vertex of gpu.threadVertices) { const first = vertex.position[0] === -1;assert.deepEqual(vertex.position, first ? [-1, 0, 0, 2] : [2, 0, 0, 4]);assert.deepEqual(vertex.color, first ? [1, 0, 0, 1] : [0, 1, 0, 1]); }
    assert.equal(cpu.current, null);assert.equal(gpu.current, null);for (const c of cpu.contexts.values()) assert.equal(c.error, 0);
}, (cpu, gpu) => { gpu.threadVertices = [];const original = gpu.immediateVertex;gpu.immediateVertex = function(...position) { gpu.threadVertices.push({ position, color: [...this.current.current.color] });return original.apply(this, position); }; })
    .then(() => console.log("PASS actual C thread guard: TLS contexts on one HDC, 128 forced open-primitive switches, nested calls, client workspace/map isolation, query replies and TLS failure rollback in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
