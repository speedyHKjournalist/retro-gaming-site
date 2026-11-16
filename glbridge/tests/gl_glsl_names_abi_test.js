"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),{execFileSync}=require("node:child_process"),{body}=require("./gl_cpu_abi_harness.js");
const source=fs.readFileSync(path.join(__dirname,"../openglproxy/opengl32_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"gl-names-thin-"));
try {
 const code=`
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "gl_protocol.h"
#include "../tests/gl_api_fixture.h"
#define APIENTRY
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_INVALID_ENUM 0x500
typedef uint32_t GLuint,GLenum;typedef int GLsizei;
static int result,emitted;static unsigned int error;
static void v86gl_set_error(GLenum e){error=e;}
static int emit_query_names(uint32_t kind,uint16_t opcode,GLsizei count,GLuint* out){assert(kind==1||kind==2);assert(!opcode&&count==1);if(result>0)*out=71;return result;}
static void emit_gl_call(uint16_t opcode,const void* payload,uint32_t size){assert(opcode==GLFN_CREATE_PROGRAM||opcode==GLFN_CREATE_SHADER);assert(payload&&size>=4);emitted++;}
${body(source,"glCreateProgram","GLuint APIENTRY ")}
${body(source,"glCreateShader","GLuint APIENTRY ")}
int main(void){
 for(result=-1;result<=0;result++){assert(!glCreateProgram());assert(!glCreateShader(GL_VERTEX_SHADER));assert(!emitted);}
 result=1;assert(glCreateProgram()==71);assert(glCreateShader(GL_FRAGMENT_SHADER)==71);assert(emitted==2);
 assert(!glCreateShader(0)&&error==GL_INVALID_ENUM&&emitted==2);return 0;
}`;
 fs.writeFileSync(path.join(dir,"test.c"),code);execFileSync(process.env.CC_HOST||"cc",["-std=c99","-Wall","-Wextra","-Werror","-I",path.join(__dirname,"../openglproxy"),path.join(dir,"test.c"),"-o",path.join(dir,"test")]);execFileSync(path.join(dir,"test"));
 console.log("PASS C GLSL creators preserve CPU names and never fabricate names when the CPU query fails");
} finally {fs.rmSync(dir,{recursive:true,force:true});}
