#ifndef D9WG_STATE_DEFAULTS_BRIDGE_H
#define D9WG_STATE_DEFAULTS_BRIDGE_H
#include "d9wg_state_defaults.h"
/* The wire starts with D3D9 defaults. Emit only the two defaults which can
 * differ by API or reported point capability, after CreateDevice and Reset. */
static BOOL emit_render_default_patches(uint32_t device, uint32_t api,
        uint32_t auto_depth, const void *render_states)
{
    const uint32_t states[] = {155u, 166u};
    for (unsigned i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
        uint32_t state = states[i], value;
        __builtin_memcpy(&value, (const uint8_t *)render_states + state*4u, 4u);
        if (value == d9wg_render_default(9, state, auto_depth, D9WG_DEFAULT_POINT_MAX_BITS)) continue;
        /* The only API-specific host default is POINTSIZE_MIN for D3D8. */
        if (state == 155u && api != 8u) continue;
        D9WGSetRenderState command = {device, state, value, 0};
        if (!emit_command(D9WG_OP_SET_RENDER_STATE, &command, sizeof(command))) return FALSE;
    }
    return TRUE;
}
#endif
