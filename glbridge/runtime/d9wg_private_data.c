#include "d9wg_private_data.h"
#include <stddef.h>

struct D9WGPrivateData {
    D9WGPrivateData* next;
    uint8_t tag[16];
    uint32_t size, flags, references;
    void* object;
    uint8_t bytes[];
};

static void copy_bytes(void* destination, const void* source, uint32_t size)
{
    uint8_t* out = destination;
    const uint8_t* in = source;
    for (uint32_t i = 0; i < size; ++i)
        out[i] = in[i];
}

static void lock_store(const D9WGPrivateOps* ops)
{
    if (ops->lock)
        ops->lock(ops->context);
}

static void unlock_store(const D9WGPrivateOps* ops)
{
    if (ops->unlock)
        ops->unlock(ops->context);
}

static D9WGPrivateData** find_entry(D9WGPrivateStore* store, const uint8_t* tag)
{
    D9WGPrivateData** link = &store->head;
    while (*link) {
        uint32_t i = 0;
        while (i < 16 && (*link)->tag[i] == tag[i])
            ++i;
        if (i == 16)
            break;
        link = &(*link)->next;
    }
    return link;
}

static void release_entry(D9WGPrivateData* entry, const D9WGPrivateOps* ops)
{
    lock_store(ops);
    uint32_t remaining = --entry->references;
    unlock_store(ops);
    if (!remaining) {
        if (entry->flags & D9WG_PRIVATE_IUNKNOWN)
            ops->release_interface(ops->context, entry->object);
        ops->free_memory(ops->context, entry);
    }
}

int d9wg_private_set(D9WGPrivateStore* store, const void* tag, const void* data,
    uint32_t size, uint32_t flags, const D9WGPrivateOps* ops)
{
    if (!store || !tag || !ops || !ops->allocate || !ops->free_memory
        || (!data && size)
        || ((flags & D9WG_PRIVATE_IUNKNOWN)
            && (size != sizeof(void*) || !data || !ops->retain_interface || !ops->release_interface)))
        return D9WG_PRIVATE_INVALID;
    uint32_t extra = flags & D9WG_PRIVATE_IUNKNOWN ? 0 : size;
    if (extra > UINT32_MAX - (uint32_t)sizeof(D9WGPrivateData))
        return D9WG_PRIVATE_OUT_OF_MEMORY;
    D9WGPrivateData* entry = ops->allocate(ops->context, (uint32_t)sizeof(*entry) + extra);
    if (!entry)
        return D9WG_PRIVATE_OUT_OF_MEMORY;
    entry->size = size;
    entry->flags = flags;
    entry->references = 1;
    entry->object = NULL;
    copy_bytes(entry->tag, tag, 16);
    if (flags & D9WG_PRIVATE_IUNKNOWN) {
        entry->object = (void*)data;
        /* Retain before replacing the old entry, including self-replacement. */
        ops->retain_interface(ops->context, entry->object);
    } else
        copy_bytes(entry->bytes, data, size);

    lock_store(ops);
    D9WGPrivateData** link = find_entry(store, entry->tag);
    D9WGPrivateData* old = *link;
    entry->next = old ? old->next : NULL;
    *link = entry;
    unlock_store(ops);
    if (old)
        release_entry(old, ops);
    return D9WG_PRIVATE_OK;
}

int d9wg_private_get(D9WGPrivateStore* store, const void* tag, void* data,
    void* size, const D9WGPrivateOps* ops)
{
    if (!store || !tag || !ops)
        return D9WG_PRIVATE_INVALID;
    lock_store(ops);
    D9WGPrivateData* entry = *find_entry(store, tag);
    if (!entry) {
        unlock_store(ops);
        return D9WG_PRIVATE_NOT_FOUND;
    }
    /* Windows faults on a NULL size for an existing tag. Refuse it safely. */
    if (!size || entry->references == UINT32_MAX) {
        unlock_store(ops);
        return D9WG_PRIVATE_INVALID;
    }
    ++entry->references;
    unlock_store(ops);

    uint32_t capacity;
    copy_bytes(&capacity, size, sizeof(capacity));
    copy_bytes(size, &entry->size, sizeof(entry->size));
    int result = D9WG_PRIVATE_OK;
    if (data) {
        if (capacity < entry->size)
            result = D9WG_PRIVATE_MORE_DATA;
        else if (entry->flags & D9WG_PRIVATE_IUNKNOWN) {
            ops->retain_interface(ops->context, entry->object);
            copy_bytes(data, &entry->object, entry->size);
        } else
            copy_bytes(data, entry->bytes, entry->size);
    }
    release_entry(entry, ops);
    return result;
}

int d9wg_private_free(D9WGPrivateStore* store, const void* tag, const D9WGPrivateOps* ops)
{
    if (!store || !tag || !ops)
        return D9WG_PRIVATE_INVALID;
    lock_store(ops);
    D9WGPrivateData** link = find_entry(store, tag);
    D9WGPrivateData* entry = *link;
    if (entry)
        *link = entry->next;
    unlock_store(ops);
    if (!entry)
        return D9WG_PRIVATE_NOT_FOUND;
    release_entry(entry, ops);
    return D9WG_PRIVATE_OK;
}

void d9wg_private_clear(D9WGPrivateStore* store, const D9WGPrivateOps* ops)
{
    lock_store(ops);
    D9WGPrivateData* entry = store->head;
    store->head = NULL;
    unlock_store(ops);
    while (entry) {
        D9WGPrivateData* next = entry->next;
        release_entry(entry, ops);
        entry = next;
    }
}
