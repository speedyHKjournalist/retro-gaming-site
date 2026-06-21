// Tiny Win32 OpenGL blend/UI state test for simple_v86_opengl_wrapper.
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_blend_ui_test.exe gl_blend_ui_test.c -lopengl32 -lgdi32 -luser32 -lkernel32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_GREATER
#define GL_GREATER 0x0204
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300
#endif
#ifndef GL_TEXTURE_ENV_MODE
#define GL_TEXTURE_ENV_MODE 0x2200
#endif
#ifndef GL_MODULATE
#define GL_MODULATE 0x2100
#endif

#define V86GL_TIMER_ID 1
#define ALPHA_TEXTURE_SIZE 32
#define PAGE_COUNT 7

static HGLRC g_rc = NULL;
static HDC g_dc = NULL;
static GLuint g_alpha_texture = 0;
static int g_last_page = -1;
static unsigned char g_alpha_pixels[ALPHA_TEXTURE_SIZE * ALPHA_TEXTURE_SIZE * 4];

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

static void setup_2d(HWND hwnd, int* width, int* height) {
    get_client_size(hwnd, width, height);
    glViewport(0, 0, *width, *height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_quad(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, 0.0f);
    glVertex3f(x1, y0, 0.0f);
    glVertex3f(x1, y1, 0.0f);
    glVertex3f(x0, y1, 0.0f);
    glEnd();
}

static void draw_textured_quad(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(x0, y0, 0.0f);
    glTexCoord2f(4.0f, 0.0f);
    glVertex3f(x1, y0, 0.0f);
    glTexCoord2f(4.0f, 4.0f);
    glVertex3f(x1, y1, 0.0f);
    glTexCoord2f(0.0f, 4.0f);
    glVertex3f(x0, y1, 0.0f);
    glEnd();
}

static void draw_line(float x0, float y0, float x1, float y1) {
    glBegin(GL_LINES);
    glVertex3f(x0, y0, 0.0f);
    glVertex3f(x1, y1, 0.0f);
    glEnd();
}

static void draw_frame(float x0, float y0, float x1, float y1) {
    glBegin(GL_LINES);
    glVertex3f(x0, y0, 0.0f);
    glVertex3f(x1, y0, 0.0f);
    glVertex3f(x1, y0, 0.0f);
    glVertex3f(x1, y1, 0.0f);
    glVertex3f(x1, y1, 0.0f);
    glVertex3f(x0, y1, 0.0f);
    glVertex3f(x0, y1, 0.0f);
    glVertex3f(x0, y0, 0.0f);
    glEnd();
}

static void build_alpha_texture(void) {
    int x;
    int y;

    for (y = 0; y < ALPHA_TEXTURE_SIZE; y++) {
        for (x = 0; x < ALPHA_TEXTURE_SIZE; x++) {
            int checker = ((x >> 2) ^ (y >> 2)) & 1;
            int i = (y * ALPHA_TEXTURE_SIZE + x) * 4;
            g_alpha_pixels[i + 0] = checker ? 255 : 50;
            g_alpha_pixels[i + 1] = checker ? 230 : 180;
            g_alpha_pixels[i + 2] = checker ? 80 : 255;
            g_alpha_pixels[i + 3] = checker ? 255 : 0;
        }
    }
}

static void init_texture(void) {
    build_alpha_texture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &g_alpha_texture);
    glBindTexture(GL_TEXTURE_2D, g_alpha_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ALPHA_TEXTURE_SIZE, ALPHA_TEXTURE_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, g_alpha_pixels);
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
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 1 glBlendFunc");
    } else if (page == 1) {
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 2 glAlphaFunc");
    } else if (page == 2) {
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 3 glColorMask");
    } else if (page == 3) {
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 4 glScissor");
    } else if (page == 4) {
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 5 glLineWidth");
    } else if (page == 5) {
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 6 glPolygonMode");
    } else {
        SetWindowTextA(hwnd, "v86 OpenGL UI state: 7 glReadPixels PCI readback");
    }
}

static void reset_state(void) {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void draw_blend_screen(void) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(1.0f, 0.05f, 0.05f, 0.62f);
    draw_quad(-0.75f, -0.55f, 0.20f, 0.55f);
    glColor4f(0.05f, 0.25f, 1.0f, 0.62f);
    draw_quad(-0.20f, -0.35f, 0.75f, 0.75f);

    glDisable(GL_BLEND);
}

