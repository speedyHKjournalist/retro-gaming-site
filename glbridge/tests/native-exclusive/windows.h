#ifndef TEST_EXCLUSIVE_WINDOWS_H
#define TEST_EXCLUSIVE_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t UINT,DWORD;typedef int32_t LONG;typedef int BOOL;typedef unsigned char BYTE;
typedef intptr_t LONG_PTR,LRESULT,LPARAM;typedef uintptr_t WPARAM;typedef void *HANDLE;
typedef struct Window *HWND;typedef LRESULT(*WNDPROC)(HWND,UINT,WPARAM,LPARAM);
typedef struct {UINT dmSize,dmPelsWidth,dmPelsHeight,dmBitsPerPel,dmFields;} DEVMODEA;
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define HEAP_ZERO_MEMORY 8
#define WM_SIZE 5
#define WM_ACTIVATEAPP 28
#define WM_ACTIVATE 6
#define WM_WINDOWPOSCHANGED 71
#define WA_INACTIVE 0
#define WM_DISPLAYCHANGE 126
#define WM_NCDESTROY 130
#define WM_SYSCOMMAND 274
#define SC_RESTORE 0xf120
#define GWLP_WNDPROC (-4)
#define SW_MINIMIZE 6
#define SW_SHOW 5
#define SWP_NOMOVE 2
#define SWP_NOSIZE 1
#define SWP_NOACTIVATE 16
#define SWP_NOZORDER 4
#define HWND_TOP ((HWND)(uintptr_t)1)
#define HWND_TOPMOST ((HWND)(uintptr_t)2)
#define CDS_FULLSCREEN 4
#define DISP_CHANGE_SUCCESSFUL 0
#define DM_PELSWIDTH 1
#define DM_PELSHEIGHT 2
#define DM_BITSPERPEL 4
#define LOWORD(v) ((UINT)(v)&0xffff)
#define HIWORD(v) (((UINT)(v)>>16)&0xffff)
LONG InterlockedIncrement(LONG*);LONG InterlockedDecrement(LONG*);
HANDLE GetProcessHeap(void);void *HeapAlloc(HANDLE,DWORD,size_t);BOOL HeapFree(HANDLE,DWORD,void*);
BOOL IsWindow(HWND);BOOL IsWindowUnicode(HWND);BOOL IsWindowVisible(HWND);BOOL IsIconic(HWND);
DWORD GetWindowThreadProcessId(HWND,DWORD*);DWORD GetCurrentThreadId(void);
HANDLE GetPropA(HWND,const char*);BOOL SetPropA(HWND,const char*,HANDLE);HANDLE RemovePropA(HWND,const char*);
LONG_PTR GetWindowLongPtrA(HWND,int);LONG_PTR GetWindowLongPtrW(HWND,int);
LONG_PTR SetWindowLongPtrA(HWND,int,LONG_PTR);LONG_PTR SetWindowLongPtrW(HWND,int,LONG_PTR);
LRESULT CallWindowProcA(WNDPROC,HWND,UINT,WPARAM,LPARAM);LRESULT CallWindowProcW(WNDPROC,HWND,UINT,WPARAM,LPARAM);
LRESULT DefWindowProcA(HWND,UINT,WPARAM,LPARAM);LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM);
void SetLastError(DWORD);DWORD GetLastError(void);
LONG ChangeDisplaySettingsA(DEVMODEA*,DWORD);BOOL ShowWindow(HWND,int);
HWND GetForegroundWindow(void);
BOOL SetWindowPos(HWND,HWND,int,int,int,int,UINT);BOOL SetForegroundWindow(HWND);HWND SetFocus(HWND);
#endif
