// Tiny Win32 OpenGL query/multitexture test for simple_v86_opengl_wrapper.
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_query_multitexture_test.exe gl_query_multitexture_test.c -lopengl32 -lgdi32 -luser32 -lkernel32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#define GL_TEXTURE1_ARB 0x84C1
#define GL_ACTIVE_TEXTURE_ARB 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB 0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif

#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif

#ifndef GL_TEXTURE_PRIORITY
#define GL_TEXTURE_PRIORITY 0x8066
#endif

#define V86GL_TIMER_ID 1
#define TEX_SIZE 64
#define PROC_CHECK_COUNT 10
#define QUERY_CHECK_COUNT 9

typedef void (APIENTRY *PFNGLACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2FARBPROC)(GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4FVARBPROC)(GLenum target, const GLfloat* v);

static HGLRC g_rc = NULL;
static HDC   g_dc = NULL;
static GLuint g_tex0 = 0;
static GLuint g_tex1 = 0;
static unsigned char g_tex0_pixels[TEX_SIZE * TEX_SIZE * 4];
static unsigned char g_tex1_pixels[TEX_SIZE * TEX_SIZE * 4];
static int g_proc_checks[PROC_CHECK_COUNT];
static int g_query_checks[QUERY_CHECK_COUNT];
static int g_last_page = -1;

static PFNGLACTIVETEXTUREARBPROC g_glActiveTextureARB = NULL;
static PFNGLCLIENTACTIVETEXTUREARBPROC g_glClientActiveTextureARB = NULL;
static PFNGLMULTITEXCOORD2FARBPROC g_glMultiTexCoord2fARB = NULL;
static PFNGLMULTITEXCOORD4FVARBPROC g_glMultiTexCoord4fvARB = NULL;
static PFNGLACTIVETEXTUREARBPROC g_glActiveTexture = NULL;
static PFNGLCLIENTACTIVETEXTUREARBPROC g_glClientActiveTexture = NULL;
static PFNGLMULTITEXCOORD2FARBPROC g_glMultiTexCoord2f = NULL;

static int token_equals(const char* a, const char* b, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return b[len] == 0;
}

static int is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int extension_has_token(const char* extensions, const char* token) {
    const char* p = extensions;
    if (!extensions || !token) {
        return 0;
    }

    while (*p) {
        const char* start;
        int len;
        while (*p && is_space_char(*p)) {
            p++;
        }
        start = p;
        while (*p && !is_space_char(*p)) {
            p++;
        }
        len = (int)(p - start);
        if (len > 0 && token_equals(start, token, len)) {
            return 1;
        }
    }

    return 0;
}

static int near_float(float a, float b) {
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return d < 0.01f;
}

static int all_checks_ok(void) {
    int i;
    for (i = 0; i < PROC_CHECK_COUNT; i++) {
        if (!g_proc_checks[i]) {
            return 0;
        }
    }
    for (i = 0; i < QUERY_CHECK_COUNT; i++) {
        if (!g_query_checks[i]) {
            return 0;
        }
    }
    return 1;
}

static void build_textures(void) {
    int x;
    int y;

    for (y = 0; y < TEX_SIZE; y++) {
        for (x = 0; x < TEX_SIZE; x++) {
            int i = (y * TEX_SIZE + x) * 4;
            int c0 = ((x >> 3) ^ (y >> 3)) & 1;
            int c1 = ((x + y) >> 4) & 1;

            g_tex0_pixels[i + 0] = c0 ? 255 : 40;
            g_tex0_pixels[i + 1] = c0 ? 255 : 210;
            g_tex0_pixels[i + 2] = c0 ? 255 : 255;
            g_tex0_pixels[i + 3] = 255;

            g_tex1_pixels[i + 0] = c1 ? 255 : 255;
            g_tex1_pixels[i + 1] = c1 ? 220 : 40;
            g_tex1_pixels[i + 2] = c1 ? 40 : 255;
            g_tex1_pixels[i + 3] = 255;
        }
    }
}

