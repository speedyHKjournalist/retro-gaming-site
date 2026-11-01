#define _KERNEL_MODE
/* mingw-w64 14 references this Vista-only type in its XP IRP union. */
#if defined(__MINGW32__) && NTDDI_VERSION < 0x06000000
 typedef struct { unsigned long Context; } SYSTEM_POWER_STATE_CONTEXT;
#endif
#include <ntifs.h>
#include "../openglproxy/v86gl_ioctl.h"

#define V86GL_DEVICE_NAME L"\\Device\\v86gl"
#define V86GL_DOS_NAME L"\\DosDevices\\v86gl"
#define V86GL_LOG_PREFIX "[v86gl.sys] "
#define V86GL_POOL_TAG 0x6c674756u
#include "virtio_transport.h"

typedef struct V86GL_ARENA {
    struct V86GL_ARENA *next;
    PVOID buffer;
    PMDL mdl;
    ULONG bytes;
    PVOID user_mapping;
    PEPROCESS process;
    HANDLE process_id;
    ULONG owner;
    ULONG references;
    BOOLEAN revoked;
} V86GL_ARENA;

typedef struct V86GL_FILE {
    V86GL_ARENA *arena;
    BOOLEAN cleaned;
} V86GL_FILE;

typedef struct V86GL_DEVICE_EXTENSION {
    V86GL_ARENA *arenas;
    V86GL_ARENA *legacy;
    VGL_TRANSPORT transport;
    FAST_MUTEX mutex;
    ULONG next_owner;
    ULONG submit_count;
    BOOLEAN notify_registered;
} V86GL_DEVICE_EXTENSION, *PV86GL_DEVICE_EXTENSION;

static PV86GL_DEVICE_EXTENSION g_extension;
/* This tiny Clang-compiled SEH boundary imports only ntoskrnl's handler. */
PVOID v86gl_try_map_user(PVOID mdl, PVOID (*map)(PVOID));

static void v86gl_lock(PV86GL_DEVICE_EXTENSION extension)
{
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(&extension->mutex);
}
static void v86gl_unlock(PV86GL_DEVICE_EXTENSION extension)
{
    ExReleaseFastMutexUnsafe(&extension->mutex);
    KeLeaveCriticalRegion();
}
static NTSTATUS v86gl_result(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    return status;
}

