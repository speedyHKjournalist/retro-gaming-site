"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef uint32_t GLenum,GLuint;typedef int BOOL;typedef float GLfloat;
#define APIENTRY
#define __declspec(x)
#define FALSE 0
#define TRUE 1
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
${transport.replaceAll("reserve_pci_record", "wire_reserve").replaceAll("emit_gl_call", "send_record")}
static BOOL g_immediate_active;static uint8_t g_immediate_bytes[V86GL_IMMEDIATE_BLOCK_BYTES];static uint32_t g_immediate_size,g_immediate_runs,g_immediate_run,blocks,vertices;
static void flush_immediate_vertices(void);
static void write_u32le(uint8_t* p,uint32_t v){memcpy(p,&v,4);}
static int reserve_pci_record(uint16_t fn,const void* data,uint32_t size,uint8_t** out){if(fn!=GLFN_IMMEDIATE_VERTEX_BLOCK)flush_immediate_vertices();return wire_reserve(fn,data,size,out);}
static int emit_pci_record(uint16_t fn,const void* data,uint32_t size,BOOL present){
 (void)present;if(fn==GLFN_IMMEDIATE_VERTEX_BLOCK){blocks++;uint32_t p=8,n=read_u32le(data);for(uint32_t i=0;i<n;i++){uint32_t a=read_u32le((const uint8_t*)data+p),v=read_u32le((const uint8_t*)data+p+4);vertices+=v;p+=8+a+v*16;}assert(p==size);}
 uint8_t* out;assert(reserve_pci_record(fn,NULL,size,&out));if(size)memcpy(out,data,size);return emit_pci_batch(0);
}
${body(source, "flush_immediate_vertices", "static void ")}
${body(source, "buffer_immediate_call", "static BOOL ")}
static void emit_gl_large_call(uint16_t fn,const void* data,uint32_t size){(void)fn;(void)data;(void)size;abort();}
${body(source, "emit_gl_call", "static void ")}
${["glColor4f", "glVertex3f", "glVertex4f", "glNormal3f", "glTexCoord2f"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
static void list(uint32_t operation,uint32_t name,uint32_t value){V86GLRecordListCommand p={operation,name,value};emit_gl_call(GLFN_LIST_COMMAND,&p,sizeof(p));}
static void begin(void){uint32_t mode=${GL.POINTS};emit_gl_call(GLFN_BEGIN,&mode,4);}
static void end(void){emit_gl_call(GLFN_END,NULL,0);}
int main(void){
 begin();glColor4f(1,0,0,1);glNormal3f(0,1,0);glTexCoord2f(.25f,.5f);glVertex4f(0,0,0,2);glVertex3f(.5f,0,0);
 glColor4f(0,1,0,1);glVertex3f(0,.5f,0);glColor4f(0,0,1,1);end();assert(blocks==1&&vertices==3);
 // Reserve-based queries must flush even when they bypass emit_gl_call.
 begin();glColor4f(.25f,.5f,.75f,1);glVertex4f(0,0,0,4);uint8_t* out;
 V86GLRecordQueryState q={0};q.pname=${GL.CURRENT_COLOR};q.value_kind=V86GL_STATE_DOUBLE;q.data_size=32;
 assert(reserve_pci_record(GLFN_QUERY_STATE,NULL,sizeof(q)+32,&out));memcpy(out,&q,sizeof(q));assert(emit_pci_batch(0));
 // Queries inside Begin are rejected, but earlier attribute records ran first.
 assert(read_u32le(out+offsetof(V86GLRecordQueryState,status))==V86GL_SYNC_QUERY_STATUS_FAILED);end();
 // More than one block is still one GL primitive, without per-vertex records.
 begin();for(uint32_t i=0;i<10000;i++)glVertex3f(0,0,0);end();assert(blocks<8&&vertices==10004);
 list(1,99,${GL.COMPILE});begin();glColor4f(.5f,.25f,1,1);glVertex4f(0,0,0,8);end();list(2,0,0);list(3,99,0);
 glColor4f(1,1,1,1);assert(!g_immediate_size&&!g_immediate_active);return 0;
}`;
run(code, (cpu, gpu) => {
    assert.equal(gpu.blockVertices.length, 10005);assert.deepEqual(gpu.blockVertices.slice(0, 4).map(v => v.position), [[0, 0, 0, 2], [.5, 0, 0, 1], [0, .5, 0, 1], [0, 0, 0, 4]]);
    assert.deepEqual(gpu.blockVertices.slice(0, 4).map(v => v.color), [[1, 0, 0, 1], [1, 0, 0, 1], [0, 1, 0, 1], [.25, .5, .75, 1]]);
    assert.deepEqual(gpu.blockVertices.at(-1).position, [0, 0, 0, 8]);assert.deepEqual(gpu.blockVertices.at(-1).color, [.5, .25, 1, 1]);
    assert.deepEqual(cpu.stateValue(GL.CURRENT_COLOR)[0], [1, 1, 1, 1]);assert.equal(cpu.current.error, GL.INVALID_OPERATION);assert.equal(cpu.current.state.begin, false);
}, (cpu, gpu) => {
    gpu.blockVertices = [];const original = gpu.immediateVertex;gpu.immediateVertex = function(...position) { gpu.blockVertices.push({ position, color: [...this.current.current.color] });return original.apply(this, position); };
}).then(() => console.log("PASS actual C immediate blocks: attributes and w, trailing changes, query flush, 10K vertices split, immutable lists and no guest attribute cache in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
