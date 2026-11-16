#ifndef D9WG_COLOR_FILL_PIXEL_H
#define D9WG_COLOR_FILL_PIXEL_H
#include <stdint.h>
#include <string.h>
/* ColorFill starts with eight-bit channels. Pack these directly for CPU
 * mirrors so a 16-bit UNORM surface does not lose precision through the
 * host's half-float representation. Returns zero for unsupported layouts. */
static unsigned d9wg_color_fill_pixel(unsigned format, uint32_t color, unsigned char out[16])
{
    unsigned r=(color>>16)&255, g=(color>>8)&255, b=color&255, a=color>>24;
    uint16_t words[4]; uint32_t packed; float values[4]; unsigned count=0;
    switch(format) {
    case 21: packed=color; break;
    case 22: packed=color|0xff000000u; break;
    case 23: words[0]=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)); memcpy(out,words,2); return 2;
    case 34: case 36:
        words[0]=(uint16_t)(r*257); words[1]=(uint16_t)(g*257);
        words[2]=(uint16_t)(b*257); words[3]=(uint16_t)(a*257);
        count=format==34?4:8; memcpy(out,words,count); return count;
    case 111: case 112: case 113:
        count=format==111?1:format==112?2:4;
        values[0]=r/255.0f; values[1]=g/255.0f; values[2]=b/255.0f; values[3]=a/255.0f;
        for(unsigned i=0;i<count;i++) {
            uint32_t bits; memcpy(&bits,&values[i],4);
            /* Nonzero 8-bit UNORM values are normal in binary16. Round to
             * nearest even, including a carry into the next exponent. */
            words[i]=bits ? (uint16_t)(((bits-0x38000000u)+0xfffu+((bits>>13)&1u))>>13) : 0;
        }
        memcpy(out,words,count*2); return count*2;
    case 114: case 115: case 116:
        values[0]=r/255.0f; values[1]=g/255.0f; values[2]=b/255.0f; values[3]=a/255.0f;
        count=format==114?4:format==115?8:16; memcpy(out,values,count); return count;
    default: return 0;
    }
    memcpy(out,&packed,4); return 4;
}
#endif
