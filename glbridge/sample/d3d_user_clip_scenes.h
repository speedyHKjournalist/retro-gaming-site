/* Shared procedural geometry for D3D7/8/9 command capture. Plane offsets
 * deliberately avoid pixel centers, so raster setup and fragment clipping
 * can be compared without an ambiguous boundary sample. */
#define USER_CLIP_TRIANGLE                                                     \
  {{-.8f, -.7f, .2f, 0xffffffffu},                                             \
   {.8f, -.7f, .8f, 0xffffffffu},                                              \
   {0, .8f, .5f, 0xffffffffu}}
static const struct Scene scenes[] = {
    {USER_CLIP_TRIANGLE, .25f, .75f, FALSE},
    {USER_CLIP_TRIANGLE, .25f, .75f, FALSE},
    {USER_CLIP_TRIANGLE, 0, 0, FALSE},
    {USER_CLIP_TRIANGLE, .25f, .75f, FALSE},
    {USER_CLIP_TRIANGLE, .25f, .75f, TRUE},
    {USER_CLIP_TRIANGLE, .25f, .75f, FALSE},
};
static const float user_planes[6][4] = {{1, 0, 0, .113f},   {0, 1, 0, -.173f},
                                        {0, 0, 1, -.4315f}, {-1, 0, 0, .237f},
                                        {0, -1, 0, -.143f}, {0, 0, -1, .681f}};
#undef USER_CLIP_TRIANGLE
