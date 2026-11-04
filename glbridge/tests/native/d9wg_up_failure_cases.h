static int up_empty_stream(D9Device*d,UINT offset,UINT stride)
{
    Interface*view=(void*)1;UINT actual_offset=0,actual_stride=0;
    CHECK(!device_get_stream_source(&d->iface,0,&view,&actual_offset,&actual_stride)&&!view&&actual_offset==offset&&actual_stride==stride);return 0;
}
static int up_failure_cases(void)
{
    UINT old_checks=checks,profiles=0,failures=0;
    const float vertices[24]={0};const uint16_t indices[6]={0,1,2,2,1,3};
    for(UINT round=0;round<16;round++)for(UINT pool=0;pool<4;pool++)for(UINT order=0;order<4;order++){
        Parent parent={0};D9Device*d=new_device(&parent);UINT old_destroys=destroys,old_emits=up_emits,old_status=up_status_calls,old_invalidations=up_invalidations;
        D9VertexBuffer*vb=new_buffer(d,pool,FALSE),*other=new_buffer(d,pool,FALSE);D9IndexBuffer*ib=new_buffer(d,pool,TRUE);
        CHECK(!device_set_stream_source(&d->iface,1,&other->iface,8,12)&&!device_set_indices(&d->iface,&ib->iface));
        for(UINT cycle=0;cycle<16;cycle++){
            CHECK(!device_set_stream_source(&d->iface,0,&vb->iface,4,20));
            CHECK(device_draw_primitive_up(&d->iface,4,2,vertices,16)==D3DERR_INVALIDCALL);
            CHECK(!up_empty_stream(d,4,20)&&!vb->binding_refs&&vb->refcount==1&&d->index_buffer==ib&&ib->binding_refs==1&&d->streams[1].buffer==other);failures++;
            CHECK(!device_set_stream_source(&d->iface,0,&vb->iface,8,24));
            CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,101,vertices,16)==D3DERR_INVALIDCALL);
            CHECK(!up_empty_stream(d,8,24)&&!d->index_buffer&&!vb->binding_refs&&!ib->binding_refs&&d->streams[1].buffer==other);failures++;
            CHECK(!device_set_indices(&d->iface,&ib->iface));
        }
        CHECK(up_emits==old_emits&&up_status_calls==old_status&&up_invalidations==old_invalidations&&!d->clip_status_valid);
        CHECK(!device_set_stream_source(&d->iface,0,&vb->iface,4,20));
        CHECK(draw_indexed_up(d,4,0,4,2,indices,101,vertices,16,FALSE,FALSE)==D3DERR_INVALIDCALL&&d->streams[0].buffer==vb&&d->index_buffer==ib);
        CHECK(draw_indexed_up(d,4,0,4,2,indices,101,vertices,16,TRUE,FALSE)==D3DERR_INVALIDCALL&&d->streams[0].buffer==vb&&d->index_buffer==ib);
        CHECK(!device_begin_state_block(&d->iface));D9StateBlock*record=d->recording_result;
        CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,101,vertices,16)==D3DERR_INVALIDCALL);
        CHECK(!up_empty_stream(d,4,20)&&!d->index_buffer&&!record->has_stream[0]&&!record->has_indices);
        Interface*block=NULL;CHECK(!device_end_state_block(&d->iface,&block));
        CHECK(!device_set_stream_source(&d->iface,0,&vb->iface,4,20)&&!device_set_indices(&d->iface,&ib->iface));
        CHECK(!state_block_capture_method(block)&&!state_block_apply_method(block)&&d->streams[0].buffer==vb&&d->index_buffer==ib&&!state_block_release(block));
        /* Basic argument refusals, status/emission failures and patch-only
         * failures retain their prior policy; this phase changes only the
         * missing-declaration refusal with otherwise valid input. */
        CHECK(device_draw_primitive_up(&d->iface,4,2,NULL,16)==D3DERR_INVALIDCALL&&d->streams[0].buffer==vb);
        CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,0,vertices,16)==D3DERR_INVALIDCALL&&d->streams[0].buffer==vb&&d->index_buffer==ib);
        CHECK(!device_set_fvf(&d->iface,2));up_status_result=D3DERR_INVALIDCALL;
        CHECK(device_draw_primitive_up(&d->iface,4,2,vertices,16)==D3DERR_INVALIDCALL&&d->streams[0].buffer==vb);
        CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,101,vertices,16)==D3DERR_INVALIDCALL&&d->streams[0].buffer==vb&&d->index_buffer==ib);up_status_result=0;
        fail_emit=1;CHECK(device_draw_primitive_up(&d->iface,4,2,vertices,16)==D3DERR_DRIVERINTERNALERROR&&d->streams[0].buffer==vb);
        CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,101,vertices,16)==D3DERR_DRIVERINTERNALERROR&&d->streams[0].buffer==vb&&d->index_buffer==ib);fail_emit=0;
        CHECK(!draw_indexed_up(d,4,0,4,2,indices,101,vertices,16,FALSE,FALSE)&&d->streams[0].buffer==vb&&d->index_buffer==ib);
        CHECK(!device_draw_primitive_up(&d->iface,4,2,vertices,16)&&!up_empty_stream(d,0,0)&&d->index_buffer==ib);
        CHECK(!device_set_stream_source(&d->iface,0,&vb->iface,4,20)&&!device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,101,vertices,16)&&!up_empty_stream(d,0,0)&&!d->index_buffer);
        CHECK(!device_set_vertex_declaration(&d->iface,NULL)&&!device_set_stream_source(&d->iface,0,&vb->iface,4,20)&&!device_set_indices(&d->iface,&ib->iface));
        CHECK(!vb_release(&vb->iface)&&!ib_release(&ib->iface)&&other->binding_refs==1);
        CHECK(device_draw_indexed_primitive_up(&d->iface,4,0,4,2,indices,101,vertices,16)==D3DERR_INVALIDCALL&&destroys==old_destroys+2&&!up_empty_stream(d,4,20));
        CHECK(d->streams[1].buffer==other&&other->binding_refs==1&&d->refcount==2&&d->child_parent_refs==1);
        if(order==0){CHECK(!device_set_stream_source(&d->iface,1,NULL,0,0)&&!vb_release(&other->iface)&&!device_release(&d->iface));}
        else if(order==1){CHECK(!vb_release(&other->iface)&&!device_release(&d->iface));}
        else if(order==2){device_clear_bindings(d);CHECK(device_release(&d->iface)==1&&!vb_release(&other->iface));}
        else {CHECK(device_release(&d->iface)==1&&!vb_release(&other->iface));}
        CHECK(!live&&!parent.refs&&destroys==old_destroys+4);profiles++;
    }
    printf("PASS actual D3D9 missing-format UP: %u checks, %u pool/owner profiles, %u missing-declaration failures; preserved failure metadata, indexed storage retirement, slot isolation, recording exclusion, patch isolation, successful zeroing and unchanged other failures\n",checks-old_checks,profiles,failures);return 0;
}
