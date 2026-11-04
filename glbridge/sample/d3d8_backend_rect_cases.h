/* Shared SDK8/private-core8 operations. Pixel expectations are constructed
 * independently of the proxy and include ordered partial failure/overlap. */
void rc_check(BOOL,const char*);void rc_report(const char*);extern UINT rc_words;
static HMODULE rc_module;static RCDevice*rc_device;
static void rc_hr(HRESULT hr,const char*s){rc_check(hr==D3D_OK,s);}
static DWORD rc_scene[64],rc_tile[16],rc_snapshot[16];
static void rc_verify(RCSurface*s,UINT w,UINT h,const DWORD*p){D3DLOCKED_RECT l;rc_hr(RC_LOCK(s,&l,NULL,16),"FAIL verify lock");for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){rc_check(((DWORD*)((BYTE*)l.pBits+y*l.Pitch))[x]==p[y*w+x],"FAIL copy pixels");rc_words++;}rc_hr(RC_UNLOCK(s),"FAIL verify unlock");}
static void rc_read(RCSurface*s,UINT w,UINT h,const DWORD*p){RCSurface*t=NULL;rc_hr(rc_image(w,h,&t),"FAIL read image");rc_hr(rc_copy(s,NULL,0,t,NULL),"FAIL read copy");rc_verify(t,w,h,p);rc_check(!RC_SREL(t),"FAIL image refs");}
static void rc_cases(void){RCSurface*back=NULL,*image=NULL,*tex=NULL,*cube=NULL;RCBase*texture=NULL,*parent=NULL;rc_hr(rc_back(&back),"FAIL back");rc_hr(rc_texture(&tex,&texture),"FAIL texture");rc_hr(rc_cube(&cube,&parent),"FAIL cube");rc_hr(rc_image(4,4,&image),"FAIL tile image");rc_hr(RC_CLEAR(rc_device,0,NULL,1,0xff336699u,1,0),"FAIL clear");for(UINT i=0;i<64;i++)rc_scene[i]=0xff336699u;
 rc_hr(rc_copy(back,NULL,0,tex,NULL),"FAIL GPU texture copy");rc_read(tex,8,8,rc_scene);
 for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)rc_tile[y*4+x]=0xff000000u|((17+x*41)<<16)|((23+y*37)<<8)|(x+y*4);
 D3DLOCKED_RECT l;rc_hr(RC_LOCK(image,&l,NULL,0),"FAIL tile lock");for(UINT y=0;y<4;y++)CopyMemory((BYTE*)l.pBits+y*l.Pitch,rc_tile+y*4,16);rc_check(rc_copy(image,NULL,0,cube,NULL)==D3DERR_INVALIDCALL,"FAIL locked source accepted");rc_hr(RC_UNLOCK(image),"FAIL tile unlock");
 rc_hr(rc_copy(image,NULL,0,cube,NULL),"FAIL CPU cube upload");RECT full={0,0,4,4};POINT placed={2,2};rc_hr(rc_copy(cube,&full,1,back,&placed),"FAIL cube GPU identity");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)rc_scene[(y+2)*8+x+2]=rc_tile[y*4+x];rc_read(back,8,8,rc_scene);
 CopyMemory(rc_snapshot,rc_tile,sizeof(rc_tile));RECT overlap={0,0,3,3};POINT shifted={1,1};rc_hr(rc_copy(image,&overlap,1,image,&shifted),"FAIL CPU overlap");for(UINT y=0;y<3;y++)for(UINT x=0;x<3;x++)rc_tile[(y+1)*4+x+1]=rc_snapshot[y*4+x];rc_verify(image,4,4,rc_tile);
 rc_hr(rc_copy(image,&full,0,back,NULL),"FAIL zero rectangles");rc_hr(rc_copy(image,NULL,0,back,&placed),"FAIL zero points");RECT rects[3]={{0,0,2,2},{2,2,4,4},{-1,0,1,1}};POINT points[3]={{0,0},{0,0},{0,0}};rc_check(rc_copy(image,rects,3,back,points)==D3DERR_INVALIDCALL,"FAIL partial array result");for(UINT y=0;y<2;y++)for(UINT x=0;x<2;x++)rc_scene[y*8+x]=rc_tile[(y+2)*4+x+2];rc_read(back,8,8,rc_scene);
 CopyMemory(rc_tile,rc_snapshot,sizeof(rc_tile));rc_hr(rc_copy(cube,&overlap,1,cube,&shifted),"FAIL cube overlap");for(UINT y=0;y<3;y++)for(UINT x=0;x<3;x++)rc_tile[(y+1)*4+x+1]=rc_snapshot[y*4+x];rc_hr(rc_copy(cube,&full,1,back,&placed),"FAIL copied cube GPU bytes");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)rc_scene[(y+2)*8+x+2]=rc_tile[y*4+x];rc_read(back,8,8,rc_scene);
 POINT invalid={0x7fffffff,0};rc_check(rc_copy(image,&full,1,back,&invalid)==D3DERR_INVALIDCALL,"FAIL destination bounds");RECT outside={0,0,5,4};rc_check(rc_copy(image,&outside,1,back,NULL)==D3DERR_INVALIDCALL,"FAIL source bounds");
 RC_SREL(image);RC_SREL(tex);RC_BREL(texture);RC_SREL(cube);RC_BREL(parent);RC_SREL(back);
}
static void rc_after_reset(void){RCSurface*s=NULL;rc_hr(rc_back(&s),"FAIL reset back");rc_hr(RC_CLEAR(rc_device,0,NULL,1,0xff11aa55u,1,0),"FAIL reset clear");for(UINT i=0;i<15;i++)rc_scene[i]=0xff11aa55u;rc_read(s,5,3,rc_scene);RC_SREL(s);}
