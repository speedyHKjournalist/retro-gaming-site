#ifndef D9WG_TEST_WINDOW_WINDOWS_H
#define D9WG_TEST_WINDOW_WINDOWS_H
#include "../native/windows.h"
typedef uintptr_t HWND,HDC;
typedef struct WindowMockRegion *HRGN;
typedef struct {LONG left,top,right,bottom;} RECT;
typedef struct {LONG x,y;} POINT;
typedef struct {DWORD dwSize,iType,nCount,nRgnSize;RECT rcBound;} RGNDATAHEADER;
typedef struct {RGNDATAHEADER rdh;char Buffer[1];} RGNDATA;
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3
#define RGN_AND 1
#define SW_RESTORE 9
HWND GetForegroundWindow(void);
BOOL IsWindow(HWND);BOOL IsWindowVisible(HWND);BOOL IsIconic(HWND);
BOOL GetClientRect(HWND,RECT*);BOOL GetWindowRect(HWND,RECT*);BOOL ClientToScreen(HWND,POINT*);
DWORD GetVersion(void);HDC GetDC(HWND);int ReleaseDC(HWND,HDC);int GetClipBox(HDC,RECT*);
HRGN CreateRectRgn(int,int,int,int);HRGN CreateRectRgnIndirect(const RECT*);
BOOL DeleteObject(HRGN);int GetRandomRgn(HDC,HRGN,int);int OffsetRgn(HRGN,int,int);
int CombineRgn(HRGN,HRGN,HRGN,int);int GetRgnBox(HRGN,RECT*);
DWORD GetRegionData(HRGN,DWORD,RGNDATA*);BOOL EqualRect(const RECT*,const RECT*);
BOOL IsRectEmpty(const RECT*);
#endif
