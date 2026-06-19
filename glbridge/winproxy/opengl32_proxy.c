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
typedef signed char GLbyte;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef unsigned int GLuint;
typedef int GLint;
typedef short GLshort;
typedef unsigned short GLushort;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;

#define GL_FALSE              0
#define GL_TRUE               1
#define GL_ZERO               0
#define GL_ONE                1
#define GL_NONE               0
#define GL_VENDOR             0x1F00
#define GL_RENDERER           0x1F01
#define GL_VERSION            0x1F02
#define GL_EXTENSIONS         0x1F03
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_NEVER              0x0200
#define GL_LESS               0x0201
#define GL_EQUAL              0x0202
#define GL_LEQUAL             0x0203
#define GL_GREATER            0x0204
#define GL_NOTEQUAL           0x0205
#define GL_GEQUAL             0x0206
#define GL_ALWAYS             0x0207
#define GL_POINTS             0x0000
#define GL_LINES              0x0001
#define GL_TRIANGLES          0x0004
#define GL_TRIANGLE_STRIP     0x0005
#define GL_TRIANGLE_FAN       0x0006
#define GL_QUADS              0x0007
#define GL_FRONT              0x0404
#define GL_BACK               0x0405
#define GL_FRONT_AND_BACK     0x0408
#define GL_CW                 0x0900
#define GL_CCW                0x0901
#define GL_CURRENT_COLOR      0x0B00
#define GL_CURRENT_INDEX      0x0B01
#define GL_CURRENT_NORMAL     0x0B02
#define GL_CURRENT_TEXTURE_COORDS 0x0B03
#define GL_POINT_SMOOTH       0x0B10
#define GL_POINT_SIZE         0x0B11
#define GL_POINT_SIZE_RANGE   0x0B12
#define GL_POINT_SIZE_GRANULARITY 0x0B13
#define GL_LINE_SMOOTH        0x0B20
#define GL_LINE_WIDTH         0x0B21
#define GL_LINE_WIDTH_RANGE   0x0B22
#define GL_LINE_STIPPLE       0x0B24
#define GL_LINE_STIPPLE_PATTERN 0x0B25
#define GL_LINE_STIPPLE_REPEAT 0x0B26
#define GL_LIST_BASE          0x0B32
#define GL_POLYGON_MODE       0x0B40
#define GL_CULL_FACE          0x0B44
#define GL_CULL_FACE_MODE     0x0B45
#define GL_FRONT_FACE         0x0B46
#define GL_LIGHTING           0x0B50
#define GL_SHADE_MODEL        0x0B54
#define GL_FOG                0x0B60
#define GL_FOG_INDEX          0x0B61
#define GL_FOG_DENSITY        0x0B62
#define GL_FOG_START          0x0B63
#define GL_FOG_END            0x0B64
#define GL_FOG_MODE           0x0B65
#define GL_FOG_COLOR          0x0B66
#define GL_DEPTH_RANGE        0x0B70
#define GL_DEPTH_TEST         0x0B71
#define GL_DEPTH_WRITEMASK    0x0B72
#define GL_DEPTH_CLEAR_VALUE  0x0B73
#define GL_DEPTH_FUNC         0x0B74
#define GL_ACCUM_CLEAR_VALUE  0x0B80
#define GL_STENCIL_TEST       0x0B90
#define GL_STENCIL_CLEAR_VALUE 0x0B91
#define GL_STENCIL_FUNC       0x0B92
#define GL_STENCIL_VALUE_MASK 0x0B93
#define GL_STENCIL_FAIL       0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL 0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS 0x0B96
#define GL_STENCIL_REF        0x0B97
#define GL_STENCIL_WRITEMASK  0x0B98
#define GL_ALPHA_TEST         0x0BC0
#define GL_ALPHA_TEST_FUNC    0x0BC1
#define GL_ALPHA_TEST_REF     0x0BC2
#define GL_DITHER             0x0BD0
#define GL_BLEND_DST          0x0BE0
#define GL_BLEND_SRC          0x0BE1
#define GL_BLEND              0x0BE2
#define GL_LOGIC_OP           0x0BF1
#define GL_INDEX_LOGIC_OP     0x0BF1
#define GL_COLOR_LOGIC_OP     0x0BF2
#define GL_SCISSOR_BOX        0x0C10
#define GL_SCISSOR_TEST       0x0C11
#define GL_INDEX_CLEAR_VALUE  0x0C20
#define GL_INDEX_WRITEMASK    0x0C21
#define GL_COLOR_CLEAR_VALUE  0x0C22
#define GL_COLOR_WRITEMASK    0x0C23
#define GL_DOUBLEBUFFER       0x0C32
#define GL_STEREO             0x0C33
#define GL_RENDER_MODE        0x0C40
#define GL_RGBA_MODE          0x0C31
#define GL_FEEDBACK_BUFFER_POINTER 0x0DF0
#define GL_FEEDBACK_BUFFER_SIZE 0x0DF1
#define GL_FEEDBACK_BUFFER_TYPE 0x0DF2
#define GL_SELECTION_BUFFER_POINTER 0x0DF3
#define GL_SELECTION_BUFFER_SIZE 0x0DF4
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_POINT_SMOOTH_HINT  0x0C51
#define GL_LINE_SMOOTH_HINT   0x0C52
#define GL_POLYGON_SMOOTH_HINT 0x0C53
#define GL_FOG_HINT           0x0C54
#define GL_TEXTURE_GEN_S      0x0C60
#define GL_TEXTURE_GEN_T      0x0C61
#define GL_TEXTURE_GEN_R      0x0C62
#define GL_TEXTURE_GEN_Q      0x0C63
#define GL_MAX_LIGHTS         0x0D31
#define GL_MAX_EVAL_ORDER     0x0D30
#define GL_MAX_CLIP_PLANES    0x0D32
#define GL_MAX_TEXTURE_SIZE   0x0D33
#define GL_MAX_ATTRIB_STACK_DEPTH 0x0D35
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH 0x0D39
#define GL_MAX_VIEWPORT_DIMS  0x0D3A
#define GL_MAX_CLIENT_ATTRIB_STACK_DEPTH 0x0D3B
#define GL_SUBPIXEL_BITS      0x0D50
#define GL_RED_BITS           0x0D52
#define GL_GREEN_BITS         0x0D53
#define GL_BLUE_BITS          0x0D54
#define GL_ALPHA_BITS         0x0D55
#define GL_DEPTH_BITS         0x0D56
#define GL_STENCIL_BITS       0x0D57
#define GL_MATRIX_MODE        0x0BA0
#define GL_VIEWPORT           0x0BA2
#define GL_MODELVIEW_MATRIX   0x0BA6
#define GL_PROJECTION_MATRIX  0x0BA7
#define GL_TEXTURE_MATRIX     0x0BA8
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3
#define GL_PROJECTION_STACK_DEPTH 0x0BA4
#define GL_TEXTURE_STACK_DEPTH 0x0BA5
#define GL_ATTRIB_STACK_DEPTH 0x0BB0
#define GL_CLIENT_ATTRIB_STACK_DEPTH 0x0BB1
#define GL_UNPACK_ALIGNMENT   0x0CF5
#define GL_PACK_ALIGNMENT     0x0D05
#define GL_TEXTURE_1D         0x0DE0
#define GL_TEXTURE_2D         0x0DE1
#define GL_TEXTURE_BINDING_1D 0x8068
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_VERTEX_ARRAY_SIZE  0x807A
#define GL_VERTEX_ARRAY_TYPE  0x807B
#define GL_VERTEX_ARRAY_STRIDE 0x807C
#define GL_VERTEX_ARRAY_POINTER 0x808E
#define GL_NORMAL_ARRAY_TYPE  0x807E
#define GL_NORMAL_ARRAY_STRIDE 0x807F
#define GL_NORMAL_ARRAY_POINTER 0x808F
#define GL_COLOR_ARRAY_SIZE   0x8081
#define GL_COLOR_ARRAY_TYPE   0x8082
#define GL_COLOR_ARRAY_STRIDE 0x8083
#define GL_COLOR_ARRAY_POINTER 0x8090
#define GL_INDEX_ARRAY        0x8077
#define GL_INDEX_ARRAY_TYPE   0x8085
#define GL_INDEX_ARRAY_STRIDE 0x8086
#define GL_INDEX_ARRAY_POINTER 0x8091
#define GL_TEXTURE_COORD_ARRAY_SIZE 0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE 0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE 0x808A
#define GL_TEXTURE_COORD_ARRAY_POINTER 0x8092
#define GL_EDGE_FLAG_ARRAY    0x8079
#define GL_EDGE_FLAG_ARRAY_STRIDE 0x808C
#define GL_EDGE_FLAG_ARRAY_POINTER 0x8093
#define GL_ALIASED_LINE_WIDTH_RANGE 0x846E
#define GL_ACTIVE_TEXTURE_ARB 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB 0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#define GL_TEXTURE0_ARB       0x84C0
#define GL_TEXTURE1_ARB       0x84C1
#define GL_TEXTURE2_ARB       0x84C2
#define GL_TEXTURE3_ARB       0x84C3
#define GL_TEXTURE4_ARB       0x84C4
#define GL_TEXTURE5_ARB       0x84C5
#define GL_TEXTURE6_ARB       0x84C6
#define GL_TEXTURE7_ARB       0x84C7
#define GL_COMBINE_ARB        0x8570
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_EXP                0x0800
#define GL_EXP2               0x0801
#define GL_KEEP               0x1E00
#define GL_REPLACE            0x1E01
#define GL_INCR               0x1E02
#define GL_DECR               0x1E03
#define GL_AMBIENT            0x1200
#define GL_DIFFUSE            0x1201
#define GL_SPECULAR           0x1202
#define GL_POSITION           0x1203
#define GL_SPOT_DIRECTION     0x1204
#define GL_SPOT_EXPONENT      0x1205
#define GL_SPOT_CUTOFF        0x1206
#define GL_CONSTANT_ATTENUATION 0x1207
#define GL_LINEAR_ATTENUATION 0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209
#define GL_EMISSION           0x1600
#define GL_SHININESS          0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_COLOR_INDEXES      0x1603
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GL_LIGHT0             0x4000
#define GL_LIGHT1             0x4001
#define GL_LIGHT2             0x4002
#define GL_LIGHT3             0x4003
#define GL_LIGHT4             0x4004
#define GL_LIGHT5             0x4005
#define GL_LIGHT6             0x4006
#define GL_LIGHT7             0x4007
#define GL_MODULATE           0x2100
#define GL_DECAL              0x2101
#define GL_S                  0x2000
#define GL_T                  0x2001
#define GL_R                  0x2002
#define GL_Q                  0x2003
#define GL_OBJECT_LINEAR      0x2401
#define GL_EYE_LINEAR         0x2400
#define GL_SPHERE_MAP         0x2402
#define GL_TEXTURE_GEN_MODE   0x2500
#define GL_OBJECT_PLANE       0x2501
#define GL_EYE_PLANE          0x2502
#define GL_TEXTURE_ENV        0x2300
#define GL_TEXTURE_ENV_MODE   0x2200
#define GL_TEXTURE_ENV_COLOR  0x2201
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_REPEAT             0x2901
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_FLAT               0x1D00
#define GL_SMOOTH             0x1D01
#define GL_RENDER             0x1C00
#define GL_FEEDBACK           0x1C01
#define GL_SELECT             0x1C02
#define GL_DONT_CARE          0x1100
#define GL_FASTEST            0x1101
#define GL_NICEST             0x1102
#define GL_POINT              0x1B00
#define GL_LINE               0x1B01
#define GL_FILL               0x1B02
#define GL_MODELVIEW          0x1700
#define GL_PROJECTION         0x1701
#define GL_TEXTURE            0x1702
#define GL_TEXTURE_PRIORITY   0x8066
#define GL_TEXTURE_RESIDENT   0x8067
#define GL_TEXTURE_MIN_LOD    0x813A
#define GL_TEXTURE_MAX_LOD    0x813B
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL  0x813D
#define GL_COLOR_MATERIAL     0x0B57
#define GL_COLOR_MATERIAL_FACE 0x0B55
#define GL_COLOR_MATERIAL_PARAMETER 0x0B56
#define GL_CLIP_PLANE0        0x3000
#define GL_CLIP_PLANE1        0x3001
#define GL_CLIP_PLANE2        0x3002
#define GL_CLIP_PLANE3        0x3003
#define GL_CLIP_PLANE4        0x3004
#define GL_CLIP_PLANE5        0x3005
#define GL_BYTE               0x1400
#define GL_UNSIGNED_BYTE      0x1401
#define GL_SHORT              0x1402
#define GL_UNSIGNED_SHORT     0x1403
#define GL_INT                0x1404
#define GL_UNSIGNED_INT       0x1405
#define GL_FLOAT              0x1406
#define GL_DOUBLE             0x140A
#define GL_RED                0x1903
#define GL_GREEN              0x1904
#define GL_BLUE               0x1905
#define GL_ALPHA              0x1906
#define GL_RGB                0x1907
#define GL_RGBA               0x1908
#define GL_LUMINANCE          0x1909
#define GL_LUMINANCE_ALPHA    0x190A
#define GL_PACK_SWAP_BYTES    0x0D00
#define GL_PACK_LSB_FIRST     0x0D01
#define GL_PACK_ROW_LENGTH    0x0D02
#define GL_PACK_SKIP_ROWS     0x0D03
#define GL_PACK_SKIP_PIXELS   0x0D04
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
#define GL_INVALID_ENUM       0x0500
#define GL_INVALID_VALUE      0x0501
#define GL_INVALID_OPERATION  0x0502
#define GL_OUT_OF_MEMORY      0x0505
#define GL_VERTEX_ARRAY       0x8074
#define GL_NORMAL_ARRAY       0x8075
#define GL_COLOR_ARRAY        0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_V2F                0x2A20
#define GL_V3F                0x2A21
#define GL_C4UB_V2F           0x2A22
#define GL_C4UB_V3F           0x2A23
#define GL_C3F_V3F            0x2A24
#define GL_N3F_V3F            0x2A25
#define GL_C4F_N3F_V3F        0x2A26
#define GL_T2F_V3F            0x2A27
#define GL_T4F_V4F            0x2A28
#define GL_T2F_C4UB_V3F       0x2A29
#define GL_T2F_C3F_V3F        0x2A2A
#define GL_T2F_N3F_V3F        0x2A2B
#define GL_T2F_C4F_N3F_V3F    0x2A2C
#define GL_T4F_C4F_N3F_V4F    0x2A2D

