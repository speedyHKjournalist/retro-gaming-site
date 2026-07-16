"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

/*
 * Cube 2: Sauerbraten's Windows renderer resolves these 143 names through
 * SDL_GL_GetProcAddress.  This is a checked-in snapshot of the calls in the
 * official r6889 src/engine/rendergl.cpp (also present as literal strings in
 * bin/sauerbraten.exe):
 *
 * https://sourceforge.net/p/sauerbraten/code/HEAD/tree/src/engine/rendergl.cpp
 *
 * Keep this list exact.  The subsets below describe which branches Cube 2
 * takes when our guest reports OpenGL 2.1 + GL_EXT_framebuffer_object.
 */
const cube2AllDynamicProcs = `
glActiveTexture
glAttachShader
glBeginQuery
glBindAttribLocation
glBindBuffer
glBindBufferBase
glBindBufferRange
glBindFragDataLocation
glBindFramebuffer
glBindFramebufferEXT
glBindRenderbuffer
glBindRenderbufferEXT
glBindVertexArray
glBindVertexArrayAPPLE
glBlendColor
glBlendEquation
glBlendEquationSeparate
glBlendFuncSeparate
glBlitFramebuffer
glBlitFramebufferEXT
glBufferData
glBufferSubData
glCheckFramebufferStatus
glCheckFramebufferStatusEXT
glCompileShader
glCompressedTexImage2D
glCompressedTexImage3D
glCompressedTexSubImage2D
glCompressedTexSubImage3D
glCopyTexSubImage3D
glCreateProgram
glCreateShader
glDeleteBuffers
glDeleteFramebuffers
glDeleteFramebuffersEXT
glDeleteProgram
glDeleteQueries
glDeleteRenderbuffers
glDeleteRenderbuffersEXT
glDeleteShader
glDeleteVertexArrays
glDeleteVertexArraysAPPLE
glDisableVertexAttribArray
glDrawBuffers
glDrawRangeElements
glEnableVertexAttribArray
glEndQuery
glFlushMappedBufferRange
glFramebufferRenderbuffer
glFramebufferRenderbufferEXT
glFramebufferTexture2D
glFramebufferTexture2DEXT
glGenBuffers
glGenFramebuffers
glGenFramebuffersEXT
glGenQueries
glGenRenderbuffers
glGenRenderbuffersEXT
glGenVertexArrays
glGenVertexArraysAPPLE
glGenerateMipmap
glGenerateMipmapEXT
glGetActiveUniform
glGetActiveUniformBlockiv
glGetActiveUniformsiv
glGetBufferSubData
glGetCompressedTexImage
glGetProgramInfoLog
glGetProgramiv
glGetQueryObjectiv
glGetQueryObjectuiv
glGetQueryiv
glGetShaderInfoLog
glGetShaderiv
glGetStringi
glGetUniformBlockIndex
glGetUniformIndices
glGetUniformLocation
glIsVertexArray
glIsVertexArrayAPPLE
glLinkProgram
glMapBuffer
glMapBufferRange
glMultiDrawArrays
glMultiDrawElements
glRenderbufferStorage
glRenderbufferStorageEXT
glShaderSource
glStencilFuncSeparate
glStencilMaskSeparate
glStencilOpSeparate
glTexImage3D
glTexSubImage3D
glUniform1f
glUniform1fv
glUniform1i
glUniform1iv
glUniform2f
glUniform2fv
glUniform2i
glUniform2iv
glUniform3f
glUniform3fv
glUniform3i
glUniform3iv
glUniform4f
glUniform4fv
glUniform4i
glUniform4iv
glUniformBlockBinding
glUniformMatrix2fv
glUniformMatrix3fv
glUniformMatrix4fv
glUnmapBuffer
glUseProgram
glVertexAttrib1f
glVertexAttrib1fv
glVertexAttrib1s
glVertexAttrib1sv
glVertexAttrib2f
glVertexAttrib2fv
glVertexAttrib2s
glVertexAttrib2sv
glVertexAttrib3f
glVertexAttrib3fv
glVertexAttrib3s
glVertexAttrib3sv
glVertexAttrib4Nbv
glVertexAttrib4Niv
glVertexAttrib4Nub
glVertexAttrib4Nubv
glVertexAttrib4Nuiv
glVertexAttrib4Nusv
glVertexAttrib4bv
glVertexAttrib4f
glVertexAttrib4fv
glVertexAttrib4iv
glVertexAttrib4s
glVertexAttrib4sv
glVertexAttrib4ubv
glVertexAttrib4uiv
glVertexAttrib4usv
glVertexAttribPointer
`.trim().split(/\s+/);

