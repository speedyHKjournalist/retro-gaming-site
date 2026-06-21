// gl_test_blend_alpha_fade.c
// Purpose: detect broken GL_BLEND / glBlendFunc / glColor4ub alpha handling in an OpenGL proxy.
//
// Correct result:
//   A colorful animated background remains visible, dimmed by a semi-transparent black fade layer.
//   Opaque 2D UI rectangles remain bright on top.
// Broken blend/alpha result:
//   The fade layer becomes fully opaque black, so only the later UI rectangles are visible.
//   This is very close to Warcraft III menu fade/mask failures.
//
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_test_blend_alpha_fade.exe gl_test_blend_alpha_fade.c -lopengl32 -lgdi32 -luser32 -lkernel32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef WGL_SWAP_MAIN_PLANE
#define WGL_SWAP_MAIN_PLANE 0x00000001
#endif

#define TIMER_ID 1

static HGLRC g_rc = NULL;
static HDC   g_dc = NULL;

static void v86gl_swap(void) {
    if (!wglSwapLayerBuffers(g_dc, WGL_SWAP_MAIN_PLANE)) {
        SwapBuffers(g_dc);
    }
}

static void set_ortho(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_background(int w, int h, GLfloat a) {
    GLfloat cx;
    GLfloat cy;
    cx = (GLfloat)w * 0.5f;
    cy = (GLfloat)h * 0.5f;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    set_ortho(w, h);

    // Colorful full-screen gradient-like quads.
    glBegin(GL_QUADS);
    glColor3f(0.05f, 0.20f, 0.80f); glVertex2f(0.0f, 0.0f);
    glColor3f(0.00f, 0.80f, 0.95f); glVertex2f((GLfloat)w, 0.0f);
    glColor3f(0.30f, 0.05f, 0.55f); glVertex2f((GLfloat)w, (GLfloat)h);
    glColor3f(0.95f, 0.55f, 0.10f); glVertex2f(0.0f, (GLfloat)h);
    glEnd();

    // Animated colored diamond so it is obvious whether the background is still visible.
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glRotatef(a, 0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(0.0f, -110.0f);
    glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(110.0f, 0.0f);
    glColor3f(0.0f, 0.0f, 1.0f); glVertex2f(0.0f, 110.0f);
    glColor3f(1.0f, 1.0f, 0.0f); glVertex2f(-110.0f, 0.0f);
    glEnd();
    glPopMatrix();
}

static void draw_black_fade_layer(int w, int h) {
    // This is the key test: color alpha is supplied through glColor4ub, not glColor4f.
    // Many fixed-pipeline games use byte colors. If the proxy ignores alpha or GL_BLEND,
    // this quad becomes opaque black and hides the background.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(0, 0, 0, 90);  // 90/255 ~= 0.35 alpha. Correct: dim, not black.

    set_ortho(w, h);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((GLfloat)w, 0.0f);
    glVertex2f((GLfloat)w, (GLfloat)h);
    glVertex2f(0.0f, (GLfloat)h);
    glEnd();

    glDisable(GL_BLEND);
    glColor4ub(255, 255, 255, 255);
}

static void draw_ui_overlay(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    set_ortho(w, h);

    // Warcraft-like opaque UI elements drawn after the fade layer.
    glBegin(GL_QUADS);
    glColor3f(0.02f, 0.02f, 0.08f);
    glVertex2f(40.0f, 45.0f); glVertex2f(330.0f, 45.0f); glVertex2f(330.0f, 215.0f); glVertex2f(40.0f, 215.0f);
    glColor3f(0.0f, 0.0f, 0.75f);
    glVertex2f(70.0f, 238.0f); glVertex2f(195.0f, 238.0f); glVertex2f(195.0f, 285.0f); glVertex2f(70.0f, 285.0f);
    glColor3f(0.0f, 0.55f, 0.0f);
    glVertex2f(220.0f, 238.0f); glVertex2f(345.0f, 238.0f); glVertex2f(345.0f, 285.0f); glVertex2f(220.0f, 285.0f);
    glColor3f(0.75f, 0.0f, 0.0f);
    glVertex2f((GLfloat)w - 190.0f, (GLfloat)h - 85.0f); glVertex2f((GLfloat)w - 40.0f, (GLfloat)h - 85.0f); glVertex2f((GLfloat)w - 40.0f, (GLfloat)h - 35.0f); glVertex2f((GLfloat)w - 190.0f, (GLfloat)h - 35.0f);
    glEnd();

    glColor3f(0.95f, 0.85f, 0.05f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(40.0f, 45.0f); glVertex2f(330.0f, 45.0f); glVertex2f(330.0f, 215.0f); glVertex2f(40.0f, 215.0f);
    glEnd();
}

static void render(HWND hwnd) {
    RECT rc;
    int w, h;
    DWORD t;
    GLfloat a;

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    t = GetTickCount();
    a = (GLfloat)(t % 60000u) * 0.06f;

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_background(w, h, a);
    draw_black_fade_layer(w, h);
    draw_ui_overlay(w, h);

    v86gl_swap();
}

static void init_gl(void) {
    glShadeModel(GL_SMOOTH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
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
        if (pf > 0) SetPixelFormat(g_dc, pf, &pfd);
        g_rc = wglCreateContext(g_dc);
        wglMakeCurrent(g_dc, g_rc);
        init_gl();
        SetTimer(hwnd, TIMER_ID, 16, NULL);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_ID) { InvalidateRect(hwnd, NULL, FALSE); return 0; }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        render(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        if (g_rc) { wglMakeCurrent(NULL, NULL); wglDeleteContext(g_rc); g_rc = NULL; }
        if (g_dc) { ReleaseDC(hwnd, g_dc); g_dc = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmd, int show) {
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    (void)prev; (void)cmd;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hinst;
    wc.lpszClassName = "V86GLBlendAlphaFade";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    hwnd = CreateWindowA("V86GLBlendAlphaFade", "TEST 2 - blend alpha fade: background must be dim, not black",
                         WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                         640, 480, NULL, NULL, hinst, NULL);
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
