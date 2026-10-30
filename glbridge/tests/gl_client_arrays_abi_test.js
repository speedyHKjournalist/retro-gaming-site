"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const { testPath } = require("./host_paths.js");
const native = require(testPath("gl_conformance/client_arrays_reference.json")).cases;
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const typed = source.slice(source.indexOf("static int emit_query_typed("), source.indexOf("static GLint query_limit_or_default"));
const names = ["glVertexPointer", "glNormalPointer", "glColorPointer", "glIndexPointer", "glEdgeFlagPointer", "glTexCoordPointer", "glSecondaryColorPointerEXT", "glFogCoordPointer",
    "glVertexAttribPointer", "glEnableVertexAttribArray", "glDisableVertexAttribArray", "glVertexAttrib4f", "glGetVertexAttribfv", "glGetVertexAttribiv", "glGetVertexAttribdv", "glGetVertexAttribPointerv", "glGetPointerv", "glInterleavedArrays"];
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef int32_t GLint; typedef uint32_t GLuint; typedef uint16_t GLushort; typedef int32_t GLsizei;
typedef float GLfloat; typedef double GLdouble; typedef unsigned char GLboolean; typedef void GLvoid; typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define V86GL_MAX_VERTEX_ATTRIBS 16
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
${Object.entries(GL).filter(([, value]) => Number.isInteger(value) && value >= 0).map(([name, value]) => "#define GL_" + name + " 0x" + value.toString(16) + "u").join("\n")}
${transport}
${typed}
typedef struct { GLint size; GLenum type; GLsizei stride; const GLvoid* pointer; GLuint buffer; BOOL enabled; GLboolean normalized; GLfloat current[4]; } ClientArrayState;
typedef ClientArrayState GenericAttribState;
static ClientArrayState g_vertex_array,g_normal_array,g_color_array,g_index_array,g_edge_flag_array,g_secondary_color_array,g_fog_coord_array,g_texcoord_arrays[8];
static GenericAttribState g_generic_attribs[16]; static GLuint g_array_buffer_binding, client_unit;
static GLvoid* g_feedback_buffer; static GLvoid* g_selection_buffer;
static unsigned int client_active_texture_index(void) { return client_unit; }
typedef V86GLRecordVertexPointerVbo VBOPointerPayload;
typedef V86GLRecordVertexAttribPointerVbo VBOAttribPointerPayload;
${body(source, "pointer_offset_u32", "static uint32_t ")}
${body(source, "valid_array_pointer_type", "static BOOL ")}
${body(source, "emit_vbo_pointer", "static void ")}
${body(source, "emit_vbo_attrib_pointer", "static void ")}
${body(source, "emit_vertex_attrib4f", "static void ")}
${body(source, "set_client_array_enabled", "static int ")}
${names.map(name => body(source, name, "void APIENTRY ")).join("\n")}
static void bind(GLuint name) { uint32_t args[]={GL_ARRAY_BUFFER,name}; g_array_buffer_binding=name; emit_gl_call(GLFN_BIND_BUFFER,args,sizeof(args)); }
int main(void) {
    GLvoid* pointer; GLfloat floats[4]; GLint ints[4]; GLdouble doubles[4];
    glVertexPointer(3,GL_FLOAT,12,(GLvoid*)0x12345678); assert(!error); g_vertex_array.pointer=(GLvoid*)0xDEAD;
    glGetPointerv(GL_VERTEX_ARRAY_POINTER,&pointer); assert((uintptr_t)pointer==0x12345678);
    glVertexPointer(3,GL_UNSIGNED_BYTE,0,NULL); assert(error==GL_INVALID_ENUM); error=0;
    glGetPointerv(GL_VERTEX_ARRAY_POINTER,&pointer); assert((uintptr_t)pointer==0x12345678);
    bind(7); glNormalPointer(GL_SHORT,8,(GLvoid*)12); glColorPointer(4,GL_UNSIGNED_BYTE,4,(GLvoid*)16);
    glIndexPointer(GL_UNSIGNED_BYTE,2,(GLvoid*)20); glEdgeFlagPointer(1,(GLvoid*)24);
    glSecondaryColorPointerEXT(3,GL_FLOAT,12,(GLvoid*)28); glFogCoordPointer(GL_DOUBLE,8,(GLvoid*)32);
    GLenum pnames[]={GL_NORMAL_ARRAY_POINTER,GL_COLOR_ARRAY_POINTER,GL_INDEX_ARRAY_POINTER,GL_EDGE_FLAG_ARRAY_POINTER,
        GL_SECONDARY_COLOR_ARRAY_POINTER_EXT,GL_FOG_COORDINATE_ARRAY_POINTER_EXT};
    for(unsigned int i=0;i<6;i++) { pointer=NULL; glGetPointerv(pnames[i],&pointer); assert((uintptr_t)pointer==12+4*i); }
    bind(0); uint32_t unit=GL_TEXTURE0+1; emit_gl_call(GLFN_CLIENT_ACTIVE_TEXTURE,&unit,4); client_unit=1;
    glTexCoordPointer(2,GL_DOUBLE,24,(GLvoid*)0x1000); glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER,&pointer); assert((uintptr_t)pointer==0x1000);
    unit=GL_TEXTURE0; emit_gl_call(GLFN_CLIENT_ACTIVE_TEXTURE,&unit,4); client_unit=0;
    unit=GL_TEXTURE0+1; emit_gl_call(GLFN_ACTIVE_TEXTURE,&unit,4); glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER,&pointer); assert(pointer==NULL);
    bind(9); glVertexAttribPointer(3,2,GL_UNSIGNED_SHORT,GL_TRUE,10,(GLvoid*)256); glEnableVertexAttribArray(3);
    g_generic_attribs[3].size=99; g_generic_attribs[3].pointer=(GLvoid*)42;
    glGetVertexAttribfv(3,GL_VERTEX_ATTRIB_ARRAY_SIZE,floats); assert(floats[0]==2);
    glGetVertexAttribiv(3,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,ints); assert(ints[0]==9);
    doubles[1]=12345; doubles[2]=23456; doubles[3]=34567;
    glGetVertexAttribdv(3,GL_VERTEX_ATTRIB_ARRAY_SIZE,doubles); assert(doubles[0]==2 && doubles[1]==12345 && doubles[2]==23456 && doubles[3]==34567);
    glGetVertexAttribPointerv(3,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer); assert((uintptr_t)pointer==256);
    glVertexAttrib4f(3,0.25,-2.75,3.5,1); glGetVertexAttribiv(3,GL_CURRENT_VERTEX_ATTRIB,ints);
    assert(ints[0]==0 && ints[1]==-2 && ints[2]==3 && ints[3]==1);
    glGetVertexAttribdv(3,GL_CURRENT_VERTEX_ATTRIB,doubles); assert(doubles[0]==0.25 && doubles[1]==-2.75 && doubles[2]==3.5 && doubles[3]==1);
    doubles[0]=17; glGetVertexAttribdv(0,GL_CURRENT_VERTEX_ATTRIB,doubles); assert(error==GL_INVALID_OPERATION && doubles[0]==17); error=0;
    pointer=(GLvoid*)42; glGetVertexAttribPointerv(16,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer); assert(error==GL_INVALID_VALUE && pointer==(GLvoid*)42); error=0;
    glDisableVertexAttribArray(3); glGetVertexAttribiv(3,GL_VERTEX_ATTRIB_ARRAY_ENABLED,ints); assert(ints[0]==0);
    bind(7); set_client_array_enabled(GL_INDEX_ARRAY,TRUE,TRUE); set_client_array_enabled(GL_EDGE_FLAG_ARRAY,TRUE,TRUE);
    glInterleavedArrays(GL_C4UB_V3F,0,NULL); assert(!error);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER,&pointer); assert((uintptr_t)pointer==4);
    glGetPointerv(GL_COLOR_ARRAY_POINTER,&pointer); assert(pointer==NULL);
    emit_query_state(GL_VERTEX_ARRAY_STRIDE,V86GL_STATE_INTEGER,ints); assert(ints[0]==16);
    emit_query_state(GL_INDEX_ARRAY,V86GL_STATE_INTEGER,ints); assert(ints[0]==0);
    emit_query_state(GL_EDGE_FLAG_ARRAY,V86GL_STATE_INTEGER,ints); assert(ints[0]==0);
    return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.state.arrays.vertex.pointer, 4);
    assert.equal(shadow.current.state.arrays.texCoord[1].pointer, 0x1000);
    assert.equal(shadow.current.state.arrays.generic[3].size, 2);
    assert.equal(gpu.arrayState("generic3").size, 2);
    const vertex = shadow.current.state.arrays.vertex;
    assert.deepEqual([vertex.size, vertex.type, vertex.stride, shadow.current.group.bufferObjects.get(vertex.bufferId).name, vertex.pointer], native.interleaved_zero_offset);
    assert.deepEqual([shadow.current.state.raster.client[GL.INDEX_ARRAY], shadow.current.state.raster.client[GL.EDGE_FLAG_ARRAY]], native.interleaved_disables);
}).then(() => console.log("gl_client_arrays_abi_test: actual C pointer encoders and typed/pointer getters use CPU state in both stream modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
