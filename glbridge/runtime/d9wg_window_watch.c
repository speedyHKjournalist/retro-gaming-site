#include "d9wg_window_watch.h"
#include "d9wg_runtime.h"

#define WINDOW_REFRESH (WM_APP + 0x19u)
#define WINDOW_RETRY_MS 1000u

typedef struct D9WGWindowWorker {
    DWORD thread_id;
    HANDLE ready;
    HMODULE module;
    HWINEVENTHOOK foreground, window;
    UINT_PTR timer;
    BOOL stopping, queue_ready, refresh_queued;
} D9WGWindowWorker;

static void* volatile window_lock_value;
static D9WGWindowWatch* watches;
static D9WGWindowWorker* worker;
static D9WGWindowFlush flush_reports;
static uint32_t epoch, failed_at;
static BOOL dispatching, pending, retry_armed;

static CRITICAL_SECTION* load_window_lock(void)
{
    return InterlockedCompareExchangePointer(&window_lock_value, NULL, NULL);
}

static void queue_refresh_locked(D9WGWindowWorker* current)
{
    if (current && !current->stopping && current->queue_ready && !current->refresh_queued)
        current->refresh_queued = PostThreadMessageA(current->thread_id, WINDOW_REFRESH, 0, 0);
}

static void notify_locked(D9WGWindowWorker* current)
{
    if (worker != current || current->stopping)
        return;
    if (dispatching) {
        pending = TRUE;
        return;
    }
    dispatching = TRUE;
    pending = FALSE;
    if (!++epoch) {
        epoch = 1;
        for (D9WGWindowWatch* entry = watches; entry; entry = entry->next)
            entry->epoch = 0;
    }
    BOOL changed = FALSE;
    D9WGWindowFlush flush = flush_reports;
    for (;;) {
        if (worker != current || current->stopping)
            break;
        D9WGWindowWatch* entry = watches;
        while (entry && entry->epoch == epoch)
            entry = entry->next;
        if (!entry)
            break;
        entry->epoch = epoch;
        D9WGWindowNotify notify = entry->notify;
        void* context = entry->context;
        /* The callback can unlink/free any API object, including itself. */
        if (notify(context))
            changed = TRUE;
    }
    if (changed && flush)
        flush();
    dispatching = FALSE;
    if (pending)
        queue_refresh_locked(worker);
}

static void CALLBACK window_event(HWINEVENTHOOK hook, DWORD event, HWND window,
    LONG object, LONG child, DWORD thread, DWORD time)
{
    (void)thread;
    (void)time;
    if (event != EVENT_SYSTEM_FOREGROUND) {
        if (object != OBJID_WINDOW || child != CHILDID_SELF || !window)
            return;
        if (event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_HIDE
            && event != EVENT_OBJECT_DESTROY && event != EVENT_OBJECT_REORDER
            && event != EVENT_OBJECT_LOCATIONCHANGE)
            return;
    }
    CRITICAL_SECTION* lock = load_window_lock();
    if (!lock)
        return;
    EnterCriticalSection(lock);
    D9WGWindowWorker* current = worker;
    if (current && hook && (hook == current->foreground || hook == current->window))
        notify_locked(current);
    LeaveCriticalSection(lock);
}

static void install_hooks_locked(D9WGWindowWorker* current)
{
    if (!current->foreground)
        current->foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND, NULL, window_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!current->window)
        current->window = SetWinEventHook(EVENT_OBJECT_DESTROY,
            EVENT_OBJECT_LOCATIONCHANGE, NULL, window_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (current->foreground && current->window) {
        if (current->timer)
            KillTimer(NULL, current->timer);
        current->timer = 0;
        retry_armed = FALSE;
    } else if (!current->timer)
        current->timer = SetTimer(NULL, 0, WINDOW_RETRY_MS, NULL);
}

