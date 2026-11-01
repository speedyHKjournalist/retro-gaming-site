#include "runtime/d9wg_vertex.h"
#define TCHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static int tex_near(float value,float wanted)
{ float delta=value-wanted;return delta<.00002f && delta>-.00002f; }
static int tex_run(const D9WGProcessLayout *source_layout,const float *input,
        const D9WGProcessLayout *destination_layout,float *output,
        const float *world_view,const D9WGProcessViewport *viewport,
        const D9WGTNLLighting *lighting,const D9WGVertexState *state,uint32_t copy)
{
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    output[4]=17;output[5]=19;output[6]=23;output[7]=29;
    return d9wg_process_vertices(source_layout,input,40,0,40,0,destination_layout,output,32,0,1,
            world_view,identity,viewport,lighting,copy,state);
}
static int texcoord_cases(unsigned iterations)
{
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float matrix[16]={2,0,0,0,0,.5f,0,0,.25f,.125f,2,3,0,0,0,1};
    float input[10]={0,0,1,0,0,-1,.25f,.75f,.6f,.2f},output[8];
    D9WGProcessLayout source,destination;D9WGVertexState state={0};
    D9WGSetMaterial material={0};D9WGTNLLightingFlags flags={0};D9WGTNLLighting lighting;
    D9WGProcessViewport viewport={0,0,64,64,0,1};
    TCHECK(d9wg_process_fvf(9,0x212,&source) && source.stride==40);
    TCHECK(d9wg_process_fvf(9,0x20104,&destination) && destination.stride==32);
    d9wg_lighting_init(&lighting,&material,0,&flags);d9wg_lighting_geometry(&lighting,identity);
    TCHECK(d9wg_vertex_texture_state(&state,0,1,0,identity));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,1));
    TCHECK(output[4]==.6f && output[5]==.2f && output[6]==23 && output[7]==29);
    TCHECK(d9wg_vertex_texture_state(&state,0,0,2,matrix));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(output[4]==.75f && output[5]==.5f && output[6]==23 && output[7]==29);
    TCHECK(d9wg_vertex_texture_state(&state,0,0,259,matrix));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(output[4]==.75f && output[5]==.5f && output[6]==2 && output[7]==29); /* raw q, not divided */
    TCHECK(d9wg_vertex_texture_state(&state,0,0,4,matrix));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,1));
    TCHECK(output[4]==.75f && output[5]==.5f && output[6]==2 && output[7]==3);
    TCHECK(d9wg_vertex_texture_state(&state,0,0,1,matrix));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(output[4]==.75f && output[5]==19 && output[6]==23 && output[7]==29);
    TCHECK(d9wg_vertex_texture_state(&state,0,0x20000,0,identity));
    float view[16]={1,0,0,0,0,1,0,0,0,0,2,0,.5f,-.25f,0,1};
    TCHECK(tex_run(&source,input,&destination,output,view,&viewport,&lighting,&state,0));
    TCHECK(output[4]==.5f && output[5]==-.25f && output[6]==2 && output[7]==29);
    float scale[16]={2,0,0,0,0,1,0,0,0,0,.5f,0,0,0,0,1};
    input[3]=.6f;input[4]=.8f;input[5]=0;d9wg_lighting_geometry(&lighting,scale);
    TCHECK(d9wg_vertex_texture_state(&state,0,0x10000,0,identity));
    TCHECK(tex_run(&source,input,&destination,output,scale,&viewport,&lighting,&state,0));
    TCHECK(tex_near(output[4],.3f) && tex_near(output[5],.8f) && output[6]==0 && output[7]==29);
    lighting.flags.normalize_normals=1;
    TCHECK(tex_run(&source,input,&destination,output,scale,&viewport,&lighting,&state,0));
    TCHECK(tex_near(output[4],.3511234415883917f) && tex_near(output[5],.9363291775690445f));
    input[3]=input[4]=0;input[5]=-1;d9wg_lighting_geometry(&lighting,identity);
    TCHECK(d9wg_vertex_texture_state(&state,0,0x30000,0,identity));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(output[4]==0 && output[5]==0 && output[6]==-1 && output[7]==29);
    input[0]=1;
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(tex_near(output[4],.7071067811865475f) && tex_near(output[6],-.7071067811865475f));
    TCHECK(d9wg_vertex_texture_state(&state,0,0x40000,0,identity));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(tex_near(output[4],.9619397662556434f) && output[5]==.5f && output[6]==23 && output[7]==29);
    input[0]=0;
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,0));
    TCHECK(output[4]==.5f && output[5]==.5f);
    TCHECK(d9wg_vertex_texture_state(&state,0,7,0,identity));
    TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,1));
    TCHECK(output[4]==17 && output[5]==19 && output[6]==23 && output[7]==29);
    TCHECK(!d9wg_vertex_texture_state(&state,8,0,0,identity));
    TCHECK(!d9wg_vertex_texture_state(&state,0,8,0,identity));
    TCHECK(!d9wg_vertex_texture_state(&state,0,0x50000,0,identity));
    TCHECK(!d9wg_vertex_texture_state(&state,0,0,5,identity));
    uint32_t random=0x713da5c9u;
    for(unsigned i=0;i<iterations;i++) {
        random=random*1664525u+1013904223u;input[6]=(float)(random&65535u)/65535.0f;
        random=random*1664525u+1013904223u;input[7]=(float)(random&65535u)/65535.0f;
        TCHECK(d9wg_vertex_texture_state(&state,0,0,2,matrix));
        TCHECK(tex_run(&source,input,&destination,output,identity,&viewport,&lighting,&state,i&1u));
        TCHECK(tex_near(output[4],2*input[6]+.25f) && tex_near(output[5],.5f*input[7]+.125f));
        TCHECK(output[6]==23 && output[7]==29);
    }
    return 0;
}
#undef TCHECK
