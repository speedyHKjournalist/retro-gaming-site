/* Actual SDK8 shader validation,sitefcac34e. */
/* ---- Stage 6: D3D8 shader model 1.x bytecode validation ---- */

static BOOL shader_regtype_valid(DWORD register_type, BOOL is_pixel_shader)
{
    switch (register_type) {
    case D3DSPR_TEMP:
    case D3DSPR_INPUT:
    case D3DSPR_CONST:
    case D3DSPR_ADDR: /* == D3DSPR_TEXTURE; d3d8types.h aliases the value. */
        return TRUE;
    case D3DSPR_RASTOUT:
    case D3DSPR_ATTROUT:
    case D3DSPR_TEXCRDOUT:
        return !is_pixel_shader;
    default:
        return FALSE;
    }
}

/*
 * Returns FALSE for any opcode outside the supported instruction set for this
 * shader type/version. The set is the host translator's, not a smaller one of
 * this DLL's own: a legal instruction refused here is a shader the backend
 * could have run, and the app has no way to tell that apart from a driver
 * with no shader support at all. What stays refused is what the host also
 * refuses (`texdepth`, `texm3x2depth`, `texm3x3`) plus anything malformed, on
 * the parser safety rule "never guess at semantics for an instruction you do
 * not implement". On success,
 * *operand_words is the number of DWORD parameter tokens following the
 * opcode token (D3DSIO_DEF's trailing four are raw float32 immediates, not
 * register-encoded operands, but are still counted here).
 */
