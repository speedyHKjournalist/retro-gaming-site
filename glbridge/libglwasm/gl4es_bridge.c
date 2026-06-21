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

extern void glActiveTexture(GLenum texture);
extern void glClientActiveTexture(GLenum texture);
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

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

static int32_t g_surface_x;
static int32_t g_surface_y;
static uint32_t g_surface_width;
static uint32_t g_surface_height;
static uint32_t g_surface_hwnd;
static int g_ready;

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
    attrs.alpha = EM_FALSE;
    attrs.depth = EM_TRUE;
    attrs.stencil = EM_TRUE;
    attrs.antialias = EM_FALSE;
    attrs.premultipliedAlpha = EM_FALSE;
    attrs.preserveDrawingBuffer = EM_TRUE;
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;

    g_webgl_context = emscripten_webgl_create_context("#v86gl_canvas", &attrs);

    if (g_webgl_context <= 0) {
        attrs.majorVersion = 2;
        g_webgl_context = emscripten_webgl_create_context("#v86gl_canvas", &attrs);
    }

    if (g_webgl_context > 0) {
        emscripten_webgl_make_context_current(g_webgl_context);
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
void v86gl_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLint x, GLint y, GLsizei width, GLsizei height,
                            GLint border) {
    if (!v86gl_ensure_ready()) return;
    glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
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

    glClientActiveTexture(restore_client_active);
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
                                const int32_t* array_meta) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays_mt(tex_unit_count, restore_client_active, array_meta);
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
                                  const int32_t* array_meta) {
    if (!v86gl_ensure_ready()) return;
    v86gl_setup_client_arrays_mt(tex_unit_count, restore_client_active, array_meta);
    glDrawElements(mode, count, type, indices);
}
