#include "d9wg_clip_draw.h"
#include "d9wg_math.h"
static float clip_read_float(const void *p)
{ float value;__builtin_memcpy(&value,p,4);return value; }
static void clip_write_float(void *p,float value)
{ __builtin_memcpy(p,&value,4); }
static void clip_color(const uint8_t *source,int32_t offset,uint32_t type,float *out,uint32_t fallback)
{
    if(offset>=0){d9wg_decl_read(type,source+offset,out);return;}
    uint32_t color=fallback;
    out[0]=(float)(color>>16&255)/255;out[1]=(float)(color>>8&255)/255;
    out[2]=(float)(color&255)/255;out[3]=(float)(color>>24)/255;
}
static const D9WGClipVertex *clip_record(const D9WGClipDrawSource *s, uint32_t offset,
        const D9WGProcessViewport **viewport)
{
    const uint8_t *vertices = s->vertices;
    uint32_t position = offset + s->layout->position;
    if (s->stream_layout) {
        const D9WGVertexElement *element = s->stream_layout->elements + (uint32_t)s->layout->position/16u;
        const D9WGVertexStream *stream = s->streams + element->stream;
        vertices = stream->vertices;position = stream->base + offset*stream->stride + element->offset;
    }
    return d9wg_clip_buffer_lookup(s->clips, position, vertices + position, viewport);
}
static int clip_load_point(const D9WGClipDrawSource *s,uint32_t offset,D9WGClipPoint *point)
{
    const D9WGProcessLayout *layout=s->layout;
    uint8_t gathered[D9WG_VERTEX_GATHER_BYTES];
    const uint8_t *data;
    if (s->stream_layout) {
        if (!s->stream_gather || !s->stream_gather(s, offset, gathered)) return 0;
        data = gathered;
    } else data = s->vertices + offset;
    const D9WGProcessViewport *processed_vp=0;
    const D9WGClipVertex *record=clip_record(s,offset,&processed_vp);
    float *out=point->data;
    for(unsigned i=0;i<D9WG_CLIP_MAX_FLOATS;++i)out[i]=0;
    for(unsigned i=0;i<6;++i)out[D9WG_CLIP_USER_FIRST+i]=
            record && (record->user_mask & (1u<<i)) ? record->user_distance[i] : 1;
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
        float screen[4];for(unsigned i=0;i<4;++i)screen[i]=clip_read_float(data+layout->position+i*4u);
        d9wg_clip_unproject(screen,&s->viewport,out);out[4]=out[2];
    }
    clip_color(data,layout->diffuse,layout->diffuse_type,out+5,0xffffffffu);
    clip_color(data,layout->specular,layout->specular_type,out+9,0);
    if(layout->psize>=0){float value[4];d9wg_decl_read(layout->psize_type,data+layout->psize,value);out[13]=value[0];}
    if(layout->normal>=0){float value[4];d9wg_decl_read(layout->normal_type,data+layout->normal,value);for(unsigned i=0;i<3;++i)out[14+i]=value[i];}
    for(unsigned t=0;t<8;++t)if(layout->texcoord[t]>=0){
        float value[4];d9wg_decl_read(layout->texcoord_type[t],data+layout->texcoord[t],value);
        for(unsigned i=0;i<layout->texcoord_floats[t];++i)out[17+t*4+i]=value[i];
    }
    return 1;
}
static void clip_pack_point(const D9WGClipDrawSource *s,const D9WGClipPoint *point,uint8_t *output)
{
    const D9WGProcessLayout *layout=s->output_layout?s->output_layout:s->layout;const float *in=point->data;
    D9WGProcessViewport vp=s->viewport;float position[4];vp.min_z=0;vp.max_z=1;
    d9wg_clip_project(in,&vp,position);position[2]=in[4]/in[3];
    for(uint32_t i=0;i<s->stride;++i)output[i]=0;
    for(unsigned i=0;i<4;++i)clip_write_float(output+layout->position+i*4u,position[i]);
    if(layout->diffuse>=0){if(layout->diffuse_type==3)__builtin_memcpy(output+layout->diffuse,in+5,16);else{uint32_t c=d9wg_pack_color(in+5);__builtin_memcpy(output+layout->diffuse,&c,4);}}
    if(layout->specular>=0){if(layout->specular_type==3)__builtin_memcpy(output+layout->specular,in+9,16);else{uint32_t c=d9wg_pack_color(in+9);__builtin_memcpy(output+layout->specular,&c,4);}}
    if(layout->psize>=0)clip_write_float(output+layout->psize,in[13]);
    if(layout->normal>=0)for(unsigned i=0;i<3;++i)clip_write_float(output+layout->normal+i*4u,in[14+i]);
    for(unsigned t=0;t<8;++t)if(layout->texcoord[t]>=0)
        for(unsigned i=0;i<layout->texcoord_floats[t];++i)clip_write_float(output+layout->texcoord[t]+i*4u,in[17+t*4+i]);
}
static int clip_output_layout(const D9WGProcessLayout *layout)
{
    if(!layout || (layout->diffuse>=0 && layout->diffuse_type!=3 && layout->diffuse_type!=4) ||
            (layout->specular>=0 && layout->specular_type!=3 && layout->specular_type!=4))return 0;
    D9WGProcessLayout canonical=*layout;canonical.diffuse_type=canonical.specular_type=4;
    return d9wg_process_canonical_output(&canonical);
}
int d9wg_clip_draw(const D9WGClipDrawSource *s,D9WGClipWork *work,
        void *staging,uint32_t staging_bytes,D9WGClipEmit emit,void *context)
{
    if(!s || !s->clips || !s->clips->head)return 0;
    if((!s->vertices && !s->stream_layout) || (s->stream_layout && !s->streams) || !s->layout || !clip_output_layout(s->output_layout?s->output_layout:s->layout) || !s->layout->position_transformed ||
            s->layout->position<0 || s->layout->position_floats!=4 || !s->count ||
            !s->stride || s->layout->stride>s->stride || (s->output_layout && s->output_layout->stride>s->stride) || !s->viewport.width || !s->viewport.height ||
            !work || !staging || !emit || s->primitive_type<1 || s->primitive_type>6 ||
            (s->primitive_type==2 && s->count%2u) || (s->primitive_type==3 && s->count<2) ||
            (s->primitive_type==4 && s->count%3u) || (s->primitive_type>4 && s->count<3) ||
            (s->indices && ((s->index_stride!=2 && s->index_stride!=4) || s->count>s->index_bytes/s->index_stride)))return -1;
    uint32_t capacity=staging_bytes/s->stride,queued=0;
    uint32_t primitives=s->primitive_type==1?s->count:s->primitive_type==2?s->count/2u:
            s->primitive_type==3?s->count-1u:s->primitive_type==4?s->count/3u:s->count-2u;
    uint32_t emitted_type=s->primitive_type==1?1:s->primitive_type<4?2:4;
    if(capacity<21)return -1;
    unsigned live=0,user_mask=0;
    for(uint32_t i=0;i<s->count;++i){
        uint32_t at;if(!d9wg_clip_vertex_offset(s,i,&at))return -1;
        const D9WGProcessViewport *vp=0;
        const D9WGClipVertex *record=clip_record(s,at,&vp);
        if(record){live=1;user_mask|=record->user_mask;}
    }
    if(!live)return 0;
    if(user_mask && capacity<D9WG_CLIP_MAX_DRAW_VERTICES)return -1;
    for(uint32_t triangle=0;triangle<primitives;++triangle){
        uint32_t indices[3];D9WGClipPoint input[3];const D9WGClipPoint *polygon;uint32_t count;
        unsigned inputs=s->primitive_type==1?1:s->primitive_type<4?2:3;
        if(s->primitive_type<4){indices[0]=s->primitive_type==2?triangle*2u:triangle;indices[1]=indices[0]+1u;}
        else if(s->primitive_type==4)for(unsigned i=0;i<3;++i)indices[i]=triangle*3u+i;
        else if(s->primitive_type==5){indices[0]=triangle+(triangle&1u);indices[1]=triangle+1u-(triangle&1u);indices[2]=triangle+2u;}
        else{indices[0]=0;indices[1]=triangle+1;indices[2]=triangle+2;}
        for(unsigned i=0;i<inputs;++i){uint32_t at;if(!d9wg_clip_vertex_offset(s,indices[i],&at))return -1;if(!clip_load_point(s,at,&input[i]))return -1;}
        if(s->flat_shading)for(unsigned i=1;i<inputs;++i)for(unsigned c=5;c<13;++c)input[i].data[c]=input[0].data[c];
        if(inputs==1){
            polygon=input;count=!d9wg_clip_outcode(input[0].data) && input[0].data[3]>0?1u:0u;
            for(unsigned i=0;i<6;++i)if((user_mask&(1u<<i)) && input[0].data[D9WG_CLIP_USER_FIRST+i]<0)count=0;
        }
        else if(inputs==2){polygon=work->polygon[0];if(!d9wg_clip_segment_planes(input,55,user_mask,work->polygon[0],&count))return -1;}
        else if(!d9wg_clip_triangle_planes(input,55,user_mask,work,&polygon,&count))return -1;
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
