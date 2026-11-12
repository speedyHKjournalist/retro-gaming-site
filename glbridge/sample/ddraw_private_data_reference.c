#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

typedef struct { const IUnknownVtbl *vtbl; LONG refs; } Counter;
static HRESULT WINAPI query(IUnknown *iface, REFIID iid, void **out)
{ (void)iface; (void)iid; if (out) *out = NULL; return E_NOINTERFACE; }
static ULONG WINAPI addref(IUnknown *iface) { return ++((Counter *)iface)->refs; }
static ULONG WINAPI release(IUnknown *iface) { return --((Counter *)iface)->refs; }
static const IUnknownVtbl counter_vtbl = {query, addref, release};
static const GUID tag = {0x93451234, 1, 2, {3,4,5,6,7,8,9,10}};
static HANDLE output;
static UINT api;
static void report(const char *name, HRESULT hr, DWORD bytes, LONG refs)
{
    char text[200]; DWORD written;
    wsprintfA(text, "PRIVATE api=%u name=%s hr=%08lX bytes=%lu refs=%ld\r\n",
        api, name, (DWORD)hr, bytes, refs);
    WriteFile(output, text, lstrlenA(text), &written, NULL);
    FlushFileBuffers(output);
}
static HRESULT set(void *surface, void *data, DWORD bytes, DWORD flags)
{
    return api == 4 ? IDirectDrawSurface4_SetPrivateData((IDirectDrawSurface4 *)surface, &tag, data, bytes, flags)
        : IDirectDrawSurface7_SetPrivateData((IDirectDrawSurface7 *)surface, &tag, data, bytes, flags);
}
static HRESULT get(void *surface, void *data, DWORD *bytes)
{
    return api == 4 ? IDirectDrawSurface4_GetPrivateData((IDirectDrawSurface4 *)surface, &tag, data, bytes)
        : IDirectDrawSurface7_GetPrivateData((IDirectDrawSurface7 *)surface, &tag, data, bytes);
}
static HRESULT clear(void *surface)
{
    return api == 4 ? IDirectDrawSurface4_FreePrivateData((IDirectDrawSurface4 *)surface, &tag)
        : IDirectDrawSurface7_FreePrivateData((IDirectDrawSurface7 *)surface, &tag);
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH]; GetModuleFileNameA(NULL, path, sizeof(path));
    UINT end = lstrlenA(path); while (end && path[end-1] != '\\') --end;
    lstrcpyA(path + end, "PRIVATE.TXT");
    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(1);
    HMODULE module = LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");
    HRESULT (WINAPI *create)(GUID *, void **, REFIID, IUnknown *) =
        (void *)GetProcAddress(module, "DirectDrawCreateEx");
    IDirectDraw7 *root = NULL;
    if (!create || FAILED(create(NULL, (void **)&root, &IID_IDirectDraw7, NULL))) ExitProcess(2);
    IDirectDraw7_SetCooperativeLevel(root, NULL, DDSCL_NORMAL);
    for (api = 4; api <= 7; api += 3) {
        DDSURFACEDESC2 desc; ZeroMemory(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.dwWidth = desc.dwHeight = 4;
        IDirectDrawSurface7 *s7 = NULL; void *surface = NULL;
        if (FAILED(IDirectDraw7_CreateSurface(root, &desc, &s7, NULL))) ExitProcess(3);
        if (api == 4) {
            if (FAILED(IDirectDrawSurface7_QueryInterface(s7, &IID_IDirectDrawSurface4, &surface))) ExitProcess(4);
            IDirectDrawSurface7_Release(s7);
        } else surface = s7;
        Counter counter = {&counter_vtbl, 1}; IUnknown *pointer = NULL;
        HRESULT hr = set(surface, &counter, sizeof(pointer), DDSPD_IUNKNOWNPOINTER);
        report("set-object", hr, sizeof(pointer), counter.refs);
        DWORD bytes = sizeof(pointer); hr = get(surface, &pointer, &bytes);
        report(pointer == (IUnknown *)&counter ? "get-object" : "bad-object", hr, bytes, counter.refs);
        hr = set(surface, &counter, 0, DDSPD_IUNKNOWNPOINTER); report("zero-object-size", hr, 0, counter.refs);
        bytes = sizeof(pointer); hr = get(surface, &pointer, &bytes); report("after-zero-size", hr, bytes, counter.refs);
        hr = set(surface, &counter, sizeof(pointer) * 2, DDSPD_IUNKNOWNPOINTER);
        report("bad-object-size", hr, sizeof(pointer) * 2, counter.refs);
        bytes = sizeof(pointer); hr = get(surface, &pointer, &bytes); report("after-bad-size", hr, bytes, counter.refs);
        DWORD data = 42; hr = set(surface, &data, 0, 0); report("zero-data-size", hr, 0, counter.refs); clear(surface);
        hr = set(surface, &data, sizeof(data), 0x80000000); report("unknown-flag", hr, 0, counter.refs); clear(surface);
        hr = set(surface, &data, sizeof(data), DDSPD_VOLATILE); report("set-volatile", hr, 0, counter.refs);
        DWORD value = 0; bytes = sizeof(value); hr = get(surface, &value, &bytes); report("get-volatile", hr, bytes, counter.refs);
        if (api == 4) IDirectDrawSurface4_ChangeUniquenessValue((IDirectDrawSurface4 *)surface);
        else IDirectDrawSurface7_ChangeUniquenessValue((IDirectDrawSurface7 *)surface);
        value = 0xdeadbeef; bytes = sizeof(value); hr = get(surface, &value, &bytes);
        report("expired", hr, bytes, value == 0xdeadbeef); clear(surface);
        hr = set(surface, &counter, sizeof(pointer), DDSPD_IUNKNOWNPOINTER | DDSPD_VOLATILE);
        report("volatile-object", hr, 0, counter.refs);
        if (api == 4) IDirectDrawSurface4_ChangeUniquenessValue((IDirectDrawSurface4 *)surface);
        else IDirectDrawSurface7_ChangeUniquenessValue((IDirectDrawSurface7 *)surface);
        report("changed-object", S_OK, 0, counter.refs);
        bytes = sizeof(pointer); hr = get(surface, &pointer, &bytes); report("expired-object", hr, bytes, counter.refs);
        hr = clear(surface); report("free-expired", hr, 0, counter.refs);
        for (UINT readonly = 0; readonly < 2; ++readonly) {
            set(surface, &data, sizeof(data), DDSPD_VOLATILE);
            DDSURFACEDESC2 lock; ZeroMemory(&lock, sizeof(lock)); lock.dwSize = sizeof(lock);
            DWORD flags = DDLOCK_WAIT | (readonly ? DDLOCK_READONLY : 0);
            hr = api == 4 ? IDirectDrawSurface4_Lock((IDirectDrawSurface4 *)surface, NULL, &lock, flags, NULL)
                : IDirectDrawSurface7_Lock((IDirectDrawSurface7 *)surface, NULL, &lock, flags, NULL);
            report(readonly ? "lock-read" : "lock-write", hr, 0, counter.refs);
            if (SUCCEEDED(hr)) {
                if (!readonly) *(BYTE *)lock.lpSurface ^= 1;
                if (api == 4) IDirectDrawSurface4_Unlock((IDirectDrawSurface4 *)surface, NULL);
                else IDirectDrawSurface7_Unlock((IDirectDrawSurface7 *)surface, NULL);
            }
            bytes = sizeof(value); hr = get(surface, &value, &bytes);
            report(readonly ? "after-read" : "after-write", hr, bytes, counter.refs); clear(surface);
        }
        set(surface, &data, sizeof(data), DDSPD_VOLATILE);
        DDBLTFX fx; ZeroMemory(&fx, sizeof(fx)); fx.dwSize = sizeof(fx); fx.dwFillColor = 0xff00;
        hr = api == 4 ? IDirectDrawSurface4_Blt((IDirectDrawSurface4 *)surface, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx)
            : IDirectDrawSurface7_Blt((IDirectDrawSurface7 *)surface, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        report("fill", hr, 0, counter.refs); bytes = sizeof(value); hr = get(surface, &value, &bytes);
        report("after-fill", hr, bytes, counter.refs); clear(surface);
        hr = set(surface, &counter, sizeof(pointer), DDSPD_IUNKNOWNPOINTER); report("before-destroy", hr, 0, counter.refs);
        IUnknown_Release((IUnknown *)surface); report("after-destroy", S_OK, 0, counter.refs);
    }
    IDirectDraw7_Release(root); FreeLibrary(module);
    const char done[] = "PASS native private data reference complete\r\n"; DWORD written;
    WriteFile(output, done, sizeof(done)-1, &written, NULL); CloseHandle(output); ExitProcess(0);
}
