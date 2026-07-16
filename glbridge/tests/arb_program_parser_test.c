#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arbconverter.h"

static int failures;

static void expect_int(const char *label, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %d, expected %d\n", label, actual, expected);
        ++failures;
    }
}

static void expect_contains(const char *label, const char *source, const char *needle) {
    if (!source || !strstr(source, needle)) {
        fprintf(stderr, "%s: generated source does not contain %s\n", label, needle);
        ++failures;
    }
}

static char *convert(const char *source, int vertex, arb_program_stats_t *stats) {
    char *error = NULL;
    int error_position = -1;
    char *result = gl4es_convertARBWithStats(source, vertex, &error,
                                              &error_position, stats);
    if (!result) {
        fprintf(stderr, "conversion failed at %d: %s\n", error_position,
                error ? error : "unknown error");
        ++failures;
    }
    free(error);
    return result;
}

static void test_vertex_state(void) {
    static const char source[] =
        "!!ARBvp1.0\n"
        "PARAM fog = state.fog.params;\n"
        "PARAM texgen = state.texgen[1].eye.s;\n"
        "PARAM clip = state.clip[2].plane;\n"
        "PARAM point = state.point.size;\n"
        "PARAM attenuation = state.point.attenuation;\n"
        "TEMP work;\n"
        "MOV work, fog;\n"
        "ADD work, work, texgen;\n"
        "ADD work, work, clip;\n"
        "ADD result.position, work, point;\n"
        "MOV result.pointsize.x, attenuation.x;\n"
        "END\n";
    arb_program_stats_t stats;
    char *glsl = convert(source, 1, &stats);
    expect_contains("vertex texgen", glsl, "gl_EyePlaneS[1]");
    expect_contains("vertex fog", glsl, "gl_Fog.density");
    expect_contains("vertex clip", glsl, "gl_ClipPlane[2]");
    expect_contains("vertex point", glsl, "gl_Point.fadeThresholdSize");
    expect_contains("vertex point result", glsl, "gl_PointSize = gl4es_PointSizeTemp.x");
    expect_int("vertex instructions", stats.instructions, 5);
    expect_int("vertex ALU", stats.alu_instructions, 5);
    expect_int("vertex temporaries", stats.temporaries, 1);
    expect_int("vertex parameters", stats.parameters, 5);
    expect_int("vertex texture indirections", stats.tex_indirections, 0);
    free(glsl);
}

static void test_fragment_state_and_indirections(void) {
    static const char source[] =
        "!!ARBfp1.0\n"
        "PARAM fog = state.fog.color;\n"
        "PARAM env = state.texenv[1].color;\n"
        "PARAM depth = state.depth.range;\n"
        "TEMP coord, sample;\n"
        "MUL coord, fragment.texcoord[0], depth;\n"
        "TEX sample, coord, texture[0], 2D;\n"
        "TEX sample, sample, texture[1], 2D;\n"
        "ADD result.color, sample, fog;\n"
        "END\n";
    arb_program_stats_t stats;
    char *glsl = convert(source, 0, &stats);
    expect_contains("fragment fog", glsl, "gl_Fog.color");
    expect_contains("fragment texenv", glsl, "gl_TextureEnvColor[1]");
    expect_contains("fragment depth range", glsl, "gl_DepthRange.diff");
    expect_int("fragment instructions", stats.instructions, 4);
    expect_int("fragment ALU", stats.alu_instructions, 2);
    expect_int("fragment texture instructions", stats.tex_instructions, 2);
    expect_int("fragment texture indirections", stats.tex_indirections, 3);
    expect_int("fragment temporaries", stats.temporaries, 2);
    expect_int("fragment parameters", stats.parameters, 3);
    expect_int("fragment attributes", stats.attributes, 1);
    free(glsl);
}

static void test_fragment_volume_texture(void) {
    static const char source[] =
        "!!ARBfp1.0\n"
        "TEX result.color, fragment.texcoord[0], texture[0], 3D;\n"
        "END\n";
    arb_program_stats_t stats;
    char *glsl = convert(source, 0, &stats);
    expect_contains("fragment volume sampler", glsl, "gl_Sampler3D_0");
    expect_contains("fragment volume function", glsl, "texture3D(");
    expect_contains("fragment volume coordinate", glsl, "gl_TexCoord[0].xyz");
    expect_int("fragment volume instructions", stats.instructions, 1);
    expect_int("fragment volume texture instructions", stats.tex_instructions, 1);
    free(glsl);
}

int main(void) {
    test_vertex_state();
    test_fragment_state_and_indirections();
    test_fragment_volume_texture();
    if (failures) {
        fprintf(stderr, "ARB parser tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("ARB parser tests: PASS");
    return 0;
}
