#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

GL4ES_ROOT="${GL4ES_ROOT:-/Users/renruisi/Desktop/Code/gl4es}"
GL4ES_LIB="${GL4ES_ROOT}/lib/libGL.a"

if [ ! -f "${GL4ES_LIB}" ]; then
  echo "Missing gl4es static library: ${GL4ES_LIB}" >&2
  exit 1
fi

emcc gl4es_bridge.c \
  -I"${GL4ES_ROOT}/include" \
  "${GL4ES_LIB}" \
  -O2 \
  -sENVIRONMENT=web \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createV86GL4ES \
  -sALLOW_MEMORY_GROWTH=1 \
  -sFULL_ES2=1 \
  -sMIN_WEBGL_VERSION=1 \
  -sMAX_WEBGL_VERSION=2 \
  -sGL_ENABLE_GET_PROC_ADDRESS=1 \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -sEXPORTED_FUNCTIONS='[
    "_v86glMakeCurrent",
    "_v86glResize",
    "_v86glReleaseCurrent",
    "_v86gl_glViewport",
    "_v86gl_glClearColor",
    "_v86gl_glClear",
    "_v86gl_glBegin",
    "_v86gl_glEnd",
    "_v86gl_glColor4f",
    "_v86gl_glVertex3f",
    "_v86gl_glFlush",
    "_v86gl_glFinish",
    "_v86gl_glMatrixMode",
    "_v86gl_glLoadIdentity",
    "_v86gl_glFrustum",
    "_v86gl_glOrtho",
    "_v86gl_glTranslatef",
    "_v86gl_glRotatef",
    "_v86gl_glScalef",
    "_v86gl_glPushMatrix",
    "_v86gl_glPopMatrix",
    "_v86gl_glEnable",
    "_v86gl_glDisable",
    "_v86gl_glDepthFunc",
    "_v86gl_glClearDepth",
    "_v86gl_glShadeModel",
    "_v86gl_glCullFace",
    "_v86gl_glFrontFace"
  ]' \
  -o gl4es.js

echo "Generated gl4es.js and gl4es.wasm"
