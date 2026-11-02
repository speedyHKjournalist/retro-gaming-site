#ifndef D9WG_PALETTE_STATE_H
#define D9WG_PALETTE_STATE_H
#include <stdint.h>
#define D9WG_PALETTE_COUNT 65536u
#define D9WG_PALETTE_ENTRIES 256u
#ifndef D9WG_PALETTE_ALLOC
#define D9WG_PALETTE_ALLOC(bytes) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (bytes))
#define D9WG_PALETTE_FREE(pointer) HeapFree(GetProcessHeap(), 0, (pointer))
#endif
typedef struct D9WGPalettePage {
    uint32_t *entries[256];
} D9WGPalettePage;
typedef struct D9WGPaletteStore {
    D9WGPalettePage *pages[256];
} D9WGPaletteStore;
/* Missing palettes are implicitly opaque white. Two sparse levels keep a high
 * palette ID cheap; allocation failures publish neither a page nor a palette. */
static __inline const uint32_t *d9wg_palette_find(const D9WGPaletteStore *store, uint32_t index)
{
    D9WGPalettePage *page;
    if (index >= D9WG_PALETTE_COUNT) return 0;
    page = store->pages[index >> 8];
    return page ? page->entries[index & 255] : 0;
}
static __inline int d9wg_palette_set(D9WGPaletteStore *store, uint32_t index, const void *entries)
{
    D9WGPalettePage *page;
    uint32_t *colors;
    const uint8_t *source = (const uint8_t *)entries;
    int fresh;
    if (index >= D9WG_PALETTE_COUNT || !entries) return 0;
    page = store->pages[index >> 8];fresh = !page;
    if (fresh && !(page = D9WG_PALETTE_ALLOC(sizeof(*page)))) return 0;
    colors = page->entries[index & 255];
    if (!colors && !(colors = D9WG_PALETTE_ALLOC(256 * sizeof(*colors)))) {
        if (fresh) D9WG_PALETTE_FREE(page);
        return 0;
    }
    for (uint32_t i = 0; i < 256; ++i) {
        const uint8_t *entry = source + i * 4;
        colors[i] = ((uint32_t)entry[3] << 24) | ((uint32_t)entry[0] << 16)
            | ((uint32_t)entry[1] << 8) | entry[2];
    }
    page->entries[index & 255] = colors;store->pages[index >> 8] = page;
    return 1;
}
static __inline void d9wg_palette_get(const D9WGPaletteStore *store, uint32_t index, void *entries)
{
    const uint32_t *colors = d9wg_palette_find(store, index);
    uint8_t *output = (uint8_t *)entries;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t value = colors ? colors[i] : UINT32_MAX;
        output[i * 4] = (uint8_t)(value >> 16);output[i * 4 + 1] = (uint8_t)(value >> 8);
        output[i * 4 + 2] = (uint8_t)value;output[i * 4 + 3] = (uint8_t)(value >> 24);
    }
}
static __inline void d9wg_palette_free(D9WGPaletteStore *store)
{
    for (uint32_t i = 0; i < 256; ++i) {
        D9WGPalettePage *page = store->pages[i];
        if (!page) continue;
        for (uint32_t j = 0; j < 256; ++j)
            if (page->entries[j]) D9WG_PALETTE_FREE(page->entries[j]);
        D9WG_PALETTE_FREE(page);store->pages[i] = 0;
    }
}
#endif
