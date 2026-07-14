// Thin exported wrapper around gl4es for v86_network_bridge.js.
// Keep these names stable; the browser bridge calls v86gl_gl* functions.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <GL/gl.h>
#include <gl4esinit.h>
#include <gl4eshint.h>
#include "gl/init.h"
#include "glx/hardext.h"

#ifdef __EMSCRIPTEN__
extern void emscripten_glUniformMatrix2x3fv(GLint location, GLsizei count,
                                            GLboolean transpose,
                                            const GLfloat* value);
extern void emscripten_glUniformMatrix3x2fv(GLint location, GLsizei count,
                                            GLboolean transpose,
                                            const GLfloat* value);
extern void emscripten_glUniformMatrix2x4fv(GLint location, GLsizei count,
                                            GLboolean transpose,
                                            const GLfloat* value);
extern void emscripten_glUniformMatrix4x2fv(GLint location, GLsizei count,
                                            GLboolean transpose,
                                            const GLfloat* value);
extern void emscripten_glUniformMatrix3x4fv(GLint location, GLsizei count,
                                            GLboolean transpose,
                                            const GLfloat* value);
extern void emscripten_glUniformMatrix4x3fv(GLint location, GLsizei count,
                                            GLboolean transpose,
                                            const GLfloat* value);
extern void emscripten_glGenQueries(GLsizei n, GLuint* ids);
extern void emscripten_glDeleteQueries(GLsizei n, const GLuint* ids);
extern GLboolean emscripten_glIsQuery(GLuint id);
extern void emscripten_glBeginQuery(GLenum target, GLuint id);
extern void emscripten_glEndQuery(GLenum target);
extern void emscripten_glGetQueryiv(GLenum target, GLenum pname,
                                    GLint* params);
extern void emscripten_glGetQueryObjectuiv(GLuint id, GLenum pname,
                                           GLuint* params);
#endif

extern void glActiveTexture(GLenum texture);
extern void glClientActiveTexture(GLenum texture);
extern void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
extern void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
extern void glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
extern void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
extern void glTexEnviv(GLenum target, GLenum pname, const GLint* params);
extern void glTexEnvfv(GLenum target, GLenum pname, const GLfloat* params);
extern void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
extern void glFogf(GLenum pname, GLfloat param);
extern void glFogi(GLenum pname, GLint param);
extern void glFogfv(GLenum pname, const GLfloat* params);
extern void glMaterialf(GLenum face, GLenum pname, GLfloat param);
extern void glMateriali(GLenum face, GLenum pname, GLint param);
extern void glMaterialfv(GLenum face, GLenum pname, const GLfloat* params);
extern void glMaterialiv(GLenum face, GLenum pname, const GLint* params);
extern void glLightf(GLenum light, GLenum pname, GLfloat param);
extern void glLighti(GLenum light, GLenum pname, GLint param);
extern void glLightfv(GLenum light, GLenum pname, const GLfloat* params);
extern void glLightiv(GLenum light, GLenum pname, const GLint* params);
extern void glLightModelf(GLenum pname, GLfloat param);
extern void glLightModeli(GLenum pname, GLint param);
extern void glLightModelfv(GLenum pname, const GLfloat* params);
extern void glLightModeliv(GLenum pname, const GLint* params);
extern void glTexGeni(GLenum coord, GLenum pname, GLint param);
extern void glTexGenf(GLenum coord, GLenum pname, GLfloat param);
extern void glTexGeniv(GLenum coord, GLenum pname, const GLint* params);
extern void glTexGenfv(GLenum coord, GLenum pname, const GLfloat* params);
extern void glClipPlane(GLenum plane, const GLdouble* equation);
extern void glPushClientAttrib(GLbitfield mask);
extern void glPopClientAttrib(void);
extern void glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
extern void glBlendEquation(GLenum mode);
extern void glBlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha);
extern void glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);
extern void glDrawBuffers(GLsizei n, const GLenum* bufs);
extern void glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
extern void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
extern void glStencilMaskSeparate(GLenum face, GLuint mask);
extern void glSampleCoverage(GLclampf value, GLboolean invert);
extern void glFogCoordf(GLfloat coord);
extern void glSecondaryColor3f(GLfloat red, GLfloat green, GLfloat blue);
extern void glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const void* pointer);
extern void glPointParameterf(GLenum pname, GLfloat param);
extern void glPointParameterfv(GLenum pname, const GLfloat* params);
extern void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                   GLsizei width, GLsizei height, GLint border,
                                   GLsizei image_size, const void* data);
extern void glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat,
                                   GLsizei width, GLint border, GLsizei image_size,
                                   const void* data);
extern void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                                   GLsizei width, GLsizei height, GLsizei depth, GLint border,
                                   GLsizei image_size, const void* data);
extern void glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                      GLsizei width, GLenum format, GLsizei image_size,
                                      const void* data);
extern void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                      GLint yoffset, GLsizei width, GLsizei height,
                                      GLenum format, GLsizei image_size, const void* data);
extern void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                      GLint yoffset, GLint zoffset, GLsizei width,
                                      GLsizei height, GLsizei depth, GLenum format,
                                      GLsizei image_size, const void* data);
extern void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLsizei height, GLsizei depth, GLint border,
                         GLenum format, GLenum type, const void* pixels);
extern void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                            GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                            GLenum format, GLenum type, const void* pixels);
extern void glTexImage1D(GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLint border, GLenum format, GLenum type,
                         const void* pixels);
extern void glTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                            GLsizei width, GLenum format, GLenum type,
                            const void* pixels);
extern void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                             GLint x, GLint y, GLsizei width, GLint border);
extern void glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                GLint x, GLint y, GLsizei width);
extern void glPointParameteri(GLenum pname, GLint param);
extern void glPointParameteriv(GLenum pname, const GLint* params);
extern void glWindowPos3f(GLfloat x, GLfloat y, GLfloat z);
extern void glPointSize(GLfloat size);
extern void glLineStipple(GLint factor, GLushort pattern);
extern void glLogicOp(GLenum opcode);
extern void glPixelTransferf(GLenum pname, GLfloat param);
extern void glPixelTransferi(GLenum pname, GLint param);
extern void glPixelZoom(GLfloat xfactor, GLfloat yfactor);
extern void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat* values);
extern void glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint* values);
extern void glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort* values);
extern void glDrawPixels(GLsizei width, GLsizei height, GLenum format,
                         GLenum type, const GLvoid* pixels);
extern void glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig,
                     GLfloat xmove, GLfloat ymove, const GLubyte* bitmap);
extern void glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                         GLenum type);
extern void glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void glAccum(GLenum op, GLfloat value);
extern void glPolygonStipple(const GLubyte* mask);
extern GLuint glCreateProgram(void);
extern GLuint glCreateShader(GLenum type);
extern void glDeleteProgram(GLuint program);
extern void glDeleteShader(GLuint shader);
extern void glAttachShader(GLuint program, GLuint shader);
extern void glDetachShader(GLuint program, GLuint shader);
extern void glShaderSource(GLuint shader, GLsizei count, const GLchar** string,
                           const GLint* length);
extern void glCompileShader(GLuint shader);
extern void glLinkProgram(GLuint program);
extern void glUseProgram(GLuint program);
extern void glValidateProgram(GLuint program);
extern void glBindAttribLocation(GLuint program, GLuint index, const char* name);
extern GLint glGetUniformLocation(GLuint program, const char* name);
extern GLint glGetAttribLocation(GLuint program, const char* name);
extern void glUniform1fv(GLint location, GLsizei count, const GLfloat* value);
extern void glUniform2fv(GLint location, GLsizei count, const GLfloat* value);
extern void glUniform3fv(GLint location, GLsizei count, const GLfloat* value);
extern void glUniform4fv(GLint location, GLsizei count, const GLfloat* value);
extern void glUniform1iv(GLint location, GLsizei count, const GLint* value);
extern void glUniform2iv(GLint location, GLsizei count, const GLint* value);
extern void glUniform3iv(GLint location, GLsizei count, const GLint* value);
extern void glUniform4iv(GLint location, GLsizei count, const GLint* value);
extern void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                               const GLfloat* value);
extern void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                               const GLfloat* value);
extern void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                               const GLfloat* value);
extern void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
extern void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                  GLboolean normalized, GLsizei stride,
                                  const void* pointer);
extern void glEnableVertexAttribArray(GLuint index);
extern void glDisableVertexAttribArray(GLuint index);
extern void glGenBuffers(GLsizei n, GLuint* buffers);
extern void glDeleteBuffers(GLsizei n, const GLuint* buffers);
extern void glBindBuffer(GLenum target, GLuint buffer);
extern void glBufferData(GLenum target, GLsizeiptr size, const void* data,
                         GLenum usage);
extern void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                            const void* data);
extern void glDrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                GLsizei count, GLenum type,
                                const void* indices);
extern void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
extern void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
extern void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
extern void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
extern void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                               GLsizei* length, GLint* size, GLenum* type,
                               GLchar* name);
extern void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                              GLsizei* length, GLint* size, GLenum* type,
                              GLchar* name);
extern void glGenFramebuffers(GLsizei n, GLuint* framebuffers);
extern void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
extern void glBindFramebuffer(GLenum target, GLuint framebuffer);
extern GLenum glCheckFramebufferStatus(GLenum target);
extern void glFramebufferTexture1D(GLenum target, GLenum attachment,
                                   GLenum textarget, GLuint texture, GLint level);
extern void glFramebufferTexture2D(GLenum target, GLenum attachment,
                                   GLenum textarget, GLuint texture, GLint level);
extern void glFramebufferTexture3D(GLenum target, GLenum attachment,
                                   GLenum textarget, GLuint texture, GLint level,
                                   GLint zoffset);
extern void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                      GLenum renderbuffertarget, GLuint renderbuffer);
extern void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers);
extern void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers);
extern void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
extern void glRenderbufferStorage(GLenum target, GLenum internalformat,
                                  GLsizei width, GLsizei height);
extern void gl4es_glProgramStringARB(GLenum target, GLenum format, GLsizei len,
                                     const GLvoid* string);
extern void gl4es_glBindProgramARB(GLenum target, GLuint program);
extern void gl4es_glDeleteProgramsARB(GLsizei n, const GLuint* programs);
extern void gl4es_glGenProgramsARB(GLsizei n, GLuint* programs);
extern void gl4es_glProgramEnvParameter4dvARB(GLenum target, GLuint index,
                                              const GLdouble* params);
