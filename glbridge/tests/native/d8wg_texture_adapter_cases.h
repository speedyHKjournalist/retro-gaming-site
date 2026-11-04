typedef struct TextureBackend {Backend core;Backend level[3];UINT calls[15],level_arg;DWORD priority,lod,flags;const RECT*rect;void*output;const void*input;REFGUID guid;DWORD*size_out;DWORD bytes;HRESULT result,surface_error;BOOL empty_surface;}TextureBackend;
static TextureBackend new_texture_backend(void){TextureBackend t={0};t.core=new_backend(1);t.core.kind=1;for(UINT i=0;i<3;i++){t.level[i]=new_backend(0);t.level[i].coupled=&t.core;t.level[i].next_child=i<2?&t.level[i+1]:NULL;}t.core.children=&t.level[0];return t;}
/* Returning this factory value moves stack storage;rebind its internal links. */
static void texture_rebind(TextureBackend*t){t->core.children=&t->level[0];for(UINT i=0;i<3;i++){t->level[i].subresource_index=i;t->level[i].coupled=&t->core;t->level[i].next_child=i<2?&t->level[i+1]:NULL;}}
static HRESULT tx_set(struct IDirect3DTexture9*p,const GUID*g,const void*data,DWORD size,DWORD flags){TextureBackend*t=(TextureBackend*)p;t->calls[0]++;t->guid=g;t->input=data;t->bytes=size;t->flags=flags;return t->result;}
static HRESULT tx_get(struct IDirect3DTexture9*p,const GUID*g,void*data,DWORD*size){TextureBackend*t=(TextureBackend*)p;t->calls[1]++;t->guid=g;t->output=data;t->size_out=size;return t->result;}
static HRESULT tx_free(struct IDirect3DTexture9*p,const GUID*g){TextureBackend*t=(TextureBackend*)p;t->calls[2]++;t->guid=g;return t->result;}
static DWORD tx_set_priority(struct IDirect3DTexture9*p,DWORD value){TextureBackend*t=(TextureBackend*)p;t->calls[3]++;DWORD old=t->priority;t->priority=value;return old;}
static DWORD tx_get_priority(struct IDirect3DTexture9*p){TextureBackend*t=(TextureBackend*)p;t->calls[4]++;return t->priority;}
static void tx_preload(struct IDirect3DTexture9*p){((TextureBackend*)p)->calls[5]++;}
static UINT tx_get_type(struct IDirect3DTexture9*p){((TextureBackend*)p)->calls[6]++;return 3;}
static DWORD tx_set_lod(struct IDirect3DTexture9*p,DWORD value){TextureBackend*t=(TextureBackend*)p;t->calls[7]++;DWORD old=t->lod;t->lod=value>=3?2:value;return old;}
static DWORD tx_get_lod(struct IDirect3DTexture9*p){TextureBackend*t=(TextureBackend*)p;t->calls[8]++;return t->lod;}
static DWORD tx_get_count(struct IDirect3DTexture9*p){((TextureBackend*)p)->calls[9]++;return 3;}
static HRESULT tx_desc(struct IDirect3DTexture9*p,UINT level,void*out,UINT bytes){TextureBackend*t=(TextureBackend*)p;t->calls[10]++;t->level_arg=level;t->output=out;t->bytes=bytes;if(!out||bytes!=32||level>=3)return D3DERR_INVALIDCALL;if(t->result)return t->result;DWORD wanted[]={21,1,0,1,256u>>(level*2),0,8u>>level,8u>>level};memcpy(out,wanted,32);return 0;}
static HRESULT tx_surface(struct IDirect3DTexture9*p,UINT level,struct IDirect3DSurface9**out){TextureBackend*t=(TextureBackend*)p;t->calls[11]++;t->level_arg=level;*out=NULL;if(level>=3)return D3DERR_INVALIDCALL;if(t->surface_error)return t->surface_error;if(t->empty_surface)return 0;*out=&t->level[level].iface;backend_add((IUnknown*)*out);return 0;}
static HRESULT tx_lock(struct IDirect3DTexture9*p,UINT level,void*out,const RECT*rect,DWORD flags){TextureBackend*t=(TextureBackend*)p;t->calls[12]++;t->level_arg=level;t->output=out;t->rect=rect;t->flags=flags;return t->result;}
static HRESULT tx_unlock(struct IDirect3DTexture9*p,UINT level){TextureBackend*t=(TextureBackend*)p;t->calls[13]++;t->level_arg=level;return t->result;}
static HRESULT tx_dirty(struct IDirect3DTexture9*p,const RECT*rect){TextureBackend*t=(TextureBackend*)p;t->calls[14]++;t->rect=rect;return t->result;}
static const D9WGTextureMethods8 texture_methods={tx_set,tx_get,tx_free,tx_set_priority,tx_get_priority,tx_preload,tx_get_type,tx_set_lod,tx_get_lod,tx_get_count,tx_desc,tx_surface,tx_lock,tx_unlock,tx_dirty};
static const D9WGTextureMethods8*d9wg_d3d9_core_texture_methods8(struct IDirect3DTexture9*p){Backend*b=(Backend*)p;return p&&p->lpVtbl==&backend_vtbl&&b->profile&&b->kind==1?&texture_methods:NULL;}
static int texture_forwarding(IDirect3DTexture8*x,TextureBackend*t,Parent8*d){
 void*out=(void*)99;const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DResource8,&IID_IDirect3DBaseTexture8,&IID_IDirect3DTexture8};
 CHECK(x->lpVtbl->QueryInterface(x,NULL,&out)==E_NOINTERFACE&&!out&&x->lpVtbl->QueryInterface(x,&bad_iid,&out)==E_NOINTERFACE&&!out&&x->lpVtbl->QueryInterface(x,&IID_IUnknown,NULL)==E_POINTER);
 for(UINT i=0;i<4;i++){CHECK(!x->lpVtbl->QueryInterface(x,ids[i],&out)&&out==x&&x->lpVtbl->Release(out)==1);}
 for(UINT byte=0;byte<16;byte++){GUID changed=IID_IDirect3DTexture8;((uint8_t*)&changed)[byte]^=0x80;out=(void*)99;CHECK(x->lpVtbl->QueryInterface(x,&changed,&out)==E_NOINTERFACE&&!out);}
 IDirect3DDevice8*device=NULL;CHECK(x->lpVtbl->GetDevice(x,NULL)==D3DERR_INVALIDCALL&&!x->lpVtbl->GetDevice(x,&device)&&(IUnknown*)device==&d->iface);IUnknown_Release((IUnknown*)device);
 CHECK(x->lpVtbl->SetPriority(x,0x81234567u)==0&&x->lpVtbl->GetPriority(x)==0x81234567u&&t->calls[3]==1&&t->calls[4]==1);
 x->lpVtbl->PreLoad(x);CHECK(t->calls[5]==1&&x->lpVtbl->GetType(x)==3&&t->calls[6]==1);
 CHECK(!x->lpVtbl->SetLOD(x,0xffffffffu)&&x->lpVtbl->GetLOD(x)==2&&t->calls[7]==1&&t->calls[8]==1&&x->lpVtbl->GetLevelCount(x)==3&&t->calls[9]==1);
 for(UINT level=0;level<3;level++){D3DSURFACE_DESC desc={{0}};CHECK(!x->lpVtbl->GetLevelDesc(x,level,&desc)&&t->level_arg==level&&t->bytes==32&&desc.words[4]==(256u>>(level*2))&&desc.words[6]==(8u>>level)&&desc.words[7]==(8u>>level));}
 D3DSURFACE_DESC desc;memset(&desc,0xcc,sizeof(desc));CHECK(x->lpVtbl->GetLevelDesc(x,3,&desc)==D3DERR_INVALIDCALL&&x->lpVtbl->GetLevelDesc(x,0,NULL)==D3DERR_INVALIDCALL&&desc.words[0]==0xccccccccu);
 DWORD data[4]={1,2,3,4},size=sizeof(data);RECT rect={1,1,2,2};D3DLOCKED_RECT locked={0};
 for(UINT failure=0;failure<2;failure++){t->result=failure?-77:0;
  CHECK(x->lpVtbl->SetPrivateData(x,&bad_iid,data,13,0x80000001u)==t->result&&t->guid==&bad_iid&&t->input==data&&t->bytes==13&&t->flags==0x80000001u);
  CHECK(x->lpVtbl->GetPrivateData(x,&bad_iid,data,&size)==t->result&&t->guid==&bad_iid&&t->output==data&&t->size_out==&size);
  CHECK(x->lpVtbl->FreePrivateData(x,&bad_iid)==t->result&&t->guid==&bad_iid);
  CHECK(x->lpVtbl->LockRect(x,1,&locked,&rect,0xffffffffu)==t->result&&t->level_arg==1&&t->output==&locked&&t->rect==&rect&&t->flags==0xffffffffu);
  CHECK(x->lpVtbl->UnlockRect(x,2)==t->result&&t->level_arg==2);
  CHECK(x->lpVtbl->AddDirtyRect(x,&rect)==t->result&&t->rect==&rect);
 }t->result=0;return 0;
}
int main(void){CHECK(!retained_surface_main());UINT before=checks,cases=0;CHECK(!d8wg_core_texture_backend8(NULL));
 for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT level=0;level<2;level++){
  TextureBackend b=new_texture_backend();texture_rebind(&b);Parent8 device=new_parent();IDirect3DTexture8*x=NULL;
  CHECK(!d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&device.iface,&x)&&x&&b.core.refs==1&&device.refs==2&&live==2&&d8wg_core_texture_backend8(x)==(struct IDirect3DTexture9*)&b.core.iface&&b.core.refs==1);
  CHECK(!texture_forwarding(x,&b,&device));UINT allocations_before=allocations;fail_allocation=allocations+1;backend_add((IUnknown*)&b.core);IDirect3DTexture8*again=NULL;
  CHECK(!d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&device.iface,&again)&&again==x&&b.core.refs==1&&device.refs==2&&allocations==allocations_before&&again->lpVtbl->Release(again)==1);fail_allocation=0;
  IDirect3DSurface8*s=NULL,*again_surface=NULL;CHECK(!x->lpVtbl->GetSurfaceLevel(x,level,&s)&&s&&b.core.refs==1&&((D8CoreTexture*)x)->refcount==2&&device.refs==2&&live==4&&!d8wg_core_texture_backend8((IDirect3DTexture8*)s));
  D3DSURFACE_DESC child_desc;CHECK(!s->lpVtbl->GetDesc(s,&child_desc)&&child_desc.words[6]==(8u>>level)&&child_desc.words[7]==(8u>>level)&&child_desc.words[4]==(256u>>(level*2)));
  void*container=NULL;CHECK(!s->lpVtbl->GetContainer(s,&IID_IDirect3DTexture8,&container)&&container==x&&x->lpVtbl->Release(container)==2);
  CHECK(!s->lpVtbl->GetContainer(s,&IID_IDirect3DBaseTexture8,&container)&&container==x&&x->lpVtbl->Release(container)==2);
  allocations_before=allocations;fail_allocation=allocations+1;CHECK(!x->lpVtbl->GetSurfaceLevel(x,level,&again_surface)&&again_surface==s&&again_surface->lpVtbl->Release(again_surface)==2&&allocations==allocations_before);fail_allocation=0;
  CHECK(x->lpVtbl->Release(x)==1&&b.core.refs==1&&device.refs==2);
  if(order==0){CHECK(!s->lpVtbl->Release(s)&&!b.core.alive&&b.core.destroys==1&&!live&&device.refs==1&&!IUnknown_Release(&device.iface));}
  else if(order==1){CHECK(IUnknown_Release(&device.iface)==1&&!s->lpVtbl->Release(s)&&!b.core.alive&&!device.refs&&!live);}
  else {b.core.private_refs=1;CHECK(!s->lpVtbl->Release(s)&&b.core.alive&&!b.core.refs&&device.refs==1&&live==4);
   if(order==2){backend_add((IUnknown*)&b.core);CHECK(!d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&device.iface,&again)&&again==x&&b.core.refs==1&&device.refs==2);CHECK(!again->lpVtbl->GetSurfaceLevel(again,level,&again_surface)&&again_surface==s&&again_surface->lpVtbl->Release(again_surface)==1&&!again->lpVtbl->Release(again));}
   else {CHECK(!x->lpVtbl->GetSurfaceLevel(x,level,&again_surface)&&again_surface==s&&b.core.refs==1&&device.refs==2&&!again_surface->lpVtbl->Release(again_surface));}
   CHECK(!b.core.refs&&device.refs==1&&b.core.alive);b.core.private_refs=0;backend_retire(&b.core);CHECK(!live&&!b.core.alive&&b.core.destroys==1&&!IUnknown_Release(&device.iface));
  }cases++;
 }
 for(UINT failure=0;failure<8;failure++){
  TextureBackend b=new_texture_backend();texture_rebind(&b);Parent8 d=new_parent();IDirect3DTexture8*out=(void*)99;HRESULT expected=D3DERR_INVALIDCALL;
  if(failure==0)CHECK(d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,NULL)==expected);
  else {if(failure==1)b.core.profile=FALSE;if(failure==2)b.core.kind=0;if(failure==4)b.core.fail_get=TRUE;if(failure==5||failure==6){fail_allocation=allocations+(failure==5?1:2);expected=E_OUTOFMEMORY;}if(failure==4)expected=-77;if(failure==7)b.core.race=2;
   CHECK(d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,failure==3?NULL:(IDirect3DDevice8*)&d.iface,&out)==expected&&!out);
  }fail_allocation=0;CHECK(!b.core.alive&&b.core.destroys==1&&d.refs==1&&!live);cases++;
 }
 {TextureBackend b=new_texture_backend();texture_rebind(&b);Parent8 d=new_parent();b.core.race=1;IDirect3DTexture8*out=NULL;CHECK(!d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&out)&&out&&b.core.refs==2&&d.refs==2);CHECK(out->lpVtbl->AddRef(out)==2&&backend_release((IUnknown*)&b.core)==1&&out->lpVtbl->Release(out)==1&&!out->lpVtbl->Release(out)&&!live&&d.refs==1);cases++;}
 {TextureBackend b=new_texture_backend();texture_rebind(&b);Parent8 d=new_parent(),other=new_parent();IDirect3DTexture8*out=NULL,*bad=(void*)99;CHECK(!d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&out));backend_add((IUnknown*)&b.core);CHECK(d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&other.iface,&bad)==D3DERR_INVALIDCALL&&!bad&&b.core.refs==1&&d.refs==2&&other.refs==1);IDirect3DTexture8Vtbl wrong={0};((D8CoreTexture*)out)->iface.lpVtbl=&wrong;backend_add((IUnknown*)&b.core);CHECK(d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&bad)==D3DERR_INVALIDCALL&&!bad);((D8CoreTexture*)out)->iface.lpVtbl=&g_core_texture8_vtbl;CHECK(!out->lpVtbl->Release(out)&&!live);cases++;}
 {TextureBackend b=new_texture_backend();texture_rebind(&b);Parent8 d=new_parent();IDirect3DTexture8*out=NULL;IDirect3DSurface8*surface=(void*)99;CHECK(!d8wg_core_wrap_texture8((struct IDirect3DTexture9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&out));UINT calls=b.calls[11];CHECK(out->lpVtbl->GetSurfaceLevel(out,0,NULL)==D3DERR_INVALIDCALL&&b.calls[11]==calls);CHECK(out->lpVtbl->GetSurfaceLevel(out,3,&surface)==D3DERR_INVALIDCALL&&!surface&&b.core.refs==1);b.surface_error=-77;CHECK(out->lpVtbl->GetSurfaceLevel(out,0,&surface)==-77&&!surface&&b.core.refs==1);b.surface_error=0;b.empty_surface=TRUE;CHECK(out->lpVtbl->GetSurfaceLevel(out,0,&surface)==D3DERR_INVALIDCALL&&!surface&&b.core.refs==1);b.empty_surface=FALSE;
  for(UINT failure=1;failure<=2;failure++){fail_allocation=allocations+failure;CHECK(out->lpVtbl->GetSurfaceLevel(out,0,&surface)==E_OUTOFMEMORY&&!surface&&b.core.refs==1&&((D8CoreTexture*)out)->refcount==1&&live==2);fail_allocation=0;}
  CHECK(!out->lpVtbl->Release(out)&&!live&&d.refs==1);cases++;
 }
 {Parent8 d=new_parent();IDirect3DTexture8*out=(void*)99;CHECK(d8wg_core_wrap_texture8(NULL,(IDirect3DDevice8*)&d.iface,&out)==D3DERR_INVALIDCALL&&!out&&d.refs==1&&!live);cases++;}
 printf("PASS actual SDK8 texture adapter: %u checks / %u cases; real19-method vtable,Resource8/BaseTexture8/Texture8 identity,actual Surface8 child containers,coupled level refs/public-zero/parent-first/device-first cleanup,priority/LOD/type/private/descriptor/lock forwarding,owned input/OOM/race/hotcache guards;backend metadata/COM shim only\n",checks-before,cases);return 0;}
