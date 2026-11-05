typedef struct SwapBackend {Backend core,back;BOOL additional,empty_back;UINT calls[2],index,type;const RECT*source,*destination;HWND window;const RGNDATA*dirty;HRESULT present_error,back_error;}SwapBackend;
static SwapBackend new_swap_backend(void){SwapBackend s={0};s.core=new_backend(1);s.core.kind=3;s.back=new_backend(0);s.additional=TRUE;return s;}
static void swap_rebind(SwapBackend*s){s->core.children=&s->back;s->back.storage_parent=&s->core;}
static HRESULT sw_present(struct IDirect3DSwapChain9*p,const RECT*src,const RECT*dst,HWND window,const RGNDATA*dirty){SwapBackend*s=(SwapBackend*)p;s->calls[0]++;s->source=src;s->destination=dst;s->window=window;s->dirty=dirty;return s->present_error;}
static HRESULT sw_back(struct IDirect3DSwapChain9*p,UINT index,UINT type,struct IDirect3DSurface9**out){SwapBackend*s=(SwapBackend*)p;s->calls[1]++;s->index=index;s->type=type;*out=NULL;if(index)return D3DERR_INVALIDCALL;if(s->back_error)return s->back_error;if(s->empty_back)return 0;*out=&s->back.iface;backend_add((IUnknown*)*out);return 0;}
static const D9WGSwapChainMethods8 swap_methods={sw_present,sw_back};
static const D9WGSwapChainMethods8*d9wg_d3d9_core_swap_chain_methods8(struct IDirect3DSwapChain9*p){SwapBackend*s=(SwapBackend*)p;return p&&p->lpVtbl==&backend_vtbl&&s->core.profile&&s->core.kind==3&&s->additional?&swap_methods:NULL;}
static int swap_forward(IDirect3DSwapChain8*x,SwapBackend*b){
 void*out=(void*)99;CHECK(x->lpVtbl->QueryInterface(x,NULL,&out)==E_NOINTERFACE&&!out&&x->lpVtbl->QueryInterface(x,&IID_IDirect3DResource8,&out)==E_NOINTERFACE&&!out&&x->lpVtbl->QueryInterface(x,&IID_IUnknown,NULL)==E_POINTER);
 const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DSwapChain8};for(UINT i=0;i<2;i++)CHECK(!x->lpVtbl->QueryInterface(x,ids[i],&out)&&out==x&&x->lpVtbl->Release(out)==1);
 for(UINT i=0;i<16;i++){GUID bad=IID_IDirect3DSwapChain8;((uint8_t*)&bad)[i]^=0x80;out=(void*)99;CHECK(x->lpVtbl->QueryInterface(x,&bad,&out)==E_NOINTERFACE&&!out);}
 RECT source={1,2,3,4},destination={5,6,7,8};RGNDATA dirty={{1,2,3,4}};HWND window=(void*)(uintptr_t)0x12345678;
 for(UINT fail=0;fail<2;fail++){b->present_error=fail?-77:0;CHECK(x->lpVtbl->Present(x,&source,&destination,window,&dirty)==b->present_error&&b->source==&source&&b->destination==&destination&&b->window==window&&b->dirty==&dirty);}b->present_error=0;
 CHECK(!x->lpVtbl->Present(x,NULL,NULL,NULL,NULL)&&!b->source&&!b->destination&&!b->window&&!b->dirty);return 0;
}
int main(void){CHECK(!retained_surface_main());UINT before=checks,cases=0;CHECK(!d8wg_core_swap_chain_backend8(NULL));
 for(UINT round=0;round<64;round++)for(UINT order=0;order<4;order++){
  SwapBackend b=new_swap_backend();swap_rebind(&b);Parent8 d=new_parent();IDirect3DSwapChain8*x=NULL;
  CHECK(!d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x)&&x&&b.core.refs==1&&d.refs==2&&live==2&&d8wg_core_swap_chain_backend8(x)==(struct IDirect3DSwapChain9*)&b.core.iface);CHECK(!swap_forward(x,&b));
  UINT count=allocations;fail_allocation=allocations+1;backend_add((IUnknown*)&b.core);IDirect3DSwapChain8*again=NULL;CHECK(!d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&again)&&again==x&&allocations==count&&again->lpVtbl->Release(again)==1&&b.core.refs==1);fail_allocation=0;
  IDirect3DSurface8*back=NULL,*back_again=NULL;CHECK(!x->lpVtbl->GetBackBuffer(x,0,0xffffffffu,&back)&&back&&b.type==0xffffffffu&&b.back.refs==1&&b.core.refs==1&&d.refs==3&&live==4&&!d8wg_core_swap_chain_backend8((IDirect3DSwapChain8*)back));
  void*container=NULL;CHECK(!back->lpVtbl->GetContainer(back,&IID_IDirect3DDevice8,&container)&&container==&d.iface&&IUnknown_Release((IUnknown*)container)==3);CHECK(back->lpVtbl->GetContainer(back,&IID_IDirect3DSwapChain8,&container)==E_NOINTERFACE&&!container);
  count=allocations;fail_allocation=allocations+1;CHECK(!x->lpVtbl->GetBackBuffer(x,0,1,&back_again)&&back_again==back&&back_again->lpVtbl->Release(back_again)==1&&allocations==count);fail_allocation=0;
  if(order==0){CHECK(!back->lpVtbl->Release(back)&&b.back.refs==0&&b.core.alive&&d.refs==2&&live==4);CHECK(!x->lpVtbl->Release(x)&&!b.core.alive&&!b.back.alive&&!live&&d.refs==1&&!IUnknown_Release(&d.iface));}
  else if(order==1){CHECK(IUnknown_Release(&d.iface)==2&&!x->lpVtbl->Release(x)&&b.core.alive&&b.core.refs==0&&b.back.refs==1&&d.refs==1);CHECK(!back->lpVtbl->Release(back)&&!b.core.alive&&!b.back.alive&&!live&&!d.refs);}
  else{b.back.private_refs=1;CHECK(!x->lpVtbl->Release(x)&&b.core.alive&&!b.core.refs&&d.refs==2);CHECK(!back->lpVtbl->Release(back)&&b.back.alive&&!b.back.refs&&d.refs==1&&live==4);
   backend_add((IUnknown*)&b.core);CHECK(!d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&again)&&again==x&&b.core.refs==1&&d.refs==2);
   if(order==2){CHECK(!again->lpVtbl->Release(again)&&b.core.alive&&d.refs==1);backend_add((IUnknown*)&b.back);CHECK(!d8wg_core_wrap_surface8(&b.back.iface,(IDirect3DDevice8*)&d.iface,&d.iface,FALSE,&back_again)&&back_again==back&&!back_again->lpVtbl->Release(back_again)&&d.refs==1);}
   else{CHECK(!again->lpVtbl->GetBackBuffer(again,0,0,&back_again)&&back_again==back&&d.refs==3);CHECK(!again->lpVtbl->Release(again)&&b.core.alive&&d.refs==2&&!back_again->lpVtbl->Release(back_again)&&d.refs==1);}
   b.back.private_refs=0;backend_retire(&b.back);CHECK(!b.core.alive&&!b.back.alive&&b.core.destroys==1&&b.back.destroys==1&&!live&&!IUnknown_Release(&d.iface));
  }cases++;
 }
 for(UINT failure=0;failure<9;failure++){
  SwapBackend b=new_swap_backend();swap_rebind(&b);Parent8 d=new_parent();IDirect3DSwapChain8*out=(void*)99;HRESULT expected=D3DERR_INVALIDCALL;
  if(failure==0)CHECK(d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,NULL)==expected);
  else{if(failure==1)b.core.profile=FALSE;if(failure==2)b.additional=FALSE;if(failure==4)b.core.fail_get=TRUE;if(failure==5||failure==6){fail_allocation=allocations+(failure==5?1:2);expected=E_OUTOFMEMORY;}if(failure==4)expected=-77;if(failure==7)b.core.race=2;if(failure==8)b.core.kind=0;
   CHECK(d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,failure==3?NULL:(IDirect3DDevice8*)&d.iface,&out)==expected&&!out);
  }fail_allocation=0;CHECK(!b.core.alive&&!b.back.alive&&!live&&d.refs==1);cases++;
 }
 {SwapBackend b=new_swap_backend();swap_rebind(&b);Parent8 d=new_parent();b.core.race=1;IDirect3DSwapChain8*x=NULL;CHECK(!d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x)&&x&&b.core.refs==2&&d.refs==2);CHECK(x->lpVtbl->AddRef(x)==2&&backend_release((IUnknown*)&b.core)==1&&x->lpVtbl->Release(x)==1&&!x->lpVtbl->Release(x)&&!live&&d.refs==1);cases++;}
 {SwapBackend b=new_swap_backend();swap_rebind(&b);Parent8 d=new_parent(),other=new_parent();IDirect3DSwapChain8*x=NULL,*out=(void*)99;CHECK(!d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x));backend_add((IUnknown*)&b.core);CHECK(d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&other.iface,&out)==D3DERR_INVALIDCALL&&!out&&b.core.refs==1);IDirect3DSwapChain8Vtbl wrong={0};((D8CoreSwapChain*)x)->iface.lpVtbl=&wrong;backend_add((IUnknown*)&b.core);CHECK(d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&out)==D3DERR_INVALIDCALL&&!out);((D8CoreSwapChain*)x)->iface.lpVtbl=&g_core_swap_chain8_vtbl;CHECK(!x->lpVtbl->Release(x)&&!live);cases++;}
 {SwapBackend b=new_swap_backend();swap_rebind(&b);Parent8 d=new_parent();IDirect3DSwapChain8*x=NULL;IDirect3DSurface8*out=(void*)99;CHECK(!d8wg_core_wrap_swap_chain8((struct IDirect3DSwapChain9*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x));UINT calls=b.calls[1];CHECK(x->lpVtbl->GetBackBuffer(x,0,0,NULL)==D3DERR_INVALIDCALL&&b.calls[1]==calls);CHECK(x->lpVtbl->GetBackBuffer(x,1,0,&out)==D3DERR_INVALIDCALL&&out==(void*)99&&b.core.refs==1);b.back_error=-77;CHECK(x->lpVtbl->GetBackBuffer(x,0,0,&out)==-77&&out==(void*)99);b.back_error=0;b.empty_back=TRUE;CHECK(x->lpVtbl->GetBackBuffer(x,0,0,&out)==D3DERR_INVALIDCALL&&!out);b.empty_back=FALSE;
  for(UINT i=1;i<=2;i++){fail_allocation=allocations+i;out=(void*)99;CHECK(x->lpVtbl->GetBackBuffer(x,0,0,&out)==E_OUTOFMEMORY&&!out&&b.back.refs==0&&b.core.refs==1&&d.refs==2&&live==2);fail_allocation=0;}
  CHECK(!x->lpVtbl->Release(x)&&!live&&d.refs==1);cases++;
 }
 {Parent8 d=new_parent();IDirect3DSwapChain8*out=(void*)99;CHECK(d8wg_core_wrap_swap_chain8(NULL,(IDirect3DDevice8*)&d.iface,&out)==D3DERR_INVALIDCALL&&!out&&!live);cases++;}
 printf("PASS actual SDK8 SwapChain8 adapter: %u checks / %u cases; real5method vtable,Present args/status,GetBackBuffer atomicerroroutput/realSurface8/Device8container/independentrefs,weakcache/publiczero/deferredstorage/parent-first/device-first/race/OOM/ownedinput guards;backend explicitCOM/metadatashim\n",checks-before,cases);return 0;
}
