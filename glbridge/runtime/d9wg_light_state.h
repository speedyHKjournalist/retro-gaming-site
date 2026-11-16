#ifndef D9WG_LIGHT_STATE_H
#define D9WG_LIGHT_STATE_H
#include <stdint.h>
/* The SDK light layouts are checked by d9wg_lighting_bridge.h. Each frontend
 * supplies its SDK type; MaxActiveLights limits enabled lights, not indices. */
typedef struct D9WGLightState {
    struct D9WGLightState *next;
    uint32_t index;
    D9WG_LIGHTING_SDK_LIGHT light;
    BOOL set, enabled, light_mask, enable_mask;
} D9WGLightState;
#ifndef D9WG_LIGHT_ALLOC
#define D9WG_LIGHT_ALLOC(bytes) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (bytes))
#define D9WG_LIGHT_FREE(pointer) HeapFree(GetProcessHeap(), 0, (pointer))
#endif
static __inline D9WGLightState *d9wg_light_find(D9WGLightState *entry, uint32_t index)
{
    while (entry && entry->index != index) entry = entry->next;
    return entry;
}
static __inline D9WGLightState *d9wg_light_get(D9WGLightState **list, uint32_t index, int create)
{
    D9WGLightState *entry = d9wg_light_find(*list, index);
    if (entry || !create) return entry;
    entry = D9WG_LIGHT_ALLOC(sizeof(*entry));
    if (!entry) return 0;
    entry->index = index;entry->next = *list;*list = entry;
    return entry;
}
static __inline void d9wg_light_free(D9WGLightState **list)
{
    D9WGLightState *entry = *list;*list = 0;
    while (entry) {D9WGLightState *next = entry->next;D9WG_LIGHT_FREE(entry);entry = next;}
}
static __inline void d9wg_light_default(D9WG_LIGHTING_SDK_LIGHT *light)
{
    /* D3DLIGHT_DIRECTIONAL, white diffuse RGB, +Z direction; all else zero.
     * Exact SDK words avoid separate D3D7/8/9 default tables. */
    static const uint32_t words[26] = {3,0x3f800000u,0x3f800000u,0x3f800000u,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x3f800000u,0,0,0,0,0,0,0};
    __builtin_memcpy(light, words, sizeof(words));
}
/* Native D3D7 and Wine's Windows-tested D3D8/9 queries return bit 7 for
 * enabled lights. Failed queries leave the caller's output untouched. */
static __inline int d9wg_light_query_enable(int known, int enabled, BOOL *output)
{
    if (!known || !output) return 0;
    *output = enabled ? 0x80 : 0;
    return 1;
}
#endif
