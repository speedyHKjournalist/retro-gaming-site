/* Frozen XP HRESULT and independent Wine access-policy facts; not production metadata.
 * Source SHA-256: 064e05cf4e85e2229b44e8c3a8bdd3036d4fccb50e7aedaab51e598996a60841 */
struct BufferLockReferenceContext {uint32_t pool,usage,effective[8];};
static const struct BufferLockReferenceContext contexts[] = {
    {0u, 0u, {0u, 16u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {0u, 8u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {0u, 512u, {0u, 16u, 4096u, 4096u, 8192u, 8192u, 4096u, 4096u}},
    {0u, 520u, {0u, 0u, 4096u, 4096u, 8192u, 8192u, 4096u, 4096u}},
    {1u, 0u, {0u, 16u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {1u, 8u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {1u, 512u, {0u, 16u, 4096u, 4096u, 8192u, 8192u, 4096u, 4096u}},
    {1u, 520u, {0u, 0u, 4096u, 4096u, 8192u, 8192u, 4096u, 4096u}},
    {2u, 0u, {0u, 16u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {2u, 8u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {2u, 512u, {0u, 16u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {2u, 520u, {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
};
static const uint32_t flags[] = {0u, 16u, 4096u, 4112u, 8192u, 8208u, 12288u, 12304u};
static const uint32_t advisory[] = {0u, 1u, 2048u, 16384u, 32768u, 2147483648u, 4294954991u};
static const uint32_t nativeFlags[] = {16u, 8192u, 4096u, 12288u, 4112u, 8208u, 3735928559u};
