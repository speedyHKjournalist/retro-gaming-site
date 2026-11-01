#ifndef D9WG_TNL_MATH_H
#define D9WG_TNL_MATH_H
/* CRT-free lighting primitives. Nonnegative base/power and cone angles in
 * [0, pi] are the Direct3D lighting domain. */
float d9wg_tnl_sqrt(float value);
float d9wg_tnl_pow(float base, float power);
float d9wg_tnl_cos_half(float angle);
#endif
