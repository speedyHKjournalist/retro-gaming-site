// gl_test_depth_clear_poison.c
// Purpose: detect broken depth clear / glDepthMask / glClearDepth handling in an OpenGL proxy.
//
// Correct result:
//   A rotating colored cube/diamond stays visible every frame. Opaque 2D UI bars are visible on top.
// Broken depth-clear result:
//   The first frame may look correct, then the 3D scene becomes black while the 2D UI bars remain visible.
//   This mimics Warcraft III: background appears briefly and then gets covered by black, while UI is still drawn.
//
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_test_depth_clear_poison.exe gl_test_depth_clear_poison.c -lopengl32 -lgdi32 -luser32 -lkernel32

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

static void set_perspective(int w, int h) {
    GLfloat aspect;
    if (h <= 0) h = 1;
    aspect = (GLfloat)w / (GLfloat)h;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect, aspect, -1.0, 1.0, 1.5, 40.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void set_ortho(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_quad3(GLfloat r, GLfloat g, GLfloat b,
                       GLfloat x0, GLfloat y0, GLfloat z0,
                       GLfloat x1, GLfloat y1, GLfloat z1,
                       GLfloat x2, GLfloat y2, GLfloat z2,
                       GLfloat x3, GLfloat y3, GLfloat z3) {
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y1, z1);
    glVertex3f(x2, y2, z2);
    glVertex3f(x3, y3, z3);
    glEnd();
}

static void draw_cube(void) {
    glBegin(GL_QUADS);

    glColor3f(1.0f, 0.10f, 0.10f);
    glVertex3f(-1.0f, -1.0f,  1.0f); glVertex3f( 1.0f, -1.0f,  1.0f); glVertex3f( 1.0f,  1.0f,  1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
    glColor3f(0.10f, 0.85f, 0.20f);
    glVertex3f(-1.0f, -1.0f, -1.0f); glVertex3f(-1.0f,  1.0f, -1.0f); glVertex3f( 1.0f,  1.0f, -1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    glColor3f(0.15f, 0.35f, 1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f); glVertex3f( 1.0f,  1.0f, -1.0f); glVertex3f( 1.0f,  1.0f,  1.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
    glColor3f(1.0f, 0.85f, 0.10f);
    glVertex3f(-1.0f, -1.0f, -1.0f); glVertex3f(-1.0f, -1.0f,  1.0f); glVertex3f(-1.0f,  1.0f,  1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glColor3f(1.0f, 0.20f, 0.90f);
    glVertex3f(-1.0f,  1.0f, -1.0f); glVertex3f(-1.0f,  1.0f,  1.0f); glVertex3f( 1.0f,  1.0f,  1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
    glColor3f(0.10f, 0.90f, 1.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f); glVertex3f( 1.0f, -1.0f, -1.0f); glVertex3f( 1.0f, -1.0f,  1.0f); glVertex3f(-1.0f, -1.0f,  1.0f);

    glEnd();
}

static void draw_ui_overlay(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    set_ortho(w, h);

    // Left panel border.
    glColor3f(0.85f, 0.75f, 0.10f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(40.0f, 50.0f); glVertex2f(300.0f, 50.0f); glVertex2f(300.0f, 210.0f); glVertex2f(40.0f, 210.0f);
    glEnd();

    // Three opaque UI buttons that should survive even if the 3D background is blocked by stale depth.
    glBegin(GL_QUADS);
    glColor3f(0.0f, 0.0f, 0.75f);
    glVertex2f(60.0f, 240.0f); glVertex2f(180.0f, 240.0f); glVertex2f(180.0f, 285.0f); glVertex2f(60.0f, 285.0f);
    glColor3f(0.0f, 0.55f, 0.0f);
    glVertex2f(220.0f, 240.0f); glVertex2f(340.0f, 240.0f); glVertex2f(340.0f, 285.0f); glVertex2f(220.0f, 285.0f);
    glColor3f(0.75f, 0.0f, 0.0f);
    glVertex2f((GLfloat)w - 180.0f, (GLfloat)h - 80.0f); glVertex2f((GLfloat)w - 40.0f, (GLfloat)h - 80.0f); glVertex2f((GLfloat)w - 40.0f, (GLfloat)h - 35.0f); glVertex2f((GLfloat)w - 180.0f, (GLfloat)h - 35.0f);
    glEnd();
}

static void install_invisible_depth_poison(void) {
    // This writes a near-depth full-screen quad with color writes disabled.
    // A correct proxy will erase it next frame with glClear(GL_DEPTH_BUFFER_BIT).
    // A broken depth clear leaves this depth in the buffer, making later 3D draws fail depth test.
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_QUADS);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glEnd();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
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

    // These lines are the actual test target.
    // If your proxy ignores glDepthMask or does not clear the depth attachment, the scene dies after frame 1.
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_TEXTURE_2D);

    set_perspective(w, h);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -7.0f);
    glRotatef(a, 0.0f, 1.0f, 0.0f);
    glRotatef(a * 0.7f, 1.0f, 0.0f, 0.0f);
    draw_cube();

    // Draw a far colored floor to make stale near-depth very obvious.
    set_perspective(w, h);
    glLoadIdentity();
    draw_quad3(0.10f, 0.25f, 0.65f, -8.0f, -3.0f, -14.0f, 8.0f, -3.0f, -14.0f, 8.0f, 3.0f, -14.0f, -8.0f, 3.0f, -14.0f);

    draw_ui_overlay(w, h);
    install_invisible_depth_poison();

    v86gl_swap();
}

static void init_gl(void) {
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_FLAT);
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
    wc.lpszClassName = "V86GLDepthClearPoison";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    hwnd = CreateWindowA("V86GLDepthClearPoison", "TEST 1 - depth clear poison: cube must stay visible",
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
