"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef uint32_t GLuint; typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static unsigned int core,ext;
#define GL_CPU_ABI_BEFORE_SEND do { uint16_t fn=*(uint16_t*)wire; if(fn==GLFN_BIND_FRAMEBUFFER)core++; if(fn==GLFN_BIND_FRAMEBUFFER_EXT)ext++; } while(0)
${transport}
static int valid_framebuffer_target(GLenum t) { return t==GL_FRAMEBUFFER_EXT || t==GL_READ_FRAMEBUFFER || t==GL_DRAW_FRAMEBUFFER; }
static int cache;
static void* find_framebuffer_state(GLuint name,BOOL create) { (void)name;(void)create;return &cache; }
static void set_framebuffer_binding_for_target(GLenum target,GLuint name) { (void)target;(void)name; }
${body(source, "bind_framebuffer", "static void ")}
${body(source, "glBindFramebufferEXT", "void APIENTRY ")}
${body(source, "glBindFramebuffer", "void APIENTRY ")}
int main(void) {
    uint32_t tex[]={GL_TEXTURE_2D,1}; emit_gl_call(GLFN_BIND_TEXTURE,tex,sizeof(tex));
    uint32_t image[]={GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,0}; emit_gl_call(GLFN_TEX_IMAGE_2D,image,sizeof(image));
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,7);
    uint32_t attachment[]={GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,1,0,0}; emit_gl_call(GLFN_FRAMEBUFFER_TEXTURE_2D,attachment,sizeof(attachment));
    uint32_t second[]={2,0,0,64,64,2,1},first[]={1,0,0,64,64,1,1};
    emit_gl_call(V86GL_CTRL_MAKE_CURRENT,second,sizeof(second)); glBindFramebuffer(GL_FRAMEBUFFER_EXT,7);
    emit_gl_call(V86GL_CTRL_MAKE_CURRENT,first,sizeof(first)); glBindFramebuffer(GL_FRAMEBUFFER_EXT,9); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,9);
    emit_gl_call(V86GL_CTRL_MAKE_CURRENT,second,sizeof(second)); glBindFramebuffer(GL_FRAMEBUFFER_EXT,9); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,7);
    assert(core==3 && ext==3 && !error); return 0;
}
`;
run(code, (shadow, gpu) => {
    const contexts = [...shadow.contexts.values()], first = contexts.find(c => c.id === 1), second = contexts.find(c => c.id === 2);
    assert.equal(shadow.stats.rejected, 0); assert.notEqual(first.privateFramebuffers.get(9), second.privateFramebuffers.get(9));
    assert.equal(first.privateFramebuffers.get(9).shared, false);
    assert.equal(second.drawFramebufferObject, second.group.sharedFramebuffers.get(7)); assert.equal(second.drawFramebufferObject.color[0].name, 1);
    const gpuContexts = [...gpu.contexts.values()], a = gpuContexts.find(c => c.protocolId === 1), b = gpuContexts.find(c => c.protocolId === 2);
    assert.notEqual(a.privateFramebuffers.get(9), b.privateFramebuffers.get(9));
    assert.equal(b.drawFramebufferObject, b.shareGroup.sharedFramebuffers.get(7)); assert.equal(b.drawFramebufferObject.color[0].name, 1);
}).then(() => console.log("gl_framebuffer_binding_abi_test: actual C core/EXT binds retain creation origin and cross-bind shared FBOs in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
