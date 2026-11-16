#ifndef DDRAW_DEPTH_LAYOUT_H
#define DDRAW_DEPTH_LAYOUT_H
#include <stdint.h>

/* Move integer fields without a floating-point round trip, including D32. */
static __inline uint32_t ddraw_depth_move_field(uint32_t word,
        uint32_t source, uint32_t destination)
{
    uint32_t source_shift = 0, destination_shift = 0, result = 0;
    uint32_t compact_source, compact_destination;
    if (!source || !destination) return 0;
    if (source == destination) return word & source;
    while (!(source & (1u << source_shift))) ++source_shift;
    while (!(destination & (1u << destination_shift))) ++destination_shift;
    compact_source = source >> source_shift;
    compact_destination = destination >> destination_shift;
    if (!(compact_source & (compact_source + 1u)) &&
            !(compact_destination & (compact_destination + 1u)))
        return (((word & source) >> source_shift) << destination_shift) & destination;
    while (source && destination) {
        uint32_t from = source & (0u - source);
        uint32_t to = destination & (0u - destination);
        if (word & from) result |= to;
        source &= source - 1u;
        destination &= destination - 1u;
    }
    return result;
}

static __inline uint32_t ddraw_depth_repack_word(uint32_t word,
        uint32_t source_depth, uint32_t source_stencil,
        uint32_t destination_depth, uint32_t destination_stencil)
{
    if (source_depth == destination_depth && source_stencil == destination_stencil)
        return word;
    return ddraw_depth_move_field(word, source_depth, destination_depth) |
        ddraw_depth_move_field(word, source_stencil, destination_stencil) |
        ddraw_depth_move_field(word, ~(source_depth | source_stencil),
                ~(destination_depth | destination_stencil));
}
#endif
