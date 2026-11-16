/* Real XP: hold three DLLs in each of two processes while legacy OpenGL's
 * IOCTL arena is mapped. Kill A, then close a duplicated parent GL handle in
 * B and unload B's frontends normally. No rendering/caps claim is made. */
#define WinMainCRTStartup owner_single_main
#include "d3d_process_owner_test.c"
#undef WinMainCRTStartup
#include "../openglproxy/v86gl_ioctl.h"
#include "../runtime/d9wg_transport.h"

typedef struct MultiControl { DWORD foreign_handle, owner_a_handle, dead_owner_handle; } MultiControl;
static char role;
static HANDLE ready_event, go_event;

static D9WGProcess *probe_owner;
static BOOL probe_flush(BOOL present) { return d9wg_transport_submit(probe_owner, present); }
static void submit_owner_probe(D9WGProcess *state)
{
    uint8_t *payload;
    D9WGHello *hello;
    probe_owner = state->canonical;
    EnterCriticalSection(&probe_owner->transport_lock);
    check(d9wg_transport_reserve(probe_owner, D9WG_OP_HELLO, sizeof(*hello), 0,
            NULL, &payload, NULL, probe_flush), "reserve live owner probe");
    hello = (D9WGHello *)payload;hello->guest_pointer_bits = 32;hello->feature_bits = 0;
    hello->session_id_low = probe_owner->session_id_low;hello->session_id_high = probe_owner->session_id_high;
    check(probe_flush(FALSE), "submit live owner probe");
    LeaveCriticalSection(&probe_owner->transport_lock);
}

static void wait_with_owner(D9WGProcess *state)
{
    HANDLE mapping;
    MultiControl *control;
    DWORD flags;
    mapping = OpenFileMappingA(FILE_MAP_WRITE, FALSE, "D9WG.Multi.Control");check(mapping != NULL, "open child publication");
    control = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(*control));check(control != NULL, "map child publication");
    if (role == 'A') control->owner_a_handle = (DWORD)(uintptr_t)state->transport;
    UnmapViewOfFile(control);CloseHandle(mapping);
    submit_owner_probe(state);
    report("READY child holding three frontends");
    check(SetEvent(ready_event), "signal child ready");
    check(WaitForSingleObject(go_event, 90000) == WAIT_OBJECT_0, "wait child release");
    check(state->references == 3 && state->dma_buffer && state->transport_generation == 1 &&
            GetHandleInformation(state->transport, &flags), "surviving owner still mapped after peer exit");
    mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, "D9WG.Multi.Control");check(mapping != NULL, "open test control");
    control = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(*control));check(control != NULL, "map test control");
    check(control->dead_owner_handle && CloseHandle((HANDLE)(uintptr_t)control->dead_owner_handle), "last duplicate after original process address space is gone");
    report("PASS dead owner's final duplicate closed");
    check(control->foreign_handle && CloseHandle((HANDLE)(uintptr_t)control->foreign_handle), "foreign last-handle cleanup");
    UnmapViewOfFile(control);CloseHandle(mapping);
    submit_owner_probe(state);
    report("PASS foreign duplicated legacy handle closed");
}

static void event_name(char *name, const char *kind, char who)
{
    wsprintfA(name, "D9WG.Multi.%s.%c", kind, who);
}

static void send_legacy(HANDLE device, V86GLMapBuffer *mapping)
{
    V86GLDMADesc *desc = (V86GLDMADesc *)(uintptr_t)mapping->user_address;
    V86GLSubmit submit = { sizeof(*desc) + 4, 0 };
    DWORD returned;
    ZeroMemory(desc, sizeof(*desc) + 4);
    desc->magic = V86GL_MAGIC;desc->version = V86GL_VERSION;desc->command_count = 1;desc->command_bytes = 4;
    ((WORD *)(desc + 1))[0] = 0xfff1;((WORD *)(desc + 1))[1] = 4;
    check(DeviceIoControl(device, V86GL_IOCTL_SUBMIT, &submit, sizeof(submit), NULL, 0, &returned, NULL), "legacy submit while D3D owners live");
}

