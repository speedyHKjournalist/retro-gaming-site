#include <stdio.h>
#include <string.h>

#include "init.h"
#include "../glx/hardext.h"
#include "fpe_shader.h"

/* fpe_shader.c reads these runtime capability structures while generating
 * source.  This focused test supplies only the fields needed by the default
 * vertex shader path, just like arb_program_parser_test.c links only the ARB
 * converter sources it exercises. */
globals4es_t globals4es = {0};
hardext_t hardext = {0};

static int failures;

static void expect_contains(const char *label, const char *source,
                            const char *needle) {
    if (!source || !strstr(source, needle)) {
        fprintf(stderr, "%s: generated source does not contain `%s`\n",
                label, needle);
        ++failures;
    }
}

static void expect_not_contains(const char *label, const char *source,
                                const char *needle) {
    if (source && strstr(source, needle)) {
        fprintf(stderr, "%s: generated source unexpectedly contains `%s`\n",
                label, needle);
        ++failures;
    }
}

static void test_fragment_only_program_with_volume_texture(void) {
    shaderconv_need_t need = {0};
    fpe_state_t state = {0};

    /* An ARB fragment program using fragment.texcoord[0] records texture unit
     * zero in need_texs.  glEnable(GL_TEXTURE_3D) independently records the
     * fixed-pipeline texture type as FPE_TEX_3D.  The generated default vertex
     * shader must satisfy the custom fragment shader's vec4 gl_TexCoord
     * contract, not shrink the varying according to the enabled target. */
    need.need_texcoord = 0;
    need.need_texs = 1;
    state.texture[0].textype = FPE_TEX_3D;

    const char *source = *fpe_VertexShader(&need, &state);
    expect_contains("volume varying type", source,
                    "varying highp vec4 _gl4es_TexCoord_0;");
    expect_contains("volume coordinate components", source,
                    "_gl4es_TexCoord_0 = gl_MultiTexCoord0.stpq;");
    expect_not_contains("truncated volume coordinate", source,
                        "_gl4es_TexCoord_0 = gl_MultiTexCoord0.st;");
}

int main(void) {
    hardext.maxtex = 8;
    hardext.maxlights = 8;
    hardext.maxplanes = 6;
    hardext.highp = 1;

    test_fragment_only_program_with_volume_texture();
    if (failures) {
        fprintf(stderr, "FPE volume vertex shader tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("FPE volume vertex shader tests: PASS");
    return 0;
}