static void load_extension_procs(void) {
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);

    g_glActiveTextureARB =
        (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTextureARB");
    g_glClientActiveTextureARB =
        (PFNGLCLIENTACTIVETEXTUREARBPROC)wglGetProcAddress("glClientActiveTextureARB");
    g_glMultiTexCoord2fARB =
        (PFNGLMULTITEXCOORD2FARBPROC)wglGetProcAddress("glMultiTexCoord2fARB");
    g_glMultiTexCoord4fvARB =
        (PFNGLMULTITEXCOORD4FVARBPROC)wglGetProcAddress("glMultiTexCoord4fvARB");
    g_glActiveTexture =
        (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTexture");
    g_glClientActiveTexture =
        (PFNGLCLIENTACTIVETEXTUREARBPROC)wglGetProcAddress("glClientActiveTexture");
    g_glMultiTexCoord2f =
        (PFNGLMULTITEXCOORD2FARBPROC)wglGetProcAddress("glMultiTexCoord2f");

    g_proc_checks[0] = extension_has_token(extensions, "GL_ARB_multitexture");
    g_proc_checks[1] = extension_has_token(extensions, "GL_EXT_bgra");
    g_proc_checks[2] = extension_has_token(extensions, "GL_EXT_texture_object");
    g_proc_checks[3] = wglGetProcAddress("glGetIntegerv") != NULL;
    g_proc_checks[4] = g_glActiveTextureARB != NULL;
    g_proc_checks[5] = g_glClientActiveTextureARB != NULL;
    g_proc_checks[6] = g_glMultiTexCoord2fARB != NULL;
    g_proc_checks[7] = g_glMultiTexCoord4fvARB != NULL;
    g_proc_checks[8] = g_glActiveTexture != NULL && g_glClientActiveTexture != NULL;
    g_proc_checks[9] = g_glMultiTexCoord2f != NULL &&
                       extensions != NULL && extensions[0] != 0;
}

static void init_multitexture_state(void) {
    build_textures();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &g_tex0);
    glGenTextures(1, &g_tex1);

    if (g_glActiveTextureARB) {
        g_glActiveTextureARB(GL_TEXTURE0_ARB);
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_tex0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 0.5f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, g_tex0_pixels);

    if (g_glActiveTextureARB) {
        g_glActiveTextureARB(GL_TEXTURE1_ARB);
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_tex1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 0.75f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, g_tex1_pixels);

    if (g_glClientActiveTextureARB) {
        g_glClientActiveTextureARB(GL_TEXTURE1_ARB);
    }
}

static void run_query_checks(void) {
    GLint value = 0;
    GLint values[4];
    GLfloat color[4];
    GLfloat priority = 0.0f;

    if (g_glActiveTextureARB) {
        g_glActiveTextureARB(GL_TEXTURE1_ARB);
    }
    if (g_glClientActiveTextureARB) {
        g_glClientActiveTextureARB(GL_TEXTURE1_ARB);
    }

    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &value);
    g_query_checks[0] = value >= 2;

    glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &value);
    g_query_checks[1] = value == GL_TEXTURE1_ARB;

    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE_ARB, &value);
    g_query_checks[2] = value == GL_TEXTURE1_ARB;

    g_query_checks[3] = glIsEnabled(GL_TEXTURE_2D) == GL_TRUE;

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
    g_query_checks[4] = value == (GLint)g_tex1;

    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &value);
    g_query_checks[5] = value == GL_LINEAR;

    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, &priority);
    g_query_checks[6] = near_float(priority, 0.75f);

    glColor4f(0.25f, 0.50f, 0.75f, 1.0f);
    glGetFloatv(GL_CURRENT_COLOR, color);
    g_query_checks[7] = near_float(color[0], 0.25f) &&
                        near_float(color[1], 0.50f) &&
                        near_float(color[2], 0.75f) &&
                        near_float(color[3], 1.0f);

    glViewport(0, 0, 320, 240);
    glGetIntegerv(GL_VIEWPORT, values);
    g_query_checks[8] = values[0] == 0 && values[1] == 0 &&
                        values[2] == 320 && values[3] == 240;
}

static void draw_rect(float x0, float y0, float x1, float y1,
                      float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, 0.0f);
    glVertex3f(x1, y0, 0.0f);
    glVertex3f(x1, y1, 0.0f);
    glVertex3f(x0, y1, 0.0f);
    glEnd();
}

static void setup_2d(HWND hwnd) {
    RECT rc;
    int width;
    int height;

    GetClientRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0) {
        width = 1;
    }
    if (height <= 0) {
        height = 1;
    }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void draw_status_grid(const int* checks, int count) {
    int i;
    float step = 1.72f / (float)count;
    float x = -0.86f;

    for (i = 0; i < count; i++) {
        float x0 = x + step * (float)i + 0.01f;
        float x1 = x + step * (float)(i + 1) - 0.01f;
        if (checks[i]) {
            draw_rect(x0, -0.35f, x1, 0.35f, 0.05f, 0.90f, 0.20f);
        } else {
            draw_rect(x0, -0.35f, x1, 0.35f, 0.95f, 0.08f, 0.08f);
        }
    }
}

