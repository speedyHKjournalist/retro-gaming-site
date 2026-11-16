#ifndef D9WG_TNL_MATH_CASES_H
#define D9WG_TNL_MATH_CASES_H
#include <math.h>
#include <stdint.h>
#include "runtime/d9wg_tnl_math.h"
#define TNL_MATH_CHECK(c) do { if(!(c)) return __LINE__; } while(0)
static int tnl_math_cases(unsigned count,double errors[2])
{
    uint32_t seed=0x7a371091u;
    errors[0]=errors[1]=0;
    TNL_MATH_CHECK(d9wg_tnl_pow(0,0)==1&&d9wg_tnl_pow(0,1)==0&&d9wg_tnl_pow(1,128)==1);
    TNL_MATH_CHECK(fabs(d9wg_tnl_pow(0.5f,2)-0.25f)<1e-7);
    for(unsigned i=0;i<count;i++) {
        seed=seed*1664525u+1013904223u;float base=(float)((seed>>8)+1u)/16777217.0f;
        seed=seed*1664525u+1013904223u;float power=(float)(seed>>8)/131072.0f;
        float expected=(float)pow((double)base,power),value=d9wg_tnl_pow(base,power);
        double error=fabs((double)value-expected);if(error>errors[0])errors[0]=error;
        /* Fixed before measurement: below one 8-bit lighting unit. */
        TNL_MATH_CHECK(error<=2e-6+fabs(expected)*3e-5);
        union {uint32_t bits;float value;} root;root.bits=seed&0x7f7fffffu;
        expected=(float)sqrt((double)root.value);value=d9wg_tnl_sqrt(root.value);
        TNL_MATH_CHECK(fabs((double)value-expected)<=fabs(expected)*2e-6+1e-30);
        float angle=(float)i/(float)(count-1u)*3.14159265358979323846f;
        expected=(float)cos((double)angle*0.5);value=d9wg_tnl_cos_half(angle);
        error=fabs((double)value-expected);if(error>errors[1])errors[1]=error;
        TNL_MATH_CHECK(error<=2e-6);
    }
    return 0;
}
#undef TNL_MATH_CHECK
#endif
