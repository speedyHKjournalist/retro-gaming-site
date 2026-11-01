#include <stdlib.h>
#include "runtime/d9wg_blend.h"
#define BCHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static void blend_word(void *to,uint32_t value) { __builtin_memcpy(to,&value,4); }
static void blend_float(void *to,float value) { __builtin_memcpy(to,&value,4); }
static int blend_near(float value,float expected)
{ float delta=value-expected;return delta<.0002f && delta>-.0002f; }
static int blend_cases(unsigned iterations)
{
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    uint32_t bytes=d9wg_blend_bytes(3,1,256);
#ifdef _WIN32
    uint8_t *storage=HeapAlloc(GetProcessHeap(),0,bytes+256u*64u);
#else
    uint8_t *storage=malloc(bytes+256u*64u);
#endif
    BCHECK(storage!=NULL);
    D9WGVertexBlend *blend=(D9WGVertexBlend *)storage;
    float (*world)[16]=(float (*)[16])(storage+bytes);
    for(unsigned m=0;m<256;m++) {
        for(unsigned i=0;i<16;i++)world[m][i]=identity[i];
        world[m][12]=(float)m*2;
    }
    world[0][0]=2;world[0][12]=1;world[1][0]=4;world[1][12]=5;
    world[2][0]=8;world[2][12]=9;world[3][0]=16;world[3][12]=13;
    D9WGProcessLayout layout;uint8_t input[64]={0};uint32_t indices[4];
    float position[4]={1,0,0,1},eye[4],clip[4],normal[3];
    BCHECK(d9wg_process_fvf(9,0x1a,&layout)); /* XYZB3 + NORMAL */
    blend_float(input+24,1);blend_float(input+12,.25f);blend_float(input+16,.25f);blend_float(input+20,.25f);
    BCHECK(d9wg_blend_init(blend,bytes,1,0,world,256,0,identity,identity));
    BCHECK(d9wg_blend_layout(blend,&layout) && d9wg_blend_indices(blend,&layout,input,indices));
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==7.5f && clip[0]==7.5f && normal[0]==.3125f && normal[1]==0);
    BCHECK(d9wg_blend_init(blend,bytes,2,0,world,256,0,identity,identity));
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==11.5f && normal[0]==.25f);
    blend_float(input+12,.125f);blend_float(input+16,.125f);blend_float(input+20,.25f);
    BCHECK(d9wg_blend_init(blend,bytes,3,0,world,256,0,identity,identity));
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==20.25f && normal[0]==.15625f);
    BCHECK(d9wg_process_fvf(9,0x1018,&layout)); /* XYZB2 + UBYTE4 + NORMAL */
    blend_float(input+12,.25f);blend_word(input+16,0x00000201u);
    blend_float(input+20,1);blend_float(input+24,0);blend_float(input+28,0);
    BCHECK(d9wg_blend_init(blend,bytes,1,1,world,256,0,identity,identity));
    BCHECK(d9wg_blend_layout(blend,&layout) && d9wg_blend_indices(blend,&layout,input,indices));
    BCHECK(indices[0]==1 && indices[1]==2);
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==15 && normal[0]==.15625f);
    BCHECK(d9wg_process_fvf(9,0x801a,&layout)); /* Same indices in D3DCOLOR RGBA order. */
    blend_float(input+16,0);blend_word(input+20,0x00010203u);
    blend_float(input+24,1);blend_float(input+28,0);blend_float(input+32,0);
    BCHECK(d9wg_blend_indices(blend,&layout,input,indices) && indices[0]==1 && indices[1]==2);
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==15 && normal[0]==.15625f);
    /* Native XYZB2/LASTBETA_D3DCOLOR stores packed RGBA weights, then
     * UBYTE4 indices; it is not a FLOAT1 weight with color indices. */
    BCHECK(d9wg_process_fvf(9,0x8018,&layout) && layout.blend_weight_type==4 && layout.blend_indices_type==5);
    blend_word(input+12,0x00800000u);blend_word(input+16,0x00000201u);
    blend_float(input+20,1);blend_float(input+24,0);blend_float(input+28,0);
    BCHECK(d9wg_blend_layout(blend,&layout));
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(blend_near(eye[0],17-8*(128.0f/255.0f)) && blend_near(normal[0],.125f+.125f*(128.0f/255.0f)));
    BCHECK(d9wg_blend_init(blend,bytes,1,1,world,2,0,identity,identity));
    BCHECK(!d9wg_blend_indices(blend,&layout,input,indices));
    BCHECK(d9wg_process_fvf(9,0x1016,&layout)); /* 0WEIGHTS, one palette index. */
    blend_word(input+12,255);blend_float(input+16,1);blend_float(input+20,0);blend_float(input+24,0);
    BCHECK(d9wg_blend_init(blend,bytes,256,1,world,256,0,identity,identity));
    BCHECK(d9wg_blend_layout(blend,&layout) && d9wg_blend_indices(blend,&layout,input,indices));
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==511 && normal[0]==1 && blend->matrix[255].ready);
    BCHECK(d9wg_process_fvf(9,0x1e,&layout) && layout.blend_indices_type==0);
    blend_float(input+12,.25f);blend_float(input+16,.25f);blend_float(input+20,.25f);blend_float(input+24,0);
    blend_float(input+28,2);blend_float(input+32,1);blend_float(input+36,0);blend_float(input+40,0);
    BCHECK(d9wg_blend_init(blend,bytes,3,1,world,256,0,identity,identity));
    BCHECK(d9wg_blend_layout(blend,&layout) && d9wg_blend_indices(blend,&layout,input,indices));
    BCHECK(indices[0]==2 && indices[1]==0 && indices[2]==0 && indices[3]==1);
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==8 && normal[0]==.34375f);
    blend_float(input+28,-1);BCHECK(!d9wg_blend_indices(blend,&layout,input,indices));
    BCHECK(!d9wg_blend_bytes(0,0,256) && !d9wg_blend_bytes(255,0,256));
    BCHECK(!d9wg_blend_init(blend,1,1,0,world,256,0,identity,identity));
    BCHECK(d9wg_process_fvf(9,2,&layout) && !d9wg_blend_layout(blend,&layout));
    /* D3D7 has VIEW/PROJECTION between its first and second world matrices. */
    world[1][0]=99;world[1][12]=99;world[2][0]=101;world[2][12]=101;
    world[3][0]=4;world[3][12]=5;
    BCHECK(d9wg_process_fvf(7,0x16,&layout)); /* XYZB1 + NORMAL */
    blend_float(input+12,.25f);blend_float(input+16,1);blend_float(input+20,0);blend_float(input+24,0);
    BCHECK(d9wg_blend_init(blend,bytes,1,0,world,4,2,identity,identity));
    d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
    BCHECK(eye[0]==7.5f && normal[0]==.3125f);
    world[1][0]=4;world[1][12]=5;world[2][0]=8;world[2][12]=9;
    /* Exercise the integrated vertex kernel, including generated attributes
     * and all-or-nothing rejection of an invalid second palette index. */
    BCHECK(d9wg_process_fvf(9,0x1a,&layout));
    blend_float(input,1);blend_float(input+4,0);blend_float(input+8,0);
    blend_float(input+12,.25f);blend_float(input+16,.25f);blend_float(input+20,.25f);
    blend_float(input+24,1);blend_float(input+28,0);blend_float(input+32,0);
    BCHECK(d9wg_blend_init(blend,bytes,1,0,world,256,0,identity,identity));
    D9WGSetMaterial material={.diffuse={1,.5f,.25f,.75f}};
    D9WGTNLLightingFlags lighting_flags={.lighting=1};D9WGTNLLighting lighting;
    D9WGSetLight light={.type=3,.diffuse={1,1,1,1},.direction={-1,0,0}};
    d9wg_lighting_init(&lighting,&material,0,&lighting_flags);d9wg_lighting_geometry(&lighting,identity);
    BCHECK(d9wg_lighting_add(&lighting,&light,identity));
    D9WGVertexState state={.blend=blend};D9WGProcessLayout destination;
    D9WGProcessViewport viewport={0,0,64,64,0,1};uint8_t result[80];
    BCHECK(d9wg_process_fvf(9,0x201c4,&destination) && destination.stride==40);
    BCHECK(d9wg_vertex_texture_state(&state,0,0x10000,0,identity));
    for(unsigned i=0;i<80;i++)result[i]=0;
    blend_float(result+36,29);
    BCHECK(d9wg_process_vertices(&layout,input,36,0,36,0,&destination,result,40,0,1,
            identity,identity,&viewport,&lighting,0,&state));
    uint32_t color;float texture_normal;
    __builtin_memcpy(&color,result+16,4);__builtin_memcpy(&texture_normal,result+24,4);
    BCHECK(color==0xbf502814u && texture_normal==.3125f);
    lighting.flags.normalize_normals=1;
    BCHECK(d9wg_process_vertices(&layout,input,36,0,36,0,&destination,result,40,0,1,
            identity,identity,&viewport,&lighting,0,&state));
    __builtin_memcpy(&color,result+16,4);__builtin_memcpy(&texture_normal,result+24,4);
    BCHECK(color==0xbfff8040u && texture_normal==1);
    world[1][14]=4;state.fog_mode=3;state.fog_start=0;state.fog_end=4;
    BCHECK(d9wg_vertex_texture_state(&state,0,0x20000,0,identity));
    BCHECK(d9wg_blend_init(blend,bytes,1,0,world,256,0,identity,identity));
    BCHECK(d9wg_process_vertices(&layout,input,36,0,36,0,&destination,result,40,0,1,
            identity,identity,&viewport,&lighting,0,&state));
    __builtin_memcpy(&color,result+20,4);__builtin_memcpy(&texture_normal,result+32,4);
    BCHECK((color>>24)==64 && texture_normal==3);
    world[1][14]=0;state.fog_mode=0;
    BCHECK(d9wg_process_fvf(9,0x1018,&layout));
    for(unsigned i=0;i<64;i++)input[i]=0;
    blend_float(input+12,.25f);blend_word(input+16,0x00000100u);
    blend_float(input+44,.25f);blend_word(input+48,0x00000200u);
    BCHECK(d9wg_blend_init(blend,bytes,1,1,world,2,0,identity,identity));
    for(unsigned i=0;i<80;i++)result[i]=0xa5;
    BCHECK(!d9wg_process_vertices(&layout,input,64,0,32,0,&destination,result,80,0,2,
            identity,identity,&viewport,&lighting,1,&state));
    for(unsigned i=0;i<80;i++)BCHECK(result[i]==0xa5);
    for(unsigned m=0;m<256;m++) {world[m][0]=1;world[m][12]=(float)m*2;}
    BCHECK(d9wg_process_fvf(9,0x1018,&layout));
    BCHECK(d9wg_blend_init(blend,bytes,1,1,world,256,0,identity,identity));
    blend_float(input+20,1);blend_float(input+24,0);blend_float(input+28,0);
    uint32_t random=0xc9b713e5u;
    for(unsigned i=0;i<iterations;i++) {
        random=random*1664525u+1013904223u;unsigned a=random&255u,b=random>>8&255u;
        float weight=(float)(random>>16&255u)/256.0f;
        blend_float(input+12,weight);blend_word(input+16,a|(b<<8));
        BCHECK(d9wg_blend_indices(blend,&layout,input,indices));
        d9wg_blend_vertex(blend,&layout,input,position,eye,clip,normal);
        BCHECK(blend_near(eye[0],1+2*(weight*a+(1-weight)*b)) && normal[0]==1 && eye[3]==1);
    }
#ifdef _WIN32
    HeapFree(GetProcessHeap(),0,storage);
#else
    free(storage);
#endif
    return 0;
}
#undef BCHECK
