#ifndef D9WG_FPU_H
#define D9WG_FPU_H
#include <stdint.h>
/* D3D8/9 x86 creation defaults: masked exceptions, single precision and
 * round-to-nearest. FPU_PRESERVE leaves the calling thread's state alone. */
uint16_t d9wg_fpu_control(uint16_t control, int preserve);
void d9wg_fpu_setup(int preserve);
#endif