static PVOID v86gl_map_user_raw(PVOID mdl)
{
    return MmMapLockedPagesSpecifyCache(mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
}

/* Called under the device mutex. Exit notification runs before the original
 * address space is deleted; a foreign duplicated-handle cleanup attaches only
 * while that notification is excluded by the same mutex. CLOSE needs no user
 * address-space access. See IRP_MJ_CLEANUP and PCREATE_PROCESS_NOTIFY_ROUTINE. */
static void v86gl_revoke_arena(PV86GL_DEVICE_EXTENSION extension, V86GL_ARENA *arena)
{
    NTSTATUS status;
    if(arena->revoked) return;
    arena->revoked = TRUE;
    status = vgl_request_owner(&extension->transport, arena->owner, VGL_UNREGISTER_ARENA, 0, 0, 0);
    /* A timed-out queue has revoked physical leases. Retiring the logical
     * owner is idempotent, so retry once on its fresh queue before freeing. */
    if(!NT_SUCCESS(status))
        status = vgl_request_owner(&extension->transport, arena->owner, VGL_UNREGISTER_ARENA, 0, 0, 0);
    if(!NT_SUCCESS(status)) DbgPrint(V86GL_LOG_PREFIX "owner=%lu retirement failed status=%08lx\n", arena->owner, status);
    if(arena->user_mapping)
    {
        KAPC_STATE attach;
        BOOLEAN foreign = arena->process != PsGetCurrentProcess();
        if(foreign) KeStackAttachProcess((PKPROCESS)arena->process, &attach);
        MmUnmapLockedPages(arena->user_mapping, arena->mdl);
        if(foreign) KeUnstackDetachProcess(&attach);
        arena->user_mapping = NULL;
    }
    if(extension->legacy == arena) extension->legacy = NULL;
}

static void v86gl_free_arena(PV86GL_DEVICE_EXTENSION extension, V86GL_ARENA *arena)
{
    V86GL_ARENA **link = &extension->arenas;
    v86gl_revoke_arena(extension, arena);
    while(*link && *link != arena) link = &(*link)->next;
    if(*link) *link = arena->next;
    if(arena->mdl) IoFreeMdl(arena->mdl);
    if(arena->buffer) MmFreeContiguousMemory(arena->buffer);
    if(arena->process) ObDereferenceObject(arena->process);
    ExFreePool(arena);
}

static void v86gl_release_file(PV86GL_DEVICE_EXTENSION extension, V86GL_FILE *file)
{
    V86GL_ARENA *arena = file->arena;
    file->arena = NULL;
    if(arena && !--arena->references) v86gl_free_arena(extension, arena);
}

static void NTAPI v86gl_process_notify(HANDLE parent_id, HANDLE process_id, BOOLEAN create)
{
    PV86GL_DEVICE_EXTENSION extension = g_extension;
    V86GL_ARENA *arena;
    UNREFERENCED_PARAMETER(parent_id);
    if(create || !extension) return;
    v86gl_lock(extension);
    for(arena = extension->arenas; arena; arena = arena->next)
        if(arena->process_id == process_id) v86gl_revoke_arena(extension, arena);
    v86gl_unlock(extension);
}

static NTSTATUS NTAPI v86gl_create_close(PDEVICE_OBJECT device_object, PIRP irp)
{
    PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    V86GL_FILE *file = stack->FileObject->FsContext;
    NTSTATUS status = STATUS_SUCCESS;
    v86gl_lock(extension);
    if(stack->MajorFunction == IRP_MJ_CREATE)
    {
        file = ExAllocatePoolWithTag(NonPagedPool, sizeof(*file), V86GL_POOL_TAG);
        if(!file) status = STATUS_INSUFFICIENT_RESOURCES;
        else { RtlZeroMemory(file, sizeof(*file));stack->FileObject->FsContext = file; }
    }
    else if(file)
    {
        v86gl_release_file(extension, file);
        file->cleaned = TRUE;
        if(stack->MajorFunction == IRP_MJ_CLOSE)
        {
            stack->FileObject->FsContext = NULL;
            ExFreePool(file);
        }
    }
    v86gl_unlock(extension);
    v86gl_result(irp, status, 0);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static ULONG v86gl_next_owner(PV86GL_DEVICE_EXTENSION extension)
{
    V86GL_ARENA *arena;
    do {
        ++extension->next_owner;
        if(!extension->next_owner) ++extension->next_owner;
        for(arena = extension->arenas; arena && arena->owner != extension->next_owner; arena = arena->next) {}
    } while(arena);
    return extension->next_owner;
}

static NTSTATUS v86gl_new_arena(PV86GL_DEVICE_EXTENSION extension, BOOLEAN d3d, V86GL_ARENA **output)
{
    V86GL_ARENA *arena;
    PHYSICAL_ADDRESS low, high, boundary, physical;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    arena = ExAllocatePoolWithTag(NonPagedPool, sizeof(*arena), V86GL_POOL_TAG);
    if(!arena) return status;
    RtlZeroMemory(arena, sizeof(*arena));
    arena->bytes = V86GL_DEFAULT_BUFFER_BYTES;
    arena->owner = d3d ? v86gl_next_owner(extension) : 0;
    arena->process = PsGetCurrentProcess();
    arena->process_id = PsGetCurrentProcessId();
    ObReferenceObject(arena->process);
    low.QuadPart = 0;high.QuadPart = 0xFFFFFFFFULL;boundary.QuadPart = 0;
    arena->buffer = MmAllocateContiguousMemorySpecifyCache(arena->bytes, low, high, boundary, MmCached);
    if(!arena->buffer) goto fail;
    /* Never expose a previous allocation's contents to user space. */
    RtlZeroMemory(arena->buffer, arena->bytes);
    arena->mdl = IoAllocateMdl(arena->buffer, arena->bytes, FALSE, FALSE, NULL);
    if(!arena->mdl) goto fail;
    MmBuildMdlForNonPagedPool(arena->mdl);
    arena->user_mapping = v86gl_try_map_user(arena->mdl, v86gl_map_user_raw);
    if(!arena->user_mapping) goto fail;
    physical = MmGetPhysicalAddress(arena->buffer);
    status = arena->owner ? vgl_request_owner(&extension->transport, arena->owner, VGL_REGISTER_ARENA,
                                               physical.LowPart, arena->bytes, 0) :
                            vgl_request(&extension->transport, VGL_REGISTER_ARENA, physical.LowPart, arena->bytes, 0);
    if(!NT_SUCCESS(status)) goto fail;
    arena->next = extension->arenas;extension->arenas = arena;
    if(!d3d) extension->legacy = arena;
    *output = arena;
    return STATUS_SUCCESS;
fail:
    v86gl_free_arena(extension, arena);
    return status;
}

static NTSTATUS v86gl_map_buffer(PV86GL_DEVICE_EXTENSION extension, V86GL_FILE *file,
                                 PIRP irp, PIO_STACK_LOCATION stack, BOOLEAN d3d)
{
    V86GLMapBuffer *result;
    V86GL_ARENA *arena = file->arena;
    NTSTATUS status;
    if(stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*result))
        return v86gl_result(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if(d3d && !extension->transport.owner_arenas)
        return v86gl_result(irp, STATUS_NOT_SUPPORTED, 0);
    if(arena && (arena->revoked || arena->process != PsGetCurrentProcess() || !!arena->owner != !!d3d))
        return v86gl_result(irp, STATUS_ACCESS_DENIED, 0);
    if(!arena)
    {
        if(d3d)
        {
            for(arena = extension->arenas; arena; arena = arena->next)
                if(arena->owner && !arena->revoked && arena->process == PsGetCurrentProcess()) break;
        }
        else
        {
            arena = extension->legacy;
            if(arena && arena->process != PsGetCurrentProcess())
                return v86gl_result(irp, STATUS_DEVICE_BUSY, 0);
        }
        if(!arena)
        {
            status = v86gl_new_arena(extension, d3d, &arena);
            if(!NT_SUCCESS(status)) return v86gl_result(irp, status, 0);
        }
        ++arena->references;file->arena = arena;
    }
    result = irp->AssociatedIrp.SystemBuffer;
    result->user_address = (V86GLU32)(ULONG_PTR)arena->user_mapping;
    result->buffer_bytes = arena->bytes;
    DbgPrint(V86GL_LOG_PREFIX "MAP owner=%lu user=%08lx bytes=%lu process=%p\n",
             arena->owner, result->user_address, result->buffer_bytes, arena->process);
    return v86gl_result(irp, STATUS_SUCCESS, sizeof(*result));
}

static NTSTATUS v86gl_submit(PV86GL_DEVICE_EXTENSION extension, V86GL_FILE *file,
                             PIRP irp, PIO_STACK_LOCATION stack)
{
    V86GLSubmit *submit;
    V86GLDMADesc *desc;
    V86GL_ARENA *arena = file->arena;
    NTSTATUS status;
    if(stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*submit))
        return v86gl_result(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if(!arena || arena->revoked || arena->process != PsGetCurrentProcess())
        return v86gl_result(irp, STATUS_ACCESS_DENIED, 0);
    submit = irp->AssociatedIrp.SystemBuffer;
    if(submit->descriptor_bytes < sizeof(*desc) || submit->descriptor_bytes > arena->bytes)
        return v86gl_result(irp, STATUS_INVALID_BUFFER_SIZE, 0);
    desc = arena->buffer;
    if(desc->magic != V86GL_MAGIC || desc->version != V86GL_VERSION ||
       desc->command_bytes != submit->descriptor_bytes - sizeof(*desc) ||
       (submit->flags & ~V86GL_SUBMIT_FORCE_PRESENT))
        return v86gl_result(irp, STATUS_INVALID_PARAMETER, 0);
    /* Positive registration is idempotent. Every submit renews after a global
     * queue reset without confusing the legacy arena's registered bit. */
    if(arena->owner || !extension->transport.registered)
    {
        PHYSICAL_ADDRESS physical = MmGetPhysicalAddress(arena->buffer);
        status = vgl_request_owner(&extension->transport, arena->owner, VGL_REGISTER_ARENA,
                                    physical.LowPart, arena->bytes, 0);
        if(!NT_SUCCESS(status)) return v86gl_result(irp, status, 0);
    }
    ++extension->submit_count;
    KeMemoryBarrier();
    status = vgl_request_owner(&extension->transport, arena->owner, VGL_SUBMIT, 0,
                                submit->descriptor_bytes, submit->flags);
    return v86gl_result(irp, status, 0);
}

static NTSTATUS v86gl_device_control_locked(PDEVICE_OBJECT device_object, PIRP irp)
{
    PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    V86GL_FILE *file = stack->FileObject->FsContext;
    if(!file || file->cleaned) return v86gl_result(irp, STATUS_INVALID_HANDLE, 0);
    switch(stack->Parameters.DeviceIoControl.IoControlCode)
    {
    case V86GL_IOCTL_MAP_BUFFER:
        return v86gl_map_buffer(extension, file, irp, stack, FALSE);
    case V86GL_IOCTL_MAP_D3D_BUFFER:
        return v86gl_map_buffer(extension, file, irp, stack, TRUE);
    case V86GL_IOCTL_SUBMIT:
        return v86gl_submit(extension, file, irp, stack);
    case V86GL_IOCTL_UNMAP_BUFFER:
        v86gl_release_file(extension, file);
        return v86gl_result(irp, STATUS_SUCCESS, 0);
    default:
        return v86gl_result(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static NTSTATUS NTAPI v86gl_device_control(PDEVICE_OBJECT device_object, PIRP irp)
{
    PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
    NTSTATUS status;
    v86gl_lock(extension);
    status = v86gl_device_control_locked(device_object, irp);
    v86gl_unlock(extension);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static void NTAPI v86gl_unload(PDRIVER_OBJECT driver_object)
{
    PDEVICE_OBJECT device_object = driver_object->DeviceObject;
    if(device_object)
    {
        PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
        UNICODE_STRING dos_name;
        if(extension->notify_registered) PsSetCreateProcessNotifyRoutine(v86gl_process_notify, TRUE);
        g_extension = NULL;
        while(extension->arenas) v86gl_free_arena(extension, extension->arenas);
        vgl_transport_destroy(&extension->transport);
        RtlInitUnicodeString(&dos_name, V86GL_DOS_NAME);
        IoDeleteSymbolicLink(&dos_name);
        IoDeleteDevice(device_object);
    }
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
    UNICODE_STRING device_name, dos_name;
    PDEVICE_OBJECT device_object = NULL;
    PV86GL_DEVICE_EXTENSION extension;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(registry_path);
    RtlInitUnicodeString(&device_name, V86GL_DEVICE_NAME);
    status = IoCreateDevice(driver_object, sizeof(*extension), &device_name,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &device_object);
    if(!NT_SUCCESS(status)) return status;
    extension = device_object->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    ExInitializeFastMutex(&extension->mutex);
    status = vgl_transport_init(&extension->transport);
    if(!NT_SUCCESS(status)) { IoDeleteDevice(device_object);return status; }
    g_extension = extension;
    status = PsSetCreateProcessNotifyRoutine(v86gl_process_notify, FALSE);
    if(!NT_SUCCESS(status)) { g_extension = NULL;vgl_transport_destroy(&extension->transport);IoDeleteDevice(device_object);return status; }
    extension->notify_registered = TRUE;
    RtlInitUnicodeString(&dos_name, V86GL_DOS_NAME);
    status = IoCreateSymbolicLink(&dos_name, &device_name);
    if(!NT_SUCCESS(status)) { v86gl_unload(driver_object);return status; }
    driver_object->MajorFunction[IRP_MJ_CREATE] = v86gl_create_close;
    driver_object->MajorFunction[IRP_MJ_CLEANUP] = v86gl_create_close;
    driver_object->MajorFunction[IRP_MJ_CLOSE] = v86gl_create_close;
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = v86gl_device_control;
    driver_object->DriverUnload = v86gl_unload;
    device_object->Flags &= ~DO_DEVICE_INITIALIZING;
    DbgPrint(V86GL_LOG_PREFIX "loaded per-process arenas common=%04x owners=%u\n",
             extension->transport.common, extension->transport.owner_arenas);
    return STATUS_SUCCESS;
}
