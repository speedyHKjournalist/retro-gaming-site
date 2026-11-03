static UINT next_dimension(UINT n) { return n > 1 ? n/2 : 1; }
static void clear_events(void)
{
    for (unsigned i=0;i<emits;++i) free(events[i].data);
    emits=event_error=0;
}
static void build_texture(Texture *t,Device *device,UINT kind,UINT width,UINT height,UINT depth,UINT levels,BOOL lazy,BOOL source)
{
    memset(t,0,sizeof(*t));t->iface.lpVtbl=kind==0?(const void*)&g_texture_vtbl:kind==1?(const void*)&G_CUBE_VTBL:(const void*)&g_volume_texture_vtbl;
    t->device=device;t->handle=source?301:401;t->pool=source?2:0;t->format=21;t->level_count=levels;
    if(kind==1)t->edge_length=width;else{t->width=width;t->height=height;if(kind==2)t->depth=depth;}
    unsigned layers=kind==1?6:1;t->levels=calloc(layers*levels,sizeof(*t->levels));if(!t->levels)abort();
    for(unsigned layer=0;layer<layers;++layer){UINT w=width,h=kind==1?width:height,d=kind==2?depth:1;
        for(unsigned level=0;level<levels;++level){Level *l=t->levels+layer*levels+level;l->width=w;l->height=h;l->depth=kind==2?d:0;l->row_pitch=w*4;l->row_count=h;l->slice_pitch=kind==2?w*h*4:0;l->byte_count=w*h*d*4;l->guard_a=0x24680;l->guard_b=0x13579;
            l->shadow_valid=source?TRUE:FALSE;if(!lazy){l->shadow=malloc(l->byte_count);if(!l->shadow)abort();for(UINT b=0;b<l->byte_count;++b)l->shadow[b]=source?(BYTE)(1+(layer*31+level*7+b)%251):0x3c;}
            w=next_dimension(w);h=next_dimension(h);d=next_dimension(d);
        }
    }
}
static void free_texture(Texture *t,UINT kind,BOOL lazy)
{
    unsigned count=t->level_count*(kind==1?6:1);for(unsigned i=0;i<count;++i)if(t->levels[i].shadow){if(lazy)frees++;free(t->levels[i].shadow);}free(t->levels);
}
static int inspect_texture(Texture *source,Texture *destination,UINT kind,UINT expected_skip,UINT count,BOOL lazy)
{
    unsigned layers=kind==1?6:1;CHECK(!event_error&&emits==layers*count);
    for(unsigned layer=0;layer<layers;++layer)for(unsigned level=0;level<source->level_count;++level){Level *from=source->levels+layer*source->level_count+level;CHECK(from->guard_a==0x24680&&from->guard_b==0x13579);for(UINT b=0;b<from->byte_count;++b)CHECK(from->shadow[b]==(BYTE)(1+(layer*31+level*7+b)%251));}

    for(unsigned layer=0;layer<layers;++layer)for(unsigned level=0;level<destination->level_count;++level){Level *to=destination->levels+layer*destination->level_count+level;CHECK(to->guard_a==0x24680&&to->guard_b==0x13579);
        if(level<count){Level *from=source->levels+layer*source->level_count+expected_skip+level;CHECK(to->shadow&&to->byte_count==from->byte_count&&!memcmp(to->shadow,from->shadow,to->byte_count));CHECK(events[layer*count+level].size==to->byte_count&&!memcmp(events[layer*count+level].data,from->shadow,to->byte_count));
#if API == 9
            CHECK(to->shadow_valid);
#endif
        }else if(lazy)CHECK(!to->shadow&&!to->shadow_valid);else for(UINT b=0;b<to->byte_count;++b)CHECK(to->shadow[b]==0x3c);
    }return 0;
}
int main(void)
{
    Device device={0},foreign={0};Texture source,destination;
    const UINT cases[][8]={{8,4,8,4,8,4,8,3},{32,16,8,6,8,4,2,4},{32,16,8,3,8,4,2,4},{32,16,8,1,8,4,2,4},{16,8,4,5,8,4,2,3},{7,5,3,3,3,2,1,2},{1,1,1,1,1,1,1,1}};
    unsigned tested=0;
    for(UINT kind=0;kind<3;++kind)for(unsigned c=0;c<7;++c)for(unsigned lazy=0;lazy<2;++lazy){const UINT *r=cases[c];build_texture(&source,&device,kind,r[0],r[1],r[2],r[3],FALSE,TRUE);build_texture(&destination,&device,kind,r[4],r[5],r[6],r[7],lazy,FALSE);
        /* Find the first matching shape by enumerating each independent axis. */
        UINT w=r[0],h=kind==1?r[0]:r[1],d=kind==2?r[2]:1,first=0;
        while(w!=r[4]||h!=(kind==1?r[4]:r[5])||d!=(kind==2?r[6]:1)){CHECK(first<31);w=next_dimension(w);h=next_dimension(h);d=next_dimension(d);++first;}
        UINT count=first<source.level_count?source.level_count-first:0;if(count>destination.level_count)count=destination.level_count;
        current_source=&source;current_destination=&destination;current_kind=kind;skip=first;copied=count;clear_events();
        CHECK(call_update(&device,&source,&destination)==0);CHECK(inspect_texture(&source,&destination,kind,first,count,lazy)==0);
        clear_events();free_texture(&source,kind,FALSE);free_texture(&destination,kind,lazy);++tested;
    }
    /* Invalid last cube face/mip must not write pixels, allocate or emit. */
    build_texture(&source,&device,1,32,32,1,6,FALSE,TRUE);build_texture(&destination,&device,1,8,8,1,4,FALSE,FALSE);current_source=&source;current_destination=&destination;current_kind=1;skip=2;copied=4;destination.levels[23].locked=TRUE;unsigned before_alloc=allocations;
    CHECK(call_update(&device,&source,&destination)==-1&&!emits&&allocations==before_alloc);for(unsigned i=0;i<24;++i)for(UINT b=0;b<destination.levels[i].byte_count;++b)CHECK(destination.levels[i].shadow[b]==0x3c);destination.levels[23].locked=FALSE;
    destination.levels[23].byte_count--;CHECK(call_update(&device,&source,&destination)==-1&&!emits);destination.levels[23].byte_count++;
    source.levels[2].row_pitch++;CHECK(call_update(&device,&source,&destination)==-1&&!emits);source.levels[2].row_pitch--;
    source.levels[2].width++;CHECK(call_update(&device,&source,&destination)==-1&&!emits);source.levels[2].width--;
    BYTE *saved_shadow=source.levels[2].shadow;source.levels[2].shadow=NULL;CHECK(call_update(&device,&source,&destination)==-1&&!emits);source.levels[2].shadow=saved_shadow;
    source.levels[2].locked=TRUE;CHECK(call_update(&device,&source,&destination)==-1&&!emits);source.levels[2].locked=FALSE;source.levels[0].locked=TRUE;CHECK(call_update(&device,&source,&destination)==0&&emits==24);source.levels[0].locked=FALSE;clear_events();
    source.device=&foreign;CHECK(call_update(&device,&source,&destination)==-1&&!emits);source.device=&device;destination.format=22;CHECK(call_update(&device,&source,&destination)==-1&&!emits);destination.format=21;
    CHECK(call_update(&device,NULL,&destination)==-1);destination.iface.lpVtbl=&g_texture_vtbl;CHECK(call_update(&device,&source,&destination)==-1);destination.iface.lpVtbl=&G_CUBE_VTBL;
#if API == 9
    source.pool=1;CHECK(call_update(&device,&source,&destination)==-1&&!emits);source.pool=2;
#endif
    for(unsigned i=0;i<24;++i)memset(destination.levels[i].shadow,0x3c,destination.levels[i].byte_count);
    fail_emit=2;CHECK(call_update(&device,&source,&destination)==-2&&emits==2);
    for(unsigned i=0;i<24;++i)if(i<2)CHECK(!memcmp(destination.levels[i].shadow,source.levels[2+i].shadow,destination.levels[i].byte_count));else for(UINT b=0;b<destination.levels[i].byte_count;++b)CHECK(destination.levels[i].shadow[b]==0x3c);
fail_emit=0;clear_events();free_texture(&source,1,FALSE);free_texture(&destination,1,FALSE);
    /* Lazy target mirrors are prepared entirely before the first pixel copy. */
    build_texture(&source,&device,0,16,8,1,5,FALSE,TRUE);build_texture(&destination,&device,0,8,4,1,3,TRUE,FALSE);current_source=&source;current_destination=&destination;current_kind=0;skip=1;copied=3;fail_alloc=attempts+2;
    CHECK(call_update(&device,&source,&destination)==-3&&!emits&&destination.levels[0].shadow&&!destination.levels[1].shadow);
    for(unsigned i=0;i<3;++i){CHECK(!destination.levels[i].shadow_valid);if(destination.levels[i].shadow)for(UINT b=0;b<destination.levels[i].byte_count;++b)CHECK(!destination.levels[i].shadow[b]);}fail_alloc=0;
    CHECK(call_update(&device,&source,&destination)==0);CHECK(inspect_texture(&source,&destination,0,1,3,TRUE)==0);clear_events();free_texture(&source,0,FALSE);free_texture(&destination,0,TRUE);
    CHECK(allocations==frees);printf("PASS actual API %u UpdateTexture: %u 2D/cube/volume mip-layout/lazy cases, independent face strides, truncated/missing mips, exact upload bytes, skipped locks, late invalid atomicity, OOM-before-copy, shadow validity and accepted transport prefixes\n",API,tested);return 0;
}
