"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, run } = require("./gl_cpu_abi_harness.js"), { code: base } = require("./gl_thread_guard_abi_test.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = "#define GL_THREAD_FIXTURE_NO_MAIN\n" + base + `
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_INVALID_PARAMETER 87
static uint32_t g_next_share_group=1;
${body(source, "wglCreateContext", "HGLRC APIENTRY ")}
${body(source, "wglCreateLayerContext", "HGLRC APIENTRY ")}
${body(source, "wglDeleteContext", "BOOL APIENTRY ")}
${body(source, "wglShareLists", "BOOL APIENTRY ")}
${body(source, "wglCopyContext", "BOOL APIENTRY ")}
int main(void){thread_id=3;g_wgl_context_count=0;pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));assert(!pthread_mutex_init(&g_api_lock.mutex,&attr));assert(!pthread_mutex_init(&g_wgl_context_lock.mutex,&attr));g_api_lock.api=1;
 capture_guest_context_state(&g_default_guest_context_state);g_default_guest_context_state_valid=TRUE;HDC dc=(HDC)(uintptr_t)99;
 assert(!wglCreateLayerContext(dc,1)&&GetLastError()==ERROR_INVALID_PARAMETER);
 HGLRC a=wglCreateContext(dc),b=wglCreateContext(dc),c=wglCreateContext(dc),d=wglCreateContext((HDC)(uintptr_t)200),e=wglCreateContext(dc),f=wglCreateLayerContext(dc,0);assert(a&&b&&c&&d&&e&&f);
 assert(wglShareLists(b,c));assert(wglShareLists(a,b));assert(((V86GLWGLContext*)a)->share_group==((V86GLWGLContext*)b)->share_group&&((V86GLWGLContext*)a)->share_group==((V86GLWGLContext*)c)->share_group);
 assert(!wglShareLists(a,d)&&GetLastError()==ERROR_INVALID_PIXEL_FORMAT);assert(!wglShareLists(a,(HGLRC)(uintptr_t)1)&&GetLastError()==ERROR_INVALID_HANDLE);
 assert(wglMakeCurrent(dc,b));assert(wglShareLists(a,b));glColor4f(1,0,0,1);assert(!wglCopyContext(a,b,0)&&GetLastError()==ERROR_BUSY);
 assert(wglMakeCurrent(NULL,NULL));assert(!wglCopyContext(a,b,1)&&GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);assert(wglCopyContext(a,b,0));assert(!wglCopyContext(a,d,0)&&GetLastError()==ERROR_INVALID_PIXEL_FORMAT);assert(!wglCopyContext(NULL,b,0)&&GetLastError()==ERROR_INVALID_HANDLE);
 assert(wglMakeCurrent(dc,e));assert(wglMakeCurrent(NULL,NULL));assert(!wglShareLists(a,e)&&GetLastError()==ERROR_BUSY);
 assert(wglMakeCurrent(dc,b));query_color(1);assert(wglDeleteContext(b));assert(wglDeleteContext(a));assert(wglDeleteContext(c));assert(wglDeleteContext(d));assert(wglDeleteContext(e));assert(wglDeleteContext(f));assert(!g_wgl_context_count);return 0;
}`;
run(code, (cpu, gpu) => { assert.equal(cpu.contexts.size, 0);assert.equal(gpu.contexts.size, 0); })
    .then(() => console.log("PASS actual C WGL context operations: whole unrealized share-group merge, idempotence, format checks, busy targets and explicit refusal of unimplemented copy/layers in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
