#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "windows.h"
#include "../../runtime/d9wg_exclusive_window.h"
struct Window {BOOL live,unicode,visible,iconic,topmost;DWORD thread;WNDPROC proc;HANDLE property;unsigned sizes,displays,restores,activations,positions,edits;};
static unsigned allocations,fail_allocation,fail_prop,fail_proc,fail_mode,error,checks;
static HWND focus_window,output_window,foreground;
static D9WGExclusiveWindow *subscription,*second_subscription;
static D9WGResetState state,second_state;
static UINT screen_width=1024,screen_height=768;
static BOOL release_on_display,destroy_on_display,check_transition,release_on_position;
LONG InterlockedIncrement(LONG *p){return ++*p;}LONG InterlockedDecrement(LONG *p){assert(*p);return --*p;}
HANDLE GetProcessHeap(void){return NULL;}
void *HeapAlloc(HANDLE h,DWORD f,size_t n){(void)h;assert(f==8);if(fail_allocation&&!--fail_allocation)return NULL;void*p=calloc(1,n);assert(p);allocations++;return p;}
BOOL HeapFree(HANDLE h,DWORD f,void*p){(void)h;assert(!f&&p&&allocations);allocations--;free(p);return TRUE;}
BOOL IsWindow(HWND w){return w&&w->live;}BOOL IsWindowUnicode(HWND w){return w->unicode;}
BOOL IsWindowVisible(HWND w){return IsWindow(w)&&w->visible;}BOOL IsIconic(HWND w){return IsWindow(w)&&w->iconic;}
DWORD GetWindowThreadProcessId(HWND w,DWORD*p){assert(!p);return w->thread;}DWORD GetCurrentThreadId(void){return 7;}
HANDLE GetPropA(HWND w,const char*n){(void)n;return IsWindow(w)?w->property:NULL;}
BOOL SetPropA(HWND w,const char*n,HANDLE p){(void)n;if(fail_prop)return FALSE;w->property=p;return TRUE;}
HANDLE RemovePropA(HWND w,const char*n){(void)n;HANDLE p=w->property;w->property=NULL;return p;}
LONG_PTR GetWindowLongPtrA(HWND w,int i){assert(i==GWLP_WNDPROC&&!w->unicode);return (LONG_PTR)w->proc;}
LONG_PTR GetWindowLongPtrW(HWND w,int i){assert(i==GWLP_WNDPROC&&w->unicode);return (LONG_PTR)w->proc;}
static LONG_PTR set_proc(HWND w,int i,LONG_PTR p,BOOL unicode){assert(i==GWLP_WNDPROC&&w->unicode==unicode);if(fail_proc){error=1;return 0;}LONG_PTR old=(LONG_PTR)w->proc;w->proc=(WNDPROC)p;return old;}
LONG_PTR SetWindowLongPtrA(HWND w,int i,LONG_PTR p){return set_proc(w,i,p,0);}LONG_PTR SetWindowLongPtrW(HWND w,int i,LONG_PTR p){return set_proc(w,i,p,1);}
LRESULT CallWindowProcA(WNDPROC p,HWND w,UINT m,WPARAM a,LPARAM b){assert(!w->unicode);return p(w,m,a,b);}
LRESULT CallWindowProcW(WNDPROC p,HWND w,UINT m,WPARAM a,LPARAM b){assert(w->unicode);return p(w,m,a,b);}
LRESULT DefWindowProcA(HWND w,UINT m,WPARAM a,LPARAM b){(void)b;assert(!w->unicode);if(m==WM_SYSCOMMAND&&a==SC_RESTORE)w->iconic=0;return 91;}
LRESULT DefWindowProcW(HWND w,UINT m,WPARAM a,LPARAM b){(void)b;assert(w->unicode);if(m==WM_SYSCOMMAND&&a==SC_RESTORE)w->iconic=0;return 92;}
void SetLastError(DWORD v){error=v;}DWORD GetLastError(void){return error;}
static LRESULT application(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 (void)lp;if(msg==WM_WINDOWPOSCHANGED){w->positions++;
  if(release_on_position){release_on_position=0;d9wg_exclusive_window_leave(&subscription);d9wg_exclusive_window_leave(&second_subscription);}
 }
 if(msg==WM_SIZE)w->sizes++;
 if(msg==WM_DISPLAYCHANGE){w->displays++;
  if(check_transition)assert(state.required&&!state.lost);
  if(release_on_display){release_on_display=0;d9wg_exclusive_window_leave(&subscription);d9wg_exclusive_window_leave(&second_subscription);}
  if(destroy_on_display){destroy_on_display=0;w->proc(w,WM_NCDESTROY,0,0);w->live=0;}
 }
 if(msg==WM_ACTIVATEAPP){w->activations++;if(check_transition)assert(state.required&&state.lost==!wp);}
 if(msg==WM_SYSCOMMAND&&wp==SC_RESTORE){assert(!w->iconic);w->restores++;}
 return 73;
}
static LRESULT replacement(HWND w,UINT m,WPARAM a,LPARAM b){return application(w,m,a,b)+1;}
LONG ChangeDisplaySettingsA(DEVMODEA *mode,DWORD flags){
 if(fail_mode)return -1;
 assert(flags==CDS_FULLSCREEN);
 screen_width=mode?mode->dmPelsWidth:1024;screen_height=mode?mode->dmPelsHeight:768;
 if(IsWindow(focus_window))focus_window->proc(focus_window,WM_DISPLAYCHANGE,32,screen_width|(screen_height<<16));
 return 0;
}
BOOL ShowWindow(HWND w,int how){assert(IsWindow(w));w->edits++;w->visible=1;w->iconic=(how==SW_MINIMIZE || how==SW_SHOWMINIMIZED);w->proc(w,WM_SIZE,w->iconic,0);return 1;}
BOOL SetWindowPos(HWND w,HWND after,int x,int y,int width,int height,UINT flags){
 assert(IsWindow(w)&&!x&&!y);
 if(after==HWND_TOPMOST)w->topmost=1;
 if((after==HWND_TOPMOST||after==HWND_TOP)&&width){assert(w==output_window&&width==640&&height==480&&(flags&SWP_NOACTIVATE));w->edits++;w->proc(w,WM_SIZE,0,0);}
 else if(!after){assert(w==output_window&&width==640&&height==480&&flags==(SWP_NOACTIVATE|SWP_NOZORDER));w->proc(w,WM_WINDOWPOSCHANGED,0,0);}
 return 1;
}
HWND GetForegroundWindow(void){return foreground;}
BOOL SetForegroundWindow(HWND w){foreground=w;return 1;}HWND SetFocus(HWND w){assert(w==focus_window);return w;}
#define CHECK(v) do{assert(v);checks++;}while(0)
int main(void){
 for(unsigned unicode=0;unicode<2;unicode++)for(unsigned same=0;same<2;same++){
  struct Window focus={.live=1,.unicode=unicode,.visible=1,.thread=7,.proc=application},output=focus;
  focus_window=&focus;output_window=same?&focus:&output;state=(D9WGResetState){0};
  CHECK(d9wg_exclusive_window_enter(&subscription,&focus,output_window,640,480,&state));
  CHECK(foreground==&focus&&focus.proc!=application&&focus.property&&allocations==2);
  CHECK(!state.required&&!state.lost&&screen_width==640&&screen_height==480);
  CHECK(same?!focus.sizes:output.sizes==2);
  check_transition=1;focus.proc(&focus,WM_ACTIVATEAPP,0,0);check_transition=0;
  CHECK(state.required&&state.lost&&output_window->iconic&&screen_width==1024);
  CHECK(d9wg_reset_cooperative_result(&state)==(int32_t)0x88760868&&d9wg_reset_present_result(&state)==(int32_t)0x88760868);
  focus.iconic=1;focus.proc(&focus,WM_ACTIVATEAPP,1,0);CHECK(state.lost);
  focus.iconic=0;focus.proc(&focus,WM_ACTIVATEAPP,1,0);CHECK(!state.lost&&state.required);
  CHECK(d9wg_reset_cooperative_result(&state)==(int32_t)0x88760869);
  focus.proc(&focus,WM_ACTIVATEAPP,0,0);CHECK(state.lost);
  unsigned notifications=focus.activations,positions=output_window->positions;
  foreground=NULL;focus.proc(&focus,WM_ACTIVATE,1,0);CHECK(state.lost&&focus.activations==notifications&&output_window->positions==positions);
  foreground=&focus;focus.iconic=1;focus.proc(&focus,WM_ACTIVATE,1,0);CHECK(state.lost&&focus.activations==notifications);
  focus.iconic=0;focus.proc(&focus,WM_ACTIVATE,1,0);CHECK(!state.lost&&state.required&&focus.activations==notifications+1);
  CHECK(output_window->positions==positions+!same);
  focus.proc(&focus,WM_ACTIVATE,1,0);CHECK(focus.activations==notifications+1);
  CHECK(d9wg_exclusive_window_enter(&subscription,&focus,output_window,640,480,&state));d9wg_reset_complete(&state);
  CHECK(!d9wg_reset_cooperative_result(&state));
  focus.iconic=1;focus.proc(&focus,WM_SYSCOMMAND,SC_RESTORE,0);CHECK(!focus.iconic&&focus.restores==1);
  focus.proc(&focus,WM_DISPLAYCHANGE,32,800|(600<<16));CHECK(state.required&&!state.lost);
  d9wg_exclusive_window_leave(&subscription);CHECK(!subscription&&!allocations&&!focus.property&&focus.proc==application);
  CHECK(d9wg_exclusive_window_enter(&subscription,&focus,output_window,640,480,&state));focus.proc=replacement;
  d9wg_exclusive_window_leave(&subscription);CHECK(focus.proc==replacement&&!allocations&&!focus.property);focus.proc=application;
  CHECK(d9wg_exclusive_window_enter(&subscription,&focus,output_window,640,480,&state));
  CHECK(d9wg_exclusive_window_enter(&second_subscription,&focus,output_window,640,480,&second_state));CHECK(allocations==3);
  release_on_display=1;focus.proc(&focus,WM_ACTIVATEAPP,0,0);CHECK(!subscription&&!second_subscription&&!allocations&&focus.proc==application);
  CHECK(d9wg_exclusive_window_enter(&subscription,&focus,output_window,640,480,&state));
  if(!same){
   focus.proc(&focus,WM_ACTIVATEAPP,0,0);foreground=&focus;release_on_position=1;
   focus.proc(&focus,WM_ACTIVATE,1,0);CHECK(!subscription&&!allocations&&focus.proc==application);
   CHECK(d9wg_exclusive_window_enter(&subscription,&focus,output_window,640,480,&state));
  }
  destroy_on_display=1;focus.proc(&focus,WM_ACTIVATEAPP,0,0);CHECK(!focus.live&&!focus.property&&allocations==1);
  d9wg_exclusive_window_leave(&subscription);CHECK(!allocations);
 }
 /* API9 retains styles and skips output changes under NOWINDOWCHANGES,
  * while focus subclassing and device loss still operate in every mode. */
 for(unsigned unicode=0;unicode<2;unicode++)for(unsigned same=0;same<2;same++)
 for(unsigned visible=0;visible<2;visible++)for(unsigned nowc=0;nowc<2;nowc++){
  struct Window focus={.live=1,.unicode=unicode,.visible=visible,.thread=7,.proc=application},output=focus;
  focus_window=&focus;output_window=same?&focus:&output;foreground=NULL;state=(D9WGResetState){0};
  UINT flags=D9WG_EXCLUSIVE_D3D9|(nowc?D9WG_EXCLUSIVE_NO_WINDOW_CHANGES:0);
  CHECK(d9wg_exclusive_window_enter_ex(&subscription,&focus,output_window,640,480,&state,flags));
  CHECK(focus.proc!=application&&screen_width==640&&screen_height==480&&!state.lost);
  CHECK(output_window->edits==(nowc?0u:2u));
  CHECK(output_window->visible==(BOOL)(nowc?visible:1)&&output_window->topmost==(!nowc&&!visible));
  check_transition=1;focus.proc(&focus,WM_ACTIVATEAPP,0,0);check_transition=0;
  CHECK(state.lost&&state.required&&screen_width==1024);
  CHECK(output_window->iconic==!nowc&&output_window->topmost==!nowc);
  CHECK(output_window->edits==(nowc?0u:3u));
  focus.iconic=1;foreground=&focus;focus.proc(&focus,WM_ACTIVATEAPP,1,0);
  CHECK(!state.lost&&state.required);
  d9wg_exclusive_window_leave(&subscription);
  CHECK(!subscription&&!allocations&&!focus.property&&focus.proc==application);
 }
 for(unsigned kind=0;kind<5;kind++){
  struct Window focus={.live=1,.thread=7,.proc=application};focus_window=output_window=&focus;
  fail_allocation=kind<2?kind+1:0;fail_prop=kind==2;fail_proc=kind==3;if(kind==4)focus.thread=9;
  CHECK(!d9wg_exclusive_window_enter(&subscription,&focus,&focus,640,480,&state));CHECK(!subscription&&!allocations&&!focus.property&&focus.proc==application);
  fail_allocation=fail_prop=fail_proc=0;
 }
 printf("PASS exclusive window native: %u checks; ANSI/Unicode, separate focus/output, lost/recovery ordering, shared hooks, callback teardown, destroyed/replaced windows and allocation/property/proc failure cleanup\n",checks);
}
