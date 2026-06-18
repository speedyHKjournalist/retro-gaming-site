// Tiny Win32 OpenGL rotating cube test for simple_v86_opengl_wrapper.
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_rotate_cube_test.exe gl_rotate_cube_test.c -lopengl32 -lgdi32 -luser32 -lkernel32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#define V86GL_TIMER_ID 1
#define TEXTURE_SIZE 64
#define PATCH_SIZE 16

#ifndef GL_TEXTURE_PRIORITY
#define GL_TEXTURE_PRIORITY 0x8066
#endif

static HGLRC g_rc = NULL;
static HDC   g_dc = NULL;
static GLuint g_checker_texture = 0;
static unsigned char g_checker_pixels[TEXTURE_SIZE * TEXTURE_SIZE * 4];
static unsigned char g_patch_pixels[PATCH_SIZE * PATCH_SIZE * 4];

static void build_checker_texture(void) {
    int x;
    int y;
    int i;

    for (y = 0; y < TEXTURE_SIZE; y++) {
        for (x = 0; x < TEXTURE_SIZE; x++) {
            int checker = ((x >> 3) ^ (y >> 3)) & 1;
            int border = (x < 2 || y < 2 || x >= TEXTURE_SIZE - 2 || y >= TEXTURE_SIZE - 2);
            unsigned char base = (unsigned char)(checker ? 235 : 65);
            unsigned char line = border ? 255 : base;
            i = (y * TEXTURE_SIZE + x) * 4;
            g_checker_pixels[i + 0] = line;
            g_checker_pixels[i + 1] = checker ? 235 : 105;
            g_checker_pixels[i + 2] = checker ? 235 : 185;
            g_checker_pixels[i + 3] = 255;
        }
    }

    for (y = 0; y < PATCH_SIZE; y++) {
        for (x = 0; x < PATCH_SIZE; x++) {
            i = (y * PATCH_SIZE + x) * 4;
            g_patch_pixels[i + 0] = 255;
            g_patch_pixels[i + 1] = (unsigned char)(40 + x * 10);
            g_patch_pixels[i + 2] = (unsigned char)(40 + y * 10);
            g_patch_pixels[i + 3] = 255;
        }
    }
}

static void init_texture(void) {
    build_checker_texture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &g_checker_texture);
    glBindTexture(GL_TEXTURE_2D, g_checker_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 1.0f);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_SIZE, TEXTURE_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, g_checker_pixels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 24, 24, PATCH_SIZE, PATCH_SIZE,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_patch_pixels);
}

static void draw_face(float r, float g, float b,
                      float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      float x3, float y3, float z3) {
    glColor3f(r, g, b);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(x1, y1, z1);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(x2, y2, z2);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(x3, y3, z3);
}

static void draw_cube_geometry(void) {
    glBegin(GL_QUADS);

    draw_face(1.0f, 0.10f, 0.10f,
              -1.0f, -1.0f,  1.0f,
               1.0f, -1.0f,  1.0f,
               1.0f,  1.0f,  1.0f,
              -1.0f,  1.0f,  1.0f);

    draw_face(0.10f, 0.85f, 0.20f,
              -1.0f, -1.0f, -1.0f,
              -1.0f,  1.0f, -1.0f,
               1.0f,  1.0f, -1.0f,
               1.0f, -1.0f, -1.0f);

    draw_face(0.15f, 0.35f, 1.0f,
               1.0f, -1.0f, -1.0f,
               1.0f,  1.0f, -1.0f,
               1.0f,  1.0f,  1.0f,
               1.0f, -1.0f,  1.0f);

    draw_face(1.0f, 0.85f, 0.10f,
              -1.0f, -1.0f, -1.0f,
              -1.0f, -1.0f,  1.0f,
              -1.0f,  1.0f,  1.0f,
              -1.0f,  1.0f, -1.0f);

    draw_face(1.0f, 0.20f, 0.90f,
              -1.0f,  1.0f, -1.0f,
              -1.0f,  1.0f,  1.0f,
               1.0f,  1.0f,  1.0f,
               1.0f,  1.0f, -1.0f);

    draw_face(0.10f, 0.90f, 1.0f,
              -1.0f, -1.0f, -1.0f,
               1.0f, -1.0f, -1.0f,
               1.0f, -1.0f,  1.0f,
              -1.0f, -1.0f,  1.0f);

    glEnd();
}

static void draw_cube(HWND hwnd) {
    RECT rc;
    int width;
    int height;
    float aspect;
    DWORD ticks;
    float angle;

    GetClientRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0) {
        width = 1;
    }
    if (height <= 0) {
        height = 1;
    }

    aspect = (float)width / (float)height;
    ticks = GetTickCount();
    angle = (float)(ticks % 60000u) * 0.06f;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect, aspect, -1.0, 1.0, 1.5, 30.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -6.0f);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glRotatef(angle * 0.7f, 1.0f, 0.0f, 0.0f);
    glRotatef(angle * 0.3f, 0.0f, 0.0f, 1.0f);

    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    draw_cube_geometry();
    glPopMatrix();

    wglSwapLayerBuffers(g_dc, WGL_SWAP_MAIN_PLANE);
}

static void init_gl_state(void) {
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_FLAT);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_TEXTURE_2D);
    init_texture();
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        PIXELFORMATDESCRIPTOR pfd;
        ZeroMemory(&pfd, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 16;
        pfd.iLayerType = PFD_MAIN_PLANE;
        g_dc = GetDC(hwnd);
        int pf = ChoosePixelFormat(g_dc, &pfd);
        if (pf > 0) {
            SetPixelFormat(g_dc, pf, &pfd);
        }
        g_rc = wglCreateContext(g_dc);
        wglMakeCurrent(g_dc, g_rc);
        init_gl_state();
        SetTimer(hwnd, V86GL_TIMER_ID, 16, NULL);
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
        draw_cube(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, V86GL_TIMER_ID);
        if (g_checker_texture) {
            glDeleteTextures(1, &g_checker_texture);
            g_checker_texture = 0;
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
    (void)prev;
    (void)cmd;
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hinst;
    wc.lpszClassName = "V86GLRotateCube";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("V86GLRotateCube", "v86 fake OpenGL textured cube",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        640, 480, NULL, NULL, hinst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg;
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
