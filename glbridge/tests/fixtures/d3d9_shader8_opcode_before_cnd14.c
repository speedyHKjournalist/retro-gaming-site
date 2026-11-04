static BOOL shader8_opcode_supported(WORD opcode, BOOL is_pixel_shader,
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
