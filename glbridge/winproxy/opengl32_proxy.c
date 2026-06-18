// Minimal fake opengl32.dll for v86 experiments.
// It serializes a tiny OpenGL subset to UDP broadcast using VGL1 packets.
// Build:
//   i686-w64-mingw32-gcc -shared -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_DllMain@12 -o opengl32.dll opengl32_proxy.c opengl32.def -Wl,--kill-at -luser32 -lkernel32 -lws2_32

#define WIN32_LEAN_AND_MEAN
#define _GDI32_
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;

#define GL_VENDOR             0x1F00
#define GL_RENDERER           0x1F01
#define GL_VERSION            0x1F02
#define GL_EXTENSIONS         0x1F03
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_POINTS             0x0000
#define GL_LINES              0x0001
#define GL_TRIANGLES          0x0004
#define GL_TRIANGLE_STRIP     0x0005
#define GL_TRIANGLE_FAN       0x0006
#define GL_QUADS              0x0007

#define VGL_MAGIC 0x314C4756u  // 'VGL1' little-endian

enum {
    OP_MAKE_CURRENT = 1,
    OP_VIEWPORT     = 2,
    OP_CLEAR_COLOR  = 3,
    OP_CLEAR        = 4,
    OP_BEGIN        = 5,
    OP_END          = 6,
    OP_COLOR4F      = 7,
    OP_VERTEX3F     = 8,
    OP_PRESENT      = 9,
    OP_RELEASE_CURRENT = 10,
    OP_GL_CALL      = 20,
    OP_GL_FRAME     = 21,
};

enum {
    GLFN_VIEWPORT    = 1,
    GLFN_CLEAR_COLOR = 2,
    GLFN_CLEAR       = 3,
    GLFN_BEGIN       = 4,
    GLFN_END         = 5,
    GLFN_COLOR4F     = 6,
    GLFN_VERTEX3F    = 7,
    GLFN_FLUSH       = 8,
    GLFN_FINISH      = 9,
    GLFN_MATRIX_MODE = 10,
    GLFN_LOAD_IDENTITY = 11,
    GLFN_FRUSTUM     = 12,
    GLFN_ORTHO       = 13,
    GLFN_TRANSLATEF  = 14,
    GLFN_ROTATEF     = 15,
    GLFN_SCALEF      = 16,
    GLFN_PUSH_MATRIX = 17,
    GLFN_POP_MATRIX  = 18,
    GLFN_ENABLE      = 19,
    GLFN_DISABLE     = 20,
    GLFN_DEPTH_FUNC  = 21,
    GLFN_CLEAR_DEPTH = 22,
    GLFN_SHADE_MODEL = 23,
    GLFN_CULL_FACE   = 24,
    GLFN_FRONT_FACE  = 25,
};

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t op;
    uint16_t size;
    uint32_t seq;
} VGLHeader;
#pragma pack(pop)

#define VGL_UDP_PORT 46000u
#define VGL_FRAME_BUFFER_SIZE 1400u

static SOCKET g_udp = INVALID_SOCKET;
static BOOL   g_udp_ready = FALSE;
static BOOL   g_wsa_started = FALSE;
static HDC    g_current_dc = NULL;
static HGLRC  g_current_ctx = NULL;
static HWND   g_current_hwnd = NULL;
static WNDPROC g_original_wndproc = NULL;
static int32_t g_last_surface_x = 0;
static int32_t g_last_surface_y = 0;
static uint32_t g_last_surface_width = 0;
static uint32_t g_last_surface_height = 0;
static BOOL g_have_last_surface = FALSE;
static uint8_t g_frame_buffer[VGL_FRAME_BUFFER_SIZE];
static uint16_t g_frame_size = 0;
static BOOL g_frame_overflow = FALSE;
static uint32_t g_seq = 1;
static GLenum g_error = 0;

static uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}

