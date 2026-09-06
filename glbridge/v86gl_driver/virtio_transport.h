#ifndef V86GL_VIRTIO_TRANSPORT_H
#define V86GL_VIRTIO_TRANSPORT_H

/* Private virtio-v86gl protocol v1; see v86/docs/glbridge.md.
 * The PCI device ID is provisional, NOT an allocated virtio-gpu ID. */
#define VGL_VIRTIO_VENDOR 0x1AF4u
#define VGL_VIRTIO_DEVICE 0x107Fu
#define VGL_VIRTIO_SUBSYSTEM 0x5686u
#define VGL_REGISTER_ARENA 1u
#define VGL_SUBMIT 2u
#define VGL_UNREGISTER_ARENA 3u
#define VGL_QUEUE_SIZE 8u
#define VGL_AVAIL_OFFSET 128u
#define VGL_USED_OFFSET 160u
#define VGL_REQUEST_OFFSET 256u
#define VGL_REPLY_OFFSET 288u

#pragma pack(push, 1)
typedef struct VGL_VIRTQ_DESC {
    ULONG address_low;
    ULONG address_high;
    ULONG length;
    USHORT flags;
    USHORT next;
} VGL_VIRTQ_DESC;
typedef struct VGL_REQUEST {
    ULONG opcode;
    ULONG address_low;
    ULONG address_high;
    ULONG length;
    ULONG flags;
    ULONG reserved;
} VGL_REQUEST;
typedef struct VGL_REPLY {
    ULONG status;
    ULONG frame;
    ULONG bytes;
    ULONG submits;
} VGL_REPLY;
#pragma pack(pop)

typedef char vgl_descriptor_size_check[sizeof(VGL_VIRTQ_DESC) == 16 ? 1 : -1];
typedef char vgl_request_size_check[sizeof(VGL_REQUEST) == 24 ? 1 : -1];
typedef char vgl_reply_size_check[sizeof(VGL_REPLY) == 16 ? 1 : -1];

typedef struct VGL_TRANSPORT {
    USHORT common;
    USHORT notify;
    USHORT isr;
    USHORT config;
    PVOID queue_memory;
    ULONG queue_physical;
    USHORT available;
    USHORT used;
    BOOLEAN initialized;
    BOOLEAN registered;
} VGL_TRANSPORT;

static ULONG vgl_read32(USHORT port, ULONG offset)
{
    return READ_PORT_ULONG((PULONG)(ULONG_PTR)(port + offset));
}
static USHORT vgl_read16(USHORT port, ULONG offset)
{
    return READ_PORT_USHORT((PUSHORT)(ULONG_PTR)(port + offset));
}
static UCHAR vgl_read8(USHORT port, ULONG offset)
{
    return READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)(port + offset));
}
static void vgl_write32(USHORT port, ULONG offset, ULONG data)
{
    WRITE_PORT_ULONG((PULONG)(ULONG_PTR)(port + offset), data);
}
static void vgl_write16(USHORT port, ULONG offset, USHORT data)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)(port + offset), data);
}
static void vgl_write8(USHORT port, ULONG offset, UCHAR data)
{
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)(port + offset), data);
}
static ULONG vgl_u32(const UCHAR *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}
static USHORT vgl_u16(const UCHAR *p)
{
    return (USHORT)(p[0] | (USHORT)p[1] << 8);
}

/* v86 exposes its devices on PCI bus 0. Discover the current BAR assignments
 * through the PCI capabilities, so firmware/XP may relocate any I/O BAR. */
