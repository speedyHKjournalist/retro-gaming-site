#define _KERNEL_MODE
/* mingw-w64 14 references this Vista-only type in its XP IRP union. The
 * context has always occupied one ULONG; no power IRPs are handled here. */
#if defined(__MINGW32__) && NTDDI_VERSION < 0x06000000
 typedef struct { unsigned long Context; } SYSTEM_POWER_STATE_CONTEXT;
#endif
#include <ntddk.h>
#include "../openglproxy/v86gl_ioctl.h"

#define V86GL_DEVICE_NAME L"\\Device\\v86gl"
#define V86GL_DOS_NAME L"\\DosDevices\\v86gl"
#define V86GL_LOG_PREFIX "[v86gl.sys] "
#include "virtio_transport.h"

typedef struct V86GL_DEVICE_EXTENSION {
    PVOID buffer;
    PMDL buffer_mdl;
    ULONG buffer_bytes;
    PVOID user_mapping;
    PEPROCESS mapping_process;
    VGL_TRANSPORT transport;
    FAST_MUTEX mutex;
    ULONG submit_count;
} V86GL_DEVICE_EXTENSION, *PV86GL_DEVICE_EXTENSION;

static NTSTATUS v86gl_result(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    return status;
}

static void v86gl_unmap_current_process(PV86GL_DEVICE_EXTENSION extension)
{
    if(extension->user_mapping && extension->mapping_process == PsGetCurrentProcess())
    {
        DbgPrint(V86GL_LOG_PREFIX "unmap user=%p process=%p\n",
                 extension->user_mapping, extension->mapping_process);
        if(extension->transport.registered &&
           !NT_SUCCESS(vgl_request(&extension->transport, VGL_UNREGISTER_ARENA, 0, 0, 0)))
            vgl_transport_destroy(&extension->transport);
        MmUnmapLockedPages(extension->user_mapping, extension->buffer_mdl);
        extension->user_mapping = NULL;
        extension->mapping_process = NULL;
    }
}

