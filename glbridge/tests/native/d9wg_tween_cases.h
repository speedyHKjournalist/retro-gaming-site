#ifndef D9WG_TWEEN_CASES_H
#define D9WG_TWEEN_CASES_H
#include <stdlib.h>
#include <string.h>
#include "runtime/d9wg_blend.h"
#include "runtime/d9wg_math.h"
#include "runtime/d9wg_clip_draw.h"
#define TCHECK(x) do {if(!(x))return __LINE__;}while(0)
static int tween_cases(unsigned iterations)
{
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float world[16],view[16],projection[16],world_view[16],wvp[16];
    memcpy(world,identity,64);memcpy(view,identity,64);memcpy(projection,identity,64);
    world[0]=2;view[13]=.25f;projection[10]=.5f;
    d9wg_multiply_matrix(world,view,world_view);d9wg_multiply_matrix(world_view,projection,wvp);
    uint32_t bytes=d9wg_blend_bytes(255,1,1);TCHECK(bytes);
    D9WGVertexBlend *blend=malloc(bytes);TCHECK(blend);
    TCHECK(d9wg_blend_init(blend,bytes,255,1,world,1,0,view,projection)&&!blend->indexed&&blend->tween_factor==0);
    D9WGVertexElement elements[]={{0,0,2,0,0,0},{0,12,2,0,3,0},{0,24,2,0,0,1},{0,36,2,0,3,1}};
    D9WGProcessLayout layout;TCHECK(d9wg_process_layout(elements,4,&layout)&&layout.position2==24&&layout.normal2==36&&d9wg_blend_layout(blend,&layout));
    float input[12]={-.5f,0,.25f,-1,0,0,.5f,0,.75f,1,0,0};
    float position[4]={-.5f,0,.25f,1},eye[4],clip[4],normal[3],world_position[4];
    for(unsigned i=0;i<iterations;++i){
        float t=((int)(i%1025u)-256)/512.0f;blend->tween_factor=t;
        d9wg_blend_vertex_world(blend,&layout,input,position,eye,clip,normal,world_position);
        TCHECK(eye[0]==-1+2*t&&eye[1]==.25f&&eye[2]==.25f+.5f*t&&eye[3]==1);
        TCHECK(clip[0]==eye[0]&&clip[1]==.25f&&clip[2]==.125f+.25f*t&&clip[3]==1);
        TCHECK(normal[0]==-.5f+t&&normal[1]==0&&normal[2]==0&&world_position[0]==eye[0]&&world_position[1]==0);
    }
    D9WGSetMaterial material={.diffuse={1,1,1,1}};D9WGTNLLightingFlags flags={.lighting=1};D9WGTNLLighting lighting;
    D9WGSetLight light={.type=3,.diffuse={1,1,1,1},.direction={-1,0,0}};
    d9wg_lighting_init(&lighting,&material,0,&flags);d9wg_lighting_geometry(&lighting,world_view);TCHECK(d9wg_lighting_add(&lighting,&light,view));
    D9WGVertexElement out_elements[]={{0,0,3,0,9,0},{0,16,4,0,10,0},{0,20,4,0,10,1},{0,24,2,0,5,0},{0,36,2,0,5,1}};
    D9WGProcessLayout destination;TCHECK(d9wg_process_layout(out_elements,5,&destination)&&destination.stride==48);
    D9WGProcessViewport viewport={0,0,64,64,0,1};D9WGVertexState state={.blend=blend,.fog_mode=3,.fog_end=1};
    TCHECK(d9wg_vertex_texture_state(&state,0,0x20000,0,identity)&&d9wg_vertex_texture_state(&state,1,0x10000,0,identity));
    uint8_t output[96];memset(output,0xa5,sizeof(output));blend->tween_factor=.75f;
    TCHECK(d9wg_process_vertices(&layout,input,sizeof(input),0,sizeof(input),0,&destination,output,sizeof(output),0,1,world_view,wvp,&viewport,&lighting,0,&state));
    float values[12];memcpy(values,output,48);uint32_t color,specular;memcpy(&color,output+16,4);memcpy(&specular,output+20,4);
    TCHECK(values[0]==48&&values[1]==24&&values[2]==.3125f&&values[3]==1&&color==0xff404040&&(specular>>24)==96);
    TCHECK(values[6]==.5f&&values[7]==.25f&&values[8]==.625f&&values[9]==.25f&&values[10]==0&&values[11]==0);
    lighting.flags.normalize_normals=1;
    TCHECK(d9wg_process_vertices(&layout,input,sizeof(input),0,sizeof(input),0,&destination,output,sizeof(output),0,1,world_view,wvp,&viewport,&lighting,0,&state));
    memcpy(&color,output+16,4);memcpy(values,output,48);TCHECK(color==0xffffffff&&values[9]==1);
    D9WGClipVertex record;state.clip_vertices=&record;state.clip_guard_band[0]=state.clip_guard_band[1]=0;state.clip_guard_band[2]=state.clip_guard_band[3]=64;
    float planes[6][4]={{1,0,0,-.25f}};d9wg_vertex_clip_planes_init(&state,1,planes,world);
    TCHECK(d9wg_process_vertices(&layout,input,sizeof(input),0,sizeof(input),0,&destination,output,sizeof(output),0,1,world_view,wvp,&viewport,&lighting,0,&state)&&record.user_distance[0]==.25f&&!(record.code&64));
    blend->tween_factor=.25f;
    TCHECK(d9wg_process_vertices(&layout,input,sizeof(input),0,sizeof(input),0,&destination,output,sizeof(output),0,1,world_view,wvp,&viewport,&lighting,0,&state)&&record.user_distance[0]==-.75f&&(record.code&64));
    state.clip_vertices=NULL;state.clip_plane_mask=0;blend->tween_factor=.75f;
    D9WGProcessLayout invalid=layout;invalid.position2=invalid.normal2=-1;memset(output,0xa5,sizeof(output));
    TCHECK(!d9wg_blend_layout(blend,&invalid)&&!d9wg_process_vertices(&invalid,input,sizeof(input),0,sizeof(input),0,&destination,output,sizeof(output),0,1,world_view,wvp,&viewport,&lighting,1,&state));
    for(unsigned i=0;i<sizeof(output);++i)TCHECK(output[i]==0xa5);
    invalid=layout;invalid.position2=-1;TCHECK(d9wg_blend_layout(blend,&invalid));
    d9wg_blend_vertex(blend,&invalid,input,position,eye,clip,normal);TCHECK(eye[0]==-1&&normal[0]==.25f);
    invalid=layout;invalid.normal2=-1;TCHECK(d9wg_blend_layout(blend,&invalid));
    d9wg_blend_vertex(blend,&invalid,input,position,eye,clip,normal);TCHECK(eye[0]==.5f&&normal[0]==-.5f);
    D9WGVertexElement duplicate[5];memcpy(duplicate,elements,sizeof(elements));duplicate[4]=elements[2];TCHECK(!d9wg_process_layout(duplicate,5,&invalid));
    uint8_t packed[48];memcpy(packed,input,48);const uint16_t half[4]={0x3800,0,0x3a00,0x3c00};const int16_t snorm[4]={32767,0,0,0};memcpy(packed+24,half,8);memcpy(packed+36,snorm,8);
    elements[2].type=16;elements[3].type=10;TCHECK(d9wg_process_layout(elements,4,&invalid));
    d9wg_blend_vertex(blend,&invalid,packed,position,eye,clip,normal);TCHECK(eye[0]==.5f&&eye[2]==.625f&&normal[0]==.25f);
    elements[2].type=elements[3].type=2;elements[2].stream=elements[3].stream=1;elements[2].offset=0;elements[3].offset=12;
    D9WGProcessStreamLayout stream_layout;TCHECK(d9wg_process_stream_layout(elements,4,&stream_layout));
    float primary[12]={-.5f,0,.25f,-1,0,0,-.25f,0,.25f,-1,0,0};
    D9WGVertexStream streams[16]={{(void*)primary,sizeof(primary),0,24},{(void*)(input+6),24,0,0}};
    TCHECK(d9wg_process_vertices_streams(&stream_layout,streams,0,&destination,output,sizeof(output),0,2,world_view,wvp,&viewport,&lighting,0,&state));
    float x;memcpy(&x,output,4);TCHECK(x==48);memcpy(&x,output+48,4);TCHECK(x==52);
    streams[1].length=23;memset(output,0xa5,sizeof(output));TCHECK(!d9wg_process_vertices_streams(&stream_layout,streams,0,&destination,output,sizeof(output),0,2,world_view,wvp,&viewport,&lighting,1,&state));for(unsigned i=0;i<sizeof(output);++i)TCHECK(output[i]==0xa5);
    streams[1].length=24;streams[0].stride=0;streams[0].length=24;
    TCHECK(d9wg_process_vertices_streams(&stream_layout,streams,0xffffffffu,&destination,output,sizeof(output),0,2,world_view,wvp,&viewport,&lighting,0,&state));
    memcpy(&x,output+48,4);TCHECK(x==48);
    input[6]=1;blend->tween_factor=1;D9WGClipDrawSource draw={.count=2};D9WGClipStatus status={0,0xffffffffu};
    TCHECK(d9wg_vertex_stream_clip_codes(&stream_layout,streams,&draw,wvp,&state,&status)&&status.clip_union==2&&status.clip_intersection==2);
    free(blend);return 0;
}
#undef TCHECK
#endif
