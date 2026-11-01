/* Test-only IOCTL constants; layout comes from production v86gl_ioctl.h. */
#ifndef TEST_WINIOCTL_H
#define TEST_WINIOCTL_H
#define FILE_DEVICE_UNKNOWN 0x22u
#define METHOD_BUFFERED 0u
#define FILE_ANY_ACCESS 0u
#define CTL_CODE(type, function, method, access) (((type) << 16) | ((access) << 14) | ((function) << 2) | (method))
#endif
