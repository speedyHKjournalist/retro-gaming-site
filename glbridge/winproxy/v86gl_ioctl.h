#ifndef V86GL_IOCTL_H
#define V86GL_IOCTL_H

#ifdef _KERNEL_MODE
#include <ntddk.h>
typedef ULONG V86GLU32;
#else
#include <stdint.h>
#include <winioctl.h>
typedef uint32_t V86GLU32;
#endif

#define V86GL_MAGIC 0x324C4756u
#define V86GL_VERSION 1u

#define V86GL_PCI_PORT_DEFAULT 0xF100u
#define V86GL_REG_STATUS 0x0Cu
#define V86GL_REG_DESC_LO 0x10u
#define V86GL_REG_DESC_HI 0x14u
#define V86GL_REG_DESC_LEN 0x18u
#define V86GL_REG_COMMAND 0x1Cu
#define V86GL_REG_LAST_FRAME 0x20u
#define V86GL_REG_LAST_BYTES 0x24u
#define V86GL_REG_ERROR 0x28u

#define V86GL_CMD_SUBMIT (1u << 0)
#define V86GL_CMD_FORCE_PRESENT (1u << 1)

#define V86GL_STATUS_ERROR (1u << 1)
#define V86GL_STATUS_SUBMITTED (1u << 7)

#define V86GL_DESC_FLAG_PRESENT (1u << 0)
#define V86GL_SUBMIT_FORCE_PRESENT (1u << 0)

#define V86GL_DEVICE_DOS_NAME "\\\\.\\v86gl"
#define V86GL_DEFAULT_BUFFER_BYTES (16u * 1024u * 1024u)

#define V86GL_IOCTL_MAP_BUFFER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define V86GL_IOCTL_SUBMIT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define V86GL_IOCTL_UNMAP_BUFFER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct V86GLDMADesc {
    V86GLU32 magic;
    V86GLU32 version;
    V86GLU32 flags;
    V86GLU32 frame_id;
    V86GLU32 command_count;
    V86GLU32 command_bytes;
    V86GLU32 reserved0;
    V86GLU32 reserved1;
} V86GLDMADesc;

typedef struct V86GLMapBuffer {
    V86GLU32 user_address;
    V86GLU32 buffer_bytes;
} V86GLMapBuffer;

typedef struct V86GLSubmit {
    V86GLU32 descriptor_bytes;
    V86GLU32 flags;
} V86GLSubmit;
#pragma pack(pop)

#endif