static BOOL shader_opcode_supported(WORD opcode, BOOL is_pixel_shader,
        UINT minor, UINT *operand_words)
{
    switch (opcode) {
    case D3DSIO_NOP:
        *operand_words = 0; return TRUE;
    case D3DSIO_MOV:
        *operand_words = 2; return TRUE;
    case D3DSIO_ADD:
    case D3DSIO_SUB:
    case D3DSIO_MUL:
    case D3DSIO_MIN:
    case D3DSIO_MAX:
    case D3DSIO_DP3:
    case D3DSIO_DP4:
        *operand_words = 3; return TRUE;
    case D3DSIO_MAD:
        *operand_words = 4; return TRUE;
    case D3DSIO_RCP:
    case D3DSIO_RSQ:
    case D3DSIO_EXP:
    case D3DSIO_LOG:
    case D3DSIO_LIT:
    case D3DSIO_FRC:
    case D3DSIO_EXPP:
    case D3DSIO_LOGP:
        if (is_pixel_shader) return FALSE;
        *operand_words = 2; return TRUE;
    case D3DSIO_SLT:
    case D3DSIO_SGE:
    case D3DSIO_DST:
        if (is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    case D3DSIO_DEF:
        *operand_words = 5; return TRUE;
    case D3DSIO_M4x4:
    case D3DSIO_M4x3:
    case D3DSIO_M3x4:
    case D3DSIO_M3x3:
    case D3DSIO_M3x2:
        /* dst, the source vector, and the first row of the matrix. These are
         * how every vs_1_x transform is spelled -- 3DMark 2001 opens with
         * `m4x4 oPos, v0, c0` -- and the host translator expands each one into
         * consecutive dot products. Refusing them refused vertex shaders as a
         * feature. */
        if (is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    case D3DSIO_LRP:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_CND:
        if (!is_pixel_shader || minor > 3u) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_CMP:
        if (!is_pixel_shader || minor < 2u) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_TEXCOORD:
        /* ps_1_4 renamed this to `texcrd dst, src` and gave it a source
         * operand; 1.1-1.3 `texcoord dst` has none. Reading the wrong number
         * of words here does not just mis-validate one instruction, it
         * desyncs the walk over everything after it. */
        if (!is_pixel_shader) return FALSE;
        *operand_words = (minor >= 4u) ? 2u : 1u; return TRUE;
    case D3DSIO_TEX:
        if (!is_pixel_shader) return FALSE;
        *operand_words = (minor >= 4u) ? 2u : 1u; return TRUE;
    case D3DSIO_TEXKILL:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 1; return TRUE;
    case D3DSIO_PHASE:
        if (!is_pixel_shader || minor != 4u) return FALSE;
        *operand_words = 0; return TRUE;
    case D3DSIO_BEM:
        /* ps_1_4's arithmetic form of the texbem displacement: `bem dst.rg,
         * src0, src1` applies the destination stage's D3DTSS_BUMPENVMAT*
         * matrix without sampling. 1.4-only -- earlier versions have only the
         * texture-addressing spelling. 3DMark 2001's Advanced Pixel Shader
         * test is a ps_1_4 shader built around it, so refusing it failed that
         * test at CreatePixelShader. */
        if (!is_pixel_shader || minor != 4u) return FALSE;
        *operand_words = 3; return TRUE;
    /*
     * The ps_1_x texture-addressing family. The host translates all of these
     * (see the TEXBEM/TEXM3x* cases in d3d9_shader_pipeline.js's emit()), so
     * refusing them here hid environment bump mapping behind a validator that
     * had never been updated to match. TEXDEPTH, TEXM3x2DEPTH and TEXM3x3 stay
     * refused -- those are the three the host itself refuses -- and TEXM3x3DIFF
     * was never implemented by any hardware.
     */
    case D3DSIO_TEXBEM:
    case D3DSIO_TEXBEML:
    case D3DSIO_TEXREG2AR:
    case D3DSIO_TEXREG2GB:
    case D3DSIO_TEXREG2RGB:
    case D3DSIO_TEXDP3:
    case D3DSIO_TEXDP3TEX:
    case D3DSIO_TEXM3x2PAD:
    case D3DSIO_TEXM3x2TEX:
    case D3DSIO_TEXM3x3PAD:
    case D3DSIO_TEXM3x3TEX:
    case D3DSIO_TEXM3x3VSPEC:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 2; return TRUE;
    case D3DSIO_TEXM3x3SPEC:
        /* The one that also takes the eye-ray constant. */
        if (!is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    default:
        return FALSE;
    }
}

/*
 * Walk one D3D8 SM1.x token stream (the app-supplied pFunction, starting
 * right after the version token) until D3DSIO_END. Rejects anything that
 * would read past `max_tokens`, any opcode outside shader_opcode_supported,
 * and any register-type field outside the recognized D3DSPR_* set, so a
 * corrupt or hostile token stream can never desync the parser or run
 * unbounded. On success *body_token_count is the token count from index 0
 * through (but excluding) the END token.
 */
static BOOL validate_shader_body(const DWORD *tokens, UINT max_tokens,
        BOOL is_pixel_shader, UINT minor, UINT *body_token_count)
{
    UINT offset = 0;
    while (offset < max_tokens) {
        DWORD token = tokens[offset];
        WORD opcode;
        UINT operand_words;

        if (token == (DWORD)D3DVS_END()) {
            *body_token_count = offset;
            return TRUE;
        }
        opcode = (WORD)(token & D3DSI_OPCODE_MASK);
        if (opcode == D3DSIO_COMMENT) {
            UINT comment_words = (UINT)((token & D3DSI_COMMENTSIZE_MASK)
                    >> D3DSI_COMMENTSIZE_SHIFT);
            if (offset + 1u + comment_words > max_tokens) {
                D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                        "offset=%u opcode=%04X reason=truncated-comment",
                        is_pixel_shader ? "pixel" : "vertex", minor, offset,
                        (UINT)opcode);
                return FALSE;
            }
            offset += 1u + comment_words;
            continue;
        }
        if (!shader_opcode_supported(opcode, is_pixel_shader, minor,
                &operand_words)) {
            D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
                    "opcode=%04X reason=unsupported-opcode",
                    is_pixel_shader ? "pixel" : "vertex", minor, offset,
                    (UINT)opcode);
            return FALSE;
        }
        if (offset + 1u + operand_words > max_tokens) {
            D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
                    "opcode=%04X reason=truncated-operands",
                    is_pixel_shader ? "pixel" : "vertex", minor, offset,
                    (UINT)opcode);
            return FALSE;
        }
        if (opcode != D3DSIO_NOP && opcode != D3DSIO_PHASE) {
            DWORD dst_regtype = tokens[offset + 1u] & D3DSP_REGTYPE_MASK;
            if (!shader_regtype_valid(dst_regtype, is_pixel_shader)) {
                D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                        "offset=%u opcode=%04X reason=invalid-dst-regtype "
                        "regtype=%08lX",
                        is_pixel_shader ? "pixel" : "vertex", minor, offset,
                        (UINT)opcode, dst_regtype);
                return FALSE;
            }
        }
        if (opcode != D3DSIO_DEF && opcode != D3DSIO_NOP
                && opcode != D3DSIO_PHASE) {
            UINT src_count = operand_words - 1u;
            UINT src;
            for (src = 0; src < src_count; ++src) {
                DWORD src_regtype =
                        tokens[offset + 2u + src] & D3DSP_REGTYPE_MASK;
                if (!shader_regtype_valid(src_regtype, is_pixel_shader)) {
                    D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                            "offset=%u opcode=%04X reason=invalid-src-regtype "
                            "source=%u regtype=%08lX",
                            is_pixel_shader ? "pixel" : "vertex", minor,
                            offset, (UINT)opcode, src, src_regtype);
                    return FALSE;
                }
            }
        }
        offset += 1u + operand_words;
    }
    D8WG_TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
            "reason=missing-end", is_pixel_shader ? "pixel" : "vertex",
            minor, offset);
    return FALSE; /* ran past max_tokens without ever finding D3DSIO_END */
}

