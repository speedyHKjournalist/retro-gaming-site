#include <stdint.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_math.h"
#define VCHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static uint32_t vertex_random = 0xb71327a9u;
static uint32_t vrandom(void)
{ vertex_random = vertex_random*1664525u + 1013904223u;return vertex_random; }
static void vword(void *to, uint32_t value) { __builtin_memcpy(to,&value,4u); }
static uint32_t vread(const void *from) { uint32_t value;__builtin_memcpy(&value,from,4u);return value; }
static void vfloat(void *to, float value) { __builtin_memcpy(to,&value,4u); }
static float vfread(const void *from) { float value;__builtin_memcpy(&value,from,4u);return value; }
static int vnear(float value, float wanted) { float delta=value-wanted;return delta < .0001f && delta > -.0001f; }
static __attribute__((noinline)) int vertex_cases(unsigned iterations)
{
    uint32_t first;
    for (unsigned i=0;i<iterations;++i) {
        uint32_t length=vrandom(),base=vrandom(),stride=(vrandom()&1023u)+1u;
        uint32_t extent=vrandom()%stride+1u,start=vrandom(),count=vrandom();
        if(i&1u) { base=length ? vrandom()%length : 0;start=vrandom()%100;count=vrandom()%100; }
        /* All 32-bit values are exactly representable; a product beyond 2^53
         * is already far larger than the maximum possible 32-bit buffer. */
        double begin=(double)base+(double)start*stride;
        double end=begin+((double)count-1.0)*stride+extent;
        int expected=count && base<=length && begin<=length && end<=length;
        int got=d9wg_process_range(length,base,stride,extent,start,count,&first);
        VCHECK(got==expected);if(got)VCHECK(first==(uint32_t)begin);
    }
    VCHECK(!d9wg_process_range(512,0,24,24,0xffffffffu,2,&first));
    VCHECK(!d9wg_process_range(512,0,0xffffffffu,24,1,0xffffffffu,&first));
    VCHECK(d9wg_process_range(44,3,61,41,0,1,&first) && first==3);
    VCHECK(!d9wg_process_range(43,3,61,41,0,1,&first));
    VCHECK(!d9wg_process_range(100,0,0,4,0,1,&first));
    VCHECK(!d9wg_process_range(100,0,4,8,0,1,&first));
    D9WGProcessLayout source,destination;
    /* XYZ,NORMAL,PSIZE,DIFFUSE,SPECULAR,TEX1(float2). Output has TEX2
     * (float4,float1), so unwritten destination components must survive. */
    VCHECK(d9wg_process_fvf(9,0x1f2u,&source));
    VCHECK(d9wg_process_fvf(9,0x000e02e4u,&destination));
    VCHECK(source.stride==44 && destination.stride==48);
    uint8_t input[512],output[512];
    for (unsigned i=0;i<512;++i) {input[i]=0x63;output[i]=0xa5;}
    uint8_t *v=input+64; /* base3 + source index1 * stride61 */
    vfloat(v,.2f);vfloat(v+4,-.3f);vfloat(v+8,.1f);
    vfloat(v+12,0);vfloat(v+16,0);vfloat(v+20,-1);vfloat(v+24,7.5f);
    vword(v+28,0xcc336699u);vword(v+32,0x596633ccu);vfloat(v+36,.25f);vfloat(v+40,.75f);
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float transform[16]={2,0,0,0,0,3,0,0,0,0,4,0,.25f,-.25f,.5f,2};
    D9WGProcessViewport viewport={7,11,100,80,.2f,.8f};
    D9WGSetMaterial material={0};D9WGTNLLightingFlags flags={0};D9WGTNLLighting lighting;
    d9wg_lighting_init(&lighting,&material,0,&flags);d9wg_lighting_geometry(&lighting,identity);
    VCHECK(d9wg_process_vertices(&source,input,108,3,61,1,&destination,output,512,1,1,
            identity,transform,&viewport,&lighting,1,0));
    uint8_t *out=output+48;
    VCHECK(vnear(vfread(out),73.25f) && vnear(vfread(out+4),74) && vnear(vfread(out+8),.47f) && vfread(out+12)==.5f);
    VCHECK(vfread(out+16)==7.5f && vread(out+20)==0xcc336699u && vread(out+24)==0x596633ccu);
    VCHECK(vfread(out+28)==.25f && vfread(out+32)==.75f);
    for(unsigned i=36;i<48;++i)VCHECK(out[i]==0xa5);
    for(unsigned i=0;i<48;++i)VCHECK(output[i]==0xa5);
    for(unsigned i=96;i<512;++i)VCHECK(output[i]==0xa5);
    for(unsigned i=0;i<512;++i)output[i]=0xa5;
    VCHECK(d9wg_process_vertices(&source,input,108,3,61,1,&destination,output,512,1,1,
            identity,transform,&viewport,&lighting,0,0));
    for(unsigned i=16;i<48;++i)VCHECK(out[i]==0xa5);
    flags.lighting=1;material.emissive[0]=.5f;material.diffuse[3]=.75f;
    d9wg_lighting_init(&lighting,&material,0,&flags);d9wg_lighting_geometry(&lighting,identity);
    VCHECK(d9wg_process_vertices(&source,input,108,3,61,1,&destination,output,512,1,1,
            identity,transform,&viewport,&lighting,0,0));
    VCHECK(vread(out+20)==0xbf800000u && vread(out+24)==0x59000000u);
    VCHECK(vread(out+16)==0xa5a5a5a5u && vread(out+28)==0xa5a5a5a5u);
    D9WGVertexState fog={.fog_mode=3,.fog_end=.2f,.fog_density=1};lighting.flags.lighting=0;
    vword(out+24,0x12345678u);
    VCHECK(d9wg_process_vertices(&source,input,108,3,61,1,&destination,output,512,1,1,
            identity,transform,&viewport,&lighting,0,&fog));
    VCHECK(vread(out+24)==0x80345678u && vread(out+20)==0xbf800000u && vread(out+28)==0xa5a5a5a5u);
    for(unsigned i=0;i<512;++i)output[i]=0xa5;
    VCHECK(!d9wg_process_vertices(&source,input,107,3,61,1,&destination,output,512,1,1,
            identity,transform,&viewport,&lighting,1,0));
    for(unsigned i=0;i<512;++i)VCHECK(output[i]==0xa5);
    D9WGVertexElement elements[]={{0,0,3,0,0,0}};
    VCHECK(d9wg_process_layout(elements,1,&source) && source.position_floats==4);
    VCHECK(d9wg_process_fvf(9,4,&destination));
    float xyzw[4]={1e-8f,0,0,1e-8f};
    VCHECK(d9wg_process_vertices(&source,xyzw,16,0,16,0,&destination,output,16,0,1,
            identity,identity,&viewport,&lighting,1,0));
    VCHECK(vnear(vfread(output),107) && vfread(output+12)==100000000.0f);
    elements[0].stream=1;VCHECK(!d9wg_process_layout(elements,1,&source));
    elements[0].stream=0;elements[0].method=1;VCHECK(!d9wg_process_layout(elements,1,&source));
    elements[0].method=0;elements[0].type=4;VCHECK(d9wg_process_layout(elements,1,&source) && source.position_type==4);
    return 0;
}
#undef VCHECK
