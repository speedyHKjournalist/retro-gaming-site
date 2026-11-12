#ifndef D9WG_LIGHTING_PROFILES_H
#define D9WG_LIGHTING_PROFILES_H
#include "d9wg_lighting.h"
/* Return zero without modifying output when the full matrix is singular. */
int d9wg_inverse_transpose4(const void *matrix, float *output);
/* Pre-D3D7 lights in world space. Keep the eye at the origin by translating
 * both positions and point lights by the world-space camera position. */
void d9wg_lighting_legacy_geometry(D9WGTNLLighting *state,
        const void *world, const void *view, float *light_transform);
/* Original D3D7 CPU processing retains its previous inverse when inversion
 * fails. Without NORMALIZENORMALS, rigid transforms bypass that cache. The
 * caller commits the returned candidate only after processing succeeds. */
int d9wg_lighting_process7_normal(D9WGTNLLighting *state,
        const void *world_view, const float *previous);
#endif
