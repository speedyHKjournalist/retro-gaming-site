#include <math.h>
#include "runtime/d9wg_vertex.h"
#define FCHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static int fog_near(float value, float wanted)
{ float delta=value-wanted;return delta<=2e-6f && delta>=-2e-6f; }
static int fog_cases(unsigned iterations)
{
    D9WGVertexState state={3,0,2,10,.25f};float position[3]={0,0,6};
    FCHECK(fog_near(d9wg_vertex_fog(&state,position),.5f));
    position[2]=-6;FCHECK(fog_near(d9wg_vertex_fog(&state,position),.5f));
    position[2]=1;FCHECK(d9wg_vertex_fog(&state,position)==1);
    position[2]=12;FCHECK(d9wg_vertex_fog(&state,position)==0);
    position[0]=3;position[1]=4;position[2]=0;state.range_fog=1;
    FCHECK(fog_near(d9wg_vertex_fog(&state,position),.625f));
    state.range_fog=0;FCHECK(d9wg_vertex_fog(&state,position)==1);
    state.fog_start=10;state.fog_end=2;position[2]=6;
    FCHECK(fog_near(d9wg_vertex_fog(&state,position),.5f));
    state.fog_start=state.fog_end=2;position[2]=1;
    FCHECK(d9wg_vertex_fog(&state,position)==1);position[2]=2;FCHECK(d9wg_vertex_fog(&state,position)==0);
    state.fog_mode=1;position[2]=4;FCHECK(fog_near(d9wg_vertex_fog(&state,position),.36787944117144233f));
    state.fog_mode=2;position[2]=8;FCHECK(fog_near(d9wg_vertex_fog(&state,position),.01831563888873418f));
    state.fog_mode=0;FCHECK(d9wg_vertex_fog(&state,position)==1);
    uint32_t random=0xe2ce53a7u;
    for(unsigned i=0;i<iterations;i++) {
        random=random*1664525u+1013904223u;position[2]=(float)(random&65535u)/2048.0f;
        random=random*1664525u+1013904223u;state.fog_density=(float)(random&65535u)/65535.0f;
        state.fog_mode=i&1u ? 1 : 2;
        float exponent=state.fog_density*position[2];if(state.fog_mode==2)exponent*=exponent;
        FCHECK(fog_near(d9wg_vertex_fog(&state,position),(float)exp(-(double)exponent)));
    }
    return 0;
}
#undef FCHECK
