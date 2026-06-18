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
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef void GLvoid;

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
#define GL_BYTE               0x1400
#define GL_UNSIGNED_BYTE      0x1401
#define GL_SHORT              0x1402
#define GL_UNSIGNED_SHORT     0x1403
#define GL_INT                0x1404
#define GL_UNSIGNED_INT       0x1405
#define GL_FLOAT              0x1406
#define GL_RED                0x1903
#define GL_GREEN              0x1904
#define GL_BLUE               0x1905
#define GL_ALPHA              0x1906
#define GL_RGB                0x1907
#define GL_RGBA               0x1908
#define GL_LUMINANCE          0x1909
#define GL_LUMINANCE_ALPHA    0x190A
#define GL_BGR_EXT            0x80E0
#define GL_BGRA_EXT           0x80E1
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_UNSIGNED_SHORT_5_6_5   0x8363
#define GL_UNSIGNED_INT_8_8_8_8   0x8035
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_UNPACK_SWAP_BYTES  0x0CF0
#define GL_UNPACK_LSB_FIRST   0x0CF1
#define GL_UNPACK_ROW_LENGTH  0x0CF2
#define GL_UNPACK_SKIP_ROWS   0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT   0x0CF5
#define GL_INVALID_ENUM       0x0500
#define GL_INVALID_VALUE      0x0501
#define GL_OUT_OF_MEMORY      0x0505

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
    OP_GL_CHUNK     = 22,
    OP_GL_BATCH     = 23,
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
    GLFN_GEN_TEXTURES = 26,
    GLFN_DELETE_TEXTURES = 27,
    GLFN_BIND_TEXTURE = 28,
    GLFN_TEX_IMAGE_2D = 29,
    GLFN_TEX_SUB_IMAGE_2D = 30,
    GLFN_TEX_PARAMETERI = 31,
    GLFN_TEX_PARAMETERF = 32,
    GLFN_PIXEL_STOREI = 33,
    GLFN_TEX_ENVI = 34,
    GLFN_TEX_ENVF = 35,
    GLFN_TEX_COORD2F = 36,
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
static uint32_t g_chunk_id = 1;
static GLuint g_next_texture_id = 1;
static GLint g_unpack_alignment = 4;
static GLint g_unpack_row_length = 0;
static GLint g_unpack_skip_rows = 0;
static GLint g_unpack_skip_pixels = 0;
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

static void emit_gl_batch(void) {
    if (g_frame_size) {
        emit_packet(OP_GL_BATCH, g_frame_buffer, g_frame_size);
    }

    g_frame_size = 0;
    g_frame_overflow = FALSE;
}

static void emit_gl_large_call(uint16_t fn, const void* args, uint32_t args_size) {
    uint32_t upload_id = g_chunk_id++;
    uint32_t offset = 0;
    uint8_t payload[VGL_FRAME_BUFFER_SIZE];
    const uint32_t header_size = 16;
    const uint32_t max_chunk = VGL_FRAME_BUFFER_SIZE - header_size;

    if (!args || !args_size) {
        return;
    }

    emit_gl_batch();

    while (offset < args_size) {
        uint32_t remaining = args_size - offset;
        uint16_t chunk_size = (uint16_t)(remaining > max_chunk ? max_chunk : remaining);

        payload[0] = (uint8_t)(upload_id & 0xFF);
        payload[1] = (uint8_t)(upload_id >> 8);
        payload[2] = (uint8_t)(upload_id >> 16);
        payload[3] = (uint8_t)(upload_id >> 24);
        payload[4] = (uint8_t)(fn & 0xFF);
        payload[5] = (uint8_t)(fn >> 8);
        payload[6] = (uint8_t)(chunk_size & 0xFF);
        payload[7] = (uint8_t)(chunk_size >> 8);
        payload[8] = (uint8_t)(args_size & 0xFF);
        payload[9] = (uint8_t)(args_size >> 8);
        payload[10] = (uint8_t)(args_size >> 16);
        payload[11] = (uint8_t)(args_size >> 24);
        payload[12] = (uint8_t)(offset & 0xFF);
        payload[13] = (uint8_t)(offset >> 8);
        payload[14] = (uint8_t)(offset >> 16);
        payload[15] = (uint8_t)(offset >> 24);

        CopyMemory(payload + header_size, (const uint8_t*)args + offset, chunk_size);
        emit_packet(OP_GL_CHUNK, payload, (uint16_t)(header_size + chunk_size));
        offset += chunk_size;
    }
}

