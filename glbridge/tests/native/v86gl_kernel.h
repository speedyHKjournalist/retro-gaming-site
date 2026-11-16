#ifndef V86GL_NATIVE_KERNEL_H
#define V86GL_NATIVE_KERNEL_H
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../openglproxy/v86gl_ioctl.h"
typedef void *PVOID;
typedef uint32_t ULONG;
typedef uintptr_t ULONG_PTR;
typedef int NTSTATUS, BOOLEAN, FAST_MUTEX;
typedef void *HANDLE;
typedef struct Process { unsigned id, references;int alive; } Process, *PEPROCESS, *PKPROCESS;
typedef struct { PEPROCESS previous; } KAPC_STATE;
typedef struct { uint64_t QuadPart;uint32_t LowPart; } PHYSICAL_ADDRESS;
typedef struct { void *buffer;PEPROCESS mapped; } MDL, *PMDL;
typedef struct { void *FsContext; } FILE_OBJECT;
typedef struct { unsigned MajorFunction;FILE_OBJECT *FileObject;
    struct { struct { ULONG IoControlCode, InputBufferLength, OutputBufferLength; } DeviceIoControl; } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct { struct { NTSTATUS Status;ULONG_PTR Information; } IoStatus;
    struct { void *SystemBuffer; } AssociatedIrp;IO_STACK_LOCATION stack;
} IRP, *PIRP;
typedef struct { void *DeviceExtension; } DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct { int registered, owner_arenas; } VGL_TRANSPORT;
#define NTAPI
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(x) ((x) >= 0)
#define STATUS_SUCCESS 0
#define STATUS_INSUFFICIENT_RESOURCES -1
#define STATUS_BUFFER_TOO_SMALL -2
#define STATUS_NOT_SUPPORTED -3
#define STATUS_ACCESS_DENIED -4
#define STATUS_DEVICE_BUSY -5
#define STATUS_INVALID_BUFFER_SIZE -6
#define STATUS_INVALID_PARAMETER -7
#define STATUS_INVALID_HANDLE -8
#define STATUS_INVALID_DEVICE_REQUEST -9
#define NonPagedPool 0
#define UserMode 1
#define MmCached 1
#define NormalPagePriority 16
#define VGL_REGISTER_ARENA 1
#define VGL_SUBMIT 2
#define VGL_UNREGISTER_ARENA 3
#define IRP_MJ_CREATE 0
#define IRP_MJ_CLOSE 2
#define IRP_MJ_CLEANUP 18
#define IO_NO_INCREMENT 0
#define V86GL_POOL_TAG 0x6c674756u
#define V86GL_LOG_PREFIX ""
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define RtlZeroMemory(p,n) memset(p,0,n)
#define DbgPrint(...) ((void)0)
#define KeMemoryBarrier() ((void)0)
#define IoGetCurrentIrpStackLocation(irp) (&(irp)->stack)
static Process processes[] = {{1,0,1},{2,0,1},{3,0,1}};
static PEPROCESS current = &processes[0];
static unsigned pools, buffers, mdls, unmapped, attached, releases, submitted, registered[1024];
static int mutex_depth, fail_pool, fail_buffer, fail_mdl, fail_map;
static PEPROCESS PsGetCurrentProcess(void) { return current; }
static HANDLE PsGetCurrentProcessId(void) { return (HANDLE)(uintptr_t)current->id; }
static void KeEnterCriticalRegion(void) {}
static void KeLeaveCriticalRegion(void) {}
static void ExAcquireFastMutexUnsafe(FAST_MUTEX *mutex) { (void)mutex;assert(!mutex_depth++); }
static void ExReleaseFastMutexUnsafe(FAST_MUTEX *mutex) { (void)mutex;assert(!--mutex_depth); }
static void IoCompleteRequest(PIRP irp, int boost) { (void)irp;(void)boost; }
static void *ExAllocatePoolWithTag(int pool, size_t bytes, unsigned tag) {
    (void)pool;(void)tag;if(fail_pool) { fail_pool=0;return NULL; }
    void *p=malloc(bytes);assert(p);pools++;return p;
}
static void ExFreePool(void *p) { assert(pools);pools--;free(p); }
static void ObReferenceObject(PEPROCESS p) { p->references++; }
static void ObDereferenceObject(PEPROCESS p) { assert(p->references);p->references--; }
static void *MmAllocateContiguousMemorySpecifyCache(unsigned bytes, PHYSICAL_ADDRESS low,
    PHYSICAL_ADDRESS high, PHYSICAL_ADDRESS boundary, int cache) {
    (void)low;(void)high;(void)boundary;(void)cache;
    if(fail_buffer) { fail_buffer=0;return NULL; }
    void *p=malloc(bytes);assert(p);memset(p,0xa5,bytes);buffers++;return p;
}
static void MmFreeContiguousMemory(void *p) { assert(buffers);buffers--;free(p); }
static PMDL IoAllocateMdl(void *buffer, unsigned bytes, int a, int b, void *irp) {
    (void)bytes;(void)a;(void)b;(void)irp;if(fail_mdl) { fail_mdl=0;return NULL; }
    PMDL mdl=calloc(1,sizeof(*mdl));assert(mdl);mdl->buffer=buffer;mdls++;return mdl;
}
static void IoFreeMdl(PMDL mdl) { assert(!mdl->mapped&&mdls);mdls--;free(mdl); }
static void MmBuildMdlForNonPagedPool(PMDL mdl) { (void)mdl; }
static void *MmMapLockedPagesSpecifyCache(PMDL mdl,int mode,int cache,void *address,int bugcheck,int priority) {
    (void)mode;(void)cache;(void)address;(void)bugcheck;(void)priority;
    if(fail_map) { fail_map=0;return NULL; }
    assert(current->alive&&!mdl->mapped);mdl->mapped=current;return mdl->buffer;
}
static void MmUnmapLockedPages(void *address,PMDL mdl) {
    assert(current->alive&&mdl->mapped==current&&address==mdl->buffer);mdl->mapped=NULL;unmapped++;
}
static void KeStackAttachProcess(PKPROCESS p,KAPC_STATE *state) {
    assert(p->alive&&mutex_depth);state->previous=current;current=p;attached++;
}
static void KeUnstackDetachProcess(KAPC_STATE *state) { current=state->previous; }
static PHYSICAL_ADDRESS MmGetPhysicalAddress(void *p) {
    PHYSICAL_ADDRESS physical={0};physical.LowPart=(uint32_t)(uintptr_t)p;return physical;
}
static NTSTATUS vgl_request_owner(VGL_TRANSPORT *transport,ULONG owner,ULONG op,ULONG address,ULONG length,ULONG flags) {
    (void)address;(void)length;(void)flags;assert(owner<1024);
    if(op==VGL_REGISTER_ARENA) { registered[owner]=1;if(!owner)transport->registered=1; }
    if(op==VGL_UNREGISTER_ARENA) { if(registered[owner])releases++;registered[owner]=0;if(!owner)transport->registered=0; }
    if(op==VGL_SUBMIT) { assert(registered[owner]);submitted++; }
    return STATUS_SUCCESS;
}
static NTSTATUS vgl_request(VGL_TRANSPORT *transport,ULONG op,ULONG address,ULONG length,ULONG flags) {
    return vgl_request_owner(transport,0,op,address,length,flags);
}
#endif
