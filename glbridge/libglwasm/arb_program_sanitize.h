#ifndef V86GL_ARB_PROGRAM_SANITIZE_H
#define V86GL_ARB_PROGRAM_SANITIZE_H

#include <stddef.h>

/* ARB assembly permits identifiers that are reserved by the GLSL emitted by
 * gl4es.  Return an allocated, NUL-terminated copy with those identifiers
 * renamed.  The caller owns *output. */
int v86gl_sanitize_arb_program(const char* input, size_t length,
                               char** output, size_t* output_length);

#endif
