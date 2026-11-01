#include "d9wg_fpu.h"

uint16_t d9wg_fpu_control(uint16_t control, int preserve)
{
    if (preserve) return control;
    /* PC (8..9), RC (10..11), exception masks (0..5). Other bits survive. */
    return (uint16_t)((control & (uint16_t)~0x0f3fu) | 0x003fu);
}

void d9wg_fpu_setup(int preserve)
{
#if defined(__i386__)
    uint16_t control;
    if (preserve) return;
    __asm__ volatile ("fnstcw %0" : "=m" (control) : : "memory");
    control = d9wg_fpu_control(control, 0);
    __asm__ volatile ("fldcw %0" : : "m" (control) : "memory");
#else
    /* Host-native pure tests run on ARM too; guest DLLs target i686 only. */
    (void)preserve;
#endif
}
