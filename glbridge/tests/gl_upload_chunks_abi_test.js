"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const wire = transport.slice(0, transport.indexOf("static int reserve_pci_record"));
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "gl_protocol.h"
#include "../tests/gl_api_fixture.h"
#include "../upload_bands.h"
typedef uint32_t GLenum;typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define GL_OUT_OF_MEMORY ${GL.OUT_OF_MEMORY}
#define GL_INVALID_OPERATION ${GL.INVALID_OPERATION}
#define GL_INVALID_VALUE ${GL.INVALID_VALUE}
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define HeapAlloc(h,f,n) malloc(n)
#define HeapFree(h,f,p) free(p)
#define GetProcessHeap() 0
#define V86GL_PACKED_DIRECT_DMA 1
#define V86GL_DIAG_RECORD(a,b) ((void)0)
${wire}
typedef struct {uint32_t words[8];} V86GLDMADesc;
static uint8_t arena[128*1024];static uint8_t* g_dma_buffer=arena;static uint32_t g_dma_capacity=sizeof(arena),g_dma_size,g_dma_command_count,g_frame_id,g_upload_token;
static int open_v86gl(void){return 1;}
static void write_u32le(uint8_t* p,uint32_t v){memcpy(p,&v,4);}
static void v86gl_error(const char* text,...){(void)text;}
static void v86gl_trace(const char* text,...){(void)text;}
static int v86gl_trace_call_allowed(void){return 0;}
static BOOL g_immediate_active;
static uint8_t g_immediate_bytes[V86GL_IMMEDIATE_BLOCK_BYTES];static uint32_t g_immediate_size,g_immediate_runs,g_immediate_run;
static void flush_immediate_vertices(void);
static int fail_after=-1;
static int emit_pci_batch(BOOL present){
 (void)present;if(!g_dma_size)return 1;
 if(fail_after==0){fail_after=-1;g_dma_size=0;g_dma_command_count=0;return 0;}
 if(fail_after>0)fail_after--;
 wire_size=g_dma_size;memcpy(wire,g_dma_buffer+sizeof(V86GLDMADesc),wire_size);
 assert(fwrite(&wire_size,4,1,stdout)==1);assert(fwrite(wire,1,wire_size,stdout)==wire_size);fflush(stdout);assert(fread(wire,1,wire_size,stdin)==wire_size);
 g_dma_size=0;g_dma_command_count=0;return 1;
}
${body(source, "reserve_pci_record", "static int ")}
${body(source, "emit_pci_record", "static int ")}
${body(source, "flush_immediate_vertices", "static void ")}
${body(source, "buffer_immediate_call", "static BOOL ")}
${body(source, "emit_gl_large_call", "static void ")}
${body(source, "emit_gl_call", "static void ")}
${body(source, "alloc_payload", "static uint8_t* ")}
${body(source, "reserve_packed_draw_payload", "static int ")}
${body(source, "commit_packed_draw_payload", "static void ")}
static void upload(uint32_t bytes,uint8_t seed){
 uint8_t* data;BOOL dma;
 assert(reserve_packed_draw_payload(GLFN_BUFFER_DATA,bytes+16,&data,&dma));assert(dma==(bytes+16<=sizeof(arena)-sizeof(V86GLDMADesc)-V86GL_HEARTBEAT_BYTES-8));
 uint32_t header[4]={${GL.ARRAY_BUFFER},bytes,${GL.STATIC_DRAW},bytes};memcpy(data,header,16);
 for(uint32_t i=0;i<bytes;i++)data[16+i]=(uint8_t)(i+seed);
 commit_packed_draw_payload(GLFN_BUFFER_DATA,data,bytes+16,dma);
}
int main(void){
 assert(v86wg_upload_band(UINT32_MAX,UINT32_MAX,1,9)==0);assert(v86wg_upload_band(100,20,7,90)==11);assert(v86wg_upload_band(100,20,7,3)==3);
 uint32_t bind[2]={${GL.ARRAY_BUFFER},7};emit_gl_call(GLFN_BIND_BUFFER,bind,8);
 upload(sizeof(arena)-sizeof(V86GLDMADesc)-V86GL_HEARTBEAT_BYTES-8-16,1);assert(emit_pci_batch(0));
 upload(600000,3);assert(emit_pci_batch(0));assert(!error);
 fail_after=1;upload(600000,9);assert(error==GL_INVALID_OPERATION);error=0;
 // An application retry uses a fresh token and supersedes the incomplete one.
 upload(600000,17);assert(emit_pci_batch(0));assert(!error);assert(g_upload_token==3);
 uint8_t dummy=0;emit_gl_large_call(GLFN_BUFFER_DATA,&dummy,V86GL_MAX_UPLOAD_BYTES+1);assert(error==GL_OUT_OF_MEMORY);
 return 0;
}`;
run(code, (cpu, gpu) => {
    const expected = Uint8Array.from({ length: 600000 }, (v, i) => i + 17), buffer = cpu.boundBuffer(GL.ARRAY_BUFFER);
    const digest=bytes=>require("node:crypto").createHash("sha256").update(bytes).digest("hex");
    assert.equal(buffer.size, expected.length);assert.equal(digest(buffer.data), digest(expected));assert.equal(cpu.current.upload, null);assert.equal(cpu.current.completedUpload.token, 3);
    const object = gpu.current.shareGroup.buffers.get(7);assert.equal(digest(object.shadow), digest(expected));
}).then(() => console.log("PASS actual C upload splitting: DMA boundary, heap fallback, 600KB assembly, transport failure/retry and shared D3D row capacity in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