#define VGL_MAGIC 0x314C4756u  // 'VGL1' little-endian
#define CLIENT_ARRAY_MT_MAGIC 0x544D4143u  // 'CAMT' little-endian

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
    GLFN_ENABLE_CLIENT_STATE = 37,
    GLFN_DISABLE_CLIENT_STATE = 38,
    GLFN_DRAW_ARRAYS = 39,
    GLFN_DRAW_ELEMENTS = 40,
    GLFN_BLEND_FUNC = 41,
    GLFN_ALPHA_FUNC = 42,
    GLFN_DEPTH_MASK = 43,
    GLFN_COLOR_MASK = 44,
    GLFN_SCISSOR = 45,
    GLFN_LINE_WIDTH = 46,
    GLFN_POLYGON_MODE = 47,
    GLFN_ACTIVE_TEXTURE = 48,
    GLFN_CLIENT_ACTIVE_TEXTURE = 49,
    GLFN_MULTI_TEX_COORD4F = 50,
    GLFN_NORMAL3F = 51,
    GLFN_FOGF = 52,
    GLFN_FOGI = 53,
    GLFN_FOGFV = 54,
    GLFN_MATERIALF = 55,
    GLFN_MATERIALI = 56,
    GLFN_MATERIALFV = 57,
    GLFN_MATERIALIV = 58,
    GLFN_TEX_ENVIV = 59,
    GLFN_TEX_ENVFV = 60,
    GLFN_LIGHTF = 61,
    GLFN_LIGHTI = 62,
    GLFN_LIGHTFV = 63,
    GLFN_LIGHTIV = 64,
    GLFN_LIGHT_MODELF = 65,
    GLFN_LIGHT_MODELI = 66,
    GLFN_LIGHT_MODELFV = 67,
    GLFN_LIGHT_MODELIV = 68,
    GLFN_LOAD_MATRIXF = 69,
    GLFN_MULT_MATRIXF = 70,
    GLFN_DEPTH_RANGE = 71,
    GLFN_CLEAR_STENCIL = 72,
    GLFN_STENCIL_FUNC = 73,
    GLFN_STENCIL_MASK = 74,
    GLFN_STENCIL_OP = 75,
    GLFN_HINT = 76,
    GLFN_POLYGON_OFFSET = 77,
    GLFN_TEX_PARAMETERIV = 78,
    GLFN_TEX_PARAMETERFV = 79,
    GLFN_TEX_GENI = 80,
    GLFN_TEX_GENF = 81,
    GLFN_TEX_GENIV = 82,
    GLFN_TEX_GENFV = 83,
    GLFN_CLIP_PLANE = 84,
    GLFN_COLOR_MATERIAL = 85,
    GLFN_PUSH_ATTRIB = 86,
    GLFN_POP_ATTRIB = 87,
    GLFN_PUSH_CLIENT_ATTRIB = 88,
    GLFN_POP_CLIENT_ATTRIB = 89,
    GLFN_DRAW_BUFFER = 90,
    GLFN_READ_BUFFER = 91,
    GLFN_COPY_TEX_IMAGE_2D = 92,
    GLFN_COPY_TEX_SUB_IMAGE_2D = 93,
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
static HINSTANCE g_instance = NULL;
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
static GLuint g_next_list_id = 1;
static GLuint g_list_base = 0;
static BOOL g_have_viewport = FALSE;
static GLint g_viewport[4] = { 0, 0, 0, 0 };
static GLfloat g_clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static GLfloat g_clear_accum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static GLfloat g_clear_index = 0.0f;
static GLfloat g_current_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static GLfloat g_current_index = 1.0f;
static GLfloat g_current_texcoord[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
static GLfloat g_current_normal[3] = { 0.0f, 0.0f, 1.0f };
static GLboolean g_current_edge_flag = GL_TRUE;
static GLenum g_matrix_mode = GL_MODELVIEW;
static GLenum g_depth_func = GL_LESS;
static GLdouble g_clear_depth = 1.0;
static GLdouble g_depth_range[2] = { 0.0, 1.0 };
static GLint g_clear_stencil = 0;
static GLenum g_stencil_func = GL_ALWAYS;
static GLint g_stencil_ref = 0;
static GLuint g_stencil_value_mask = 0xFFFFFFFFu;
static GLuint g_stencil_write_mask = 0xFFFFFFFFu;
static GLenum g_stencil_fail = GL_KEEP;
static GLenum g_stencil_zfail = GL_KEEP;
static GLenum g_stencil_zpass = GL_KEEP;
static GLenum g_shade_model = GL_SMOOTH;
static GLenum g_cull_face_mode = GL_BACK;
static GLenum g_front_face_mode = GL_CCW;
static GLenum g_blend_src = GL_ONE;
static GLenum g_blend_dst = GL_ZERO;
static GLenum g_alpha_func = GL_ALWAYS;
static GLfloat g_alpha_ref = 0.0f;
static GLboolean g_depth_mask = GL_TRUE;
static GLboolean g_color_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
static GLint g_scissor_box[4] = { 0, 0, 0, 0 };
static GLfloat g_point_size = 1.0f;
static GLfloat g_line_width = 1.0f;
static GLint g_line_stipple_factor = 1;
static GLushort g_line_stipple_pattern = 0xFFFFu;
static GLenum g_polygon_mode_front = GL_FILL;
static GLenum g_polygon_mode_back = GL_FILL;
static GLfloat g_polygon_offset_factor = 0.0f;
static GLfloat g_polygon_offset_units = 0.0f;
static GLenum g_color_material_face = GL_FRONT_AND_BACK;
static GLenum g_color_material_param = GL_AMBIENT_AND_DIFFUSE;
static GLenum g_active_texture = GL_TEXTURE0_ARB;
static GLenum g_client_active_texture = GL_TEXTURE0_ARB;
static GLint g_unpack_alignment = 4;
static GLint g_unpack_row_length = 0;
static GLint g_unpack_skip_rows = 0;
static GLint g_unpack_skip_pixels = 0;
static GLint g_pack_alignment = 4;
static GLint g_pack_row_length = 0;
static GLint g_pack_skip_rows = 0;
static GLint g_pack_skip_pixels = 0;
static GLenum g_render_mode = GL_RENDER;
static GLsizei g_feedback_buffer_size = 0;
static GLenum g_feedback_buffer_type = 0;
static GLfloat* g_feedback_buffer = NULL;
static GLsizei g_selection_buffer_size = 0;
static GLuint* g_selection_buffer = NULL;
static GLuint g_name_stack[64];
static GLint g_name_stack_depth = 0;
static GLuint g_index_mask = 0xFFFFFFFFu;
static GLenum g_error = 0;
static const char* g_gl_extensions =
    "GL_ARB_multitexture "
    "GL_ARB_texture_env_combine "
    "GL_EXT_bgra "
    "GL_EXT_compiled_vertex_array "
    "GL_EXT_texture_env_add "
    "GL_EXT_texture_env_combine "
    "GL_EXT_texture_filter_anisotropic "
    "GL_EXT_texture_lod_bias "
    "GL_EXT_texture_object";
static const char* g_wgl_extensions =
    "WGL_ARB_extensions_string "
    "WGL_EXT_extensions_string "
    "WGL_EXT_swap_control";
static int g_swap_interval = 0;

#define V86GL_MAX_TEXTURE_UNITS 8
#define V86GL_MAX_TEXTURE_STATES 4096
#define V86GL_MAX_CAP_STATES 128

typedef struct {
    GLuint bound_2d;
} TextureUnitState;

typedef struct {
    BOOL used;
    GLuint name;
    GLenum target;
    GLint min_filter;
    GLint mag_filter;
    GLint wrap_s;
    GLint wrap_t;
    GLint base_level;
    GLint max_level;
    GLfloat min_lod;
    GLfloat max_lod;
    GLfloat priority;
    GLfloat border_color[4];
} TextureObjectState;

typedef struct {
    GLenum cap;
    BOOL enabled;
} CapState;

static TextureUnitState g_texture_units[V86GL_MAX_TEXTURE_UNITS];
static TextureObjectState g_texture_states[V86GL_MAX_TEXTURE_STATES];
static TextureObjectState g_default_texture_2d = {
    TRUE,
    0,
    GL_TEXTURE_2D,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR,
    GL_REPEAT,
    GL_REPEAT,
    0,
    1000,
    -1000.0f,
    1000.0f,
    1.0f,
    { 0.0f, 0.0f, 0.0f, 0.0f }
};
static CapState g_cap_states[V86GL_MAX_CAP_STATES];

typedef struct {
    BOOL enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    const GLvoid* pointer;
} ClientArrayState;

typedef struct {
    uint32_t enabled;
    int32_t size;
    uint32_t type;
    int32_t stride;
    uint32_t data_size;
} ClientArrayBlockHeader;

typedef struct {
    BOOL enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    uint32_t data_size;
    const uint8_t* data;
} ClientArrayCopy;

static ClientArrayState g_vertex_array = { FALSE, 3, GL_FLOAT, 0, NULL };
static ClientArrayState g_color_array = { FALSE, 4, GL_FLOAT, 0, NULL };
static ClientArrayState g_index_array = { FALSE, 1, GL_FLOAT, 0, NULL };
static ClientArrayState g_texcoord_arrays[V86GL_MAX_TEXTURE_UNITS] = {
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
    { FALSE, 2, GL_FLOAT, 0, NULL },
};
static ClientArrayState g_normal_array = { FALSE, 3, GL_FLOAT, 0, NULL };
static ClientArrayState g_edge_flag_array = { FALSE, 1, GL_UNSIGNED_BYTE, 0, NULL };

static uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}

static GLint active_texture_index(void) {
    if (g_active_texture < GL_TEXTURE0_ARB ||
        g_active_texture >= GL_TEXTURE0_ARB + V86GL_MAX_TEXTURE_UNITS) {
        return 0;
    }

    return (GLint)(g_active_texture - GL_TEXTURE0_ARB);
}

static GLint client_active_texture_index(void) {
    if (g_client_active_texture < GL_TEXTURE0_ARB ||
        g_client_active_texture >= GL_TEXTURE0_ARB + V86GL_MAX_TEXTURE_UNITS) {
        return 0;
    }

    return (GLint)(g_client_active_texture - GL_TEXTURE0_ARB);
}

static BOOL default_cap_enabled(GLenum cap) {
    return cap == GL_DITHER;
}

static void set_cap_state(GLenum cap, BOOL enabled) {
    uint32_t i;
    uint32_t free_slot = V86GL_MAX_CAP_STATES;

    for (i = 0; i < V86GL_MAX_CAP_STATES; i++) {
        if (g_cap_states[i].cap == cap) {
            g_cap_states[i].enabled = enabled;
            return;
        }

        if (!g_cap_states[i].cap && free_slot == V86GL_MAX_CAP_STATES) {
            free_slot = i;
        }
    }

    if (free_slot < V86GL_MAX_CAP_STATES) {
        g_cap_states[free_slot].cap = cap;
        g_cap_states[free_slot].enabled = enabled;
    }
}

static BOOL get_cap_state(GLenum cap) {
    uint32_t i;

    for (i = 0; i < V86GL_MAX_CAP_STATES; i++) {
        if (g_cap_states[i].cap == cap) {
            return g_cap_states[i].enabled;
        }
    }

    return default_cap_enabled(cap);
}

static TextureObjectState* texture_state_defaults(TextureObjectState* state, GLuint name, GLenum target) {
    ZeroMemory(state, sizeof(*state));
    state->used = TRUE;
    state->name = name;
    state->target = target;
    state->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    state->mag_filter = GL_LINEAR;
    state->wrap_s = GL_REPEAT;
    state->wrap_t = GL_REPEAT;
    state->base_level = 0;
    state->max_level = 1000;
    state->min_lod = -1000.0f;
    state->max_lod = 1000.0f;
    state->priority = 1.0f;
    return state;
}

static TextureObjectState* find_texture_state(GLuint name, GLenum target, BOOL create) {
    uint32_t i;
    TextureObjectState* free_state = NULL;

    if (!name) {
        return target == GL_TEXTURE_2D ? &g_default_texture_2d : NULL;
    }

    for (i = 0; i < V86GL_MAX_TEXTURE_STATES; i++) {
        TextureObjectState* state = &g_texture_states[i];
        if (state->used && state->name == name) {
            if (state->target && state->target != target) {
                g_error = GL_INVALID_OPERATION;
                return NULL;
            }

            if (!state->target) {
                state->target = target;
            }

            return state;
        }

        if (!state->used && !free_state) {
            free_state = state;
        }
    }

    if (!create || !free_state) {
        return NULL;
    }

    return texture_state_defaults(free_state, name, target);
}

static void delete_texture_state(GLuint name) {
    uint32_t i;
    uint32_t unit;

    if (!name) {
        return;
    }

    for (i = 0; i < V86GL_MAX_TEXTURE_STATES; i++) {
        if (g_texture_states[i].used && g_texture_states[i].name == name) {
            ZeroMemory(&g_texture_states[i], sizeof(g_texture_states[i]));
            break;
        }
    }

    for (unit = 0; unit < V86GL_MAX_TEXTURE_UNITS; unit++) {
        if (g_texture_units[unit].bound_2d == name) {
            g_texture_units[unit].bound_2d = 0;
        }
    }
}

static TextureObjectState* bound_texture_state(GLenum target, BOOL create) {
    GLint unit = active_texture_index();

    if (target != GL_TEXTURE_2D) {
        g_error = GL_INVALID_ENUM;
        return NULL;
    }

    return find_texture_state(g_texture_units[unit].bound_2d, target, create);
}

static void update_texture_parameter_i(GLenum target, GLenum pname, GLint param) {
    TextureObjectState* state = bound_texture_state(target, TRUE);

    if (!state) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        state->min_filter = param;
        break;
    case GL_TEXTURE_MAG_FILTER:
        state->mag_filter = param;
        break;
    case GL_TEXTURE_WRAP_S:
        state->wrap_s = param;
        break;
    case GL_TEXTURE_WRAP_T:
        state->wrap_t = param;
        break;
    case GL_TEXTURE_BASE_LEVEL:
        state->base_level = param;
        break;
    case GL_TEXTURE_MAX_LEVEL:
        state->max_level = param;
        break;
    case GL_TEXTURE_MIN_LOD:
        state->min_lod = (GLfloat)param;
        break;
    case GL_TEXTURE_MAX_LOD:
        state->max_lod = (GLfloat)param;
        break;
    case GL_TEXTURE_PRIORITY:
        state->priority = (GLfloat)param;
        break;
    case GL_TEXTURE_BORDER_COLOR:
        state->border_color[0] = (GLfloat)param;
        state->border_color[1] = (GLfloat)param;
        state->border_color[2] = (GLfloat)param;
        state->border_color[3] = (GLfloat)param;
        break;
    default:
        break;
    }
}

static void update_texture_parameter_f(GLenum target, GLenum pname, GLfloat param) {
    TextureObjectState* state = bound_texture_state(target, TRUE);

    if (!state) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        state->min_filter = (GLint)param;
        break;
    case GL_TEXTURE_MAG_FILTER:
        state->mag_filter = (GLint)param;
        break;
    case GL_TEXTURE_WRAP_S:
        state->wrap_s = (GLint)param;
        break;
    case GL_TEXTURE_WRAP_T:
        state->wrap_t = (GLint)param;
        break;
    case GL_TEXTURE_BASE_LEVEL:
        state->base_level = (GLint)param;
        break;
    case GL_TEXTURE_MAX_LEVEL:
        state->max_level = (GLint)param;
        break;
    case GL_TEXTURE_MIN_LOD:
        state->min_lod = param;
        break;
    case GL_TEXTURE_MAX_LOD:
        state->max_lod = param;
        break;
    case GL_TEXTURE_PRIORITY:
        state->priority = param;
        break;
    case GL_TEXTURE_BORDER_COLOR:
        state->border_color[0] = param;
        state->border_color[1] = param;
        state->border_color[2] = param;
        state->border_color[3] = param;
        break;
    default:
        break;
    }
}

static void current_viewport(GLint* params) {
    if (g_have_viewport) {
        CopyMemory(params, g_viewport, sizeof(g_viewport));
        return;
    }

    params[0] = 0;
    params[1] = 0;
    params[2] = g_have_last_surface ? (GLint)g_last_surface_width : 640;
    params[3] = g_have_last_surface ? (GLint)g_last_surface_height : 480;
}

static void current_scissor_box(GLint* params) {
    if (g_scissor_box[2] > 0 || g_scissor_box[3] > 0) {
        CopyMemory(params, g_scissor_box, sizeof(g_scissor_box));
        return;
    }

    current_viewport(params);
}

static void identity_matrix(GLfloat* params) {
    uint32_t i;

    for (i = 0; i < 16; i++) {
        params[i] = (i % 5) == 0 ? 1.0f : 0.0f;
    }
}

static int fog_value_count(GLenum pname) {
    switch (pname) {
    case GL_FOG_COLOR:
        return 4;
    case GL_FOG_INDEX:
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
    case GL_FOG_MODE:
        return 1;
    default:
        return 0;
    }
}

static int fog_scalar_valid(GLenum pname) {
    switch (pname) {
    case GL_FOG_INDEX:
    case GL_FOG_DENSITY:
    case GL_FOG_START:
    case GL_FOG_END:
    case GL_FOG_MODE:
        return 1;
    default:
        return 0;
    }
}

static int texenv_value_count(GLenum pname) {
    switch (pname) {
    case GL_TEXTURE_ENV_COLOR:
        return 4;
    case GL_TEXTURE_ENV_MODE:
        return 1;
    default:
        return 0;
    }
}

static int texture_parameter_value_count(GLenum pname) {
    return pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
}

static int texgen_coord_valid(GLenum coord) {
    return coord == GL_S || coord == GL_T || coord == GL_R || coord == GL_Q;
}

static int texgen_value_count(GLenum pname) {
    switch (pname) {
    case GL_OBJECT_PLANE:
    case GL_EYE_PLANE:
        return 4;
    case GL_TEXTURE_GEN_MODE:
        return 1;
    default:
        return 0;
    }
}

static int texgen_scalar_valid(GLenum pname) {
    return pname == GL_TEXTURE_GEN_MODE;
}

static int clip_plane_valid(GLenum plane) {
    return plane >= GL_CLIP_PLANE0 && plane <= GL_CLIP_PLANE5;
}

static int get_value_count(GLenum pname) {
    switch (pname) {
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX:
        return 16;
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    case GL_COLOR_WRITEMASK:
    case GL_CURRENT_COLOR:
    case GL_CURRENT_TEXTURE_COORDS:
    case GL_COLOR_CLEAR_VALUE:
    case GL_ACCUM_CLEAR_VALUE:
        return 4;
    case GL_POLYGON_MODE:
    case GL_MAX_VIEWPORT_DIMS:
    case GL_DEPTH_RANGE:
    case GL_LINE_WIDTH_RANGE:
    case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_POINT_SIZE_RANGE:
        return 2;
    default:
        return 1;
    }
}

static int material_face_valid(GLenum face) {
    return face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK;
}

static int material_value_count(GLenum pname) {
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
        return 4;
    case GL_COLOR_INDEXES:
        return 3;
    case GL_SHININESS:
        return 1;
    default:
        return 0;
    }
}

static int material_scalar_valid(GLenum pname) {
    return pname == GL_SHININESS;
}

static int light_name_valid(GLenum light) {
    return light >= GL_LIGHT0 && light <= GL_LIGHT7;
}

static int light_value_count(GLenum pname) {
    switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_POSITION:
        return 4;
    case GL_SPOT_DIRECTION:
        return 3;
    case GL_SPOT_EXPONENT:
    case GL_SPOT_CUTOFF:
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        return 1;
    default:
        return 0;
    }
}

static int light_scalar_valid(GLenum pname) {
    return light_value_count(pname) == 1;
}

static int light_model_value_count(GLenum pname) {
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
    case GL_LIGHT_MODEL_TWO_SIDE:
        return 1;
    default:
        return 0;
    }
}

