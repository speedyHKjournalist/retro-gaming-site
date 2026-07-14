#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../libglwasm/arb_program_sanitize.h"

#ifdef V86GL_WITH_GL4ES_CONVERTER
extern char* gl4es_convertARB(const char* code, int vertex,
                              char** error_message, int* error_position);
#endif

static int check(const char* input, const char* expected) {
    char* output = NULL;
    size_t output_length = 0;
    int ok = v86gl_sanitize_arb_program(input, strlen(input),
                                        &output, &output_length);

    if (!ok || !output || output_length != strlen(expected) ||
        memcmp(output, expected, output_length + 1u)) {
        fprintf(stderr, "sanitize mismatch\ninput:    %s\nexpected: %s\nactual:   %s\n",
                input, expected, output ? output : "(null)");
        free(output);
        return 0;
    }
    free(output);
    return 1;
}

#ifdef V86GL_WITH_GL4ES_CONVERTER
static int check_gl4es_conversion(void) {
    static const char input[] =
        "!!ARBfp1.0\nPARAM const = {1, 0, 0, 1};\n"
        "MOV result.color, const;\nEND";
    char* sanitized = NULL;
    size_t sanitized_length = 0;
    char* converted;
    char* error_message = NULL;
    int error_position = -1;

    if (!v86gl_sanitize_arb_program(input, strlen(input),
                                    &sanitized, &sanitized_length)) {
        return 0;
    }
    converted = gl4es_convertARB(sanitized, 0,
                                 &error_message, &error_position);
    if (!converted || error_position != -1 ||
        !strstr(converted, "_v86gl_const") ||
        strstr(converted, "vec4 const")) {
        fprintf(stderr, "gl4es conversion failed at %d: %s\n%s\n",
                error_position, error_message ? error_message : "(none)",
                converted ? converted : "(null)");
        free(sanitized);
        free(converted);
        free(error_message);
        return 0;
    }
    free(sanitized);
    free(converted);
    free(error_message);
    (void)sanitized_length;
    return 1;
}
#endif

int main(void) {
    if (!check("!!ARBfp1.0\nPARAM const = {1, 0, 0, 1};\nMOV result.color, const;\nEND",
               "!!ARBfp1.0\nPARAM _v86gl_const = {1, 0, 0, 1};\nMOV result.color, _v86gl_const;\nEND")) {
        return 1;
    }
    if (!check("!!ARBvp1.0\nTEMP half, value$0;\nMOV result.position, value$0;\nEND",
               "!!ARBvp1.0\nTEMP _v86gl_half, _v86gl_value_0;\nMOV result.position, _v86gl_value_0;\nEND")) {
        return 1;
    }
    if (!check("!!ARBfp1.0\nTEMP R0;\nMOV result.color, fragment.color;\nEND",
               "!!ARBfp1.0\nTEMP R0;\nMOV result.color, fragment.color;\nEND")) {
        return 1;
    }
#ifdef V86GL_WITH_GL4ES_CONVERTER
    if (!check_gl4es_conversion()) {
        return 1;
    }
#endif
    puts("arb_program_sanitize_test: ok");
    return 0;
}
