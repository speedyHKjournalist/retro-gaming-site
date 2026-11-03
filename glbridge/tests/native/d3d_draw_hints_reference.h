/* Independent draw input/geometry facts; source SHA-256: 22a3cc2cafdaaf6fd267a4c954e4a35d9ad4c24b8d48863826e85e96c6c5f556 */
struct DrawHintCase {int32_t base;uint32_t first,count,start,primitives,left,right,mode;};
static const struct DrawHintCase draw_cases[] = {
    {0, 0u, 6u, 0u, 2u, 4294901760u, 4278255360u, 0u},
    {0, 0u, 0u, 0u, 2u, 4294901760u, 4278255360u, 0u},
    {0, 4294967295u, 2u, 0u, 2u, 4294901760u, 4278255360u, 0u},
    {0, 123u, 1u, 0u, 2u, 4294901760u, 4278255360u, 0u},
    {3, 0u, 0u, 0u, 1u, 4279242768u, 4278255360u, 0u},
    {-3, 4294967295u, 0u, 3u, 1u, 4294901760u, 4279242768u, 0u},
    {-1, 0u, 0u, 1u, 1u, 4294901760u, 4279242768u, 0u},
    {(-2147483647 - 1), 4294967295u, 0u, 4294967295u, 0u, 4279242768u, 4279242768u, 1u},
    {0, 0u, 0u, 3u, 1u, 4279242768u, 4278255360u, 2u},
};