static NTSTATUS vgl_discover(VGL_TRANSPORT *t)
{
    ULONG dev, fun;
    UCHAR pci[256];
    for(dev = 0; dev < 32; ++dev)
    {
        for(fun = 0; fun < 8; ++fun)
        {
            ULONG slot = dev | (fun << 5);
            ULONG got = HalGetBusDataByOffset(PCIConfiguration, 0, slot, pci, 0, sizeof(pci));
            ULONG cap, steps, multiplier = 0;
            USHORT command;
            USHORT ports[5] = {0, 0, 0, 0, 0};
            if(got < 64 || vgl_u16(pci) == 0xFFFF) continue;
            if(vgl_u16(pci) != VGL_VIRTIO_VENDOR ||
               vgl_u16(pci + 2) != VGL_VIRTIO_DEVICE ||
               vgl_u16(pci + 0x2E) != VGL_VIRTIO_SUBSYSTEM) continue;
            if(got != sizeof(pci) || !(pci[6] & 0x10)) return STATUS_DEVICE_CONFIGURATION_ERROR;
            cap = pci[0x34] & ~3u;
            for(steps = 0; cap && steps < 48; ++steps)
            {
                ULONG type, bar, offset, length, address;
                if(cap < 64 || cap > 240) return STATUS_DEVICE_CONFIGURATION_ERROR;
                if(pci[cap] == 9)
                {
                    type = pci[cap + 3];
                    if(pci[cap + 2] < 16 || cap + pci[cap + 2] > sizeof(pci))
                        return STATUS_DEVICE_CONFIGURATION_ERROR;
                    if(type >= 1 && type <= 4)
                    {
                        bar = pci[cap + 4];
                        if(bar >= 6) return STATUS_DEVICE_CONFIGURATION_ERROR;
                        address = vgl_u32(pci + 0x10 + 4 * bar);
                        offset = vgl_u32(pci + cap + 8);
                        length = vgl_u32(pci + cap + 12);
                        if(!(address & 1) || offset > 0xFFFF || length > 0xFFFF ||
                           (address & ~3u) > 0xFFFF - offset ||
                           (address & ~3u) + offset > 0x10000 - length ||
                           length < (type == 1 ? 56u : type == 4 ? 16u : type == 2 ? 2u : 1u))
                            return STATUS_DEVICE_CONFIGURATION_ERROR;
                        ports[type] = (USHORT)((address & ~3u) + offset);
                        if(type == 2)
                        {
                            if(pci[cap + 2] < 20) return STATUS_DEVICE_CONFIGURATION_ERROR;
                            multiplier = vgl_u32(pci + cap + 16);
                        }
                    }
                }
                cap = pci[cap + 1] & ~3u;
            }
            if(cap || !ports[1] || !ports[2] || !ports[3] || !ports[4])
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            /* The command queue has notify offset zero in protocol v1. */
            if(multiplier != 0) return STATUS_NOT_SUPPORTED;
            command = vgl_u16(pci + 4) | 5; /* I/O space + bus mastering */
            if(HalSetBusDataByOffset(PCIConfiguration, 0, slot, &command, 4, sizeof(command)) != sizeof(command))
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            t->common = ports[1]; t->notify = ports[2];
            t->isr = ports[3]; t->config = ports[4];
            if(vgl_read32(t->config, 0) != V86GL_MAGIC ||
               vgl_read32(t->config, 4) != 1 ||
               vgl_read32(t->config, 8) != V86GL_DEFAULT_BUFFER_BYTES)
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            DbgPrint(V86GL_LOG_PREFIX "virtio discovered slot=%lu common=%04x notify=%04x\n",
                     slot, t->common, t->notify);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_SUCH_DEVICE;
}

static void vgl_transport_destroy(VGL_TRANSPORT *t)
{
    /* Reset revokes the registered arena and all pending host write callbacks
     * before any of the driver's physical allocations can be released. */
    if(t->initialized)
    {
        vgl_write8(t->common, 20, 0);
        t->initialized = FALSE;
    }
    t->registered = FALSE;
    if(t->queue_memory)
    {
        MmFreeContiguousMemory(t->queue_memory);
        t->queue_memory = NULL;
    }
}

static NTSTATUS vgl_transport_init(VGL_TRANSPORT *t)
{
    PHYSICAL_ADDRESS low, high, boundary, physical;
    NTSTATUS status = vgl_discover(t);
    VGL_VIRTQ_DESC *descriptors;
    if(!NT_SUCCESS(status)) return status;
    vgl_write8(t->common, 20, 0);
    t->initialized = TRUE;
    if(vgl_read8(t->common, 20) != 0) goto failed;
    vgl_write8(t->common, 20, 3); /* ACKNOWLEDGE | DRIVER */
    vgl_write32(t->common, 0, 0);
    if(!(vgl_read32(t->common, 4) & 1)) goto failed;
    vgl_write32(t->common, 0, 1);
    if(!(vgl_read32(t->common, 4) & 1)) goto failed;
    vgl_write32(t->common, 8, 0);
    vgl_write32(t->common, 12, 1); /* custom SHARED_ARENA */
    vgl_write32(t->common, 8, 1);
    vgl_write32(t->common, 12, 1); /* VIRTIO_F_VERSION_1 */
    vgl_write8(t->common, 20, 11); /* FEATURES_OK */
    if(!(vgl_read8(t->common, 20) & 8)) goto failed;
    vgl_write16(t->common, 22, 0);
    if(vgl_read16(t->common, 24) < VGL_QUEUE_SIZE || vgl_read16(t->common, 30) != 0)
        goto failed;
    low.QuadPart = 0; high.QuadPart = 0xFFFFFFFFULL; boundary.QuadPart = 0;
    t->queue_memory = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, low, high, boundary, MmCached);
    if(!t->queue_memory) { vgl_transport_destroy(t); return STATUS_INSUFFICIENT_RESOURCES; }
    RtlZeroMemory(t->queue_memory, PAGE_SIZE);
    physical = MmGetPhysicalAddress(t->queue_memory);
    if(physical.HighPart) goto failed;
    t->queue_physical = physical.LowPart;
    t->available = t->used = 0;
    descriptors = t->queue_memory;
    descriptors[0].address_low = physical.LowPart + VGL_REQUEST_OFFSET;
    descriptors[0].length = sizeof(VGL_REQUEST);
    descriptors[0].flags = 1; descriptors[0].next = 1;
    descriptors[1].address_low = physical.LowPart + VGL_REPLY_OFFSET;
    descriptors[1].length = sizeof(VGL_REPLY);
    descriptors[1].flags = 2;
    *(USHORT *)((PUCHAR)t->queue_memory + VGL_AVAIL_OFFSET) = 1; /* NO_INTERRUPT */
    vgl_write16(t->common, 24, VGL_QUEUE_SIZE);
    vgl_write32(t->common, 32, physical.LowPart);
    vgl_write32(t->common, 36, 0);
    vgl_write32(t->common, 40, physical.LowPart + VGL_AVAIL_OFFSET);
    vgl_write32(t->common, 44, 0);
    vgl_write32(t->common, 48, physical.LowPart + VGL_USED_OFFSET);
    vgl_write32(t->common, 52, 0);
    KeMemoryBarrier();
    vgl_write16(t->common, 28, 1);
    vgl_write8(t->common, 20, 15); /* DRIVER_OK */
    if((vgl_read8(t->common, 20) & 0xCF) != 15) goto failed;
    return STATUS_SUCCESS;
failed:
    vgl_transport_destroy(t);
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static NTSTATUS vgl_request(VGL_TRANSPORT *t, ULONG opcode, ULONG address, ULONG length, ULONG flags)
{
    VGL_REQUEST *request;
    volatile VGL_REPLY *reply;
    volatile USHORT *avail, *used;
    volatile ULONG *used_entry;
    ULONG attempt;
    LARGE_INTEGER delay;
    if(!t->initialized) return STATUS_DEVICE_NOT_READY;
    request = (VGL_REQUEST *)((PUCHAR)t->queue_memory + VGL_REQUEST_OFFSET);
    reply = (volatile VGL_REPLY *)((PUCHAR)t->queue_memory + VGL_REPLY_OFFSET);
    avail = (volatile USHORT *)((PUCHAR)t->queue_memory + VGL_AVAIL_OFFSET);
    used = (volatile USHORT *)((PUCHAR)t->queue_memory + VGL_USED_OFFSET);
    RtlZeroMemory(request, sizeof(*request));
    request->opcode = opcode; request->address_low = address;
    request->length = length; request->flags = flags;
    reply->status = 0xFFFFFFFFu;
    avail[2 + (t->available & (VGL_QUEUE_SIZE - 1))] = 0;
    KeMemoryBarrier();
    avail[1] = ++t->available;
    KeMemoryBarrier();
    vgl_write16(t->notify, 0, 0);
    /* v86 normally accepts the command during the notification write. The
     * bounded passive-level wait also handles delayed virtqueue completion. */
    delay.QuadPart = -10000; /* 1 ms */
    for(attempt = 0; attempt < 5000; ++attempt)
    {
        if(used[1] != t->used) break;
        if(vgl_read8(t->common, 20) & 0xC0) break;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    KeMemoryBarrier();
    if(used[1] != (USHORT)(t->used + 1))
    {
        vgl_transport_destroy(t);
        return STATUS_IO_TIMEOUT;
    }
    used_entry = (volatile ULONG *)((PUCHAR)t->queue_memory + VGL_USED_OFFSET + 4 +
        (t->used & (VGL_QUEUE_SIZE - 1)) * 8);
    if(used_entry[0] != 0 || used_entry[1] != sizeof(VGL_REPLY))
    {
        vgl_transport_destroy(t);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    ++t->used;
    if(reply->status)
    {
        DbgPrint(V86GL_LOG_PREFIX "virtio request=%lu failed status=%lu\n", opcode, reply->status);
        return STATUS_UNSUCCESSFUL;
    }
    if(opcode == VGL_REGISTER_ARENA) t->registered = TRUE;
    if(opcode == VGL_UNREGISTER_ARENA) t->registered = FALSE;
    return STATUS_SUCCESS;
}
#endif
