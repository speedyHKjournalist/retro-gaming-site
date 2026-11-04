static int zero_up_cases(void)
{
    UINT old_checks=checks,profiles=0,noops=0,zero_stride=0;
    const UINT strides[]={1,12,20,0xffffffffu},primitives[]={1,4,0,0xffffffffu};
    for(UINT pool=0;pool<4;pool++)for(UINT format=0;format<2;format++)for(UINT recording=0;recording<2;recording++)for(UINT order=0;order<4;order++){
        Parent parent={0};D9Device*d=new_device(&parent);D9VertexBuffer*vb=new_buffer(d,pool,FALSE);D9IndexBuffer*ib=new_buffer(d,pool,TRUE);
        CHECK(!device_set_stream_source(&d->iface,0,&vb->iface,4,20)&&!device_set_indices(&d->iface,&ib->iface));
        if(format)CHECK(!device_set_fvf(&d->iface,2));
        if(recording)CHECK(!device_begin_state_block(&d->iface));
        CHECK(!vb_release(&vb->iface)&&!ib_release(&ib->iface));
        D9VertexDeclaration*decl=d->vertex_declaration;
        UINT old_emits=up_emits,old_status=up_status_calls,old_invalidations=up_invalidations,old_destroys=destroys,old_fvf_sets=fvf_sets;LONG refs=d->refcount;
        d->clip_status=(D9WGClipStatus){0xdeadbeef,0x80000000};d->clip_status_valid=TRUE;
        for(UINT cycle=0;cycle<32;cycle++){
            const void*pointer=cycle%2?(void*)(uintptr_t)1:NULL;
            CHECK(!device_draw_primitive_up(&d->iface,primitives[cycle%4],0,pointer,strides[cycle%4]));
            CHECK(!device_draw_indexed_primitive_up(&d->iface,primitives[cycle%4],0xffffffffu,cycle%2?0:0xffffffffu,0,pointer,0,pointer,strides[cycle%4]));noops+=2;
            CHECK(device_draw_primitive_up(&d->iface,4,0,pointer,0)==D3DERR_INVALIDCALL);
            CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,0,0,pointer,101,pointer,0)==D3DERR_INVALIDCALL);zero_stride+=2;
            CHECK(d->streams[0].buffer==vb&&d->streams[0].offset==4&&d->streams[0].stride==20&&d->index_buffer==ib&&vb->binding_refs==1&&ib->binding_refs==1&&!vb->refcount&&!ib->refcount);
            CHECK(d->refcount==refs&&d->vertex_declaration==decl&&d->fvf==(format?2u:0u)&&up_emits==old_emits&&up_status_calls==old_status&&up_invalidations==old_invalidations&&destroys==old_destroys&&fvf_sets==old_fvf_sets);
            CHECK(d->clip_status_valid&&d->clip_status.clip_union==0xdeadbeef&&d->clip_status.clip_intersection==0x80000000);
            if(recording)CHECK(!d->recording_result->has_stream[0]&&!d->recording_result->has_indices&&!d->recording_result->has_vertex_format);
        }
        if(recording){Interface*block=NULL;CHECK(!device_end_state_block(&d->iface,&block)&&!state_block_capture_method(block)&&!state_block_apply_method(block)&&!state_block_release(block));}
        CHECK(d->streams[0].buffer==vb&&d->index_buffer==ib);
        if(order==0){device_clear_bindings(d);CHECK(!device_release(&d->iface));}
        else if(order==1){CHECK(!device_release(&d->iface));}
        else {Interface*held=NULL;UINT stride=0;CHECK(!device_get_stream_source(&d->iface,0,&held,NULL,&stride)&&held==&vb->iface);
            if(order==2)device_clear_bindings(d);
            CHECK(device_release(&d->iface)==1&&!vb_release(held));}
        CHECK(!live&&!parent.refs&&destroys==old_destroys+2+format);profiles++;
    }
    printf("PASS actual D3D9 zero-primitive UP: %u checks, %u pool/format/record/owner profiles, %u successful no-ops, %u zero-stride refusals; null/poison data and ignored overflow/invalid primitive inputs, exact state and pins, no status/transport/storage effects\n",checks-old_checks,profiles,noops,zero_stride);return 0;
}
