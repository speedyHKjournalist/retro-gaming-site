#include "d9wg_clip_draw.h"
#include "d9wg_math.h"
static float clip_read_float(const void *p)
{ float value;__builtin_memcpy(&value,p,4);return value; }
static void clip_write_float(void *p,float value)
{ __builtin_memcpy(p,&value,4); }
static uint32_t clip_read_index(const D9WGClipDrawSource *s,uint32_t i)
{
    uint32_t index=0;
    if(s->indices)__builtin_memcpy(&index,s->indices+(uint64_t)i*s->index_stride,s->index_stride);
    else index=i;
    return index;
}
static int clip_vertex_offset(const D9WGClipDrawSource *s,uint32_t i,uint32_t *offset)
{
    uint32_t index=clip_read_index(s,i);
    int64_t vertex=(int64_t)s->first_vertex+s->base_vertex+index;
    if(index>=s->vertex_count || vertex<0)return 0;
    uint64_t at=(uint64_t)s->base+(uint64_t)vertex*s->stride;
    if(at>s->length || s->layout->stride>s->length-at)return 0;
    *offset=(uint32_t)at;return 1;
}
static void clip_color(const uint8_t *source,int32_t offset,float *out,uint32_t fallback)
{
    uint32_t color=fallback;if(offset>=0)__builtin_memcpy(&color,source+offset,4);
    out[0]=(float)(color>>16&255)/255;out[1]=(float)(color>>8&255)/255;
    out[2]=(float)(color&255)/255;out[3]=(float)(color>>24)/255;
}
static void clip_load_point(const D9WGClipDrawSource *s,uint32_t offset,D9WGClipPoint *point)
{
    const D9WGProcessLayout *layout=s->layout;
    const uint8_t *data=s->vertices+offset;
    const D9WGProcessViewport *processed_vp=0;
    const D9WGClipVertex *record=d9wg_clip_buffer_lookup(s->clips,offset+layout->position,
            data+layout->position,&processed_vp);
    float *out=point->data;
    for(unsigned i=0;i<D9WG_CLIP_MAX_FLOATS;++i)out[i]=0;
    if(record){
        /* Rewrite only the X/Y clip basis when the draw viewport changed.
         * These algebraic forms remain valid at W=0 and keep original depth
         * clipping independent of already mapped screen depth. */
        float x_scale=processed_vp->width/s->viewport.width;
        float y_scale=processed_vp->height/s->viewport.height;
        out[3]=record->clip[3];out[2]=record->clip[2];
        out[0]=record->clip[0]*x_scale+out[3]*(x_scale-1+
                2*(processed_vp->x-s->viewport.x)/s->viewport.width);
        out[1]=record->clip[1]*y_scale+out[3]*(1-y_scale-
                2*(processed_vp->y-s->viewport.y)/s->viewport.height);
        out[4]=processed_vp->min_z*out[3]+out[2]*(processed_vp->max_z-processed_vp->min_z);
    }else{
        float rhw=clip_read_float(data+layout->position+12);
        out[3]=rhw!=0?1/rhw:1;
        out[0]=((clip_read_float(data+layout->position)-s->viewport.x)*2/s->viewport.width-1)*out[3];
        out[1]=(1-(clip_read_float(data+layout->position+4)-s->viewport.y)*2/s->viewport.height)*out[3];
        out[2]=out[4]=clip_read_float(data+layout->position+8)*out[3];
    }
    clip_color(data,layout->diffuse,out+5,0xffffffffu);
    clip_color(data,layout->specular,out+9,0);
    if(layout->psize>=0)out[13]=clip_read_float(data+layout->psize);
    if(layout->normal>=0)for(unsigned i=0;i<3;++i)out[14+i]=clip_read_float(data+layout->normal+i*4u);
    for(unsigned t=0;t<8;++t)if(layout->texcoord[t]>=0)
        for(unsigned i=0;i<layout->texcoord_floats[t];++i)out[17+t*4+i]=clip_read_float(data+layout->texcoord[t]+i*4u);
}
static void clip_pack_point(const D9WGClipDrawSource *s,const D9WGClipPoint *point,uint8_t *output)
{
    const D9WGProcessLayout *layout=s->layout;const float *in=point->data;
    D9WGProcessViewport vp=s->viewport;float position[4];vp.min_z=0;vp.max_z=1;
    d9wg_clip_project(in,&vp,position);position[2]=in[4]/in[3];
    for(uint32_t i=0;i<s->stride;++i)output[i]=0;
    for(unsigned i=0;i<4;++i)clip_write_float(output+layout->position+i*4u,position[i]);
    if(layout->diffuse>=0){uint32_t c=d9wg_pack_color(in+5);__builtin_memcpy(output+layout->diffuse,&c,4);}
    if(layout->specular>=0){uint32_t c=d9wg_pack_color(in+9);__builtin_memcpy(output+layout->specular,&c,4);}
    if(layout->psize>=0)clip_write_float(output+layout->psize,in[13]);
    if(layout->normal>=0)for(unsigned i=0;i<3;++i)clip_write_float(output+layout->normal+i*4u,in[14+i]);
    for(unsigned t=0;t<8;++t)if(layout->texcoord[t]>=0)
        for(unsigned i=0;i<layout->texcoord_floats[t];++i)clip_write_float(output+layout->texcoord[t]+i*4u,in[17+t*4+i]);
}
int d9wg_clip_draw(const D9WGClipDrawSource *s,D9WGClipWork *work,
        void *staging,uint32_t staging_bytes,D9WGClipEmit emit,void *context)
{
    if(!s || !s->clips || !s->clips->head)return 0;
    if(!s->vertices || !s->layout || !s->layout->position_transformed ||
            s->layout->position<0 || s->layout->position_floats!=4 || !s->count ||
            !s->stride || s->layout->stride>s->stride || !s->viewport.width || !s->viewport.height ||
            !work || !staging || !emit || s->primitive_type<1 || s->primitive_type>6 ||
            (s->primitive_type==2 && s->count%2u) || (s->primitive_type==3 && s->count<2) ||
            (s->primitive_type==4 && s->count%3u) || (s->primitive_type>4 && s->count<3) ||
            (s->indices && ((s->index_stride!=2 && s->index_stride!=4) || s->count>s->index_bytes/s->index_stride)))return -1;
    uint32_t capacity=staging_bytes/s->stride,queued=0;
    uint32_t primitives=s->primitive_type==1?s->count:s->primitive_type==2?s->count/2u:
            s->primitive_type==3?s->count-1u:s->primitive_type==4?s->count/3u:s->count-2u;
    uint32_t emitted_type=s->primitive_type==1?1:s->primitive_type<4?2:4;
    if(capacity<21)return -1;
    unsigned live=0;
    for(uint32_t i=0;i<s->count;++i){
        uint32_t at;if(!clip_vertex_offset(s,i,&at))return -1;
        const D9WGProcessViewport *vp=0;
        live|=d9wg_clip_buffer_lookup(s->clips,at+s->layout->position,
                s->vertices+at+s->layout->position,&vp)!=0;
    }
    if(!live)return 0;
    for(uint32_t triangle=0;triangle<primitives;++triangle){
        uint32_t indices[3];D9WGClipPoint input[3];const D9WGClipPoint *polygon;uint32_t count;
        unsigned inputs=s->primitive_type==1?1:s->primitive_type<4?2:3;
        if(s->primitive_type<4){indices[0]=s->primitive_type==2?triangle*2u:triangle;indices[1]=indices[0]+1u;}
        else if(s->primitive_type==4)for(unsigned i=0;i<3;++i)indices[i]=triangle*3u+i;
        else if(s->primitive_type==5){indices[0]=triangle+(triangle&1u);indices[1]=triangle+1u-(triangle&1u);indices[2]=triangle+2u;}
        else{indices[0]=0;indices[1]=triangle+1;indices[2]=triangle+2;}
        for(unsigned i=0;i<inputs;++i){uint32_t at;if(!clip_vertex_offset(s,indices[i],&at))return -1;clip_load_point(s,at,&input[i]);}
        if(s->flat_shading)for(unsigned i=1;i<inputs;++i)for(unsigned c=5;c<13;++c)input[i].data[c]=input[0].data[c];
        if(inputs==1){polygon=input;count=!d9wg_clip_outcode(input[0].data) && input[0].data[3]>0?1u:0u;}
        else if(inputs==2){polygon=work->polygon[0];if(!d9wg_clip_segment(input,49,work->polygon[0],&count))return -1;}
        else if(!d9wg_clip_triangle(input,49,work,&polygon,&count))return -1;
        if(!count || (inputs==3 && count<3))continue;
        uint32_t vertices=inputs<3?count:(count-2u)*3u;
        if(vertices>capacity-queued){if(queued && !emit(context,emitted_type,staging,queued,s->stride))return -2;queued=0;}
        if(inputs<3){for(uint32_t i=0;i<count;++i)clip_pack_point(s,&polygon[i],(uint8_t*)staging+(queued++)*s->stride);continue;}
        for(uint32_t i=1;i+1<count;++i){
            const uint32_t fan[3]={0,i,i+1};
            for(unsigned c=0;c<3;++c)clip_pack_point(s,&polygon[fan[c]],(uint8_t*)staging+(queued++)*s->stride);
        }
    }
    if(queued && !emit(context,emitted_type,staging,queued,s->stride))return -2;
    return 1;
}
