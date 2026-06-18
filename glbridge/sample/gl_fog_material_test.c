// Tiny Win32 OpenGL fog/material test for simple_v86_opengl_wrapper.
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_fog_material_test.exe gl_fog_material_test.c -lopengl32 -lgdi32 -luser32 -lkernel32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING 0x0B50
#endif
#ifndef GL_LIGHT0
#define GL_LIGHT0 0x4000
#endif
#ifndef GL_NORMALIZE
#define GL_NORMALIZE 0x0BA1
#endif
#ifndef GL_FOG
#define GL_FOG 0x0B60
#endif
#ifndef GL_FOG_DENSITY
#define GL_FOG_DENSITY 0x0B62
#endif
#ifndef GL_FOG_START
#define GL_FOG_START 0x0B63
#endif
#ifndef GL_FOG_END
#define GL_FOG_END 0x0B64
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE 0x0B65
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR 0x0B66
#endif
#ifndef GL_EXP2
#define GL_EXP2 0x0801
#endif
#ifndef GL_LINEAR
#define GL_LINEAR 0x2601
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_AMBIENT_AND_DIFFUSE
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#endif
#ifndef GL_DIFFUSE
#define GL_DIFFUSE 0x1201
#endif
#ifndef GL_SPECULAR
#define GL_SPECULAR 0x1202
#endif
#ifndef GL_EMISSION
#define GL_EMISSION 0x1600
#endif
#ifndef GL_SHININESS
#define GL_SHININESS 0x1601
#endif

#define V86GL_TIMER_ID 1
#define PAGE_COUNT 3
#define GL_FULL_INT 2147483647L

static HGLRC g_rc = NULL;
static HDC g_dc = NULL;
static int g_last_page = -1;

static void get_client_size(HWND hwnd, int* width, int* height) {
    RECT rc;

    GetClientRect(hwnd, &rc);
    *width = rc.right - rc.left;
    *height = rc.bottom - rc.top;
    if (*width <= 0) {
        *width = 1;
    }
    if (*height <= 0) {
        *height = 1;
    }
}

static void setup_projection(HWND hwnd) {
    int width;
    int height;
    float aspect;

    get_client_size(hwnd, &width, &height);
    aspect = (float)width / (float)height;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect, aspect, -1.0, 1.0, 1.5, 30.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void reset_state(void) {
    static const GLfloat no_emission[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat default_specular[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat default_diffuse[4] = { 0.8f, 0.8f, 0.8f, 1.0f };

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_NORMALIZE);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, no_emission);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, default_specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, default_diffuse);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0.0f);
}

static void clear_scene(void) {
    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void draw_quad_z(float x0, float y0, float x1, float y1, float z) {
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z);
    glVertex3f(x1, y0, z);
    glVertex3f(x1, y1, z);
    glVertex3f(x0, y1, z);
    glEnd();
}

static void draw_lit_quad(float x0, float y0, float x1, float y1,
                          float nx, float ny, float nz) {
    glBegin(GL_QUADS);
    glNormal3f(nx, ny, nz);
    glVertex3f(x0, y0, -5.0f);
    glNormal3f(nx, ny, nz);
    glVertex3f(x1, y0, -5.0f);
    glNormal3f(nx, ny, nz);
    glVertex3f(x1, y1, -5.0f);
    glNormal3f(nx, ny, nz);
    glVertex3f(x0, y1, -5.0f);
    glEnd();
}

static void draw_fog_screen(HWND hwnd) {
    static const GLfloat fog_color[4] = { 0.58f, 0.62f, 0.72f, 1.0f };
    int i;

    setup_projection(hwnd);
    clear_scene();
    reset_state();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);
    glFogfv(GL_FOG_COLOR, fog_color);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 2.0f);
    glFogf(GL_FOG_END, 13.0f);
    glFogf(GL_FOG_DENSITY, 0.20f);

    for (i = 0; i < 7; i++) {
        float x0 = -0.92f + (float)i * 0.27f;
        float x1 = x0 + 0.20f;
        float z = -2.0f - (float)i * 1.7f;
        glColor3f(1.0f, 0.25f + (float)i * 0.08f, 0.08f);
        draw_quad_z(x0, -0.18f, x1, 0.62f, z);
    }

    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, 0.16f);
    for (i = 0; i < 7; i++) {
        float x0 = -0.92f + (float)i * 0.27f;
        float x1 = x0 + 0.20f;
        float z = -2.0f - (float)i * 1.7f;
        glColor3f(0.05f, 0.95f, 0.35f + (float)i * 0.06f);
        draw_quad_z(x0, -0.82f, x1, -0.32f, z);
    }

    glDisable(GL_FOG);
}

