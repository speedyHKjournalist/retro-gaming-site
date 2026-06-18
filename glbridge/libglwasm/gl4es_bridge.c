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