static void emit_gl_call(uint16_t fn, const void* args, uint32_t args_size) {
    uint32_t record_size = (uint32_t)(sizeof(uint16_t) + sizeof(uint16_t) + args_size);
    uint8_t* p;

    if (record_size > VGL_FRAME_BUFFER_SIZE) {
        emit_gl_large_call(fn, args, args_size);
        return;
    }

    if ((uint32_t)g_frame_size + record_size > VGL_FRAME_BUFFER_SIZE) {
        emit_gl_batch();
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

static uint32_t align_u32(uint32_t value, uint32_t alignment) {
    if (alignment <= 1) {
        return value;
    }

    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t gl_format_components(GLenum format) {
    switch (format) {
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        return 1;
    case GL_LUMINANCE_ALPHA:
        return 2;
    case GL_RGB:
    case GL_BGR_EXT:
        return 3;
    case GL_RGBA:
    case GL_BGRA_EXT:
        return 4;
    default:
        return 0;
    }
}

static uint32_t gl_pixel_bytes(GLenum format, GLenum type) {
    uint32_t comps = gl_format_components(format);

    if (!comps) {
        return 0;
    }

    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return comps;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return comps * 2u;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return comps * 4u;
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_5_6_5:
        return 2u;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
        return 4u;
    default:
        return 0;
    }
}

static uint32_t gl_pixel_span(GLsizei width, GLsizei height, GLenum format, GLenum type) {
    uint32_t pixel_bytes;
    uint32_t row_pixels;
    uint32_t row_bytes;
    uint32_t row_stride;
    uint64_t skip_bytes;
    uint64_t total;

    if (width <= 0 || height <= 0) {
        return 0;
    }

    pixel_bytes = gl_pixel_bytes(format, type);
    if (!pixel_bytes) {
        return 0;
    }

    row_pixels = g_unpack_row_length > 0 ? (uint32_t)g_unpack_row_length : (uint32_t)width;
    if (row_pixels < (uint32_t)width) {
        row_pixels = (uint32_t)width;
    }

    row_bytes = row_pixels * pixel_bytes;
    row_stride = align_u32(row_bytes, (uint32_t)(g_unpack_alignment > 0 ? g_unpack_alignment : 1));
    skip_bytes = (uint64_t)(g_unpack_skip_rows > 0 ? g_unpack_skip_rows : 0) * row_stride +
                 (uint64_t)(g_unpack_skip_pixels > 0 ? g_unpack_skip_pixels : 0) * pixel_bytes;
    total = skip_bytes +
            (uint64_t)((uint32_t)height - 1u) * row_stride +
            (uint64_t)(uint32_t)width * pixel_bytes;

    if (total > 0xFFFFFFFFu) {
        return 0;
    }

    return (uint32_t)total;
}

static uint8_t* alloc_payload(uint32_t size) {
    uint8_t* payload = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, size ? size : 1);

    if (!payload) {
        g_error = GL_OUT_OF_MEMORY;
    }

    return payload;
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

__declspec(dllexport)
void APIENTRY glGenTextures(GLsizei n, GLuint* textures) {
    uint32_t total_size;
    uint8_t* payload;
    GLint i;

    if (n < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!textures || n == 0) {
        return;
    }

    total_size = sizeof(uint32_t) + (uint32_t)n * sizeof(uint32_t);
    payload = alloc_payload(total_size);
    if (!payload) {
        return;
    }

    *(uint32_t*)payload = (uint32_t)n;
    for (i = 0; i < n; i++) {
        GLuint name = g_next_texture_id++;
        if (!name) {
            name = g_next_texture_id++;
        }
        textures[i] = name;
        ((uint32_t*)(payload + sizeof(uint32_t)))[i] = name;
    }

    emit_gl_call(GLFN_GEN_TEXTURES, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glDeleteTextures(GLsizei n, const GLuint* textures) {
    uint32_t total_size;
    uint8_t* payload;

    if (n < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!textures || n == 0) {
        return;
    }

    total_size = sizeof(uint32_t) + (uint32_t)n * sizeof(uint32_t);
    payload = alloc_payload(total_size);
    if (!payload) {
        return;
    }

    *(uint32_t*)payload = (uint32_t)n;
    CopyMemory(payload + sizeof(uint32_t), textures, (uint32_t)n * sizeof(uint32_t));
    emit_gl_call(GLFN_DELETE_TEXTURES, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glBindTexture(GLenum target, GLuint texture) {
    struct { uint32_t target, texture; } payload;
    payload.target = (uint32_t)target;
    payload.texture = (uint32_t)texture;
    emit_gl_call(GLFN_BIND_TEXTURE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLsizei height, GLint border,
                           GLenum format, GLenum type, const GLvoid* pixels) {
    struct {
        uint32_t target;
        int32_t level;
        int32_t internalformat;
        int32_t width;
        int32_t height;
        int32_t border;
        uint32_t format;
        uint32_t type;
        uint32_t data_size;
    } meta;
    uint32_t data_size = pixels ? gl_pixel_span(width, height, format, type) : 0;
    uint32_t total_size = sizeof(meta) + data_size;
    uint8_t* payload;

    if (pixels && !data_size) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload = alloc_payload(total_size);
    if (!payload) {
        return;
    }

    meta.target = (uint32_t)target;
    meta.level = level;
    meta.internalformat = internalformat;
    meta.width = width;
    meta.height = height;
    meta.border = border;
    meta.format = (uint32_t)format;
    meta.type = (uint32_t)type;
    meta.data_size = data_size;
    CopyMemory(payload, &meta, sizeof(meta));
    if (data_size) {
        CopyMemory(payload + sizeof(meta), pixels, data_size);
    }

    emit_gl_call(GLFN_TEX_IMAGE_2D, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height,
                              GLenum format, GLenum type, const GLvoid* pixels) {
    struct {
        uint32_t target;
        int32_t level;
        int32_t xoffset;
        int32_t yoffset;
        int32_t width;
        int32_t height;
        uint32_t format;
        uint32_t type;
        uint32_t data_size;
    } meta;
    uint32_t data_size = pixels ? gl_pixel_span(width, height, format, type) : 0;
    uint32_t total_size = sizeof(meta) + data_size;
    uint8_t* payload;

    if (pixels && !data_size) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload = alloc_payload(total_size);
    if (!payload) {
        return;
    }

    meta.target = (uint32_t)target;
    meta.level = level;
    meta.xoffset = xoffset;
    meta.yoffset = yoffset;
    meta.width = width;
    meta.height = height;
    meta.format = (uint32_t)format;
    meta.type = (uint32_t)type;
    meta.data_size = data_size;
    CopyMemory(payload, &meta, sizeof(meta));
    if (data_size) {
        CopyMemory(payload + sizeof(meta), pixels, data_size);
    }

    emit_gl_call(GLFN_TEX_SUB_IMAGE_2D, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    struct { uint32_t target, pname; int32_t param; } payload;
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_PARAMETERI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    struct { uint32_t target, pname; float param; } payload;
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_PARAMETERF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPixelStorei(GLenum pname, GLint param) {
    struct { uint32_t pname; int32_t param; } payload;

    switch (pname) {
    case GL_UNPACK_ALIGNMENT:
        if (param == 1 || param == 2 || param == 4 || param == 8) {
            g_unpack_alignment = param;
        }
        break;
    case GL_UNPACK_ROW_LENGTH:
        g_unpack_row_length = param > 0 ? param : 0;
        break;
    case GL_UNPACK_SKIP_ROWS:
        g_unpack_skip_rows = param > 0 ? param : 0;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        g_unpack_skip_pixels = param > 0 ? param : 0;
        break;
    default:
        break;
    }

    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_PIXEL_STOREI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param) {
    struct { uint32_t target, pname; int32_t param; } payload;
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_ENVI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    struct { uint32_t target, pname; float param; } payload;
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_ENVF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexCoord2f(GLfloat s, GLfloat t) {
    struct { float s, t; } payload;
    payload.s = s;
    payload.t = t;
    emit_gl_call(GLFN_TEX_COORD2F, &payload, sizeof(payload));
}
