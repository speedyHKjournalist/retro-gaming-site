typedef struct DeviceView8Context {D9Device*device;UINT retired,children,expected_children,finalizers;BOOL logical_zero;TokenPrivate*private_data;}DeviceView8Context;
typedef struct DeviceChildView8Context {DeviceView8Context*owner;Interface*object;UINT retired;}DeviceChildView8Context;
static UINT device_view8_errors;
static DeviceView8Context*device_view8_finalizing;
static void WINAPI device_view8_retired(void*p){
 DeviceView8Context*x=p;x->retired++;
 if(view_lock_depth||x->retired!=1||x->children!=x->expected_children||!x->logical_zero||x->device->refcount||x->device->child_parent_refs||x->device->parent->refs!=1)device_view8_errors++;
 if(x->private_data&&(x->private_data->refs||x->private_data->calls!=1||x->finalizers!=1))device_view8_errors++;
 void*out=(void*)99;if(VIEW_GET(&x->device->iface,&out)!=D3DERR_INVALIDCALL||out)device_view8_errors++;
 if(VIEW_ATTACH(&x->device->iface,x,device_view8_retired)!=D3DERR_INVALIDCALL)device_view8_errors++;
 core_view_retire8(x->device); /* Tombstoned device recursion is harmless. */
}
static void WINAPI device_child_view8_retired(void*p){
 DeviceChildView8Context*x=p;DeviceView8Context*owner=x->owner;x->retired++;owner->children++;
 if(view_lock_depth||x->retired!=1||owner->retired||owner->device->parent->refs!=1)device_view8_errors++;
 void*out=(void*)99;if(VIEW_GET(&owner->device->iface,&out)||out!=owner)device_view8_errors++;
 out=(void*)99;if(VIEW_GET(x->object,&out)!=D3DERR_INVALIDCALL||out)device_view8_errors++;
}
static void device_view8_private_finalizer(void*p){
 TokenPrivate*data=p;DeviceView8Context*x=device_view8_finalizing;void*out=(void*)99;
 if(!x||data!=x->private_data||data->device!=x->device||data->pins!=1||x->retired||view_lock_depth)device_view8_errors++;
 else{if(VIEW_GET(&x->device->iface,&out)||out!=x)device_view8_errors++;x->finalizers++;}
}
static int device_view8_core_cases(void){
 UINT before=checks,cases=0;active_compatibility_api=8;device_view8_errors=0;
 for(UINT round=0;round<64;round++)for(UINT order=0;order<4;order++)for(UINT mode=0;mode<3;mode++){
  Parent parent={0};D9Device*d=new_device(&parent);DeviceView8Context meta={.device=d,.expected_children=order==3?3:2,.logical_zero=TRUE};DeviceChildView8Context children[3]={{.owner=&meta},{.owner=&meta},{.owner=&meta}};
  D9SwapChain implicit={0};D9Surface*bb=HeapAlloc(NULL,8,sizeof(*bb)),*depth=HeapAlloc(NULL,8,sizeof(*depth));bb->iface.lpVtbl=&g_surface_vtbl;bb->device=d;bb->backbuffer=TRUE;bb->swap_chain=&implicit;d->implicit_back_buffer=bb;depth->iface.lpVtbl=&g_surface_vtbl;depth->device=d;depth->auto_depth_stencil=TRUE;d->implicit_depth_stencil=depth;
  UINT allocations_before=allocations;LONG pins=d->refcount,childpins=d->child_parent_refs;void*out=(void*)99;CHECK(!VIEW_GET(&d->iface,&out)&&!out&&allocations==allocations_before);
  CHECK(!VIEW_ATTACH(&d->iface,&meta,device_view8_retired)&&d->refcount==pins&&d->child_parent_refs==childpins&&live==4);allocations_before=allocations;
  CHECK(!VIEW_GET(&d->iface,&out)&&out==&meta&&allocations==allocations_before&&VIEW_ATTACH(&d->iface,&meta,device_view8_retired)==D3DERR_INVALIDCALL&&allocations==allocations_before);
  children[0].object=&bb->iface;children[1].object=&depth->iface;CHECK(!VIEW_ATTACH(&bb->iface,&children[0],device_child_view8_retired)&&!VIEW_ATTACH(&depth->iface,&children[1],device_child_view8_retired)&&d->refcount==pins&&d->child_parent_refs==childpins);
  TokenPrivate private_data={.device=d,.mode=mode};if(mode){BYTE tag[16]={46,1};meta.private_data=&private_data;CHECK(!d9wg_private_set(&bb->private_data,tag,&private_data,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops)&&private_data.refs==1);device_view8_finalizing=&meta;device_view_finalizer_hook=device_view8_private_finalizer;}
  else{device_view8_finalizing=NULL;device_view_finalizer_hook=NULL;}
  if(order==0){CHECK(device_release(&d->iface)==(mode==2?1u:0u));}
  else if(order==1){CHECK(device_add_ref(&d->iface)==2&&device_release(&d->iface)==1&&!meta.retired&&!meta.children);CHECK(!VIEW_GET(&d->iface,&out)&&out==&meta);CHECK(device_release(&d->iface)==(mode==2?1u:0u));}
  else if(order==2){CHECK(surface_add_ref(&bb->iface)==1&&device_release(&d->iface)==1&&!meta.retired&&!meta.children&&!VIEW_GET(&d->iface,&out)&&out==&meta);CHECK(!surface_release(&bb->iface));}
  else{Interface*target=NULL;CHECK(!create_target_texture(d,8,8,22,1,0,0,&target));children[2].object=target;CHECK(!VIEW_ATTACH(target,&children[2],device_child_view8_retired)&&!device_set_render_target(&d->iface,3,target)&&!surface_release(target)&&d->refcount==1);CHECK(device_release(&d->iface)==(mode==2?1u:0u));}
  if(mode==2){CHECK(!meta.retired&&meta.children==(order==3?1u:0u)&&meta.finalizers==1&&private_data.calls==1&&!private_data.refs&&d->refcount==1&&!d->releasing_owned_refs);CHECK(!VIEW_GET(&d->iface,&out)&&out==&meta);CHECK(!device_release(&d->iface));}
  CHECK(meta.retired==1&&meta.children==meta.expected_children&&children[0].retired==1&&children[1].retired==1&&!g_core_views8&&!view_lock_depth&&!device_view8_errors&&!live&&!parent.refs);if(order==3)CHECK(children[2].retired==1);if(mode)CHECK(meta.finalizers==1&&private_data.calls==1&&!private_data.refs);cases++;
 }
 device_view_finalizer_hook=NULL;device_view8_finalizing=NULL;
 for(UINT guard=0;guard<4;guard++){
  Parent parent={0};D9Device*d=new_device(&parent);DeviceView8Context meta={.device=d,.logical_zero=TRUE};UINT allocated=allocations,alive=live;void*out=(void*)99;
  if(guard==0){active_compatibility_api=9;CHECK(VIEW_GET(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&VIEW_ATTACH(&d->iface,&meta,device_view8_retired)==D3DERR_INVALIDCALL);active_compatibility_api=8;}
  else if(guard==1){d->iface.lpVtbl=(void*)0x1234;CHECK(VIEW_GET(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&VIEW_ATTACH(&d->iface,&meta,device_view8_retired)==D3DERR_INVALIDCALL);d->iface.lpVtbl=&g_device_vtbl;}
  else if(guard==2){CHECK(VIEW_GET(&d->iface,NULL)==D3DERR_INVALIDCALL&&VIEW_ATTACH(&d->iface,NULL,device_view8_retired)==D3DERR_INVALIDCALL&&VIEW_ATTACH(&d->iface,&meta,NULL)==D3DERR_INVALIDCALL);}
  else{fail_allocation=allocations+1;CHECK(VIEW_ATTACH(&d->iface,&meta,device_view8_retired)==E_OUTOFMEMORY);fail_allocation=0;allocated=allocations;}
  CHECK(allocations==allocated&&live==alive&&d->refcount==1&&!d->child_parent_refs&&!g_core_views8&&!view_lock_depth&&!device_release(&d->iface)&&!live&&!parent.refs&&!meta.retired);cases++;
 }
 active_compatibility_api=9;
 printf("PASS actual core8 weak device views: %u checks / %u cases; 768logical-zero/backend-pin/implicit-public/bound-private andfinalizer-revival groups,device metadata lastafterimplicit/resource callbacks,private-data-before-retire,tombstone recursion,noobject/device pins,ordinary9/type/OOM/duplicate/output guards;explicit native metadata contexts,notfullDevice8facade\n",checks-before,cases);return 0;
}
