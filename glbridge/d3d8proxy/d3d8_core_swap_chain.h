#ifndef D8WG_CORE_SWAP_CHAIN_H
#define D8WG_CORE_SWAP_CHAIN_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* Consumes one owned additional-chain core reference on every return path.
 * Back buffers have independent frontend Surface8 references andDevice8
 * containers. Weak metadata survives publiczero while real storage is retained. */
HRESULT WINAPI d8wg_core_wrap_swap_chain8(struct IDirect3DSwapChain9 *owned,
        IDirect3DDevice8 *device, IDirect3DSwapChain8 **out);
/* Borrowed while the frontend view/storage is retained. */
struct IDirect3DSwapChain9 *WINAPI d8wg_core_swap_chain_backend8(IDirect3DSwapChain8 *texture);
#endif
