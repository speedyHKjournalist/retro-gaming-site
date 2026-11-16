/* Actual previous implementation from site 8509914, SDK9. Positive oracle only. */
static HRESULT WINAPI d3d_check_device_format_before9(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    BOOL ok = FALSE;
    HRESULT result;
    (void)iface; (void)adapter_format;
    /* 3DMark05's 0x0046F240 probe stops looking for filtered depth textures
     * when '-PCF' succeeds. Our depth comparison sampler supports PCF, so
     * NOTAVAILABLE is the expected answer here. Keep this query out of the
     * generic refusal warning; never advertise it as a creatable format. */
    if (!adapter && type == D3DDEVTYPE_HAL && !usage
            && resource_type == D3DRTYPE_TEXTURE && format == D9FOURCC_NO_PCF) {
        TRACE("PROBE CheckDeviceFormat -PCF -> %08lX (expected negative probe)",
                (DWORD)D3DERR_NOTAVAILABLE);
        HOSTLOG_INFO("CheckDeviceFormat -PCF: negative capability probe; "
                "depth comparison filtering is available");
        return D3DERR_NOTAVAILABLE;
    }
    if (!adapter && type == D3DDEVTYPE_HAL) {
        /* D3DUSAGE_QUERY_* bits exist only in this call and never reach a
         * Create*, so they are answered here and masked off before the shared
         * creation predicate sees the usage. D3DUSAGE_AUTOGENMIPMAP is masked
         * for the same reason from the other direction: the create paths
         * accept a valid 2D/cube request even if generation is unavailable.
         * Only the query returns D3DOK_NOAUTOGEN; creation returns D3D_OK
         * with a single accessible level in either case. */
        const DWORD query_bits = D3DUSAGE_QUERY_LEGACYBUMPMAP
                | D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_FILTER
                | D3DUSAGE_QUERY_SRGBWRITE
                | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING
                | D3DUSAGE_QUERY_VERTEXTEXTURE | D3DUSAGE_QUERY_WRAPANDMIP;
        /* Behaviours this backend does not implement. Unlike the create paths,
         * the query may safely refuse them: an app told no here picks something
         * else, whereas an app refused a create has nowhere to go. Legacy
         * bump-map formats are exposed as ordinary sampled textures rather than
         * through the fixed-function bump pipeline; displacement mapping and
         * N-patch tessellation do not exist here at all.
         *
         * D3DUSAGE_AUTOGENMIPMAP is no longer among them: the host allocates
         * the chain behind the app's single visible level and refills it by
         * blitting each level from the one above whenever level 0 changes.
         * Volume textures are the exception, and D3D9 does not support
         * automatic generation for those either. */
        /* D3DUSAGE_DMAP stays unimplemented: displacement mapping samples a
         * texture *during* tessellation, and nothing here can. D3DUSAGE_
         * NPATCHES is gone from this list -- N-patch tessellation is
         * implemented, and a buffer created with the usage is otherwise an
         * ordinary vertex buffer. */
        const DWORD unimplemented = D3DUSAGE_QUERY_LEGACYBUMPMAP
                | D3DUSAGE_DMAP
                | (((resource_type != D3DRTYPE_TEXTURE && resource_type != D3DRTYPE_CUBETEXTURE)
                        || (usage & D3DUSAGE_DYNAMIC)) ? D3DUSAGE_AUTOGENMIPMAP : 0u);
        const DWORD creation_usage = usage
                & ~(query_bits | D3DUSAGE_AUTOGENMIPMAP
                    | D3DUSAGE_SOFTWAREPROCESSING);
        BOOL renderable = supported_render_target_format(format);
        if (!(usage & unimplemented)
                && texture_create_supported(resource_type, creation_usage,
                        format, D3DPOOL_DEFAULT)
                && d9wg_format_query_supported(resource_type, usage, format,
                        D9WG_HOST_FEATURE_MASK, shader_model_version(), renderable)) {
            uint32_t host_features = 0;
            uint32_t info = d9wg_format_query_info(format);
            if ((info & 32u) || ((info & 4u)
                    && (usage & (D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)))) {
                host_features = runtime_host_features();
            }
            ok = d9wg_format_query_supported(resource_type, usage, format,
                    host_features, shader_model_version(), renderable);
        }
    }
    result = ok ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (ok && (usage & D3DUSAGE_AUTOGENMIPMAP)
            && !d9wg_autogen_mips_supported(resource_type, format))
        result = D3DOK_NOAUTOGEN;
    TRACE("%s CheckDeviceFormat adapter=%lu type=%lu adapter_fmt=%08lX "
            "usage=%08lX resource_type=%lu format=%08lX -> %08lX",
            SUCCEEDED(result) ? "OK" : "FAIL", adapter, (DWORD)type,
            (DWORD)adapter_format, usage, (DWORD)resource_type,
            (DWORD)format, (DWORD)result);
    /*
     * D3DERR_NOTAVAILABLE is a legitimate answer, not a refusal, which is
     * exactly why it needs reporting: an app that asks whether it may use a
     * format and is told no does not fail, it quietly does without -- loads no
     * texture, draws no model, logs nothing. That is indistinguishable from
     * "the app never wanted to draw it", and the difference is the whole
     * question when content is missing with a clean command stream. Deduped by
     * text, so each distinct usage/type/format triple costs one line however
     * many times it is probed.
     */
    if (!ok)
        HOSTLOG_REFUSED("CheckDeviceFormat said no: format=%08lX usage=%08lX "
                "resource_type=%lu (an app told no here silently does "
                "without)", (DWORD)format, usage, (DWORD)resource_type);
    return result;
}
