#ifndef D9WG_BC_CODEC_H
#define D9WG_BC_CODEC_H
#include <stdint.h>

/* Stored-channel BC1/2/3 blocks. DXT2/4 premultiplication is a transfer
 * policy, not an alternate block layout. All accesses permit unaligned data.
 * Format layout and interpolation:
 * https://learn.microsoft.com/en-us/windows/win32/direct3d9/opaque-and-1-bit-alpha-textures
 * https://learn.microsoft.com/en-us/windows/win32/direct3d9/textures-with-alpha-channels */
static unsigned d9wg_bc_kind(uint32_t format)
{
    return (format & 0xffffffu) == 0x545844u &&
        (format >> 24) >= '1' && (format >> 24) <= '5' ? (format >> 24) - '0' : 0u;
}
static uint16_t d9wg_bc_u16(const uint8_t *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}
static void d9wg_bc_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void d9wg_bc_565(uint16_t v, uint8_t out[4])
{
    unsigned r = v >> 11, g = (v >> 5) & 63u, b = v & 31u;
    out[0] = (uint8_t)((r << 3) | (r >> 2));
    out[1] = (uint8_t)((g << 2) | (g >> 4));
    out[2] = (uint8_t)((b << 3) | (b >> 2));
    out[3] = 255;
}
static void d9wg_bc_palette(unsigned kind, uint16_t a, uint16_t b, uint8_t p[4][4])
{
    d9wg_bc_565(a, p[0]); d9wg_bc_565(b, p[1]);
    for (unsigned c = 0; c < 4; ++c) {
        if (a > b || kind != 1u) {
            p[2][c] = c == 3u ? 255 : (uint8_t)((2u*p[0][c] + p[1][c] + 1u)/3u);
            p[3][c] = c == 3u ? 255 : (uint8_t)((p[0][c] + 2u*p[1][c] + 1u)/3u);
        } else {
            p[2][c] = c == 3u ? 255 : (uint8_t)((p[0][c] + p[1][c])/2u);
            p[3][c] = 0;
        }
    }
}
static void d9wg_bc_alpha(uint8_t a, uint8_t b, uint8_t p[8])
{
    p[0] = a; p[1] = b;
    unsigned divisor = a > b ? 7u : 5u;
    for (unsigned i = 2; i < divisor + 1u; ++i)
        p[i] = (uint8_t)(((divisor + 1u - i)*a + (i - 1u)*b + divisor/2u)/divisor);
    if (a <= b) { p[6] = 0; p[7] = 255; }
}
static int d9wg_bc_decode(uint32_t format, const uint8_t *block, uint8_t out[16][4])
{
    unsigned kind = d9wg_bc_kind(format);
    if (!kind || !block || !out) return 0;
    const uint8_t *color = block + (kind == 1u ? 0u : 8u);
    uint8_t p[4][4], alpha[8];
    d9wg_bc_palette(kind, d9wg_bc_u16(color), d9wg_bc_u16(color + 2u), p);
    d9wg_bc_alpha(block[0], block[1], alpha);
    for (unsigned i = 0; i < 16u; ++i) {
        unsigned selector = (color[4u + i/4u] >> ((i%4u)*2u)) & 3u;
        for (unsigned c = 0; c < 4u; ++c) out[i][c] = p[selector][c];
        if (kind == 2u || kind == 3u)
            out[i][3] = ((block[i/2u] >> ((i%2u)*4u)) & 15u)*17u;
        else if (kind >= 4u) {
            unsigned bit = i*3u, byte = 2u + bit/8u, shift = bit%8u;
            unsigned bits = block[byte];
            if (shift > 5u) bits |= (unsigned)block[byte + 1u] << 8;
            out[i][3] = alpha[(bits >> shift) & 7u];
        }
    }
    return 1;
}
static uint16_t d9wg_bc_pack565(const uint8_t p[4])
{
    return (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3));
}
static unsigned d9wg_bc_rgb_error(const uint8_t a[4], const uint8_t b[4])
{
    unsigned error = 0;
    for (unsigned c = 0; c < 3u; ++c) { int d = (int)a[c] - b[c]; error += d*d; }
    return error;
}
static unsigned d9wg_bc_block_error(unsigned kind, const uint8_t colors[16][4],
        uint16_t a, uint16_t b)
{
    uint8_t palette[4][4]; unsigned total = 0;
    d9wg_bc_palette(kind, a, b, palette);
    unsigned count = kind == 1u && a <= b ? 3u : 4u;
    for (unsigned i = 0; i < 16u; ++i) {
        if (kind == 1u && colors[i][3] < 128u) continue;
        unsigned best = ~0u;
        for (unsigned j = 0; j < count; ++j) {
            unsigned error = d9wg_bc_rgb_error(colors[i], palette[j]);
            if (error < best) best = error;
        }
        total += best;
    }
    return total;
}
static uint8_t d9wg_bc_channel(float v)
{
    return v <= 0.0f ? 0u : v >= 255.0f ? 255u : (uint8_t)(v + 0.5f);
}
static void d9wg_bc_refine(unsigned kind, const uint8_t colors[16][4],
        unsigned transparent, uint16_t *a, uint16_t *b)
{
    unsigned error = d9wg_bc_block_error(kind, colors, *a, *b);
    for (unsigned iteration = 0; iteration < 6u && error; ++iteration) {
        uint8_t palette[4][4]; unsigned aa = 0, ab = 0, bb = 0, ya[3] = {0}, yb[3] = {0};
        unsigned divisor = transparent ? 2u : 3u, count = transparent ? 3u : 4u;
        d9wg_bc_palette(kind, *a, *b, palette);
        for (unsigned i = 0; i < 16u; ++i) {
            if (kind == 1u && colors[i][3] < 128u) continue;
            unsigned best = 0, closest = ~0u;
            for (unsigned j = 0; j < count; ++j) {
                unsigned e = d9wg_bc_rgb_error(colors[i], palette[j]);
                if (e < closest) { closest = e; best = j; }
            }
            unsigned wa = best == 0u ? divisor : best == 1u ? 0u : best == 2u ? divisor - 1u : 1u;
            unsigned wb = divisor - wa;
            aa += wa*wa; ab += wa*wb; bb += wb*wb;
            for (unsigned c = 0; c < 3u; ++c) { ya[c] += wa*colors[i][c]; yb[c] += wb*colors[i][c]; }
        }
        float det = (float)aa*bb - (float)ab*ab;
        if (det <= 0.0f) break;
        uint8_t ca[4] = {0}, cb[4] = {0};
        for (unsigned c = 0; c < 3u; ++c) {
            ca[c] = d9wg_bc_channel(((float)ya[c]*bb - (float)yb[c]*ab)*divisor/det);
            cb[c] = d9wg_bc_channel(((float)yb[c]*aa - (float)ya[c]*ab)*divisor/det);
        }
        uint16_t na = d9wg_bc_pack565(ca), nb = d9wg_bc_pack565(cb);
        if ((transparent && na > nb) || (!transparent && na < nb)) { uint16_t t = na; na = nb; nb = t; }
        unsigned candidate = d9wg_bc_block_error(kind, colors, na, nb);
        if (candidate >= error) break;
        *a = na; *b = nb; error = candidate;
    }
}
/* A deterministic endpoint search over actual input colors. In particular,
 * two-color blocks retain both colors, rather than inventing RGB box corners. */
