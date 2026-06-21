// v86 PCI DMA -> OpenGL bridge.
// Guest opengl32.dll packs GL calls, this file decodes them and forwards them
// to a gl4es wasm/js module. gl4es owns the OpenGL fixed pipeline -> WebGL
// translation; this file only does command parsing, canvas placement, and calls.

(function(global) {
    "use strict";

    const OP_MAKE_CURRENT = 1;
    const OP_VIEWPORT = 2;
    const OP_CLEAR_COLOR = 3;
    const OP_CLEAR = 4;
    const OP_BEGIN = 5;
    const OP_END = 6;
    const OP_COLOR4F = 7;
    const OP_VERTEX3F = 8;
    const OP_PRESENT = 9;
    const OP_RELEASE_CURRENT = 10;
    const OP_GL_CALL = 20;
    const OP_GL_FRAME = 21;
    const OP_GL_CHUNK = 22;
    const OP_GL_BATCH = 23;

    const CLIENT_ARRAY_MT_MAGIC = 0x544D4143;
    const V86GL_CTRL_MAKE_CURRENT = 0xFFF0;
    const V86GL_CTRL_RELEASE_CURRENT = 0xFFF1;
    const V86GL_CTRL_DESTROY_CONTEXT = 0xFFF2;
    const V86GL_EXTENDED_RECORD_SIZE = 0xFFFF;

    const GLFN_VIEWPORT = 1;
    const GLFN_CLEAR_COLOR = 2;
    const GLFN_CLEAR = 3;
    const GLFN_BEGIN = 4;
    const GLFN_END = 5;
    const GLFN_COLOR4F = 6;
    const GLFN_VERTEX3F = 7;
    const GLFN_FLUSH = 8;
    const GLFN_FINISH = 9;
    const GLFN_MATRIX_MODE = 10;
    const GLFN_LOAD_IDENTITY = 11;
    const GLFN_FRUSTUM = 12;
    const GLFN_ORTHO = 13;
    const GLFN_TRANSLATEF = 14;
    const GLFN_ROTATEF = 15;
    const GLFN_SCALEF = 16;
    const GLFN_PUSH_MATRIX = 17;
    const GLFN_POP_MATRIX = 18;
    const GLFN_ENABLE = 19;
    const GLFN_DISABLE = 20;
    const GLFN_DEPTH_FUNC = 21;
    const GLFN_CLEAR_DEPTH = 22;
    const GLFN_SHADE_MODEL = 23;
    const GLFN_CULL_FACE = 24;
    const GLFN_FRONT_FACE = 25;
    const GLFN_GEN_TEXTURES = 26;
    const GLFN_DELETE_TEXTURES = 27;
    const GLFN_BIND_TEXTURE = 28;
    const GLFN_TEX_IMAGE_2D = 29;
    const GLFN_TEX_SUB_IMAGE_2D = 30;
    const GLFN_TEX_PARAMETERI = 31;
    const GLFN_TEX_PARAMETERF = 32;
    const GLFN_PIXEL_STOREI = 33;
    const GLFN_TEX_ENVI = 34;
    const GLFN_TEX_ENVF = 35;
    const GLFN_TEX_COORD2F = 36;
    const GLFN_ENABLE_CLIENT_STATE = 37;
    const GLFN_DISABLE_CLIENT_STATE = 38;
    const GLFN_DRAW_ARRAYS = 39;
    const GLFN_DRAW_ELEMENTS = 40;
    const GLFN_BLEND_FUNC = 41;
    const GLFN_ALPHA_FUNC = 42;
    const GLFN_DEPTH_MASK = 43;
    const GLFN_COLOR_MASK = 44;
    const GLFN_SCISSOR = 45;
    const GLFN_LINE_WIDTH = 46;
    const GLFN_POLYGON_MODE = 47;
    const GLFN_ACTIVE_TEXTURE = 48;
    const GLFN_CLIENT_ACTIVE_TEXTURE = 49;
    const GLFN_MULTI_TEX_COORD4F = 50;
    const GLFN_NORMAL3F = 51;
    const GLFN_FOGF = 52;
    const GLFN_FOGI = 53;
    const GLFN_FOGFV = 54;
    const GLFN_MATERIALF = 55;
    const GLFN_MATERIALI = 56;
    const GLFN_MATERIALFV = 57;
    const GLFN_MATERIALIV = 58;
    const GLFN_TEX_ENVIV = 59;
    const GLFN_TEX_ENVFV = 60;
    const GLFN_LIGHTF = 61;
    const GLFN_LIGHTI = 62;
    const GLFN_LIGHTFV = 63;
    const GLFN_LIGHTIV = 64;
    const GLFN_LIGHT_MODELF = 65;
    const GLFN_LIGHT_MODELI = 66;
    const GLFN_LIGHT_MODELFV = 67;
    const GLFN_LIGHT_MODELIV = 68;
    const GLFN_LOAD_MATRIXF = 69;
    const GLFN_MULT_MATRIXF = 70;
    const GLFN_DEPTH_RANGE = 71;
    const GLFN_CLEAR_STENCIL = 72;
    const GLFN_STENCIL_FUNC = 73;
    const GLFN_STENCIL_MASK = 74;
    const GLFN_STENCIL_OP = 75;
    const GLFN_HINT = 76;
    const GLFN_POLYGON_OFFSET = 77;
    const GLFN_TEX_PARAMETERIV = 78;
    const GLFN_TEX_PARAMETERFV = 79;
    const GLFN_TEX_GENI = 80;
    const GLFN_TEX_GENF = 81;
    const GLFN_TEX_GENIV = 82;
    const GLFN_TEX_GENFV = 83;
    const GLFN_CLIP_PLANE = 84;
    const GLFN_COLOR_MATERIAL = 85;
    const GLFN_PUSH_ATTRIB = 86;
    const GLFN_POP_ATTRIB = 87;
    const GLFN_PUSH_CLIENT_ATTRIB = 88;
    const GLFN_POP_CLIENT_ATTRIB = 89;
    const GLFN_DRAW_BUFFER = 90;
    const GLFN_READ_BUFFER = 91;
    const GLFN_COPY_TEX_IMAGE_2D = 92;
    const GLFN_COPY_TEX_SUB_IMAGE_2D = 93;
    const GLFN_READ_PIXELS = 94;
    const GLFN_BLEND_COLOR = 95;
    const GLFN_BLEND_EQUATION = 96;
    const GLFN_BLEND_FUNC_SEPARATE = 97;
    const GLFN_SAMPLE_COVERAGE = 98;
    const GLFN_GENERATE_MIPMAP = 99;
    const GLFN_FOG_COORDF = 100;
    const GLFN_SECONDARY_COLOR3F = 101;
    const GLFN_POINT_PARAMETERF = 102;
    const GLFN_POINT_PARAMETERFV = 103;
    const GLFN_COMPRESSED_TEX_IMAGE_2D = 104;
    const GLFN_TEX_IMAGE_3D = 105;
    const GLFN_TEX_SUB_IMAGE_3D = 106;

    const V86GL_READ_PIXELS_HEADER_SIZE = 32;
    const V86GL_READ_PIXELS_STATUS_PENDING = 0;
    const V86GL_READ_PIXELS_STATUS_OK = 1;
    const V86GL_READ_PIXELS_STATUS_FAILED = 2;

    const OP_NAMES = {
        [OP_MAKE_CURRENT]: "MAKE_CURRENT",
        [OP_VIEWPORT]: "VIEWPORT",
        [OP_CLEAR_COLOR]: "CLEAR_COLOR",
        [OP_CLEAR]: "CLEAR",
        [OP_BEGIN]: "BEGIN",
        [OP_END]: "END",
        [OP_COLOR4F]: "COLOR4F",
        [OP_VERTEX3F]: "VERTEX3F",
        [OP_PRESENT]: "PRESENT",
        [OP_RELEASE_CURRENT]: "RELEASE_CURRENT",
        [OP_GL_CALL]: "GL_CALL",
        [OP_GL_FRAME]: "GL_FRAME",
        [OP_GL_CHUNK]: "GL_CHUNK",
        [OP_GL_BATCH]: "GL_BATCH",
    };

    const GLFN_NAMES = {
        [GLFN_VIEWPORT]: "glViewport",
        [GLFN_CLEAR_COLOR]: "glClearColor",
        [GLFN_CLEAR]: "glClear",
        [GLFN_BEGIN]: "glBegin",
        [GLFN_END]: "glEnd",
        [GLFN_COLOR4F]: "glColor4f",
        [GLFN_VERTEX3F]: "glVertex3f",
        [GLFN_FLUSH]: "glFlush",
        [GLFN_FINISH]: "glFinish",
        [GLFN_MATRIX_MODE]: "glMatrixMode",
        [GLFN_LOAD_IDENTITY]: "glLoadIdentity",
        [GLFN_FRUSTUM]: "glFrustum",
        [GLFN_ORTHO]: "glOrtho",
        [GLFN_TRANSLATEF]: "glTranslatef",
        [GLFN_ROTATEF]: "glRotatef",
        [GLFN_SCALEF]: "glScalef",
        [GLFN_PUSH_MATRIX]: "glPushMatrix",
        [GLFN_POP_MATRIX]: "glPopMatrix",
        [GLFN_ENABLE]: "glEnable",
        [GLFN_DISABLE]: "glDisable",
        [GLFN_DEPTH_FUNC]: "glDepthFunc",
        [GLFN_CLEAR_DEPTH]: "glClearDepth",
        [GLFN_SHADE_MODEL]: "glShadeModel",
        [GLFN_CULL_FACE]: "glCullFace",
        [GLFN_FRONT_FACE]: "glFrontFace",
        [GLFN_GEN_TEXTURES]: "glGenTextures",
        [GLFN_DELETE_TEXTURES]: "glDeleteTextures",
        [GLFN_BIND_TEXTURE]: "glBindTexture",
        [GLFN_TEX_IMAGE_2D]: "glTexImage2D",
        [GLFN_TEX_SUB_IMAGE_2D]: "glTexSubImage2D",
        [GLFN_TEX_PARAMETERI]: "glTexParameteri",
        [GLFN_TEX_PARAMETERF]: "glTexParameterf",
        [GLFN_PIXEL_STOREI]: "glPixelStorei",
        [GLFN_TEX_ENVI]: "glTexEnvi",
        [GLFN_TEX_ENVF]: "glTexEnvf",
        [GLFN_TEX_COORD2F]: "glTexCoord2f",
        [GLFN_ENABLE_CLIENT_STATE]: "glEnableClientState",
        [GLFN_DISABLE_CLIENT_STATE]: "glDisableClientState",
        [GLFN_DRAW_ARRAYS]: "glDrawArrays",
        [GLFN_DRAW_ELEMENTS]: "glDrawElements",
        [GLFN_BLEND_FUNC]: "glBlendFunc",
        [GLFN_ALPHA_FUNC]: "glAlphaFunc",
        [GLFN_DEPTH_MASK]: "glDepthMask",
        [GLFN_COLOR_MASK]: "glColorMask",
        [GLFN_SCISSOR]: "glScissor",
        [GLFN_LINE_WIDTH]: "glLineWidth",
        [GLFN_POLYGON_MODE]: "glPolygonMode",
        [GLFN_ACTIVE_TEXTURE]: "glActiveTexture",
        [GLFN_CLIENT_ACTIVE_TEXTURE]: "glClientActiveTexture",
        [GLFN_MULTI_TEX_COORD4F]: "glMultiTexCoord4f",
        [GLFN_NORMAL3F]: "glNormal3f",
        [GLFN_FOGF]: "glFogf",
        [GLFN_FOGI]: "glFogi",
        [GLFN_FOGFV]: "glFogfv",
        [GLFN_MATERIALF]: "glMaterialf",
        [GLFN_MATERIALI]: "glMateriali",
        [GLFN_MATERIALFV]: "glMaterialfv",
        [GLFN_MATERIALIV]: "glMaterialiv",
        [GLFN_TEX_ENVIV]: "glTexEnviv",
        [GLFN_TEX_ENVFV]: "glTexEnvfv",
        [GLFN_LIGHTF]: "glLightf",
        [GLFN_LIGHTI]: "glLighti",
        [GLFN_LIGHTFV]: "glLightfv",
        [GLFN_LIGHTIV]: "glLightiv",
        [GLFN_LIGHT_MODELF]: "glLightModelf",
        [GLFN_LIGHT_MODELI]: "glLightModeli",
        [GLFN_LIGHT_MODELFV]: "glLightModelfv",
        [GLFN_LIGHT_MODELIV]: "glLightModeliv",
        [GLFN_LOAD_MATRIXF]: "glLoadMatrixf",
        [GLFN_MULT_MATRIXF]: "glMultMatrixf",
        [GLFN_DEPTH_RANGE]: "glDepthRange",
        [GLFN_CLEAR_STENCIL]: "glClearStencil",
        [GLFN_STENCIL_FUNC]: "glStencilFunc",
        [GLFN_STENCIL_MASK]: "glStencilMask",
        [GLFN_STENCIL_OP]: "glStencilOp",
        [GLFN_HINT]: "glHint",
        [GLFN_POLYGON_OFFSET]: "glPolygonOffset",
        [GLFN_TEX_PARAMETERIV]: "glTexParameteriv",
        [GLFN_TEX_PARAMETERFV]: "glTexParameterfv",
        [GLFN_TEX_GENI]: "glTexGeni",
        [GLFN_TEX_GENF]: "glTexGenf",
        [GLFN_TEX_GENIV]: "glTexGeniv",
        [GLFN_TEX_GENFV]: "glTexGenfv",
        [GLFN_CLIP_PLANE]: "glClipPlane",
        [GLFN_COLOR_MATERIAL]: "glColorMaterial",
        [GLFN_PUSH_ATTRIB]: "glPushAttrib",
        [GLFN_POP_ATTRIB]: "glPopAttrib",
        [GLFN_PUSH_CLIENT_ATTRIB]: "glPushClientAttrib",
        [GLFN_POP_CLIENT_ATTRIB]: "glPopClientAttrib",
        [GLFN_DRAW_BUFFER]: "glDrawBuffer",
        [GLFN_READ_BUFFER]: "glReadBuffer",
        [GLFN_COPY_TEX_IMAGE_2D]: "glCopyTexImage2D",
        [GLFN_COPY_TEX_SUB_IMAGE_2D]: "glCopyTexSubImage2D",
        [GLFN_READ_PIXELS]: "glReadPixels",
        [GLFN_BLEND_COLOR]: "glBlendColor",
        [GLFN_BLEND_EQUATION]: "glBlendEquation",
        [GLFN_BLEND_FUNC_SEPARATE]: "glBlendFuncSeparate",
        [GLFN_SAMPLE_COVERAGE]: "glSampleCoverage",
        [GLFN_GENERATE_MIPMAP]: "glGenerateMipmap",
        [GLFN_FOG_COORDF]: "glFogCoordf",
        [GLFN_SECONDARY_COLOR3F]: "glSecondaryColor3f",
        [GLFN_POINT_PARAMETERF]: "glPointParameterf",
        [GLFN_POINT_PARAMETERFV]: "glPointParameterfv",
        [GLFN_COMPRESSED_TEX_IMAGE_2D]: "glCompressedTexImage2D",
        [GLFN_TEX_IMAGE_3D]: "glTexImage3D",
        [GLFN_TEX_SUB_IMAGE_3D]: "glTexSubImage3D",
    };

    function u16(a, o) { return a[o] | (a[o + 1] << 8); }
    function u32(a, o) { return (a[o] | (a[o + 1] << 8) | (a[o + 2] << 16) | (a[o + 3] << 24)) >>> 0; }
    function writeU32(a, o, v) {
        a[o] = v & 0xFF;
        a[o + 1] = (v >>> 8) & 0xFF;
        a[o + 2] = (v >>> 16) & 0xFF;
        a[o + 3] = v >>> 24;
    }
    function i32(a, o) { return u32(a, o) | 0; }
    function f32(a, o) {
        const b = new Uint8Array([a[o], a[o + 1], a[o + 2], a[o + 3]]);
        return new DataView(b.buffer).getFloat32(0, true);
    }
    function f64(a, o) {
        const b = new Uint8Array([
            a[o], a[o + 1], a[o + 2], a[o + 3],
            a[o + 4], a[o + 5], a[o + 6], a[o + 7],
        ]);
        return new DataView(b.buffer).getFloat64(0, true);
    }

    class Gl4esRenderer {
        constructor(canvas, module, log) {
            this.canvas = canvas;
            this.module = module;
            this.log = log;
            this.missing = Object.create(null);
        }

        makeCurrent(surface) {
            this.callOptional(["v86glMakeCurrent", "_v86glMakeCurrent"], [
                surface.hwnd, surface.x, surface.y, surface.width, surface.height,
            ], ["number", "number", "number", "number", "number"]);
        }

        resize(width, height) {
            if (this.canvas.width !== width || this.canvas.height !== height) {
                this.canvas.width = width;
                this.canvas.height = height;
            }

            this.callOptional(["v86glResize", "_v86glResize", "setCanvasSize"], [
                width, height,
            ], ["number", "number"]);
        }

        malloc(size) {
            const fn = this.module && (this.module._malloc || this.module.malloc);
            if (typeof fn !== "function") {
                this.warnMissing("malloc");
                return 0;
            }

            return fn.call(this.module, size);
        }

        free(ptr) {
            const fn = this.module && (this.module._free || this.module.free);
            if (ptr && typeof fn === "function") {
                fn.call(this.module, ptr);
            }
        }

        heapU8() {
            const heap = this.module && this.module.HEAPU8;
            if (!heap) {
                this.warnMissing("HEAPU8");
                return null;
            }

            return heap;
        }

        withHeapBytes(bytes, callback) {
            if (!bytes || bytes.length === 0) {
                return callback(0);
            }

            const ptr = this.malloc(bytes.length);
            const heap = this.heapU8();
            if (!ptr || !heap) {
                this.free(ptr);
                return false;
            }

            heap.set(bytes, ptr);
            try {
                return callback(ptr);
            } finally {
                this.free(ptr);
            }
        }

        withHeapOutput(size, output, callback) {
            if (!size || !output || output.length !== size) {
                return false;
            }

            const ptr = this.malloc(size);
            let heap = this.heapU8();
            if (!ptr || !heap || ptr + size > heap.length) {
                this.free(ptr);
                return false;
            }

            try {
                if (!callback(ptr)) {
                    return false;
                }

                // A GL call can grow wasm memory, so reacquire the view before copying.
                heap = this.heapU8();
                if (!heap || ptr + size > heap.length) {
                    return false;
                }

                output.set(heap.subarray(ptr, ptr + size));
                return true;
            } finally {
                this.free(ptr);
            }
        }

        withHeapU32(values, callback) {
            const bytes = new Uint8Array(values.length * 4);
            const view = new DataView(bytes.buffer);
            for (let i = 0; i < values.length; i++) {
                view.setUint32(i * 4, values[i] >>> 0, true);
            }

            return this.withHeapBytes(bytes, callback);
        }

        withHeapI32(values, callback) {
            const bytes = new Uint8Array(values.length * 4);
            const view = new DataView(bytes.buffer);
            for (let i = 0; i < values.length; i++) {
                view.setInt32(i * 4, values[i] | 0, true);
            }

            return this.withHeapBytes(bytes, callback);
        }

        withHeapF32(values, callback) {
            const bytes = new Uint8Array(values.length * 4);
            const view = new DataView(bytes.buffer);
            for (let i = 0; i < values.length; i++) {
                view.setFloat32(i * 4, values[i], true);
            }

            return this.withHeapBytes(bytes, callback);
        }

        withHeapBlocks(blocks, callback) {
            const ptrs = new Array(blocks.length).fill(0);
            const allocated = [];
            let needsHeap = false;

            for (let i = 0; i < blocks.length; i++) {
                if (blocks[i] && blocks[i].bytes && blocks[i].bytes.length) {
                    needsHeap = true;
                    break;
                }
            }

            const heap = needsHeap ? this.heapU8() : null;
            if (needsHeap && !heap) {
                return false;
            }

            try {
                for (let i = 0; i < blocks.length; i++) {
                    const block = blocks[i];
                    if (!block || !block.bytes || !block.bytes.length) {
                        continue;
                    }

                    const ptr = this.malloc(block.bytes.length);
                    if (!ptr) {
                        return false;
                    }

                    heap.set(block.bytes, ptr);
                    ptrs[i] = ptr;
                    allocated.push(ptr);
                }

                return callback(ptrs);
            } finally {
                for (let i = 0; i < allocated.length; i++) {
                    this.free(allocated[i]);
                }
            }
        }

        glCall(fn, p) {
            this.log("gl4es dispatch", GLFN_NAMES[fn] || ("fn#" + fn),
                "payload", p.length, "bytes");
            switch (fn) {
            case GLFN_VIEWPORT:
                this.callGL("Viewport", [i32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_CLEAR_COLOR:
                this.callGL("ClearColor", [f32(p, 0), f32(p, 4), f32(p, 8), f32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_CLEAR:
                this.callGL("Clear", [u32(p, 0)], ["number"]);
                break;
            case GLFN_BEGIN:
                this.callGL("Begin", [u32(p, 0)], ["number"]);
                break;
            case GLFN_END:
                this.callGL("End", [], []);
                break;
            case GLFN_COLOR4F:
                this.callGL("Color4f", [f32(p, 0), f32(p, 4), f32(p, 8), f32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_VERTEX3F:
                this.callGL("Vertex3f", [f32(p, 0), f32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_FLUSH:
                this.callGL("Flush", [], []);
                break;
            case GLFN_FINISH:
                this.callGL("Finish", [], []);
                break;
            case GLFN_MATRIX_MODE:
                this.callGL("MatrixMode", [u32(p, 0)], ["number"]);
                break;
            case GLFN_LOAD_IDENTITY:
                this.callGL("LoadIdentity", [], []);
                break;
            case GLFN_FRUSTUM:
                this.callGL("Frustum", [
                    f64(p, 0), f64(p, 8), f64(p, 16),
                    f64(p, 24), f64(p, 32), f64(p, 40),
                ], ["number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_ORTHO:
                this.callGL("Ortho", [
                    f64(p, 0), f64(p, 8), f64(p, 16),
                    f64(p, 24), f64(p, 32), f64(p, 40),
                ], ["number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_TRANSLATEF:
                this.callGL("Translatef", [f32(p, 0), f32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_ROTATEF:
                this.callGL("Rotatef", [f32(p, 0), f32(p, 4), f32(p, 8), f32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_SCALEF:
                this.callGL("Scalef", [f32(p, 0), f32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_PUSH_MATRIX:
                this.callGL("PushMatrix", [], []);
                break;
            case GLFN_POP_MATRIX:
                this.callGL("PopMatrix", [], []);
                break;
            case GLFN_ENABLE:
                this.callGL("Enable", [u32(p, 0)], ["number"]);
                break;
            case GLFN_DISABLE:
                this.callGL("Disable", [u32(p, 0)], ["number"]);
                break;
            case GLFN_DEPTH_FUNC:
                this.callGL("DepthFunc", [u32(p, 0)], ["number"]);
                break;
            case GLFN_CLEAR_DEPTH:
                this.callGL("ClearDepth", [f64(p, 0)], ["number"]);
                break;
            case GLFN_SHADE_MODEL:
                this.callGL("ShadeModel", [u32(p, 0)], ["number"]);
                break;
            case GLFN_CULL_FACE:
                this.callGL("CullFace", [u32(p, 0)], ["number"]);
                break;
            case GLFN_FRONT_FACE:
                this.callGL("FrontFace", [u32(p, 0)], ["number"]);
                break;
            case GLFN_GEN_TEXTURES:
                this.callTextureNameArray("GenTextures", p);
                break;
            case GLFN_DELETE_TEXTURES:
                this.callTextureNameArray("DeleteTextures", p);
                break;
            case GLFN_BIND_TEXTURE:
                this.callGL("BindTexture", [u32(p, 0), u32(p, 4)], ["number", "number"]);
                break;
            case GLFN_TEX_IMAGE_2D:
                this.callTexImage2D(p);
                break;
            case GLFN_COMPRESSED_TEX_IMAGE_2D:
                this.callCompressedTexImage2D(p);
                break;
            case GLFN_TEX_IMAGE_3D:
                this.callTexImage3D(p);
                break;
            case GLFN_TEX_SUB_IMAGE_3D:
                this.callTexSubImage3D(p);
                break;
            case GLFN_TEX_SUB_IMAGE_2D:
                this.callTexSubImage2D(p);
                break;
            case GLFN_TEX_PARAMETERI:
                this.callGL("TexParameteri", [u32(p, 0), u32(p, 4), i32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_TEX_PARAMETERF:
                this.callGL("TexParameterf", [u32(p, 0), u32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_PIXEL_STOREI:
                this.callGL("PixelStorei", [u32(p, 0), i32(p, 4)], ["number", "number"]);
                break;
            case GLFN_TEX_ENVI:
                this.callGL("TexEnvi", [u32(p, 0), u32(p, 4), i32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_TEX_ENVF:
                this.callGL("TexEnvf", [u32(p, 0), u32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_TEX_ENVIV:
                this.callGL("TexEnviv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    i32(p, 12), i32(p, 16), i32(p, 20), i32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_TEX_ENVFV:
                this.callGL("TexEnvfv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    f32(p, 12), f32(p, 16), f32(p, 20), f32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_TEX_COORD2F:
                this.callGL("TexCoord2f", [f32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            case GLFN_ENABLE_CLIENT_STATE:
                this.callGL("EnableClientState", [u32(p, 0)], ["number"]);
                break;
            case GLFN_DISABLE_CLIENT_STATE:
                this.callGL("DisableClientState", [u32(p, 0)], ["number"]);
                break;
            case GLFN_DRAW_ARRAYS:
                this.callDrawArrays(p);
                break;
            case GLFN_DRAW_ELEMENTS:
                this.callDrawElements(p);
                break;
            case GLFN_BLEND_FUNC:
                this.callGL("BlendFunc", [u32(p, 0), u32(p, 4)], ["number", "number"]);
                break;
            case GLFN_BLEND_COLOR:
                this.callGL("BlendColor", [f32(p, 0), f32(p, 4), f32(p, 8), f32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_BLEND_EQUATION:
                this.callGL("BlendEquation", [u32(p, 0)], ["number"]);
                break;
            case GLFN_BLEND_FUNC_SEPARATE:
                this.callGL("BlendFuncSeparate", [u32(p, 0), u32(p, 4), u32(p, 8), u32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_SAMPLE_COVERAGE:
                this.callGL("SampleCoverage", [f32(p, 0), u32(p, 4)], ["number", "number"]);
                break;
            case GLFN_GENERATE_MIPMAP:
                this.callGL("GenerateMipmap", [u32(p, 0)], ["number"]);
                break;
            case GLFN_FOG_COORDF:
                this.callGL("FogCoordf", [f32(p, 0)], ["number"]);
                break;
            case GLFN_SECONDARY_COLOR3F:
                this.callGL("SecondaryColor3f", [f32(p, 0), f32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_POINT_PARAMETERF:
                this.callGL("PointParameterf", [u32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            case GLFN_POINT_PARAMETERFV:
                this.callGL("PointParameterfv3", [u32(p, 0), f32(p, 4), f32(p, 8), f32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_ALPHA_FUNC:
                this.callGL("AlphaFunc", [u32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            case GLFN_DEPTH_MASK:
                this.callGL("DepthMask", [u32(p, 0)], ["number"]);
                break;
            case GLFN_COLOR_MASK:
                this.callGL("ColorMask", [
                    u32(p, 0), u32(p, 4), u32(p, 8), u32(p, 12),
                ], ["number", "number", "number", "number"]);
                break;
            case GLFN_SCISSOR:
                this.callGL("Scissor", [i32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12)], ["number", "number", "number", "number"]);
                break;
            case GLFN_LINE_WIDTH:
                this.callGL("LineWidth", [f32(p, 0)], ["number"]);
                break;
            case GLFN_POLYGON_MODE:
                this.callGL("PolygonMode", [u32(p, 0), u32(p, 4)], ["number", "number"]);
                break;
            case GLFN_ACTIVE_TEXTURE:
                this.callGL("ActiveTexture", [u32(p, 0)], ["number"]);
                break;
            case GLFN_CLIENT_ACTIVE_TEXTURE:
                this.callGL("ClientActiveTexture", [u32(p, 0)], ["number"]);
                break;
            case GLFN_MULTI_TEX_COORD4F:
                this.callGL("MultiTexCoord4f", [
                    u32(p, 0), f32(p, 4), f32(p, 8), f32(p, 12), f32(p, 16),
                ], ["number", "number", "number", "number", "number"]);
                break;
            case GLFN_NORMAL3F:
                this.callGL("Normal3f", [f32(p, 0), f32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_FOGF:
                this.callGL("Fogf", [u32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            case GLFN_FOGI:
                this.callGL("Fogi", [u32(p, 0), i32(p, 4)], ["number", "number"]);
                break;
            case GLFN_FOGFV:
                this.callGL("Fogfv4", [
                    u32(p, 0), u32(p, 4), f32(p, 8), f32(p, 12), f32(p, 16), f32(p, 20),
                ], ["number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_MATERIALF:
                this.callGL("Materialf", [u32(p, 0), u32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_MATERIALI:
                this.callGL("Materiali", [u32(p, 0), u32(p, 4), i32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_MATERIALFV:
                this.callGL("Materialfv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    f32(p, 12), f32(p, 16), f32(p, 20), f32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_MATERIALIV:
                this.callGL("Materialiv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    i32(p, 12), i32(p, 16), i32(p, 20), i32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_LIGHTF:
                this.callGL("Lightf", [u32(p, 0), u32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_LIGHTI:
                this.callGL("Lighti", [u32(p, 0), u32(p, 4), i32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_LIGHTFV:
                this.callGL("Lightfv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    f32(p, 12), f32(p, 16), f32(p, 20), f32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_LIGHTIV:
                this.callGL("Lightiv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    i32(p, 12), i32(p, 16), i32(p, 20), i32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_LIGHT_MODELF:
                this.callGL("LightModelf", [u32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            case GLFN_LIGHT_MODELI:
                this.callGL("LightModeli", [u32(p, 0), i32(p, 4)], ["number", "number"]);
                break;
            case GLFN_LIGHT_MODELFV:
                this.callGL("LightModelfv4", [
                    u32(p, 0), u32(p, 4),
                    f32(p, 8), f32(p, 12), f32(p, 16), f32(p, 20),
                ], ["number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_LIGHT_MODELIV:
                this.callGL("LightModeliv4", [
                    u32(p, 0), u32(p, 4),
                    i32(p, 8), i32(p, 12), i32(p, 16), i32(p, 20),
                ], ["number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_LOAD_MATRIXF:
                this.callMatrixf("LoadMatrixf", p);
                break;
            case GLFN_MULT_MATRIXF:
                this.callMatrixf("MultMatrixf", p);
                break;
            case GLFN_DEPTH_RANGE:
                this.callGL("DepthRange", [f64(p, 0), f64(p, 8)], ["number", "number"]);
                break;
            case GLFN_CLEAR_STENCIL:
                this.callGL("ClearStencil", [i32(p, 0)], ["number"]);
                break;
            case GLFN_STENCIL_FUNC:
                this.callGL("StencilFunc", [u32(p, 0), i32(p, 4), u32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_STENCIL_MASK:
                this.callGL("StencilMask", [u32(p, 0)], ["number"]);
                break;
            case GLFN_STENCIL_OP:
                this.callGL("StencilOp", [u32(p, 0), u32(p, 4), u32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_HINT:
                this.callGL("Hint", [u32(p, 0), u32(p, 4)], ["number", "number"]);
                break;
            case GLFN_POLYGON_OFFSET:
                this.callGL("PolygonOffset", [f32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            case GLFN_TEX_PARAMETERIV:
                this.callGL("TexParameteriv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    i32(p, 12), i32(p, 16), i32(p, 20), i32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_TEX_PARAMETERFV:
                this.callGL("TexParameterfv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    f32(p, 12), f32(p, 16), f32(p, 20), f32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_TEX_GENI:
                this.callGL("TexGeni", [u32(p, 0), u32(p, 4), i32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_TEX_GENF:
                this.callGL("TexGenf", [u32(p, 0), u32(p, 4), f32(p, 8)], ["number", "number", "number"]);
                break;
            case GLFN_TEX_GENIV:
                this.callGL("TexGeniv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    i32(p, 12), i32(p, 16), i32(p, 20), i32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_TEX_GENFV:
                this.callGL("TexGenfv4", [
                    u32(p, 0), u32(p, 4), u32(p, 8),
                    f32(p, 12), f32(p, 16), f32(p, 20), f32(p, 24),
                ], ["number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_CLIP_PLANE:
                this.callGL("ClipPlane4d", [
                    u32(p, 0), f64(p, 4), f64(p, 12), f64(p, 20), f64(p, 28),
                ], ["number", "number", "number", "number", "number"]);
                break;
            case GLFN_COLOR_MATERIAL:
                this.callGL("ColorMaterial", [u32(p, 0), u32(p, 4)], ["number", "number"]);
                break;
            case GLFN_PUSH_ATTRIB:
                this.callGL("PushAttrib", [u32(p, 0)], ["number"]);
                break;
            case GLFN_POP_ATTRIB:
                this.callGL("PopAttrib", [], []);
                break;
            case GLFN_PUSH_CLIENT_ATTRIB:
                this.callGL("PushClientAttrib", [u32(p, 0)], ["number"]);
                break;
            case GLFN_POP_CLIENT_ATTRIB:
                this.callGL("PopClientAttrib", [], []);
                break;
            case GLFN_DRAW_BUFFER:
                this.callGL("DrawBuffer", [u32(p, 0)], ["number"]);
                break;
            case GLFN_READ_BUFFER:
                this.callGL("ReadBuffer", [u32(p, 0)], ["number"]);
                break;
            case GLFN_COPY_TEX_IMAGE_2D:
                this.callGL("CopyTexImage2D", [
                    u32(p, 0), i32(p, 4), u32(p, 8), i32(p, 12),
                    i32(p, 16), i32(p, 20), i32(p, 24), i32(p, 28),
                ], ["number", "number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_COPY_TEX_SUB_IMAGE_2D:
                this.callGL("CopyTexSubImage2D", [
                    u32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12),
                    i32(p, 16), i32(p, 20), i32(p, 24), i32(p, 28),
                ], ["number", "number", "number", "number", "number", "number", "number", "number"]);
                break;
            case GLFN_READ_PIXELS:
                this.callReadPixels(p);
                break;
            default:
                this.warnMissing("GL function id " + fn);
                break;
            }
        }

        callMatrixf(suffix, p) {
            if (p.length < 64) {
                return false;
            }

            const values = [];
            for (let i = 0; i < 16; i++) {
                values.push(f32(p, i * 4));
            }

            return this.withHeapF32(values, ptr =>
                this.callGL(suffix, [ptr], ["number"]));
        }

        callTextureNameArray(suffix, p) {
            if (p.length < 4) {
                return false;
            }

            const n = u32(p, 0);
            const ids = [];
            for (let i = 0; i < n && 4 + i * 4 + 4 <= p.length; i++) {
                ids.push(u32(p, 4 + i * 4));
            }

            return this.withHeapU32(ids, ptr =>
                this.callGL(suffix, [ids.length, ptr], ["number", "number"]));
        }

        callTexImage2D(p) {
            if (p.length < 36) {
                return false;
            }

            const dataSize = u32(p, 32);
            const bytes = dataSize ? p.slice(36, 36 + dataSize) : null;
            return this.withHeapBytes(bytes, ptr =>
                this.callGL("TexImage2D", [
                    u32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12), i32(p, 16),
                    i32(p, 20), u32(p, 24), u32(p, 28), ptr,
                ], ["number", "number", "number", "number", "number", "number", "number", "number", "number"]));
        }

        callCompressedTexImage2D(p) {
            if (p.length < 28) {
                return false;
            }

            const dataSize = u32(p, 24);
            if (28 + dataSize > p.length) {
                return false;
            }
            const bytes = dataSize ? p.slice(28, 28 + dataSize) : null;
            return this.withHeapBytes(bytes, ptr =>
                this.callGL("CompressedTexImage2D", [
                    u32(p, 0), i32(p, 4), u32(p, 8), i32(p, 12), i32(p, 16),
                    i32(p, 20), dataSize, ptr,
                ], ["number", "number", "number", "number", "number", "number", "number", "number"]));
        }

        callTexImage3D(p) {
            if (p.length < 40) {
                return false;
            }

            const dataSize = u32(p, 36);
            if (40 + dataSize > p.length) {
                return false;
            }
            const bytes = dataSize ? p.slice(40, 40 + dataSize) : null;
            return this.withHeapBytes(bytes, ptr =>
                this.callGL("TexImage3D", [
                    u32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12), i32(p, 16),
                    i32(p, 20), i32(p, 24), u32(p, 28), u32(p, 32), ptr,
                ], ["number", "number", "number", "number", "number", "number", "number", "number", "number", "number"]));
        }

        callTexSubImage3D(p) {
            if (p.length < 44) {
                return false;
            }

            const dataSize = u32(p, 40);
            if (44 + dataSize > p.length) {
                return false;
            }
            const bytes = dataSize ? p.slice(44, 44 + dataSize) : null;
            return this.withHeapBytes(bytes, ptr =>
                this.callGL("TexSubImage3D", [
                    u32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12), i32(p, 16),
                    i32(p, 20), i32(p, 24), i32(p, 28), u32(p, 32), u32(p, 36), ptr,
                ], ["number", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number"]));
        }

        callTexSubImage2D(p) {
            if (p.length < 36) {
                return false;
            }

            const dataSize = u32(p, 32);
            const bytes = dataSize ? p.slice(36, 36 + dataSize) : null;
            return this.withHeapBytes(bytes, ptr =>
                this.callGL("TexSubImage2D", [
                    u32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12), i32(p, 16),
                    i32(p, 20), u32(p, 24), u32(p, 28), ptr,
                ], ["number", "number", "number", "number", "number", "number", "number", "number", "number"]));
        }

        callReadPixels(p) {
            if (p.length < V86GL_READ_PIXELS_HEADER_SIZE) {
                return false;
            }

            const dataSize = u32(p, 24);
            const output = p.subarray(V86GL_READ_PIXELS_HEADER_SIZE);
            if (dataSize !== output.length) {
                writeU32(p, 28, V86GL_READ_PIXELS_STATUS_FAILED);
                return false;
            }

            writeU32(p, 28, V86GL_READ_PIXELS_STATUS_PENDING);
            const ok = this.withHeapOutput(dataSize, output, ptr =>
                this.callReadPixelsExport([
                    i32(p, 0), i32(p, 4), i32(p, 8), i32(p, 12),
                    u32(p, 16), u32(p, 20), ptr,
                ]));
            writeU32(p, 28, ok ? V86GL_READ_PIXELS_STATUS_OK : V86GL_READ_PIXELS_STATUS_FAILED);
            return ok;
        }

        parseClientArrayBlocks(p, offset, count) {
            const blocks = [];

            for (let i = 0; i < count; i++) {
                if (offset + 20 > p.length) {
                    return null;
                }

                const enabled = u32(p, offset) !== 0;
                const size = i32(p, offset + 4);
                const type = u32(p, offset + 8);
                const stride = i32(p, offset + 12);
                const dataSize = u32(p, offset + 16);
                offset += 20;

                if (offset + dataSize > p.length) {
                    return null;
                }

                blocks.push({
                    enabled,
                    size: enabled ? size : 0,
                    type: enabled ? type : 0,
                    stride: enabled ? stride : 0,
                    bytes: enabled && dataSize ? p.slice(offset, offset + dataSize) : null,
                });
                offset += dataSize;
            }

            return { blocks, offset };
        }

        clientArrayArgs(blocks, ptrs) {
            const vertex = blocks[0];
            const color = blocks[1];
            const texcoord = blocks[2];
            const normal = blocks[3];

            return [
                vertex.size, vertex.type, vertex.stride, ptrs[0],
                color.size, color.type, color.stride, ptrs[1],
                texcoord.size, texcoord.type, texcoord.stride, ptrs[2],
                normal.type, normal.stride, ptrs[3],
            ];
        }

        clientArrayMetaValues(blocks, ptrs) {
            const values = [];

            for (let i = 0; i < blocks.length; i++) {
                const block = blocks[i] || {};
                values.push(block.size | 0);
                values.push(block.type | 0);
                values.push(block.stride | 0);
                values.push(ptrs[i] | 0);
            }

            return values;
        }

        withClientArrayMeta(blocks, ptrs, callback) {
            return this.withHeapI32(this.clientArrayMetaValues(blocks, ptrs), callback);
        }

        callDrawArrays(p) {
            if (p.length < 8) {
                return false;
            }

            if (p.length >= 20 && u32(p, 8) === CLIENT_ARRAY_MT_MAGIC) {
                const mode = u32(p, 0);
                const count = i32(p, 4);
                const texUnitCount = u32(p, 12);
                const clientActiveTexture = u32(p, 16);
                if (texUnitCount > 8) {
                    return false;
                }
                const parsed = this.parseClientArrayBlocks(p, 20, 3 + texUnitCount);
                if (!parsed) {
                    return false;
                }

                return this.withHeapBlocks(parsed.blocks, ptrs =>
                    this.withClientArrayMeta(parsed.blocks, ptrs, metaPtr =>
                        this.callGL("DrawArraysPackedMT", [
                            mode, count, texUnitCount, clientActiveTexture, metaPtr,
                        ], ["number", "number", "number", "number", "number"])));
            }

            const parsed = this.parseClientArrayBlocks(p, 8, 4);
            if (!parsed) {
                return false;
            }

            const mode = u32(p, 0);
            const count = i32(p, 4);
            return this.withHeapBlocks(parsed.blocks, ptrs =>
                this.callGL("DrawArraysPacked", [
                    mode, count,
                    ...this.clientArrayArgs(parsed.blocks, ptrs),
                ], [
                    "number", "number",
                    "number", "number", "number", "number",
                    "number", "number", "number", "number",
                    "number", "number", "number", "number",
                    "number", "number", "number",
                ]));
        }

        callDrawElements(p) {
            if (p.length < 16) {
                return false;
            }

            const indexDataSize = u32(p, 12);
            if (p.length >= 28 && u32(p, 16) === CLIENT_ARRAY_MT_MAGIC) {
                if (28 + indexDataSize > p.length) {
                    return false;
                }

                const indexBlock = { bytes: indexDataSize ? p.slice(28, 28 + indexDataSize) : null };
                const mode = u32(p, 0);
                const count = i32(p, 4);
                const indexType = u32(p, 8);
                const texUnitCount = u32(p, 20);
                const clientActiveTexture = u32(p, 24);
                if (texUnitCount > 8) {
                    return false;
                }
                const parsed = this.parseClientArrayBlocks(p, 28 + indexDataSize, 3 + texUnitCount);
                if (!parsed) {
                    return false;
                }

                const blocks = [indexBlock, ...parsed.blocks];
                return this.withHeapBlocks(blocks, ptrs =>
                    this.withClientArrayMeta(parsed.blocks, ptrs.slice(1), metaPtr =>
                        this.callGL("DrawElementsPackedMT", [
                            mode, count, indexType, ptrs[0],
                            texUnitCount, clientActiveTexture, metaPtr,
                        ], [
                            "number", "number", "number", "number",
                            "number", "number", "number",
                        ])));
            }

            if (16 + indexDataSize > p.length) {
                return false;
            }

            const indexBlock = { bytes: indexDataSize ? p.slice(16, 16 + indexDataSize) : null };
            const parsed = this.parseClientArrayBlocks(p, 16 + indexDataSize, 4);
            if (!parsed) {
                return false;
            }

            const blocks = [indexBlock, ...parsed.blocks];
            const mode = u32(p, 0);
            const count = i32(p, 4);
            const indexType = u32(p, 8);
            return this.withHeapBlocks(blocks, ptrs =>
                this.callGL("DrawElementsPacked", [
                    mode, count, indexType, ptrs[0],
                    ...this.clientArrayArgs(parsed.blocks, ptrs.slice(1)),
                ], [
                    "number", "number", "number", "number",
                    "number", "number", "number", "number",
                    "number", "number", "number", "number",
                    "number", "number", "number", "number",
                    "number", "number", "number",
                ]));
        }

        present() {
            this.callGL("Flush", [], []);
        }

        releaseCurrent() {
            this.callOptional(["v86glReleaseCurrent", "_v86glReleaseCurrent"], [], []);
        }

        destroy() {
            const destroyed = this.callOptional([
                "v86glDestroyRenderer",
                "_v86glDestroyRenderer",
            ], [], []);
            this.module = null;
            return destroyed;
        }

        callGL(suffix, args, argTypes) {
            const ok = this.callOptional([
                "v86gl_gl" + suffix,
                "_v86gl_gl" + suffix,
                "gl" + suffix,
                "_gl" + suffix,
                "gl4es_gl" + suffix,
                "_gl4es_gl" + suffix,
            ], args, argTypes);
            if (!ok) {
                this.warnMissing("gl" + suffix);
            }
            return ok;
        }

        callReadPixelsExport(args) {
            const module = this.module;
            const names = [
                "v86gl_glReadPixels",
                "_v86gl_glReadPixels",
                "glReadPixels",
                "_glReadPixels",
            ];
            const argTypes = ["number", "number", "number", "number", "number", "number", "number"];

            if (!module) {
                return false;
            }

            for (let i = 0; i < names.length; i++) {
                const fn = module[names[i]];
                if (typeof fn === "function") {
                    return !!fn.apply(module, args);
                }
            }

            if (typeof module.ccall === "function") {
                try {
                    return !!module.ccall("v86gl_glReadPixels", "number", argTypes, args);
                } catch (err) {
                    return false;
                }
            }

            this.warnMissing("glReadPixels");
            return false;
        }

        callOptional(names, args, argTypes) {
            const module = this.module;
            if (!module) {
                return false;
            }

            for (let i = 0; i < names.length; i++) {
                const fn = module[names[i]];
                if (typeof fn === "function") {
                    try {
                        fn.apply(module, args);
                        return true;
                    } catch (err) {
                        console.error("[v86gl] gl4es export threw", names[i], err);
                        return false;
                    }
                }
            }

            if (typeof module.ccall === "function") {
                const cName = names[0].charAt(0) === "_" ? names[0].slice(1) : names[0];
                try {
                    module.ccall(cName, null, argTypes || [], args || []);
                    return true;
                } catch (err) {
                    console.error("[v86gl] gl4es ccall threw", cName, err);
                    return false;
                }
            }

            return false;
        }

        warnMissing(name) {
            if (this.missing[name]) {
                return;
            }

            this.missing[name] = true;
            console.warn("[v86gl] gl4es export not found:", name);
        }
    }

    class V86GLNetworkBridge {
        constructor(emulator, canvas, options) {
            this.emulator = emulator;
            this.canvas = canvas;
            this.options = options || {};
            this.buf = [];
            this.pendingPackets = [];
            this.pendingPCIBatches = [];
            this.packetCount = 0;
            this.byteCount = 0;
            this.chunkedCalls = Object.create(null);
            this.frameStates = Object.create(null);
            this.lastPresentedFrameId = 0;
            this.surface = { hwnd: 0, x: 0, y: 0, width: 0, height: 0 };
            this.container = canvas.parentElement;
            this.screenCanvas = this.findScreenCanvas();
            this.renderer = null;
            this.rendererGeneration = 0;

            this.setRendererFromOptions();
            emulator.add_listener("v86gl-pci-frame", event => this.pushPCIBatch(event));
            this.log("installed PCI DMA listener");
        }

        setRendererFromOptions() {
            const module = this.options.gl4es || global.GL4ES || global.gl4es;

            this.setRendererModule(module, this.rendererGeneration);
        }

        setRendererModule(module, generation) {
            if (!module) {
                throw new Error("gl4es module is required: pass { gl4es } or set window.GL4ES/window.gl4es before installing v86gl");
            }

            if (module && typeof module.then === "function") {
                module.then(resolved => this.setGL4ES(resolved, generation)).catch(err => {
                    if (generation === this.rendererGeneration) {
                        console.error("[v86gl] failed to initialise a fresh gl4es renderer", err);
                    }
                });
                return;
            }

            this.setGL4ES(module, generation);
        }

        setGL4ES(module, generation) {
            if (generation !== this.rendererGeneration) {
                return;
            }

            this.renderer = new Gl4esRenderer(this.canvas, module, (...args) => this.log(...args));
            this.renderer.resize(this.surface.width || this.canvas.width || 640, this.surface.height || this.canvas.height || 480);
            this.log("using gl4es renderer");

            const pending = this.pendingPackets.splice(0);
            for (let i = 0; i < pending.length; i++) {
                this.dispatch(pending[i][0], pending[i][1], pending[i][2]);
            }

            const pendingPCIBatches = this.pendingPCIBatches.splice(0);
            for (let i = 0; i < pendingPCIBatches.length; i++) {
                this.pushPCIBatch(pendingPCIBatches[i]);
            }
        }

        requireRenderer() {
            if (!this.renderer) {
                throw new Error("gl4es module is not ready yet");
            }

            return this.renderer;
        }

        log(...args) {
            if (this.options.logPackets) {
                console.log("[v86gl]", ...args);
            }
        }

        findScreenCanvas() {
            if (!this.container) {
                return null;
            }

            const canvases = this.container.getElementsByTagName("canvas");
            for (let i = 0; i < canvases.length; i++) {
                if (canvases[i] !== this.canvas) {
                    return canvases[i];
                }
            }

            return null;
        }

        pushBytes(bytes) {
            this.byteCount += bytes.length;

            for (let i = 0; i < bytes.length; i++) {
                this.buf.push(bytes[i] & 0xFF);
            }

            this.parse();
        }

        parse() {
            while (this.buf.length >= 12) {
                if (this.buf[0] !== 0x56 || this.buf[1] !== 0x47 || this.buf[2] !== 0x4C || this.buf[3] !== 0x31) {
                    this.buf.shift();
                    continue;
                }

                const op = u16(this.buf, 4);
                const size = u16(this.buf, 6);
                const total = 12 + size;
                if (this.buf.length < total) {
                    return;
                }

                const seq = u32(this.buf, 8);
                const payload = this.buf.slice(12, total);
                this.buf.splice(0, total);
                this.packetCount++;
                this.log("packet", this.packetCount, "seq", seq, OP_NAMES[op] || op, "payload", size, "bytes");
                this.dispatch(op, payload, seq);
            }
        }

        dispatch(op, p, seq) {
            if (!this.renderer) {
                this.pendingPackets.push([op, p, seq]);
                return;
            }

            const renderer = this.requireRenderer();

            switch (op) {
            case OP_MAKE_CURRENT:
                this.makeCurrent(p);
                break;
            case OP_GL_CALL:
                this.dispatchGLCall(p);
                break;
            case OP_GL_FRAME:
                this.dispatchGLFramePacket(p, true);
                break;
            case OP_GL_BATCH:
                this.dispatchGLFramePacket(p, false);
                break;
            case OP_GL_CHUNK:
                this.dispatchGLChunk(p);
                break;
            case OP_VIEWPORT:
                this.resize(i32(p, 8), i32(p, 12));
                renderer.glCall(GLFN_VIEWPORT, p);
                break;
            case OP_CLEAR_COLOR:
                renderer.glCall(GLFN_CLEAR_COLOR, p);
                break;
            case OP_CLEAR:
                renderer.glCall(GLFN_CLEAR, p);
                break;
            case OP_BEGIN:
                renderer.glCall(GLFN_BEGIN, p);
                break;
            case OP_END:
                renderer.glCall(GLFN_END, p);
                break;
            case OP_COLOR4F:
                renderer.glCall(GLFN_COLOR4F, p);
                break;
            case OP_VERTEX3F:
                renderer.glCall(GLFN_VERTEX3F, p);
                break;
            case OP_PRESENT:
                this.dispatchPresentPacket(p);
                break;
            case OP_RELEASE_CURRENT:
                this.releaseCurrent();
                break;
            }
        }

        dispatchGLCall(p) {
            if (p.length < 2) {
                return;
            }

            const fn = u16(p, 0);
            const args = p.slice(2);
            this.log("gl", GLFN_NAMES[fn] || fn, "payload", args.length, "bytes");

            if (fn === GLFN_VIEWPORT && args.length >= 16) {
                this.resize(i32(args, 8), i32(args, 12));
            }

            this.requireRenderer().glCall(fn, args);
        }

        isStaleFrame(frameId) {
            return this.lastPresentedFrameId && frameId <= this.lastPresentedFrameId;
        }

        getFrameState(frameId) {
            let state = this.frameStates[frameId];
            if (!state) {
                state = {
                    id: frameId,
                    expectedLargeCalls: null,
                    completedLargeCalls: 0,
                    pendingUploadCount: 0,
                    items: [],
                    completedUploads: Object.create(null),
                    itemsExecuted: false,
                    presentRequested: false,
                };
                this.frameStates[frameId] = state;
            }

            return state;
        }

        noteExpectedLargeCalls(state, expectedLargeCalls) {
            if (expectedLargeCalls === null || expectedLargeCalls === undefined) {
                return;
            }

            if (state.expectedLargeCalls === null || expectedLargeCalls > state.expectedLargeCalls) {
                state.expectedLargeCalls = expectedLargeCalls;
            }
        }

        executeGLCommands(p, source) {
            let offset = 0;
            let commands = 0;
            const renderer = this.requireRenderer();

            while (offset + 4 <= p.length) {
                const fn = u16(p, offset);
                let size = u16(p, offset + 2);
                offset += 4;

                if (size === V86GL_EXTENDED_RECORD_SIZE) {
                    if (offset + 4 > p.length) {
                        console.warn("[v86gl] truncated extended GL frame command", fn, p.length - offset);
                        break;
                    }

                    size = u32(p, offset);
                    offset += 4;
                }

                if (offset + size > p.length) {
                    console.warn("[v86gl] truncated GL frame command", fn, size, p.length - offset);
                    break;
                }

                const args = p.subarray(offset, offset + size);
                offset += size;
                commands++;

                this.log("command", {
                    source: source || "packet",
                    index: commands,
                    fn,
                    name: GLFN_NAMES[fn] || ("fn#" + fn),
                    payloadBytes: size,
                });

                if (fn === V86GL_CTRL_MAKE_CURRENT) {
                    this.makeCurrent(args);
                    continue;
                }

                if (fn === V86GL_CTRL_RELEASE_CURRENT) {
                    this.releaseCurrent();
                    continue;
                }

                if (fn === V86GL_CTRL_DESTROY_CONTEXT) {
                    this.destroyContext();
                    continue;
                }

                if (fn === GLFN_VIEWPORT && args.length >= 16) {
                    this.resize(i32(args, 8), i32(args, 12));
                }

                renderer.glCall(fn, args);
            }

            this.log("command stream complete", {
                source: source || "packet",
                decodedCommands: commands,
                streamBytes: p.length,
                consumedBytes: offset,
            });
            return commands;
        }

        pushPCIBatch(event) {
            if (!this.renderer) {
                const bytes = event.bytes instanceof Uint8Array ? event.bytes.slice() : new Uint8Array(event.bytes || []);
                this.pendingPCIBatches.push({
                    ...event,
                    bytes,
                });
                return;
            }

            const bytes = event.bytes instanceof Uint8Array ? event.bytes : new Uint8Array(event.bytes || []);
            const frameId = event.frameId >>> 0;
            if (frameId && this.isStaleFrame(frameId)) {
                this.log("drop stale pci frame", frameId, "bytes", bytes.length);
                return;
            }

            const source = "pci frame=" + frameId + " submit=" + (event.submitCount >>> 0);
            this.log("pci frame received", {
                frameId,
                submitCount: event.submitCount >>> 0,
                commandCount: event.commandCount >>> 0,
                commandBytes: bytes.length,
                descAddr: "0x" + ((event.descAddr || 0) >>> 0).toString(16),
                descLen: event.descLen >>> 0,
                flags: "0x" + ((event.flags || 0) >>> 0).toString(16),
            });
            const decoded = this.executeGLCommands(bytes, source);
            if (event.commandCount !== undefined && decoded !== (event.commandCount >>> 0)) {
                console.error("[v86gl] PCI command count mismatch", {
                    frameId,
                    submitCount: event.submitCount >>> 0,
                    descriptorCount: event.commandCount >>> 0,
                    decodedCount: decoded,
                    commandBytes: bytes.length,
                });
            }

            if (event.flags & 1) {
                if (frameId) {
                    this.presentFrame(frameId);
                } else {
                    this.present();
                }
            }
        }

        dispatchGLFramePacket(p, shouldPresent) {
            if (p.length < 8) {
                this.executeGLCommands(p);
                if (shouldPresent) {
                    this.present();
                }
                return;
            }

            const frameId = u32(p, 0);
            const expectedLargeCalls = u32(p, 4);
            const commands = p.slice(8);
            if (this.isStaleFrame(frameId)) {
                this.log("drop stale frame packet", frameId, "bytes", commands.length);
                return;
            }

            const state = this.getFrameState(frameId);
            this.noteExpectedLargeCalls(state, expectedLargeCalls);
            state.items.push({
                type: "commands",
                payload: commands,
            });

            if (shouldPresent) {
                state.presentRequested = true;
            }

            if (state.presentRequested) {
                this.tryPresentFrame(state);
            }
        }

        dispatchPresentPacket(p) {
            if (p.length < 8) {
                this.present();
                return;
            }

            const frameId = u32(p, 0);
            const expectedLargeCalls = u32(p, 4);
            if (this.isStaleFrame(frameId)) {
                this.log("drop stale present", frameId);
                return;
            }

            const state = this.getFrameState(frameId);
            this.noteExpectedLargeCalls(state, expectedLargeCalls);
            state.presentRequested = true;
            this.tryPresentFrame(state);
        }

        executeFrameItems(state) {
            if (state.itemsExecuted) {
                return;
            }

            for (const item of state.items) {
                if (item.type === "commands") {
                    this.executeGLCommands(item.payload);
                    continue;
                }

                if (item.type !== "upload") {
                    continue;
                }

                const upload = item.upload;
                if (!upload || !upload.ready || upload.failed || upload.executed) {
                    continue;
                }

                this.log("chunked gl", GLFN_NAMES[upload.fn] || upload.fn, upload.totalSize, "bytes");
                this.requireRenderer().glCall(upload.fn, upload.data);
                upload.executed = true;
            }

            state.itemsExecuted = true;
        }

        tryPresentFrame(state) {
            if (!state.presentRequested) {
                return;
            }

            if (state.pendingUploadCount > 0) {
                this.log("delay present", state.id, "pending uploads", state.pendingUploadCount);
                return;
            }

            if (state.expectedLargeCalls !== null &&
                state.completedLargeCalls < state.expectedLargeCalls) {
                this.log("delay present", state.id, "completed large calls",
                    state.completedLargeCalls, "of", state.expectedLargeCalls);
                return;
            }

            this.executeFrameItems(state);
            this.presentFrame(state.id);
        }

        dispatchGLChunk(p) {
            if (p.length < 20) {
                return;
            }

            const frameId = u32(p, 0);
            const uploadId = u32(p, 4);
            const fn = u16(p, 8);
            const chunkSize = u16(p, 10);
            const totalSize = u32(p, 12);
            const offset = u32(p, 16);
            if (this.isStaleFrame(frameId)) {
                this.log("drop stale chunk", frameId, uploadId);
                return;
            }

            if (offset + chunkSize > totalSize || 20 + chunkSize > p.length) {
                console.warn("[v86gl] invalid GL chunk", frameId, uploadId, fn, offset, chunkSize, totalSize);
                return;
            }

            const state = this.getFrameState(frameId);
            const key = frameId + ":" + uploadId;
            if (state.completedUploads[uploadId]) {
                this.log("duplicate completed chunk", frameId, uploadId, offset, chunkSize);
                return;
            }

            let upload = this.chunkedCalls[key];
            if (!upload) {
                upload = {
                    frameId,
                    uploadId,
                    fn,
                    totalSize,
                    received: 0,
                    data: new Uint8Array(totalSize),
                    receivedMask: new Uint8Array(totalSize),
                    ready: false,
                    failed: false,
                    executed: false,
                };
                this.chunkedCalls[key] = upload;
                state.pendingUploadCount++;
                state.items.push({
                    type: "upload",
                    upload,
                });
            }

            if (upload.fn !== fn || upload.totalSize !== totalSize || upload.frameId !== frameId) {
                console.warn("[v86gl] mismatched GL chunk", frameId, uploadId, fn, totalSize);
                delete this.chunkedCalls[key];
                if (state.pendingUploadCount > 0) {
                    state.pendingUploadCount--;
                }
                upload.failed = true;
                upload.ready = true;
                state.completedLargeCalls++;
                state.completedUploads[uploadId] = true;
                this.tryPresentFrame(state);
                return;
            }

            upload.data.set(p.slice(20, 20 + chunkSize), offset);

            let newBytes = 0;
            for (let i = 0; i < chunkSize; i++) {
                const byteOffset = offset + i;
                if (!upload.receivedMask[byteOffset]) {
                    upload.receivedMask[byteOffset] = 1;
                    newBytes++;
                }
            }

            if (!newBytes) {
                this.log("duplicate chunk", frameId, uploadId, offset, chunkSize);
                return;
            }

            upload.received += newBytes;

            if (upload.received >= upload.totalSize) {
                delete this.chunkedCalls[key];
                if (state.pendingUploadCount > 0) {
                    state.pendingUploadCount--;
                }
                state.completedLargeCalls++;
                state.completedUploads[uploadId] = true;
                upload.ready = true;
                this.tryPresentFrame(state);
            }
        }

        makeCurrent(p) {
            if (p.length >= 20) {
                this.surface.hwnd = u32(p, 0);
                this.surface.x = i32(p, 4);
                this.surface.y = i32(p, 8);
                this.surface.width = u32(p, 12) || 640;
                this.surface.height = u32(p, 16) || 480;
            } else {
                this.surface.hwnd = u32(p, 0);
                this.surface.x = 0;
                this.surface.y = 0;
                this.surface.width = u32(p, 4) || 640;
                this.surface.height = u32(p, 8) || 480;
            }

            this.resize(this.surface.width, this.surface.height, this.surface.x, this.surface.y);
            this.requireRenderer().makeCurrent(this.surface);
            this.canvas.style.display = "block";
            this.canvas.style.visibility = "visible";
        }

        resize(width, height, x, y) {
            if (width <= 0 || height <= 0) {
                return;
            }

            if (x !== undefined) {
                this.surface.x = x;
            }
            if (y !== undefined) {
                this.surface.y = y;
            }

            this.surface.width = width;
            this.surface.height = height;
            this.requireRenderer().resize(width, height);
            this.positionCanvas();
        }

        styleOverlayCanvas(canvas, left, top, width, height, visible) {
            if (!canvas) {
                return;
            }

            canvas.style.position = "absolute";
            canvas.style.left = left + "px";
            canvas.style.top = top + "px";
            canvas.style.setProperty("width", width + "px", "important");
            canvas.style.setProperty("height", height + "px", "important");
            canvas.style.setProperty("max-width", "none", "important");
            canvas.style.setProperty("max-height", "none", "important");
            canvas.style.pointerEvents = "none";
            canvas.style.display = visible ? "block" : "none";
        }

        positionCanvas() {
            const w = this.surface.width || this.canvas.width;
            const h = this.surface.height || this.canvas.height;
            let left = this.surface.x;
            let top = this.surface.y;
            let width = w;
            let height = h;

            if (this.container && this.screenCanvas && this.screenCanvas.width && this.screenCanvas.height) {
                const containerRect = this.container.getBoundingClientRect();
                const screenRect = this.screenCanvas.getBoundingClientRect();
                const scaleX = screenRect.width / this.screenCanvas.width;
                const scaleY = screenRect.height / this.screenCanvas.height;
                left = screenRect.left - containerRect.left + this.surface.x * scaleX;
                top = screenRect.top - containerRect.top + this.surface.y * scaleY;
                width = w * scaleX;
                height = h * scaleY;
            }

            this.styleOverlayCanvas(this.canvas, left, top, width, height, true);
            this.canvas.style.visibility = "visible";
        }

        presentFrame(frameId) {
            this.present();
            this.lastPresentedFrameId = frameId;

            for (const key in this.frameStates) {
                const id = Number(key);
                if (id <= frameId) {
                    delete this.frameStates[key];
                }
            }
        }

        present() {
            this.positionCanvas();
            this.requireRenderer().present();
            this.canvas.style.display = "block";
            this.canvas.style.visibility = "visible";
            this.log("present", this.canvas.width, "x", this.canvas.height,
                "surface", this.surface.width, "x", this.surface.height);
        }

        releaseCurrent() {
            this.requireRenderer().releaseCurrent();
            // WGL permits a context to be unbound and re-bound around a frame.
            // Keep the last completed frame visible until an explicit teardown.
            this.log("released current context");
        }

        replaceOverlayCanvas() {
            const oldCanvas = this.canvas;
            if (!oldCanvas || !oldCanvas.parentNode) {
                return;
            }

            // A WebGL context is bound to a canvas. Replacing the node prevents
            // browsers from handing the next gl4es instance the old context.
            const freshCanvas = oldCanvas.cloneNode(false);
            oldCanvas.parentNode.replaceChild(freshCanvas, oldCanvas);
            this.canvas = freshCanvas;
            this.container = freshCanvas.parentElement;
            this.screenCanvas = this.findScreenCanvas();
        }

        createFreshRenderer() {
            const reset = this.options.resetGL4ESRenderer || global.resetV86GL4ESRenderer;
            if (typeof reset === "function") {
                return reset(this.canvas);
            }

            const factory = this.options.createGL4ESRenderer || global.createV86GL4ESRenderer;
            if (typeof factory === "function") {
                const renderer = factory(this.canvas);
                global.GL4ES = renderer;
                return renderer;
            }

            const rawFactory = global.createV86GL4ES;
            if (typeof rawFactory === "function") {
                const renderer = rawFactory({ canvas: this.canvas });
                global.GL4ES = renderer;
                return renderer;
            }

            throw new Error("gl4es renderer factory is unavailable after context destruction");
        }

        destroyContext() {
            const oldRenderer = this.renderer;
            this.renderer = null;
            if (oldRenderer) {
                oldRenderer.destroy();
            }
            this.chunkedCalls = Object.create(null);
            this.frameStates = Object.create(null);
            this.lastPresentedFrameId = 0;
            this.canvas.style.display = "none";
            this.canvas.style.visibility = "hidden";
            this.replaceOverlayCanvas();

            const generation = ++this.rendererGeneration;
            try {
                this.setRendererModule(this.createFreshRenderer(), generation);
                this.log("destroyed current context and started a fresh renderer", generation);
            } catch (err) {
                console.error("[v86gl] could not recreate gl4es after context destruction", err);
            }
        }
    }

    global.installV86GLNetworkBridge = function(emulator, canvas, options) {
        return new V86GLNetworkBridge(emulator, canvas, options);
    };
})(typeof window !== "undefined" ? window : globalThis);