extern void gl4es_glProgramEnvParameter4fvARB(GLenum target, GLuint index,
                                              const GLfloat* params);
extern void gl4es_glProgramLocalParameter4dvARB(GLenum target, GLuint index,
                                                const GLdouble* params);
extern void gl4es_glProgramLocalParameter4fvARB(GLenum target, GLuint index,
                                                const GLfloat* params);
extern void gl4es_glGetProgramEnvParameterdvARB(GLenum target, GLuint index,
                                                GLdouble* params);
extern void gl4es_glGetProgramEnvParameterfvARB(GLenum target, GLuint index,
                                                GLfloat* params);
extern void gl4es_glGetProgramLocalParameterdvARB(GLenum target, GLuint index,
                                                  GLdouble* params);
extern void gl4es_glGetProgramLocalParameterfvARB(GLenum target, GLuint index,
                                                  GLfloat* params);
extern void gl4es_glGetProgramivARB(GLenum target, GLenum pname, GLint* params);
extern void gl4es_glGetProgramStringARB(GLenum target, GLenum pname,
                                        GLvoid* string);
extern GLboolean gl4es_glIsProgramARB(GLuint program);
extern void gl4es_glProgramEnvParameters4fvEXT(GLenum target, GLuint index,
                                               GLsizei count,
                                               const GLfloat* params);
extern void gl4es_glProgramLocalParameters4fvEXT(GLenum target, GLuint index,
                                                 GLsizei count,
                                                 const GLfloat* params);

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

#ifndef GL_PROGRAM_LENGTH_ARB
#define GL_PROGRAM_LENGTH_ARB 0x8627
#endif

#ifndef GL_SECONDARY_COLOR_ARRAY
#define GL_SECONDARY_COLOR_ARRAY 0x845E
#endif

#ifndef GL_FOG_COORDINATE_ARRAY
#define GL_FOG_COORDINATE_ARRAY 0x8457
#endif

#ifndef GL_TEXTURE_RECTANGLE_ARB
#define GL_TEXTURE_RECTANGLE_ARB 0x84F5
#endif

#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif

#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif

#ifndef GL_ANY_SAMPLES_PASSED_CONSERVATIVE
#define GL_ANY_SAMPLES_PASSED_CONSERVATIVE 0x8D6A
#endif

#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif

static int32_t g_surface_x;
static int32_t g_surface_y;
static uint32_t g_surface_width;
static uint32_t g_surface_height;
static uint32_t g_surface_hwnd;
static int g_ready;
static int g_webgl_major_version;

#ifdef __EMSCRIPTEN__
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_context;

EM_JS(void, v86gl_lose_webgl_context, (), {
    const canvas = document.getElementById("v86gl_canvas");
    const gl = canvas && (canvas.getContext("webgl2") ||
                          canvas.getContext("webgl") ||
                          canvas.getContext("experimental-webgl"));
    const loseContext = gl && gl.getExtension("WEBGL_lose_context");
    if (loseContext) {
        loseContext.loseContext();
    }
});
#endif

#define V86GL_MAX_TEXTURES 16384
#define V86GL_MAX_PROGRAMS 1024
#define V86GL_MAX_ARB_PROGRAMS 1024
#define V86GL_MAX_SHADERS 2048
#define V86GL_MAX_UNIFORM_LOCATIONS 4096
#define V86GL_MAX_ATTRIB_LOCATIONS 512
#define V86GL_MAX_VERTEX_ATTRIBS 16
#define V86GL_MAX_FRAMEBUFFERS 1024
#define V86GL_MAX_RENDERBUFFERS 1024
#define V86GL_MAX_QUERIES 4096
#define V86GL_MAX_BUFFERS 4096
#define V86GL_OBJECT_KIND_SHADER 1u
#define V86GL_OBJECT_KIND_PROGRAM 2u
#define V86GL_OBJECT_KIND_QUERY 3u
#define V86GL_ACTIVE_KIND_UNIFORM 1u
#define V86GL_ACTIVE_KIND_ATTRIB 2u
#define V86GL_PROGRAM_PARAMETER_ENV 1u
#define V86GL_PROGRAM_PARAMETER_LOCAL 2u

typedef struct {
    GLuint guest;
    GLuint host;
} V86GLTextureName;

typedef struct {
    GLuint guest;
    GLuint host;
} V86GLNameMap;

typedef struct {
    GLint guest;
    GLint host;
} V86GLLocationMap;

static V86GLTextureName g_textures[V86GL_MAX_TEXTURES];
static uint32_t g_texture_count;
static V86GLNameMap g_programs[V86GL_MAX_PROGRAMS];
static V86GLNameMap g_arb_programs[V86GL_MAX_ARB_PROGRAMS];
static uint32_t g_program_count;
static uint32_t g_arb_program_count;
static V86GLNameMap g_shaders[V86GL_MAX_SHADERS];
static uint32_t g_shader_count;
static V86GLNameMap g_framebuffers[V86GL_MAX_FRAMEBUFFERS];
static uint32_t g_framebuffer_count;
static V86GLNameMap g_renderbuffers[V86GL_MAX_RENDERBUFFERS];
static uint32_t g_renderbuffer_count;
static V86GLNameMap g_queries[V86GL_MAX_QUERIES];
static uint32_t g_query_count;
static V86GLNameMap g_buffers[V86GL_MAX_BUFFERS];
static uint32_t g_buffer_count;
static V86GLLocationMap g_uniform_locations[V86GL_MAX_UNIFORM_LOCATIONS];
static uint32_t g_uniform_location_count;
static V86GLLocationMap g_attrib_locations[V86GL_MAX_ATTRIB_LOCATIONS];
static uint32_t g_attrib_location_count;
/* Desktop GL exposes a mutable default texture object (name 0) for each
 * texture target on every texture unit.  WebGL has no such object:
 * texParameter on an unbound target is an INVALID_OPERATION.  Keep distinct
 * host stand-ins because gl4es tracks 1D/2D/3D/cube/rectangle separately. */
static GLuint g_default_texture_1d;
static GLuint g_default_texture_2d;
static GLuint g_default_texture_3d;
static GLuint g_default_texture_cube;
static GLuint g_default_texture_rectangle;

static GLuint* v86gl_default_texture_slot(GLenum target) {
    switch (target) {
    case GL_TEXTURE_1D:
        return &g_default_texture_1d;
    case GL_TEXTURE_3D:
        return &g_default_texture_3d;
    case GL_TEXTURE_CUBE_MAP:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return &g_default_texture_cube;
    case GL_TEXTURE_RECTANGLE_ARB:
        return &g_default_texture_rectangle;
    case GL_TEXTURE_2D:
    default:
        return &g_default_texture_2d;
    }
}

static GLenum v86gl_binding_target(GLenum target) {
    switch (target) {
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return GL_TEXTURE_CUBE_MAP;
    default:
        return target;
    }
}

static GLuint v86gl_default_texture(GLenum target) {
    GLuint* texture = v86gl_default_texture_slot(target);

    if (!*texture) {
        glGenTextures(1, texture);
    }
    return *texture;
}

static void v86gl_bind_default_textures(void) {
    static const GLenum targets[] = {
        GL_TEXTURE_1D,
        GL_TEXTURE_2D,
        GL_TEXTURE_3D,
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_RECTANGLE_ARB,
    };
    GLsizei i;
    uint32_t target_index;

    for (i = 0; i < 8; i++) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + i));
        for (target_index = 0; target_index < sizeof(targets) / sizeof(targets[0]); target_index++) {
            GLenum target = targets[target_index];
            GLuint texture = v86gl_default_texture(target);
            if (texture) {
                glBindTexture(target, texture);
            }
        }
    }
    glActiveTexture(GL_TEXTURE0);
}

static void v86gl_apply_gl4es_context_hints(void) {
#ifdef __EMSCRIPTEN__
    if (g_webgl_major_version >= 2) {
        /* gl4es cannot run its normal hardware probe under Emscripten, so it
         * falls back to limited NPOT. WebGL2 has full NPOT texture support,
         * and the guest proxy already advertises GL_ARB_texture_non_power_of_two. */
        hardext.npot = 3;
        globals4es.npot = 2;
        globals4es.defaultwrap = 0;
        globals4es.forcenpot = 0;
        printf("[v86gl] WebGL2 full NPOT override enabled\n");
    }
#endif
}

static int v86gl_ensure_ready(void) {
    if (g_ready) {
        return 1;
    }

#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = EM_FALSE;
    attrs.depth = EM_TRUE;
    attrs.stencil = EM_TRUE;
    attrs.antialias = EM_FALSE;
    attrs.premultipliedAlpha = EM_FALSE;
    attrs.preserveDrawingBuffer = EM_TRUE;
    /* Texture3D requires WebGL2.  Prefer it when available, while retaining
     * the WebGL1 fallback for browsers that cannot create a WebGL2 canvas. */
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;

    g_webgl_context = emscripten_webgl_create_context("#v86gl_canvas", &attrs);
    if (g_webgl_context > 0) {
        g_webgl_major_version = 2;
    }

    if (g_webgl_context <= 0) {
        attrs.majorVersion = 1;
        g_webgl_context = emscripten_webgl_create_context("#v86gl_canvas", &attrs);
        if (g_webgl_context > 0) {
            g_webgl_major_version = 1;
        }
    }

    if (g_webgl_context > 0) {
        emscripten_webgl_make_context_current(g_webgl_context);
    } else {
        return 0;
    }
#endif

    initialize_gl4es();
    v86gl_apply_gl4es_context_hints();
    /* Preserve explicit mip chains uploaded by WineD3D.  Mode 3 makes gl4es
     * ignore every level above zero and demotes mip min-filters, which keeps
     * incomplete compressed textures visible but breaks ordinary D3D8 mip
     * selection.  Compressed formats stay conservative through the advertised
     * capability profile and v86gl_glGenerateMipmap() below. */
    glHint(GL_MIPMAP_HINT_GL4ES, 0);
    v86gl_bind_default_textures();
    g_ready = 1;
    return 1;
}

static GLuint v86gl_host_texture(GLuint guest, int create) {
    uint32_t i;
    GLuint host;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_texture_count; i++) {
        if (g_textures[i].guest == guest) {
            return g_textures[i].host;
        }
    }

    if (!create || g_texture_count >= V86GL_MAX_TEXTURES) {
        return 0;
    }

    glGenTextures(1, &host);
    g_textures[g_texture_count].guest = guest;
    g_textures[g_texture_count].host = host;
    g_texture_count++;
    return host;
}

