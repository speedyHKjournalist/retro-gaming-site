"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process"), { body } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), driver = fs.readFileSync(process.env.V86GL_TRANSPORT_SOURCE || path.join(__dirname, "../v86gl_driver/virtio_transport.h"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef uint32_t ULONG,DWORD;typedef uint16_t USHORT;typedef uint8_t* PUCHAR;typedef int NTSTATUS;
#define TRUE 1
#define FALSE 0
#define V86GL_DIAG_ENABLED 1
#define V86GL_DIAG(...) do{file_lines++;}while(0)
#define lstrcpyA strcpy
#define lstrcatA strcat
#define lstrlenA strlen
#define wvsprintfA vsprintf
static BOOL g_trace_enabled,g_trace_debug_string;static unsigned debug_lines,file_lines;
static void v86gl_trace_initialize(void){}
static void OutputDebugStringA(const char* text){assert(strstr(text,"[v86gl.dll] test 42\\r\\n"));debug_lines++;}
${body(source, "v86gl_logv", "static void ")}
static void log_test(BOOL force,const char* format,...){va_list args;va_start(args,format);v86gl_logv(force,format,args);va_end(args);}
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY -1
#define STATUS_INVALID_PARAMETER -2
#define STATUS_UNSUCCESSFUL -3
#define STATUS_IO_TIMEOUT -4
#define NT_SUCCESS(s) ((s)>=0)
#define VGL_REQUEST_OFFSET 256
#define VGL_REPLY_OFFSET 288
#define VGL_AVAIL_OFFSET 128
#define VGL_QUEUE_SIZE 8
#define VGL_REGISTER_ARENA 1
#define VGL_UNREGISTER_ARENA 3
#define RtlZeroMemory(p,n) memset(p,0,n)
#define KeMemoryBarrier() ((void)0)
#define DbgPrint(...) ((void)0)
${driver.slice(driver.indexOf("typedef struct VGL_REQUEST"),driver.indexOf("#pragma pack(pop)"))}
typedef struct {void* queue_memory;USHORT available,notify;BOOL initialized,registered;} VGL_TRANSPORT;
static uint8_t queue[512];static ULONG reply_status;static NTSTATUS wait_status;static unsigned inits,resets;
static NTSTATUS vgl_transport_init(VGL_TRANSPORT* t){inits++;t->initialized=TRUE;return STATUS_SUCCESS;}
static NTSTATUS vgl_transport_reset(VGL_TRANSPORT* t){resets++;t->initialized=TRUE;t->registered=FALSE;return STATUS_SUCCESS;}
static void vgl_write16(USHORT port,ULONG offset,USHORT value){assert(!port&&!offset&&!value);((VGL_REPLY*)(queue+VGL_REPLY_OFFSET))->status=reply_status;}
static NTSTATUS vgl_wait(VGL_TRANSPORT* t){(void)t;return wait_status;}
${body(driver, "vgl_request", "static NTSTATUS ")}
int main(void){
 log_test(TRUE,"test %d",42);assert(file_lines==1&&!debug_lines);log_test(FALSE,"test %d",42);assert(file_lines==1&&!debug_lines);g_trace_enabled=TRUE;log_test(FALSE,"test %d",42);assert(file_lines==2&&!debug_lines);g_trace_debug_string=TRUE;log_test(FALSE,"test %d",42);assert(file_lines==3&&debug_lines==1);
 VGL_TRANSPORT t={0};t.queue_memory=queue;assert(vgl_request(&t,1,4096,65536,0)==STATUS_SUCCESS&&t.registered&&inits==1);reply_status=1;assert(vgl_request(&t,2,0,36,0)==STATUS_INVALID_PARAMETER);reply_status=2;assert(vgl_request(&t,2,0,36,0)==STATUS_DEVICE_NOT_READY&&!t.registered);reply_status=3;assert(vgl_request(&t,2,0,36,0)==STATUS_DEVICE_NOT_READY);reply_status=7;assert(vgl_request(&t,2,0,36,0)==STATUS_UNSUCCESSFUL);
 reply_status=0;assert(vgl_request(&t,1,4096,65536,0)==STATUS_SUCCESS&&t.registered);wait_status=STATUS_IO_TIMEOUT;assert(vgl_request(&t,2,0,36,0)==STATUS_IO_TIMEOUT&&resets==1&&!t.registered);wait_status=0;assert(vgl_request(&t,1,4096,65536,0)==STATUS_SUCCESS&&t.registered);assert(vgl_request(&t,3,0,0,0)==STATUS_SUCCESS&&!t.registered);return 0;
}`;
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-transport-diagnostics-"));
try {
    const file = path.join(directory, "test.c"), binary = path.join(directory, "test");fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C transport status and logging: retryable driver errors, queue reset/re-registration and debug strings only under explicit opt-in");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
