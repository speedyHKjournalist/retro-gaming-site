"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const cacheType = source.slice(source.indexOf("typedef struct LockedClientRead"), source.indexOf("static LockedClientRead*"));
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef uint32_t GLenum,GLuint,GLbitfield;typedef int32_t GLint,GLsizei;typedef int BOOL;typedef float GLfloat;typedef void GLvoid;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define CopyMemory memcpy
#define GetProcessHeap() 0
#define HeapFree(h,f,p) free(p)
static int fail_allocation;
static void* allocate(size_t n){return fail_allocation?NULL:malloc(n);}
#define HeapAlloc(h,f,n) allocate(n)
${transport}
#undef locked_client_data
#undef release_locked_client_arrays
${cacheType}
static GLint g_array_lock_first;static GLsizei g_array_lock_count;static LockedClientRead* g_locked_client_reads;static uint32_t g_locked_client_bytes;
#define V86GL_LOCK_CACHE_BYTES (16u*1024u*1024u)
${body(source, "release_locked_client_arrays", "static void ")}
${body(source, "locked_client_data", "static const uint8_t* ")}
${body(source, "query_array_lock", "static BOOL ")}
${body(source, "glLockArraysEXT", "void APIENTRY ")}
${body(source, "glUnlockArraysEXT", "void APIENTRY ")}
static uint32_t gl_error(void){V86GLRecordQueryError q={0};uint8_t* p;reserve_pci_record(GLFN_QUERY_ERROR,NULL,sizeof(q),&p);memcpy(p,&q,sizeof(q));assert(emit_pci_batch(0));assert(read_u32le(p+offsetof(V86GLRecordQueryError,status))==1);return read_u32le(p+offsetof(V86GLRecordQueryError,value));}
int main(void){
 glLockArraysEXT(-1,3);assert(!g_array_lock_count&&gl_error()==${GL.INVALID_VALUE});glLockArraysEXT(0,0);assert(gl_error()==${GL.INVALID_VALUE});
 glLockArraysEXT(2,3);assert(g_array_lock_first==2&&g_array_lock_count==3);uint8_t data[64];for(uint32_t i=0;i<64;i++)data[i]=(uint8_t)i;
 const uint8_t* a=locked_client_data(data,8,4,16,20);assert(a!=data+16&&!memcmp(a,data+16,20));assert(g_locked_client_bytes==20);
 const uint8_t* b=locked_client_data(data,8,4,24,4);assert(b==a+8&&g_locked_client_bytes==20);
 assert(locked_client_data(data,8,4,8,4)==data+8);assert(locked_client_data(data,8,4,40,4)==data+40);
 glLockArraysEXT(0,2);assert(gl_error()==${GL.INVALID_OPERATION}&&g_array_lock_first==2&&g_locked_client_bytes==20);
 uint32_t mode=${GL.POINTS};emit_gl_call(GLFN_BEGIN,&mode,4);glUnlockArraysEXT();emit_gl_call(GLFN_END,NULL,0);assert(gl_error()==${GL.INVALID_OPERATION}&&g_array_lock_count==3);
 glUnlockArraysEXT();assert(!g_locked_client_reads&&!g_locked_client_bytes&&!g_array_lock_count);glUnlockArraysEXT();assert(gl_error()==${GL.INVALID_OPERATION});
 // Client-stack restoration is CPU-owned. Reading the restored range refreshes
 // the encoding cache rather than manufacturing success from local state.
 glLockArraysEXT(2,3);uint32_t mask=0xFFFFFFFFu;emit_gl_call(GLFN_PUSH_CLIENT_ATTRIB,&mask,4);glUnlockArraysEXT();emit_gl_call(GLFN_POP_CLIENT_ATTRIB,NULL,0);assert(query_array_lock(2,0,0));assert(g_array_lock_first==2&&g_array_lock_count==3);glUnlockArraysEXT();
 glLockArraysEXT(0,3);fail_allocation=1;assert(locked_client_data(data,8,4,0,20)==data&&!g_locked_client_bytes);fail_allocation=0;glUnlockArraysEXT();
 glLockArraysEXT(0,INT32_MAX);assert(locked_client_data(data,8,4,0,4)==data&&!g_locked_client_reads);glUnlockArraysEXT();
 glLockArraysEXT(2,3);a=locked_client_data(data,8,4,16,20);assert(a!=data+16);release_locked_client_arrays();assert(!g_locked_client_reads);glUnlockArraysEXT();
 assert(!gl_error()&&!error);return 0;
}`;
run(code, cpu => { assert.deepEqual(cpu.current.state.arrays.lock, { first: 0, count: 0 });assert.equal(cpu.current.error, 0); })
    .then(() => console.log("PASS actual C LockArrays: CPU errors/range, client-stack refresh, stable strided cache hits, allocation/budget fallback and cleanup in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
