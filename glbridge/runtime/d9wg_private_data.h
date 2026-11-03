#ifndef D9WG_PRIVATE_DATA_H
#define D9WG_PRIVATE_DATA_H
#include <stdint.h>

typedef struct D9WGPrivateData D9WGPrivateData;
typedef struct D9WGPrivateStore {
    D9WGPrivateData* head;
} D9WGPrivateStore;

typedef struct D9WGPrivateOps {
    void* context;
    void* (*allocate)(void*, uint32_t);
    void (*free_memory)(void*, void*);
    void (*retain_interface)(void*, void*);
    void (*release_interface)(void*, void*);
    void (*lock)(void*);
    void (*unlock)(void*);
} D9WGPrivateOps;

enum {
    D9WG_PRIVATE_OK,
    D9WG_PRIVATE_INVALID,
    D9WG_PRIVATE_NOT_FOUND,
    D9WG_PRIVATE_MORE_DATA,
    D9WG_PRIVATE_OUT_OF_MEMORY,
    D9WG_PRIVATE_IUNKNOWN = 1
};

/* Tags are opaque 16-byte GUIDs. A zero-initialized store is ready to use.
 * Interface callbacks run without the store lock. Immutable entry pins keep
 * data/object storage alive while Get invokes a reentrant AddRef callback.
 * Get reads/writes four bytes of size in host order through byte copies, so
 * DWORD storage and an overlapping output buffer do not require alias casts. */
int d9wg_private_set(D9WGPrivateStore*, const void* tag, const void* data,
    uint32_t size, uint32_t flags, const D9WGPrivateOps*);
int d9wg_private_get(D9WGPrivateStore*, const void* tag, void* data,
    void* size, const D9WGPrivateOps*);
int d9wg_private_free(D9WGPrivateStore*, const void* tag, const D9WGPrivateOps*);
void d9wg_private_clear(D9WGPrivateStore*, const D9WGPrivateOps*);

#endif