/* Unconditional Windows + non-Apple loads after Cube accepts GL >= 2.0. */
const cube2OpenGL20StartupProcs = `
glActiveTexture
glAttachShader
glBeginQuery
glBindAttribLocation
glBindBuffer
glBlendColor
glBlendEquation
glBlendEquationSeparate
glBlendFuncSeparate
glBufferData
glBufferSubData
glCompileShader
glCompressedTexImage2D
glCompressedTexImage3D
glCompressedTexSubImage2D
glCompressedTexSubImage3D
glCopyTexSubImage3D
glCreateProgram
glCreateShader
glDeleteBuffers
glDeleteProgram
glDeleteQueries
glDeleteShader
glDisableVertexAttribArray
glDrawBuffers
glDrawRangeElements
glEnableVertexAttribArray
glEndQuery
glGenBuffers
glGenQueries
glGetActiveUniform
glGetBufferSubData
glGetCompressedTexImage
glGetProgramInfoLog
glGetProgramiv
glGetQueryObjectiv
glGetQueryObjectuiv
glGetQueryiv
glGetShaderInfoLog
glGetShaderiv
glGetUniformLocation
glLinkProgram
glMapBuffer
glMultiDrawArrays
glMultiDrawElements
glShaderSource
glStencilFuncSeparate
glStencilMaskSeparate
glStencilOpSeparate
glTexImage3D
glTexSubImage3D
glUniform1f
glUniform1fv
glUniform1i
glUniform1iv
glUniform2f
glUniform2fv
glUniform2i
glUniform2iv
glUniform3f
glUniform3fv
glUniform3i
glUniform3iv
glUniform4f
glUniform4fv
glUniform4i
glUniform4iv
glUniformMatrix2fv
glUniformMatrix3fv
glUniformMatrix4fv
glUnmapBuffer
glUseProgram
glVertexAttrib1f
glVertexAttrib1fv
glVertexAttrib1s
glVertexAttrib1sv
glVertexAttrib2f
glVertexAttrib2fv
glVertexAttrib2s
glVertexAttrib2sv
glVertexAttrib3f
glVertexAttrib3fv
glVertexAttrib3s
glVertexAttrib3sv
glVertexAttrib4Nbv
glVertexAttrib4Niv
glVertexAttrib4Nub
glVertexAttrib4Nubv
glVertexAttrib4Nuiv
glVertexAttrib4Nusv
glVertexAttrib4bv
glVertexAttrib4f
glVertexAttrib4fv
glVertexAttrib4iv
glVertexAttrib4s
glVertexAttrib4sv
glVertexAttrib4ubv
glVertexAttrib4uiv
glVertexAttrib4usv
glVertexAttribPointer
`.trim().split(/\s+/);

/* Hard-required fallback selected by GL_EXT_framebuffer_object on GL 2.1. */
const cube2ExtFramebufferProcs = `
glBindRenderbufferEXT
glDeleteRenderbuffersEXT
glGenRenderbuffersEXT
glRenderbufferStorageEXT
glCheckFramebufferStatusEXT
glBindFramebufferEXT
glDeleteFramebuffersEXT
glGenFramebuffersEXT
glFramebufferTexture2DEXT
glFramebufferRenderbufferEXT
glGenerateMipmapEXT
`.trim().split(/\s+/);

function setFromMatches(source, pattern, group) {
    return new Set(Array.from(source.matchAll(pattern), match => match[group]));
}

function main() {
    const proxyDir = path.join(__dirname, "..", "winproxy");
    const proxySource = fs.readFileSync(path.join(proxyDir, "opengl32_proxy.c"), "utf8");
    const defSource = fs.readFileSync(path.join(proxyDir, "opengl32.def"), "utf8");

    /* wglGetProcAddress first checks the DLL export table, then PROC_ENTRY. */
    const exports = setFromMatches(defSource, /^\s+(gl[A-Za-z0-9_]+)(?:@\d+)?\s*$/gm, 1);
    const fallbacks = setFromMatches(proxySource, /PROC_ENTRY\((gl[A-Za-z0-9_]+)\)/g, 1);
    const available = new Set([...exports, ...fallbacks]);
    const required = [...cube2OpenGL20StartupProcs, ...cube2ExtFramebufferProcs];

    assert.equal(cube2AllDynamicProcs.length, 143,
        "official Cube 2 dynamic-proc snapshot changed unexpectedly");
    assert.equal(new Set(cube2AllDynamicProcs).size, 143,
        "Cube 2 dynamic-proc snapshot contains duplicates");
    for (const name of required) {
        assert.ok(cube2AllDynamicProcs.includes(name), `${name} is absent from the exact snapshot`);
    }

    const missing = required.filter(name => !available.has(name));
    assert.deepEqual(missing, [],
        `OpenGL 2.1 + EXT FBO Cube 2 path has unresolved procedures: ${missing.join(", ")}`);

    const optionalMissing = cube2AllDynamicProcs.filter(name => !available.has(name));
    console.log(`cube2_gl_proc_coverage_test: ok (${required.length} required, ` +
        `${optionalMissing.length} capability-gated optional procedures unavailable)`);
}

main();