static void v86gl_forget_texture(GLuint guest) {
    uint32_t i;

    for (i = 0; i < g_texture_count; i++) {
        if (g_textures[i].guest == guest) {
            g_textures[i] = g_textures[g_texture_count - 1u];
            g_texture_count--;
            return;
        }
    }
}

static GLuint v86gl_host_program(GLuint guest) {
    uint32_t i;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_program_count; i++) {
        if (g_programs[i].guest == guest) {
            return g_programs[i].host;
        }
    }
    return 0;
}

static GLuint v86gl_host_arb_program(GLuint guest, int create) {
    uint32_t i;
    GLuint host = 0;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_arb_program_count; i++) {
        if (g_arb_programs[i].guest == guest) {
            return g_arb_programs[i].host;
        }
    }

    if (!create || g_arb_program_count >= V86GL_MAX_ARB_PROGRAMS) {
        return 0;
    }

    gl4es_glGenProgramsARB(1, &host);
    if (!host) {
        return 0;
    }
    g_arb_programs[g_arb_program_count].guest = guest;
    g_arb_programs[g_arb_program_count].host = host;
    g_arb_program_count++;
    return host;
}

static GLuint v86gl_host_shader(GLuint guest) {
    uint32_t i;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_shader_count; i++) {
        if (g_shaders[i].guest == guest) {
            return g_shaders[i].host;
        }
    }
    return 0;
}

static GLuint v86gl_host_framebuffer(GLuint guest) {
    uint32_t i;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_framebuffer_count; i++) {
        if (g_framebuffers[i].guest == guest) {
            return g_framebuffers[i].host;
        }
    }
    return 0;
}

static GLuint v86gl_host_renderbuffer(GLuint guest) {
    uint32_t i;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_renderbuffer_count; i++) {
        if (g_renderbuffers[i].guest == guest) {
            return g_renderbuffers[i].host;
        }
    }
    return 0;
}

static GLuint v86gl_host_query(GLuint guest) {
    uint32_t i;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_query_count; i++) {
        if (g_queries[i].guest == guest) {
            return g_queries[i].host;
        }
    }
    return 0;
}

static GLuint v86gl_host_buffer(GLuint guest, int create) {
    uint32_t i;
    GLuint host = 0;

    if (!guest) {
        return 0;
    }

    for (i = 0; i < g_buffer_count; i++) {
        if (g_buffers[i].guest == guest) {
            return g_buffers[i].host;
        }
    }

    if (!create || g_buffer_count >= V86GL_MAX_BUFFERS) {
        return 0;
    }

    glGenBuffers(1, &host);
    if (!host) {
        return 0;
    }
    g_buffers[g_buffer_count].guest = guest;
    g_buffers[g_buffer_count].host = host;
    g_buffer_count++;
    return host;
}

static int v86gl_occlusion_queries_supported(void) {
    return g_webgl_major_version >= 2;
}

static GLenum v86gl_webgl_query_target(GLenum target) {
    switch (target) {
    case GL_SAMPLES_PASSED:
    case GL_ANY_SAMPLES_PASSED:
        return GL_ANY_SAMPLES_PASSED;
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        return GL_ANY_SAMPLES_PASSED_CONSERVATIVE;
    default:
        return 0;
    }
}

static void v86gl_map_name(V86GLNameMap* maps, uint32_t* count,
                           uint32_t capacity, GLuint guest, GLuint host) {
    uint32_t i;

    if (!guest) {
        return;
    }

    for (i = 0; i < *count; i++) {
        if (maps[i].guest == guest) {
            maps[i].host = host;
            return;
        }
    }

    if (*count < capacity) {
        maps[*count].guest = guest;
        maps[*count].host = host;
        (*count)++;
    }
}

static void v86gl_forget_name(V86GLNameMap* maps, uint32_t* count, GLuint guest) {
    uint32_t i;

    for (i = 0; i < *count; i++) {
        if (maps[i].guest == guest) {
            maps[i] = maps[*count - 1u];
            (*count)--;
            return;
        }
    }
}

static void v86gl_map_location(V86GLLocationMap* maps, uint32_t* count,
                               uint32_t capacity, GLint guest, GLint host) {
    uint32_t i;

    if (guest < 0) {
        return;
    }

    for (i = 0; i < *count; i++) {
        if (maps[i].guest == guest) {
            maps[i].host = host;
            return;
        }
    }

    if (*count < capacity) {
        maps[*count].guest = guest;
        maps[*count].host = host;
        (*count)++;
    }
}

static GLint v86gl_host_uniform_location(GLint guest) {
    uint32_t i;

    if (guest < 0) {
        return -1;
    }

    for (i = 0; i < g_uniform_location_count; i++) {
        if (g_uniform_locations[i].guest == guest) {
            return g_uniform_locations[i].host;
        }
    }
    return -1;
}

static GLint v86gl_host_attrib_index(GLint guest) {
    uint32_t i;

    if (guest < 0) {
        return -1;
    }

    for (i = 0; i < g_attrib_location_count; i++) {
        if (g_attrib_locations[i].guest == guest) {
            return g_attrib_locations[i].host;
        }
    }

    return guest < V86GL_MAX_VERTEX_ATTRIBS ? guest : -1;
}

static char* v86gl_copy_name(GLsizei length, const char* name) {
    char* out;

    if (length < 0 || (length > 0 && !name)) {
        return 0;
    }

    out = (char*)malloc((size_t)length + 1u);
    if (!out) {
        return 0;
    }

    if (length > 0) {
        memcpy(out, name, (size_t)length);
    }
    out[length] = '\0';
    return out;
}

static void v86gl_reset_gl2_maps(void) {
    g_program_count = 0;
    g_arb_program_count = 0;
    g_shader_count = 0;
    g_uniform_location_count = 0;
    g_attrib_location_count = 0;
    g_framebuffer_count = 0;
    g_renderbuffer_count = 0;
    g_query_count = 0;
    g_buffer_count = 0;
}

EMSCRIPTEN_KEEPALIVE
void v86glMakeCurrent(uint32_t hwnd, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    g_surface_hwnd = hwnd;
    g_surface_x = x;
    g_surface_y = y;
    g_surface_width = width;
    g_surface_height = height;
    (void)v86gl_ensure_ready();
}

EMSCRIPTEN_KEEPALIVE
void v86glResize(uint32_t width, uint32_t height) {
    g_surface_width = width;
    g_surface_height = height;
    (void)v86gl_ensure_ready();
}

EMSCRIPTEN_KEEPALIVE
void v86glReleaseCurrent(void) {
    g_surface_hwnd = 0;
}

/*
 * A guest WGL context must not share the previous process' gl4es state.
 * gl4es' static initialisation is intentionally one-shot, so the browser
 * bridge discards this entire WASM instance after this call and creates a
 * fresh one for the next guest context.
 */