void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH], name[64], child_role[2];
    UINT index;
    GetModuleFileNameA(NULL, path, sizeof(path));lstrcpyA(directory, path);
    index = (UINT)lstrlenA(directory);while (index && directory[index - 1] != '\\') --index;directory[index] = 0;
    if (GetEnvironmentVariableA("D9WG_OWNER_CHILD", child_role, sizeof(child_role)) == 1) {
        role = child_role[0];lstrcpyA(path, directory);lstrcatA(path, role == 'A' ? "OWNER_A.TXT" : "OWNER_B.TXT");
        log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (log_file == INVALID_HANDLE_VALUE) ExitProcess(2);
        event_name(name, "Ready", role);ready_event = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
        event_name(name, "Go", role);go_event = OpenEventA(SYNCHRONIZE, FALSE, name);
        check(ready_event && go_event, "open parent events");owner_ready_hook = wait_with_owner;
        run_order(role == 'B');report("PASS child completed after peer exit");CloseHandle(log_file);ExitProcess(0);
    } else {
        PROCESS_INFORMATION children[2];STARTUPINFOA startup;
        HANDLE ready[2], go[2], legacy, metadata;
        MultiControl *control;V86GLMapBuffer mapping;DWORD returned, exit_code;BOOL fault_expected;
        lstrcpyA(name, "OWNER.TXT");lstrcpyA(path, directory);lstrcatA(path, name);
        log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (log_file == INVALID_HANDLE_VALUE) ExitProcess(2);
        report("READY real XP multiple process owner probe");
        metadata = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*control), "D9WG.Multi.Control");check(metadata != NULL, "create test control");
        control = MapViewOfFile(metadata, FILE_MAP_WRITE, 0, 0, sizeof(*control));check(control != NULL, "map parent control");ZeroMemory(control, sizeof(*control));
        legacy = CreateFileA(V86GL_DEVICE_DOS_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        check(legacy != INVALID_HANDLE_VALUE, "open legacy device");
        fault_expected = GetEnvironmentVariableA("D9WG_MAP_EXCEPTION", name, sizeof(name)) > 0;
        if (fault_expected) {
            check(!DeviceIoControl(legacy, V86GL_IOCTL_MAP_BUFFER, NULL, 0, &mapping, sizeof(mapping), &returned, NULL) &&
                    GetLastError() == ERROR_NO_SYSTEM_RESOURCES, "kernel mapping exception becomes allocation failure");
            report("PASS real kernel mapping exception caught");
        }
        check(DeviceIoControl(legacy, V86GL_IOCTL_MAP_BUFFER, NULL, 0, &mapping, sizeof(mapping), &returned, NULL), "map legacy device");
        send_legacy(legacy, &mapping);
        for (index = 0; index < 2; ++index) {
            role = (char)('A' + index);event_name(name, "Ready", role);ready[index] = CreateEventA(NULL, TRUE, FALSE, name);
            event_name(name, "Go", role);go[index] = CreateEventA(NULL, TRUE, FALSE, name);check(ready[index] && go[index], "create child events");
            child_role[0] = role;child_role[1] = 0;check(SetEnvironmentVariableA("D9WG_OWNER_CHILD", child_role), "set child role");
            ZeroMemory(&startup, sizeof(startup));startup.cb = sizeof(startup);GetModuleFileNameA(NULL, path, sizeof(path));
            check(CreateProcessA(path, NULL, NULL, NULL, FALSE, 0, NULL, directory, &startup, &children[index]), "launch owner child");
            CloseHandle(children[index].hThread);
        }
        SetEnvironmentVariableA("D9WG_OWNER_CHILD", NULL);
        check(WaitForMultipleObjects(2, ready, TRUE, 90000) == WAIT_OBJECT_0, "two processes hold DirectX while legacy GL mapped");
        report("PASS two simultaneous DirectX process owners plus legacy GL arena");
        send_legacy(legacy, &mapping);
        {
            HANDLE held;
            check(control->owner_a_handle && DuplicateHandle(children[0].hProcess, (HANDLE)(uintptr_t)control->owner_a_handle,
                    children[1].hProcess, &held, 0, FALSE, DUPLICATE_SAME_ACCESS), "hold A's device file open in B beyond A's exit");
            control->dead_owner_handle = (DWORD)(uintptr_t)held;
        }
        check(TerminateProcess(children[0].hProcess, 73) && WaitForSingleObject(children[0].hProcess, 30000) == WAIT_OBJECT_0, "kill owner A");
        send_legacy(legacy, &mapping);report("PASS legacy GL continues after peer kill");
        {
            HANDLE foreign;
            check(DuplicateHandle(GetCurrentProcess(), legacy, children[1].hProcess, &foreign, 0, FALSE, DUPLICATE_SAME_ACCESS), "duplicate legacy handle into B");
            control->foreign_handle = (DWORD)(uintptr_t)foreign;
        }
        check(CloseHandle(legacy), "close original legacy handle");check(SetEvent(go[1]), "release surviving owner B");
        check(WaitForSingleObject(children[1].hProcess, 90000) == WAIT_OBJECT_0 &&
                GetExitCodeProcess(children[1].hProcess, &exit_code) && exit_code == 0, "B survives kill and foreign legacy cleanup");
        report("PASS survivor unloads creator and remaining frontends");
        for (index = 0; index < 2; ++index) { CloseHandle(children[index].hProcess);CloseHandle(ready[index]);CloseHandle(go[index]); }
        UnmapViewOfFile(control);CloseHandle(metadata);
        report("PASS all real XP multiple process owner checks");CloseHandle(log_file);ExitProcess(0);
    }
}
