#ifndef D3D8_PROTOCOL_H
#define D3D8_PROTOCOL_H

/*
 * The D3D8 guest frontend does not define a wire protocol of its own.
 *
 * D3D8 is very nearly a semantic subset of D3D9: the two APIs share render
 * state numbering, texture-stage-state numbering, FVF bit layout, primitive
 * types, formats, and -- for shader model 1.x -- bytecode token encoding.
 * The places they genuinely differ are few, local, and enumerated in this
 * header.  Every one of them is a guest-side translation, so the host sees a
 * single D3D9-shaped command stream no matter which API produced it.
 *
 * This is the same architecture DXVK uses (its d3d8 is a translation layer
 * over its d3d9 rather than a second backend), and for the same reason: a
 * second backend has to re-implement cube/volume textures, clip planes, point
 * sprites, vertex blending, multi-stream layouts, palettes, multisampling,
 * readback and the whole shader translator, all of which ../d3d9-webgpu/
 * already implements and 3DMark06 already exercises.
 *
 * Consequences that are easy to get wrong, so stated once here:
 *
 *  - Batches ride V86GL_CTRL_D3D9_BATCH (0xFFE1) and are decoded by
 *    ../d3d9-webgpu/d3d9_executor.js.  ../d3d8-webgpu/d3d8_executor.js is no
 *    longer on the path for new work; see this directory's README.
 *  - The last D9WG_RESPONSE_REGION_BYTES of the DMA mapping are the host's
 *    response region, not batch space.  batch_capacity() must exclude it.
 *  - A guest process loads either d3d8.dll or d3d9.dll, never both, so the
 *    two frontends never contend for the single DMA arena.
 */

#include "../d3d9proxy/d3d9_protocol.h"

/* API8 state translation is shared with the private core8 adapters. */
#include "../runtime/d9wg_state_bridge8.h"

/* SDK-independent declaration fields and register semantics. */
#include "../runtime/d9wg_declaration8_policy.h"

#endif