EMSCRIPTEN_KEEPALIVE
void v86glDestroyRenderer(void) {
    if (!g_ready) {
        return;
    }

    glFlush();
    close_gl4es();
    g_ready = 0;
    g_surface_hwnd = 0;
    g_surface_x = 0;
    g_surface_y = 0;
    g_surface_width = 0;
    g_surface_height = 0;
    g_texture_count = 0;
    v86gl_reset_gl2_maps();
    g_default_texture_1d = 0;
    g_default_texture_2d = 0;
    g_default_texture_3d = 0;
    g_default_texture_cube = 0;
    g_default_texture_rectangle = 0;
    g_webgl_major_version = 0;

#ifdef __EMSCRIPTEN__
    if (g_webgl_context > 0) {
        /* The canvas is replaced by the JS bridge; lose this context now so
         * repeated guest process exits cannot exhaust the browser's context
         * limit while waiting for garbage collection. */
        v86gl_lose_webgl_context();
        emscripten_webgl_make_context_current(0);
        emscripten_webgl_destroy_context(g_webgl_context);
        g_webgl_context = 0;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!v86gl_ensure_ready()) return;
    glViewport(x, y, width, height);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (!v86gl_ensure_ready()) return;
    glClearColor(red, green, blue, alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClear(GLbitfield mask) {
    if (!v86gl_ensure_ready()) return;
    glClear(mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBegin(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glBegin(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glEnd(void) {
    if (!v86gl_ensure_ready()) return;
    glEnd();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (!v86gl_ensure_ready()) return;
    glColor4f(red, green, blue, alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (!v86gl_ensure_ready()) return;
    glVertex3f(x, y, z);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (!v86gl_ensure_ready()) return;
    glVertex4f(x, y, z, w);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFlush(void) {
    if (!v86gl_ensure_ready()) return;
    glFlush();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFinish(void) {
    if (!v86gl_ensure_ready()) return;
    glFinish();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMatrixMode(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glMatrixMode(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLoadIdentity(void) {
    if (!v86gl_ensure_ready()) return;
    glLoadIdentity();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLoadMatrixf(const GLfloat* m) {
    if (!v86gl_ensure_ready()) return;
    if (!m) return;
    glLoadMatrixf(m);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMultMatrixf(const GLfloat* m) {
    if (!v86gl_ensure_ready()) return;
    if (!m) return;
    glMultMatrixf(m);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                     GLdouble zNear, GLdouble zFar) {
    if (!v86gl_ensure_ready()) return;
    glFrustum(left, right, bottom, top, zNear, zFar);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                   GLdouble zNear, GLdouble zFar) {
    if (!v86gl_ensure_ready()) return;
    glOrtho(left, right, bottom, top, zNear, zFar);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (!v86gl_ensure_ready()) return;
    glTranslatef(x, y, z);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    if (!v86gl_ensure_ready()) return;
    glRotatef(angle, x, y, z);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glScalef(GLfloat x, GLfloat y, GLfloat z) {
    if (!v86gl_ensure_ready()) return;
    glScalef(x, y, z);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPushMatrix(void) {
    if (!v86gl_ensure_ready()) return;
    glPushMatrix();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPopMatrix(void) {
    if (!v86gl_ensure_ready()) return;
    glPopMatrix();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glEnable(GLenum cap) {
    if (!v86gl_ensure_ready()) return;
    glEnable(cap);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDisable(GLenum cap) {
    if (!v86gl_ensure_ready()) return;
    glDisable(cap);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDepthFunc(GLenum func) {
    if (!v86gl_ensure_ready()) return;
    glDepthFunc(func);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClearDepth(GLdouble depth) {
    if (!v86gl_ensure_ready()) return;
    glClearDepth(depth);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDepthRange(GLdouble zNear, GLdouble zFar) {
    if (!v86gl_ensure_ready()) return;
    glDepthRange(zNear, zFar);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClearStencil(GLint s) {
    if (!v86gl_ensure_ready()) return;
    glClearStencil(s);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    if (!v86gl_ensure_ready()) return;
    glStencilFunc(func, ref, mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glStencilMask(GLuint mask) {
    if (!v86gl_ensure_ready()) return;
    glStencilMask(mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    if (!v86gl_ensure_ready()) return;
    glStencilOp(fail, zfail, zpass);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glShadeModel(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glShadeModel(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCullFace(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glCullFace(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFrontFace(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glFrontFace(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glHint(GLenum target, GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glHint(target, mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPushAttrib(GLbitfield mask) {
    if (!v86gl_ensure_ready()) return;
    glPushAttrib(mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPopAttrib(void) {
    if (!v86gl_ensure_ready()) return;
    glPopAttrib();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPushClientAttrib(GLbitfield mask) {
    if (!v86gl_ensure_ready()) return;
    glPushClientAttrib(mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPopClientAttrib(void) {
    if (!v86gl_ensure_ready()) return;
    glPopClientAttrib();
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawBuffer(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glDrawBuffer(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glReadBuffer(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glReadBuffer(mode);
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, void* pixels) {
    if (!pixels || width < 0 || height < 0 || !v86gl_ensure_ready()) {
        return 0;
    }

    glReadPixels(x, y, width, height, format, type, pixels);
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenTextures(GLsizei n, const GLuint* textures) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n <= 0 || !textures) return;

    for (i = 0; i < n; i++) {
        (void)v86gl_host_texture(textures[i], 1);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteTextures(GLsizei n, const GLuint* textures) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n <= 0 || !textures) return;

    for (i = 0; i < n; i++) {
        GLuint host = v86gl_host_texture(textures[i], 0);
        if (host) {
            glDeleteTextures(1, &host);
            v86gl_forget_texture(textures[i]);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBindTexture(GLenum target, GLuint texture) {
    GLuint host;

    if (!v86gl_ensure_ready()) return;
    host = texture == 0 ? v86gl_default_texture(target) :
        v86gl_host_texture(texture, 1);
    glBindTexture(v86gl_binding_target(target), host);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenBuffersMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n <= 0 || !guest_names) return;

    for (i = 0; i < n; i++) {
        (void)v86gl_host_buffer(guest_names[i], 1);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteBuffersMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n <= 0 || !guest_names) return;

    for (i = 0; i < n; i++) {
        GLuint host = v86gl_host_buffer(guest_names[i], 0);
        if (host) {
            glDeleteBuffers(1, &host);
            v86gl_forget_name(g_buffers, &g_buffer_count, guest_names[i]);
        }
    }
}

#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif

EMSCRIPTEN_KEEPALIVE
void v86gl_glBindBufferMapped(GLenum target, GLuint guest_buffer) {
    GLuint host;

    if (!v86gl_ensure_ready()) return;

    /* The proxy always resolves GL_PIXEL_PACK_BUFFER/GL_PIXEL_UNPACK_BUFFER
     * offsets to real bytes before a record ever reaches the wire (see
     * unpack_pixel_pointer()/pack_pixel_pointer() in opengl32_proxy.c), so
     * every TexImage/TexSubImage/ReadPixels call below always receives a
     * real heap pointer. Forwarding the guest's bind would leave that target
     * bound on the real WebGL2 context, which then reinterprets those real
     * pointers as byte offsets into the bound buffer instead -- corrupting
     * every subsequent pixel transfer and leaving render-target textures
     * with incomplete storage (this is what produced the repeated
     * "Framebuffer is incomplete" errors seen after a guest PBO upload).
     * gl4es does not track these two targets either (see its own
     * "unhandled Buffer type" warning), so there is no functional reason to
     * forward them; keep them permanently unbound instead. */
    if (target == GL_PIXEL_PACK_BUFFER || target == GL_PIXEL_UNPACK_BUFFER) {
        return;
    }

    host = guest_buffer ? v86gl_host_buffer(guest_buffer, 1) : 0;
    glBindBuffer(target, host);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBufferDataMapped(GLenum target, GLsizeiptr size, GLenum usage,
                              GLsizeiptr data_size, const void* data) {
    const void* upload = NULL;

    if (!v86gl_ensure_ready()) return;
    if (size < 0 || data_size < 0 || data_size > size) return;
    if (target == GL_PIXEL_PACK_BUFFER || target == GL_PIXEL_UNPACK_BUFFER) {
        return;
    }
    if (data && data_size == size && data_size > 0) {
        upload = data;
    }
    glBufferData(target, size, upload, usage);
    if (data && data_size > 0 && data_size < size) {
        glBufferSubData(target, 0, data_size, data);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBufferSubDataMapped(GLenum target, GLintptr offset, GLsizeiptr size,
                                 GLsizeiptr data_size, const void* data) {
    if (!v86gl_ensure_ready()) return;
    if (target == GL_PIXEL_PACK_BUFFER || target == GL_PIXEL_UNPACK_BUFFER) {
        return;
    }
    if (offset < 0 || size < 0 || data_size != size || (size && !data)) return;
    if (size) {
        glBufferSubData(target, offset, size, data);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glVertexPointerVBO(GLint size, GLenum type, GLsizei stride,
                              uintptr_t offset) {
    if (!v86gl_ensure_ready()) return;
    glVertexPointer(size, type, stride, (const void*)offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glColorPointerVBO(GLint size, GLenum type, GLsizei stride,
                             uintptr_t offset) {
    if (!v86gl_ensure_ready()) return;
    glColorPointer(size, type, stride, (const void*)offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexCoordPointerVBO(GLint size, GLenum type, GLsizei stride,
                                uintptr_t offset) {
    if (!v86gl_ensure_ready()) return;
    glTexCoordPointer(size, type, stride, (const void*)offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glNormalPointerVBO(GLint size, GLenum type, GLsizei stride,
                              uintptr_t offset) {
    (void)size;
    if (!v86gl_ensure_ready()) return;
    glNormalPointer(type, stride, (const void*)offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glSecondaryColorPointerVBO(GLint size, GLenum type, GLsizei stride,
                                      uintptr_t offset) {
    if (!v86gl_ensure_ready()) return;
    glSecondaryColorPointer(size, type, stride, (const void*)offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFogCoordPointerVBO(GLint size, GLenum type, GLsizei stride,
                                uintptr_t offset) {
    (void)size;
    if (!v86gl_ensure_ready()) return;
    glFogCoordPointer(type, stride, (const void*)offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glVertexAttribPointerMapped(GLuint guest_index, GLint size,
                                       GLenum type, GLboolean normalized,
                                       GLsizei stride, uintptr_t offset) {
    GLint host_index;

    if (!v86gl_ensure_ready()) return;
    host_index = v86gl_host_attrib_index((GLint)guest_index);
    if (host_index >= 0) {
        glVertexAttribPointer((GLuint)host_index, size, type, normalized,
                              stride, (const void*)offset);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                        GLsizei width, GLsizei height, GLint border,
                        GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexImage1D(GLenum target, GLint level, GLint internalformat,
                        GLsizei width, GLint border,
                        GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexImage1D(target, level, internalformat, width, border, format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                  GLsizei width, GLsizei height, GLint border,
                                  GLsizei image_size, const void* data) {
    if (!v86gl_ensure_ready()) return;
    glCompressedTexImage2D(target, level, internalformat, width, height, border,
                           image_size, data);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat,
                                  GLsizei width, GLint border, GLsizei image_size,
                                  const void* data) {
    if (!v86gl_ensure_ready()) return;
    glCompressedTexImage1D(target, level, internalformat, width, border, image_size, data);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                                  GLsizei image_size, const void* data) {
    if (!v86gl_ensure_ready()) return;
    glCompressedTexImage3D(target, level, internalformat, width, height, depth, border,
                           image_size, data);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                     GLsizei width, GLenum format, GLsizei image_size,
                                     const void* data) {
    if (!v86gl_ensure_ready()) return;
    glCompressedTexSubImage1D(target, level, xoffset, width, format, image_size, data);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                     GLint yoffset, GLsizei width, GLsizei height,
                                     GLenum format, GLsizei image_size, const void* data) {
    if (!v86gl_ensure_ready()) return;
    glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format,
                              image_size, data);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                     GLint yoffset, GLint zoffset, GLsizei width,
                                     GLsizei height, GLsizei depth, GLenum format,
                                     GLsizei image_size, const void* data) {
    if (!v86gl_ensure_ready()) return;
    glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height,
                              depth, format, image_size, data);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexImage3D(GLenum target, GLint level, GLint internalformat,
                        GLsizei width, GLsizei height, GLsizei depth, GLint border,
                        GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexImage3D(target, level, internalformat, width, height, depth, border,
                 format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                           GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                           GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth,
                    format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                           GLsizei width, GLsizei height,
                           GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                           GLsizei width, GLenum format, GLenum type,
                           const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexSubImage1D(target, level, xoffset, width, format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLint x, GLint y, GLsizei width, GLsizei height,
                            GLint border) {
    if (!v86gl_ensure_ready()) return;
    glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                            GLint x, GLint y, GLsizei width, GLint border) {
    if (!v86gl_ensure_ready()) return;
    glCopyTexImage1D(target, level, internalformat, x, y, width, border);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCopyTexSubImage2D(GLenum target, GLint level,
                               GLint xoffset, GLint yoffset,
                               GLint x, GLint y,
                               GLsizei width, GLsizei height) {
    if (!v86gl_ensure_ready()) return;
    glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                               GLint x, GLint y, GLsizei width) {
    if (!v86gl_ensure_ready()) return;
    glCopyTexSubImage1D(target, level, xoffset, x, y, width);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glTexParameteri(target, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glTexParameterf(target, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexParameteriv4(GLenum target, GLenum pname, GLsizei count,
                             GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glTexParameteriv(target, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexParameterfv4(GLenum target, GLenum pname, GLsizei count,
                             GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glTexParameterfv(target, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelStorei(GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glPixelStorei(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexEnvi(GLenum target, GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glTexEnvi(target, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glTexEnvf(target, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexEnviv4(GLenum target, GLenum pname, GLsizei count,
                       GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glTexEnviv(target, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexEnvfv4(GLenum target, GLenum pname, GLsizei count,
                       GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glTexEnvfv(target, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexCoord2f(GLfloat s, GLfloat t) {
    if (!v86gl_ensure_ready()) return;
    glTexCoord2f(s, t);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    if (!v86gl_ensure_ready()) return;
    glTexCoord4f(s, t, r, q);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexGeni(GLenum coord, GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glTexGeni(coord, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexGenf(GLenum coord, GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glTexGenf(coord, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexGeniv4(GLenum coord, GLenum pname, GLsizei count,
                       GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glTexGeniv(coord, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexGenfv4(GLenum coord, GLenum pname, GLsizei count,
                       GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glTexGenfv(coord, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClipPlane4d(GLenum plane, GLdouble v0, GLdouble v1,
                         GLdouble v2, GLdouble v3) {
    GLdouble values[4];
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glClipPlane(plane, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glColorMaterial(GLenum face, GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glColorMaterial(face, mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glActiveTexture(GLenum texture) {
    if (!v86gl_ensure_ready()) return;
    glActiveTexture(texture);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClientActiveTexture(GLenum texture) {
    if (!v86gl_ensure_ready()) return;
    glClientActiveTexture(texture);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    if (!v86gl_ensure_ready()) return;
    glMultiTexCoord4f(target, s, t, r, q);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    if (!v86gl_ensure_ready()) return;
    glNormal3f(nx, ny, nz);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFogf(GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glFogf(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFogi(GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glFogi(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFogfv4(GLenum pname, GLsizei count,
                    GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glFogfv(pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glMaterialf(face, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMateriali(GLenum face, GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glMateriali(face, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMaterialfv4(GLenum face, GLenum pname, GLsizei count,
                         GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glMaterialfv(face, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMaterialiv4(GLenum face, GLenum pname, GLsizei count,
                         GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glMaterialiv(face, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightf(GLenum light, GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glLightf(light, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLighti(GLenum light, GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glLighti(light, pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightfv4(GLenum light, GLenum pname, GLsizei count,
                      GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glLightfv(light, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightiv4(GLenum light, GLenum pname, GLsizei count,
                      GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glLightiv(light, pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightModelf(GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glLightModelf(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightModeli(GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glLightModeli(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightModelfv4(GLenum pname, GLsizei count,
                           GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glLightModelfv(pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLightModeliv4(GLenum pname, GLsizei count,
                           GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4];
    (void)count;
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    values[3] = v3;
    glLightModeliv(pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBlendFunc(GLenum sfactor, GLenum dfactor) {
    if (!v86gl_ensure_ready()) return;
    glBlendFunc(sfactor, dfactor);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (!v86gl_ensure_ready()) return;
    glBlendColor(red, green, blue, alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBlendEquation(GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glBlendEquation(mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb,
                               GLenum src_alpha, GLenum dst_alpha) {
    if (!v86gl_ensure_ready()) return;
    glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glSampleCoverage(GLclampf value, GLboolean invert) {
    if (!v86gl_ensure_ready()) return;
    glSampleCoverage(value, invert);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenerateMipmap(GLenum target) {
    if (!v86gl_ensure_ready()) return;
    /* The browser cannot generate mipmaps for several legacy compressed
     * formats.  Sampling an incomplete mip chain is black, so retain level 0
     * instead of issuing a WebGL-invalid generate call. */
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFogCoordf(GLfloat coord) {
    if (!v86gl_ensure_ready()) return;
    glFogCoordf(coord);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glSecondaryColor3f(GLfloat red, GLfloat green, GLfloat blue) {
    if (!v86gl_ensure_ready()) return;
    glSecondaryColor3f(red, green, blue);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPointParameterf(GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glPointParameterf(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPointParameterfv3(GLenum pname, GLfloat v0, GLfloat v1, GLfloat v2) {
    GLfloat values[3];
    if (!v86gl_ensure_ready()) return;
    values[0] = v0;
    values[1] = v1;
    values[2] = v2;
    glPointParameterfv(pname, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPointParameteri(GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glPointParameteri(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPointParameteriv(GLenum pname, const GLint* params) {
    if (!params || !v86gl_ensure_ready()) return;
    glPointParameteriv(pname, params);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glWindowPos3f(GLfloat x, GLfloat y, GLfloat z) {
    if (!v86gl_ensure_ready()) return;
    glWindowPos3f(x, y, z);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (!v86gl_ensure_ready()) return;
    glRasterPos4f(x, y, z, w);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPointSize(GLfloat size) {
    if (!v86gl_ensure_ready()) return;
    glPointSize(size);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLineStipple(GLint factor, GLushort pattern) {
    if (!v86gl_ensure_ready()) return;
    glLineStipple(factor, pattern);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLogicOp(GLenum opcode) {
    if (!v86gl_ensure_ready()) return;
    glLogicOp(opcode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelTransferf(GLenum pname, GLfloat param) {
    if (!v86gl_ensure_ready()) return;
    glPixelTransferf(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelTransferi(GLenum pname, GLint param) {
    if (!v86gl_ensure_ready()) return;
    glPixelTransferi(pname, param);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelZoom(GLfloat xfactor, GLfloat yfactor) {
    if (!v86gl_ensure_ready()) return;
    glPixelZoom(xfactor, yfactor);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat* values) {
    if ((mapsize > 0 && !values) || !v86gl_ensure_ready()) return;
    glPixelMapfv(map, mapsize, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint* values) {
    if ((mapsize > 0 && !values) || !v86gl_ensure_ready()) return;
    glPixelMapuiv(map, mapsize, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort* values) {
    if ((mapsize > 0 && !values) || !v86gl_ensure_ready()) return;
    glPixelMapusv(map, mapsize, values);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawPixels(GLsizei width, GLsizei height, GLenum format,
                        GLenum type, const GLvoid* pixels) {
    if (!v86gl_ensure_ready()) return;
    glDrawPixels(width, height, format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig,
                    GLfloat xmove, GLfloat ymove, const GLubyte* bitmap) {
    if (!v86gl_ensure_ready()) return;
    glBitmap(width, height, xorig, yorig, xmove, ymove, bitmap);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                        GLenum type) {
    if (!v86gl_ensure_ready()) return;
    glCopyPixels(x, y, width, height, type);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (!v86gl_ensure_ready()) return;
    glClearAccum(red, green, blue, alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glAccum(GLenum op, GLfloat value) {
    if (!v86gl_ensure_ready()) return;
    glAccum(op, value);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPolygonStipple(const GLubyte* mask) {
    if (!mask || !v86gl_ensure_ready()) return;
    glPolygonStipple(mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glAlphaFunc(GLenum func, GLclampf ref) {
    if (!v86gl_ensure_ready()) return;
    glAlphaFunc(func, ref);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDepthMask(GLboolean flag) {
    if (!v86gl_ensure_ready()) return;
    glDepthMask(flag);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    if (!v86gl_ensure_ready()) return;
    glColorMask(red, green, blue, alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!v86gl_ensure_ready()) return;
    glScissor(x, y, width, height);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLineWidth(GLfloat width) {
    if (!v86gl_ensure_ready()) return;
    glLineWidth(width);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPolygonMode(GLenum face, GLenum mode) {
    if (!v86gl_ensure_ready()) return;
    glPolygonMode(face, mode);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glPolygonOffset(GLfloat factor, GLfloat units) {
    if (!v86gl_ensure_ready()) return;
    glPolygonOffset(factor, units);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glEnableClientState(GLenum array) {
    if (!v86gl_ensure_ready()) return;
    glEnableClientState(array);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDisableClientState(GLenum array) {
    if (!v86gl_ensure_ready()) return;
    glDisableClientState(array);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha) {
    if (!v86gl_ensure_ready()) return;
    glBlendEquationSeparate(mode_rgb, mode_alpha);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawBuffers(GLsizei n, const GLenum* bufs) {
    if (!v86gl_ensure_ready()) return;
    if (n > 0 && !bufs) return;
    glDrawBuffers(n, bufs);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) {
    if (!v86gl_ensure_ready()) return;
    glStencilOpSeparate(face, fail, zfail, zpass);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    if (!v86gl_ensure_ready()) return;
    glStencilFuncSeparate(face, func, ref, mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glStencilMaskSeparate(GLenum face, GLuint mask) {
    if (!v86gl_ensure_ready()) return;
    glStencilMaskSeparate(face, mask);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenProgramsARBMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    for (i = 0; i < n; i++) {
        GLuint host = 0;
        if (!guest_names[i] || v86gl_host_arb_program(guest_names[i], 0) ||
            g_arb_program_count >= V86GL_MAX_ARB_PROGRAMS) {
            continue;
        }
        gl4es_glGenProgramsARB(1, &host);
        if (host) {
            v86gl_map_name(g_arb_programs, &g_arb_program_count,
                           V86GL_MAX_ARB_PROGRAMS, guest_names[i], host);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteProgramsARBMapped(GLsizei n, const GLuint* guest_names) {
    GLuint* host_names;
    GLsizei i;
    GLsizei host_count = 0;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    if (!n) return;

    host_names = (GLuint*)malloc((size_t)n * sizeof(GLuint));
    if (!host_names) return;

    for (i = 0; i < n; i++) {
        GLuint host = v86gl_host_arb_program(guest_names[i], 0);
        if (host) {
            host_names[host_count++] = host;
        }
        v86gl_forget_name(g_arb_programs, &g_arb_program_count, guest_names[i]);
    }

    if (host_count > 0) {
        gl4es_glDeleteProgramsARB(host_count, host_names);
    }
    free(host_names);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBindProgramARBMapped(GLenum target, GLuint guest_program) {
    GLuint host_program = 0;

    if (!v86gl_ensure_ready()) return;
    if (guest_program) {
        host_program = v86gl_host_arb_program(guest_program, 1);
        if (!host_program) return;
    }
    gl4es_glBindProgramARB(target, host_program);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glProgramStringARB(GLenum target, GLenum format, GLsizei length,
                              const GLvoid* string) {
    if (!v86gl_ensure_ready()) return;
    if (length < 0 || (length > 0 && !string)) return;
    gl4es_glProgramStringARB(target, format, length, string ? string : "");
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glProgramParameterfvARB(uint32_t parameter_kind, GLenum target,
                                   GLuint index, GLsizei count,
                                   const GLfloat* params) {
    if (!v86gl_ensure_ready()) return;
    if (count < 0 || (count > 0 && !params)) return;
    if (!count) return;

    if (parameter_kind == V86GL_PROGRAM_PARAMETER_ENV) {
        if (count == 1) {
            gl4es_glProgramEnvParameter4fvARB(target, index, params);
        } else {
            gl4es_glProgramEnvParameters4fvEXT(target, index, count, params);
        }
    } else if (parameter_kind == V86GL_PROGRAM_PARAMETER_LOCAL) {
        if (count == 1) {
            gl4es_glProgramLocalParameter4fvARB(target, index, params);
        } else {
            gl4es_glProgramLocalParameters4fvEXT(target, index, count, params);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glProgramParameterdvARB(uint32_t parameter_kind, GLenum target,
                                   GLuint index, const GLdouble* params) {
    if (!v86gl_ensure_ready() || !params) return;

    if (parameter_kind == V86GL_PROGRAM_PARAMETER_ENV) {
        gl4es_glProgramEnvParameter4dvARB(target, index, params);
    } else if (parameter_kind == V86GL_PROGRAM_PARAMETER_LOCAL) {
        gl4es_glProgramLocalParameter4dvARB(target, index, params);
    }
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glGetProgramivARB(GLenum target, GLenum pname, GLint* params) {
    if (!v86gl_ensure_ready() || !params) return 0;
    gl4es_glGetProgramivARB(target, pname, params);
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glGetProgramParameterfvARB(uint32_t parameter_kind, GLenum target,
                                     GLuint index, GLfloat* params) {
    if (!v86gl_ensure_ready() || !params) return 0;

    if (parameter_kind == V86GL_PROGRAM_PARAMETER_ENV) {
        gl4es_glGetProgramEnvParameterfvARB(target, index, params);
        return 1;
    }
    if (parameter_kind == V86GL_PROGRAM_PARAMETER_LOCAL) {
        gl4es_glGetProgramLocalParameterfvARB(target, index, params);
        return 1;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glGetProgramParameterdvARB(uint32_t parameter_kind, GLenum target,
                                     GLuint index, GLdouble* params) {
    if (!v86gl_ensure_ready() || !params) return 0;

    if (parameter_kind == V86GL_PROGRAM_PARAMETER_ENV) {
        gl4es_glGetProgramEnvParameterdvARB(target, index, params);
        return 1;
    }
    if (parameter_kind == V86GL_PROGRAM_PARAMETER_LOCAL) {
        gl4es_glGetProgramLocalParameterdvARB(target, index, params);
        return 1;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glGetProgramStringARB(GLenum target, GLenum pname, GLsizei bufSize,
                                GLsizei* length, GLvoid* string) {
    GLint program_length = 0;

    if (!v86gl_ensure_ready() || bufSize < 0) return 0;
    gl4es_glGetProgramivARB(target, GL_PROGRAM_LENGTH_ARB, &program_length);
    if (length) {
        *length = program_length;
    }
    if (bufSize > 0 && string) {
        gl4es_glGetProgramStringARB(target, pname, string);
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glQueryString(GLenum name, GLsizei bufSize, GLsizei* length,
                        GLchar* string) {
    const GLubyte* value;
    size_t value_length;
    size_t copy_length;

    if (!v86gl_ensure_ready() || bufSize < 0) return 0;
    value = glGetString(name);
    if (!value) return 0;

    value_length = strlen((const char*)value);
    if (length) {
        *length = (GLsizei)value_length;
    }
    if (bufSize > 0 && string) {
        copy_length = value_length < (size_t)(bufSize - 1) ? value_length : (size_t)(bufSize - 1);
        if (copy_length) {
            memcpy(string, value, copy_length);
        }
        string[copy_length] = '\0';
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glQueryInteger(GLenum pname, GLint* value) {
    if (!v86gl_ensure_ready() || !value) return 0;
    glGetIntegerv(pname, value);
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCreateProgramMapped(GLuint guest_program) {
    GLuint host_program;

    if (!v86gl_ensure_ready()) return;
    host_program = glCreateProgram();
    v86gl_map_name(g_programs, &g_program_count, V86GL_MAX_PROGRAMS,
                   guest_program, host_program);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCreateShaderMapped(GLuint guest_shader, GLenum type) {
    GLuint host_shader;

    if (!v86gl_ensure_ready()) return;
    host_shader = glCreateShader(type);
    v86gl_map_name(g_shaders, &g_shader_count, V86GL_MAX_SHADERS,
                   guest_shader, host_shader);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteProgramMapped(GLuint guest_program) {
    GLuint host_program;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    if (host_program) {
        glDeleteProgram(host_program);
        v86gl_forget_name(g_programs, &g_program_count, guest_program);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteShaderMapped(GLuint guest_shader) {
    GLuint host_shader;

    if (!v86gl_ensure_ready()) return;
    host_shader = v86gl_host_shader(guest_shader);
    if (host_shader) {
        glDeleteShader(host_shader);
        v86gl_forget_name(g_shaders, &g_shader_count, guest_shader);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glAttachShaderMapped(GLuint guest_program, GLuint guest_shader) {
    GLuint host_program;
    GLuint host_shader;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    host_shader = v86gl_host_shader(guest_shader);
    if (host_program && host_shader) {
        glAttachShader(host_program, host_shader);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDetachShaderMapped(GLuint guest_program, GLuint guest_shader) {
    GLuint host_program;
    GLuint host_shader;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    host_shader = v86gl_host_shader(guest_shader);
    if (host_program && host_shader) {
        glDetachShader(host_program, host_shader);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glShaderSourceMapped(GLuint guest_shader, GLsizei length, const char* source) {
    GLuint host_shader;
    const char* strings[1];
    GLint lengths[1];

    if (!v86gl_ensure_ready()) return;
    host_shader = v86gl_host_shader(guest_shader);
    if (!host_shader || length < 0 || (length > 0 && !source)) {
        return;
    }

    strings[0] = source ? source : "";
    lengths[0] = length;
    glShaderSource(host_shader, 1, strings, lengths);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glCompileShaderMapped(GLuint guest_shader) {
    GLuint host_shader;

    if (!v86gl_ensure_ready()) return;
    host_shader = v86gl_host_shader(guest_shader);
    if (host_shader) {
        glCompileShader(host_shader);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glLinkProgramMapped(GLuint guest_program) {
    GLuint host_program;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    if (host_program) {
        glLinkProgram(host_program);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glUseProgramMapped(GLuint guest_program) {
    if (!v86gl_ensure_ready()) return;
    glUseProgram(v86gl_host_program(guest_program));
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glValidateProgramMapped(GLuint guest_program) {
    GLuint host_program;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    if (host_program) {
        glValidateProgram(host_program);
    }
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glQueryObjectivMapped(uint32_t object_kind, GLuint guest_name,
                                GLenum pname, GLint* value) {
    GLuint host_name;

    if (!v86gl_ensure_ready() || !value) return 0;
    if (object_kind == V86GL_OBJECT_KIND_SHADER) {
        host_name = v86gl_host_shader(guest_name);
        if (!host_name) return 0;
        glGetShaderiv(host_name, pname, value);
        return 1;
    }
    if (object_kind == V86GL_OBJECT_KIND_PROGRAM) {
        host_name = v86gl_host_program(guest_name);
        if (!host_name) return 0;
        glGetProgramiv(host_name, pname, value);
        return 1;
    }
    if (object_kind == V86GL_OBJECT_KIND_QUERY) {
#ifdef __EMSCRIPTEN__
        GLuint query_value = 0;

        if (!v86gl_occlusion_queries_supported()) return 0;
        host_name = v86gl_host_query(guest_name);
        if (!host_name || !emscripten_glIsQuery(host_name)) return 0;

        if (pname == GL_QUERY_RESULT_AVAILABLE) {
            emscripten_glGetQueryObjectuiv(host_name, pname, &query_value);
            *value = (GLint)query_value;
            return 1;
        }
        if (pname == GL_QUERY_RESULT) {
            emscripten_glGetQueryObjectuiv(host_name, GL_QUERY_RESULT_AVAILABLE,
                                           &query_value);
            if (!query_value) {
                *value = 1;
                return 1;
            }
            query_value = 0;
            emscripten_glGetQueryObjectuiv(host_name, GL_QUERY_RESULT,
                                           &query_value);
            *value = query_value ? 1 : 0;
            return 1;
        }
#else
        (void)pname;
#endif
        return 0;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glQueryObjectLogMapped(uint32_t object_kind, GLuint guest_name,
                                 GLsizei bufSize, GLsizei* length,
                                 GLchar* infoLog) {
    GLuint host_name;

    if (!v86gl_ensure_ready() || bufSize < 0) return 0;
    if (object_kind == V86GL_OBJECT_KIND_SHADER) {
        host_name = v86gl_host_shader(guest_name);
        if (!host_name) return 0;
        glGetShaderInfoLog(host_name, bufSize, length, infoLog);
        return 1;
    }
    if (object_kind == V86GL_OBJECT_KIND_PROGRAM) {
        host_name = v86gl_host_program(guest_name);
        if (!host_name) return 0;
        glGetProgramInfoLog(host_name, bufSize, length, infoLog);
        return 1;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int v86gl_glQueryActiveMapped(uint32_t active_kind, GLuint guest_program,
                              GLuint index, GLsizei bufSize, GLsizei* length,
                              GLint* size, GLenum* type, GLchar* name) {
    GLuint host_program;

    if (!v86gl_ensure_ready() || bufSize < 0) return 0;
    host_program = v86gl_host_program(guest_program);
    if (!host_program) return 0;
    if (active_kind == V86GL_ACTIVE_KIND_UNIFORM) {
        glGetActiveUniform(host_program, index, bufSize, length, size, type, name);
        return 1;
    }
    if (active_kind == V86GL_ACTIVE_KIND_ATTRIB) {
        glGetActiveAttrib(host_program, index, bufSize, length, size, type, name);
        return 1;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBindAttribLocationMapped(GLuint guest_program, GLuint index,
                                      GLsizei name_length, const char* name) {
    GLuint host_program;
    char* owned_name;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    owned_name = v86gl_copy_name(name_length, name);
    if (host_program && owned_name) {
        glBindAttribLocation(host_program, index, owned_name);
    }
    free(owned_name);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMapUniformLocation(GLuint guest_program, GLint guest_location,
                                GLsizei name_length, const char* name) {
    GLuint host_program;
    GLint host_location = -1;
    char* owned_name;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    owned_name = v86gl_copy_name(name_length, name);
    if (host_program && owned_name) {
        host_location = glGetUniformLocation(host_program, owned_name);
    }
    v86gl_map_location(g_uniform_locations, &g_uniform_location_count,
                       V86GL_MAX_UNIFORM_LOCATIONS, guest_location, host_location);
    free(owned_name);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMapAttribLocation(GLuint guest_program, GLint guest_index,
                               GLsizei name_length, const char* name) {
    GLuint host_program;
    GLint host_index = -1;
    char* owned_name;

    if (!v86gl_ensure_ready()) return;
    host_program = v86gl_host_program(guest_program);
    owned_name = v86gl_copy_name(name_length, name);
    if (host_program && owned_name) {
        host_index = glGetAttribLocation(host_program, owned_name);
    }
    v86gl_map_location(g_attrib_locations, &g_attrib_location_count,
                       V86GL_MAX_ATTRIB_LOCATIONS, guest_index, host_index);
    free(owned_name);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glUniformfvMapped(GLint guest_location, GLint components,
                             GLsizei count, const GLfloat* value) {
    GLint host_location;

    if (!v86gl_ensure_ready()) return;
    host_location = v86gl_host_uniform_location(guest_location);
    if (host_location < 0 || count < 0 || (count > 0 && !value)) {
        return;
    }

    switch (components) {
    case 1: glUniform1fv(host_location, count, value); break;
    case 2: glUniform2fv(host_location, count, value); break;
    case 3: glUniform3fv(host_location, count, value); break;
    case 4: glUniform4fv(host_location, count, value); break;
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glUniformivMapped(GLint guest_location, GLint components,
                             GLsizei count, const GLint* value) {
    GLint host_location;

    if (!v86gl_ensure_ready()) return;
    host_location = v86gl_host_uniform_location(guest_location);
    if (host_location < 0 || count < 0 || (count > 0 && !value)) {
        return;
    }

    switch (components) {
    case 1: glUniform1iv(host_location, count, value); break;
    case 2: glUniform2iv(host_location, count, value); break;
    case 3: glUniform3iv(host_location, count, value); break;
    case 4: glUniform4iv(host_location, count, value); break;
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glUniformMatrixfvMapped(GLint guest_location, GLint dimension,
                                   GLsizei count, GLboolean transpose,
                                   const GLfloat* value) {
    GLint host_location;

    if (!v86gl_ensure_ready()) return;
    host_location = v86gl_host_uniform_location(guest_location);
    if (host_location < 0 || count < 0 || (count > 0 && !value)) {
        return;
    }

    switch (dimension) {
    case 2: glUniformMatrix2fv(host_location, count, transpose, value); break;
    case 3: glUniformMatrix3fv(host_location, count, transpose, value); break;
    case 4: glUniformMatrix4fv(host_location, count, transpose, value); break;
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glUniformMatrixRectfvMapped(GLint guest_location, GLint columns,
                                       GLint rows, GLsizei count,
                                       GLboolean transpose,
                                       const GLfloat* value) {
    GLint host_location;

    if (!v86gl_ensure_ready()) return;
    host_location = v86gl_host_uniform_location(guest_location);
    if (host_location < 0 || count < 0 || (count > 0 && !value)) {
        return;
    }

#ifdef __EMSCRIPTEN__
    if (g_webgl_major_version < 2) {
        return;
    }
    if (columns == 2 && rows == 3) emscripten_glUniformMatrix2x3fv(host_location, count, transpose, value);
    else if (columns == 3 && rows == 2) emscripten_glUniformMatrix3x2fv(host_location, count, transpose, value);
    else if (columns == 2 && rows == 4) emscripten_glUniformMatrix2x4fv(host_location, count, transpose, value);
    else if (columns == 4 && rows == 2) emscripten_glUniformMatrix4x2fv(host_location, count, transpose, value);
    else if (columns == 3 && rows == 4) emscripten_glUniformMatrix3x4fv(host_location, count, transpose, value);
    else if (columns == 4 && rows == 3) emscripten_glUniformMatrix4x3fv(host_location, count, transpose, value);
#else
    (void)columns;
    (void)rows;
    (void)transpose;
#endif
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenFramebuffersMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    for (i = 0; i < n; i++) {
        GLuint host = 0;
        glGenFramebuffers(1, &host);
        v86gl_map_name(g_framebuffers, &g_framebuffer_count,
                       V86GL_MAX_FRAMEBUFFERS, guest_names[i], host);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteFramebuffersMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    for (i = 0; i < n; i++) {
        GLuint host = v86gl_host_framebuffer(guest_names[i]);
        if (host) {
            glDeleteFramebuffers(1, &host);
            v86gl_forget_name(g_framebuffers, &g_framebuffer_count, guest_names[i]);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBindFramebufferMapped(GLenum target, GLuint guest_framebuffer) {
    if (!v86gl_ensure_ready()) return;
    glBindFramebuffer(target, v86gl_host_framebuffer(guest_framebuffer));
}

EMSCRIPTEN_KEEPALIVE
GLenum v86gl_glCheckFramebufferStatusMapped(GLenum target) {
    if (!v86gl_ensure_ready()) return 0;
    return glCheckFramebufferStatus(target);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFramebufferTextureMapped(GLenum target, GLenum attachment,
                                      GLenum textarget, GLuint guest_texture,
                                      GLint level, GLint zoffset) {
    GLuint host_texture;

    if (!v86gl_ensure_ready()) return;
    host_texture = v86gl_host_texture(guest_texture, 0);
    if (textarget == GL_TEXTURE_1D) {
        glFramebufferTexture1D(target, attachment, textarget, host_texture, level);
    } else if (textarget == GL_TEXTURE_3D) {
        glFramebufferTexture3D(target, attachment, textarget, host_texture, level, zoffset);
    } else {
        glFramebufferTexture2D(target, attachment, textarget, host_texture, level);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glFramebufferRenderbufferMapped(GLenum target, GLenum attachment,
                                           GLenum renderbuffertarget,
                                           GLuint guest_renderbuffer) {
    if (!v86gl_ensure_ready()) return;
    glFramebufferRenderbuffer(target, attachment, renderbuffertarget,
                              v86gl_host_renderbuffer(guest_renderbuffer));
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenRenderbuffersMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    for (i = 0; i < n; i++) {
        GLuint host = 0;
        glGenRenderbuffers(1, &host);
        v86gl_map_name(g_renderbuffers, &g_renderbuffer_count,
                       V86GL_MAX_RENDERBUFFERS, guest_names[i], host);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteRenderbuffersMapped(GLsizei n, const GLuint* guest_names) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    for (i = 0; i < n; i++) {
        GLuint host = v86gl_host_renderbuffer(guest_names[i]);
        if (host) {
            glDeleteRenderbuffers(1, &host);
            v86gl_forget_name(g_renderbuffers, &g_renderbuffer_count, guest_names[i]);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBindRenderbufferMapped(GLenum target, GLuint guest_renderbuffer) {
    if (!v86gl_ensure_ready()) return;
    glBindRenderbuffer(target, v86gl_host_renderbuffer(guest_renderbuffer));
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glRenderbufferStorageMapped(GLenum target, GLenum internalformat,
                                       GLsizei width, GLsizei height) {
    if (!v86gl_ensure_ready()) return;
    glRenderbufferStorage(target, internalformat, width, height);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glGenQueriesMapped(GLsizei n, const GLuint* guest_names) {
#ifdef __EMSCRIPTEN__
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;
    if (!v86gl_occlusion_queries_supported()) return;

    for (i = 0; i < n; i++) {
        GLuint host = 0;
        if (!guest_names[i]) {
            continue;
        }
        emscripten_glGenQueries(1, &host);
        if (host) {
            v86gl_map_name(g_queries, &g_query_count,
                           V86GL_MAX_QUERIES, guest_names[i], host);
        }
    }
#else
    (void)n;
    (void)guest_names;
#endif
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDeleteQueriesMapped(GLsizei n, const GLuint* guest_names) {
#ifdef __EMSCRIPTEN__
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (n < 0 || (n > 0 && !guest_names)) return;

    for (i = 0; i < n; i++) {
        GLuint host = v86gl_host_query(guest_names[i]);
        if (host && v86gl_occlusion_queries_supported()) {
            emscripten_glDeleteQueries(1, &host);
        }
        v86gl_forget_name(g_queries, &g_query_count, guest_names[i]);
    }
#else
    (void)n;
    (void)guest_names;
#endif
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glBeginQueryMapped(GLenum target, GLuint guest_query) {
#ifdef __EMSCRIPTEN__
    GLenum webgl_target;
    GLuint host;

    if (!v86gl_ensure_ready()) return;
    if (!v86gl_occlusion_queries_supported()) return;
    webgl_target = v86gl_webgl_query_target(target);
    host = v86gl_host_query(guest_query);
    if (!webgl_target || !host) return;

    emscripten_glBeginQuery(webgl_target, host);
#else
    (void)target;
    (void)guest_query;
#endif
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glEndQueryMapped(GLenum target) {
#ifdef __EMSCRIPTEN__
    GLenum webgl_target;

    if (!v86gl_ensure_ready()) return;
    if (!v86gl_occlusion_queries_supported()) return;
    webgl_target = v86gl_webgl_query_target(target);
    if (!webgl_target) return;

    emscripten_glEndQuery(webgl_target);
#else
    (void)target;
#endif
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glVertexAttrib4fMapped(GLuint guest_index, GLfloat x, GLfloat y,
                                  GLfloat z, GLfloat w) {
    GLint host_index;

    if (!v86gl_ensure_ready()) return;
    host_index = v86gl_host_attrib_index((GLint)guest_index);
    if (host_index >= 0) {
        glVertexAttrib4f((GLuint)host_index, x, y, z, w);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glEnableVertexAttribArrayMapped(GLuint guest_index) {
    GLint host_index;

    if (!v86gl_ensure_ready()) return;
    host_index = v86gl_host_attrib_index((GLint)guest_index);
    if (host_index >= 0) {
        glEnableVertexAttribArray((GLuint)host_index);
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDisableVertexAttribArrayMapped(GLuint guest_index) {
    GLint host_index;

    if (!v86gl_ensure_ready()) return;
    host_index = v86gl_host_attrib_index((GLint)guest_index);
    if (host_index >= 0) {
        glDisableVertexAttribArray((GLuint)host_index);
    }
}

static void v86gl_setup_client_arrays(GLint vertex_size, GLenum vertex_type,
                                      GLsizei vertex_stride, const void* vertex_data,
                                      GLint color_size, GLenum color_type,
                                      GLsizei color_stride, const void* color_data,
                                      GLint texcoord_size, GLenum texcoord_type,
                                      GLsizei texcoord_stride, const void* texcoord_data,
                                      GLenum normal_type, GLsizei normal_stride,
                                      const void* normal_data) {
    if (vertex_data && vertex_size > 0) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(vertex_size, vertex_type, vertex_stride, vertex_data);
    } else {
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    if (color_data && color_size > 0) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(color_size, color_type, color_stride, color_data);
    } else {
        glDisableClientState(GL_COLOR_ARRAY);
    }

    if (texcoord_data && texcoord_size > 0) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(texcoord_size, texcoord_type, texcoord_stride, texcoord_data);
    } else {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    if (normal_data) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(normal_type, normal_stride, normal_data);
    } else {
        glDisableClientState(GL_NORMAL_ARRAY);
    }
}

typedef struct {
    GLint size;
    GLenum type;
    GLsizei stride;
    const void* data;
} V86GLClientArrayMeta;

static V86GLClientArrayMeta v86gl_client_array_meta_at(const int32_t* values, uint32_t index) {
    V86GLClientArrayMeta meta;
    const int32_t* row = values + index * 4u;
    meta.size = row[0];
    meta.type = (GLenum)row[1];
    meta.stride = row[2];
    meta.data = row[3] ? (const void*)(uintptr_t)(uint32_t)row[3] : 0;
    return meta;
}

static void v86gl_setup_client_arrays_mt(GLsizei tex_unit_count,
                                         GLenum restore_client_active,
                                         GLboolean has_secondary_color,
                                         GLboolean has_fog_coord,
                                         const int32_t* values) {
    V86GLClientArrayMeta vertex;
    V86GLClientArrayMeta color;
    V86GLClientArrayMeta normal;
    GLsizei i;

    if (!values) {
        return;
    }

    vertex = v86gl_client_array_meta_at(values, 0);
    color = v86gl_client_array_meta_at(values, 1);
    normal = v86gl_client_array_meta_at(values, 2);

    if (vertex.data && vertex.size > 0) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(vertex.size, vertex.type, vertex.stride, vertex.data);
    } else {
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    if (color.data && color.size > 0) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(color.size, color.type, color.stride, color.data);
    } else {
        glDisableClientState(GL_COLOR_ARRAY);
    }

    if (normal.data) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(normal.type, normal.stride, normal.data);
    } else {
        glDisableClientState(GL_NORMAL_ARRAY);
    }

    if (tex_unit_count > 8) {
        tex_unit_count = 8;
    }

    for (i = 0; i < tex_unit_count; i++) {
        V86GLClientArrayMeta texcoord = v86gl_client_array_meta_at(values, 3u + (uint32_t)i);
        glClientActiveTexture((GLenum)(GL_TEXTURE0 + i));
        if (texcoord.data && texcoord.size > 0) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(texcoord.size, texcoord.type, texcoord.stride, texcoord.data);
        } else {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
    }

    if (has_secondary_color) {
        V86GLClientArrayMeta secondary =
            v86gl_client_array_meta_at(values, 3u + (uint32_t)tex_unit_count);
        if (secondary.data && secondary.size == 3) {
            glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
            glSecondaryColorPointer(secondary.size, secondary.type,
                                    secondary.stride, secondary.data);
        } else {
            glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
        }
    } else {
        glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
    }

    if (has_fog_coord) {
        V86GLClientArrayMeta fog_coord = v86gl_client_array_meta_at(
            values, 3u + (uint32_t)tex_unit_count + (has_secondary_color ? 1u : 0u));
        if (fog_coord.data && fog_coord.size == 1) {
            glEnableClientState(GL_FOG_COORDINATE_ARRAY);
            glFogCoordPointer(fog_coord.type, fog_coord.stride, fog_coord.data);
        } else {
            glDisableClientState(GL_FOG_COORDINATE_ARRAY);
        }
    } else {
        glDisableClientState(GL_FOG_COORDINATE_ARRAY);
    }

    glClientActiveTexture(restore_client_active);
}

typedef struct {
    GLint guest_index;
    GLboolean normalized;
    GLboolean enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    const void* data;
} V86GLGenericAttribMeta;

static V86GLGenericAttribMeta v86gl_generic_attrib_meta_at(const int32_t* values,
                                                           uint32_t index) {
    V86GLGenericAttribMeta meta;
    const int32_t* row = values + index * 7u;
    meta.guest_index = row[0];
    meta.normalized = row[1] ? GL_TRUE : GL_FALSE;
    meta.enabled = row[2] ? GL_TRUE : GL_FALSE;
    meta.size = row[3];
    meta.type = (GLenum)row[4];
    meta.stride = row[5];
    meta.data = row[6] ? (const void*)(uintptr_t)(uint32_t)row[6] : 0;
    return meta;
}

static void v86gl_setup_generic_attribs(GLsizei attrib_count, const int32_t* values) {
    GLsizei i;

    if (!values || attrib_count <= 0) {
        return;
    }

    if (attrib_count > V86GL_MAX_VERTEX_ATTRIBS) {
        attrib_count = V86GL_MAX_VERTEX_ATTRIBS;
    }

    for (i = 0; i < attrib_count; i++) {
        V86GLGenericAttribMeta meta = v86gl_generic_attrib_meta_at(values, (uint32_t)i);
        GLint host_index = v86gl_host_attrib_index(meta.guest_index);
        if (host_index < 0 || host_index >= V86GL_MAX_VERTEX_ATTRIBS) {
            continue;
        }

        if (meta.enabled && meta.data && meta.size > 0) {
            glEnableVertexAttribArray((GLuint)host_index);
            glVertexAttribPointer((GLuint)host_index, meta.size, meta.type,
                                  meta.normalized, meta.stride, meta.data);
        } else {
            glDisableVertexAttribArray((GLuint)host_index);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawArraysDirect(GLenum mode, GLint first, GLsizei count) {
    if (!v86gl_ensure_ready()) return;
    glDrawArrays(mode, first, count);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawElementsDirect(GLenum mode, GLuint start, GLuint end,
                                GLsizei count, GLenum type,
                                uintptr_t index_offset) {
    (void)start;
    (void)end;
    if (!v86gl_ensure_ready()) return;
    glDrawElements(mode, count, type, (const void*)index_offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawRangeElementsDirect(GLenum mode, GLuint start, GLuint end,
                                     GLsizei count, GLenum type,
                                     uintptr_t index_offset) {
    if (!v86gl_ensure_ready()) return;
    glDrawRangeElements(mode, start, end, count, type,
                        (const void*)index_offset);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMultiDrawArraysDirect(GLenum mode, GLsizei primcount,
                                   const int32_t* pairs) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (primcount <= 0 || !pairs) return;

    for (i = 0; i < primcount; i++) {
        GLint first = pairs[i * 2];
        GLsizei count = pairs[i * 2 + 1];
        if (count > 0) {
            glDrawArrays(mode, first, count);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glMultiDrawElementsDirect(GLenum mode, GLenum type,
                                     GLsizei primcount,
                                     const int32_t* pairs) {
    GLsizei i;

    if (!v86gl_ensure_ready()) return;
    if (primcount <= 0 || !pairs) return;

    for (i = 0; i < primcount; i++) {
        GLsizei count = pairs[i * 2];
        uintptr_t index_offset = (uintptr_t)(uint32_t)pairs[i * 2 + 1];
        if (count > 0) {
            glDrawElements(mode, count, type, (const void*)index_offset);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawArraysPacked(GLenum mode, GLsizei count,
                              GLint vertex_size, GLenum vertex_type,
                              GLsizei vertex_stride, const void* vertex_data,
                              GLint color_size, GLenum color_type,
                              GLsizei color_stride, const void* color_data,
                              GLint texcoord_size, GLenum texcoord_type,
                              GLsizei texcoord_stride, const void* texcoord_data,
                              GLenum normal_type, GLsizei normal_stride,
                              const void* normal_data) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays(vertex_size, vertex_type, vertex_stride, vertex_data,
                              color_size, color_type, color_stride, color_data,
                              texcoord_size, texcoord_type, texcoord_stride, texcoord_data,
                              normal_type, normal_stride, normal_data);
    glDrawArrays(mode, 0, count);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawArraysPackedMT(GLenum mode, GLsizei count,
                                GLsizei tex_unit_count, GLenum restore_client_active,
                                GLboolean has_secondary_color,
                                GLboolean has_fog_coord,
                                const int32_t* array_meta) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays_mt(tex_unit_count, restore_client_active,
                                 has_secondary_color, has_fog_coord, array_meta);
    glDrawArrays(mode, 0, count);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawArraysPackedGL2(GLenum mode, GLsizei count,
                                 GLsizei tex_unit_count, GLenum restore_client_active,
                                 GLboolean has_secondary_color,
                                 GLboolean has_fog_coord,
                                 const int32_t* array_meta,
                                 GLsizei generic_attrib_count,
                                 const int32_t* generic_attrib_meta) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays_mt(tex_unit_count, restore_client_active,
                                 has_secondary_color, has_fog_coord, array_meta);
    v86gl_setup_generic_attribs(generic_attrib_count, generic_attrib_meta);
    glDrawArrays(mode, 0, count);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawElementsPacked(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                GLint vertex_size, GLenum vertex_type,
                                GLsizei vertex_stride, const void* vertex_data,
                                GLint color_size, GLenum color_type,
                                GLsizei color_stride, const void* color_data,
                                GLint texcoord_size, GLenum texcoord_type,
                                GLsizei texcoord_stride, const void* texcoord_data,
                                GLenum normal_type, GLsizei normal_stride,
                                const void* normal_data) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays(vertex_size, vertex_type, vertex_stride, vertex_data,
                              color_size, color_type, color_stride, color_data,
                              texcoord_size, texcoord_type, texcoord_stride, texcoord_data,
                              normal_type, normal_stride, normal_data);
    glDrawElements(mode, count, type, indices);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawElementsPackedMT(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                  GLsizei tex_unit_count, GLenum restore_client_active,
                                  GLboolean has_secondary_color,
                                  GLboolean has_fog_coord,
                                  const int32_t* array_meta) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays_mt(tex_unit_count, restore_client_active,
                                 has_secondary_color, has_fog_coord, array_meta);
    glDrawElements(mode, count, type, indices);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glDrawElementsPackedGL2(GLenum mode, GLsizei count, GLenum type,
                                   const void* indices,
                                   GLsizei tex_unit_count,
                                   GLenum restore_client_active,
                                   GLboolean has_secondary_color,
                                   GLboolean has_fog_coord,
                                   const int32_t* array_meta,
                                   GLsizei generic_attrib_count,
                                   const int32_t* generic_attrib_meta) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays_mt(tex_unit_count, restore_client_active,
                                 has_secondary_color, has_fog_coord, array_meta);
    v86gl_setup_generic_attribs(generic_attrib_count, generic_attrib_meta);
    glDrawElements(mode, count, type, indices);
}
