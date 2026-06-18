// Thin exported wrapper around gl4es for v86_network_bridge.js.
// Keep these names stable; the browser bridge calls v86gl_gl* functions.

#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <GL/gl.h>
#include <gl4esinit.h>

static int32_t g_surface_x;
static int32_t g_surface_y;
static uint32_t g_surface_width;
static uint32_t g_surface_height;
static uint32_t g_surface_hwnd;
static int g_ready;

#define V86GL_MAX_TEXTURES 16384

typedef struct {
    GLuint guest;
    GLuint host;
} V86GLTextureName;

static V86GLTextureName g_textures[V86GL_MAX_TEXTURES];
static uint32_t g_texture_count;

static int v86gl_ensure_ready(void) {
    if (g_ready) {
        return 1;
    }

#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = EM_TRUE;
    attrs.depth = EM_TRUE;
    attrs.stencil = EM_FALSE;
    attrs.antialias = EM_FALSE;
    attrs.preserveDrawingBuffer = EM_TRUE;
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        emscripten_webgl_create_context("#v86gl_canvas", &attrs);

    if (ctx <= 0) {
        attrs.majorVersion = 2;
        ctx = emscripten_webgl_create_context("#v86gl_canvas", &attrs);
    }

    if (ctx > 0) {
        emscripten_webgl_make_context_current(ctx);
    } else {
        return 0;
    }
#endif

    initialize_gl4es();
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
    host = v86gl_host_texture(texture, texture != 0);
    glBindTexture(target, host);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                        GLsizei width, GLsizei height, GLint border,
                        GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

EMSCRIPTEN_KEEPALIVE
void v86gl_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                           GLsizei width, GLsizei height,
                           GLenum format, GLenum type, const void* pixels) {
    if (!v86gl_ensure_ready()) return;
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
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
void v86gl_glTexCoord2f(GLfloat s, GLfloat t) {
    if (!v86gl_ensure_ready()) return;
    glTexCoord2f(s, t);
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