static int light_model_scalar_valid(GLenum pname) {
    return light_model_value_count(pname) == 1;
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

static uint32_t gl_pixel_span_with_state(GLsizei width, GLsizei height, GLenum format, GLenum type,
                                         GLint alignment, GLint row_length,
                                         GLint skip_rows, GLint skip_pixels) {
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

    row_pixels = row_length > 0 ? (uint32_t)row_length : (uint32_t)width;
    if (row_pixels < (uint32_t)width) {
        row_pixels = (uint32_t)width;
    }

    row_bytes = row_pixels * pixel_bytes;
    row_stride = align_u32(row_bytes, (uint32_t)(alignment > 0 ? alignment : 1));
    skip_bytes = (uint64_t)(skip_rows > 0 ? skip_rows : 0) * row_stride +
                 (uint64_t)(skip_pixels > 0 ? skip_pixels : 0) * pixel_bytes;
    total = skip_bytes +
            (uint64_t)((uint32_t)height - 1u) * row_stride +
            (uint64_t)(uint32_t)width * pixel_bytes;

    if (total > 0xFFFFFFFFu) {
        return 0;
    }

    return (uint32_t)total;
}

static uint32_t gl_pixel_span(GLsizei width, GLsizei height, GLenum format, GLenum type) {
    return gl_pixel_span_with_state(width, height, format, type,
                                    g_unpack_alignment, g_unpack_row_length,
                                    g_unpack_skip_rows, g_unpack_skip_pixels);
}

static uint32_t gl_read_pixel_span(GLsizei width, GLsizei height, GLenum format, GLenum type) {
    return gl_pixel_span_with_state(width, height, format, type,
                                    g_pack_alignment, g_pack_row_length,
                                    g_pack_skip_rows, g_pack_skip_pixels);
}

static uint32_t gl_type_bytes(GLenum type) {
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return 4;
    case GL_DOUBLE:
        return 8;
    default:
        return 0;
    }
}

static uint32_t client_array_element_size(GLint size, GLenum type) {
    uint32_t bytes = gl_type_bytes(type);

    if (size <= 0 || !bytes) {
        return 0;
    }

    return (uint32_t)size * bytes;
}

static int client_array_copy(ClientArrayState* state, GLint first, GLsizei count, ClientArrayCopy* out) {
    uint32_t element_size;
    uint32_t stride;
    uint64_t offset;
    uint64_t span;

    ZeroMemory(out, sizeof(*out));
    if (!state->enabled) {
        return 1;
    }

    if (!state->pointer) {
        g_error = GL_INVALID_OPERATION;
        return 0;
    }

    element_size = client_array_element_size(state->size, state->type);
    if (!element_size) {
        g_error = GL_INVALID_ENUM;
        return 0;
    }

    stride = state->stride > 0 ? (uint32_t)state->stride : element_size;
    offset = (uint64_t)(uint32_t)first * stride;
    span = count > 0 ? (uint64_t)((uint32_t)count - 1u) * stride + element_size : 0;
    if (offset > UINT32_MAX || span > UINT32_MAX) {
        g_error = GL_INVALID_VALUE;
        return 0;
    }

    out->enabled = TRUE;
    out->size = state->size;
    out->type = state->type;
    out->stride = state->stride;
    out->data_size = (uint32_t)span;
    out->data = (const uint8_t*)state->pointer + (uint32_t)offset;
    return 1;
}

static uint32_t client_array_blocks_size(const ClientArrayCopy* arrays, uint32_t count) {
    uint32_t i;
    uint64_t total = 0;

    for (i = 0; i < count; i++) {
        total += sizeof(ClientArrayBlockHeader) + arrays[i].data_size;
        if (total > UINT32_MAX) {
            return 0;
        }
    }

    return (uint32_t)total;
}

static uint8_t* write_client_array_block(uint8_t* p, const ClientArrayCopy* array) {
    ClientArrayBlockHeader h;

    h.enabled = array->enabled ? 1u : 0u;
    h.size = array->enabled ? array->size : 0;
    h.type = array->enabled ? (uint32_t)array->type : 0u;
    h.stride = array->enabled ? array->stride : 0;
    h.data_size = array->enabled ? array->data_size : 0u;
    CopyMemory(p, &h, sizeof(h));
    p += sizeof(h);

    if (h.data_size) {
        CopyMemory(p, array->data, h.data_size);
        p += h.data_size;
    }

    return p;
}

static int set_client_array_enabled(GLenum array, BOOL enabled, BOOL emit) {
    uint32_t payload = (uint32_t)array;
    ClientArrayState* texcoord = &g_texcoord_arrays[client_active_texture_index()];

    switch (array) {
    case GL_VERTEX_ARRAY:
        g_vertex_array.enabled = enabled;
        break;
    case GL_COLOR_ARRAY:
        g_color_array.enabled = enabled;
        break;
    case GL_INDEX_ARRAY:
        g_index_array.enabled = enabled;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        texcoord->enabled = enabled;
        break;
    case GL_NORMAL_ARRAY:
        g_normal_array.enabled = enabled;
        break;
    case GL_EDGE_FLAG_ARRAY:
        g_edge_flag_array.enabled = enabled;
        break;
    default:
        g_error = GL_INVALID_ENUM;
        return 0;
    }

    if (emit) {
        emit_gl_call(enabled ? GLFN_ENABLE_CLIENT_STATE : GLFN_DISABLE_CLIENT_STATE,
                     &payload, sizeof(payload));
    }

    return 1;
}

static int gl_index_bytes(GLenum type) {
    switch (type) {
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_UNSIGNED_INT:
        return 4;
    default:
        return 0;
    }
}

static int max_draw_index(const GLvoid* indices, GLsizei count, GLenum type, uint32_t* max_index) {
    GLsizei i;

    *max_index = 0;
    if (count <= 0) {
        return 1;
    }

    if (!indices) {
        g_error = GL_INVALID_OPERATION;
        return 0;
    }

    switch (type) {
    case GL_UNSIGNED_BYTE: {
        const GLubyte* p = (const GLubyte*)indices;
        for (i = 0; i < count; i++) {
            if (p[i] > *max_index) {
                *max_index = p[i];
            }
        }
        return 1;
    }
    case GL_UNSIGNED_SHORT: {
        const uint16_t* p = (const uint16_t*)indices;
        for (i = 0; i < count; i++) {
            if (p[i] > *max_index) {
                *max_index = p[i];
            }
        }
        return 1;
    }
    case GL_UNSIGNED_INT: {
        const uint32_t* p = (const uint32_t*)indices;
        for (i = 0; i < count; i++) {
            if (p[i] > *max_index) {
                *max_index = p[i];
            }
        }
        return 1;
    }
    default:
        g_error = GL_INVALID_ENUM;
        return 0;
    }
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
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = hinst;
    }

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

static void fill_default_pixel_format(PIXELFORMATDESCRIPTOR* pfd) {
    if (!pfd) {
        return;
    }

    ZeroMemory(pfd, sizeof(*pfd));
    pfd->nSize = sizeof(*pfd);
    pfd->nVersion = 1;
    pfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd->iPixelType = PFD_TYPE_RGBA;
    pfd->cColorBits = 32;
    pfd->cRedBits = 8;
    pfd->cGreenBits = 8;
    pfd->cBlueBits = 8;
    pfd->cAlphaBits = 8;
    pfd->cDepthBits = 24;
    pfd->cStencilBits = 8;
    pfd->iLayerType = PFD_MAIN_PLANE;
}

__declspec(dllexport)
int APIENTRY wglChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR* ppfd) {
    (void)hdc;
    (void)ppfd;
    return 1;
}

__declspec(dllexport)
BOOL APIENTRY wglSetPixelFormat(HDC hdc, int format, const PIXELFORMATDESCRIPTOR* ppfd) {
    (void)hdc;
    (void)format;
    (void)ppfd;
    return TRUE;
}

__declspec(dllexport)
int APIENTRY wglDescribePixelFormat(HDC hdc, int format, UINT bytes, PIXELFORMATDESCRIPTOR* ppfd) {
    (void)hdc;
    (void)format;
    if (ppfd && bytes >= sizeof(*ppfd)) {
        fill_default_pixel_format(ppfd);
    }
    return 1;
}

__declspec(dllexport)
int APIENTRY wglGetPixelFormat(HDC hdc) {
    (void)hdc;
    return 1;
}

__declspec(dllexport)
HGLRC APIENTRY wglCreateLayerContext(HDC hdc, int layer) {
    (void)layer;
    return wglCreateContext(hdc);
}

