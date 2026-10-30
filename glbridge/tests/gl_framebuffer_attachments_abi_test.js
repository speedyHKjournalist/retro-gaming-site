"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const { hostPath } = require("./host_paths.js"); require(hostPath("gl-webgpu/gl_shadow_textures.js"));
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum; typedef uint32_t GLuint; typedef int32_t GLint; typedef int BOOL;
#define APIENTRY
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static unsigned int dimensions[3];
#define GL_CPU_ABI_BEFORE_SEND do { uint16_t fn=*(uint16_t*)wire; if(fn>=GLFN_FRAMEBUFFER_TEXTURE_1D && fn<=GLFN_FRAMEBUFFER_TEXTURE_3D)dimensions[fn-GLFN_FRAMEBUFFER_TEXTURE_1D]++; } while(0)
${transport}
${body(source, "framebuffer_texture_ext", "static void ")}
${["glFramebufferTexture1DEXT", "glFramebufferTexture1D", "glFramebufferTexture2DEXT", "glFramebufferTexture2D", "glFramebufferTexture3DEXT", "glFramebufferTexture3D", "glFramebufferRenderbufferEXT", "glFramebufferRenderbuffer"].map(n => body(source, n, "void APIENTRY ")).join("\n")}
int main(void) {
    uint32_t bind[]={GL_TEXTURE_2D,1}; emit_gl_call(GLFN_BIND_TEXTURE,bind,sizeof(bind));
    uint32_t image[]={GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,0}; emit_gl_call(GLFN_TEX_IMAGE_2D,image,sizeof(image));
    uint32_t fbo[]={GL_FRAMEBUFFER_EXT,1}; emit_gl_call(GLFN_BIND_FRAMEBUFFER,fbo,sizeof(fbo));
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,1,0);
    glFramebufferTexture1DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,1,0);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,100,0);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,1,-1);
    glFramebufferTexture3DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,0,0,-1,-1);
    glFramebufferTexture3D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,0,0,-2,-2);
    glFramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,1,0);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,0,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER_EXT,100);
    glFramebufferTexture1D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,0,0,-1);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,1,0);
    assert(dimensions[0]==2 && dimensions[1]==5 && dimensions[2]==2 && !error); return 0;
}
`;
run(code, shadow => {
    assert.equal(shadow.stats.rejected, 4); assert.equal(shadow.current.error, GL.INVALID_ENUM);
    assert.equal(shadow.current.framebuffers.get(1).color[0].name, 1);
    assert.equal(shadow.framebufferStatus(GL.FRAMEBUFFER_EXT), GL.FRAMEBUFFER_COMPLETE);
}).then(() => console.log("gl_framebuffer_attachments_abi_test: actual C attachment APIs preserve dimensionality, delegate validation and ignore detach arguments in both modes (passed)"))
    .catch(error => { console.error(error); process.exitCode = 1; });
