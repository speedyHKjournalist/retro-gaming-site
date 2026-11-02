#ifndef D9WG_WATCH_TEST_WINDOWS_H
#define D9WG_WATCH_TEST_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef int BOOL;
typedef void *HANDLE,*HMODULE,*HINSTANCE,*HWND;
typedef uintptr_t UINT_PTR,WPARAM;typedef intptr_t LPARAM;
typedef pthread_mutex_t CRITICAL_SECTION;
typedef struct MockHook *HWINEVENTHOOK;
typedef void (*WINEVENTPROC)(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
typedef struct{HWND hwnd;UINT message;WPARAM wParam;LPARAM lParam;}MSG;
#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define WM_QUIT 0x12u
#define WM_USER 0x400u
#define WM_APP 0x8000u
#define WM_TIMER 0x113u
#define PM_NOREMOVE 0u
#define EVENT_SYSTEM_FOREGROUND 3u
#define EVENT_OBJECT_DESTROY 0x8001u
#define EVENT_OBJECT_SHOW 0x8002u
#define EVENT_OBJECT_HIDE 0x8003u
#define EVENT_OBJECT_REORDER 0x8004u
#define EVENT_OBJECT_LOCATIONCHANGE 0x800bu
#define OBJID_WINDOW 0
#define CHILDID_SELF 0
#define WINEVENT_OUTOFCONTEXT 0u
#define HEAP_ZERO_MEMORY 8u
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4u
#define DUPLICATE_SAME_ACCESS 2u
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xffffffffu
void *InterlockedCompareExchangePointer(void*volatile*,void*,void*);
void EnterCriticalSection(CRITICAL_SECTION*);void LeaveCriticalSection(CRITICAL_SECTION*);
DWORD GetTickCount(void);HANDLE GetProcessHeap(void);void *HeapAlloc(HANDLE,DWORD,size_t);BOOL HeapFree(HANDLE,DWORD,void*);
BOOL GetModuleHandleExA(DWORD,const char*,HMODULE*);BOOL FreeLibrary(HMODULE);
void FreeLibraryAndExitThread(HMODULE,DWORD);
HANDLE CreateEventA(void*,BOOL,BOOL,const char*);BOOL SetEvent(HANDLE);DWORD WaitForSingleObject(HANDLE,DWORD);
HANDLE GetCurrentProcess(void);BOOL DuplicateHandle(HANDLE,HANDLE,HANDLE,HANDLE*,DWORD,BOOL,DWORD);
HANDLE CreateThread(void*,size_t,DWORD (*)(void*),void*,DWORD,DWORD*);BOOL CloseHandle(HANDLE);
BOOL PeekMessageA(MSG*,HWND,UINT,UINT,UINT);int GetMessageA(MSG*,HWND,UINT,UINT);
BOOL PostThreadMessageA(DWORD,UINT,WPARAM,LPARAM);
HWINEVENTHOOK SetWinEventHook(DWORD,DWORD,HMODULE,WINEVENTPROC,DWORD,DWORD,DWORD);
BOOL UnhookWinEvent(HWINEVENTHOOK);UINT_PTR SetTimer(HWND,UINT_PTR,UINT,void*);BOOL KillTimer(HWND,UINT_PTR);
#endif