static NTSTATUS NTAPI v86gl_create_close(PDEVICE_OBJECT device_object, PIRP irp)
{
    PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);

    /* Keep normal kernel APCs disabled without changing IRQL. The fast
     * mutex enters the dispatcher wait path only when contended. */
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(&extension->mutex);
    if(stack->MajorFunction == IRP_MJ_CLOSE)
    {
        DbgPrint(V86GL_LOG_PREFIX "close process=%p\n", PsGetCurrentProcess());
        v86gl_unmap_current_process(extension);
    }
    else
    {
        DbgPrint(V86GL_LOG_PREFIX "open process=%p\n", PsGetCurrentProcess());
    }

    ExReleaseFastMutexUnsafe(&extension->mutex);
    KeLeaveCriticalRegion();
    v86gl_result(irp, STATUS_SUCCESS, 0);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS v86gl_map_buffer(PV86GL_DEVICE_EXTENSION extension, PIRP irp,
                                 PIO_STACK_LOCATION stack)
{
    V86GLMapBuffer* result;
    PHYSICAL_ADDRESS physical;
    NTSTATUS status;

    if(stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*result))
    {
        DbgPrint(V86GL_LOG_PREFIX "MAP_BUFFER rejected output=%lu required=%lu\n",
                 stack->Parameters.DeviceIoControl.OutputBufferLength,
                 sizeof(*result));
        return v86gl_result(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }

    if(extension->user_mapping && extension->mapping_process != PsGetCurrentProcess())
    {
        DbgPrint(V86GL_LOG_PREFIX "MAP_BUFFER busy owner=%p requester=%p\n",
                 extension->mapping_process, PsGetCurrentProcess());
        return v86gl_result(irp, STATUS_DEVICE_BUSY, 0);
    }

    if(!extension->user_mapping)
    {
        extension->user_mapping = MmMapLockedPagesSpecifyCache(
            extension->buffer_mdl,
            UserMode,
            MmCached,
            NULL,
            FALSE,
            NormalPagePriority
        );

        if(!extension->user_mapping)
        {
            DbgPrint(V86GL_LOG_PREFIX "MAP_BUFFER map failed bytes=%lu\n",
                     extension->buffer_bytes);
            return v86gl_result(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }

        extension->mapping_process = PsGetCurrentProcess();
    }

    if(!extension->transport.registered)
    {
        physical = MmGetPhysicalAddress(extension->buffer);
        status = vgl_request(&extension->transport, VGL_REGISTER_ARENA,
                             physical.LowPart, extension->buffer_bytes, 0);
        if(!NT_SUCCESS(status))
        {
            v86gl_unmap_current_process(extension);
            return v86gl_result(irp, status, 0);
        }
    }
    result = irp->AssociatedIrp.SystemBuffer;
    result->user_address = (V86GLU32)(ULONG_PTR)extension->user_mapping;
    result->buffer_bytes = extension->buffer_bytes;
    DbgPrint(V86GL_LOG_PREFIX "MAP_BUFFER ok user=%08lx bytes=%lu process=%p\n",
             result->user_address, result->buffer_bytes, extension->mapping_process);
    return v86gl_result(irp, STATUS_SUCCESS, sizeof(*result));
}

static NTSTATUS v86gl_submit(PV86GL_DEVICE_EXTENSION extension, PIRP irp,
                             PIO_STACK_LOCATION stack)
{
    V86GLSubmit* submit;
    V86GLDMADesc* desc;
    NTSTATUS status;

    if(stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*submit))
    {
        DbgPrint(V86GL_LOG_PREFIX "SUBMIT rejected input=%lu required=%lu\n",
                 stack->Parameters.DeviceIoControl.InputBufferLength,
                 sizeof(*submit));
        return v86gl_result(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }

    if(extension->mapping_process != PsGetCurrentProcess())
    {
        DbgPrint(V86GL_LOG_PREFIX "SUBMIT denied owner=%p requester=%p\n",
                 extension->mapping_process, PsGetCurrentProcess());
        return v86gl_result(irp, STATUS_ACCESS_DENIED, 0);
    }

    submit = irp->AssociatedIrp.SystemBuffer;
    if(submit->descriptor_bytes < sizeof(*desc) ||
       submit->descriptor_bytes > extension->buffer_bytes)
    {
        DbgPrint(V86GL_LOG_PREFIX "SUBMIT rejected descriptorBytes=%lu capacity=%lu\n",
                 submit->descriptor_bytes, extension->buffer_bytes);
        return v86gl_result(irp, STATUS_INVALID_BUFFER_SIZE, 0);
    }

    desc = extension->buffer;
    if(desc->magic != V86GL_MAGIC ||
       desc->version != V86GL_VERSION ||
       desc->command_bytes > submit->descriptor_bytes - sizeof(*desc))
    {
        DbgPrint(V86GL_LOG_PREFIX "SUBMIT bad descriptor magic=%08lx version=%lu frame=%lu commands=%lu commandBytes=%lu descBytes=%lu\n",
                 desc->magic, desc->version, desc->frame_id, desc->command_count,
                 desc->command_bytes, submit->descriptor_bytes);
        return v86gl_result(irp, STATUS_INVALID_PARAMETER, 0);
    }

    if(submit->flags & ~V86GL_SUBMIT_FORCE_PRESENT)
        return v86gl_result(irp, STATUS_INVALID_PARAMETER, 0);
    ++extension->submit_count;
    KeMemoryBarrier();
    status = vgl_request(&extension->transport, VGL_SUBMIT, 0,
                         submit->descriptor_bytes, submit->flags);
    if(!NT_SUCCESS(status))
        DbgPrint(V86GL_LOG_PREFIX "SUBMIT #%lu failed virtio status=%08lx\n",
                 extension->submit_count, status);
    return v86gl_result(irp, status, 0);
}

static NTSTATUS v86gl_device_control_locked(PDEVICE_OBJECT device_object, PIRP irp)
{
    PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);

    switch(stack->Parameters.DeviceIoControl.IoControlCode)
    {
    case V86GL_IOCTL_MAP_BUFFER:
        return v86gl_map_buffer(extension, irp, stack);
    case V86GL_IOCTL_SUBMIT:
        return v86gl_submit(extension, irp, stack);
    case V86GL_IOCTL_UNMAP_BUFFER:
        DbgPrint(V86GL_LOG_PREFIX "UNMAP_BUFFER request process=%p\n", PsGetCurrentProcess());
        v86gl_unmap_current_process(extension);
        return v86gl_result(irp, STATUS_SUCCESS, 0);
    default:
        DbgPrint(V86GL_LOG_PREFIX "unknown ioctl=%08lx\n",
                 stack->Parameters.DeviceIoControl.IoControlCode);
        return v86gl_result(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static NTSTATUS NTAPI v86gl_device_control(PDEVICE_OBJECT device_object, PIRP irp)
{
    PV86GL_DEVICE_EXTENSION extension = device_object->DeviceExtension;
    NTSTATUS status;
    /* Keep normal kernel APCs disabled without changing IRQL. The fast
     * mutex enters the dispatcher wait path only when contended. */
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(&extension->mutex);
    status = v86gl_device_control_locked(device_object, irp);
    ExReleaseFastMutexUnsafe(&extension->mutex);
    KeLeaveCriticalRegion();
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

        vgl_transport_destroy(&extension->transport);
        if(extension->buffer_mdl)
        {
            IoFreeMdl(extension->buffer_mdl);
        }
        if(extension->buffer)
        {
            MmFreeContiguousMemory(extension->buffer);
        }

        RtlInitUnicodeString(&dos_name, V86GL_DOS_NAME);
        DbgPrint(V86GL_LOG_PREFIX "unload submits=%lu\n", extension->submit_count);
        IoDeleteSymbolicLink(&dos_name);
        IoDeleteDevice(device_object);
    }
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
    UNICODE_STRING device_name;
    UNICODE_STRING dos_name;
    PDEVICE_OBJECT device_object = NULL;
    PV86GL_DEVICE_EXTENSION extension;
    PHYSICAL_ADDRESS lowest;
    PHYSICAL_ADDRESS highest;
    PHYSICAL_ADDRESS boundary;
    PHYSICAL_ADDRESS physical;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(registry_path);

    RtlInitUnicodeString(&device_name, V86GL_DEVICE_NAME);
    status = IoCreateDevice(driver_object,
                            sizeof(*extension),
                            &device_name,
                            FILE_DEVICE_UNKNOWN,
                            0,
                            FALSE,
                            &device_object);
    if(!NT_SUCCESS(status))
    {
        DbgPrint(V86GL_LOG_PREFIX "IoCreateDevice failed status=%08lx\n", status);
        return status;
    }

    extension = device_object->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    extension->buffer_bytes = V86GL_DEFAULT_BUFFER_BYTES;
    ExInitializeFastMutex(&extension->mutex);

    lowest.QuadPart = 0;
    highest.QuadPart = 0xFFFFFFFFULL;
    boundary.QuadPart = 0;
    extension->buffer = MmAllocateContiguousMemorySpecifyCache(
        extension->buffer_bytes,
        lowest,
        highest,
        boundary,
        MmCached
    );
    if(!extension->buffer)
    {
        DbgPrint(V86GL_LOG_PREFIX "contiguous allocation failed bytes=%lu\n",
                 extension->buffer_bytes);
        IoDeleteDevice(device_object);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    extension->buffer_mdl = IoAllocateMdl(
        extension->buffer,
        extension->buffer_bytes,
        FALSE,
        FALSE,
        NULL
    );
    if(!extension->buffer_mdl)
    {
        DbgPrint(V86GL_LOG_PREFIX "IoAllocateMdl failed buffer=%p bytes=%lu\n",
                 extension->buffer, extension->buffer_bytes);
        MmFreeContiguousMemory(extension->buffer);
        IoDeleteDevice(device_object);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MmBuildMdlForNonPagedPool(extension->buffer_mdl);

    status = vgl_transport_init(&extension->transport);
    if(!NT_SUCCESS(status))
    {
        DbgPrint(V86GL_LOG_PREFIX "virtio initialization failed status=%08lx\n", status);
        IoFreeMdl(extension->buffer_mdl);
        MmFreeContiguousMemory(extension->buffer);
        IoDeleteDevice(device_object);
        return status;
    }

    RtlInitUnicodeString(&dos_name, V86GL_DOS_NAME);
    status = IoCreateSymbolicLink(&dos_name, &device_name);
    if(!NT_SUCCESS(status))
    {
        DbgPrint(V86GL_LOG_PREFIX "IoCreateSymbolicLink failed status=%08lx\n", status);
        vgl_transport_destroy(&extension->transport);
        IoFreeMdl(extension->buffer_mdl);
        MmFreeContiguousMemory(extension->buffer);
        IoDeleteDevice(device_object);
        return status;
    }

    driver_object->MajorFunction[IRP_MJ_CREATE] = v86gl_create_close;
    driver_object->MajorFunction[IRP_MJ_CLOSE] = v86gl_create_close;
    driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = v86gl_device_control;
    driver_object->DriverUnload = v86gl_unload;
    physical = MmGetPhysicalAddress(extension->buffer);
    DbgPrint(V86GL_LOG_PREFIX "loaded virtio perf1 common=%04x buffer=%p phys=%08lx bytes=%lu\n",
             extension->transport.common, extension->buffer, physical.LowPart,
             extension->buffer_bytes);
    return STATUS_SUCCESS;
}
