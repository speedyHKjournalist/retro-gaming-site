// gl_test_swap_postclear.c
// Purpose: detect broken SwapBuffers / back-buffer isolation / command queue frame-boundary handling.
//
// Correct result:
//   The visible screen shows a blue/green scene with a rotating colored diamond and UI blocks.
//   You should NOT see the post-swap black clear or magenta X.
// Broken result:
//   If the proxy draws directly to the WebGL canvas, treats SwapBuffers as a no-op, or reorders a post-swap
//   glClear before/after the wrong batch, the visible image becomes black and/or shows a magenta X.
//
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_test_swap_postclear.exe gl_test_swap_postclear.c -lopengl32 -lgdi32 -luser32 -lkernel32

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

static void draw_scene_before_swap(int w, int h, GLfloat a) {
    GLfloat cx;
    GLfloat cy;
    cx = (GLfloat)w * 0.5f;
    cy = (GLfloat)h * 0.5f;

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    set_ortho(w, h);

    // The actual frame that should be presented.
    glClearColor(0.02f, 0.08f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBegin(GL_QUADS);
    glColor3f(0.04f, 0.14f, 0.45f); glVertex2f(0.0f, 0.0f);
    glColor3f(0.08f, 0.45f, 0.80f); glVertex2f((GLfloat)w, 0.0f);
    glColor3f(0.03f, 0.28f, 0.16f); glVertex2f((GLfloat)w, (GLfloat)h);
    glColor3f(0.05f, 0.45f, 0.20f); glVertex2f(0.0f, (GLfloat)h);
    glEnd();

    // Animated diamond marker.
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glRotatef(a, 0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glColor3f(1.0f, 0.1f, 0.1f); glVertex2f(0.0f, -100.0f);
    glColor3f(0.1f, 1.0f, 0.1f); glVertex2f(100.0f, 0.0f);
    glColor3f(0.1f, 0.4f, 1.0f); glVertex2f(0.0f, 100.0f);
    glColor3f(1.0f, 1.0f, 0.1f); glVertex2f(-100.0f, 0.0f);
    glEnd();
    glPopMatrix();

    // Opaque UI drawn before swap; this should be visible.
    glBegin(GL_QUADS);
    glColor3f(0.0f, 0.0f, 0.70f);
    glVertex2f(50.0f, 50.0f); glVertex2f(260.0f, 50.0f); glVertex2f(260.0f, 130.0f); glVertex2f(50.0f, 130.0f);
    glColor3f(0.75f, 0.65f, 0.05f);
    glVertex2f(70.0f, 160.0f); glVertex2f(290.0f, 160.0f); glVertex2f(290.0f, 205.0f); glVertex2f(70.0f, 205.0f);
    glColor3f(0.75f, 0.0f, 0.0f);
    glVertex2f((GLfloat)w - 180.0f, (GLfloat)h - 80.0f); glVertex2f((GLfloat)w - 40.0f, (GLfloat)h - 80.0f); glVertex2f((GLfloat)w - 40.0f, (GLfloat)h - 35.0f); glVertex2f((GLfloat)w - 180.0f, (GLfloat)h - 35.0f);
    glEnd();
}

static void poison_back_buffer_after_swap(int w, int h) {
    // This code intentionally runs AFTER SwapBuffers and is NOT followed by another SwapBuffers.
    // Correct WGL: it clears/draws into the back buffer only, so the user never sees it.
    // Broken proxy/single-buffer canvas: the user sees black or a magenta X.
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    set_ortho(w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLineWidth(6.0f);
    glColor3f(1.0f, 0.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex2f(20.0f, 20.0f); glVertex2f((GLfloat)w - 20.0f, (GLfloat)h - 20.0f);
    glVertex2f((GLfloat)w - 20.0f, 20.0f); glVertex2f(20.0f, (GLfloat)h - 20.0f);
    glEnd();
    glLineWidth(1.0f);

    // Force the post-swap commands to be sent. They still must not become visible until a later swap.
    glFlush();
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

    draw_scene_before_swap(w, h, a);
    v86gl_swap();
    poison_back_buffer_after_swap(w, h);
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
    wc.lpszClassName = "V86GLSwapPostClear";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    hwnd = CreateWindowA("V86GLSwapPostClear", "TEST 3 - swap/backbuffer: black post-clear must NOT be visible",
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
