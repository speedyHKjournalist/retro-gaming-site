"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process"), { body } = require("./gl_cpu_abi_harness.js"), { code: formats } = require("./gl_wgl_formats_abi_test.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = formats.slice(0, formats.indexOf("int main(void){"))
    .replace("static DWORD last_error;", "static int intervals[4];static DWORD last_error;")
    .replace("assert(!wcscmp(key,L\"V86GL.PixelFormat\"));return (HANDLE)(uintptr_t)properties[(uintptr_t)window-101];", "return (HANDLE)(uintptr_t)(!wcscmp(key,L\"V86GL.SwapInterval\")?intervals:properties)[(uintptr_t)window-101];")
    .replace("assert(!wcscmp(key,L\"V86GL.PixelFormat\"));if(fail_property)", "if(fail_property)")
    .replace("properties[(uintptr_t)window-101]=(int)(uintptr_t)value;", "(!wcscmp(key,L\"V86GL.SwapInterval\")?intervals:properties)[(uintptr_t)window-101]=(int)(uintptr_t)value;") + `
#define ERROR_SUCCESS 0
#define ERROR_GEN_FAILURE 31
#define ERROR_INVALID_DATA 13
#define ERROR_DC_NOT_FOUND 1425
#define WGL_SWAP_MAIN_PLANE 1
#define WGL_SWAPMULTIPLE_MAX 16
#define V86GL_DIAG_ENABLED 0
#define v86gl_trace(...) ((void)0)
typedef struct {HDC hdc;UINT uiFlags;} WGLSWAP;
typedef struct {HWND current_hwnd;} V86GLWGLContext;
static V86GLWGLContext context,*current;
static V86GLWGLContext* v86gl_current_context(void){return current;}
static uint32_t g_frame_id;static unsigned submits,waits;static HWND reported;
static int reserve_fails,submit_fails,host_fails;static V86GLRecordSwapWindow request;
static void emit_window_state(HWND window,BOOL flush){assert(!flush);reported=window;}
static int reserve_pci_record(uint16_t opcode,const void* data,uint32_t size,uint8_t** args){assert(opcode==V86GL_CTRL_SWAP_WINDOW&&size==sizeof(request));if(reserve_fails)return 0;memcpy(&request,data,size);*args=(uint8_t*)&request;return 1;}
static int emit_pci_batch(BOOL present){assert(!present);submits++;if(submit_fails)return 0;request.status=host_fails?2:1;return 1;}
static BOOL wait_for_host_status(const uint8_t* status,const char* what){assert(status==(uint8_t*)&request+8&&!strcmp(what,"SwapBuffers"));waits++;return *(const uint32_t*)status==1;}
${body(source, "window_swap_interval", "static int ")}
${body(source, "emit_frame", "static BOOL ")}
${body(source, "wglSwapBuffers", "BOOL APIENTRY ")}
${body(source, "wglSwapLayerBuffers", "BOOL APIENTRY ")}
${body(source, "wglSwapMultipleBuffers", "DWORD APIENTRY ")}
${body(source, "wglSwapIntervalEXT", "BOOL APIENTRY ")}
${body(source, "wglGetSwapIntervalEXT", "int APIENTRY ")}
int main(void){
 HDC a=(HDC)(uintptr_t)1,b=(HDC)(uintptr_t)3,single=(HDC)(uintptr_t)5;
 assert(!wglSwapBuffers(a)&&!submits);assert(wglSetPixelFormat(a,1,NULL));assert(wglSetPixelFormat(b,2,NULL));assert(wglSetPixelFormat(single,3,NULL));assert(!window_format_flags((HWND)(uintptr_t)101));
 assert(!wglSwapIntervalEXT(0)&&last_error==ERROR_DC_NOT_FOUND);assert(!wglGetSwapIntervalEXT()&&last_error==ERROR_DC_NOT_FOUND);
 current=&context;context.current_hwnd=(HWND)(uintptr_t)101;assert(wglGetSwapIntervalEXT()==1);assert(!wglSwapIntervalEXT(-1)&&last_error==ERROR_INVALID_DATA);assert(wglGetSwapIntervalEXT()==1);
 assert(wglSwapIntervalEXT(0)&&!wglGetSwapIntervalEXT());context.current_hwnd=(HWND)(uintptr_t)102;assert(wglGetSwapIntervalEXT()==1);assert(wglSwapIntervalEXT(2)&&wglGetSwapIntervalEXT()==2);
 assert(wglSwapBuffers(a)&&request.hwnd==101&&request.interval==0&&reported==(HWND)(uintptr_t)101&&submits==1&&waits==1);assert(current==&context&&context.current_hwnd==(HWND)(uintptr_t)102&&wglGetSwapIntervalEXT()==2);
 assert(wglSwapBuffers(b)&&request.hwnd==102&&request.interval==2&&submits==2&&waits==2);assert(wglSwapIntervalEXT(999)&&wglGetSwapIntervalEXT()==V86GL_MAX_SWAP_INTERVAL);
 fail_property=1;assert(!wglSwapIntervalEXT(1)&&wglGetSwapIntervalEXT()==V86GL_MAX_SWAP_INTERVAL);fail_property=0;
 assert(wglSwapBuffers(single)&&submits==2);assert(!wglSwapBuffers((HDC)(uintptr_t)10)&&last_error==ERROR_INVALID_PIXEL_FORMAT);assert(!wglSwapBuffers(NULL)&&last_error==ERROR_INVALID_HANDLE);
 assert(!wglSwapLayerBuffers(a,2)&&last_error==ERROR_INVALID_PARAMETER&&submits==2);assert(wglSwapLayerBuffers(a,1)&&submits==3);
 current=NULL;assert(wglSwapBuffers(b)&&request.hwnd==102&&request.interval==V86GL_MAX_SWAP_INTERVAL&&submits==4);
 WGLSWAP swaps[]={{a,1},{b,1},{single,1}};assert(wglSwapMultipleBuffers(3,swaps)==3&&submits==6);assert(!wglSwapMultipleBuffers(1,NULL)&&last_error==ERROR_INVALID_PARAMETER);assert(!wglSwapMultipleBuffers(17,swaps));assert(!wglSwapMultipleBuffers(0,NULL));
 unsigned frames=g_frame_id;reserve_fails=1;assert(!wglSwapBuffers(a)&&last_error==ERROR_GEN_FAILURE&&submits==6);reserve_fails=0;submit_fails=1;assert(!wglSwapBuffers(a)&&waits==6);submit_fails=0;host_fails=1;assert(!wglSwapBuffers(a)&&waits==7&&g_frame_id==frames);return 0;
}`;
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-wgl-swap-"));
try {
    const file = path.join(directory, "test.c"), binary = path.join(directory, "test");fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"), file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C WGL swap: HDC target independent of current RC, single-buffer no-op, window-scoped interval/default/clamp, host reply and failed transport");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
