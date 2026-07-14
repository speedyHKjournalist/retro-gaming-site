#include "arb_program_sanitize.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int v86gl_identifier_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int v86gl_identifier_char(unsigned char c) {
    return v86gl_identifier_start(c) || (c >= '0' && c <= '9') || c == '$';
}

static int v86gl_reserved_identifier(const char* text, size_t length) {
    static const char* const reserved[] = {
        "asm", "attribute", "bool", "break", "case", "cast", "centroid",
        "class", "common", "const", "continue", "default", "discard", "do",
        "double", "dvec2", "dvec3", "dvec4", "else", "enum", "extern",
        "external", "filter", "fixed", "flat", "float", "for", "fvec2",
        "fvec3", "fvec4", "goto", "half", "highp", "hvec2", "hvec3",
        "hvec4", "if", "in", "inline", "inout", "input", "int", "interface",
        "invariant", "layout", "long", "lowp", "mat2", "mat2x2", "mat2x3",
        "mat2x4", "mat3", "mat3x2", "mat3x3", "mat3x4", "mat4", "mat4x2",
        "mat4x3", "mat4x4", "mediump", "namespace", "noinline",
        "noperspective", "out", "output", "partition", "public", "resource",
        "return", "row_major", "sampler1D", "sampler1DShadow", "sampler2D",
        "sampler2DShadow", "sampler3D", "samplerCube", "short", "sizeof",
        "smooth", "static", "struct", "subroutine", "superp", "switch",
        "template", "this", "true", "typedef", "uint", "uniform", "union",
        "unsigned", "using", "varying", "vec2", "vec3", "vec4", "void",
        "volatile", "while"
    };
    size_t i;

    if (length >= 3 && text[0] == 'g' && text[1] == 'l' && text[2] == '_') {
        return 1;
    }
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (strlen(reserved[i]) == length && !memcmp(text, reserved[i], length)) {
            return 1;
        }
    }
    return 0;
}

int v86gl_sanitize_arb_program(const char* input, size_t length,
                               char** output, size_t* output_length) {
    static const char prefix[] = "_v86gl_";
    size_t extra = 0;
    size_t input_at = 0;
    size_t output_at = 0;
    char* copy;

    if (!output || !output_length || (!input && length)) {
        return 0;
    }
    *output = NULL;
    *output_length = 0;

    while (input_at < length) {
        size_t start;
        size_t token_length;
        size_t i;
        int rename;

        if (!v86gl_identifier_start((unsigned char)input[input_at])) {
            input_at++;
            continue;
        }
        start = input_at++;
        while (input_at < length &&
               v86gl_identifier_char((unsigned char)input[input_at])) {
            input_at++;
        }
        token_length = input_at - start;
        rename = v86gl_reserved_identifier(input + start, token_length);
        for (i = 0; i < token_length; i++) {
            if (input[start + i] == '$') {
                rename = 1;
                break;
            }
        }
        if (rename) {
            if (extra > SIZE_MAX - (sizeof(prefix) - 1u)) {
                return 0;
            }
            extra += sizeof(prefix) - 1u;
        }
    }

    if (length == SIZE_MAX || extra > SIZE_MAX - length - 1u) {
        return 0;
    }
    copy = (char*)malloc(length + extra + 1u);
    if (!copy) {
        return 0;
    }

    input_at = 0;
    while (input_at < length) {
        size_t start;
        size_t token_length;
        size_t i;
        int rename;

        if (!v86gl_identifier_start((unsigned char)input[input_at])) {
            copy[output_at++] = input[input_at++];
            continue;
        }
        start = input_at++;
        while (input_at < length &&
               v86gl_identifier_char((unsigned char)input[input_at])) {
            input_at++;
        }
        token_length = input_at - start;
        rename = v86gl_reserved_identifier(input + start, token_length);
        for (i = 0; i < token_length; i++) {
            if (input[start + i] == '$') {
                rename = 1;
                break;
            }
        }
        if (rename) {
            memcpy(copy + output_at, prefix, sizeof(prefix) - 1u);
            output_at += sizeof(prefix) - 1u;
        }
        for (i = 0; i < token_length; i++) {
            char c = input[start + i];
            copy[output_at++] = c == '$' ? '_' : c;
        }
    }

    copy[output_at] = '\0';
    *output = copy;
    *output_length = output_at;
    return 1;
}
