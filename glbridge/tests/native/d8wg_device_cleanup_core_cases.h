#define DEVICE_ATTACH d9wg_d3d9_core_attach_device_view8
#define OWNED_CLEANUP d9wg_d3d9_core_release_owned8
typedef struct Cleanup8Context{Storage8Context storage;D9VertexDeclaration*cache;D9Surface*child;UINT calls,raw_active,raw_detached,expected_calls,finalizers;BOOL recording,in_callback,drop_external;}Cleanup8Context;
static UINT cleanup8_errors,cleanup8_guard_retired;
static Cleanup8Context*cleanup8_finalizing;
static void WINAPI cleanup8_observer(void*p){
 Cleanup8Context*x=p;D9Device*d=x->storage.device;
 if(x->in_callback){cleanup8_errors++;return;}
 x->in_callback=TRUE;x->calls++;
 void*found=NULL;
 if(view_lock_depth||!d->releasing_owned_refs||d->refcount<1||d->parent->refs!=1||x->storage.retired||VIEW_GET(&d->iface,&found)||found!=x){cleanup8_errors++;x->in_callback=FALSE;return;}
 if(x->raw_active){
  D9StateBlock*token=d->state_blocks;while(token&&token->handle8!=x->storage.token)token=token->next_device_resource;
  BOOL cache=FALSE;for(D9VertexDeclaration*c=d->vertex_declarations;c;c=c->next_device_resource)if(c==x->cache&&c->fvf_cache_ref)cache=TRUE;
  if(!token||!cache||!!d->recording_result!=x->recording||d->vertex_declaration!=x->storage.decl||d->vertex_shader!=x->storage.vs||d->pixel_shader!=x->storage.ps)cleanup8_errors++;
  x->raw_active=0;x->raw_detached++;
 }
 LONG refs=d->refcount,pins=d->child_parent_refs;
 if(OWNED_CLEANUP(&d->iface)!=D3DERR_INVALIDCALL||d->refcount!=refs||d->child_parent_refs!=pins)cleanup8_errors++;
 x->in_callback=FALSE;
}
static void WINAPI cleanup8_retired(void*p){
 Cleanup8Context*x=p;
 if(x->calls!=x->expected_calls||x->raw_active||x->raw_detached!=1||x->in_callback||view_lock_depth)cleanup8_errors++;
 UINT calls=x->calls;core_device_cleanup8(x->storage.device);if(x->calls!=calls)cleanup8_errors++;
 storage8_retired(&x->storage);
}
static void cleanup8_finalizer(void*p){
 TokenPrivate*data=p;Cleanup8Context*x=cleanup8_finalizing;D9Device*d=x->storage.device;
 if(data->device!=d||x->storage.retired||x->raw_active||!x->raw_detached||!x->calls||!d->releasing_owned_refs||view_lock_depth||d->parent->refs!=1)cleanup8_errors++;
 x->finalizers++;
 if(x->drop_external){device_release(&d->iface);surface_release(&x->child->iface);}
}
static void WINAPI cleanup8_guard_retire(void*p){(void)p;cleanup8_guard_retired++;}
static int device_cleanup8_core_cases(void){
 UINT before=checks,cases=0;active_compatibility_api=8;cleanup8_errors=0;storage8_errors=0;
 for(UINT round=0;round<64;round++)for(UINT order=0;order<4;order++)for(UINT retain=0;retain<2;retain++){
  Parent parent={0};D9Device*d=new_device(&parent);D9VertexDeclaration*decl=new_declaration(d);D9Shader*vs=new_shader(d,FALSE),*ps=new_shader(d,TRUE);
  Cleanup8Context meta={.storage={.device=d,.decl=decl,.vs=vs,.ps=ps,.destroy_before=device_destroys},.raw_active=1,.recording=order<2,.drop_external=order==3,.expected_calls=order==2?1:2};if(retain&&order<3)meta.expected_calls++;
  Interface*objects[3]={&decl->iface,&vs->iface.vertex,&ps->iface.pixel};for(UINT i=0;i<3;i++)CHECK(!STORAGE_ADD(&d->iface,objects[i]));
  CHECK(!decl_release(&decl->iface)&&!vertex_shader_release(&vs->iface.vertex)&&!pixel_shader_release(&ps->iface.pixel)&&d->refcount==1&&!d->child_parent_refs);
  CHECK(!DEVICE_ATTACH(&d->iface,&meta,cleanup8_observer,cleanup8_retired));CHECK(DEVICE_ATTACH(&d->iface,&meta,cleanup8_observer,cleanup8_guard_retire)==D3DERR_INVALIDCALL);CHECK(!storage8_record(&d->iface,&meta.storage));CHECK(!TOKEN_CREATE(&d->iface,storage8_record,&meta.storage,&meta.storage.token));
  D9WGVertexElement element={0,0,2,0,0,0};CHECK(!device_fvf_declaration(d,2,&element,1,&meta.cache)&&meta.cache&&meta.cache->fvf_cache_ref&&!meta.cache->refcount);
  if(meta.recording)CHECK(!device_begin_state_block(&d->iface)&&!storage8_record(&d->iface,&meta.storage));
  D9SwapChain implicit={0};D9Surface*bb=HeapAlloc(NULL,8,sizeof(*bb));bb->iface.lpVtbl=&g_surface_vtbl;bb->device=d;bb->backbuffer=TRUE;bb->swap_chain=&implicit;d->implicit_back_buffer=bb;meta.child=bb;
  if(order==1||order==3)CHECK(surface_add_ref(&bb->iface)==1);
  TokenPrivate private_data={.device=d,.mode=retain?2:1};BYTE tag[16]={48,1};
  if(order==3){Interface*target=NULL;CHECK(!create_target_texture(d,8,8,22,1,0,0,&target));CHECK(!d9wg_private_set(&((D9Surface*)target)->private_data,tag,&private_data,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops));CHECK(!device_set_render_target(&d->iface,3,target)&&!surface_release(target));}
  else if(retain)CHECK(!d9wg_private_set(&bb->private_data,tag,&private_data,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops));
  cleanup8_finalizing=&meta;device_view_finalizer_hook=cleanup8_finalizer;
  if(order==2)CHECK(device_release(&d->iface)==(retain?1u:0u));
  else{
   CHECK(!OWNED_CLEANUP(&d->iface));
   if(order==3){if(retain){CHECK(!meta.storage.retired&&d->refcount==1&&!d->child_parent_refs&&meta.calls==1&&meta.finalizers==1);CHECK(!device_release(&d->iface));}}
   else{CHECK(!meta.storage.retired&&meta.calls==1&&!meta.raw_active&&meta.raw_detached==1&&!meta.finalizers&&!d->releasing_owned_refs&&!d->recording_result&&!d->state_blocks&&!d->vertex_declaration&&!d->vertex_shader&&!d->pixel_shader&&decl->binding_refs==1&&vs->binding_refs==1&&ps->binding_refs==1&&d->vertex_declarations==decl&&d->refcount==(order==1?2:1));CHECK(TOKEN_APPLY(&d->iface,meta.storage.token)==D3DERR_INVALIDCALL);CHECK(device_release(&d->iface)==(order==1||retain?1u:0u));if(order==1)CHECK(!surface_release(&bb->iface));}
  }
  if(retain&&order<3){CHECK(!meta.storage.retired&&d->refcount==1&&!d->child_parent_refs&&meta.finalizers==1&&private_data.calls==1&&!private_data.refs);CHECK(!device_release(&d->iface));}
  CHECK(meta.storage.retired==1&&meta.calls==meta.expected_calls&&meta.raw_detached==1&&!meta.raw_active&&!cleanup8_errors&&!storage8_errors&&!live&&!parent.refs&&!g_core_views8&&!view_lock_depth);
  if(order==3||retain)CHECK(meta.finalizers==1&&private_data.calls==1&&!private_data.refs);device_view_finalizer_hook=NULL;cleanup8_finalizing=NULL;cases++;
 }
 for(UINT guard=0;guard<9;guard++){
  Parent parent={0};D9Device*d=new_device(&parent);UINT n=allocations,alive=live;LONG refs=d->refcount;Interface bad={.lpVtbl=(void*)0x1234};void*view=(void*)123;
  if(guard==0)CHECK(DEVICE_ATTACH(NULL,view,cleanup8_observer,cleanup8_guard_retire)==D3DERR_INVALIDCALL&&OWNED_CLEANUP(NULL)==D3DERR_INVALIDCALL);
  else if(guard==1)CHECK(DEVICE_ATTACH(&bad,view,cleanup8_observer,cleanup8_guard_retire)==D3DERR_INVALIDCALL&&OWNED_CLEANUP(&bad)==D3DERR_INVALIDCALL);
  else if(guard==2){active_compatibility_api=9;CHECK(DEVICE_ATTACH(&d->iface,view,cleanup8_observer,cleanup8_guard_retire)==D3DERR_INVALIDCALL&&OWNED_CLEANUP(&d->iface)==D3DERR_INVALIDCALL);active_compatibility_api=8;}
  else if(guard==3)CHECK(DEVICE_ATTACH(&d->iface,NULL,cleanup8_observer,cleanup8_guard_retire)==D3DERR_INVALIDCALL);
  else if(guard==4)CHECK(DEVICE_ATTACH(&d->iface,view,NULL,cleanup8_guard_retire)==D3DERR_INVALIDCALL);
  else if(guard==5)CHECK(DEVICE_ATTACH(&d->iface,view,cleanup8_observer,NULL)==D3DERR_INVALIDCALL);
  else if(guard==6){fail_allocation=allocations+1;CHECK(DEVICE_ATTACH(&d->iface,view,cleanup8_observer,cleanup8_guard_retire)==E_OUTOFMEMORY);fail_allocation=0;n=allocations;}
  else if(guard==7){cleanup8_guard_retired=0;CHECK(!VIEW_ATTACH(&d->iface,view,cleanup8_guard_retire));n=allocations;alive=live;CHECK(DEVICE_ATTACH(&d->iface,view,cleanup8_observer,cleanup8_guard_retire)==D3DERR_INVALIDCALL);}
  else{d->releasing_owned_refs=TRUE;CHECK(OWNED_CLEANUP(&d->iface)==D3DERR_INVALIDCALL);d->releasing_owned_refs=FALSE;}
  CHECK(allocations==n&&live==alive&&d->refcount==refs&&!d->child_parent_refs&&!device_release(&d->iface)&&!live&&!parent.refs&&!g_core_views8);if(guard==7)CHECK(cleanup8_guard_retired==1);cases++;
 }
 active_compatibility_api=9;
 printf("PASS actual core8 device cleanup observers: %u checks / %u cases; before recording/block/live/cache destruction,outside transportlock,temporary pin,nested rejection,idempotent explicit/final cleanup,public child pins,callback drops last other refs orrevives device,weak identity/retirement andprofile/type/null/OOM/duplicate guards;explicit frontend rawsnapshot metadata fixture,not fullDevice8facade\n",checks-before,cases);return 0;
}
