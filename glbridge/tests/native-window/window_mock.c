#include "window_mock.h"
#include <assert.h>
#include <string.h>
struct WindowMockRegion {unsigned count,live;RECT rects[40];};
static struct WindowMockRegion pool[4];
WindowFixture fixture;WindowCalls calls;
void window_mock_reset(void){memset(&fixture,0,sizeof(fixture));memset(&calls,0,sizeof(calls));memset(pool,0,sizeof(pool));fixture=(WindowFixture){.foreground=1,.valid=1,.visible=1,.client={0,0,160,100},.outer={12,0,188,138},.origin={20,30},.clip_kind=SIMPLEREGION,.region_count=1,.screen_rects={{20,30,180,130}}};}
HWND GetForegroundWindow(void){calls.foreground++;return fixture.foreground;}
BOOL IsWindow(HWND w){calls.valid++;return w&&fixture.valid;}
BOOL IsWindowVisible(HWND w){assert(w);calls.visible++;return fixture.visible;}
BOOL IsIconic(HWND w){assert(w);calls.iconic++;return fixture.iconic;}
BOOL GetClientRect(HWND w,RECT *r){assert(w);calls.client++;if(fixture.failures&FAIL_CLIENT)return FALSE;*r=fixture.client;return TRUE;}
BOOL ClientToScreen(HWND w,POINT *p){assert(w);calls.origin++;if(fixture.failures&FAIL_ORIGIN)return FALSE;p->x+=fixture.origin.x;p->y+=fixture.origin.y;return TRUE;}
BOOL GetWindowRect(HWND w,RECT *r){assert(w);calls.outer++;if(fixture.failures&FAIL_OUTER)return FALSE;*r=fixture.outer;return TRUE;}
DWORD GetVersion(void){return fixture.non_nt?0x80000000u:0;}
HDC GetDC(HWND w){assert(w);calls.dc++;return fixture.failures&FAIL_DC?0:1;}
int ReleaseDC(HWND w,HDC dc){assert(w&&dc);calls.release++;return 1;}
int GetClipBox(HDC dc,RECT *r){assert(dc);calls.clip++;*r=fixture.client;return (int)fixture.clip_kind;}
static int kind(HRGN r){return r->count>1?COMPLEXREGION:r->count?SIMPLEREGION:NULLREGION;}
HRGN CreateRectRgn(int l,int t,int r,int b){calls.create++;if(fixture.failures&FAIL_CREATE)return NULL;for(unsigned i=0;i<4;++i)if(!pool[i].live){pool[i].live=1;pool[i].count=r>l&&b>t;pool[i].rects[0]=(RECT){l,t,r,b};return &pool[i];}assert(0);return NULL;}
HRGN CreateRectRgnIndirect(const RECT *r){return CreateRectRgn(r->left,r->top,r->right,r->bottom);}
BOOL DeleteObject(HRGN r){assert(r&&r->live);r->live=0;calls.deleted++;return TRUE;}
int GetRandomRgn(HDC dc,HRGN r,int type){assert(dc&&r&&type==4);calls.random++;if(fixture.failures&FAIL_RANDOM)return 0;r->count=fixture.region_count;assert(r->count<=40);memcpy(r->rects,fixture.screen_rects,r->count*sizeof(RECT));return 1;}
int OffsetRgn(HRGN r,int x,int y){for(unsigned i=0;i<r->count;++i){r->rects[i].left+=x;r->rects[i].right+=x;r->rects[i].top+=y;r->rects[i].bottom+=y;}return kind(r);}
int CombineRgn(HRGN dst,HRGN src,HRGN clip,int mode){assert(mode==RGN_AND&&clip->count==1);RECT c=clip->rects[0];unsigned n=0;for(unsigned i=0;i<src->count;++i){RECT r=src->rects[i];if(r.left<c.left)r.left=c.left;if(r.top<c.top)r.top=c.top;if(r.right>c.right)r.right=c.right;if(r.bottom>c.bottom)r.bottom=c.bottom;if(r.right>r.left&&r.bottom>r.top)dst->rects[n++]=r;}dst->count=n;return kind(dst);}
int GetRgnBox(HRGN r,RECT *out){assert(r->count==1);*out=r->rects[0];return kind(r);}
DWORD GetRegionData(HRGN r,DWORD bytes,RGNDATA *out){calls.data++;DWORD required=(DWORD)(sizeof(RGNDATAHEADER)+r->count*sizeof(RECT));if(!out)return fixture.failures&FAIL_DATA_SIZE?0:required;if(bytes<required||fixture.failures&FAIL_DATA_READ)return 0;out->rdh=(RGNDATAHEADER){.dwSize=sizeof(RGNDATAHEADER),.iType=1,.nCount=r->count,.nRgnSize=r->count*sizeof(RECT)};memcpy(out->Buffer,r->rects,r->count*sizeof(RECT));return required;}
BOOL EqualRect(const RECT *a,const RECT *b){return !memcmp(a,b,sizeof(*a));}
BOOL IsRectEmpty(const RECT *r){return r->right<=r->left||r->bottom<=r->top;}