static int open_udp(void) {
    if (g_udp_ready) {
        return 1;
    }

    if (!g_wsa_started) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return 0;
        }
        g_wsa_started = TRUE;
    }

    g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp == INVALID_SOCKET) {
        return 0;
    }

    BOOL yes = TRUE;
    setsockopt(g_udp, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    g_udp_ready = TRUE;
    return 1;
}

static void emit_packet(uint16_t op, const void* payload, uint16_t size) {
    uint8_t packet[sizeof(VGLHeader) + VGL_FRAME_BUFFER_SIZE];
    struct sockaddr_in dst;
    VGLHeader h;
    int total = (int)(sizeof(h) + size);

    if (size > VGL_FRAME_BUFFER_SIZE || !open_udp()) {
        return;
    }

    h.magic = VGL_MAGIC;
    h.op = op;
    h.size = size;
    h.seq = g_seq++;
    CopyMemory(packet, &h, sizeof(h));

    if (payload && size) {
        CopyMemory(packet + sizeof(h), payload, size);
    }

    ZeroMemory(&dst, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = bswap16((uint16_t)VGL_UDP_PORT);
    dst.sin_addr.s_addr = 0xFFFFFFFFu;

    sendto(g_udp, (const char*)packet, total, 0, (const struct sockaddr*)&dst, sizeof(dst));
}

static void emit_gl_call(uint16_t fn, const void* args, uint16_t args_size) {
    uint16_t record_size = (uint16_t)(sizeof(uint16_t) + sizeof(uint16_t) + args_size);
    uint8_t* p;

    if (record_size > VGL_FRAME_BUFFER_SIZE ||
        (uint32_t)g_frame_size + record_size > VGL_FRAME_BUFFER_SIZE) {
        g_frame_overflow = TRUE;
        return;
    }

    p = g_frame_buffer + g_frame_size;
    p[0] = (uint8_t)(fn & 0xFF);
    p[1] = (uint8_t)(fn >> 8);
    p[2] = (uint8_t)(args_size & 0xFF);
    p[3] = (uint8_t)(args_size >> 8);

    if (args && args_size) {
        CopyMemory(p + 4, args, args_size);
    }

    g_frame_size = (uint16_t)(g_frame_size + record_size);
}

static void emit_frame(void) {
    if (g_frame_size) {
        emit_packet(OP_GL_FRAME, g_frame_buffer, g_frame_size);
    } else {
        emit_packet(OP_PRESENT, NULL, 0);
    }

    g_frame_size = 0;
    g_frame_overflow = FALSE;
}

static void emit_current_surface(HWND hwnd) {
    BOOL force = !g_have_last_surface;
    POINT client_origin;
    RECT rc;
    ZeroMemory(&client_origin, sizeof(client_origin));
    ZeroMemory(&rc, sizeof(rc));

    if (hwnd) {
        GetClientRect(hwnd, &rc);
        ClientToScreen(hwnd, &client_origin);
    }

    struct {
        uint32_t hwnd;
        int32_t x;
        int32_t y;
        uint32_t width;
        uint32_t height;
    } payload;

    payload.hwnd = (uint32_t)(uintptr_t)hwnd;
    payload.x = client_origin.x;
    payload.y = client_origin.y;
    payload.width = (uint32_t)(rc.right - rc.left);
    payload.height = (uint32_t)(rc.bottom - rc.top);

    if (!payload.width) {
        payload.width = 640;
    }

    if (!payload.height) {
        payload.height = 480;
    }

    if (!force &&
        payload.x == g_last_surface_x &&
        payload.y == g_last_surface_y &&
        payload.width == g_last_surface_width &&
        payload.height == g_last_surface_height) {
        return;
    }

    g_last_surface_x = payload.x;
    g_last_surface_y = payload.y;
    g_last_surface_width = payload.width;
    g_last_surface_height = payload.height;
    g_have_last_surface = TRUE;

    emit_packet(OP_MAKE_CURRENT, &payload, sizeof(payload));
}

static void restore_window_proc(void) {
    if (g_current_hwnd && g_original_wndproc) {
        SetWindowLongA(g_current_hwnd, GWL_WNDPROC, (LONG)g_original_wndproc);
    }

    g_current_hwnd = NULL;
    g_original_wndproc = NULL;
    g_have_last_surface = FALSE;
}

static LRESULT CALLBACK vgl_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC original = g_original_wndproc;

    if (msg == WM_MOVE || msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED) {
        emit_current_surface(hwnd);
    } else if (msg == WM_CLOSE || msg == WM_DESTROY || msg == WM_NCDESTROY) {
        emit_packet(OP_RELEASE_CURRENT, NULL, 0);
    }

    if (msg == WM_NCDESTROY && hwnd == g_current_hwnd) {
        g_current_hwnd = NULL;
        g_original_wndproc = NULL;
    }

    if (original) {
        return CallWindowProcA(original, hwnd, msg, wp, lp);
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void hook_window(HWND hwnd) {
    if (!hwnd || hwnd == g_current_hwnd) {
        return;
    }

    restore_window_proc();
    g_original_wndproc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)vgl_window_proc);

    if (g_original_wndproc) {
        g_current_hwnd = hwnd;
    }
}