static DWORD WINAPI window_worker(void* context)
{
    D9WGWindowWorker* current = context;
    CRITICAL_SECTION* lock = load_window_lock();
    MSG message;
    /* Create the queue before publishing readiness; a concurrent final remove
     * can now always wake GetMessage. A pre-start remove sets stopping. */
    PeekMessageA(&message, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    EnterCriticalSection(lock);
    current->queue_ready = TRUE;
    if (!current->stopping)
        install_hooks_locked(current);
    SetEvent(current->ready);
    if (!current->stopping)
        notify_locked(current);
    BOOL stopping = current->stopping;
    LeaveCriticalSection(lock);
    while (!stopping && GetMessageA(&message, NULL, 0, 0) > 0) {
        EnterCriticalSection(lock);
        if (message.message == WINDOW_REFRESH) {
            current->refresh_queued = FALSE;
            if (!current->stopping) {
                install_hooks_locked(current);
                notify_locked(current);
            }
        } else if (message.message == WM_TIMER && message.wParam == current->timer) {
            if (!current->stopping) {
                install_hooks_locked(current);
                notify_locked(current);
            }
        }
        stopping = current->stopping;
        LeaveCriticalSection(lock);
    }
    /* Win32 requires both unhooks on the installing thread. Module retention
     * keeps this callback/code and the process lock alive until they finish. */
    if (current->foreground)
        UnhookWinEvent(current->foreground);
    if (current->window)
        UnhookWinEvent(current->window);
    if (current->timer)
        KillTimer(NULL, current->timer);
    EnterCriticalSection(lock);
    if (worker == current)
        worker = NULL;
    LeaveCriticalSection(lock);
    HMODULE module = current->module;
    CloseHandle(current->ready);
    HeapFree(GetProcessHeap(), 0, current);
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

static BOOL start_worker_locked(void)
{
    if (worker && !worker->stopping)
        return TRUE;
    if (!watches)
        return FALSE;
    DWORD now = GetTickCount();
    if (!d9wg_retry_ready(now, failed_at, retry_armed))
        return FALSE;
    failed_at = now;
    retry_armed = TRUE;
    D9WGWindowWorker* current = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*current));
    if (!current)
        return FALSE;
    current->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!current->ready || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (const char*)(uintptr_t)window_worker, &current->module)) {
        if (current->ready)
            CloseHandle(current->ready);
        HeapFree(GetProcessHeap(), 0, current);
        return FALSE;
    }
    HANDLE thread = CreateThread(NULL, 0, window_worker, current, 0, &current->thread_id);
    if (!thread) {
        FreeLibrary(current->module);
        CloseHandle(current->ready);
        HeapFree(GetProcessHeap(), 0, current);
        return FALSE;
    }
    CloseHandle(thread);
    worker = current;
    retry_armed = FALSE;
    return TRUE;
}

BOOL d9wg_window_watch_add(D9WGWindowWatch* watch, void* context,
    D9WGWindowNotify notify, D9WGWindowFlush flush, CRITICAL_SECTION* lock)
{
    if (!watch || !notify || !lock)
        return FALSE;
    EnterCriticalSection(lock);
    void* established = InterlockedCompareExchangePointer(&window_lock_value, lock, NULL);
    if (established && established != lock) {
        LeaveCriticalSection(lock);
        return FALSE;
    }
    D9WGWindowWatch* entry = watches;
    while (entry && entry != watch)
        entry = entry->next;
    if (!entry) {
        watch->context = context;
        watch->notify = notify;
        watch->epoch = dispatching ? epoch : 0;
        watch->next = watches;
        watches = watch;
    }
    flush_reports = flush;
    BOOL result = start_worker_locked();
    HANDLE ready = NULL;
    if (result && !dispatching)
        DuplicateHandle(GetCurrentProcess(), worker->ready, GetCurrentProcess(),
            &ready, 0, FALSE, DUPLICATE_SAME_ACCESS);
    queue_refresh_locked(worker);
    LeaveCriticalSection(lock);
    /* Registration starts outside DllMain. Never wait while holding the lock
     * needed by the worker, and keep startup optional and bounded. */
    if (ready) {
        WaitForSingleObject(ready, WINDOW_RETRY_MS);
        CloseHandle(ready);
    }
    return result;
}

void d9wg_window_watch_remove(D9WGWindowWatch* watch)
{
    CRITICAL_SECTION* lock = load_window_lock();
    if (!watch || !lock)
        return;
    EnterCriticalSection(lock);
    D9WGWindowWatch** link = &watches;
    while (*link && *link != watch)
        link = &(*link)->next;
    if (*link) {
        *link = watch->next;
        watch->next = NULL;
        watch->context = NULL;
        watch->notify = NULL;
    }
    if (!watches && worker && !worker->stopping) {
        worker->stopping = TRUE;
        if (worker->queue_ready)
            PostThreadMessageA(worker->thread_id, WM_QUIT, 0, 0);
    }
    LeaveCriticalSection(lock);
}

void d9wg_window_watch_retry(void)
{
    CRITICAL_SECTION* lock = load_window_lock();
    if (!lock)
        return;
    EnterCriticalSection(lock);
    if (start_worker_locked() && worker->queue_ready && !worker->timer
        && (!worker->foreground || !worker->window)) {
        DWORD now = GetTickCount();
        if (d9wg_retry_ready(now, failed_at, retry_armed)) {
            failed_at = now;
            retry_armed = TRUE;
            queue_refresh_locked(worker);
        }
    }
    LeaveCriticalSection(lock);
}