__declspec(dllexport)
BOOL APIENTRY wglCopyContext(HGLRC src, HGLRC dst, UINT mask) {
    (void)src;
    (void)dst;
    (void)mask;
    return TRUE;
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
DWORD APIENTRY wglSwapMultipleBuffers(UINT count, const WGLSWAP* swaps) {
    (void)swaps;
    if (count) {
        emit_frame();
    }
    return count;
}

__declspec(dllexport)
BOOL APIENTRY wglDescribeLayerPlane(HDC hdc, int format, int plane, UINT bytes,
                                    LPLAYERPLANEDESCRIPTOR lpd) {
    (void)hdc;
    (void)format;
    (void)plane;
    if (lpd && bytes >= sizeof(*lpd)) {
        ZeroMemory(lpd, sizeof(*lpd));
        lpd->nSize = sizeof(*lpd);
        lpd->nVersion = 1;
        lpd->dwFlags = LPD_SUPPORT_OPENGL | LPD_DOUBLEBUFFER;
        lpd->iPixelType = PFD_TYPE_RGBA;
        lpd->cColorBits = 32;
        lpd->cDepthBits = 24;
        lpd->cStencilBits = 8;
    }
    return TRUE;
}

__declspec(dllexport)
int APIENTRY wglSetLayerPaletteEntries(HDC hdc, int layer, int start, int count,
                                       const COLORREF* entries) {
    (void)hdc;
    (void)layer;
    (void)start;
    (void)entries;
    return count < 0 ? 0 : count;
}

__declspec(dllexport)
int APIENTRY wglGetLayerPaletteEntries(HDC hdc, int layer, int start, int count,
                                       COLORREF* entries) {
    int i;
    (void)hdc;
    (void)layer;
    (void)start;
    if (entries && count > 0) {
        for (i = 0; i < count; i++) {
            entries[i] = RGB(0, 0, 0);
        }
    }
    return count < 0 ? 0 : count;
}

__declspec(dllexport)
BOOL APIENTRY wglRealizeLayerPalette(HDC hdc, int layer, BOOL realize) {
    (void)hdc;
    (void)layer;
    (void)realize;
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglUseFontBitmapsA(HDC hdc, DWORD first, DWORD count, DWORD list_base) {
    (void)hdc;
    (void)first;
    (void)count;
    (void)list_base;
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglUseFontBitmapsW(HDC hdc, DWORD first, DWORD count, DWORD list_base) {
    (void)hdc;
    (void)first;
    (void)count;
    (void)list_base;
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglUseFontOutlinesA(HDC hdc, DWORD first, DWORD count, DWORD list_base,
                                  FLOAT deviation, FLOAT extrusion, int format,
                                  LPGLYPHMETRICSFLOAT metrics) {
    (void)hdc;
    (void)first;
    (void)count;
    (void)list_base;
    (void)deviation;
    (void)extrusion;
    (void)format;
    (void)metrics;
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglUseFontOutlinesW(HDC hdc, DWORD first, DWORD count, DWORD list_base,
                                  FLOAT deviation, FLOAT extrusion, int format,
                                  LPGLYPHMETRICSFLOAT metrics) {
    (void)hdc;
    (void)first;
    (void)count;
    (void)list_base;
    (void)deviation;
    (void)extrusion;
    (void)format;
    (void)metrics;
    return TRUE;
}

__declspec(dllexport)
BOOL APIENTRY wglSwapIntervalEXT(int interval) {
    g_swap_interval = interval;
    return TRUE;
}

__declspec(dllexport)
int APIENTRY wglGetSwapIntervalEXT(void) {
    return g_swap_interval;
}

__declspec(dllexport)
const char* APIENTRY wglGetExtensionsStringARB(HDC hdc) {
    (void)hdc;
    return g_wgl_extensions;
}

__declspec(dllexport)
const char* APIENTRY wglGetExtensionsStringEXT(void) {
    return g_wgl_extensions;
}

__declspec(dllexport)
const GLubyte* APIENTRY glGetString(GLenum name) {
    switch (name) {
    case GL_VENDOR:     return (const GLubyte*)"v86";
    case GL_RENDERER:   return (const GLubyte*)"v86 fake OpenGL over UDP";
    case GL_VERSION:    return (const GLubyte*)"1.1";
    case GL_EXTENSIONS:
        return (const GLubyte*)g_gl_extensions;
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
void APIENTRY glGetIntegerv(GLenum pname, GLint* params) {
    GLint viewport[4];
    GLint scissor[4];
    ClientArrayState* texcoord = &g_texcoord_arrays[client_active_texture_index()];

    if (!params) {
        return;
    }

    switch (pname) {
    case GL_MATRIX_MODE:
        params[0] = (GLint)g_matrix_mode;
        break;
    case GL_VIEWPORT:
        current_viewport(viewport);
        CopyMemory(params, viewport, sizeof(viewport));
        break;
    case GL_SCISSOR_BOX:
        current_scissor_box(scissor);
        CopyMemory(params, scissor, sizeof(scissor));
        break;
    case GL_COLOR_WRITEMASK:
        params[0] = g_color_mask[0] ? GL_TRUE : GL_FALSE;
        params[1] = g_color_mask[1] ? GL_TRUE : GL_FALSE;
        params[2] = g_color_mask[2] ? GL_TRUE : GL_FALSE;
        params[3] = g_color_mask[3] ? GL_TRUE : GL_FALSE;
        break;
    case GL_DEPTH_WRITEMASK:
        params[0] = g_depth_mask ? GL_TRUE : GL_FALSE;
        break;
    case GL_DEPTH_FUNC:
        params[0] = (GLint)g_depth_func;
        break;
    case GL_STENCIL_CLEAR_VALUE:
        params[0] = g_clear_stencil;
        break;
    case GL_STENCIL_FUNC:
        params[0] = (GLint)g_stencil_func;
        break;
    case GL_STENCIL_REF:
        params[0] = g_stencil_ref;
        break;
    case GL_STENCIL_VALUE_MASK:
        params[0] = (GLint)g_stencil_value_mask;
        break;
    case GL_STENCIL_WRITEMASK:
        params[0] = (GLint)g_stencil_write_mask;
        break;
    case GL_STENCIL_FAIL:
        params[0] = (GLint)g_stencil_fail;
        break;
    case GL_STENCIL_PASS_DEPTH_FAIL:
        params[0] = (GLint)g_stencil_zfail;
        break;
    case GL_STENCIL_PASS_DEPTH_PASS:
        params[0] = (GLint)g_stencil_zpass;
        break;
    case GL_SHADE_MODEL:
        params[0] = (GLint)g_shade_model;
        break;
    case GL_CULL_FACE_MODE:
        params[0] = (GLint)g_cull_face_mode;
        break;
    case GL_FRONT_FACE:
        params[0] = (GLint)g_front_face_mode;
        break;
    case GL_BLEND_SRC:
        params[0] = (GLint)g_blend_src;
        break;
    case GL_BLEND_DST:
        params[0] = (GLint)g_blend_dst;
        break;
    case GL_ALPHA_TEST_FUNC:
        params[0] = (GLint)g_alpha_func;
        break;
    case GL_INDEX_CLEAR_VALUE:
        params[0] = (GLint)g_clear_index;
        break;
    case GL_INDEX_WRITEMASK:
        params[0] = (GLint)g_index_mask;
        break;
    case GL_POINT_SIZE:
        params[0] = (GLint)g_point_size;
        break;
    case GL_POINT_SIZE_RANGE:
        params[0] = 1;
        params[1] = 1;
        break;
    case GL_POINT_SIZE_GRANULARITY:
        params[0] = 1;
        break;
    case GL_LINE_STIPPLE_REPEAT:
        params[0] = g_line_stipple_factor;
        break;
    case GL_LINE_STIPPLE_PATTERN:
        params[0] = (GLint)g_line_stipple_pattern;
        break;
    case GL_LIST_BASE:
        params[0] = (GLint)g_list_base;
        break;
    case GL_POLYGON_MODE:
        params[0] = (GLint)g_polygon_mode_front;
        params[1] = (GLint)g_polygon_mode_back;
        break;
    case GL_COLOR_MATERIAL_FACE:
        params[0] = (GLint)g_color_material_face;
        break;
    case GL_COLOR_MATERIAL_PARAMETER:
        params[0] = (GLint)g_color_material_param;
        break;
    case GL_ACTIVE_TEXTURE_ARB:
        params[0] = (GLint)g_active_texture;
        break;
    case GL_CLIENT_ACTIVE_TEXTURE_ARB:
        params[0] = (GLint)g_client_active_texture;
        break;
    case GL_MAX_TEXTURE_UNITS_ARB:
        params[0] = V86GL_MAX_TEXTURE_UNITS;
        break;
    case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
        params[0] = 1;
        break;
    case GL_TEXTURE_BINDING_2D:
        params[0] = (GLint)g_texture_units[active_texture_index()].bound_2d;
        break;
    case GL_TEXTURE_BINDING_1D:
        params[0] = 0;
        break;
    case GL_PACK_ALIGNMENT:
        params[0] = g_pack_alignment;
        break;
    case GL_PACK_ROW_LENGTH:
        params[0] = g_pack_row_length;
        break;
    case GL_PACK_SKIP_ROWS:
        params[0] = g_pack_skip_rows;
        break;
    case GL_PACK_SKIP_PIXELS:
        params[0] = g_pack_skip_pixels;
        break;
    case GL_UNPACK_ALIGNMENT:
        params[0] = g_unpack_alignment;
        break;
    case GL_UNPACK_ROW_LENGTH:
        params[0] = g_unpack_row_length;
        break;
    case GL_UNPACK_SKIP_ROWS:
        params[0] = g_unpack_skip_rows;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        params[0] = g_unpack_skip_pixels;
        break;
    case GL_VERTEX_ARRAY_SIZE:
        params[0] = g_vertex_array.size;
        break;
    case GL_VERTEX_ARRAY_TYPE:
        params[0] = (GLint)g_vertex_array.type;
        break;
    case GL_VERTEX_ARRAY_STRIDE:
        params[0] = g_vertex_array.stride;
        break;
    case GL_COLOR_ARRAY_SIZE:
        params[0] = g_color_array.size;
        break;
    case GL_COLOR_ARRAY_TYPE:
        params[0] = (GLint)g_color_array.type;
        break;
    case GL_COLOR_ARRAY_STRIDE:
        params[0] = g_color_array.stride;
        break;
    case GL_INDEX_ARRAY_TYPE:
        params[0] = (GLint)g_index_array.type;
        break;
    case GL_INDEX_ARRAY_STRIDE:
        params[0] = g_index_array.stride;
        break;
    case GL_TEXTURE_COORD_ARRAY_SIZE:
        params[0] = texcoord->size;
        break;
    case GL_TEXTURE_COORD_ARRAY_TYPE:
        params[0] = (GLint)texcoord->type;
        break;
    case GL_TEXTURE_COORD_ARRAY_STRIDE:
        params[0] = texcoord->stride;
        break;
    case GL_NORMAL_ARRAY_TYPE:
        params[0] = (GLint)g_normal_array.type;
        break;
    case GL_NORMAL_ARRAY_STRIDE:
        params[0] = g_normal_array.stride;
        break;
    case GL_EDGE_FLAG_ARRAY_STRIDE:
        params[0] = g_edge_flag_array.stride;
        break;
    case GL_LINE_WIDTH:
        params[0] = (GLint)g_line_width;
        break;
    case GL_LINE_WIDTH_RANGE:
    case GL_ALIASED_LINE_WIDTH_RANGE:
        params[0] = 1;
        params[1] = 1;
        break;
    case GL_MAX_TEXTURE_SIZE:
        params[0] = 2048;
        break;
    case GL_MAX_VIEWPORT_DIMS:
        params[0] = 4096;
        params[1] = 4096;
        break;
    case GL_MAX_LIGHTS:
        params[0] = 8;
        break;
    case GL_MAX_EVAL_ORDER:
        params[0] = 8;
        break;
    case GL_MAX_CLIP_PLANES:
        params[0] = 6;
        break;
    case GL_MAX_MODELVIEW_STACK_DEPTH:
        params[0] = 32;
        break;
    case GL_MAX_PROJECTION_STACK_DEPTH:
        params[0] = 2;
        break;
    case GL_MAX_TEXTURE_STACK_DEPTH:
        params[0] = 2;
        break;
    case GL_MAX_ATTRIB_STACK_DEPTH:
        params[0] = 16;
        break;
    case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH:
        params[0] = 16;
        break;
    case GL_MODELVIEW_STACK_DEPTH:
    case GL_PROJECTION_STACK_DEPTH:
    case GL_TEXTURE_STACK_DEPTH:
    case GL_ATTRIB_STACK_DEPTH:
    case GL_CLIENT_ATTRIB_STACK_DEPTH:
        params[0] = 1;
        break;
    case GL_SUBPIXEL_BITS:
        params[0] = 4;
        break;
    case GL_RED_BITS:
    case GL_GREEN_BITS:
    case GL_BLUE_BITS:
    case GL_ALPHA_BITS:
        params[0] = 8;
        break;
    case GL_DEPTH_BITS:
        params[0] = 24;
        break;
    case GL_STENCIL_BITS:
        params[0] = 8;
        break;
    case GL_DOUBLEBUFFER:
    case GL_RGBA_MODE:
        params[0] = GL_TRUE;
        break;
    case GL_STEREO:
        params[0] = GL_FALSE;
        break;
    case GL_RENDER_MODE:
        params[0] = (GLint)g_render_mode;
        break;
    case GL_FEEDBACK_BUFFER_SIZE:
        params[0] = g_feedback_buffer_size;
        break;
    case GL_FEEDBACK_BUFFER_TYPE:
        params[0] = (GLint)g_feedback_buffer_type;
        break;
    case GL_SELECTION_BUFFER_SIZE:
        params[0] = g_selection_buffer_size;
        break;
    default:
        g_error = GL_INVALID_ENUM;
        break;
    }
}

__declspec(dllexport)
void APIENTRY glGetFloatv(GLenum pname, GLfloat* params) {
    GLint ints[4];

    if (!params) {
        return;
    }

    switch (pname) {
    case GL_CURRENT_COLOR:
        CopyMemory(params, g_current_color, sizeof(g_current_color));
        break;
    case GL_CURRENT_INDEX:
        params[0] = g_current_index;
        break;
    case GL_CURRENT_TEXTURE_COORDS:
        CopyMemory(params, g_current_texcoord, sizeof(g_current_texcoord));
        break;
    case GL_CURRENT_NORMAL:
        CopyMemory(params, g_current_normal, sizeof(g_current_normal));
        break;
    case GL_COLOR_CLEAR_VALUE:
        CopyMemory(params, g_clear_color, sizeof(g_clear_color));
        break;
    case GL_ACCUM_CLEAR_VALUE:
        CopyMemory(params, g_clear_accum, sizeof(g_clear_accum));
        break;
    case GL_DEPTH_CLEAR_VALUE:
        params[0] = (GLfloat)g_clear_depth;
        break;
    case GL_DEPTH_RANGE:
        params[0] = (GLfloat)g_depth_range[0];
        params[1] = (GLfloat)g_depth_range[1];
        break;
    case GL_ALPHA_TEST_REF:
        params[0] = g_alpha_ref;
        break;
    case GL_INDEX_CLEAR_VALUE:
        params[0] = g_clear_index;
        break;
    case GL_POINT_SIZE:
        params[0] = g_point_size;
        break;
    case GL_POINT_SIZE_RANGE:
        params[0] = 1.0f;
        params[1] = 1.0f;
        break;
    case GL_POINT_SIZE_GRANULARITY:
        params[0] = 1.0f;
        break;
    case GL_LINE_WIDTH:
        params[0] = g_line_width;
        break;
    case GL_LINE_WIDTH_RANGE:
    case GL_ALIASED_LINE_WIDTH_RANGE:
        params[0] = 1.0f;
        params[1] = 1.0f;
        break;
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX:
        identity_matrix(params);
        break;
    default: {
        GLenum old_error = g_error;
        glGetIntegerv(pname, ints);
        if (g_error == old_error) {
            params[0] = (GLfloat)ints[0];
            if (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX ||
                pname == GL_COLOR_WRITEMASK || pname == GL_POLYGON_MODE ||
                pname == GL_MAX_VIEWPORT_DIMS) {
                params[1] = (GLfloat)ints[1];
            }
            if (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX ||
                pname == GL_COLOR_WRITEMASK) {
                params[2] = (GLfloat)ints[2];
                params[3] = (GLfloat)ints[3];
            }
        }
        break;
    }
    }
}

__declspec(dllexport)
void APIENTRY glGetDoublev(GLenum pname, GLdouble* params) {
    GLfloat values[16];
    int count = get_value_count(pname);
    int i;
    GLenum old_error;

    if (!params) {
        return;
    }

    old_error = g_error;
    ZeroMemory(values, sizeof(values));
    glGetFloatv(pname, values);
    if (g_error != old_error) {
        return;
    }

    for (i = 0; i < count; i++) {
        params[i] = (GLdouble)values[i];
    }
}

__declspec(dllexport)
void APIENTRY glGetBooleanv(GLenum pname, GLboolean* params) {
    GLfloat values[16];
    int count = get_value_count(pname);
    int i;
    GLenum old_error;

    if (!params) {
        return;
    }

    old_error = g_error;
    ZeroMemory(values, sizeof(values));
    glGetFloatv(pname, values);
    if (g_error != old_error) {
        return;
    }

    for (i = 0; i < count; i++) {
        params[i] = values[i] != 0.0f ? GL_TRUE : GL_FALSE;
    }
}

__declspec(dllexport)
GLboolean APIENTRY glIsEnabled(GLenum cap) {
    ClientArrayState* texcoord = &g_texcoord_arrays[client_active_texture_index()];

    switch (cap) {
    case GL_VERTEX_ARRAY:
        return g_vertex_array.enabled ? GL_TRUE : GL_FALSE;
    case GL_COLOR_ARRAY:
        return g_color_array.enabled ? GL_TRUE : GL_FALSE;
    case GL_INDEX_ARRAY:
        return g_index_array.enabled ? GL_TRUE : GL_FALSE;
    case GL_TEXTURE_COORD_ARRAY:
        return texcoord->enabled ? GL_TRUE : GL_FALSE;
    case GL_NORMAL_ARRAY:
        return g_normal_array.enabled ? GL_TRUE : GL_FALSE;
    case GL_EDGE_FLAG_ARRAY:
        return g_edge_flag_array.enabled ? GL_TRUE : GL_FALSE;
    default:
        return get_cap_state(cap) ? GL_TRUE : GL_FALSE;
    }
}

__declspec(dllexport)
void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    struct { int32_t x, y, width, height; } payload;
    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_viewport[0] = x;
    g_viewport[1] = y;
    g_viewport[2] = width;
    g_viewport[3] = height;
    g_have_viewport = TRUE;
    payload.x = x;
    payload.y = y;
    payload.width = width;
    payload.height = height;
    emit_gl_call(GLFN_VIEWPORT, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    struct { float r, g, b, a; } payload;
    g_clear_color[0] = r;
    g_clear_color[1] = g;
    g_clear_color[2] = b;
    g_clear_color[3] = a;
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
    g_current_color[0] = r;
    g_current_color[1] = g;
    g_current_color[2] = b;
    g_current_color[3] = 1.0f;
    payload.r = r;
    payload.g = g;
    payload.b = b;
    payload.a = 1.0f;
    emit_gl_call(GLFN_COLOR4F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    struct { float r, g, b, a; } payload;
    g_current_color[0] = r;
    g_current_color[1] = g;
    g_current_color[2] = b;
    g_current_color[3] = a;
    payload.r = r;
    payload.g = g;
    payload.b = b;
    payload.a = a;
    emit_gl_call(GLFN_COLOR4F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    glColor4f((GLfloat)r / 255.0f,
              (GLfloat)g / 255.0f,
              (GLfloat)b / 255.0f,
              (GLfloat)a / 255.0f);
}

__declspec(dllexport)
void APIENTRY glColor4ubv(const GLubyte* v) {
    if (v) {
        glColor4ub(v[0], v[1], v[2], v[3]);
    }
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
void APIENTRY glVertex3fv(const GLfloat* v) {
    if (v) {
        glVertex3f(v[0], v[1], v[2]);
    }
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
    g_matrix_mode = mode;
    emit_gl_call(GLFN_MATRIX_MODE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLoadIdentity(void) {
    emit_gl_call(GLFN_LOAD_IDENTITY, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glLoadMatrixf(const GLfloat* m) {
    float payload[16];

    if (!m) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    CopyMemory(payload, m, sizeof(payload));
    emit_gl_call(GLFN_LOAD_MATRIXF, payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLoadMatrixd(const GLdouble* m) {
    float payload[16];
    uint32_t i;

    if (!m) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    for (i = 0; i < 16; i++) {
        payload[i] = (GLfloat)m[i];
    }

    emit_gl_call(GLFN_LOAD_MATRIXF, payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glMultMatrixf(const GLfloat* m) {
    float payload[16];

    if (!m) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    CopyMemory(payload, m, sizeof(payload));
    emit_gl_call(GLFN_MULT_MATRIXF, payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glMultMatrixd(const GLdouble* m) {
    float payload[16];
    uint32_t i;

    if (!m) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    for (i = 0; i < 16; i++) {
        payload[i] = (GLfloat)m[i];
    }

    emit_gl_call(GLFN_MULT_MATRIXF, payload, sizeof(payload));
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
    set_cap_state(cap, TRUE);
    emit_gl_call(GLFN_ENABLE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glDisable(GLenum cap) {
    uint32_t payload = (uint32_t)cap;
    set_cap_state(cap, FALSE);
    emit_gl_call(GLFN_DISABLE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glDepthFunc(GLenum func) {
    uint32_t payload = (uint32_t)func;
    g_depth_func = func;
    emit_gl_call(GLFN_DEPTH_FUNC, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClearDepth(GLdouble depth) {
    double payload = depth;
    g_clear_depth = depth;
    emit_gl_call(GLFN_CLEAR_DEPTH, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glDepthRange(GLdouble zNear, GLdouble zFar) {
    struct { double zNear, zFar; } payload;
    g_depth_range[0] = zNear;
    g_depth_range[1] = zFar;
    payload.zNear = zNear;
    payload.zFar = zFar;
    emit_gl_call(GLFN_DEPTH_RANGE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClearStencil(GLint s) {
    int32_t payload = s;
    g_clear_stencil = s;
    emit_gl_call(GLFN_CLEAR_STENCIL, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    struct { uint32_t func; int32_t ref; uint32_t mask; } payload;
    g_stencil_func = func;
    g_stencil_ref = ref;
    g_stencil_value_mask = mask;
    payload.func = (uint32_t)func;
    payload.ref = ref;
    payload.mask = mask;
    emit_gl_call(GLFN_STENCIL_FUNC, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glStencilMask(GLuint mask) {
    uint32_t payload = mask;
    g_stencil_write_mask = mask;
    emit_gl_call(GLFN_STENCIL_MASK, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    struct { uint32_t fail, zfail, zpass; } payload;
    g_stencil_fail = fail;
    g_stencil_zfail = zfail;
    g_stencil_zpass = zpass;
    payload.fail = (uint32_t)fail;
    payload.zfail = (uint32_t)zfail;
    payload.zpass = (uint32_t)zpass;
    emit_gl_call(GLFN_STENCIL_OP, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glShadeModel(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    g_shade_model = mode;
    emit_gl_call(GLFN_SHADE_MODEL, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glCullFace(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    g_cull_face_mode = mode;
    emit_gl_call(GLFN_CULL_FACE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glFrontFace(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    g_front_face_mode = mode;
    emit_gl_call(GLFN_FRONT_FACE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glHint(GLenum target, GLenum mode) {
    struct { uint32_t target, mode; } payload;
    payload.target = (uint32_t)target;
    payload.mode = (uint32_t)mode;
    emit_gl_call(GLFN_HINT, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPushAttrib(GLbitfield mask) {
    uint32_t payload = (uint32_t)mask;
    emit_gl_call(GLFN_PUSH_ATTRIB, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPopAttrib(void) {
    emit_gl_call(GLFN_POP_ATTRIB, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glPushClientAttrib(GLbitfield mask) {
    uint32_t payload = (uint32_t)mask;
    emit_gl_call(GLFN_PUSH_CLIENT_ATTRIB, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPopClientAttrib(void) {
    emit_gl_call(GLFN_POP_CLIENT_ATTRIB, NULL, 0);
}

__declspec(dllexport)
void APIENTRY glDrawBuffer(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_DRAW_BUFFER, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glReadBuffer(GLenum mode) {
    uint32_t payload = (uint32_t)mode;
    emit_gl_call(GLFN_READ_BUFFER, &payload, sizeof(payload));
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
        (void)find_texture_state(name, 0, TRUE);
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
    {
        GLsizei i;
        for (i = 0; i < n; i++) {
            delete_texture_state(textures[i]);
        }
    }
    emit_gl_call(GLFN_DELETE_TEXTURES, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glBindTexture(GLenum target, GLuint texture) {
    struct { uint32_t target, texture; } payload;
    if (target == GL_TEXTURE_2D) {
        TextureObjectState* state = find_texture_state(texture, target, TRUE);
        if (!state) {
            return;
        }

        g_texture_units[active_texture_index()].bound_2d = texture;
    }

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
void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                               GLint x, GLint y, GLsizei width, GLsizei height,
                               GLint border) {
    struct {
        uint32_t target;
        int32_t level;
        uint32_t internalformat;
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
        int32_t border;
    } payload;

    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    payload.target = (uint32_t)target;
    payload.level = level;
    payload.internalformat = (uint32_t)internalformat;
    payload.x = x;
    payload.y = y;
    payload.width = width;
    payload.height = height;
    payload.border = border;
    emit_gl_call(GLFN_COPY_TEX_IMAGE_2D, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level,
                                  GLint xoffset, GLint yoffset,
                                  GLint x, GLint y,
                                  GLsizei width, GLsizei height) {
    struct {
        uint32_t target;
        int32_t level;
        int32_t xoffset;
        int32_t yoffset;
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    } payload;

    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    payload.target = (uint32_t)target;
    payload.level = level;
    payload.xoffset = xoffset;
    payload.yoffset = yoffset;
    payload.x = x;
    payload.y = y;
    payload.width = width;
    payload.height = height;
    emit_gl_call(GLFN_COPY_TEX_SUB_IMAGE_2D, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    struct { uint32_t target, pname; int32_t param; } payload;
    update_texture_parameter_i(target, pname, param);
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_PARAMETERI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    struct { uint32_t target, pname; float param; } payload;
    update_texture_parameter_f(target, pname, param);
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_PARAMETERF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    struct {
        uint32_t target;
        uint32_t pname;
        uint32_t count;
        int32_t values[4];
    } payload;
    int count = texture_parameter_value_count(pname);
    int i;

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    if (pname == GL_TEXTURE_BORDER_COLOR) {
        TextureObjectState* state = bound_texture_state(target, TRUE);
        if (!state) {
            return;
        }
        for (i = 0; i < 4; i++) {
            state->border_color[i] = (GLfloat)params[i];
        }
    } else {
        update_texture_parameter_i(target, pname, params[0]);
    }

    emit_gl_call(GLFN_TEX_PARAMETERIV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    struct {
        uint32_t target;
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = texture_parameter_value_count(pname);
    int i;

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    if (pname == GL_TEXTURE_BORDER_COLOR) {
        TextureObjectState* state = bound_texture_state(target, TRUE);
        if (!state) {
            return;
        }
        CopyMemory(state->border_color, params, sizeof(state->border_color));
    } else {
        update_texture_parameter_f(target, pname, params[0]);
    }

    emit_gl_call(GLFN_TEX_PARAMETERFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    TextureObjectState* state;

    if (!params) {
        return;
    }

    state = bound_texture_state(target, FALSE);
    if (!state) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        params[0] = state->min_filter;
        break;
    case GL_TEXTURE_MAG_FILTER:
        params[0] = state->mag_filter;
        break;
    case GL_TEXTURE_WRAP_S:
        params[0] = state->wrap_s;
        break;
    case GL_TEXTURE_WRAP_T:
        params[0] = state->wrap_t;
        break;
    case GL_TEXTURE_BASE_LEVEL:
        params[0] = state->base_level;
        break;
    case GL_TEXTURE_MAX_LEVEL:
        params[0] = state->max_level;
        break;
    case GL_TEXTURE_MIN_LOD:
        params[0] = (GLint)state->min_lod;
        break;
    case GL_TEXTURE_MAX_LOD:
        params[0] = (GLint)state->max_lod;
        break;
    case GL_TEXTURE_PRIORITY:
        params[0] = (GLint)state->priority;
        break;
    case GL_TEXTURE_RESIDENT:
        params[0] = GL_TRUE;
        break;
    case GL_TEXTURE_BORDER_COLOR:
        params[0] = (GLint)state->border_color[0];
        params[1] = (GLint)state->border_color[1];
        params[2] = (GLint)state->border_color[2];
        params[3] = (GLint)state->border_color[3];
        break;
    default:
        g_error = GL_INVALID_ENUM;
        break;
    }
}

__declspec(dllexport)
void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    TextureObjectState* state;

    if (!params) {
        return;
    }

    state = bound_texture_state(target, FALSE);
    if (!state) {
        return;
    }

    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        params[0] = (GLfloat)state->min_filter;
        break;
    case GL_TEXTURE_MAG_FILTER:
        params[0] = (GLfloat)state->mag_filter;
        break;
    case GL_TEXTURE_WRAP_S:
        params[0] = (GLfloat)state->wrap_s;
        break;
    case GL_TEXTURE_WRAP_T:
        params[0] = (GLfloat)state->wrap_t;
        break;
    case GL_TEXTURE_BASE_LEVEL:
        params[0] = (GLfloat)state->base_level;
        break;
    case GL_TEXTURE_MAX_LEVEL:
        params[0] = (GLfloat)state->max_level;
        break;
    case GL_TEXTURE_MIN_LOD:
        params[0] = state->min_lod;
        break;
    case GL_TEXTURE_MAX_LOD:
        params[0] = state->max_lod;
        break;
    case GL_TEXTURE_PRIORITY:
        params[0] = state->priority;
        break;
    case GL_TEXTURE_RESIDENT:
        params[0] = 1.0f;
        break;
    case GL_TEXTURE_BORDER_COLOR:
        CopyMemory(params, state->border_color, sizeof(state->border_color));
        break;
    default:
        g_error = GL_INVALID_ENUM;
        break;
    }
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
    case GL_PACK_ALIGNMENT:
        if (param == 1 || param == 2 || param == 4 || param == 8) {
            g_pack_alignment = param;
        }
        break;
    case GL_PACK_ROW_LENGTH:
        g_pack_row_length = param > 0 ? param : 0;
        break;
    case GL_PACK_SKIP_ROWS:
        g_pack_skip_rows = param > 0 ? param : 0;
        break;
    case GL_PACK_SKIP_PIXELS:
        g_pack_skip_pixels = param > 0 ? param : 0;
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
void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint* params) {
    struct {
        uint32_t target;
        uint32_t pname;
        uint32_t count;
        int32_t values[4];
    } payload;
    int count = texenv_value_count(pname);
    int i;

    if (!count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_TEX_ENVIV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat* params) {
    struct {
        uint32_t target;
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = texenv_value_count(pname);
    int i;

    if (!count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.target = (uint32_t)target;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_TEX_ENVFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexCoord2f(GLfloat s, GLfloat t) {
    struct { float s, t; } payload;
    g_current_texcoord[0] = s;
    g_current_texcoord[1] = t;
    g_current_texcoord[2] = 0.0f;
    g_current_texcoord[3] = 1.0f;
    payload.s = s;
    payload.t = t;
    emit_gl_call(GLFN_TEX_COORD2F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexCoord2fv(const GLfloat* v) {
    if (v) {
        glTexCoord2f(v[0], v[1]);
    }
}

__declspec(dllexport)
void APIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param) {
    struct { uint32_t coord, pname; int32_t param; } payload;
    if (!texgen_coord_valid(coord) || !texgen_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.coord = (uint32_t)coord;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_GENI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param) {
    struct { uint32_t coord, pname; float param; } payload;
    if (!texgen_coord_valid(coord) || !texgen_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.coord = (uint32_t)coord;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_TEX_GENF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param) {
    glTexGenf(coord, pname, (GLfloat)param);
}

__declspec(dllexport)
void APIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint* params) {
    struct {
        uint32_t coord;
        uint32_t pname;
        uint32_t count;
        int32_t values[4];
    } payload;
    int count = texgen_value_count(pname);
    int i;

    if (!texgen_coord_valid(coord) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.coord = (uint32_t)coord;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_TEX_GENIV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat* params) {
    struct {
        uint32_t coord;
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = texgen_value_count(pname);
    int i;

    if (!texgen_coord_valid(coord) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.coord = (uint32_t)coord;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_TEX_GENFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glTexGendv(GLenum coord, GLenum pname, const GLdouble* params) {
    GLfloat values[4];
    int count = texgen_value_count(pname);
    int i;

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    for (i = 0; i < count && i < 4; i++) {
        values[i] = (GLfloat)params[i];
    }

    glTexGenfv(coord, pname, values);
}

__declspec(dllexport)
void APIENTRY glClipPlane(GLenum plane, const GLdouble* equation) {
    struct {
        uint32_t plane;
        double values[4];
    } payload;

    if (!clip_plane_valid(plane)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!equation) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    payload.plane = (uint32_t)plane;
    CopyMemory(payload.values, equation, sizeof(payload.values));
    emit_gl_call(GLFN_CLIP_PLANE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glColorMaterial(GLenum face, GLenum mode) {
    struct { uint32_t face, mode; } payload;
    if (!material_face_valid(face)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_color_material_face = face;
    g_color_material_param = mode;
    payload.face = (uint32_t)face;
    payload.mode = (uint32_t)mode;
    emit_gl_call(GLFN_COLOR_MATERIAL, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glActiveTextureARB(GLenum texture) {
    uint32_t payload = (uint32_t)texture;

    if (texture < GL_TEXTURE0_ARB ||
        texture >= GL_TEXTURE0_ARB + V86GL_MAX_TEXTURE_UNITS) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_active_texture = texture;
    emit_gl_call(GLFN_ACTIVE_TEXTURE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glActiveTexture(GLenum texture) {
    glActiveTextureARB(texture);
}

__declspec(dllexport)
void APIENTRY glClientActiveTextureARB(GLenum texture) {
    uint32_t payload = (uint32_t)texture;

    if (texture < GL_TEXTURE0_ARB ||
        texture >= GL_TEXTURE0_ARB + V86GL_MAX_TEXTURE_UNITS) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_client_active_texture = texture;
    emit_gl_call(GLFN_CLIENT_ACTIVE_TEXTURE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glClientActiveTexture(GLenum texture) {
    glClientActiveTextureARB(texture);
}

static void emit_multi_tex_coord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    struct {
        uint32_t target;
        float s;
        float t;
        float r;
        float q;
    } payload;

    if (target < GL_TEXTURE0_ARB ||
        target >= GL_TEXTURE0_ARB + V86GL_MAX_TEXTURE_UNITS) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.target = (uint32_t)target;
    payload.s = s;
    payload.t = t;
    payload.r = r;
    payload.q = q;
    emit_gl_call(GLFN_MULTI_TEX_COORD4F, &payload, sizeof(payload));
}

#define DEFINE_MULTI_TEX_COORD_SCALAR(SUFFIX, TYPE) \
__declspec(dllexport) void APIENTRY glMultiTexCoord1##SUFFIX##ARB(GLenum target, TYPE s) { \
    emit_multi_tex_coord4f(target, (GLfloat)s, 0.0f, 0.0f, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glMultiTexCoord2##SUFFIX##ARB(GLenum target, TYPE s, TYPE t) { \
    emit_multi_tex_coord4f(target, (GLfloat)s, (GLfloat)t, 0.0f, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glMultiTexCoord3##SUFFIX##ARB(GLenum target, TYPE s, TYPE t, TYPE r) { \
    emit_multi_tex_coord4f(target, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glMultiTexCoord4##SUFFIX##ARB(GLenum target, TYPE s, TYPE t, TYPE r, TYPE q) { \
    emit_multi_tex_coord4f(target, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q); \
}

#define DEFINE_MULTI_TEX_COORD_VECTOR(SUFFIX, TYPE) \
__declspec(dllexport) void APIENTRY glMultiTexCoord1##SUFFIX##vARB(GLenum target, const TYPE* v) { \
    if (v) emit_multi_tex_coord4f(target, (GLfloat)v[0], 0.0f, 0.0f, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glMultiTexCoord2##SUFFIX##vARB(GLenum target, const TYPE* v) { \
    if (v) emit_multi_tex_coord4f(target, (GLfloat)v[0], (GLfloat)v[1], 0.0f, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glMultiTexCoord3##SUFFIX##vARB(GLenum target, const TYPE* v) { \
    if (v) emit_multi_tex_coord4f(target, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1.0f); \
} \
__declspec(dllexport) void APIENTRY glMultiTexCoord4##SUFFIX##vARB(GLenum target, const TYPE* v) { \
    if (v) emit_multi_tex_coord4f(target, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); \
}

DEFINE_MULTI_TEX_COORD_SCALAR(d, GLdouble)
DEFINE_MULTI_TEX_COORD_SCALAR(f, GLfloat)
DEFINE_MULTI_TEX_COORD_SCALAR(i, GLint)
DEFINE_MULTI_TEX_COORD_SCALAR(s, GLshort)
DEFINE_MULTI_TEX_COORD_VECTOR(d, GLdouble)
DEFINE_MULTI_TEX_COORD_VECTOR(f, GLfloat)
DEFINE_MULTI_TEX_COORD_VECTOR(i, GLint)
DEFINE_MULTI_TEX_COORD_VECTOR(s, GLshort)

#undef DEFINE_MULTI_TEX_COORD_SCALAR
#undef DEFINE_MULTI_TEX_COORD_VECTOR

__declspec(dllexport)
void APIENTRY glMultiTexCoord1f(GLenum target, GLfloat s) {
    glMultiTexCoord1fARB(target, s);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t) {
    glMultiTexCoord2fARB(target, s, t);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r) {
    glMultiTexCoord3fARB(target, s, t, r);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    glMultiTexCoord4fARB(target, s, t, r, q);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord1fv(GLenum target, const GLfloat* v) {
    glMultiTexCoord1fvARB(target, v);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord2fv(GLenum target, const GLfloat* v) {
    glMultiTexCoord2fvARB(target, v);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord3fv(GLenum target, const GLfloat* v) {
    glMultiTexCoord3fvARB(target, v);
}

__declspec(dllexport)
void APIENTRY glMultiTexCoord4fv(GLenum target, const GLfloat* v) {
    glMultiTexCoord4fvARB(target, v);
}

__declspec(dllexport)
void APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    struct { float nx, ny, nz; } payload;
    g_current_normal[0] = nx;
    g_current_normal[1] = ny;
    g_current_normal[2] = nz;
    payload.nx = nx;
    payload.ny = ny;
    payload.nz = nz;
    emit_gl_call(GLFN_NORMAL3F, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glNormal3fv(const GLfloat* v) {
    if (v) {
        glNormal3f(v[0], v[1], v[2]);
    }
}

__declspec(dllexport)
void APIENTRY glFogf(GLenum pname, GLfloat param) {
    struct { uint32_t pname; float param; } payload;
    if (!fog_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_FOGF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glFogi(GLenum pname, GLint param) {
    struct { uint32_t pname; int32_t param; } payload;
    if (!fog_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_FOGI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glFogfv(GLenum pname, const GLfloat* params) {
    struct {
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = fog_value_count(pname);
    int i;

    if (!count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_FOGFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    struct { uint32_t face, pname; float param; } payload;
    if (!material_face_valid(face) || !material_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.face = (uint32_t)face;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_MATERIALF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param) {
    struct { uint32_t face, pname; int32_t param; } payload;
    if (!material_face_valid(face) || !material_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.face = (uint32_t)face;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_MATERIALI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat* params) {
    struct {
        uint32_t face;
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = material_value_count(pname);
    int i;

    if (!material_face_valid(face) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.face = (uint32_t)face;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_MATERIALFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glMaterialiv(GLenum face, GLenum pname, const GLint* params) {
    struct {
        uint32_t face;
        uint32_t pname;
        uint32_t count;
        int32_t values[4];
    } payload;
    int count = material_value_count(pname);
    int i;

    if (!material_face_valid(face) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.face = (uint32_t)face;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_MATERIALIV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightf(GLenum light, GLenum pname, GLfloat param) {
    struct { uint32_t light, pname; float param; } payload;
    if (!light_name_valid(light) || !light_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.light = (uint32_t)light;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_LIGHTF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLighti(GLenum light, GLenum pname, GLint param) {
    struct { uint32_t light, pname; int32_t param; } payload;
    if (!light_name_valid(light) || !light_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.light = (uint32_t)light;
    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_LIGHTI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat* params) {
    struct {
        uint32_t light;
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = light_value_count(pname);
    int i;

    if (!light_name_valid(light) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.light = (uint32_t)light;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_LIGHTFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightiv(GLenum light, GLenum pname, const GLint* params) {
    struct {
        uint32_t light;
        uint32_t pname;
        uint32_t count;
        int32_t values[4];
    } payload;
    int count = light_value_count(pname);
    int i;

    if (!light_name_valid(light) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.light = (uint32_t)light;
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_LIGHTIV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightModelf(GLenum pname, GLfloat param) {
    struct { uint32_t pname; float param; } payload;
    if (!light_model_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_LIGHT_MODELF, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightModeli(GLenum pname, GLint param) {
    struct { uint32_t pname; int32_t param; } payload;
    if (!light_model_scalar_valid(pname)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    payload.pname = (uint32_t)pname;
    payload.param = param;
    emit_gl_call(GLFN_LIGHT_MODELI, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightModelfv(GLenum pname, const GLfloat* params) {
    struct {
        uint32_t pname;
        uint32_t count;
        float values[4];
    } payload;
    int count = light_model_value_count(pname);
    int i;

    if (!count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_LIGHT_MODELFV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLightModeliv(GLenum pname, const GLint* params) {
    struct {
        uint32_t pname;
        uint32_t count;
        int32_t values[4];
    } payload;
    int count = light_model_value_count(pname);
    int i;

    if (!count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    ZeroMemory(&payload, sizeof(payload));
    payload.pname = (uint32_t)pname;
    payload.count = (uint32_t)count;
    for (i = 0; i < count; i++) {
        payload.values[i] = params[i];
    }

    emit_gl_call(GLFN_LIGHT_MODELIV, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor) {
    struct { uint32_t sfactor, dfactor; } payload;
    g_blend_src = sfactor;
    g_blend_dst = dfactor;
    payload.sfactor = (uint32_t)sfactor;
    payload.dfactor = (uint32_t)dfactor;
    emit_gl_call(GLFN_BLEND_FUNC, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glAlphaFunc(GLenum func, GLclampf ref) {
    struct { uint32_t func; float ref; } payload;
    g_alpha_func = func;
    g_alpha_ref = ref;
    payload.func = (uint32_t)func;
    payload.ref = ref;
    emit_gl_call(GLFN_ALPHA_FUNC, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glDepthMask(GLboolean flag) {
    uint32_t payload = flag ? 1u : 0u;
    g_depth_mask = flag ? GL_TRUE : GL_FALSE;
    emit_gl_call(GLFN_DEPTH_MASK, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    struct { uint32_t red, green, blue, alpha; } payload;
    g_color_mask[0] = red ? GL_TRUE : GL_FALSE;
    g_color_mask[1] = green ? GL_TRUE : GL_FALSE;
    g_color_mask[2] = blue ? GL_TRUE : GL_FALSE;
    g_color_mask[3] = alpha ? GL_TRUE : GL_FALSE;
    payload.red = red ? 1u : 0u;
    payload.green = green ? 1u : 0u;
    payload.blue = blue ? 1u : 0u;
    payload.alpha = alpha ? 1u : 0u;
    emit_gl_call(GLFN_COLOR_MASK, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    struct { int32_t x, y, width, height; } payload;
    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    payload.x = x;
    payload.y = y;
    payload.width = width;
    payload.height = height;
    g_scissor_box[0] = x;
    g_scissor_box[1] = y;
    g_scissor_box[2] = width;
    g_scissor_box[3] = height;
    emit_gl_call(GLFN_SCISSOR, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glLineWidth(GLfloat width) {
    float payload = width;
    if (width <= 0.0f) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_line_width = width;
    emit_gl_call(GLFN_LINE_WIDTH, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPolygonMode(GLenum face, GLenum mode) {
    struct { uint32_t face, mode; } payload;
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_polygon_mode_front = mode;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_polygon_mode_back = mode;
    }
    payload.face = (uint32_t)face;
    payload.mode = (uint32_t)mode;
    emit_gl_call(GLFN_POLYGON_MODE, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) {
    struct { float factor, units; } payload;
    g_polygon_offset_factor = factor;
    g_polygon_offset_units = units;
    payload.factor = factor;
    payload.units = units;
    emit_gl_call(GLFN_POLYGON_OFFSET, &payload, sizeof(payload));
}

__declspec(dllexport)
void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, GLvoid* pixels) {
    uint32_t data_size;

    (void)x;
    (void)y;

    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!pixels || width == 0 || height == 0) {
        return;
    }

    data_size = gl_read_pixel_span(width, height, format, type);
    if (!data_size) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    ZeroMemory(pixels, data_size);
}

__declspec(dllexport)
void APIENTRY glEnableClientState(GLenum array) {
    (void)set_client_array_enabled(array, TRUE, TRUE);
}

__declspec(dllexport)
void APIENTRY glDisableClientState(GLenum array) {
    (void)set_client_array_enabled(array, FALSE, TRUE);
}

__declspec(dllexport)
void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    if (size < 2 || size > 4) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!gl_type_bytes(type)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_vertex_array.size = size;
    g_vertex_array.type = type;
    g_vertex_array.stride = stride;
    g_vertex_array.pointer = pointer;
}

__declspec(dllexport)
void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    if (size < 3 || size > 4) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!gl_type_bytes(type)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_color_array.size = size;
    g_color_array.type = type;
    g_color_array.stride = stride;
    g_color_array.pointer = pointer;
}

__declspec(dllexport)
void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    ClientArrayState* texcoord = &g_texcoord_arrays[client_active_texture_index()];

    if (size < 1 || size > 4) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!gl_type_bytes(type)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    texcoord->size = size;
    texcoord->type = type;
    texcoord->stride = stride;
    texcoord->pointer = pointer;
}

__declspec(dllexport)
void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!gl_type_bytes(type)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_normal_array.size = 3;
    g_normal_array.type = type;
    g_normal_array.stride = stride;
    g_normal_array.pointer = pointer;
}

__declspec(dllexport)
void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    ClientArrayCopy arrays[3 + V86GL_MAX_TEXTURE_UNITS];
    uint32_t array_block_count = 3u + V86GL_MAX_TEXTURE_UNITS;
    uint32_t block_size;
    uint32_t total_size;
    uint8_t* payload;
    uint8_t* p;
    uint32_t i;
    struct {
        uint32_t mode;
        int32_t count;
        uint32_t magic;
        uint32_t tex_unit_count;
        uint32_t client_active_texture;
    } header;

    if (first < 0 || count < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (count == 0) {
        return;
    }

    if (!g_vertex_array.enabled) {
        g_error = GL_INVALID_OPERATION;
        return;
    }

    if (!client_array_copy(&g_vertex_array, first, count, &arrays[0]) ||
        !client_array_copy(&g_color_array, first, count, &arrays[1]) ||
        !client_array_copy(&g_normal_array, first, count, &arrays[2])) {
        return;
    }

    for (i = 0; i < V86GL_MAX_TEXTURE_UNITS; i++) {
        if (!client_array_copy(&g_texcoord_arrays[i], first, count, &arrays[3 + i])) {
            return;
        }
    }

    block_size = client_array_blocks_size(arrays, array_block_count);
    if (!block_size || block_size > UINT32_MAX - sizeof(header)) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    total_size = sizeof(header) + block_size;
    payload = alloc_payload(total_size);
    if (!payload) {
        return;
    }

    header.mode = (uint32_t)mode;
    header.count = count;
    header.magic = CLIENT_ARRAY_MT_MAGIC;
    header.tex_unit_count = V86GL_MAX_TEXTURE_UNITS;
    header.client_active_texture = (uint32_t)g_client_active_texture;
    CopyMemory(payload, &header, sizeof(header));
    p = payload + sizeof(header);
    for (i = 0; i < array_block_count; i++) {
        p = write_client_array_block(p, &arrays[i]);
    }

    emit_gl_call(GLFN_DRAW_ARRAYS, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    ClientArrayCopy arrays[3 + V86GL_MAX_TEXTURE_UNITS];
    uint32_t array_block_count = 3u + V86GL_MAX_TEXTURE_UNITS;
    uint32_t max_index;
    uint32_t array_count;
    uint32_t index_size;
    uint32_t index_data_size;
    uint32_t block_size;
    uint32_t total_size;
    uint8_t* payload;
    uint8_t* p;
    uint32_t i;
    struct {
        uint32_t mode;
        int32_t count;
        uint32_t type;
        uint32_t index_data_size;
        uint32_t magic;
        uint32_t tex_unit_count;
        uint32_t client_active_texture;
    } header;

    if (count < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (count == 0) {
        return;
    }

    if (!g_vertex_array.enabled) {
        g_error = GL_INVALID_OPERATION;
        return;
    }

    index_size = (uint32_t)gl_index_bytes(type);
    if (!index_size) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if ((uint32_t)count > UINT32_MAX / index_size) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!max_draw_index(indices, count, type, &max_index)) {
        return;
    }

    if (max_index >= 0x7FFFFFFFu) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    array_count = max_index + 1u;
    if (!client_array_copy(&g_vertex_array, 0, (GLsizei)array_count, &arrays[0]) ||
        !client_array_copy(&g_color_array, 0, (GLsizei)array_count, &arrays[1]) ||
        !client_array_copy(&g_normal_array, 0, (GLsizei)array_count, &arrays[2])) {
        return;
    }

    for (i = 0; i < V86GL_MAX_TEXTURE_UNITS; i++) {
        if (!client_array_copy(&g_texcoord_arrays[i], 0, (GLsizei)array_count, &arrays[3 + i])) {
            return;
        }
    }

    index_data_size = (uint32_t)count * index_size;
    block_size = client_array_blocks_size(arrays, array_block_count);
    if (index_data_size > UINT32_MAX - sizeof(header) ||
        !block_size ||
        block_size > UINT32_MAX - sizeof(header) - index_data_size) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    total_size = sizeof(header) + index_data_size + block_size;
    payload = alloc_payload(total_size);
    if (!payload) {
        return;
    }

    header.mode = (uint32_t)mode;
    header.count = count;
    header.type = (uint32_t)type;
    header.index_data_size = index_data_size;
    header.magic = CLIENT_ARRAY_MT_MAGIC;
    header.tex_unit_count = V86GL_MAX_TEXTURE_UNITS;
    header.client_active_texture = (uint32_t)g_client_active_texture;
    CopyMemory(payload, &header, sizeof(header));
    p = payload + sizeof(header);
    CopyMemory(p, indices, index_data_size);
    p += index_data_size;
    for (i = 0; i < array_block_count; i++) {
        p = write_client_array_block(p, &arrays[i]);
    }

    emit_gl_call(GLFN_DRAW_ELEMENTS, payload, total_size);
    HeapFree(GetProcessHeap(), 0, payload);
}

__declspec(dllexport)
void APIENTRY glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid* pointer) {
    const uint8_t* base = (const uint8_t*)pointer;
    ClientArrayState* texcoord = &g_texcoord_arrays[client_active_texture_index()];
    GLsizei row_size = 0;
    GLint vertex_size = 3;
    GLint tex_size = 0;
    GLint color_size = 0;
    GLint normal_offset = -1;
    GLint vertex_offset = -1;
    GLint tex_offset = -1;
    GLint color_offset = -1;
    GLenum color_type = GL_FLOAT;

    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    switch (format) {
    case GL_V2F:
        row_size = 8; vertex_size = 2; vertex_offset = 0;
        break;
    case GL_V3F:
        row_size = 12; vertex_size = 3; vertex_offset = 0;
        break;
    case GL_C4UB_V2F:
        row_size = 12; color_size = 4; color_type = GL_UNSIGNED_BYTE; color_offset = 0; vertex_size = 2; vertex_offset = 4;
        break;
    case GL_C4UB_V3F:
        row_size = 16; color_size = 4; color_type = GL_UNSIGNED_BYTE; color_offset = 0; vertex_size = 3; vertex_offset = 4;
        break;
    case GL_C3F_V3F:
        row_size = 24; color_size = 3; color_offset = 0; vertex_size = 3; vertex_offset = 12;
        break;
    case GL_N3F_V3F:
        row_size = 24; normal_offset = 0; vertex_size = 3; vertex_offset = 12;
        break;
    case GL_C4F_N3F_V3F:
        row_size = 40; color_size = 4; color_offset = 0; normal_offset = 16; vertex_size = 3; vertex_offset = 28;
        break;
    case GL_T2F_V3F:
        row_size = 20; tex_size = 2; tex_offset = 0; vertex_size = 3; vertex_offset = 8;
        break;
    case GL_T4F_V4F:
        row_size = 32; tex_size = 4; tex_offset = 0; vertex_size = 4; vertex_offset = 16;
        break;
    case GL_T2F_C4UB_V3F:
        row_size = 24; tex_size = 2; tex_offset = 0; color_size = 4; color_type = GL_UNSIGNED_BYTE; color_offset = 8; vertex_size = 3; vertex_offset = 12;
        break;
    case GL_T2F_C3F_V3F:
        row_size = 32; tex_size = 2; tex_offset = 0; color_size = 3; color_offset = 8; vertex_size = 3; vertex_offset = 20;
        break;
    case GL_T2F_N3F_V3F:
        row_size = 32; tex_size = 2; tex_offset = 0; normal_offset = 8; vertex_size = 3; vertex_offset = 20;
        break;
    case GL_T2F_C4F_N3F_V3F:
        row_size = 48; tex_size = 2; tex_offset = 0; color_size = 4; color_offset = 8; normal_offset = 24; vertex_size = 3; vertex_offset = 36;
        break;
    case GL_T4F_C4F_N3F_V4F:
        row_size = 60; tex_size = 4; tex_offset = 0; color_size = 4; color_offset = 16; normal_offset = 32; vertex_size = 4; vertex_offset = 44;
        break;
    default:
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (stride == 0) {
        stride = row_size;
    }

    g_vertex_array.size = vertex_size;
    g_vertex_array.type = GL_FLOAT;
    g_vertex_array.stride = stride;
    g_vertex_array.pointer = base ? base + vertex_offset : NULL;

    if (tex_size) {
        texcoord->size = tex_size;
        texcoord->type = GL_FLOAT;
        texcoord->stride = stride;
        texcoord->pointer = base ? base + tex_offset : NULL;
    }

    if (color_size) {
        g_color_array.size = color_size;
        g_color_array.type = color_type;
        g_color_array.stride = stride;
        g_color_array.pointer = base ? base + color_offset : NULL;
    }

    if (normal_offset >= 0) {
        g_normal_array.size = 3;
        g_normal_array.type = GL_FLOAT;
        g_normal_array.stride = stride;
        g_normal_array.pointer = base ? base + normal_offset : NULL;
    }

    (void)set_client_array_enabled(GL_VERTEX_ARRAY, TRUE, TRUE);
    (void)set_client_array_enabled(GL_TEXTURE_COORD_ARRAY, tex_size ? TRUE : FALSE, TRUE);
    (void)set_client_array_enabled(GL_COLOR_ARRAY, color_size ? TRUE : FALSE, TRUE);
    (void)set_client_array_enabled(GL_NORMAL_ARRAY, normal_offset >= 0 ? TRUE : FALSE, TRUE);
}

__declspec(dllexport)
void APIENTRY glLockArraysEXT(GLint first, GLsizei count) {
    (void)first;
    if (count < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glUnlockArraysEXT(void) {
}

static GLfloat color_from_byte(GLbyte v) {
    return v < 0 ? (GLfloat)v / 128.0f : (GLfloat)v / 127.0f;
}

static GLfloat color_from_short(GLshort v) {
    return v < 0 ? (GLfloat)v / 32768.0f : (GLfloat)v / 32767.0f;
}

static GLfloat color_from_int(GLint v) {
    return v < 0 ? (GLfloat)((GLdouble)v / 2147483648.0) :
                   (GLfloat)((GLdouble)v / 2147483647.0);
}

static GLfloat color_from_ubyte(GLubyte v) {
    return (GLfloat)v / 255.0f;
}

static GLfloat color_from_ushort(GLushort v) {
    return (GLfloat)v / 65535.0f;
}

static GLfloat color_from_uint(GLuint v) {
    return (GLfloat)((GLdouble)v / 4294967295.0);
}

static GLfloat color_from_double(GLdouble v) {
    return (GLfloat)v;
}

#define DEFINE_COLOR_VARIANT(SUFFIX, TYPE, CONVERT) \
__declspec(dllexport) void APIENTRY glColor3##SUFFIX(TYPE r, TYPE g, TYPE b) { \
    glColor4f(CONVERT(r), CONVERT(g), CONVERT(b), 1.0f); \
} \
__declspec(dllexport) void APIENTRY glColor3##SUFFIX##v(const TYPE* v) { \
    if (v) glColor3##SUFFIX(v[0], v[1], v[2]); \
} \
__declspec(dllexport) void APIENTRY glColor4##SUFFIX(TYPE r, TYPE g, TYPE b, TYPE a) { \
    glColor4f(CONVERT(r), CONVERT(g), CONVERT(b), CONVERT(a)); \
} \
__declspec(dllexport) void APIENTRY glColor4##SUFFIX##v(const TYPE* v) { \
    if (v) glColor4##SUFFIX(v[0], v[1], v[2], v[3]); \
}

DEFINE_COLOR_VARIANT(b, GLbyte, color_from_byte)
DEFINE_COLOR_VARIANT(d, GLdouble, color_from_double)
DEFINE_COLOR_VARIANT(i, GLint, color_from_int)
DEFINE_COLOR_VARIANT(s, GLshort, color_from_short)
DEFINE_COLOR_VARIANT(ui, GLuint, color_from_uint)
DEFINE_COLOR_VARIANT(us, GLushort, color_from_ushort)

#undef DEFINE_COLOR_VARIANT

__declspec(dllexport)
void APIENTRY glColor3fv(const GLfloat* v) {
    if (v) {
        glColor3f(v[0], v[1], v[2]);
    }
}

__declspec(dllexport)
void APIENTRY glColor4fv(const GLfloat* v) {
    if (v) {
        glColor4f(v[0], v[1], v[2], v[3]);
    }
}

__declspec(dllexport)
void APIENTRY glColor3ub(GLubyte r, GLubyte g, GLubyte b) {
    glColor4f(color_from_ubyte(r), color_from_ubyte(g), color_from_ubyte(b), 1.0f);
}

__declspec(dllexport)
void APIENTRY glColor3ubv(const GLubyte* v) {
    if (v) {
        glColor3ub(v[0], v[1], v[2]);
    }
}

#define DEFINE_NORMAL_VARIANT(SUFFIX, TYPE) \
__declspec(dllexport) void APIENTRY glNormal3##SUFFIX(TYPE nx, TYPE ny, TYPE nz) { \
    glNormal3f((GLfloat)nx, (GLfloat)ny, (GLfloat)nz); \
} \
__declspec(dllexport) void APIENTRY glNormal3##SUFFIX##v(const TYPE* v) { \
    if (v) glNormal3##SUFFIX(v[0], v[1], v[2]); \
}

DEFINE_NORMAL_VARIANT(b, GLbyte)
DEFINE_NORMAL_VARIANT(d, GLdouble)
DEFINE_NORMAL_VARIANT(i, GLint)
DEFINE_NORMAL_VARIANT(s, GLshort)

#undef DEFINE_NORMAL_VARIANT

static void tex_coord4f_compat(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    struct { float s, t; } payload;
    g_current_texcoord[0] = s;
    g_current_texcoord[1] = t;
    g_current_texcoord[2] = r;
    g_current_texcoord[3] = q;
    payload.s = s;
    payload.t = t;
    emit_gl_call(GLFN_TEX_COORD2F, &payload, sizeof(payload));
}

#define DEFINE_TEX_COORD_VARIANT(SUFFIX, TYPE) \
__declspec(dllexport) void APIENTRY glTexCoord1##SUFFIX(TYPE s) { \
    tex_coord4f_compat((GLfloat)s, 0.0f, 0.0f, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glTexCoord1##SUFFIX##v(const TYPE* v) { \
    if (v) glTexCoord1##SUFFIX(v[0]); \
} \
__declspec(dllexport) void APIENTRY glTexCoord2##SUFFIX(TYPE s, TYPE t) { \
    tex_coord4f_compat((GLfloat)s, (GLfloat)t, 0.0f, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glTexCoord2##SUFFIX##v(const TYPE* v) { \
    if (v) glTexCoord2##SUFFIX(v[0], v[1]); \
} \
__declspec(dllexport) void APIENTRY glTexCoord3##SUFFIX(TYPE s, TYPE t, TYPE r) { \
    tex_coord4f_compat((GLfloat)s, (GLfloat)t, (GLfloat)r, 1.0f); \
} \
__declspec(dllexport) void APIENTRY glTexCoord3##SUFFIX##v(const TYPE* v) { \
    if (v) glTexCoord3##SUFFIX(v[0], v[1], v[2]); \
} \
__declspec(dllexport) void APIENTRY glTexCoord4##SUFFIX(TYPE s, TYPE t, TYPE r, TYPE q) { \
    tex_coord4f_compat((GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q); \
} \
__declspec(dllexport) void APIENTRY glTexCoord4##SUFFIX##v(const TYPE* v) { \
    if (v) glTexCoord4##SUFFIX(v[0], v[1], v[2], v[3]); \
}

DEFINE_TEX_COORD_VARIANT(d, GLdouble)
DEFINE_TEX_COORD_VARIANT(i, GLint)
DEFINE_TEX_COORD_VARIANT(s, GLshort)

#undef DEFINE_TEX_COORD_VARIANT

__declspec(dllexport)
void APIENTRY glTexCoord1f(GLfloat s) {
    tex_coord4f_compat(s, 0.0f, 0.0f, 1.0f);
}

__declspec(dllexport)
void APIENTRY glTexCoord1fv(const GLfloat* v) {
    if (v) {
        glTexCoord1f(v[0]);
    }
}

__declspec(dllexport)
void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) {
    tex_coord4f_compat(s, t, r, 1.0f);
}

__declspec(dllexport)
void APIENTRY glTexCoord3fv(const GLfloat* v) {
    if (v) {
        glTexCoord3f(v[0], v[1], v[2]);
    }
}

__declspec(dllexport)
void APIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    tex_coord4f_compat(s, t, r, q);
}

__declspec(dllexport)
void APIENTRY glTexCoord4fv(const GLfloat* v) {
    if (v) {
        glTexCoord4f(v[0], v[1], v[2], v[3]);
    }
}

#define DEFINE_VERTEX_VARIANT(SUFFIX, TYPE) \
__declspec(dllexport) void APIENTRY glVertex2##SUFFIX(TYPE x, TYPE y) { \
    glVertex3f((GLfloat)x, (GLfloat)y, 0.0f); \
} \
__declspec(dllexport) void APIENTRY glVertex2##SUFFIX##v(const TYPE* v) { \
    if (v) glVertex2##SUFFIX(v[0], v[1]); \
} \
__declspec(dllexport) void APIENTRY glVertex3##SUFFIX(TYPE x, TYPE y, TYPE z) { \
    glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z); \
} \
__declspec(dllexport) void APIENTRY glVertex3##SUFFIX##v(const TYPE* v) { \
    if (v) glVertex3##SUFFIX(v[0], v[1], v[2]); \
} \
__declspec(dllexport) void APIENTRY glVertex4##SUFFIX(TYPE x, TYPE y, TYPE z, TYPE w) { \
    (void)w; \
    glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z); \
} \
__declspec(dllexport) void APIENTRY glVertex4##SUFFIX##v(const TYPE* v) { \
    if (v) glVertex4##SUFFIX(v[0], v[1], v[2], v[3]); \
}

DEFINE_VERTEX_VARIANT(d, GLdouble)
DEFINE_VERTEX_VARIANT(i, GLint)
DEFINE_VERTEX_VARIANT(s, GLshort)

#undef DEFINE_VERTEX_VARIANT

__declspec(dllexport)
void APIENTRY glVertex2fv(const GLfloat* v) {
    if (v) {
        glVertex2f(v[0], v[1]);
    }
}

__declspec(dllexport)
void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    (void)w;
    glVertex3f(x, y, z);
}

__declspec(dllexport)
void APIENTRY glVertex4fv(const GLfloat* v) {
    if (v) {
        glVertex4f(v[0], v[1], v[2], v[3]);
    }
}

__declspec(dllexport)
void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    glBegin(GL_QUADS);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
}

__declspec(dllexport)
void APIENTRY glRectfv(const GLfloat* v1, const GLfloat* v2) {
    if (v1 && v2) {
        glRectf(v1[0], v1[1], v2[0], v2[1]);
    }
}

__declspec(dllexport)
void APIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2) {
    glRectf((GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

__declspec(dllexport)
void APIENTRY glRectdv(const GLdouble* v1, const GLdouble* v2) {
    if (v1 && v2) {
        glRectd(v1[0], v1[1], v2[0], v2[1]);
    }
}

__declspec(dllexport)
void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2) {
    glRectf((GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

__declspec(dllexport)
void APIENTRY glRectiv(const GLint* v1, const GLint* v2) {
    if (v1 && v2) {
        glRecti(v1[0], v1[1], v2[0], v2[1]);
    }
}

__declspec(dllexport)
void APIENTRY glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2) {
    glRectf((GLfloat)x1, (GLfloat)y1, (GLfloat)x2, (GLfloat)y2);
}

__declspec(dllexport)
void APIENTRY glRectsv(const GLshort* v1, const GLshort* v2) {
    if (v1 && v2) {
        glRects(v1[0], v1[1], v2[0], v2[1]);
    }
}

__declspec(dllexport)
void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z) {
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

__declspec(dllexport)
void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) {
    glRotatef((GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

__declspec(dllexport)
void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z) {
    glScalef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

__declspec(dllexport)
void APIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    g_clear_accum[0] = red;
    g_clear_accum[1] = green;
    g_clear_accum[2] = blue;
    g_clear_accum[3] = alpha;
}

__declspec(dllexport)
void APIENTRY glClearIndex(GLfloat c) {
    g_clear_index = c;
}

__declspec(dllexport)
void APIENTRY glIndexMask(GLuint mask) {
    g_index_mask = mask;
}

__declspec(dllexport)
void APIENTRY glIndexf(GLfloat c) {
    g_current_index = c;
}

__declspec(dllexport)
void APIENTRY glIndexfv(const GLfloat* c) {
    if (c) {
        glIndexf(c[0]);
    }
}

__declspec(dllexport)
void APIENTRY glIndexd(GLdouble c) {
    glIndexf((GLfloat)c);
}

__declspec(dllexport)
void APIENTRY glIndexdv(const GLdouble* c) {
    if (c) {
        glIndexd(c[0]);
    }
}

__declspec(dllexport)
void APIENTRY glIndexi(GLint c) {
    glIndexf((GLfloat)c);
}

__declspec(dllexport)
void APIENTRY glIndexiv(const GLint* c) {
    if (c) {
        glIndexi(c[0]);
    }
}

__declspec(dllexport)
void APIENTRY glIndexs(GLshort c) {
    glIndexf((GLfloat)c);
}

__declspec(dllexport)
void APIENTRY glIndexsv(const GLshort* c) {
    if (c) {
        glIndexs(c[0]);
    }
}

__declspec(dllexport)
void APIENTRY glIndexub(GLubyte c) {
    glIndexf((GLfloat)c);
}

__declspec(dllexport)
void APIENTRY glIndexubv(const GLubyte* c) {
    if (c) {
        glIndexub(c[0]);
    }
}

__declspec(dllexport)
void APIENTRY glEdgeFlag(GLboolean flag) {
    g_current_edge_flag = flag ? GL_TRUE : GL_FALSE;
}

__declspec(dllexport)
void APIENTRY glEdgeFlagv(const GLboolean* flag) {
    if (flag) {
        glEdgeFlag(flag[0]);
    }
}

__declspec(dllexport)
void APIENTRY glIndexPointer(GLenum type, GLsizei stride, const GLvoid* pointer) {
    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!gl_type_bytes(type)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    g_index_array.size = 1;
    g_index_array.type = type;
    g_index_array.stride = stride;
    g_index_array.pointer = pointer;
}

__declspec(dllexport)
void APIENTRY glEdgeFlagPointer(GLsizei stride, const GLvoid* pointer) {
    if (stride < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_edge_flag_array.size = 1;
    g_edge_flag_array.type = GL_UNSIGNED_BYTE;
    g_edge_flag_array.stride = stride;
    g_edge_flag_array.pointer = pointer;
}

__declspec(dllexport)
void APIENTRY glArrayElement(GLint i) {
    (void)i;
}

__declspec(dllexport)
void APIENTRY glPointSize(GLfloat size) {
    if (size <= 0.0f) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_point_size = size;
}

__declspec(dllexport)
void APIENTRY glLineStipple(GLint factor, GLushort pattern) {
    if (factor < 1) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_line_stipple_factor = factor;
    g_line_stipple_pattern = pattern;
}

__declspec(dllexport)
void APIENTRY glLogicOp(GLenum opcode) {
    (void)opcode;
}

__declspec(dllexport)
void APIENTRY glAccum(GLenum op, GLfloat value) {
    (void)op;
    (void)value;
}

__declspec(dllexport)
void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig,
                       GLfloat xmove, GLfloat ymove, const GLubyte* bitmap) {
    (void)xorig;
    (void)yorig;
    (void)xmove;
    (void)ymove;
    (void)bitmap;
    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type) {
    (void)x;
    (void)y;
    (void)type;
    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels) {
    (void)format;
    (void)type;
    (void)pixels;
    if (width < 0 || height < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glPixelStoref(GLenum pname, GLfloat param) {
    glPixelStorei(pname, (GLint)param);
}

__declspec(dllexport)
void APIENTRY glPixelTransferf(GLenum pname, GLfloat param) {
    (void)pname;
    (void)param;
}

__declspec(dllexport)
void APIENTRY glPixelTransferi(GLenum pname, GLint param) {
    (void)pname;
    (void)param;
}

__declspec(dllexport)
void APIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor) {
    (void)xfactor;
    (void)yfactor;
}

__declspec(dllexport)
void APIENTRY glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat* values) {
    (void)map;
    (void)values;
    if (mapsize < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint* values) {
    (void)map;
    (void)values;
    if (mapsize < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort* values) {
    (void)map;
    (void)values;
    if (mapsize < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glGetPixelMapfv(GLenum map, GLfloat* values) {
    (void)map;
    if (values) {
        values[0] = 0.0f;
    }
}

__declspec(dllexport)
void APIENTRY glGetPixelMapuiv(GLenum map, GLuint* values) {
    (void)map;
    if (values) {
        values[0] = 0;
    }
}

__declspec(dllexport)
void APIENTRY glGetPixelMapusv(GLenum map, GLushort* values) {
    (void)map;
    if (values) {
        values[0] = 0;
    }
}

__declspec(dllexport)
void APIENTRY glPolygonStipple(const GLubyte* mask) {
    (void)mask;
}

__declspec(dllexport)
void APIENTRY glGetPolygonStipple(GLubyte* mask) {
    if (mask) {
        FillMemory(mask, 128, 0xFF);
    }
}

__declspec(dllexport)
GLuint APIENTRY glGenLists(GLsizei range) {
    GLuint first;

    if (range <= 0) {
        return 0;
    }

    first = g_next_list_id;
    g_next_list_id += (GLuint)range;
    if (g_next_list_id <= first) {
        g_next_list_id = first + 1;
    }
    return first;
}

__declspec(dllexport)
void APIENTRY glDeleteLists(GLuint list, GLsizei range) {
    (void)list;
    if (range < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
GLboolean APIENTRY glIsList(GLuint list) {
    return (list > 0 && list < g_next_list_id) ? GL_TRUE : GL_FALSE;
}

__declspec(dllexport)
void APIENTRY glNewList(GLuint list, GLenum mode) {
    (void)list;
    (void)mode;
}

__declspec(dllexport)
void APIENTRY glEndList(void) {
}

__declspec(dllexport)
void APIENTRY glCallList(GLuint list) {
    (void)list;
}

__declspec(dllexport)
void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid* lists) {
    (void)type;
    (void)lists;
    if (n < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glListBase(GLuint base) {
    g_list_base = base;
}

__declspec(dllexport)
void APIENTRY glSelectBuffer(GLsizei size, GLuint* buffer) {
    if (size < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_selection_buffer_size = size;
    g_selection_buffer = buffer;
}

__declspec(dllexport)
void APIENTRY glFeedbackBuffer(GLsizei size, GLenum type, GLfloat* buffer) {
    if (size < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    g_feedback_buffer_size = size;
    g_feedback_buffer_type = type;
    g_feedback_buffer = buffer;
}

__declspec(dllexport)
GLint APIENTRY glRenderMode(GLenum mode) {
    GLenum old_mode = g_render_mode;
    if (mode != GL_RENDER && mode != GL_SELECT && mode != GL_FEEDBACK) {
        g_error = GL_INVALID_ENUM;
        return 0;
    }

    g_render_mode = mode;
    (void)old_mode;
    return 0;
}

__declspec(dllexport)
void APIENTRY glInitNames(void) {
    g_name_stack_depth = 0;
}

__declspec(dllexport)
void APIENTRY glLoadName(GLuint name) {
    if (g_name_stack_depth <= 0) {
        g_error = GL_INVALID_OPERATION;
        return;
    }

    g_name_stack[g_name_stack_depth - 1] = name;
}

__declspec(dllexport)
void APIENTRY glPushName(GLuint name) {
    if (g_name_stack_depth >= (GLint)(sizeof(g_name_stack) / sizeof(g_name_stack[0]))) {
        g_error = GL_INVALID_OPERATION;
        return;
    }

    g_name_stack[g_name_stack_depth++] = name;
}

__declspec(dllexport)
void APIENTRY glPopName(void) {
    if (g_name_stack_depth <= 0) {
        g_error = GL_INVALID_OPERATION;
        return;
    }

    g_name_stack_depth--;
}

__declspec(dllexport)
void APIENTRY glPassThrough(GLfloat token) {
    (void)token;
}

__declspec(dllexport)
void APIENTRY glFogiv(GLenum pname, const GLint* params) {
    GLfloat values[4];
    int count = fog_value_count(pname);
    int i;

    if (!params) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    for (i = 0; i < count; i++) {
        values[i] = (GLfloat)params[i];
    }

    glFogfv(pname, values);
}

static void get_zero_floats(GLfloat* params, int count) {
    int i;
    if (!params) {
        return;
    }
    for (i = 0; i < count; i++) {
        params[i] = 0.0f;
    }
}

static void get_zero_ints(GLint* params, int count) {
    int i;
    if (!params) {
        return;
    }
    for (i = 0; i < count; i++) {
        params[i] = 0;
    }
}

__declspec(dllexport)
void APIENTRY glGetClipPlane(GLenum plane, GLdouble* equation) {
    int i;
    if (!clip_plane_valid(plane)) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    if (equation) {
        for (i = 0; i < 4; i++) {
            equation[i] = 0.0;
        }
    }
}

__declspec(dllexport)
void APIENTRY glGetLightfv(GLenum light, GLenum pname, GLfloat* params) {
    int count = light_value_count(pname);
    if (!light_name_valid(light) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    get_zero_floats(params, count);
}

__declspec(dllexport)
void APIENTRY glGetLightiv(GLenum light, GLenum pname, GLint* params) {
    int count = light_value_count(pname);
    if (!light_name_valid(light) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    get_zero_ints(params, count);
}

__declspec(dllexport)
void APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat* params) {
    int count = material_value_count(pname);
    if (!material_face_valid(face) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    get_zero_floats(params, count);
}

__declspec(dllexport)
void APIENTRY glGetMaterialiv(GLenum face, GLenum pname, GLint* params) {
    int count = material_value_count(pname);
    if (!material_face_valid(face) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    get_zero_ints(params, count);
}

__declspec(dllexport)
void APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat* params) {
    if (target != GL_TEXTURE_ENV || !params) {
        if (target != GL_TEXTURE_ENV) {
            g_error = GL_INVALID_ENUM;
        }
        return;
    }

    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        params[0] = (GLfloat)GL_MODULATE;
        break;
    case GL_TEXTURE_ENV_COLOR:
        get_zero_floats(params, 4);
        break;
    default:
        g_error = GL_INVALID_ENUM;
        break;
    }
}

__declspec(dllexport)
void APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint* params) {
    if (target != GL_TEXTURE_ENV || !params) {
        if (target != GL_TEXTURE_ENV) {
            g_error = GL_INVALID_ENUM;
        }
        return;
    }

    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        params[0] = GL_MODULATE;
        break;
    case GL_TEXTURE_ENV_COLOR:
        get_zero_ints(params, 4);
        break;
    default:
        g_error = GL_INVALID_ENUM;
        break;
    }
}

__declspec(dllexport)
void APIENTRY glGetTexGenfv(GLenum coord, GLenum pname, GLfloat* params) {
    int count = texgen_value_count(pname);
    if (!texgen_coord_valid(coord) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    get_zero_floats(params, count);
}

__declspec(dllexport)
void APIENTRY glGetTexGeniv(GLenum coord, GLenum pname, GLint* params) {
    int count = texgen_value_count(pname);
    if (!texgen_coord_valid(coord) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    get_zero_ints(params, count);
}

__declspec(dllexport)
void APIENTRY glGetTexGendv(GLenum coord, GLenum pname, GLdouble* params) {
    GLfloat values[4];
    int count = texgen_value_count(pname);
    int i;
    if (!params) {
        return;
    }

    if (!texgen_coord_valid(coord) || !count) {
        g_error = GL_INVALID_ENUM;
        return;
    }

    ZeroMemory(values, sizeof(values));
    glGetTexGenfv(coord, pname, values);
    for (i = 0; i < count; i++) {
        params[i] = (GLdouble)values[i];
    }
}

__declspec(dllexport)
void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels) {
    (void)target;
    (void)level;
    (void)format;
    (void)type;
    (void)pixels;
}

__declspec(dllexport)
void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat* params) {
    (void)level;
    (void)pname;
    if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) {
        g_error = GL_INVALID_ENUM;
        return;
    }
    if (params) {
        params[0] = 0.0f;
    }
}

__declspec(dllexport)
void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    (void)level;
    (void)pname;
    if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) {
        g_error = GL_INVALID_ENUM;
        return;
    }
    if (params) {
        params[0] = 0;
    }
}

__declspec(dllexport)
void APIENTRY glGetPointerv(GLenum pname, GLvoid** params) {
    ClientArrayState* texcoord = &g_texcoord_arrays[client_active_texture_index()];
    if (!params) {
        return;
    }

    switch (pname) {
    case GL_VERTEX_ARRAY_POINTER:
        *params = (GLvoid*)g_vertex_array.pointer;
        break;
    case GL_COLOR_ARRAY_POINTER:
        *params = (GLvoid*)g_color_array.pointer;
        break;
    case GL_INDEX_ARRAY_POINTER:
        *params = (GLvoid*)g_index_array.pointer;
        break;
    case GL_TEXTURE_COORD_ARRAY_POINTER:
        *params = (GLvoid*)texcoord->pointer;
        break;
    case GL_NORMAL_ARRAY_POINTER:
        *params = (GLvoid*)g_normal_array.pointer;
        break;
    case GL_EDGE_FLAG_ARRAY_POINTER:
        *params = (GLvoid*)g_edge_flag_array.pointer;
        break;
    case GL_FEEDBACK_BUFFER_POINTER:
        *params = (GLvoid*)g_feedback_buffer;
        break;
    case GL_SELECTION_BUFFER_POINTER:
        *params = (GLvoid*)g_selection_buffer;
        break;
    default:
        *params = NULL;
        g_error = GL_INVALID_ENUM;
        break;
    }
}

__declspec(dllexport)
GLboolean APIENTRY glIsTexture(GLuint texture) {
    uint32_t i;
    if (!texture) {
        return GL_FALSE;
    }

    for (i = 0; i < V86GL_MAX_TEXTURE_STATES; i++) {
        if (g_texture_states[i].used && g_texture_states[i].name == texture) {
            return GL_TRUE;
        }
    }

    return GL_FALSE;
}

__declspec(dllexport)
GLboolean APIENTRY glAreTexturesResident(GLsizei n, const GLuint* textures, GLboolean* residences) {
    GLsizei i;
    (void)textures;
    if (n < 0) {
        g_error = GL_INVALID_VALUE;
        return GL_FALSE;
    }

    if (residences) {
        for (i = 0; i < n; i++) {
            residences[i] = GL_TRUE;
        }
    }
    return GL_TRUE;
}

__declspec(dllexport)
void APIENTRY glPrioritizeTextures(GLsizei n, const GLuint* textures, const GLclampf* priorities) {
    GLsizei i;
    uint32_t j;
    if (n < 0) {
        g_error = GL_INVALID_VALUE;
        return;
    }

    if (!textures || !priorities) {
        return;
    }

    for (i = 0; i < n; i++) {
        for (j = 0; j < V86GL_MAX_TEXTURE_STATES; j++) {
            if (g_texture_states[j].used && g_texture_states[j].name == textures[i]) {
                g_texture_states[j].priority = priorities[i];
                break;
            }
        }
    }
}

__declspec(dllexport)
void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internalformat,
                           GLsizei width, GLint border,
                           GLenum format, GLenum type, const GLvoid* pixels) {
    (void)level;
    (void)internalformat;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
    if (target != GL_TEXTURE_1D) {
        g_error = GL_INVALID_ENUM;
        return;
    }
    if (width < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                              GLsizei width, GLenum format, GLenum type,
                              const GLvoid* pixels) {
    (void)level;
    (void)xoffset;
    (void)format;
    (void)type;
    (void)pixels;
    if (target != GL_TEXTURE_1D) {
        g_error = GL_INVALID_ENUM;
        return;
    }
    if (width < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalFormat,
                               GLint x, GLint y, GLsizei width, GLint border) {
    (void)level;
    (void)internalFormat;
    (void)x;
    (void)y;
    (void)border;
    if (target != GL_TEXTURE_1D) {
        g_error = GL_INVALID_ENUM;
        return;
    }
    if (width < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                  GLint x, GLint y, GLsizei width) {
    (void)level;
    (void)xoffset;
    (void)x;
    (void)y;
    if (target != GL_TEXTURE_1D) {
        g_error = GL_INVALID_ENUM;
        return;
    }
    if (width < 0) {
        g_error = GL_INVALID_VALUE;
    }
}

__declspec(dllexport)
void APIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble* points) {
    (void)target; (void)u1; (void)u2; (void)stride; (void)points;
    if (order <= 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat* points) {
    (void)target; (void)u1; (void)u2; (void)stride; (void)points;
    if (order <= 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder,
                      GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble* points) {
    (void)target; (void)u1; (void)u2; (void)ustride; (void)v1; (void)v2; (void)vstride; (void)points;
    if (uorder <= 0 || vorder <= 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
                      GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat* points) {
    (void)target; (void)u1; (void)u2; (void)ustride; (void)v1; (void)v2; (void)vstride; (void)points;
    if (uorder <= 0 || vorder <= 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMapGrid1d(GLint un, GLdouble u1, GLdouble u2) {
    (void)u1; (void)u2;
    if (un < 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMapGrid1f(GLint un, GLfloat u1, GLfloat u2) {
    (void)u1; (void)u2;
    if (un < 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2) {
    (void)u1; (void)u2; (void)v1; (void)v2;
    if (un < 0 || vn < 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2) {
    (void)u1; (void)u2; (void)v1; (void)v2;
    if (un < 0 || vn < 0) g_error = GL_INVALID_VALUE;
}

__declspec(dllexport)
void APIENTRY glEvalCoord1d(GLdouble u) {
    (void)u;
}

__declspec(dllexport)
void APIENTRY glEvalCoord1dv(const GLdouble* u) {
    if (u) glEvalCoord1d(u[0]);
}

__declspec(dllexport)
void APIENTRY glEvalCoord1f(GLfloat u) {
    (void)u;
}

__declspec(dllexport)
void APIENTRY glEvalCoord1fv(const GLfloat* u) {
    if (u) glEvalCoord1f(u[0]);
}

__declspec(dllexport)
void APIENTRY glEvalCoord2d(GLdouble u, GLdouble v) {
    (void)u; (void)v;
}

__declspec(dllexport)
void APIENTRY glEvalCoord2dv(const GLdouble* u) {
    if (u) glEvalCoord2d(u[0], u[1]);
}

__declspec(dllexport)
void APIENTRY glEvalCoord2f(GLfloat u, GLfloat v) {
    (void)u; (void)v;
}

__declspec(dllexport)
void APIENTRY glEvalCoord2fv(const GLfloat* u) {
    if (u) glEvalCoord2f(u[0], u[1]);
}

__declspec(dllexport)
void APIENTRY glEvalMesh1(GLenum mode, GLint i1, GLint i2) {
    (void)mode; (void)i1; (void)i2;
}

__declspec(dllexport)
void APIENTRY glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2) {
    (void)mode; (void)i1; (void)i2; (void)j1; (void)j2;
}

__declspec(dllexport)
void APIENTRY glEvalPoint1(GLint i) {
    (void)i;
}

__declspec(dllexport)
void APIENTRY glEvalPoint2(GLint i, GLint j) {
    (void)i; (void)j;
}

__declspec(dllexport)
void APIENTRY glGetMapdv(GLenum target, GLenum query, GLdouble* v) {
    (void)target; (void)query;
    if (v) v[0] = 0.0;
}

__declspec(dllexport)
void APIENTRY glGetMapfv(GLenum target, GLenum query, GLfloat* v) {
    (void)target; (void)query;
    if (v) v[0] = 0.0f;
}

__declspec(dllexport)
void APIENTRY glGetMapiv(GLenum target, GLenum query, GLint* v) {
    (void)target; (void)query;
    if (v) v[0] = 0;
}

#define DEFINE_RASTER_POS_VARIANT(SUFFIX, TYPE) \
__declspec(dllexport) void APIENTRY glRasterPos2##SUFFIX(TYPE x, TYPE y) { (void)x; (void)y; } \
__declspec(dllexport) void APIENTRY glRasterPos2##SUFFIX##v(const TYPE* v) { if (v) glRasterPos2##SUFFIX(v[0], v[1]); } \
__declspec(dllexport) void APIENTRY glRasterPos3##SUFFIX(TYPE x, TYPE y, TYPE z) { (void)x; (void)y; (void)z; } \
__declspec(dllexport) void APIENTRY glRasterPos3##SUFFIX##v(const TYPE* v) { if (v) glRasterPos3##SUFFIX(v[0], v[1], v[2]); } \
__declspec(dllexport) void APIENTRY glRasterPos4##SUFFIX(TYPE x, TYPE y, TYPE z, TYPE w) { (void)x; (void)y; (void)z; (void)w; } \
__declspec(dllexport) void APIENTRY glRasterPos4##SUFFIX##v(const TYPE* v) { if (v) glRasterPos4##SUFFIX(v[0], v[1], v[2], v[3]); }

DEFINE_RASTER_POS_VARIANT(d, GLdouble)
DEFINE_RASTER_POS_VARIANT(f, GLfloat)
DEFINE_RASTER_POS_VARIANT(i, GLint)
DEFINE_RASTER_POS_VARIANT(s, GLshort)

#undef DEFINE_RASTER_POS_VARIANT

typedef struct {
    const char* name;
    PROC proc;
} ProcLookupEntry;

#define PROC_ENTRY(fn) { #fn, (PROC)fn }

__declspec(dllexport)
PROC APIENTRY wglGetProcAddress(LPCSTR name) {
    static const ProcLookupEntry entries[] = {
        PROC_ENTRY(glGetString),
        PROC_ENTRY(glGetError),
        PROC_ENTRY(wglGetExtensionsStringARB),
        PROC_ENTRY(wglGetExtensionsStringEXT),
        PROC_ENTRY(glGetIntegerv),
        PROC_ENTRY(glGetFloatv),
        PROC_ENTRY(glGetDoublev),
        PROC_ENTRY(glGetBooleanv),
        PROC_ENTRY(glIsEnabled),
        PROC_ENTRY(glGetTexParameteriv),
        PROC_ENTRY(glGetTexParameterfv),
        PROC_ENTRY(glLoadMatrixf),
        PROC_ENTRY(glLoadMatrixd),
        PROC_ENTRY(glMultMatrixf),
        PROC_ENTRY(glMultMatrixd),
        PROC_ENTRY(glDepthRange),
        PROC_ENTRY(glClearStencil),
        PROC_ENTRY(glStencilFunc),
        PROC_ENTRY(glStencilMask),
        PROC_ENTRY(glStencilOp),
        PROC_ENTRY(glHint),
        PROC_ENTRY(glPushAttrib),
        PROC_ENTRY(glPopAttrib),
        PROC_ENTRY(glPushClientAttrib),
        PROC_ENTRY(glPopClientAttrib),
        PROC_ENTRY(glDrawBuffer),
        PROC_ENTRY(glReadBuffer),
        PROC_ENTRY(glCopyTexImage2D),
        PROC_ENTRY(glCopyTexSubImage2D),
        PROC_ENTRY(glTexParameteriv),
        PROC_ENTRY(glTexParameterfv),
        PROC_ENTRY(glTexEnviv),
        PROC_ENTRY(glTexEnvfv),
        PROC_ENTRY(glTexGeni),
        PROC_ENTRY(glTexGenf),
        PROC_ENTRY(glTexGend),
        PROC_ENTRY(glTexGeniv),
        PROC_ENTRY(glTexGenfv),
        PROC_ENTRY(glTexGendv),
        PROC_ENTRY(glClipPlane),
        PROC_ENTRY(glColorMaterial),
        PROC_ENTRY(glPolygonOffset),
        PROC_ENTRY(glActiveTexture),
        PROC_ENTRY(glClientActiveTexture),
        PROC_ENTRY(glActiveTextureARB),
        PROC_ENTRY(glClientActiveTextureARB),
        PROC_ENTRY(glMultiTexCoord1dARB),
        PROC_ENTRY(glMultiTexCoord2dARB),
        PROC_ENTRY(glMultiTexCoord3dARB),
        PROC_ENTRY(glMultiTexCoord4dARB),
        PROC_ENTRY(glMultiTexCoord1fARB),
        PROC_ENTRY(glMultiTexCoord2fARB),
        PROC_ENTRY(glMultiTexCoord3fARB),
        PROC_ENTRY(glMultiTexCoord4fARB),
        PROC_ENTRY(glMultiTexCoord1iARB),
        PROC_ENTRY(glMultiTexCoord2iARB),
        PROC_ENTRY(glMultiTexCoord3iARB),
        PROC_ENTRY(glMultiTexCoord4iARB),
        PROC_ENTRY(glMultiTexCoord1sARB),
        PROC_ENTRY(glMultiTexCoord2sARB),
        PROC_ENTRY(glMultiTexCoord3sARB),
        PROC_ENTRY(glMultiTexCoord4sARB),
        PROC_ENTRY(glMultiTexCoord1dvARB),
        PROC_ENTRY(glMultiTexCoord2dvARB),
        PROC_ENTRY(glMultiTexCoord3dvARB),
        PROC_ENTRY(glMultiTexCoord4dvARB),
        PROC_ENTRY(glMultiTexCoord1fvARB),
        PROC_ENTRY(glMultiTexCoord2fvARB),
        PROC_ENTRY(glMultiTexCoord3fvARB),
        PROC_ENTRY(glMultiTexCoord4fvARB),
        PROC_ENTRY(glMultiTexCoord1ivARB),
        PROC_ENTRY(glMultiTexCoord2ivARB),
        PROC_ENTRY(glMultiTexCoord3ivARB),
        PROC_ENTRY(glMultiTexCoord4ivARB),
        PROC_ENTRY(glMultiTexCoord1svARB),
        PROC_ENTRY(glMultiTexCoord2svARB),
        PROC_ENTRY(glMultiTexCoord3svARB),
        PROC_ENTRY(glMultiTexCoord4svARB),
        PROC_ENTRY(glMultiTexCoord1f),
        PROC_ENTRY(glMultiTexCoord2f),
        PROC_ENTRY(glMultiTexCoord3f),
        PROC_ENTRY(glMultiTexCoord4f),
        PROC_ENTRY(glMultiTexCoord1fv),
        PROC_ENTRY(glMultiTexCoord2fv),
        PROC_ENTRY(glMultiTexCoord3fv),
        PROC_ENTRY(glMultiTexCoord4fv),
        PROC_ENTRY(glNormal3fv),
        PROC_ENTRY(glColor4ub),
        PROC_ENTRY(glColor4ubv),
        PROC_ENTRY(glTexCoord2fv),
        PROC_ENTRY(glVertex3fv),
        PROC_ENTRY(glLightf),
        PROC_ENTRY(glLighti),
        PROC_ENTRY(glLightfv),
        PROC_ENTRY(glLightiv),
        PROC_ENTRY(glLightModelf),
        PROC_ENTRY(glLightModeli),
        PROC_ENTRY(glLightModelfv),
        PROC_ENTRY(glLightModeliv),
    };
    uint32_t i;
    PROC exported;

    if (!name) {
        return NULL;
    }

    exported = g_instance ? GetProcAddress((HMODULE)g_instance, name) : NULL;
    if (exported) {
        return exported;
    }

    for (i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (lstrcmpA(name, entries[i].name) == 0) {
            return entries[i].proc;
        }
    }

    return NULL;
}

__declspec(dllexport)
PROC APIENTRY wglGetDefaultProcAddress(LPCSTR name) {
    return wglGetProcAddress(name);
}

#undef PROC_ENTRY