static void draw_materialfv_screen(HWND hwnd) {
    static const GLfloat red_mat[4] = { 0.95f, 0.12f, 0.08f, 1.0f };
    static const GLfloat green_mat[4] = { 0.08f, 0.85f, 0.22f, 1.0f };
    static const GLfloat blue_mat[4] = { 0.08f, 0.22f, 0.95f, 1.0f };
    static const GLfloat white_specular[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    setup_projection(hwnd);
    clear_scene();
    reset_state();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white_specular);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, red_mat);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 4.0f);
    draw_lit_quad(-0.90f, -0.52f, -0.34f, 0.52f, 0.0f, 0.0f, 1.0f);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, green_mat);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
    draw_lit_quad(-0.28f, -0.52f, 0.28f, 0.52f, 0.45f, 0.0f, 0.90f);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, blue_mat);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 96.0f);
    draw_lit_quad(0.34f, -0.52f, 0.90f, 0.52f, -0.60f, 0.25f, 0.75f);

    glDisable(GL_LIGHTING);
}

static void draw_materialiv_screen(HWND hwnd) {
    static const GLint red_emission[4] = { GL_FULL_INT, 0, 0, GL_FULL_INT };
    static const GLint green_emission[4] = { 0, GL_FULL_INT, 0, GL_FULL_INT };
    static const GLint blue_emission[4] = { 0, 0, GL_FULL_INT, GL_FULL_INT };
    static const GLint dark_diffuse[4] = { 0, 0, 0, GL_FULL_INT };

    setup_projection(hwnd);
    clear_scene();
    reset_state();

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glMaterialiv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, dark_diffuse);

    glMaterialiv(GL_FRONT_AND_BACK, GL_EMISSION, red_emission);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 8);
    draw_lit_quad(-0.90f, -0.52f, -0.34f, 0.52f, 0.0f, 0.0f, 1.0f);

    glMaterialiv(GL_FRONT_AND_BACK, GL_EMISSION, green_emission);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 32);
    draw_lit_quad(-0.28f, -0.52f, 0.28f, 0.52f, 0.0f, 0.0f, 1.0f);

    glMaterialiv(GL_FRONT_AND_BACK, GL_EMISSION, blue_emission);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 96);
    draw_lit_quad(0.34f, -0.52f, 0.90f, 0.52f, 0.0f, 0.0f, 1.0f);

    glDisable(GL_LIGHTING);
}

static int current_page(void) {
    return (int)((GetTickCount() / 3000u) % PAGE_COUNT);
}

static void update_title(HWND hwnd, int page) {
    if (page == g_last_page) {
        return;
    }

    g_last_page = page;
    if (page == 0) {
        SetWindowTextA(hwnd, "v86 OpenGL fog/material: 1 glFogf glFogi glFogfv");
    } else if (page == 1) {
        SetWindowTextA(hwnd, "v86 OpenGL fog/material: 2 glNormal3f glMaterialfv glMaterialf");
    } else {
        SetWindowTextA(hwnd, "v86 OpenGL fog/material: 3 glMaterialiv glMateriali");
    }
}

static void draw_test(HWND hwnd) {
    int page = current_page();

    update_title(hwnd, page);
    if (page == 0) {
        draw_fog_screen(hwnd);
    } else if (page == 1) {
        draw_materialfv_screen(hwnd);
    } else {
        draw_materialiv_screen(hwnd);
    }

    wglSwapLayerBuffers(g_dc, WGL_SWAP_MAIN_PLANE);
}

static void init_gl_state(void) {
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
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
        SetTimer(hwnd, V86GL_TIMER_ID, 33, NULL);
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
    wc.lpszClassName = "V86GLFogMaterial";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hwnd = CreateWindowA("V86GLFogMaterial", "v86 OpenGL fog/material test",
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
