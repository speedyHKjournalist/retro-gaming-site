"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, run } = require("./gl_cpu_abi_harness.js"), { code: base } = require("./gl_thread_guard_abi_test.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = "#define GL_THREAD_FIXTURE_NO_MAIN\n" + base + `
static uint32_t g_next_share_group=1;
${body(source, "wglCreateContext", "HGLRC APIENTRY ")}
${body(source, "wglDeleteContext", "BOOL APIENTRY ")}
static HGLRC foreign_context;static int foreign_ready,foreign_release;
static void* hold_context(void* unused){(void)unused;thread_id=4;assert(wglMakeCurrent((HDC)(uintptr_t)100,foreign_context));
 pthread_mutex_lock(&turns);foreign_ready=1;pthread_cond_broadcast(&changed);while(!foreign_release)pthread_cond_wait(&changed,&turns);pthread_mutex_unlock(&turns);
 assert(wglGetCurrentContext()==foreign_context);assert(wglMakeCurrent(NULL,NULL));return NULL;
}
int main(void){thread_id=3;g_wgl_context_count=0;pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));assert(!pthread_mutex_init(&g_api_lock.mutex,&attr));assert(!pthread_mutex_init(&g_wgl_context_lock.mutex,&attr));g_api_lock.api=1;
 capture_guest_context_state(&g_default_guest_context_state);g_default_guest_context_state_valid=TRUE;
 assert(!wglCreateContext(NULL));assert(GetLastError()==ERROR_INVALID_HANDLE);glVertex4fv((const GLfloat*)(uintptr_t)1);assert(!glGetError());
 HGLRC a=wglCreateContext((HDC)(uintptr_t)99),b=wglCreateContext((HDC)(uintptr_t)100);assert(a&&b&&a!=b&&!wglGetCurrentContext());
 assert(wglMakeCurrent((HDC)(uintptr_t)99,a));glColor4f(1,0,0,1);assert(!wglMakeCurrent((HDC)(uintptr_t)99,(HGLRC)(uintptr_t)0xBAD));assert(GetLastError()==ERROR_INVALID_HANDLE);assert(!wglGetCurrentContext()&&!wglGetCurrentDC());
 assert(wglMakeCurrent((HDC)(uintptr_t)99,a));query_color(1);assert(!wglMakeCurrent(NULL,a));assert(!wglGetCurrentContext());
 assert(wglMakeCurrent((HDC)(uintptr_t)99,a));assert(wglMakeCurrent((HDC)(uintptr_t)0xBAD,NULL));assert(!wglGetCurrentContext());
 assert(wglMakeCurrent((HDC)(uintptr_t)99,a));tls_fail=1;assert(!wglMakeCurrent((HDC)(uintptr_t)100,b));assert(!wglGetCurrentContext());assert(wglMakeCurrent((HDC)(uintptr_t)99,a));query_color(1);
 assert(wglDeleteContext(a));assert(!wglGetCurrentContext()&&!wglGetCurrentDC());assert(!wglDeleteContext(a)&&GetLastError()==ERROR_INVALID_HANDLE);
 HGLRC c=wglCreateContext((HDC)(uintptr_t)101);assert(c);foreign_context=b;pthread_t holder;assert(!pthread_create(&holder,NULL,hold_context,NULL));
 pthread_mutex_lock(&turns);while(!foreign_ready)pthread_cond_wait(&changed,&turns);pthread_mutex_unlock(&turns);
 assert(wglMakeCurrent((HDC)(uintptr_t)101,c));assert(!wglDeleteContext(b)&&GetLastError()==ERROR_BUSY);assert(wglGetCurrentContext()==c);
 assert(!wglMakeCurrent((HDC)(uintptr_t)100,b)&&GetLastError()==ERROR_BUSY);assert(!wglGetCurrentContext());
 pthread_mutex_lock(&turns);foreign_release=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&turns);assert(!pthread_join(holder,NULL));
 assert(wglDeleteContext(b));assert(wglMakeCurrent((HDC)(uintptr_t)101,c));assert(wglDeleteContext(c));assert(!g_wgl_context_count&&!wglGetCurrentContext());
 glVertex4fv((const GLfloat*)(uintptr_t)1);assert(!glGetError()&&!g_api_depth&&!g_dma_size);return 0;
}`;
run(code, (cpu, gpu) => { assert.equal(cpu.current, null);assert.equal(cpu.contexts.size, 0);assert.equal(gpu.contexts.size, 0); })
    .then(() => console.log("PASS actual C WGL lifecycle: failed binds release, NULL context ignores HDC, own-current deletion, foreign ownership, state rebind and ignored no-context pointers in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
