#define CHECK(v) do{if(!(v)){fprintf(stderr,"Surface8 adapter line %u: %s\n",__LINE__,#v);return 1;}checks++;}while(0)
typedef struct Backend {struct IDirect3DSurface9 iface;struct Backend*coupled,*children,*next_child;UINT kind,subresource_index;LONG refs,private_refs;BOOL profile,alive;void*view,*entry;D9WGCoreViewRetire8 retire;UINT destroys,calls[6];REFGUID guid;const void*input;void*output;DWORD size,flags;DWORD*size_out;const RECT*rect;HRESULT result;BOOL fail_get;UINT race;}Backend;
typedef struct Parent8 {IUnknown iface;LONG refs;Backend*backend;struct Parent8*device;UINT add_calls,release_calls;BOOL is_texture;}Parent8;
static const IUnknownVtbl backend_vtbl,parent_vtbl;
static Backend*backend(IUnknown*p){return (Backend*)p;}
static void backend_retire(Backend*b){if(b->refs||b->private_refs)return;b->alive=FALSE;b->destroys++;for(Backend*child=b->children;child;child=child->next_child){child->alive=FALSE;child->destroys++;void*cv=child->view,*ce=child->entry;D9WGCoreViewRetire8 cr=child->retire;child->view=NULL;child->entry=NULL;if(cr)cr(cv);HeapFree(NULL,0,ce);}void*view=b->view,*entry=b->entry;D9WGCoreViewRetire8 retire=b->retire;b->view=NULL;b->entry=NULL;if(retire)retire(view);HeapFree(NULL,0,entry);}
static ULONG backend_add(IUnknown*p){Backend*b=backend(p);assert(b->alive);if(b->coupled)return backend_add((IUnknown*)b->coupled);return ++b->refs;}
static ULONG backend_release(IUnknown*p){Backend*b=backend(p);assert(b->alive);if(b->coupled)return backend_release((IUnknown*)b->coupled);assert(b->refs>0);ULONG result=--b->refs;backend_retire(b);return result;}
static HRESULT backend_query(IUnknown*p,REFIID iid,void**out){(void)iid;if(!out)return E_POINTER;*out=p;backend_add(p);return 0;}
static const IUnknownVtbl backend_vtbl={backend_query,backend_add,backend_release};
static ULONG parent_add(IUnknown*p){Parent8*x=(Parent8*)p;x->add_calls++;ULONG result=++x->refs;if(result==1&&x->backend){backend_add((IUnknown*)x->backend);if(x->device)IUnknown_AddRef(&x->device->iface);}return result;}
static ULONG parent_release(IUnknown*p){Parent8*x=(Parent8*)p;assert(x->refs>0);x->release_calls++;ULONG result=--x->refs;if(!result&&x->backend){Parent8*device=x->device;backend_release((IUnknown*)x->backend);if(device)IUnknown_Release(&device->iface);}return result;}
static HRESULT parent_query(IUnknown*p,REFIID iid,void**out){Parent8*x=(Parent8*)p;if(!out)return E_POINTER;*out=NULL;if(!core_surface8_guid(iid,&IID_IUnknown)&&!core_surface8_guid(iid,x->is_texture?&IID_IDirect3DTexture8:&IID_IDirect3DDevice8))return E_NOINTERFACE;*out=p;parent_add(p);return 0;}
static const IUnknownVtbl parent_vtbl={parent_query,parent_add,parent_release};
static Backend new_backend(UINT refs){Backend b={0};b.iface.lpVtbl=&backend_vtbl;b.refs=refs;b.profile=TRUE;b.alive=TRUE;return b;}
static Parent8 new_parent(void){Parent8 p={0};p.iface.lpVtbl=&parent_vtbl;p.refs=1;return p;}
static HRESULT mock_set(struct IDirect3DSurface9*p,const GUID*g,const void*data,DWORD size,DWORD flags){Backend*b=(Backend*)p;b->calls[0]++;b->guid=g;b->input=data;b->size=size;b->flags=flags;return b->result;}
static HRESULT mock_get(struct IDirect3DSurface9*p,const GUID*g,void*data,DWORD*size){Backend*b=(Backend*)p;b->calls[1]++;b->guid=g;b->output=data;b->size_out=size;return b->result;}
static HRESULT mock_free(struct IDirect3DSurface9*p,const GUID*g){Backend*b=(Backend*)p;b->calls[2]++;b->guid=g;return b->result;}
static HRESULT mock_desc(struct IDirect3DSurface9*p,void*out,UINT bytes){Backend*b=(Backend*)p;b->calls[3]++;b->output=out;b->size=bytes;if(!out||bytes!=32)return D3DERR_INVALIDCALL;if(b->result)return b->result;if(b->coupled){UINT level=b->subresource_index;DWORD words[]={21,1,0,1,256u>>(level*2),0,8u>>level,8u>>level};memcpy(out,words,32);}else for(UINT i=0;i<8;i++)((DWORD*)out)[i]=0x12340000u+i;return 0;}
static HRESULT mock_lock(struct IDirect3DSurface9*p,void*out,const RECT*rect,DWORD flags){Backend*b=(Backend*)p;b->calls[4]++;b->output=out;b->rect=rect;b->flags=flags;return b->result;}
static HRESULT mock_unlock(struct IDirect3DSurface9*p){Backend*b=(Backend*)p;b->calls[5]++;return b->result;}
static const D9WGSurfaceMethods8 mock_methods={mock_set,mock_get,mock_free,mock_desc,mock_lock,mock_unlock};
static const D9WGSurfaceMethods8*d9wg_d3d9_core_surface_methods8(struct IDirect3DSurface9*p){Backend*b=(Backend*)p;return p&&p->lpVtbl==&backend_vtbl&&b->profile&&!b->kind?&mock_methods:NULL;}
static HRESULT d9wg_d3d9_core_get_view8(IUnknown*p,void**out){Backend*b=(Backend*)p;*out=NULL;if(b->fail_get)return -77;*out=b->view;return 0;}
static HRESULT d9wg_d3d9_core_attach_view8(IUnknown*p,void*view,D9WGCoreViewRetire8 retire){Backend*b=(Backend*)p;if(b->view)return D3DERR_INVALIDCALL;if(b->race){UINT mode=b->race;b->race=0;if(mode==2)return D3DERR_INVALIDCALL;
#ifdef D8WG_NATIVE_VOLUME_ADAPTER
if(b->kind==1){D8CoreVolumeTexture*winner=HeapAlloc(NULL,8,sizeof(*winner));assert(winner);*winner=*(D8CoreVolumeTexture*)view;b->view=winner;}else if(b->kind==2){D8CoreVolume*winner=HeapAlloc(NULL,8,sizeof(*winner));assert(winner);*winner=*(D8CoreVolume*)view;b->view=winner;}else
#endif
#ifdef D8WG_NATIVE_CUBE_ADAPTER
if(b->kind){D8CoreCubeTexture*winner=HeapAlloc(NULL,8,sizeof(*winner));assert(winner);*winner=*(D8CoreCubeTexture*)view;b->view=winner;}else
#endif
#ifdef D8WG_NATIVE_TEXTURE_ADAPTER
if(b->kind){D8CoreTexture*winner=HeapAlloc(NULL,8,sizeof(*winner));assert(winner);*winner=*(D8CoreTexture*)view;b->view=winner;}else
#endif
{D8CoreSurface*winner=HeapAlloc(NULL,8,sizeof(*winner));assert(winner);*winner=*(D8CoreSurface*)view;b->view=winner;}
b->entry=HeapAlloc(NULL,8,8);assert(b->entry);b->retire=retire;backend_add(p);return D3DERR_INVALIDCALL;}void*entry=HeapAlloc(NULL,8,8);if(!entry)return E_OUTOFMEMORY;b->entry=entry;b->view=view;b->retire=retire;return 0;}
static int forwarding(IDirect3DSurface8*s,Backend*b,Parent8*device,Parent8*container){
    void*out=(void*)99;LONG refs=((D8CoreSurface*)s)->container_refcount?container->refs:((D8CoreSurface*)s)->refcount;
    CHECK(s->lpVtbl->QueryInterface(s,NULL,&out)==E_NOINTERFACE&&!out&&s->lpVtbl->QueryInterface(s,&bad_iid,&out)==E_NOINTERFACE&&!out&&s->lpVtbl->QueryInterface(s,&IID_IUnknown,NULL)==E_POINTER);
    for(UINT byte=0;byte<16;byte++){GUID changed=IID_IDirect3DSurface8;((uint8_t*)&changed)[byte]^=0x80;out=(void*)99;CHECK(s->lpVtbl->QueryInterface(s,&changed,&out)==E_NOINTERFACE&&!out);}
    const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DResource8,&IID_IDirect3DSurface8};for(UINT i=0;i<3;i++){CHECK(!s->lpVtbl->QueryInterface(s,ids[i],&out)&&out==s);CHECK(s->lpVtbl->Release(out)==(ULONG)refs);}
    IDirect3DDevice8*owner=NULL;CHECK(s->lpVtbl->GetDevice(s,NULL)==D3DERR_INVALIDCALL&&!s->lpVtbl->GetDevice(s,&owner)&&owner==(IDirect3DDevice8*)&device->iface);IUnknown_Release((IUnknown*)owner);
    CHECK(s->lpVtbl->GetContainer(s,NULL,&out)==E_NOINTERFACE&&!out&&s->lpVtbl->GetContainer(s,&bad_iid,&out)==E_NOINTERFACE&&!out&&s->lpVtbl->GetContainer(s,&IID_IUnknown,NULL)==E_POINTER);
    CHECK(!s->lpVtbl->GetContainer(s,&IID_IUnknown,&out)&&out==&container->iface);IUnknown_Release((IUnknown*)out);
    DWORD words[8]={0},size=32;RECT rect={1,2,3,4};D3DLOCKED_RECT lock={0};
    CHECK(!s->lpVtbl->GetDesc(s,(D3DSURFACE_DESC*)words)&&b->output==words&&b->size==32);for(UINT i=0;i<8;i++)CHECK(words[i]==0x12340000u+i);
    CHECK(s->lpVtbl->GetDesc(s,NULL)==D3DERR_INVALIDCALL);
    for(UINT i=0;i<2;i++){b->result=i?-77:0;CHECK(s->lpVtbl->SetPrivateData(s,&bad_iid,words,19,0x80000001u)==b->result&&b->guid==&bad_iid&&b->input==words&&b->size==19&&b->flags==0x80000001u);
        CHECK(s->lpVtbl->GetPrivateData(s,&bad_iid,words,&size)==b->result&&b->guid==&bad_iid&&b->output==words&&b->size_out==&size);
        CHECK(s->lpVtbl->FreePrivateData(s,&bad_iid)==b->result&&b->guid==&bad_iid);
        CHECK(s->lpVtbl->LockRect(s,&lock,&rect,0xffffffffu)==b->result&&b->output==&lock&&b->rect==&rect&&b->flags==0xffffffffu);
        CHECK(s->lpVtbl->UnlockRect(s)==b->result);
    }b->result=0;return 0;
}
int main(void){UINT cases=0;for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT coupled=0;coupled<2;coupled++){
    Backend b=new_backend(1);Parent8 device=new_parent(),container=new_parent();container.is_texture=TRUE;container.backend=&b;container.device=&device;
    if(coupled){device.refs++;backend_add((IUnknown*)&b);}/* frontend texture plus owned surface getter */
    IUnknown*parent=coupled?&container.iface:&device.iface;IDirect3DSurface8*s=NULL;
    CHECK(!d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&device.iface,parent,coupled,&s)&&s&&b.refs==1&&device.refs==2&&live==2);
    CHECK(!forwarding(s,&b,&device,coupled?&container:&device));
    backend_add((IUnknown*)&b);UINT cached_allocations=allocations;fail_allocation=allocations+1;IDirect3DSurface8*again=NULL;CHECK(!d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&device.iface,parent,coupled,&again)&&again==s&&b.refs==1&&device.refs==2&&live==2&&allocations==cached_allocations);fail_allocation=0;
    CHECK(s->lpVtbl->Release(again)==(coupled?2u:1u));
    if(coupled)CHECK(IUnknown_Release(&container.iface)==1);
    if(order==0){CHECK(!s->lpVtbl->Release(s)&&!b.alive&&b.destroys==1&&device.refs==1&&!live);CHECK(!IUnknown_Release(&device.iface));}
    else if(order==1){CHECK(IUnknown_Release(&device.iface)==1&&!s->lpVtbl->Release(s)&&!b.alive&&b.destroys==1&&!device.refs&&!live);}
    else {b.private_refs=1;CHECK(!s->lpVtbl->Release(s)&&b.alive&&!b.refs&&device.refs==1&&live==2);if(order==2){CHECK(s->lpVtbl->AddRef(s)==1&&b.refs==1&&device.refs==2);CHECK(!s->lpVtbl->Release(s)&&b.alive&&!b.refs&&device.refs==1);}else{backend_add((IUnknown*)&b);again=NULL;CHECK(!d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&device.iface,parent,coupled,&again)&&again==s&&b.refs==1&&device.refs==2);CHECK(!again->lpVtbl->Release(again)&&b.alive&&!b.refs&&device.refs==1);}b.private_refs=0;backend_retire(&b);CHECK(!b.alive&&b.destroys==1&&!live&&!IUnknown_Release(&device.iface));}
    cases++;
 }
 for(UINT kind=0;kind<10;kind++){
    Backend b=new_backend(1);Parent8 d=new_parent(),container=new_parent();IDirect3DSurface8*out=(void*)99;HRESULT expected=D3DERR_INVALIDCALL;
    if(kind==0)CHECK(d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&d.iface,&d.iface,FALSE,NULL)==expected);
    else {if(kind==1)b.profile=FALSE;if(kind==4)b.fail_get=TRUE;if(kind==5||kind==6){fail_allocation=allocations+(kind==5?1:2);expected=E_OUTOFMEMORY;}if(kind==4)expected=-77;if(kind==7)b.race=2;
        if(kind==8||kind==9){expected=D3D_OK;b.race=kind==8?1:0;}
        CHECK(d8wg_core_wrap_surface8(&b.iface,kind==2?NULL:(IDirect3DDevice8*)&d.iface,kind==3?NULL:&d.iface,FALSE,&out)==expected);
        if(expected==D3D_OK){CHECK(out&&b.refs==(kind==8?2:1)&&d.refs==2);if(kind==8){CHECK(out->lpVtbl->AddRef(out)==2);CHECK(backend_release((IUnknown*)&b)==1);CHECK(out->lpVtbl->Release(out)==1);}CHECK(!out->lpVtbl->Release(out));}else CHECK(!out);
    }
    fail_allocation=0;CHECK(!b.alive&&b.destroys==1&&d.refs==1&&container.refs==1&&!live&&!IUnknown_Release(&d.iface));cases++;
 }
 {Parent8 d=new_parent();IDirect3DSurface8*out=(void*)99;CHECK(d8wg_core_wrap_surface8(NULL,(IDirect3DDevice8*)&d.iface,&d.iface,FALSE,&out)==D3DERR_INVALIDCALL&&!out&&d.refs==1&&!live);cases++;}
 {Backend b=new_backend(1);Parent8 d=new_parent(),other=new_parent();IDirect3DSurface8*out=NULL,*bad=(void*)99;CHECK(!d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&d.iface,&d.iface,FALSE,&out));backend_add((IUnknown*)&b);CHECK(d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&other.iface,&d.iface,FALSE,&bad)==D3DERR_INVALIDCALL&&!bad&&b.refs==1&&d.refs==2&&other.refs==1);backend_add((IUnknown*)&b);CHECK(d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&d.iface,&other.iface,FALSE,&bad)==D3DERR_INVALIDCALL&&!bad&&b.refs==1);backend_add((IUnknown*)&b);CHECK(d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&d.iface,&d.iface,TRUE,&bad)==D3DERR_INVALIDCALL&&!bad&&b.refs==1);CHECK(!out->lpVtbl->Release(out)&&!live);cases++;}
 {Backend b=new_backend(1);Parent8 d=new_parent();IDirect3DSurface8*out=NULL,*bad=(void*)99;CHECK(!d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&d.iface,&d.iface,FALSE,&out));IDirect3DSurface8Vtbl wrong={0};((D8CoreSurface*)out)->iface.lpVtbl=&wrong;backend_add((IUnknown*)&b);CHECK(d8wg_core_wrap_surface8(&b.iface,(IDirect3DDevice8*)&d.iface,&d.iface,FALSE,&bad)==D3DERR_INVALIDCALL&&!bad&&b.refs==1);((D8CoreSurface*)out)->iface.lpVtbl=&g_core_surface8_vtbl;CHECK(!out->lpVtbl->Release(out)&&!live);cases++;}
 printf("PASS actual SDK8 surface adapter: %u checks / %u cases; real11-method vtable,frontend identity/container/device queries,own/coupled refs andsingle core pin,cached public-zero revival,owned-input consumption/OOM/racing publication,typed descriptor/private/lock forwarding andexact cleanup;backend metadata/COM shim only\n",checks,cases);return 0;}