static void draw_multitexture_quad(void) {
    static const GLfloat t10[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat t11[4] = { 2.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat t12[4] = { 2.0f, 2.0f, 0.0f, 1.0f };
    static const GLfloat t13[4] = { 0.0f, 2.0f, 0.0f, 1.0f };

    if (!g_glActiveTextureARB || !g_glMultiTexCoord2fARB || !g_glMultiTexCoord4fvARB) {
        draw_rect(-0.75f, -0.55f, 0.75f, 0.55f, 0.95f, 0.08f, 0.08f);
        return;
    }

    g_glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_tex0);
    g_glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_tex1);

    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    g_glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 0.0f);
    g_glMultiTexCoord4fvARB(GL_TEXTURE1_ARB, t10);
    glVertex3f(-0.78f, -0.58f, 0.0f);

    g_glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 3.0f, 0.0f);
    g_glMultiTexCoord4fvARB(GL_TEXTURE1_ARB, t11);
    glVertex3f(0.78f, -0.58f, 0.0f);

    g_glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 3.0f, 3.0f);
    g_glMultiTexCoord4fvARB(GL_TEXTURE1_ARB, t12);
    glVertex3f(0.78f, 0.58f, 0.0f);

    g_glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 3.0f);
    g_glMultiTexCoord4fvARB(GL_TEXTURE1_ARB, t13);
    glVertex3f(-0.78f, 0.58f, 0.0f);
    glEnd();

    g_glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    g_glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);

    if (all_checks_ok()) {
        draw_rect(-0.92f, -0.88f, 0.92f, -0.78f, 0.05f, 0.90f, 0.20f);
    } else {
        draw_rect(-0.92f, -0.88f, 0.92f, -0.78f, 0.95f, 0.08f, 0.08f);
    }
}

static void update_title(HWND hwnd, int page) {
    static const char* titles[] = {
        "v86 OpenGL query test: 1 extensions + wglGetProcAddress",
        "v86 OpenGL query test: 2 glGet state cache",
        "v86 OpenGL query test: 3 ARB multitexture draw"
    };

    if (page != g_last_page) {
        g_last_page = page;
        SetWindowTextA(hwnd, titles[page]);
    }
}

static void draw_test(HWND hwnd) {
    int page = (int)((GetTickCount() / 3000u) % 3u);

    update_title(hwnd, page);
    setup_2d(hwnd);

    if (page == 0) {
        glDisable(GL_TEXTURE_2D);
        draw_status_grid(g_proc_checks, PROC_CHECK_COUNT);
    } else if (page == 1) {
        glDisable(GL_TEXTURE_2D);
        draw_status_grid(g_query_checks, QUERY_CHECK_COUNT);
    } else {
        draw_multitexture_quad();
    }

    wglSwapLayerBuffers(g_dc, WGL_SWAP_MAIN_PLANE);
}

static void init_gl_state(void) {
    load_extension_procs();
    init_multitexture_state();
    run_query_checks();
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        PIXELFORMATDESCRIPTOR pfd;
        int pf;
        ZeroMemory(&pfd, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 16;
        pfd.iLayerType = PFD_MAIN_PLANE;
        g_dc = GetDC(hwnd);
        pf = ChoosePixelFormat(g_dc, &pfd);
        if (pf > 0) {
            SetPixelFormat(g_dc, pf, &pfd);
        }
        g_rc = wglCreateContext(g_dc);
        wglMakeCurrent(g_dc, g_rc);
        init_gl_state();
        SetTimer(hwnd, V86GL_TIMER_ID, 50, NULL);
        return 0;
    }
    case WM_TIMER:
        if (wp == V86GL_TIMER_ID) {
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        draw_test(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, V86GL_TIMER_ID);
        if (g_tex0) {
            glDeleteTextures(1, &g_tex0);
        }
        if (g_tex1) {
            glDeleteTextures(1, &g_tex1);
        }
        if (g_rc) {
            wglMakeCurrent(NULL, NULL);
            wglDeleteContext(g_rc);
        }
        if (g_dc) {
            ReleaseDC(hwnd, g_dc);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmd, int show) {
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;

    (void)prev;
    (void)cmd;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hinst;
    wc.lpszClassName = "V86GLQueryMultitexture";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hwnd = CreateWindowA("V86GLQueryMultitexture",
        "v86 OpenGL query test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        760, 520, NULL, NULL, hinst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

void WINAPI WinMainCRTStartup(void) {
    int code = WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
    ExitProcess((UINT)code);
}
