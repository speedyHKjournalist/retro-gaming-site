/* Test-only Win32 declarations for host-compiled C behavior tests. */
#ifndef TEST_WINDOWS_H
#define TEST_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef int BOOL;
typedef void *HANDLE,*HINSTANCE,*LPVOID;
typedef struct {LONG held;} CRITICAL_SECTION;
typedef struct {DWORD dwLowDateTime,dwHighDateTime;} FILETIME;
typedef union {struct {DWORD LowPart;LONG HighPart;};int64_t QuadPart;} LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define PAGE_READWRITE 4
#define FILE_MAP_ALL_ACCESS 15
DWORD GetCurrentProcessId(void);DWORD GetTickCount(void);
void GetSystemTimeAsFileTime(FILETIME*);BOOL QueryPerformanceCounter(LARGE_INTEGER*);
HANDLE CreateFileMappingA(HANDLE,void*,DWORD,DWORD,DWORD,const char*);
void *MapViewOfFile(HANDLE,DWORD,DWORD,DWORD,size_t);
BOOL UnmapViewOfFile(const void*);BOOL CloseHandle(HANDLE);
LONG InterlockedIncrement(volatile LONG*);LONG InterlockedDecrement(volatile LONG*);
void InitializeCriticalSection(CRITICAL_SECTION*);void DeleteCriticalSection(CRITICAL_SECTION*);
void EnterCriticalSection(CRITICAL_SECTION*);void LeaveCriticalSection(CRITICAL_SECTION*);
BOOL DisableThreadLibraryCalls(HINSTANCE);

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define ERROR_INVALID_DATA 13u
#define ZeroMemory(p,n) memset(p,0,n)
HANDLE CreateFileA(const char*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
BOOL DeviceIoControl(HANDLE,DWORD,const void*,DWORD,void*,DWORD,DWORD*,void*);
DWORD GetLastError(void);void SetLastError(DWORD);
#endif
