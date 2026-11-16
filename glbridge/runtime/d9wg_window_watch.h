#ifndef D9WG_WINDOW_WATCH_H
#define D9WG_WINDOW_WATCH_H
#include <windows.h>
#include <stdint.h>

/* Intrusive subscriptions have no fixed device limit. add/remove synchronize
 * with callbacks using the process transport lock; remove returns only after
 * any callback using the object has finished. Call remove before freeing it.
 * A callback may remove itself or another subscription. No object pointer is
 * retained across callbacks. Hooks and their message pump live on a private
 * thread, including their same-thread unregistration. */
typedef int (*D9WGWindowNotify)(void* context);
typedef void (*D9WGWindowFlush)(void);
typedef struct D9WGWindowWatch {
    struct D9WGWindowWatch* next;
    void* context;
    D9WGWindowNotify notify;
    uint32_t epoch;
} D9WGWindowWatch;

BOOL d9wg_window_watch_add(D9WGWindowWatch* watch, void* context,
    D9WGWindowNotify notify, D9WGWindowFlush flush, CRITICAL_SECTION* lock);
void d9wg_window_watch_remove(D9WGWindowWatch* watch);
/* A failed optional allocation/start is retried at most once per second.
 * Present invokes this without repeating any window geometry/GDI query. */
void d9wg_window_watch_retry(void);
#endif
