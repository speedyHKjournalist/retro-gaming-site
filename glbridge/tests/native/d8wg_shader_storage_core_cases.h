#define STORAGE_ADD d9wg_d3d9_core_shader_storage_add_ref8
#define STORAGE_RELEASE d9wg_d3d9_core_shader_storage_release8
typedef struct Storage8Context{D9Device*device;D9VertexDeclaration*decl;D9Shader*vs,*ps;UINT retired,finalizers,destroy_before;DWORD token;}Storage8Context;
static UINT storage8_errors;
static Storage8Context*storage8_finalizing;
static HRESULT WINAPI storage8_record(Interface*iface,const void*context){
 const Storage8Context*x=context;HRESULT hr=d9wg_d3d9_core_set_vertex_shader8(iface,&x->decl->iface,&x->vs->iface.vertex);
 return hr?hr:device_set_pixel_shader(iface,&x->ps->iface.pixel);
}
static void WINAPI storage8_retired(void*p){
 Storage8Context*x=p;D9Device*d=x->device;
 /* Frontend-held shader resources retire before device destruction/flush. */
 if(x->retired++||device_destroys!=x->destroy_before||d->refcount||d->child_parent_refs||d->parent->refs!=1||d->state_blocks||d->vertex_shader||d->pixel_shader||d->vertex_declaration||x->decl->refcount||x->vs->refcount||x->ps->refcount||x->decl->binding_refs!=1||x->vs->binding_refs!=1||x->ps->binding_refs!=1)storage8_errors++;
 UINT before=destroys;shader_storage_retiring=TRUE;
 if(STORAGE_RELEASE(&d->iface,&x->decl->iface)||STORAGE_RELEASE(&d->iface,&x->vs->iface.vertex)||STORAGE_RELEASE(&d->iface,&x->ps->iface.pixel))storage8_errors++;
 shader_storage_retiring=FALSE;
 if(destroys!=before+3||d->vertex_declarations||d->shaders)storage8_errors++;
}
static void storage8_finalizer(void*p){
 TokenPrivate*private_data=p;Storage8Context*x=storage8_finalizing;D9Device*d=x->device;
 if(private_data->device!=d||x->retired||private_data->pins!=1||d->refcount!=2||d->child_parent_refs||d->vertex_shader||d->pixel_shader||d->vertex_declaration||x->decl->binding_refs!=1||x->vs->binding_refs!=1||x->ps->binding_refs!=1)storage8_errors++;
 if(decl_add_ref(&x->decl->iface)!=1||vertex_shader_add_ref(&x->vs->iface.vertex)!=1||pixel_shader_add_ref(&x->ps->iface.pixel)!=1||d->refcount!=5||d->child_parent_refs!=3)storage8_errors++;
 if(decl_release(&x->decl->iface)||vertex_shader_release(&x->vs->iface.vertex)||pixel_shader_release(&x->ps->iface.pixel)||d->refcount!=2||d->child_parent_refs)storage8_errors++;
 x->finalizers++;
}
static int shader_storage8_core_cases(void){
 UINT before=checks,cases=0;active_compatibility_api=8;storage8_errors=0;
 for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++){
  Parent parent={0};D9Device*d=new_device(&parent);D9VertexDeclaration*decl=new_declaration(d);D9Shader*vs=new_shader(d,FALSE),*ps=new_shader(d,TRUE);Storage8Context meta={.device=d,.decl=decl,.vs=vs,.ps=ps,.destroy_before=device_destroys};Interface*objects[3]={&decl->iface,&vs->iface.vertex,&ps->iface.pixel};
  CHECK(!VIEW_ATTACH(&d->iface,&meta,storage8_retired));UINT allocated=allocations,emitted=pair_submissions;
  for(UINT i=0;i<3;i++)CHECK(!STORAGE_ADD(&d->iface,objects[i]));
  CHECK(d->refcount==4&&d->child_parent_refs==3&&decl->refcount==1&&vs->refcount==1&&ps->refcount==1&&decl->binding_refs==1&&vs->binding_refs==1&&ps->binding_refs==1&&allocations==allocated&&pair_submissions==emitted);
  CHECK(!decl_release(&decl->iface)&&!vertex_shader_release(&vs->iface.vertex)&&!pixel_shader_release(&ps->iface.pixel)&&d->refcount==1&&!d->child_parent_refs&&!device_has_reset_blockers(d));
  for(UINT i=0;i<3;i++)CHECK(!STORAGE_ADD(&d->iface,objects[i]));
  CHECK(decl->binding_refs==2&&vs->binding_refs==2&&ps->binding_refs==2&&!decl->refcount&&!vs->refcount&&!ps->refcount&&d->refcount==1&&!d->child_parent_refs&&allocations==allocated&&pair_submissions==emitted);
  for(UINT i=0;i<3;i++)CHECK(!STORAGE_RELEASE(&d->iface,objects[i]));
  CHECK(decl->binding_refs==1&&vs->binding_refs==1&&ps->binding_refs==1&&d->refcount==1);
  if(order){CHECK(!storage8_record(&d->iface,&meta));Interface*a=NULL,*b=NULL,*c=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&a)&&a==objects[0]&&!device_get_vertex_shader(&d->iface,&b)&&b==objects[1]&&!device_get_pixel_shader(&d->iface,&c)&&c==objects[2]&&d->refcount==4&&d->child_parent_refs==3);CHECK(!decl_release(a)&&!vertex_shader_release(b)&&!pixel_shader_release(c)&&d->refcount==1&&!d->child_parent_refs);}
  if(order==2){CHECK(!TOKEN_CREATE(&d->iface,storage8_record,&meta,&meta.token)&&meta.token&&state_block_handle8(d,meta.token)&&d->refcount==1&&!d->child_parent_refs);CHECK(!TOKEN_APPLY(&d->iface,meta.token)&&decl->binding_refs==3&&vs->binding_refs==3&&ps->binding_refs==3);}
  TokenPrivate private_data={.device=d,.mode=2};
  if(order==3){D9Surface*bb=HeapAlloc(NULL,8,sizeof(*bb));bb->iface.lpVtbl=&g_surface_vtbl;bb->device=d;bb->backbuffer=TRUE;d->implicit_back_buffer=bb;BYTE tag[16]={47,1};CHECK(!d9wg_private_set(&bb->private_data,tag,&private_data,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops)&&private_data.refs==1);storage8_finalizing=&meta;device_view_finalizer_hook=storage8_finalizer;}
  CHECK(device_release(&d->iface)==(order==3?1u:0u));
  if(order==3){CHECK(!meta.retired&&meta.finalizers==1&&!storage8_errors&&private_data.calls==1&&!private_data.refs&&d->refcount==1&&!d->child_parent_refs&&decl->binding_refs==1&&vs->binding_refs==1&&ps->binding_refs==1);CHECK(!storage8_record(&d->iface,&meta)&&!device_release(&d->iface));device_view_finalizer_hook=NULL;storage8_finalizing=NULL;}
  CHECK(meta.retired==1&&!storage8_errors&&!live&&!parent.refs&&!g_core_views8&&!view_lock_depth&&!g_command_count);cases++;
 }
 Parent parent={0};D9Device*d=new_device(&parent);D9VertexDeclaration*decl=new_declaration(d);D9Shader*vs=new_shader(d,FALSE),*ps=new_shader(d,TRUE);Interface*objects[3]={&decl->iface,&vs->iface.vertex,&ps->iface.pixel};int alien;Interface bad={.lpVtbl=&alien};D9Device foreign={0};foreign.iface.lpVtbl=&g_device_vtbl;
