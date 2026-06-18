// Tiny Win32 OpenGL client-array test for simple_v86_opengl_wrapper.
// Build:
//   i686-w64-mingw32-gcc -mwindows -Os -s -nostdlib -Wl,--subsystem,windows:5.01 -Wl,-e,_WinMainCRTStartup@0 -o gl_client_arrays_test.exe gl_client_arrays_test.c -lopengl32 -lgdi32 -luser32 -lkernel32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
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
#ifndef GL_T2F_C4UB_V3F
#define GL_T2F_C4UB_V3F 0x2A29
#endif

#define V86GL_TIMER_ID 1
#define TEST_TEXTURE_SIZE 32

typedef struct {
    GLfloat s;
    GLfloat t;
    GLubyte r;
    GLubyte g;
    GLubyte b;
    GLubyte a;
    GLfloat x;
    GLfloat y;
    GLfloat z;
} T2F_C4UB_V3F_Vertex;

static HGLRC g_rc = NULL;
static HDC g_dc = NULL;
static GLuint g_texture = 0;
static int g_last_page = -1;
static unsigned char g_texture_pixels[TEST_TEXTURE_SIZE * TEST_TEXTURE_SIZE * 4];

static GLfloat g_triangle_vertices[] = {
    -0.85f, -0.70f, 0.0f,
     0.85f, -0.70f, 0.0f,
     0.00f,  0.78f, 0.0f,
};

static GLfloat g_triangle_colors[] = {
    1.00f, 0.10f, 0.10f, 1.0f,
    0.10f, 0.95f, 0.25f, 1.0f,
    0.15f, 0.35f, 1.00f, 1.0f,
};

static GLfloat g_quad_vertices[] = {
    -0.78f, -0.62f, 0.0f,
     0.78f, -0.62f, 0.0f,
     0.78f,  0.62f, 0.0f,
    -0.78f,  0.62f, 0.0f,
};

static GLfloat g_quad_texcoords[] = {
    0.0f, 0.0f,
    3.0f, 0.0f,
    3.0f, 3.0f,
    0.0f, 3.0f,
};

static GLfloat g_quad_normals[] = {
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 1.0f,
};

static GLushort g_quad_indices[] = {
    0, 1, 2,
    0, 2, 3,
};

static T2F_C4UB_V3F_Vertex g_interleaved_vertices[] = {
    { 0.0f, 0.0f, 255,  80,  60, 255, -0.82f, -0.65f, 0.0f },
    { 2.0f, 0.0f,  80, 255,  90, 255,  0.82f, -0.65f, 0.0f },
    { 2.0f, 2.0f,  80, 120, 255, 255,  0.82f,  0.65f, 0.0f },
    { 0.0f, 0.0f, 255,  80,  60, 255, -0.82f, -0.65f, 0.0f },
    { 2.0f, 2.0f,  80, 120, 255, 255,  0.82f,  0.65f, 0.0f },
    { 0.0f, 2.0f, 255, 230,  70, 255, -0.82f,  0.65f, 0.0f },
};

static void build_texture(void) {
    int x;
    int y;

    for (y = 0; y < TEST_TEXTURE_SIZE; y++) {
        for (x = 0; x < TEST_TEXTURE_SIZE; x++) {
            int checker = ((x >> 2) ^ (y >> 2)) & 1;
            int i = (y * TEST_TEXTURE_SIZE + x) * 4;
            g_texture_pixels[i + 0] = checker ? 250 : 30;
            g_texture_pixels[i + 1] = checker ? 250 : 130;
            g_texture_pixels[i + 2] = checker ? 250 : 210;
            g_texture_pixels[i + 3] = 255;
        }
    }
}

static void init_texture(void) {
    build_texture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEST_TEXTURE_SIZE, TEST_TEXTURE_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, g_texture_pixels);
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
}

static int current_page(void) {
    return (int)((GetTickCount() / 3000u) % 3u);
}

static void update_title(HWND hwnd, int page) {
    if (page == g_last_page) {
        return;
    }

    g_last_page = page;
    if (page == 0) {
        SetWindowTextA(hwnd, "v86 OpenGL client arrays: 1 glDrawArrays + color pointer");
    } else if (page == 1) {
        SetWindowTextA(hwnd, "v86 OpenGL client arrays: 2 glDrawElements + texcoord pointer");
    } else {
        SetWindowTextA(hwnd, "v86 OpenGL client arrays: 3 glInterleavedArrays T2F_C4UB_V3F");
    }
}

static void reset_client_arrays(void) {
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_arrays_screen(void) {
    glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, g_triangle_vertices);
    glColorPointer(4, GL_FLOAT, 0, g_triangle_colors);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    reset_client_arrays();
}

static void draw_elements_screen(void) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, g_quad_vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, g_quad_texcoords);
    glNormalPointer(GL_FLOAT, 0, g_quad_normals);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, g_quad_indices);
    reset_client_arrays();
}

static void draw_interleaved_screen(void) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glInterleavedArrays(GL_T2F_C4UB_V3F, 0, g_interleaved_vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    reset_client_arrays();
}

static void draw_scene(HWND hwnd) {
    int page = current_page();

    update_title(hwnd, page);
    setup_2d(hwnd);

    if (page == 0) {
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
    } else if (page == 1) {
        glClearColor(0.03f, 0.07f, 0.06f, 1.0f);
    } else {
        glClearColor(0.07f, 0.05f, 0.03f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    if (page == 0) {
        draw_arrays_screen();
    } else if (page == 1) {
        draw_elements_screen();
    } else {
        draw_interleaved_screen();
    }

    wglSwapLayerBuffers(g_dc, WGL_SWAP_MAIN_PLANE);
}

static void init_gl_state(void) {
    glDisable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    init_texture();
    reset_client_arrays();
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
        if (g_texture) {
            glDeleteTextures(1, &g_texture);
            g_texture = 0;
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
    wc.lpszClassName = "V86GLClientArrays";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hwnd = CreateWindowA("V86GLClientArrays", "v86 OpenGL client arrays",
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
