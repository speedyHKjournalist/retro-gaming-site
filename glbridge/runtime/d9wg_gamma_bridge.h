#ifndef D9WG_GAMMA_BRIDGE_H
#define D9WG_GAMMA_BRIDGE_H

/* Caller provides the SDK ramp type, protocol and emit_command. Both SDKs
 * expose three contiguous banks of 256 unsigned 16-bit entries. */
static BOOL d9wg_emit_gamma_ramp(uint32_t device_handle, DWORD flags,
    const D3DGAMMARAMP* ramp)
{
    D9WGSetGammaRamp command;
    command.device_handle = device_handle;
    command.swap_chain = 0;
    command.flags = flags;
    command.reserved = 0;
    for (UINT i = 0; i < 256; ++i) {
        command.ramp[i] = ramp->red[i];
        command.ramp[256 + i] = ramp->green[i];
        command.ramp[512 + i] = ramp->blue[i];
    }
    return emit_command(D9WG_OP_SET_GAMMA_RAMP, &command, sizeof(command));
}

static void d9wg_set_gamma_ramp(uint32_t device_handle, DWORD flags,
    const D3DGAMMARAMP* ramp, D3DGAMMARAMP* stored, BOOL* supplied,
    DWORD* stored_flags)
{
    if (!ramp)
        return;
    *stored = *ramp;
    *supplied = TRUE;
    *stored_flags = flags;
    /* The API returns void. Queries retain the requested ramp even if the
     * command queue cannot accept it, as in the D3D9 frontend's prior policy. */
    d9wg_emit_gamma_ramp(device_handle, flags, ramp);
}

static void d9wg_get_gamma_ramp(const D3DGAMMARAMP* stored, BOOL supplied,
    D3DGAMMARAMP* ramp)
{
    if (!ramp)
        return;
    if (supplied) {
        *ramp = *stored;
        return;
    }
    for (UINT i = 0; i < 256; ++i) {
        WORD value = (WORD)(i * 257u);
        ramp->red[i] = ramp->green[i] = ramp->blue[i] = value;
    }
}

#endif
