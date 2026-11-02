#ifndef D9WG_WINDOW_MOCK_H
#define D9WG_WINDOW_MOCK_H
#include <windows.h>
enum {FAIL_CLIENT=1,FAIL_ORIGIN=2,FAIL_OUTER=4,FAIL_DC=8,FAIL_CREATE=16,FAIL_RANDOM=32,FAIL_DATA_SIZE=64,FAIL_DATA_READ=128};
typedef struct {
 HWND foreground;BOOL valid,visible,iconic;RECT client,outer;POINT origin;
 uint32_t failures,non_nt,clip_kind,region_count;RECT screen_rects[40];
} WindowFixture;
typedef struct {unsigned foreground,valid,visible,iconic,client,origin,outer,dc,release,create,deleted,random,clip,data;} WindowCalls;
extern WindowFixture fixture;extern WindowCalls calls;
void window_mock_reset(void);
#endif
