"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef uint32_t DWORD,GLenum;typedef int BOOL;typedef void* HANDLE;
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define V86GL_DIAG_ENABLED 0
#define V86GL_DIAG_RECORD(a,b) ((void)0)
#define V86GL_MAGIC 0x324C4756u
#define V86GL_VERSION 1u
#define V86GL_DESC_FLAG_PRESENT 1u
#define V86GL_SUBMIT_FORCE_PRESENT 1u
#define V86GL_IOCTL_MAP_BUFFER 1
#define V86GL_IOCTL_SUBMIT 2
#define V86GL_IOCTL_UNMAP_BUFFER 3
#define V86GL_DEVICE_DOS_NAME "v86gl"
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0
#define GL_INVALID_OPERATION ${GL.INVALID_OPERATION}
#define GL_OUT_OF_MEMORY ${GL.OUT_OF_MEMORY}
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
${transport.slice(0, transport.indexOf("static int reserve_pci_record"))}
typedef struct {uint32_t magic,version,flags,frame_id,command_count,command_bytes,reserved0,reserved1;} V86GLDMADesc;
typedef struct {uintptr_t user_address;uint32_t buffer_bytes;} V86GLMapBuffer;
typedef struct {uint32_t descriptor_bytes,flags;} V86GLSubmit;
static uint8_t arena[65536],*g_dma_buffer;static uint32_t g_dma_capacity,g_dma_size,g_dma_command_count,g_frame_id;
static HANDLE g_v86gl=INVALID_HANDLE_VALUE;static BOOL g_v86gl_ready,g_have_last_surface=TRUE;static DWORD last_error;
static unsigned opens,maps,closes,unmaps,submits;static int fail_open=1,fail_map=1,fail_submit,fail_after_accept;
static DWORD GetLastError(void){return last_error;}
static void SetLastError(DWORD value){last_error=value;}
static void roundtrip(void){assert(fwrite(&wire_size,4,1,stdout)==1);assert(fwrite(wire,1,wire_size,stdout)==wire_size);fflush(stdout);assert(fread(wire,1,wire_size,stdin)==wire_size);}
static HANDLE CreateFileA(const char* name,DWORD access,DWORD share,void* security,DWORD mode,DWORD attrs,HANDLE template){(void)name;(void)access;(void)share;(void)security;(void)mode;(void)attrs;(void)template;opens++;if(fail_open){fail_open=0;last_error=2;return INVALID_HANDLE_VALUE;}return (HANDLE)(uintptr_t)7;}
static BOOL CloseHandle(HANDLE handle){assert(handle==(HANDLE)(uintptr_t)7);closes++;return TRUE;}
static BOOL DeviceIoControl(HANDLE handle,DWORD op,const void* input,DWORD input_size,void* output,DWORD output_size,DWORD* returned,void* overlapped){
 assert(handle==(HANDLE)(uintptr_t)7&&!overlapped);(void)input;(void)input_size;(void)output_size;
 if(op==V86GL_IOCTL_MAP_BUFFER){maps++;if(fail_map){fail_map=0;last_error=8;return FALSE;}V86GLMapBuffer* map=output;map->user_address=(uintptr_t)arena;map->buffer_bytes=sizeof(arena);*returned=sizeof(*map);return TRUE;}
 if(op==V86GL_IOCTL_UNMAP_BUFFER){unmaps++;uint16_t record[]={V86GL_CTRL_DESTROY_CONTEXT,0};wire_size=4;memcpy(wire,record,4);roundtrip();return TRUE;}
 assert(op==V86GL_IOCTL_SUBMIT);submits++;if(fail_submit){fail_submit=0;last_error=21;return FALSE;}
 const V86GLDMADesc* desc=(const V86GLDMADesc*)arena;assert(desc->magic==V86GL_MAGIC&&desc->version==1);wire_size=desc->command_bytes;memcpy(wire,arena+32,wire_size);roundtrip();memcpy(arena+32,wire,wire_size);if(fail_after_accept){fail_after_accept=0;last_error=121;return FALSE;}return TRUE;
}
static void v86gl_trace(const char* text,...){(void)text;}
static void v86gl_error(const char* text,...){(void)text;}
static int v86gl_trace_call_allowed(void){return 0;}
static void flush_immediate_vertices(void){}
static void write_u32le(uint8_t* p,uint32_t value){memcpy(p,&value,4);}
${body(source, "close_v86gl", "static void ")}
${body(source, "open_v86gl", "static int ")}
${body(source, "reset_pci_stream", "static void ")}
${body(source, "emit_pci_batch", "static int ")}
${body(source, "reserve_pci_record", "static int ")}
${body(source, "emit_pci_record", "static int ")}
int main(void){
 assert(!open_v86gl());assert(!open_v86gl());assert(open_v86gl());assert(opens==3&&maps==2&&closes==1&&!unmaps&&g_v86gl_ready);
 uint32_t bind[]={${GL.ARRAY_BUFFER},7},data[]={${GL.ARRAY_BUFFER},4,${GL.STATIC_DRAW},4,0x12345678};float red[]={1,0,0,1},blue[]={0,0,1,1},green[]={0,1,0,1},translate[]={1,0,0};
 assert(emit_pci_record(GLFN_BIND_BUFFER,bind,sizeof(bind),FALSE));assert(emit_pci_record(GLFN_BUFFER_DATA,data,sizeof(data),FALSE));assert(emit_pci_record(GLFN_COLOR4F,red,sizeof(red),TRUE));
 g_have_last_surface=TRUE;fail_submit=1;assert(!emit_pci_record(GLFN_COLOR4F,blue,sizeof(blue),TRUE));assert(!g_dma_size&&!g_dma_command_count&&g_v86gl_ready&&!g_have_last_surface&&closes==1&&!unmaps&&GetLastError()==21);
 assert(emit_pci_record(GLFN_COLOR4F,green,sizeof(green),TRUE));assert(opens==3&&maps==2&&closes==1&&!unmaps);
 fail_after_accept=1;assert(!emit_pci_record(GLFN_TRANSLATEF,translate,sizeof(translate),TRUE));assert(!g_dma_size&&GetLastError()==121);
 assert(emit_pci_record(GLFN_COLOR4F,red,sizeof(red),TRUE));assert(submits==5&&opens==3&&maps==2&&closes==1&&!unmaps&&error==GL_INVALID_OPERATION);return 0;
}`;
run(code, (cpu, gpu) => {
    assert.equal(cpu.boundBuffer(GL.ARRAY_BUFFER).data[0], 0x78);assert.equal(gpu.current.shareGroup.buffers.get(7).shadow[0], 0x78);
    assert.equal(cpu.current.state.stacks[GL.MODELVIEW].at(-1)[12], 1);assert.equal(gpu.topOf(GL.MODELVIEW)[12], 1);
    assert.deepEqual(cpu.stateValue(GL.CURRENT_COLOR)[0], [1, 0, 0, 1]);
}).then(() => console.log("PASS actual C transport recovery: open/map retries, same-owner objects survive failed submits, unknown acceptance is not replayed, later calls continue in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