/* Input channels are already in the target stored alpha convention. */
static int d9wg_bc_encode(uint32_t format, const uint8_t in[16][4], uint8_t *block)
{
    unsigned kind = d9wg_bc_kind(format);
    if (!kind || !in || !block) return 0;
    uint8_t colors[16][4], palette[4][4];
    unsigned first = 0, last = 0, furthest = 0, found = 0, transparent = 0;
    for (unsigned i = 0; i < 16u; ++i) {
        for (unsigned c = 0; c < 4u; ++c) colors[i][c] = in[i][c];
        if (kind == 1u && in[i][3] < 128u) { transparent = 1; continue; }
        if (!found) { first = last = i; found = 1; }
        for (unsigned j = 0; j < i; ++j) {
            if (kind == 1u && in[j][3] < 128u) continue;
            unsigned error = d9wg_bc_rgb_error(colors[i], colors[j]);
            if (error > furthest) { furthest = error; first = j; last = i; }
        }
    }
    uint16_t a = found ? d9wg_bc_pack565(colors[first]) : 0;
    uint16_t b = found ? d9wg_bc_pack565(colors[last]) : 0;
    if ((transparent && a > b) || (!transparent && a < b)) { uint16_t t = a; a = b; b = t; }
    d9wg_bc_refine(kind, colors, transparent, &a, &b);
    d9wg_bc_palette(kind, a, b, palette);
    uint8_t *color = block + (kind == 1u ? 0u : 8u);
    for (unsigned i = 0; i < (kind == 1u ? 8u : 16u); ++i) block[i] = 0;
    d9wg_bc_put16(color, a); d9wg_bc_put16(color + 2u, b);
    for (unsigned i = 0; i < 16u; ++i) {
        unsigned best = 0, error = ~0u, count = kind == 1u && a <= b ? 3u : 4u;
        if (kind == 1u && in[i][3] < 128u) best = 3u;
        else for (unsigned c = 0; c < count; ++c) {
            unsigned e = d9wg_bc_rgb_error(colors[i], palette[c]);
            if (e < error) { error = e; best = c; }
        }
        color[4u + i/4u] |= best << ((i%4u)*2u);
        if (kind == 2u || kind == 3u) block[i/2u] |= (in[i][3] >> 4) << ((i%2u)*4u);
    }
    if (kind >= 4u) {
        uint8_t minimum = 255, maximum = 0, alpha[8];
        for (unsigned i = 0; i < 16u; ++i) {
            if (in[i][3] < minimum) minimum = in[i][3];
            if (in[i][3] > maximum) maximum = in[i][3];
        }
        block[0] = maximum; block[1] = minimum;
        d9wg_bc_alpha(maximum, minimum, alpha);
        for (unsigned i = 0; i < 16u; ++i) {
            unsigned best = 0, error = ~0u;
            for (unsigned j = 0; j < 8u; ++j) {
                int d = (int)in[i][3] - alpha[j]; unsigned e = d*d;
                if (e < error) { error = e; best = j; }
            }
            unsigned bit = i*3u, byte = 2u + bit/8u, shift = bit%8u;
            block[byte] |= best << shift;
            if (shift > 5u) block[byte + 1u] |= best >> (8u - shift);
        }
    }
    return 1;
}
#endif