#define STORAGE_REJECT(dev,obj) do{UINT n=allocations,commands=pair_submissions;LONG refs=d->refcount,pins=d->child_parent_refs;CHECK(STORAGE_ADD(dev,obj)==D3DERR_INVALIDCALL&&STORAGE_RELEASE(dev,obj)==D3DERR_INVALIDCALL);CHECK(allocations==n&&pair_submissions==commands&&d->refcount==refs&&d->child_parent_refs==pins&&decl->refcount==1&&vs->refcount==1&&ps->refcount==1&&!decl->binding_refs&&!vs->binding_refs&&!ps->binding_refs);cases++;}while(0)
 STORAGE_REJECT(NULL,objects[0]);STORAGE_REJECT(&d->iface,NULL);STORAGE_REJECT(&d->iface,&bad);STORAGE_REJECT(&bad,objects[0]);
 active_compatibility_api=9;for(UINT i=0;i<3;i++)STORAGE_REJECT(&d->iface,objects[i]);active_compatibility_api=8;
 for(UINT i=0;i<3;i++){STORAGE_REJECT(&foreign.iface,objects[i]);CHECK(STORAGE_RELEASE(&d->iface,objects[i])==D3DERR_INVALIDCALL&&!decl->binding_refs&&!vs->binding_refs&&!ps->binding_refs&&d->refcount==4&&d->child_parent_refs==3);cases++;}
#undef STORAGE_REJECT
 CHECK(!decl_release(&decl->iface)&&!vertex_shader_release(&vs->iface.vertex)&&!pixel_shader_release(&ps->iface.pixel)&&!device_release(&d->iface)&&!live&&!parent.refs);
 active_compatibility_api=9;
 printf("PASS actual core8 shader storage holds: %u checks / %u cases; declaration/VS/PS private storage,no public/device pins,public-zero clone/query revival,live andcaptured bindings,finalizer device revival,late metadata cleanup whiledevice/root alive,profile/type/owner/underflow guards;explicit frontend handle-metadata context,notfullDevice8facade\n",checks-before,cases);return 0;
}