BOOL WINAPI DllMain
(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    (void)hinst;
    (void)reserved;

    if (reason == DLL_PROCESS_DETACH) {
        restore_window_proc();

        if (g_udp != INVALID_SOCKET) {
            closesocket(g_udp);
            g_udp = INVALID_SOCKET;
        }

        if (g_wsa_started) {
            WSACleanup();
            g_wsa_started = FALSE;
        }
    }

    return TRUE;
}

__declspec(dllexport)
HGLRC APIENTRY wglCreateContext(HDC hdc) {
    (void)hdc;
    return (HGLRC)0x1001;
}

__declspec(dllexport)
BOOL APIENTRY wglDeleteContext(HGLRC ctx) {
    (void)ctx;
    restore_window_proc();
    emit_packet(OP_RELEASE_CURRENT, NULL, 0);
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglMakeCurrent(HDC hdc, HGLRC ctx) {
    g_current_dc = hdc;
    g_current_ctx = ctx;

    if (!hdc || !ctx) {
        restore_window_proc();
        emit_packet(OP_RELEASE_CURRENT, NULL, 0);
        return TRUE;
    }

    HWND hwnd = hdc ? WindowFromDC(hdc) : NULL;
    hook_window(hwnd);
    emit_current_surface(hwnd);
    return TRUE;
}

__declspec(dllexport)
HGLRC APIENTRY wglGetCurrentContext(void) {
    return g_current_ctx;
}

__declspec(dllexport)
HDC APIENTRY wglGetCurrentDC(void) {
    return g_current_dc;
}

__declspec(dllexport)
BOOL APIENTRY wglShareLists(HGLRC a, HGLRC b) {
    (void)a;
    (void)b;
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglSwapLayerBuffers(HDC hdc, UINT planes) {
    (void)hdc;
    (void)planes;
    emit_frame();
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglSwapBuffers(HDC hdc) {
    (void)hdc;
    emit_frame();
    return TRUE;
}

__declspec(dllexport)
PROC APIENTRY wglGetProcAddress(LPCSTR name) {
    (void)name;
    return NULL;
}

__declspec(dllexport)
const GLubyte* APIENTRY glGetString(GLenum name) {
    switch (name) {
    case GL_VENDOR:     return (const GLubyte*)"v86";
    case GL_RENDERER:   return (const GLubyte*)"v86 fake OpenGL over UDP";
    case GL_VERSION:    return (const GLubyte*)"1.1";
    case GL_EXTENSIONS: return (const GLubyte*)"";
    default:            return (const GLubyte*)"";
    }
}

__declspec(dllexport)
GLenum APIENTRY glGetError(void) {
    GLenum e = g_error;
    g_error = 0;
    return e;
}

__declspec(dllexport)
void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    struct { int32_t x, y, width, height; } payload;
    payload.x = x;
    payload.y = y;
    payload.width = width;
    payload.height = height;
    emit_gl_call(GLFN_VIEWPORT, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    struct { float r, g, b, a; } payload;
    payload.r = r;
    payload.g = g;
    payload.b = b;
    payload.a = a;
    emit_gl_call(GLFN_CLEAR_COLOR, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClear(GLbitfield mask) {
    uint32_t payload = (uint32_t)mask;
    emit_gl_call(GLFN_CLEAR, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glBegin(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_BEGIN, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glEnd(void) {
    emit_gl_call(GLFN_END, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b) {
    struct { float r, g, b, a; } payload;
    payload.r = r;
    payload.g = g;
    payload.b = b;
    payload.a = 1.0f;
    emit_gl_call(GLFN_COLOR4F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    struct { float r, g, b, a; } payload;
    payload.r = r;
    payload.g = g;
    payload.b = b;
    payload.a = a;
    emit_gl_call(GLFN_COLOR4F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glVertex2f(GLfloat x, GLfloat y) {
    struct { float x, y, z; } payload;
    payload.x = x;
    payload.y = y;
    payload.z = 0.0f;
    emit_gl_call(GLFN_VERTEX3F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    struct { float x, y, z; } payload;
    payload.x = x;
    payload.y = y;
    payload.z = z;
    emit_gl_call(GLFN_VERTEX3F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glFlush(void) {
    emit_gl_call(GLFN_FLUSH, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glFinish(void) {
    emit_gl_call(GLFN_FINISH, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glMatrixMode(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_MATRIX_MODE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLoadIdentity(void) {
    emit_gl_call(GLFN_LOAD_IDENTITY, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) {
    struct { double left, right, bottom, top, zNear, zFar; } payload;
    payload.left = left;
    payload.right = right;
    payload.bottom = bottom;
    payload.top = top;
    payload.zNear = zNear;
    payload.zFar = zFar;
    emit_gl_call(GLFN_FRUSTUM, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) {
    struct { double left, right, bottom, top, zNear, zFar; } payload;
    payload.left = left;
    payload.right = right;
    payload.bottom = bottom;
    payload.top = top;
    payload.zNear = zNear;
    payload.zFar = zFar;
    emit_gl_call(GLFN_ORTHO, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    struct { float x, y, z; } payload;
    payload.x = x;
    payload.y = y;
    payload.z = z;
    emit_gl_call(GLFN_TRANSLATEF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    struct { float angle, x, y, z; } payload;
    payload.angle = angle;
    payload.x = x;
    payload.y = y;
    payload.z = z;
    emit_gl_call(GLFN_ROTATEF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z) {
    struct { float x, y, z; } payload;
    payload.x = x;
    payload.y = y;
    payload.z = z;
    emit_gl_call(GLFN_SCALEF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPushMatrix(void) {
    emit_gl_call(GLFN_PUSH_MATRIX, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glPopMatrix(void) {
    emit_gl_call(GLFN_POP_MATRIX, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glEnable(GLenum cap) {
    uint32_t payload = (uint32_t)cap;
    emit_gl_call(GLFN_ENABLE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glDisable(GLenum cap) {
    uint32_t payload = (uint32_t)cap;
    emit_gl_call(GLFN_DISABLE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glDepthFunc(GLenum func) {
    uint32_t payload = (uint32_t)func;
    emit_gl_call(GLFN_DEPTH_FUNC, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClearDepth(GLdouble depth) {
    double payload = depth;
    emit_gl_call(GLFN_CLEAR_DEPTH, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glShadeModel(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_SHADE_MODEL, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glCullFace(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_CULL_FACE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glFrontFace(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_FRONT_FACE, &payload, sizeof(payload));
}
