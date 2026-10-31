"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const { hostPath } = require("./host_paths.js"), P = require(hostPath("gl-webgpu/gl_protocol.js"));
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
#include "../tests/gl_api_fixture.h"
typedef uint32_t GLenum,GLuint;typedef int32_t GLint,GLsizei;typedef uint16_t GLushort;typedef uint8_t GLboolean;typedef float GLfloat;typedef double GLdouble;typedef int BOOL;typedef size_t SIZE_T;
#define APIENTRY
#define GL_TRUE 1
#define GL_FALSE 0
#define FALSE 0
#define GL_INVALID_OPERATION ${GL.INVALID_OPERATION}
#define GL_INVALID_VALUE ${GL.INVALID_VALUE}
#define GL_OUT_OF_MEMORY ${GL.OUT_OF_MEMORY}
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define GetProcessHeap() 0
static int fail_alloc;
#define HeapAlloc(h,f,n) (fail_alloc?NULL:malloc(n))
#define HeapFree(h,f,p) free(p)
${transport}
${["emit_query_typed", "emit_query_parameter_index_count", "emit_query_parameter_count", "emit_query_parameter"].map(name => body(source,name,"static int ")).join("\n")}
${body(source,"glAreTexturesResident","GLboolean APIENTRY ")}
static void expect_error(GLenum expected){uint8_t* p;V86GLRecordQueryError q={0};assert(reserve_pci_record(GLFN_QUERY_ERROR,&q,sizeof(q),&p));assert(emit_pci_batch(0));if(read_u32le(p)!=1||read_u32le(p+4)!=expected)fprintf(stderr,"query status=%u error=%u expected=%u\\n",read_u32le(p),read_u32le(p+4),expected);assert(read_u32le(p)==1&&read_u32le(p+4)==expected);error=0;}
int main(void){
 GLboolean values[]={0xA5,0xA5};GLuint names[]={7,8};assert(glAreTexturesResident(0,NULL,NULL));assert(!glAreTexturesResident(-1,NULL,values)&&error==GL_INVALID_VALUE);error=0;assert(!glAreTexturesResident(1,NULL,values)&&error==GL_INVALID_VALUE);error=0;
 assert(!glAreTexturesResident(2,names,values)&&values[0]==0xA5&&values[1]==0xA5);expect_error(GL_INVALID_VALUE);
 uint32_t bind[]={${GL.TEXTURE_2D},7};emit_gl_call(GLFN_BIND_TEXTURE,bind,8);bind[1]=8;emit_gl_call(GLFN_BIND_TEXTURE,bind,8);assert(glAreTexturesResident(2,names,values)&&values[0]==0xA5&&values[1]==0xA5);assert(glAreTexturesResident(2,names,NULL));
 uint32_t remove[]={1,8};emit_gl_call(GLFN_DELETE_TEXTURES,remove,8);assert(!glAreTexturesResident(2,names,values)&&values[0]==0xA5&&values[1]==0xA5);expect_error(GL_INVALID_VALUE);
 names[0]=0;assert(!glAreTexturesResident(1,names,values));expect_error(GL_INVALID_VALUE);names[0]=7;
 uint32_t mode=${GL.POINTS};emit_gl_call(GLFN_BEGIN,&mode,4);assert(!glAreTexturesResident(0,NULL,NULL));emit_gl_call(GLFN_END,NULL,0);expect_error(GL_INVALID_OPERATION);
 assert(glAreTexturesResident(1,names,values)&&values[0]==0xA5);fail_alloc=1;assert(!glAreTexturesResident(1,names,values)&&error==GL_OUT_OF_MEMORY&&values[0]==0xA5);fail_alloc=0;error=0;
 bind[1]=9;emit_gl_call(GLFN_BIND_TEXTURE,bind,8);names[1]=9;assert(!glAreTexturesResident(2,names,values)&&values[0]==1&&values[1]==0);names[0]=9;names[1]=0;values[0]=values[1]=0xA5;assert(!glAreTexturesResident(2,names,values)&&values[0]==0xA5&&values[1]==0xA5);expect_error(GL_INVALID_VALUE);return 0;
}`;
run(code, (cpu, gpu) => { assert.equal(cpu.current.error, 0);assert.ok(gpu.current.shareGroup.textures.has(7)); }, cpu => {
    // Exercise the C aggregate/output contract with a completed CPU reply
    // reporting one nonresident object. Production currently keeps all live
    // objects resident; no physical residency eviction is claimed here.
    const processBatch = cpu.process.bind(cpu);
    cpu.process = (bytes, metadata) => {
        const segments = processBatch(bytes, metadata), view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        if (view.getUint16(0, true) === P.GLFN.QUERY_PARAMETER && view.getUint32(4 + P.F.QUERY_PARAMETER.query_kind, true) === P.C.V86GL_PARAMETER_TEXTURE_RESIDENCY &&
                view.getUint32(4 + P.F.QUERY_PARAMETER.target, true) === 9 && view.getUint32(4 + P.F.QUERY_PARAMETER.status, true) === 1)
            view.setFloat64(4 + P.RECORDS.QUERY_PARAMETER.size, 0, true);
        return segments;
    };
})
    .then(() => console.log("PASS actual C texture residency: CPU checks live/deleted/zero names and Begin/End, all-resident leaves output unchanged in both transports"))
    .catch(error => { console.error(error);process.exitCode = 1; });
