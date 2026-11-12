#include "d9wg_lighting_profiles.h"
#include "d9wg_tnl_math.h"
static float word(const void *matrix, unsigned index)
{
    float value;__builtin_memcpy(&value,(const unsigned char*)matrix+index*4u,4u);return value;
}
int d9wg_inverse_transpose4(const void *matrix, float *output)
{
    double rows[4][8];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<8;++c)
        rows[r][c]=c<4?word(matrix,r*4u+c):(double)(c-4u==r);
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r){
            double a=rows[r][c]<0?-rows[r][c]:rows[r][c];
            double b=rows[pivot][c]<0?-rows[pivot][c]:rows[pivot][c];
            if(a>b)pivot=r;
        }
        double divisor=rows[pivot][c];
        if(!divisor||!__builtin_isfinite(divisor))return 0;
        if(pivot!=c)for(unsigned k=0;k<8;++k){double t=rows[c][k];rows[c][k]=rows[pivot][k];rows[pivot][k]=t;}
        for(unsigned k=0;k<8;++k)rows[c][k]/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=c){
            double scale=rows[r][c];
            for(unsigned k=0;k<8;++k)rows[r][k]-=scale*rows[c][k];
        }
    }
    float result[16];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        result[r*4u+c]=(float)rows[c][4u+r];
        if(!__builtin_isfinite(result[r*4u+c]))return 0;
    }
    __builtin_memcpy(output,result,sizeof(result));return 1;
}
void d9wg_lighting_legacy_geometry(D9WGTNLLighting *state,
        const void *world, const void *view, float *light_transform)
{
    float inverse[16],eye[3]={0,0,0};
    if(d9wg_inverse_transpose4(view,inverse)&&inverse[15]!=0.0f)
        for(unsigned i=0;i<3;++i)eye[i]=inverse[i*4u+3u]/inverse[15];
    for(unsigned i=0;i<16;++i){
        state->normal_matrix[i]=0.0f;
        state->position_matrix[i]=word(world,i);
        light_transform[i]=(i%5u)==0u?1.0f:0.0f;
    }
    state->normal_matrix[15]=state->position_matrix[15]=1.0f;
    for(unsigned row=0;row<3;++row){
        float basis[3];
        for(unsigned c=0;c<3;++c)basis[c]=word(world,row*4u+c);
        d9wg_tnl_normalize3(basis,0.0f);
        for(unsigned c=0;c<3;++c)state->normal_matrix[row*4u+c]=basis[c];
        state->position_matrix[row*4u+3u]=0.0f;
        state->position_matrix[12u+row]-=eye[row];
        light_transform[12u+row]=-eye[row];
    }
    state->position_matrix_override=1;
    state->flags.normalize_normals=0;
}
static int rigid(const void *matrix)
{
    if(word(matrix,3)||word(matrix,7)||word(matrix,11)||word(matrix,15)!=1.0f)return 0;
    /* Only exact rigid matrices use the shortcut. Near-rigid matrices still
     * receive their mathematically valid inverse, without an epsilon policy. */
    for(unsigned r=0;r<3;++r)for(unsigned c=r;c<3;++c){
        double product=0;
        for(unsigned k=0;k<3;++k)product+=(double)word(matrix,r*4u+k)*word(matrix,c*4u+k);
        if(product!=(double)(r==c))return 0;
    }
    return 1;
}
int d9wg_lighting_process7_normal(D9WGTNLLighting *state,
        const void *world_view, const float *previous)
{
    if(!state->flags.normalize_normals&&rigid(world_view)){
        for(unsigned i=0;i<16;++i)state->normal_matrix[i]=word(world_view,i);
        return 0;
    }
    if(d9wg_inverse_transpose4(world_view,state->normal_matrix))return 1;
    __builtin_memcpy(state->normal_matrix,previous,16u*4u);return 0;
}
