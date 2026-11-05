#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
static const GUID dv_private_tag={0x86456789,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
HRESULT WINAPI dv9_private(IDirect3DSurface9*surface,IUnknown*object){return IDirect3DSurface9_SetPrivateData(surface,&dv_private_tag,object,sizeof(object),D3DSPD_IUNKNOWN);}

void sc_check(BOOL,const char*);
static void ds9_hr(HRESULT hr){sc_check(hr==D3D_OK,"FAIL SDK9 shader storage method");}
HRESULT WINAPI ds9_bind(IDirect3DDevice9*d,IUnknown*decl,IUnknown*vs,IUnknown*ps){HRESULT hr=IDirect3DDevice9_SetVertexDeclaration(d,(IDirect3DVertexDeclaration9*)decl);if(SUCCEEDED(hr))hr=IDirect3DDevice9_SetVertexShader(d,(IDirect3DVertexShader9*)vs);if(SUCCEEDED(hr))hr=IDirect3DDevice9_SetPixelShader(d,(IDirect3DPixelShader9*)ps);return hr;}
void ds9_metadata(IDirect3DDevice9*d,IUnknown*decl,IUnknown*vs,IUnknown*ps){
 D3DVERTEXELEMENT9 elements[2];UINT count=2;ds9_hr(IDirect3DVertexDeclaration9_GetDeclaration((IDirect3DVertexDeclaration9*)decl,elements,&count));sc_check(count==2&&elements[0].Stream==0&&elements[0].Offset==0&&elements[0].Type==3&&elements[0].Method==0&&elements[0].Usage==0&&elements[0].UsageIndex==0&&elements[1].Stream==255&&elements[1].Type==17,"FAIL held declaration bytes");
 const DWORD vertex[]={0xfffe0101u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0x90e40000u,0xffffu},pixel[]={0xffff0101u,1,0x800f0000u,0x90e40000u,0xffffu};DWORD code[8];UINT bytes=sizeof(code);ds9_hr(IDirect3DVertexShader9_GetFunction((IDirect3DVertexShader9*)vs,code,&bytes));sc_check(bytes==sizeof(vertex),"FAIL held VS size");for(UINT i=0;i<8;i++)sc_check(code[i]==vertex[i],"FAIL held VS bytes");bytes=sizeof(code);ds9_hr(IDirect3DPixelShader9_GetFunction((IDirect3DPixelShader9*)ps,code,&bytes));sc_check(bytes==sizeof(pixel),"FAIL held PS size");for(UINT i=0;i<5;i++)sc_check(code[i]==pixel[i],"FAIL held PS bytes");
 IDirect3DDevice9*owner=NULL;ds9_hr(IDirect3DVertexDeclaration9_GetDevice((IDirect3DVertexDeclaration9*)decl,&owner));sc_check(owner==d,"FAIL held declaration device");IDirect3DDevice9_Release(owner);ds9_hr(IDirect3DVertexShader9_GetDevice((IDirect3DVertexShader9*)vs,&owner));sc_check(owner==d,"FAIL held VS device");IDirect3DDevice9_Release(owner);ds9_hr(IDirect3DPixelShader9_GetDevice((IDirect3DPixelShader9*)ps,&owner));sc_check(owner==d,"FAIL held PS device");IDirect3DDevice9_Release(owner);
}
void ds9_query(IDirect3DDevice9*d,IUnknown*decl,IUnknown*vs,IUnknown*ps){IDirect3DVertexDeclaration9*a=NULL;IDirect3DVertexShader9*b=NULL;IDirect3DPixelShader9*c=NULL;ds9_hr(IDirect3DDevice9_GetVertexDeclaration(d,&a));ds9_hr(IDirect3DDevice9_GetVertexShader(d,&b));ds9_hr(IDirect3DDevice9_GetPixelShader(d,&c));sc_check((IUnknown*)a==decl&&(IUnknown*)b==vs&&(IUnknown*)c==ps,"FAIL held shader binding identity");ds9_metadata(d,decl,vs,ps);sc_check(!IDirect3DVertexDeclaration9_Release(a)&&!IDirect3DVertexShader9_Release(b)&&!IDirect3DPixelShader9_Release(c),"FAIL public-zero shader query revival");}

void ds9_empty(IDirect3DDevice9*d){IDirect3DVertexDeclaration9*a=(void*)99;IDirect3DVertexShader9*b=(void*)99;IDirect3DPixelShader9*c=(void*)99;ds9_hr(IDirect3DDevice9_GetVertexDeclaration(d,&a));ds9_hr(IDirect3DDevice9_GetVertexShader(d,&b));ds9_hr(IDirect3DDevice9_GetPixelShader(d,&c));sc_check(!a&&!b&&!c,"FAIL owned cleanup left live shader bindings");}
void ds9_child(IDirect3DDevice9*d,IDirect3DVertexBuffer9*child){IDirect3DDevice9*owner=NULL;ds9_hr(IDirect3DVertexBuffer9_GetDevice(child,&owner));sc_check(owner==d,"FAIL cleanup child device identity");IDirect3DDevice9_Release(owner);}