static void draw_alpha_screen(void) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_alpha_texture);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    draw_textured_quad(-0.78f, -0.66f, 0.78f, 0.66f);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
}

static void draw_color_mask_screen(void) {
    glColor4f(0.18f, 0.18f, 0.18f, 1.0f);
    draw_quad(-0.85f, -0.65f, 0.85f, 0.65f);

    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    draw_quad(-0.60f, -0.42f, 0.60f, 0.42f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glColor4f(1.0f, 0.0f, 1.0f, 1.0f);
    draw_quad(0.62f, 0.44f, 0.84f, 0.66f);
}

static void draw_scissor_screen(int width, int height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(width / 4, height / 4, width / 2, height / 2);
    glClearColor(0.0f, 0.75f, 0.30f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    glColor4f(1.0f, 1.0f, 0.15f, 1.0f);
    glLineWidth(3.0f);
    draw_frame(-0.50f, -0.50f, 0.50f, 0.50f);
    glLineWidth(1.0f);
}

static void draw_line_width_screen(void) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 0.20f, 0.15f, 1.0f);
    glLineWidth(1.0f);
    draw_line(-0.82f, 0.42f, 0.82f, 0.42f);
    glColor4f(0.20f, 1.0f, 0.25f, 1.0f);
    glLineWidth(4.0f);
    draw_line(-0.82f, 0.02f, 0.82f, 0.02f);
    glColor4f(0.25f, 0.55f, 1.0f, 1.0f);
    glLineWidth(8.0f);
    draw_line(-0.82f, -0.42f, 0.82f, -0.42f);
    glLineWidth(1.0f);
}

static void draw_polygon_mode_screen(void) {
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0f);
    glColor4f(1.0f, 0.85f, 0.10f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-0.70f, -0.55f, 0.0f);
    glVertex3f(0.70f, -0.55f, 0.0f);
    glVertex3f(0.50f, 0.55f, 0.0f);
    glVertex3f(-0.50f, 0.55f, 0.0f);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
}

static void draw_readpixels_screen(int width, int height) {
    unsigned char px[4] = { 1, 2, 3, 4 };

    glColor4f(1.0f, 0.05f, 0.05f, 1.0f);
    draw_quad(-0.82f, -0.35f, -0.35f, 0.35f);
    glColor4f(0.05f, 0.95f, 0.15f, 1.0f);
    draw_quad(-0.35f, -0.35f, 0.12f, 0.35f);
    glColor4f(0.15f, 0.35f, 1.0f, 1.0f);
    draw_quad(0.12f, -0.35f, 0.58f, 0.35f);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glColor4f(1.0f, 0.90f, 0.10f, 1.0f);
    draw_quad(0.60f, -0.42f, 0.88f, 0.42f);
    glColor4f((GLfloat)px[0] / 255.0f,
              (GLfloat)px[1] / 255.0f,
              (GLfloat)px[2] / 255.0f,
              1.0f);
    draw_quad(0.64f, -0.34f, 0.84f, 0.34f);
}

static void draw_scene(HWND hwnd) {
    int width;
    int height;
    int page = current_page();

    update_title(hwnd, page);
    setup_2d(hwnd, &width, &height);
    reset_state();

    glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (page == 0) {
        draw_blend_screen();
    } else if (page == 1) {
        draw_alpha_screen();
    } else if (page == 2) {
        draw_color_mask_screen();
    } else if (page == 3) {
        draw_scissor_screen(width, height);
    } else if (page == 4) {
        draw_line_width_screen();
    } else if (page == 5) {
        draw_polygon_mode_screen();
    } else {
        draw_readpixels_screen(width, height);
    }

    reset_state();
    wglSwapLayerBuffers(g_dc, WGL_SWAP_MAIN_PLANE);
}

static void init_gl_state(void) {
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    init_texture();
    reset_state();
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
        draw_scene(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, V86GL_TIMER_ID);
        if (g_alpha_texture) {
            glDeleteTextures(1, &g_alpha_texture);
            g_alpha_texture = 0;
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
    wc.lpszClassName = "V86GLBlendUI";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hwnd = CreateWindowA("V86GLBlendUI", "v86 OpenGL UI state",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        720, 540, NULL, NULL, hinst, NULL);
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
