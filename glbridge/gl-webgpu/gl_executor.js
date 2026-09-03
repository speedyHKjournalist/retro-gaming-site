// OpenGL 1.1-2.1 executed on WebGPU.
//
// This is the host half of the OpenGL path. The guest DLL
// (openglproxy/opengl32_proxy.c) remains the guest-side command encoder: it
// serialises the same 217 opcodes into the same PCI DMA arena. What changed is
// everything after the opcode -- the GL state machine, the resources and the
// shaders now live here, in JavaScript, against a GPUDevice shared with
// d3d9_executor.js.
//
// Three decisions shape the whole file:
//
//   * The authoritative GL state machine is here, not in the guest. That is
//     what lets glGetError, glGetIntegerv, glGetUniformLocation and friends be
//     answered synchronously without a GPU round trip -- only glReadPixels and
//     occlusion results actually need one (plan 4.8).
//
//   * Clip space is flipped once, in the vertex shader (plan 4.3). Framebuffer
//     row 0 is then GL's bottom row, so viewport, scissor, readback,
//     glCopyTexImage and render-to-texture orientation all need no conversion.
//     The cost is reversed winding, which lives in exactly one function
//     (gpuFrontFace), and a flip in the present blit.
//
//   * A draw resolves state into a signature and looks the pipeline up. Any
//     state that changes the picture must reach the signature; a field that
//     does not is a bug whose symptom is "I changed the state and nothing
//     happened".
//
// See docs/opengl-webgpu-implementation-plan.zh-CN.md.

(function(global) {
    "use strict";

    const nodeRequire = (typeof require === "function" &&
        typeof module !== "undefined") ? require : null;
    const wire = nodeRequire ? nodeRequire("./gl_wire.js") : global.GLWireFormat;
    const constants = nodeRequire ? nodeRequire("./gl_constants.js") :
        global.GLWGConstants;
    const stateLayout = nodeRequire ? nodeRequire("./gl_state_layout.js") :
        global.GLStateLayout;
    const translator = nodeRequire ? nodeRequire("./gl_shader_translator.js") :
        global.GLShaderTranslator;
    const fixedFunction = nodeRequire ? nodeRequire("./gl_fixed_function.js") :
        global.GLFixedFunction;
    const arbProgram = nodeRequire ? nodeRequire("./gl_arb_program.js") :
        global.GLARBProgram;
    const gpuHost = nodeRequire ? nodeRequire("../webgpu_host.js") :
        global.V86GPUHost;

    const GL = constants.GL;
    const GLFN = constants.GLFN;
    const CTRL = constants.CTRL;
    const SIGNATURES = wire.SIGNATURES;

    const EXECUTOR_REVISION = 1;

    const MAX_TEXTURE_UNITS = 8;
    const MAX_TEXTURE_COORDS = 8;
    const MAX_LIGHTS = 8;
    const MAX_CLIP_PLANES = 6;

    // glPushAttrib/glPushClientAttrib masks.  Keep these local to the executor:
    // they are state-group selectors rather than wire opcodes, and spelling the
    // groups out here makes it much harder for a newly added state field to be
    // silently restored by the wrong mask.
    const ATTRIB = {
        CURRENT: 0x00000001, POINT: 0x00000002, LINE: 0x00000004,
        POLYGON: 0x00000008, POLYGON_STIPPLE: 0x00000010,
        PIXEL_MODE: 0x00000020, LIGHTING: 0x00000040, FOG: 0x00000080,
        DEPTH_BUFFER: 0x00000100, ACCUM_BUFFER: 0x00000200,
        STENCIL_BUFFER: 0x00000400, VIEWPORT: 0x00000800,
        TRANSFORM: 0x00001000, ENABLE: 0x00002000,
        COLOR_BUFFER: 0x00004000, HINT: 0x00008000,
        EVAL: 0x00010000, LIST: 0x00020000, TEXTURE: 0x00040000,
        SCISSOR: 0x00080000, MULTISAMPLE: 0x20000000,
    };
    const CLIENT_ATTRIB = { PIXEL_STORE: 0x00000001, VERTEX_ARRAY: 0x00000002 };
    const MAX_VERTEX_ATTRIBS = 16;
    const MAX_DRAW_BUFFERS = 8;
    const MODELVIEW_STACK_DEPTH = 32;
    const OTHER_STACK_DEPTH = 4;
    /* A guest process can flush object-deletion records after its final
     * DESTROY_CONTEXT record. destroyContext() has already retired every
     * object in the orphaned share group, so replaying those deletes has no
     * semantic work left and should not look like a rendering warning. */
    const CONTEXT_FREE_TEARDOWN_OPS = new Set([
        GLFN.DELETE_TEXTURES, GLFN.DELETE_BUFFERS, GLFN.DELETE_PROGRAMS_ARB,
        GLFN.DELETE_PROGRAM, GLFN.DELETE_SHADER, GLFN.DELETE_FRAMEBUFFERS,
        GLFN.DELETE_RENDERBUFFERS, GLFN.DELETE_QUERIES,
    ]);

    function isContextFreeTeardownNoop(fn, view, offset, size) {
        if (CONTEXT_FREE_TEARDOWN_OPS.has(fn)) return true;
        /* GLView unbinds both ARB targets after deleting its last HGLRC. The
         * payload is {target, program}; only program zero is harmless. Keep a
         * warning for a non-zero late bind because that can reveal a genuine
         * control/GL stream ordering bug. */
        return fn === GLFN.BIND_PROGRAM_ARB && size >= 8 &&
            view.getUint32(offset + 4, true) === 0;
    }

    const TEXTURE_USAGE_COPY_SRC = 0x01;
    const TEXTURE_USAGE_COPY_DST = 0x02;
    const TEXTURE_USAGE_TEXTURE_BINDING = 0x04;
    const TEXTURE_USAGE_RENDER_ATTACHMENT = 0x10;
    const BUFFER_USAGE_MAP_READ = 0x0001;
    const BUFFER_USAGE_COPY_SRC = 0x0004;
    const BUFFER_USAGE_COPY_DST = 0x0008;
    const BUFFER_USAGE_INDEX = 0x0010;
    const BUFFER_USAGE_VERTEX = 0x0020;
    const BUFFER_USAGE_UNIFORM = 0x0040;
    const BUFFER_USAGE_QUERY_RESOLVE = 0x0200;
    /* D-07. Matches V86GL_QUERY_VISIBLE_SAMPLES in the guest, which uses the
     * same value when it has to answer a blocking GL_QUERY_RESULT itself. */
    const OCCLUSION_VISIBLE_SAMPLES = 0x7FFFFFFF;

    /*
     * The first bytes of a record, as 32-bit words and as hex.
     *
     * Words because every payload in this protocol starts with a run of
     * uint32 fields, so a decoded word list can be read straight against the
     * struct in openglproxy/opengl32_proxy.c; hex because the bytes after
     * them usually cannot.
     */
    function hexPreview(bytes, offset, size, limit) {
        // `offset` is relative to the Uint8Array passed to submit(), just like
        // DataView offsets and Uint8Array indices.  The array may itself be a
        // view into guest RAM with a non-zero byteOffset; subtracting that
        // backing-buffer offset a second time reads an unrelated batch.
        const start = offset;
        const count = Math.min(size, limit || 48);
        if (start < 0 || count <= 0) return "";
        const slice = bytes.subarray(start, start + count);
        const words = [];
        const view = new DataView(slice.buffer, slice.byteOffset, slice.byteLength);
        for (let i = 0; i + 4 <= slice.byteLength && words.length < 8; i += 4)
            words.push(view.getUint32(i, true));
        let hex = "";
        for (let i = 0; i < slice.byteLength; ++i)
            hex += (i && !(i % 4) ? " " : "") +
                slice[i].toString(16).padStart(2, "0");
        return "words[" + words.join(",") + "] " + hex +
            (size > count ? " …(" + size + " bytes)" : "");
    }

    /* The response region carved out of the tail of v86gl.sys's DMA
     * allocation, mirroring D9WG's layout so both paths look the same to
     * anyone reading guest memory. See plan section 6.2. */
    const GLWG_RESPONSE_REGION_BYTES = 4 * 1024 * 1024;
    const GLWG_SLOT_BYTES = 16;
    const GLWG_SLOT_COUNT = 1024;
    const GLWG_QUERY_REGION_BYTES = GLWG_SLOT_BYTES * GLWG_SLOT_COUNT;
    const GLWG_READBACK_REGION_OFFSET = GLWG_QUERY_REGION_BYTES;
    const GLWG_HEARTBEAT_BYTES = 16;
    const GLWG_HEARTBEAT_OFFSET =
        GLWG_RESPONSE_REGION_BYTES - GLWG_HEARTBEAT_BYTES;
    const GLWG_RESPONSE_PENDING = 0;
    const GLWG_RESPONSE_OK = 1;
    const GLWG_RESPONSE_FAILED = 2;

    const CLIENT_ARRAY_MT_MAGIC = 0x544D4143;   // 'CAMT'
    const CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT = 0x80000000;
    const CLIENT_ARRAY_MT_FOG_COORD_BIT = 0x40000000;

    const UNIFORM_RING_BYTES = 4 * 1024 * 1024;
    const VERTEX_RING_BYTES = 8 * 1024 * 1024;

    /* ================================================================== */
    /* Small helpers                                                      */
    /* ================================================================== */

    function alignUp(value, alignment) {
        return Math.ceil(value / alignment) * alignment;
    }

    function clamp(value, low, high) {
        return value < low ? low : (value > high ? high : value);
    }

    class GLStreamError extends Error {}

    /* ---- matrices: column-major, the same order GL and WGSL both use ---- */

    function identity4(out) {
        const m = out || new Float32Array(16);
        m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 0;
        m[4] = 0; m[5] = 1; m[6] = 0; m[7] = 0;
        m[8] = 0; m[9] = 0; m[10] = 1; m[11] = 0;
        m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
        return m;
    }

    function multiply4(a, b, out) {
        const m = out || new Float32Array(16);
        for (let c = 0; c < 4; ++c) {
            const b0 = b[c * 4], b1 = b[c * 4 + 1];
            const b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
            m[c * 4] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
            m[c * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
            m[c * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
            m[c * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
        }
        return m;
    }

    function invert4(m, out) {
        const inv = out || new Float32Array(16);
        const a = m;
        inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] +
                 a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] -
                 a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] +
                 a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] -
                  a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] -
                 a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] +
                 a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] -
                 a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] +
                  a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] +
                 a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
        inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] -
                 a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
        inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] +
                  a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
        inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] -
                  a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
        inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] -
                 a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
        inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] +
                 a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
        inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] -
                  a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
        inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] +
                  a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
        let det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
        if (det === 0) return identity4(inv);
        det = 1.0 / det;
        for (let i = 0; i < 16; ++i) inv[i] *= det;
        return inv;
    }

    function transpose4(m, out) {
        const t = out || new Float32Array(16);
        for (let c = 0; c < 4; ++c)
            for (let r = 0; r < 4; ++r)
                t[r * 4 + c] = m[c * 4 + r];
        return t;
    }

    /* gl_NormalMatrix is the inverse transpose of the modelview's upper 3x3. */
    function normalMatrixOf(modelview, out) {
        const n = out || new Float32Array(9);
        const a = modelview;
        const m00 = a[0], m01 = a[4], m02 = a[8];
        const m10 = a[1], m11 = a[5], m12 = a[9];
        const m20 = a[2], m21 = a[6], m22 = a[10];
        const c00 = m11 * m22 - m12 * m21;
        const c01 = m12 * m20 - m10 * m22;
        const c02 = m10 * m21 - m11 * m20;
        let det = m00 * c00 + m01 * c01 + m02 * c02;
        if (det === 0) {
            n[0] = 1; n[1] = 0; n[2] = 0;
            n[3] = 0; n[4] = 1; n[5] = 0;
            n[6] = 0; n[7] = 0; n[8] = 1;
            return n;
        }
        det = 1 / det;
        // Column-major: column j holds the inverse-transpose's jth column,
        // which is the jth row of the inverse -- hence the cofactor order.
        n[0] = c00 * det; n[1] = c01 * det; n[2] = c02 * det;
        n[3] = (m02 * m21 - m01 * m22) * det;
        n[4] = (m00 * m22 - m02 * m20) * det;
        n[5] = (m01 * m20 - m00 * m21) * det;
        n[6] = (m01 * m12 - m02 * m11) * det;
        n[7] = (m02 * m10 - m00 * m12) * det;
        n[8] = (m00 * m11 - m01 * m10) * det;
        return n;
    }

    function transformPoint4(m, v, out) {
        const o = out || new Float32Array(4);
        o[0] = m[0]*v[0] + m[4]*v[1] + m[8]*v[2] + m[12]*v[3];
        o[1] = m[1]*v[0] + m[5]*v[1] + m[9]*v[2] + m[13]*v[3];
        o[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14]*v[3];
        o[3] = m[3]*v[0] + m[7]*v[1] + m[11]*v[2] + m[15]*v[3];
        return o;
    }

    /* ================================================================== */
    /* GL enum translation                                                */
    /* ================================================================== */

    const COMPARE_FUNCTIONS = {
        [GL.NEVER]: "never", [GL.LESS]: "less", [GL.EQUAL]: "equal",
        [GL.LEQUAL]: "less-equal", [GL.GREATER]: "greater",
        [GL.NOTEQUAL]: "not-equal", [GL.GEQUAL]: "greater-equal",
        [GL.ALWAYS]: "always",
    };

    const ALPHA_TEST_NAMES = {
        [GL.NEVER]: "never", [GL.LESS]: "less", [GL.EQUAL]: "equal",
        [GL.LEQUAL]: "lequal", [GL.GREATER]: "greater",
        [GL.NOTEQUAL]: "notequal", [GL.GEQUAL]: "gequal",
        [GL.ALWAYS]: "always",
    };

    const STENCIL_OPERATIONS = {
        [GL.KEEP]: "keep", [GL.ZERO]: "zero", [GL.REPLACE]: "replace",
        [GL.INCR]: "increment-clamp", [GL.DECR]: "decrement-clamp",
        [GL.INVERT]: "invert",
        [GL.INCR_WRAP]: "increment-wrap", [GL.DECR_WRAP]: "decrement-wrap",
    };

    const BLEND_FACTORS = {
        [GL.ZERO]: "zero", [GL.ONE]: "one",
        [GL.SRC_COLOR]: "src", [GL.ONE_MINUS_SRC_COLOR]: "one-minus-src",
        [GL.DST_COLOR]: "dst", [GL.ONE_MINUS_DST_COLOR]: "one-minus-dst",
        [GL.SRC_ALPHA]: "src-alpha", [GL.ONE_MINUS_SRC_ALPHA]: "one-minus-src-alpha",
        [GL.DST_ALPHA]: "dst-alpha", [GL.ONE_MINUS_DST_ALPHA]: "one-minus-dst-alpha",
        [GL.CONSTANT_COLOR]: "constant",
        [GL.ONE_MINUS_CONSTANT_COLOR]: "one-minus-constant",
        // WebGPU has no separate alpha blend constant: one setBlendConstant
        // covers both, so GL_CONSTANT_ALPHA maps to the same factor and the
        // executor writes the constant's alpha into it. Exact for the usual
        // case where a program sets one constant colour and uses both.
        [GL.CONSTANT_ALPHA]: "constant",
        [GL.ONE_MINUS_CONSTANT_ALPHA]: "one-minus-constant",
        [GL.SRC_ALPHA_SATURATE]: "src-alpha-saturated",
    };

    const BLEND_EQUATIONS = {
        [GL.FUNC_ADD]: "add",
        [GL.FUNC_SUBTRACT]: "subtract",
        [GL.FUNC_REVERSE_SUBTRACT]: "reverse-subtract",
        [GL.MIN]: "min", [GL.MAX]: "max",
        [GL.MIN_EXT]: "min", [GL.MAX_EXT]: "max",
    };

    /* The selected default pixel format is currently single-sampled, but ARB
     * sample coverage still has observable all-or-nothing coverage there. */
    function sampleCoverageMask(state) {
        if (!state.enabled.has(GL.MULTISAMPLE) ||
                !state.enabled.has(GL.SAMPLE_COVERAGE))
            return 0xffffffff;
        let covered = state.sampleCoverage.value > 0 ? 1 : 0;
        if (state.sampleCoverage.invert) covered ^= 1;
        return covered;
    }

    const ADDRESS_MODES = {
        [GL.REPEAT]: "repeat",
        [GL.CLAMP_TO_EDGE]: "clamp-to-edge",
        [GL.MIRRORED_REPEAT]: "mirror-repeat",
        // GL_CLAMP and GL_CLAMP_TO_BORDER sample the border colour outside
        // [0,1]; WebGPU has no border addressing at all, so the sampler clamps
        // and the shader is responsible for the border. Deviation D-03.
        [GL.CLAMP]: "clamp-to-edge",
        [GL.CLAMP_TO_BORDER]: "clamp-to-edge",
    };

    /* GL's ten primitive modes over WebGPU's five. Everything that has no
     * direct topology is expanded through an index buffer (plan 4.6). */
    const PRIMITIVE_TOPOLOGY = {
        [GL.POINTS]: "point-list",
        [GL.LINES]: "line-list",
        [GL.LINE_STRIP]: "line-strip",
        [GL.LINE_LOOP]: "line-strip",
        [GL.TRIANGLES]: "triangle-list",
        [GL.TRIANGLE_STRIP]: "triangle-strip",
        [GL.TRIANGLE_FAN]: "triangle-list",
        [GL.QUADS]: "triangle-list",
        [GL.QUAD_STRIP]: "triangle-list",
        [GL.POLYGON]: "triangle-list",
    };

    function needsIndexExpansion(mode) {
        return mode === GL.TRIANGLE_FAN || mode === GL.QUADS ||
            mode === GL.QUAD_STRIP || mode === GL.POLYGON ||
            mode === GL.LINE_LOOP;
    }

    /*
     * Flat shading takes its colour from a primitive-specific vertex, and the
     * table is not "the first one": a triangle fan uses the second vertex of
     * each triangle and a quad uses the fourth. WGSL's @interpolate(flat)
     * takes the first, so expansion puts GL's choice there. Getting this wrong
     * shifts colours by one vertex on flat-shaded models only, which is why
     * gl_flat_shading_test.exe walks the whole table.
     */
    function expandIndices(mode, count, base) {
        const first = base || 0;
        switch (mode) {
        case GL.TRIANGLE_FAN: {
            if (count < 3) return new Uint32Array(0);
            const out = new Uint32Array((count - 2) * 3);
            let w = 0;
            for (let i = 0; i < count - 2; ++i) {
                // provoking vertex i+1 leads
                out[w++] = first + i + 1;
                out[w++] = first + i + 2;
                out[w++] = first;
            }
            return out;
        }
        case GL.QUADS: {
            const quads = Math.floor(count / 4);
            const out = new Uint32Array(quads * 6);
            let w = 0;
            for (let q = 0; q < quads; ++q) {
                const b = first + q * 4;
                // provoking vertex is the quad's last, so it leads both halves
                out[w++] = b + 3; out[w++] = b; out[w++] = b + 1;
                out[w++] = b + 3; out[w++] = b + 1; out[w++] = b + 2;
            }
            return out;
        }
        case GL.QUAD_STRIP: {
            if (count < 4) return new Uint32Array(0);
            const quads = Math.floor(count / 2) - 1;
            const out = new Uint32Array(quads * 6);
            let w = 0;
            for (let q = 0; q < quads; ++q) {
                const b = first + q * 2;
                // GL's provoking vertex for quad-strip quad i is 2i + 2
                out[w++] = b + 3; out[w++] = b; out[w++] = b + 1;
                out[w++] = b + 3; out[w++] = b + 1; out[w++] = b + 2;
            }
            return out;
        }
        case GL.POLYGON: {
            if (count < 3) return new Uint32Array(0);
            const out = new Uint32Array((count - 2) * 3);
            let w = 0;
            for (let i = 0; i < count - 2; ++i) {
                // the whole polygon takes its flat colour from vertex 0
                out[w++] = first;
                out[w++] = first + i + 1;
                out[w++] = first + i + 2;
            }
            return out;
        }
        case GL.LINE_LOOP: {
            if (count < 2) return new Uint32Array(0);
            const out = new Uint32Array(count + 1);
            for (let i = 0; i < count; ++i) out[i] = first + i;
            out[count] = first;
            return out;
        }
        default: {
            const out = new Uint32Array(count);
            for (let i = 0; i < count; ++i) out[i] = first + i;
            return out;
        }
        }
    }

    /* The same expansion applied to an existing index array. */
    function expandIndexArray(mode, indices) {
        const count = indices.length;
        const map = expandIndices(mode, count, 0);
        const out = new Uint32Array(map.length);
        for (let i = 0; i < map.length; ++i) out[i] = indices[map[i]];
        return out;
    }

    function expandedIndexCount(mode, count) {
        switch (mode) {
        case GL.TRIANGLE_FAN: return count < 3 ? 0 : (count - 2) * 3;
        case GL.QUADS: return Math.floor(count / 4) * 6;
        case GL.QUAD_STRIP: return count < 4 ? 0 : (Math.floor(count / 2) - 1) * 6;
        case GL.POLYGON: return count < 3 ? 0 : (count - 2) * 3;
        case GL.LINE_LOOP: return count < 2 ? 0 : count + 1;
        default: return count;
        }
    }

    function isPolygonPrimitive(mode) {
        return mode === GL.TRIANGLES || mode === GL.TRIANGLE_STRIP ||
            mode === GL.TRIANGLE_FAN || mode === GL.QUADS ||
            mode === GL.QUAD_STRIP || mode === GL.POLYGON;
    }

    /* Produces independent triangles before polygon raster mode is applied.
     * `source` is the application's optional index array; without it the
     * vertices are numbered from zero and drawIndexed's baseVertex preserves
     * glDrawArrays(first). */
    function triangleListIndices(mode, count, source) {
        const input = source || (() => {
            const out = new Uint32Array(count);
            for (let i = 0; i < count; ++i) out[i] = i;
            return out;
        })();
        if (mode === GL.TRIANGLES)
            return input.slice(0, Math.floor(input.length / 3) * 3);
        if (mode === GL.TRIANGLE_STRIP) {
            if (input.length < 3) return new Uint32Array(0);
            const out = new Uint32Array((input.length - 2) * 3);
            let at = 0;
            for (let i = 0; i + 2 < input.length; ++i) {
                if (i & 1) {
                    out[at++] = input[i + 1]; out[at++] = input[i];
                } else {
                    out[at++] = input[i]; out[at++] = input[i + 1];
                }
                out[at++] = input[i + 2];
            }
            return out;
        }
        return expandIndexArray(mode, input);
    }

    function polygonRasterIndices(mode, count, source, rasterMode) {
        const triangles = triangleListIndices(mode, count, source);
        if (rasterMode === GL.POINT) return triangles;
        if (rasterMode !== GL.LINE) return triangles;
        const out = new Uint32Array(triangles.length * 2);
        let at = 0;
        for (let i = 0; i + 2 < triangles.length; i += 3) {
            const a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            out[at++] = a; out[at++] = b;
            out[at++] = b; out[at++] = c;
            out[at++] = c; out[at++] = a;
        }
        return out;
    }

    /* ================================================================== */
    /* Pixel formats                                                      */
    /* ================================================================== */

    /*
     * GL's texture formats are three loosely coupled parameters: the internal
     * format says what is stored, and (format, type) say how the bytes arriving
     * from the guest are laid out. WebGPU has one format per texture, so both
     * halves are resolved here: incoming rows are converted to the storage
     * format, and the *base* internal format is remembered because the fixed
     * pipeline's texture environment depends on it.
     *
     * The channel expansion is the part worth getting exactly right:
     *   ALPHA           -> (0, 0, 0, a)
     *   LUMINANCE       -> (l, l, l, 1)
     *   LUMINANCE_ALPHA -> (l, l, l, a)
     *   INTENSITY       -> (i, i, i, i)
     *   RGB             -> (r, g, b, 1)
     * Every one of those is a rule from the GL spec's texture-sampling table,
     * and getting one wrong is a colour shift nobody can trace back to here.
     */

    const BASE_INTERNAL_FORMATS = {
        [GL.ALPHA]: "ALPHA", [GL.ALPHA4]: "ALPHA", [GL.ALPHA8]: "ALPHA",
        [GL.ALPHA12]: "ALPHA", [GL.ALPHA16]: "ALPHA",
        [GL.LUMINANCE]: "LUMINANCE", [GL.LUMINANCE4]: "LUMINANCE",
        [GL.LUMINANCE8]: "LUMINANCE", [GL.LUMINANCE12]: "LUMINANCE",
        [GL.LUMINANCE16]: "LUMINANCE",
        [GL.LUMINANCE_ALPHA]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE4_ALPHA4]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE8_ALPHA8]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE12_ALPHA12]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE16_ALPHA16]: "LUMINANCE_ALPHA",
        [GL.INTENSITY]: "INTENSITY", [GL.INTENSITY8]: "INTENSITY",
        [GL.INTENSITY16]: "INTENSITY",
        [GL.RGB]: "RGB", [GL.RGB4]: "RGB", [GL.RGB5]: "RGB", [GL.RGB8]: "RGB",
        [GL.RGB10]: "RGB", [GL.RGB12]: "RGB", [GL.RGB16]: "RGB",
        [GL.R3_G3_B2]: "RGB", [GL.RGB565]: "RGB",
        [GL.RGBA]: "RGBA", [GL.RGBA2]: "RGBA", [GL.RGBA4]: "RGBA",
        [GL.RGB5_A1]: "RGBA", [GL.RGBA8]: "RGBA", [GL.RGB10_A2]: "RGBA",
        [GL.RGBA12]: "RGBA", [GL.RGBA16]: "RGBA",
        [GL.BGR]: "RGB", [GL.BGRA]: "RGBA",
        3: "RGB", 4: "RGBA", 1: "LUMINANCE", 2: "LUMINANCE_ALPHA",
        [GL.DEPTH_COMPONENT]: "DEPTH",
        [GL.DEPTH_COMPONENT16]: "DEPTH",
        [GL.DEPTH_COMPONENT24]: "DEPTH",
        [GL.DEPTH_COMPONENT32]: "DEPTH",
        [GL.SRGB]: "RGB", [GL.SRGB8]: "RGB",
        [GL.SRGB_ALPHA]: "RGBA", [GL.SRGB8_ALPHA8]: "RGBA",
        [GL.RGBA16F_ARB]: "RGBA", [GL.RGBA32F_ARB]: "RGBA",
        [GL.RGB16F_ARB]: "RGB", [GL.RGB32F_ARB]: "RGB",
        [GL.COLOR_INDEX]: "COLOR_INDEX",
        [GL.COLOR_INDEX8_EXT]: "COLOR_INDEX",
    };

    const COMPRESSED_FORMATS = {
        [GL.COMPRESSED_RGB_S3TC_DXT1_EXT]:
            { gpu: "bc1-rgba-unorm", blockBytes: 8, base: "RGB", dxt: 1 },
        [GL.COMPRESSED_RGBA_S3TC_DXT1_EXT]:
            { gpu: "bc1-rgba-unorm", blockBytes: 8, base: "RGBA", dxt: 1 },
        [GL.COMPRESSED_RGBA_S3TC_DXT3_EXT]:
            { gpu: "bc2-rgba-unorm", blockBytes: 16, base: "RGBA", dxt: 3 },
        [GL.COMPRESSED_RGBA_S3TC_DXT5_EXT]:
            { gpu: "bc3-rgba-unorm", blockBytes: 16, base: "RGBA", dxt: 5 },
    };

    function storageFormatFor(internalFormat, features) {
        const compressed = COMPRESSED_FORMATS[internalFormat];
        if (compressed) {
            // Without the BC feature the blocks are decoded on the CPU rather
            // than the extension being withdrawn, because the guest may have
            // already been told S3TC exists. The decode is deterministic, so
            // the picture is identical; only the memory cost differs.
            return features && features.bc ?
                { gpu: compressed.gpu, compressed: true, blockBytes: compressed.blockBytes,
                  base: compressed.base, dxt: compressed.dxt } :
                { gpu: "rgba8unorm", compressed: false, decodeDXT: compressed.dxt,
                  base: compressed.base, blockBytes: compressed.blockBytes };
        }
        const base = BASE_INTERNAL_FORMATS[internalFormat];
        switch (internalFormat) {
        case GL.DEPTH_COMPONENT:
        case GL.DEPTH_COMPONENT24:
            // depth24plus has an implementation-defined texel layout and cannot
            // receive queue.writeTexture data. depth32float preserves uploaded
            // depth values and remains renderable/sampleable on core WebGPU.
            return { gpu: "depth32float", depth: true, base: "DEPTH" };
        case GL.DEPTH_COMPONENT16:
            return { gpu: "depth16unorm", depth: true, base: "DEPTH" };
        case GL.DEPTH_COMPONENT32:
            return { gpu: "depth32float", depth: true, base: "DEPTH" };
        case GL.SRGB:
        case GL.SRGB8:
        case GL.SRGB_ALPHA:
        case GL.SRGB8_ALPHA8:
            return { gpu: "rgba8unorm-srgb", base: base || "RGBA" };
        case GL.RGBA16F_ARB:
        case GL.RGB16F_ARB:
            return { gpu: "rgba16float", base: base || "RGBA", float: true };
        case GL.RGBA32F_ARB:
        case GL.RGB32F_ARB:
            return { gpu: "rgba32float", base: base || "RGBA", float: true };
        case GL.COLOR_INDEX:
        case GL.COLOR_INDEX8_EXT:
            // Palettised textures keep their index on the GPU and are resolved
            // in the shader, the same way the D3D9 path handles P8.
            return { gpu: "r8uint", base: "COLOR_INDEX", palettised: true };
        default:
            return { gpu: "rgba8unorm", base: base || "RGBA" };
        }
    }

    /* Components per texel in the *source* data, by format. */
    const SOURCE_COMPONENTS = {
        [GL.RED]: 1, [GL.GREEN]: 1, [GL.BLUE]: 1, [GL.ALPHA]: 1,
        [GL.LUMINANCE]: 1, [GL.INTENSITY]: 1, [GL.DEPTH_COMPONENT]: 1,
        [GL.COLOR_INDEX]: 1, [GL.STENCIL_INDEX]: 1,
        [GL.LUMINANCE_ALPHA]: 2,
        [GL.RGB]: 3, [GL.BGR]: 3,
        [GL.RGBA]: 4, [GL.BGRA]: 4, [GL.ABGR_EXT]: 4,
    };

    const TYPE_BYTES = {
        [GL.UNSIGNED_BYTE]: 1, [GL.BYTE]: 1,
        [GL.UNSIGNED_SHORT]: 2, [GL.SHORT]: 2,
        [GL.HALF_FLOAT]: 2,
        [GL.UNSIGNED_INT]: 4, [GL.INT]: 4, [GL.FLOAT]: 4,
        [GL.UNSIGNED_BYTE_3_3_2]: 1, [GL.UNSIGNED_BYTE_2_3_3_REV]: 1,
        [GL.UNSIGNED_SHORT_5_6_5]: 2, [GL.UNSIGNED_SHORT_5_6_5_REV]: 2,
        [GL.UNSIGNED_SHORT_4_4_4_4]: 2, [GL.UNSIGNED_SHORT_4_4_4_4_REV]: 2,
        [GL.UNSIGNED_SHORT_5_5_5_1]: 2, [GL.UNSIGNED_SHORT_1_5_5_5_REV]: 2,
        [GL.UNSIGNED_INT_8_8_8_8]: 4, [GL.UNSIGNED_INT_8_8_8_8_REV]: 4,
        [GL.UNSIGNED_INT_10_10_10_2]: 4, [GL.UNSIGNED_INT_2_10_10_10_REV]: 4,
    };

    const PACKED_TYPES = new Set([
        GL.UNSIGNED_BYTE_3_3_2, GL.UNSIGNED_BYTE_2_3_3_REV,
        GL.UNSIGNED_SHORT_5_6_5, GL.UNSIGNED_SHORT_5_6_5_REV,
        GL.UNSIGNED_SHORT_4_4_4_4, GL.UNSIGNED_SHORT_4_4_4_4_REV,
        GL.UNSIGNED_SHORT_5_5_5_1, GL.UNSIGNED_SHORT_1_5_5_5_REV,
        GL.UNSIGNED_INT_8_8_8_8, GL.UNSIGNED_INT_8_8_8_8_REV,
        GL.UNSIGNED_INT_10_10_10_2, GL.UNSIGNED_INT_2_10_10_10_REV,
    ]);

    function sourceTexelBytes(format, type) {
        const typeBytes = TYPE_BYTES[type];
        if (typeBytes === undefined) return 0;
        if (PACKED_TYPES.has(type)) return typeBytes;
        const components = SOURCE_COMPONENTS[format];
        if (components === undefined) return 0;
        return components * typeBytes;
    }

    function expandBits(value, bits) {
        // Replicating the high bits is GL's rule for widening a packed
        // component, and it is why 5-bit 31 becomes 255 and not 248.
        if (bits >= 8) return (value >>> (bits - 8)) & 0xff;
        let out = value << (8 - bits);
        let filled = bits;
        while (filled < 8) {
            out |= value >>> (bits - Math.min(bits, 8 - filled));
            filled += bits;
        }
        return out & 0xff;
    }

    /*
     * Reads one source texel into out[0..3] as 0..255, applying the source
     * format's channel meaning but not the internal format's expansion.
     */
    function readSourceTexel(view, byteOffset, format, type, out) {
        const packed = PACKED_TYPES.has(type);
        if (packed) {
            let raw;
            switch (TYPE_BYTES[type]) {
            case 1: raw = view.getUint8(byteOffset); break;
            case 2: raw = view.getUint16(byteOffset, true); break;
            default: raw = view.getUint32(byteOffset, true); break;
            }
            switch (type) {
            case GL.UNSIGNED_BYTE_3_3_2:
                out[0] = expandBits((raw >> 5) & 7, 3);
                out[1] = expandBits((raw >> 2) & 7, 3);
                out[2] = expandBits(raw & 3, 2);
                out[3] = 255;
                return;
            case GL.UNSIGNED_BYTE_2_3_3_REV:
                out[0] = expandBits(raw & 7, 3);
                out[1] = expandBits((raw >> 3) & 7, 3);
                out[2] = expandBits((raw >> 6) & 3, 2);
                out[3] = 255;
                return;
            case GL.UNSIGNED_SHORT_5_6_5:
                out[0] = expandBits((raw >> 11) & 31, 5);
                out[1] = expandBits((raw >> 5) & 63, 6);
                out[2] = expandBits(raw & 31, 5);
                out[3] = 255;
                return;
            case GL.UNSIGNED_SHORT_5_6_5_REV:
                out[0] = expandBits(raw & 31, 5);
                out[1] = expandBits((raw >> 5) & 63, 6);
                out[2] = expandBits((raw >> 11) & 31, 5);
                out[3] = 255;
                return;
            case GL.UNSIGNED_SHORT_4_4_4_4:
                out[0] = expandBits((raw >> 12) & 15, 4);
                out[1] = expandBits((raw >> 8) & 15, 4);
                out[2] = expandBits((raw >> 4) & 15, 4);
                out[3] = expandBits(raw & 15, 4);
                return;
            case GL.UNSIGNED_SHORT_4_4_4_4_REV:
                out[0] = expandBits(raw & 15, 4);
                out[1] = expandBits((raw >> 4) & 15, 4);
                out[2] = expandBits((raw >> 8) & 15, 4);
                out[3] = expandBits((raw >> 12) & 15, 4);
                return;
            case GL.UNSIGNED_SHORT_5_5_5_1:
                out[0] = expandBits((raw >> 11) & 31, 5);
                out[1] = expandBits((raw >> 6) & 31, 5);
                out[2] = expandBits((raw >> 1) & 31, 5);
                out[3] = (raw & 1) ? 255 : 0;
                return;
            case GL.UNSIGNED_SHORT_1_5_5_5_REV:
                out[0] = expandBits(raw & 31, 5);
                out[1] = expandBits((raw >> 5) & 31, 5);
                out[2] = expandBits((raw >> 10) & 31, 5);
                out[3] = (raw & 0x8000) ? 255 : 0;
                return;
            case GL.UNSIGNED_INT_8_8_8_8:
                out[0] = (raw >>> 24) & 0xff; out[1] = (raw >>> 16) & 0xff;
                out[2] = (raw >>> 8) & 0xff; out[3] = raw & 0xff;
                return;
            case GL.UNSIGNED_INT_8_8_8_8_REV:
                out[0] = raw & 0xff; out[1] = (raw >>> 8) & 0xff;
                out[2] = (raw >>> 16) & 0xff; out[3] = (raw >>> 24) & 0xff;
                return;
            case GL.UNSIGNED_INT_10_10_10_2:
                out[0] = expandBits((raw >>> 22) & 1023, 10);
                out[1] = expandBits((raw >>> 12) & 1023, 10);
                out[2] = expandBits((raw >>> 2) & 1023, 10);
                out[3] = expandBits(raw & 3, 2);
                return;
            default:  // UNSIGNED_INT_2_10_10_10_REV
                out[0] = expandBits(raw & 1023, 10);
                out[1] = expandBits((raw >>> 10) & 1023, 10);
                out[2] = expandBits((raw >>> 20) & 1023, 10);
                out[3] = expandBits((raw >>> 30) & 3, 2);
                return;
            }
        }

        const typeBytes = TYPE_BYTES[type];
        const components = SOURCE_COMPONENTS[format] || 1;
        const read = i => {
            const at = byteOffset + i * typeBytes;
            switch (type) {
            case GL.UNSIGNED_BYTE: return view.getUint8(at);
            case GL.BYTE: return clamp(view.getInt8(at), 0, 127) * 2;
            case GL.UNSIGNED_SHORT: return view.getUint16(at, true) >>> 8;
            case GL.SHORT: return clamp(view.getInt16(at, true), 0, 32767) >>> 7;
            case GL.UNSIGNED_INT: return view.getUint32(at, true) >>> 24;
            case GL.INT: return clamp(view.getInt32(at, true), 0, 0x7fffffff) >>> 23;
            case GL.FLOAT: return clamp(Math.round(view.getFloat32(at, true) * 255), 0, 255);
            default: return 0;
            }
        };
        const c = [];
        for (let i = 0; i < components; ++i) c.push(read(i));

        switch (format) {
        case GL.RED: out[0] = c[0]; out[1] = 0; out[2] = 0; out[3] = 255; return;
        case GL.GREEN: out[0] = 0; out[1] = c[0]; out[2] = 0; out[3] = 255; return;
        case GL.BLUE: out[0] = 0; out[1] = 0; out[2] = c[0]; out[3] = 255; return;
        case GL.ALPHA: out[0] = 0; out[1] = 0; out[2] = 0; out[3] = c[0]; return;
        case GL.LUMINANCE:
        case GL.INTENSITY:
        case GL.DEPTH_COMPONENT:
        case GL.COLOR_INDEX:
        case GL.STENCIL_INDEX:
            out[0] = c[0]; out[1] = c[0]; out[2] = c[0]; out[3] = 255; return;
        case GL.LUMINANCE_ALPHA:
            out[0] = c[0]; out[1] = c[0]; out[2] = c[0]; out[3] = c[1]; return;
        case GL.RGB: out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = 255; return;
        case GL.BGR: out[0] = c[2]; out[1] = c[1]; out[2] = c[0]; out[3] = 255; return;
        case GL.RGBA: out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = c[3]; return;
        case GL.BGRA: out[0] = c[2]; out[1] = c[1]; out[2] = c[0]; out[3] = c[3]; return;
        case GL.ABGR_EXT: out[0] = c[3]; out[1] = c[2]; out[2] = c[1]; out[3] = c[0]; return;
        default: out[0] = c[0] || 0; out[1] = c[1] || 0; out[2] = c[2] || 0;
                 out[3] = c[3] === undefined ? 255 : c[3]; return;
        }
    }

    /* Applies the internal base format's expansion, in place. */
    function applyBaseFormat(base, texel) {
        switch (base) {
        case "ALPHA":
            texel[0] = 0; texel[1] = 0; texel[2] = 0;
            return;
        case "LUMINANCE":
            texel[1] = texel[0]; texel[2] = texel[0]; texel[3] = 255;
            return;
        case "LUMINANCE_ALPHA":
            texel[1] = texel[0]; texel[2] = texel[0];
            return;
        case "INTENSITY":
            texel[1] = texel[0]; texel[2] = texel[0]; texel[3] = texel[0];
            return;
        case "RGB":
            texel[3] = 255;
            return;
        default:
            return;
        }
    }

    /*
     * Converts a rectangle of guest pixel data into tightly packed RGBA8 rows.
     *
     * The fast path exists because it is the one every game actually uses:
     * GL_RGBA/GL_UNSIGNED_BYTE into an RGBA internal format is a memcpy, and
     * routing a 1024x1024 texture through the per-texel reader instead would
     * be visible as a hitch on every level load.
     */
    function convertPixels(source, byteOffset, width, height, depth,
            format, type, base, unpack) {
        const rowLength = unpack.rowLength || width;
        const skipPixels = unpack.skipPixels || 0;
        const skipRows = unpack.skipRows || 0;
        const skipImages = unpack.skipImages || 0;
        const alignment = unpack.alignment || 4;
        const texelBytes = sourceTexelBytes(format, type);
        if (!texelBytes) return null;
        const srcRowBytes = alignUp(rowLength * texelBytes, alignment);
        const imageHeight = unpack.imageHeight || height;
        const out = new Uint8Array(width * height * depth * 4);
        // The view is relative to `source`, so every offset below is too --
        // mixing in source.byteOffset would double-count it for any subarray.
        const view = new DataView(source.buffer, source.byteOffset,
            source.byteLength);
        const limit = source.byteLength;

        const fastCopy = type === GL.UNSIGNED_BYTE && format === GL.RGBA &&
            base === "RGBA";
        const fastBGRA = type === GL.UNSIGNED_BYTE && format === GL.BGRA &&
            base === "RGBA";
        const fastRGB = type === GL.UNSIGNED_BYTE && format === GL.RGB &&
            (base === "RGB" || base === "RGBA");

        const texel = new Uint8Array(4);
        for (let z = 0; z < depth; ++z) {
            for (let y = 0; y < height; ++y) {
                let src = byteOffset +
                    (skipImages + z) * imageHeight * srcRowBytes +
                    (skipRows + y) * srcRowBytes + skipPixels * texelBytes;
                let dst = ((z * height + y) * width) * 4;
                if (src + width * texelBytes > limit) return null;
                if (fastCopy) {
                    out.set(source.subarray(src, src + width * 4), dst);
                    continue;
                }
                if (fastBGRA) {
                    for (let x = 0; x < width; ++x) {
                        out[dst] = view.getUint8(src + 2);
                        out[dst + 1] = view.getUint8(src + 1);
                        out[dst + 2] = view.getUint8(src);
                        out[dst + 3] = view.getUint8(src + 3);
                        src += 4; dst += 4;
                    }
                    continue;
                }
                if (fastRGB) {
                    for (let x = 0; x < width; ++x) {
                        out[dst] = view.getUint8(src);
                        out[dst + 1] = view.getUint8(src + 1);
                        out[dst + 2] = view.getUint8(src + 2);
                        out[dst + 3] = 255;
                        src += 3; dst += 4;
                    }
                    continue;
                }
                for (let x = 0; x < width; ++x) {
                    readSourceTexel(view, src, format, type, texel);
                    applyBaseFormat(base, texel);
                    out[dst] = texel[0]; out[dst + 1] = texel[1];
                    out[dst + 2] = texel[2]; out[dst + 3] = texel[3];
                    src += texelBytes; dst += 4;
                }
            }
        }
        return out;
    }

    function halfToFloat(value) {
        const sign = (value & 0x8000) ? -1 : 1;
        const exponent = (value >>> 10) & 0x1f;
        const fraction = value & 0x3ff;
        if (!exponent) return sign * Math.pow(2, -14) * (fraction / 1024);
        if (exponent === 31) return fraction ? NaN : sign * Infinity;
        return sign * Math.pow(2, exponent - 15) * (1 + fraction / 1024);
    }

    function floatToHalf(value) {
        if (Number.isNaN(value)) return 0x7e00;
        const sign = value < 0 || Object.is(value, -0) ? 0x8000 : 0;
        const absolute = Math.abs(value);
        if (absolute === Infinity) return sign | 0x7c00;
        if (absolute === 0) return sign;
        let exponent = Math.floor(Math.log2(absolute));
        let mantissa = absolute / Math.pow(2, exponent) - 1;
        if (exponent < -14) {
            const subnormal = Math.round(absolute / Math.pow(2, -24));
            return sign | Math.min(0x3ff, subnormal);
        }
        if (exponent > 15) return sign | 0x7c00;
        let fraction = Math.round(mantissa * 1024);
        if (fraction === 1024) { fraction = 0; ++exponent; }
        if (exponent > 15) return sign | 0x7c00;
        return sign | ((exponent + 15) << 10) | (fraction & 0x3ff);
    }

    function readSourceTexelFloat(view, byteOffset, format, type, out) {
        if (PACKED_TYPES.has(type)) {
            const bytes = new Uint8Array(4);
            readSourceTexel(view, byteOffset, format, type, bytes);
            for (let i = 0; i < 4; ++i) out[i] = bytes[i] / 255;
            return;
        }
        const typeBytes = TYPE_BYTES[type];
        const components = SOURCE_COMPONENTS[format] || 1;
        const read = i => {
            const at = byteOffset + i * typeBytes;
            switch (type) {
            case GL.UNSIGNED_BYTE: return view.getUint8(at) / 255;
            case GL.BYTE: return Math.max(view.getInt8(at) / 127, -1);
            case GL.UNSIGNED_SHORT: return view.getUint16(at, true) / 65535;
            case GL.SHORT: return Math.max(view.getInt16(at, true) / 32767, -1);
            case GL.UNSIGNED_INT: return view.getUint32(at, true) / 4294967295;
            case GL.INT: return Math.max(view.getInt32(at, true) / 2147483647, -1);
            case GL.FLOAT: return view.getFloat32(at, true);
            case GL.HALF_FLOAT: return halfToFloat(view.getUint16(at, true));
            default: return 0;
            }
        };
        const c = [];
        for (let i = 0; i < components; ++i) c.push(read(i));
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
        switch (format) {
        case GL.RED: out[0] = c[0]; break;
        case GL.GREEN: out[1] = c[0]; break;
        case GL.BLUE: out[2] = c[0]; break;
        case GL.ALPHA: out[3] = c[0]; break;
        case GL.LUMINANCE: out[0] = out[1] = out[2] = c[0]; break;
        case GL.INTENSITY: out[0] = out[1] = out[2] = out[3] = c[0]; break;
        case GL.DEPTH_COMPONENT:
        case GL.COLOR_INDEX:
        case GL.STENCIL_INDEX: out[0] = out[1] = out[2] = c[0]; break;
        case GL.LUMINANCE_ALPHA:
            out[0] = out[1] = out[2] = c[0]; out[3] = c[1]; break;
        case GL.RGB: out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; break;
        case GL.BGR: out[0] = c[2]; out[1] = c[1]; out[2] = c[0]; break;
        case GL.RGBA: out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = c[3]; break;
        case GL.BGRA: out[0] = c[2]; out[1] = c[1]; out[2] = c[0]; out[3] = c[3]; break;
        case GL.ABGR_EXT:
            out[0] = c[3]; out[1] = c[2]; out[2] = c[1]; out[3] = c[0]; break;
        default:
            for (let i = 0; i < Math.min(4, c.length); ++i) out[i] = c[i];
        }
    }

    function convertPixelsForStorage(source, byteOffset, width, height, depth,
            format, type, storage, unpack) {
        if (!storage.float && !storage.depth && !storage.palettised) {
            const pixels = convertPixels(source, byteOffset, width, height, depth,
                format, type, storage.base, unpack);
            return pixels ? { pixels, bytesPerTexel: 4, componentType: "u8",
                               componentCount: 4 } : null;
        }
        const texelBytes = sourceTexelBytes(format, type);
        if (!texelBytes) return null;
        const rowLength = unpack.rowLength || width;
        const skipPixels = unpack.skipPixels || 0;
        const skipRows = unpack.skipRows || 0;
        const skipImages = unpack.skipImages || 0;
        const alignment = unpack.alignment || 4;
        const srcRowBytes = alignUp(rowLength * texelBytes, alignment);
        const imageHeight = unpack.imageHeight || height;
        const texels = width * height * depth;
        let bytesPerTexel, componentType, componentCount, result, output;
        if (storage.palettised) {
            bytesPerTexel = 1; componentType = "u8"; componentCount = 1;
            result = new Uint8Array(texels); output = result;
        } else if (storage.gpu === "rgba16float") {
            bytesPerTexel = 8; componentType = "f16"; componentCount = 4;
            result = new Uint8Array(texels * bytesPerTexel);
            output = new Uint16Array(result.buffer);
        } else if (storage.gpu === "rgba32float") {
            bytesPerTexel = 16; componentType = "f32"; componentCount = 4;
            result = new Uint8Array(texels * bytesPerTexel);
            output = new Float32Array(result.buffer);
        } else if (storage.gpu === "depth16unorm") {
            bytesPerTexel = 2; componentType = "u16"; componentCount = 1;
            result = new Uint8Array(texels * bytesPerTexel);
            output = new Uint16Array(result.buffer);
        } else {
            bytesPerTexel = 4; componentType = "f32"; componentCount = 1;
            result = new Uint8Array(texels * bytesPerTexel);
            output = new Float32Array(result.buffer);
        }
        const view = new DataView(source.buffer, source.byteOffset, source.byteLength);
        const texel = new Float32Array(4);
        let dst = 0;
        for (let z = 0; z < depth; ++z) {
            for (let y = 0; y < height; ++y) {
                let src = byteOffset + (skipImages + z) * imageHeight * srcRowBytes +
                    (skipRows + y) * srcRowBytes + skipPixels * texelBytes;
                if (src + width * texelBytes > source.byteLength) return null;
                for (let x = 0; x < width; ++x) {
                    readSourceTexelFloat(view, src, format, type, texel);
                    if (storage.palettised) {
                        output[dst++] = clamp(Math.round(texel[0] * 255), 0, 255);
                    } else if (componentCount === 1) {
                        const value = clamp(texel[0], 0, 1);
                        output[dst++] = componentType === "u16" ?
                            Math.round(value * 65535) : value;
                    } else {
                        for (let c = 0; c < 4; ++c)
                            output[dst++] = componentType === "f16" ?
                                floatToHalf(texel[c]) : texel[c];
                    }
                    src += texelBytes;
                }
            }
        }
        return { pixels: result, bytesPerTexel, componentType, componentCount };
    }

    /* ---- S3TC decode, used when the adapter has no BC support ---- */

    function decodeDXTColorBlock(src, at, out, outStride, x0, y0, width, height,
            dxt1Alpha) {
        const c0 = src[at] | (src[at + 1] << 8);
        const c1 = src[at + 2] | (src[at + 3] << 8);
        const palette = new Uint8Array(16);
        const unpack565 = (c, i) => {
            palette[i] = expandBits((c >> 11) & 31, 5);
            palette[i + 1] = expandBits((c >> 5) & 63, 6);
            palette[i + 2] = expandBits(c & 31, 5);
            palette[i + 3] = 255;
        };
        unpack565(c0, 0);
        unpack565(c1, 4);
        if (c0 > c1 || !dxt1Alpha) {
            for (let k = 0; k < 3; ++k) {
                palette[8 + k] = (2 * palette[k] + palette[4 + k] + 1) / 3 | 0;
                palette[12 + k] = (palette[k] + 2 * palette[4 + k] + 1) / 3 | 0;
            }
            palette[11] = 255; palette[15] = 255;
        } else {
            for (let k = 0; k < 3; ++k)
                palette[8 + k] = (palette[k] + palette[4 + k]) >> 1;
            palette[11] = 255;
            palette[12] = 0; palette[13] = 0; palette[14] = 0; palette[15] = 0;
        }
        const bits = src[at + 4] | (src[at + 5] << 8) | (src[at + 6] << 16) |
            (src[at + 7] << 24);
        for (let y = 0; y < 4; ++y) {
            for (let x = 0; x < 4; ++x) {
                const px = x0 + x, py = y0 + y;
                if (px >= width || py >= height) continue;
                const index = (bits >>> (2 * (4 * y + x))) & 3;
                const o = py * outStride + px * 4;
                out[o] = palette[index * 4];
                out[o + 1] = palette[index * 4 + 1];
                out[o + 2] = palette[index * 4 + 2];
                out[o + 3] = palette[index * 4 + 3];
            }
        }
    }

    function decodeDXT(dxt, src, width, height) {
        const out = new Uint8Array(width * height * 4);
        const stride = width * 4;
        const blockBytes = dxt === 1 ? 8 : 16;
        const blocksX = Math.ceil(width / 4);
        const blocksY = Math.ceil(height / 4);
        let at = 0;
        for (let by = 0; by < blocksY; ++by) {
            for (let bx = 0; bx < blocksX; ++bx) {
                const x0 = bx * 4, y0 = by * 4;
                if (dxt === 1) {
                    decodeDXTColorBlock(src, at, out, stride, x0, y0,
                        width, height, true);
                } else {
                    decodeDXTColorBlock(src, at + 8, out, stride, x0, y0,
                        width, height, false);
                    if (dxt === 3) {
                        for (let y = 0; y < 4; ++y) {
                            for (let x = 0; x < 4; ++x) {
                                const px = x0 + x, py = y0 + y;
                                if (px >= width || py >= height) continue;
                                const nibble = at + y * 2 + (x >> 1);
                                const value = (x & 1) ? (src[nibble] >> 4) :
                                    (src[nibble] & 15);
                                out[py * stride + px * 4 + 3] = expandBits(value, 4);
                            }
                        }
                    } else {
                        const a0 = src[at], a1 = src[at + 1];
                        const alpha = new Uint8Array(8);
                        alpha[0] = a0; alpha[1] = a1;
                        if (a0 > a1) {
                            for (let k = 1; k < 7; ++k)
                                alpha[k + 1] = ((7 - k) * a0 + k * a1 + 3) / 7 | 0;
                        } else {
                            for (let k = 1; k < 5; ++k)
                                alpha[k + 1] = ((5 - k) * a0 + k * a1 + 2) / 5 | 0;
                            alpha[6] = 0; alpha[7] = 255;
                        }
                        let low = src[at + 2] | (src[at + 3] << 8) |
                            (src[at + 4] << 16);
                        let high = src[at + 5] | (src[at + 6] << 8) |
                            (src[at + 7] << 16);
                        for (let y = 0; y < 4; ++y) {
                            for (let x = 0; x < 4; ++x) {
                                const px = x0 + x, py = y0 + y;
                                if (px >= width || py >= height) continue;
                                const bit = 3 * (4 * y + x);
                                const value = bit < 24 ?
                                    (low >>> bit) & 7 :
                                    (high >>> (bit - 24)) & 7;
                                out[py * stride + px * 4 + 3] = alpha[value];
                            }
                        }
                    }
                }
                at += blockBytes;
            }
        }
        return out;
    }

    /* ================================================================== */
    /* GL context state                                                   */
    /* ================================================================== */

    function cloneStateValue(value) {
        if (value === null || value === undefined || typeof value !== "object")
            return value;
        if (ArrayBuffer.isView(value)) return new value.constructor(value);
        if (value instanceof Set) return new Set(value);
        if (Array.isArray(value)) return value.map(cloneStateValue);
        const copy = Object.create(Object.getPrototypeOf(value) === null ? null :
            Object.prototype);
        for (const key of Object.keys(value)) copy[key] = cloneStateValue(value[key]);
        return copy;
    }

    function snapshotFields(state, fields) {
        const snapshot = Object.create(null);
        for (const field of fields) snapshot[field] = cloneStateValue(state[field]);
        return snapshot;
    }

    function restoreFields(state, snapshot, fields) {
        for (const field of fields)
            if (Object.prototype.hasOwnProperty.call(snapshot, field))
                state[field] = cloneStateValue(snapshot[field]);
    }

    const SERVER_ATTRIB_FIELDS = [
        [ATTRIB.CURRENT, ["current"]],
        [ATTRIB.POINT, ["pointSize", "point"]],
        [ATTRIB.LINE, ["lineWidth", "lineStipple"]],
        [ATTRIB.POLYGON, ["cullFace", "frontFace", "polygonMode", "polygonOffset"]],
        [ATTRIB.POLYGON_STIPPLE, ["polygonStipple"]],
        [ATTRIB.PIXEL_MODE, ["pixelTransfer", "pixelZoom", "readBuffer"]],
        [ATTRIB.LIGHTING, ["lightModel", "lights", "material", "colorMaterial",
                           "shadeModel"]],
        [ATTRIB.FOG, ["fog"]],
        [ATTRIB.DEPTH_BUFFER, ["depthFunc", "depthMask", "clearDepth"]],
        [ATTRIB.ACCUM_BUFFER, ["clearAccum"]],
        [ATTRIB.STENCIL_BUFFER, ["stencil", "clearStencil"]],
        [ATTRIB.VIEWPORT, ["viewport", "depthRange"]],
        [ATTRIB.TRANSFORM, ["matrixMode", "clipPlanes"]],
        [ATTRIB.COLOR_BUFFER, ["clearColor", "colorMask", "blend", "alphaFunc",
                              "logicOp", "drawBuffers"]],
        [ATTRIB.TEXTURE, ["activeTexture", "textureUnits"]],
        [ATTRIB.SCISSOR, ["scissor"]],
        [ATTRIB.MULTISAMPLE, ["sampleCoverage"]],
    ];

    function fieldsForServerAttrib(mask) {
        const fields = new Set();
        for (const [bit, names] of SERVER_ATTRIB_FIELDS)
            if (mask & bit) for (const name of names) fields.add(name);
        if (mask & ATTRIB.ENABLE) fields.add("enabled");
        return [...fields];
    }

    function captureEnableState(state) {
        return {
            enabled: new Set(state.enabled),
            textureTargets: state.textureUnits.map(unit => new Set(unit.enabledTargets)),
            texGen: state.textureUnits.map(unit => unit.texGen.map(gen => gen.enabled)),
            lights: state.lights.map(light => light.enabled),
            colorMaterial: state.colorMaterial.enabled,
        };
    }

    function restoreEnableState(state, snapshot) {
        state.enabled = new Set(snapshot.enabled);
        state.textureUnits.forEach((unit, i) => {
            unit.enabledTargets = new Set(snapshot.textureTargets[i]);
            unit.texGen.forEach((gen, j) => { gen.enabled = snapshot.texGen[i][j]; });
        });
        state.lights.forEach((light, i) => { light.enabled = snapshot.lights[i]; });
        state.colorMaterial.enabled = snapshot.colorMaterial;
    }

    /*
     * One of these per wglCreateContext. Everything the GL spec calls "server
     * state" lives here, at its documented default -- and the defaults matter:
     * a game that never calls glDepthFunc expects GL_LESS, and a wrong default
     * shows up as a subtly wrong picture in exactly the titles that touch the
     * least state.
     */
    function createContextState(id, shareGroup) {
        const state = {
            id, shareGroup,
            error: GL.NO_ERROR,
            errorQueue: [],
            arbErrorString: "",
            arbErrorPosition: -1,

            matrixMode: GL.MODELVIEW,
            stacks: {
                [GL.MODELVIEW]: [identity4()],
                [GL.PROJECTION]: [identity4()],
                [GL.COLOR]: [identity4()],
            },
            textureStacks: Array.from({ length: MAX_TEXTURE_COORDS },
                () => [identity4()]),

            current: {
                color: new Float32Array([1, 1, 1, 1]),
                secondaryColor: new Float32Array([0, 0, 0, 1]),
                normal: new Float32Array([0, 0, 1]),
                fogCoord: 0,
                edgeFlag: true,
                texCoord: Array.from({ length: MAX_TEXTURE_COORDS },
                    () => new Float32Array([0, 0, 0, 1])),
                rasterPos: new Float32Array([0, 0, 0, 1]),
                rasterValid: true,
            },

            enabled: new Set(),

            viewport: { x: 0, y: 0, width: 0, height: 0 },
            depthRange: { near: 0, far: 1 },
            scissor: { x: 0, y: 0, width: 0, height: 0, set: false },

            clearColor: new Float32Array([0, 0, 0, 0]),
            clearDepth: 1,
            clearStencil: 0,
            clearAccum: new Float32Array([0, 0, 0, 0]),

            depthFunc: GL.LESS,
            depthMask: true,
            colorMask: [true, true, true, true],
            stencil: {
                front: { func: GL.ALWAYS, ref: 0, valueMask: 0xffffffff,
                         writeMask: 0xffffffff, fail: GL.KEEP,
                         zfail: GL.KEEP, zpass: GL.KEEP },
                back: { func: GL.ALWAYS, ref: 0, valueMask: 0xffffffff,
                        writeMask: 0xffffffff, fail: GL.KEEP,
                        zfail: GL.KEEP, zpass: GL.KEEP },
            },
            blend: {
                srcRGB: GL.ONE, dstRGB: GL.ZERO,
                srcAlpha: GL.ONE, dstAlpha: GL.ZERO,
                equationRGB: GL.FUNC_ADD, equationAlpha: GL.FUNC_ADD,
                color: new Float32Array([0, 0, 0, 0]),
            },
            alphaFunc: { func: GL.ALWAYS, ref: 0 },
            logicOp: GL.COPY,
            cullFace: GL.BACK,
            frontFace: GL.CCW,
            polygonMode: { front: GL.FILL, back: GL.FILL },
            polygonOffset: { factor: 0, units: 0 },
            shadeModel: GL.SMOOTH,
            lineWidth: 1,
            lineStipple: { pattern: 0xffff, factor: 1 },
            polygonStipple: new Uint8Array(128).fill(0xff),
            pointSize: 1,
            point: {
                size: 1, sizeMin: 0, sizeMax: 64, fadeThreshold: 1,
                attenuation: new Float32Array([1, 0, 0]),
                spriteCoordOrigin: GL.UPPER_LEFT,
            },
            sampleCoverage: { value: 1, invert: false },

            lightModel: {
                ambient: new Float32Array([0.2, 0.2, 0.2, 1]),
                twoSide: false,
                localViewer: false,
                colorControl: GL.SINGLE_COLOR,
            },
            lights: Array.from({ length: MAX_LIGHTS }, (unused, i) => ({
                enabled: false,
                ambient: new Float32Array([0, 0, 0, 1]),
                diffuse: new Float32Array(i === 0 ? [1, 1, 1, 1] : [0, 0, 0, 1]),
                specular: new Float32Array(i === 0 ? [1, 1, 1, 1] : [0, 0, 0, 1]),
                position: new Float32Array([0, 0, 1, 0]),
                spotDirection: new Float32Array([0, 0, -1]),
                spotExponent: 0,
                spotCutoff: 180,
                constantAttenuation: 1,
                linearAttenuation: 0,
                quadraticAttenuation: 0,
                // Eye-space copies, refreshed when the light or the modelview
                // changes: GL specifies that glLight* captures the modelview in
                // force at the time of the call.
                eyePosition: new Float32Array([0, 0, 1, 0]),
                eyeSpotDirection: new Float32Array([0, 0, -1]),
            })),
            material: {
                front: newMaterial(), back: newMaterial(),
            },
            colorMaterial: { enabled: false, face: GL.FRONT_AND_BACK,
                             mode: GL.AMBIENT_AND_DIFFUSE },

            fog: {
                mode: GL.EXP, density: 1, start: 0, end: 1,
                color: new Float32Array([0, 0, 0, 0]),
                coordSource: GL.FRAGMENT_DEPTH,
            },

            clipPlanes: Array.from({ length: MAX_CLIP_PLANES },
                () => new Float32Array([0, 0, 0, 0])),

            activeTexture: 0,
            clientActiveTexture: 0,
            textureUnits: Array.from({ length: MAX_TEXTURE_UNITS }, () => ({
                bindings: Object.create(null),   // target -> texture name
                enabledTargets: new Set(),
                env: newTexEnv(),
                texGen: Array.from({ length: 4 }, (unused, i) => ({
                    enabled: false,
                    mode: GL.EYE_LINEAR,
                    objectPlane: new Float32Array(
                        i === 0 ? [1, 0, 0, 0] : (i === 1 ? [0, 1, 0, 0] : [0, 0, 0, 0])),
                    eyePlane: new Float32Array(
                        i === 0 ? [1, 0, 0, 0] : (i === 1 ? [0, 1, 0, 0] : [0, 0, 0, 0])),
                })),
                lodBias: 0,
            })),

            pixelStore: {
                unpackAlignment: 4, unpackRowLength: 0, unpackSkipPixels: 0,
                unpackSkipRows: 0, unpackSkipImages: 0, unpackImageHeight: 0,
                unpackSwapBytes: false, unpackLsbFirst: false,
                packAlignment: 4, packRowLength: 0, packSkipPixels: 0,
                packSkipRows: 0, packSwapBytes: false, packLsbFirst: false,
            },
            pixelTransfer: { redScale: 1, greenScale: 1, blueScale: 1,
                             alphaScale: 1, redBias: 0, greenBias: 0,
                             blueBias: 0, alphaBias: 0, mapColor: false,
                             postColorMatrixScale:
                                new Float32Array([1, 1, 1, 1]),
                             postColorMatrixBias:
                                new Float32Array([0, 0, 0, 0]) },
            pixelZoom: { x: 1, y: 1 },

            arrayBuffer: 0,
            elementArrayBuffer: 0,
            pixelPackBuffer: 0,
            pixelUnpackBuffer: 0,
            currentProgram: 0,
            arbVertexProgram: 0,
            arbFragmentProgram: 0,

            drawFramebuffer: 0,
            readFramebuffer: 0,
            renderbuffer: 0,
            drawBuffers: [GL.BACK],
            readBuffer: GL.BACK,

            activeQueries: Object.create(null),

            // Immediate mode
            immediate: null,
            renderMode: GL.RENDER,

            // Generic vertex attributes (GL 2.0)
            genericAttribs: Array.from({ length: MAX_VERTEX_ATTRIBS }, () => ({
                enabled: false,
                value: new Float32Array([0, 0, 0, 1]),
            })),

            attribStack: [],
            clientAttribStack: [],
        };
        // GL's default enables. Everything else starts off.
        state.enabled.add(GL.DITHER);
        state.enabled.add(GL.MULTISAMPLE);
        return state;
    }

    function newMaterial() {
        return {
            ambient: new Float32Array([0.2, 0.2, 0.2, 1]),
            diffuse: new Float32Array([0.8, 0.8, 0.8, 1]),
            specular: new Float32Array([0, 0, 0, 1]),
            emission: new Float32Array([0, 0, 0, 1]),
            shininess: 0,
        };
    }

    function newTexEnv() {
        return {
            mode: GL.MODULATE,
            color: new Float32Array([0, 0, 0, 0]),
            combineRGB: GL.MODULATE, combineAlpha: GL.MODULATE,
            srcRGB: [GL.TEXTURE, GL.PREVIOUS, GL.CONSTANT],
            srcAlpha: [GL.TEXTURE, GL.PREVIOUS, GL.CONSTANT],
            operandRGB: [GL.SRC_COLOR, GL.SRC_COLOR, GL.SRC_ALPHA],
            operandAlpha: [GL.SRC_ALPHA, GL.SRC_ALPHA, GL.SRC_ALPHA],
            rgbScale: 1, alphaScale: 1,
            lodBias: 0,
            pointSprite: false,
        };
    }

    /* Names the fixed-function generator understands, from GL enums. */
    const TEXENV_MODE_NAMES = {
        [GL.REPLACE]: "REPLACE", [GL.MODULATE]: "MODULATE",
        [GL.DECAL]: "DECAL", [GL.BLEND]: "BLEND", [GL.ADD]: "ADD",
        [GL.COMBINE]: "COMBINE",
    };
    const COMBINE_RGB_NAMES = {
        [GL.REPLACE]: "REPLACE", [GL.MODULATE]: "MODULATE", [GL.ADD]: "ADD",
        [GL.ADD_SIGNED]: "ADD_SIGNED", [GL.INTERPOLATE]: "INTERPOLATE",
        [GL.SUBTRACT]: "SUBTRACT",
        [GL.DOT3_RGB]: "DOT3_RGB", [GL.DOT3_RGBA]: "DOT3_RGBA",
    };
    const COMBINE_SOURCE_NAMES = {
        [GL.TEXTURE]: "TEXTURE", [GL.CONSTANT]: "CONSTANT",
        [GL.PRIMARY_COLOR]: "PRIMARY_COLOR", [GL.PREVIOUS]: "PREVIOUS",
    };
    for (let i = 0; i < MAX_TEXTURE_UNITS; ++i)
        COMBINE_SOURCE_NAMES[GL.TEXTURE0 + i] = "TEXTURE" + i;
    const OPERAND_NAMES = {
        [GL.SRC_COLOR]: "SRC_COLOR",
        [GL.ONE_MINUS_SRC_COLOR]: "ONE_MINUS_SRC_COLOR",
        [GL.SRC_ALPHA]: "SRC_ALPHA",
        [GL.ONE_MINUS_SRC_ALPHA]: "ONE_MINUS_SRC_ALPHA",
    };
    const TEXGEN_MODE_NAMES = {
        [GL.OBJECT_LINEAR]: "OBJECT", [GL.EYE_LINEAR]: "EYE",
        [GL.SPHERE_MAP]: "SPHERE", [GL.REFLECTION_MAP]: "REFLECTION",
        [GL.NORMAL_MAP]: "NORMAL",
    };
    const COLOR_MATERIAL_MODES = {
        [GL.EMISSION]: "EMISSION", [GL.AMBIENT]: "AMBIENT",
        [GL.DIFFUSE]: "DIFFUSE", [GL.SPECULAR]: "SPECULAR",
        [GL.AMBIENT_AND_DIFFUSE]: "AMBIENT_AND_DIFFUSE",
    };
    const FACE_NAMES = {
        [GL.FRONT]: "FRONT", [GL.BACK]: "BACK",
        [GL.FRONT_AND_BACK]: "FRONT_AND_BACK",
    };

    /* The GL_TEXTURE_* target a unit samples, in GL's own priority order:
     * when several targets are enabled on one unit, cube beats 3D beats 2D
     * beats 1D. */
    const TARGET_PRIORITY = [
        [GL.TEXTURE_CUBE_MAP, "Cube"],
        [GL.TEXTURE_3D, "3D"],
        [GL.TEXTURE_2D, "2D"],
        [GL.TEXTURE_1D, "1D"],
    ];

    /* ================================================================== */
    /* Resources                                                          */
    /* ================================================================== */

    /*
     * A share group owns the objects wglShareLists makes common: textures,
     * buffers, display lists, shaders, programs and renderbuffers. Framebuffer
     * objects, vertex array state and query objects are per context, which is
     * what desktop GL specifies -- and getting that split wrong is how a
     * capability-probe context ends up destroying the real one's state, the
     * failure the old bridge's README documented as unfixed.
     */
    function createShareGroup(id) {
        return {
            id,
            textures: new Map(),
            buffers: new Map(),
            shaders: new Map(),
            programs: new Map(),
            renderbuffers: new Map(),
            arbPrograms: new Map(),
            nextName: { texture: 1, buffer: 1, shader: 1, renderbuffer: 1,
                        arbProgram: 1 },
        };
    }

    function createTexture(name) {
        return {
            name,
            target: 0,
            levels: [],            // per level: {width, height, depth, bytes, format}
            gpuTexture: null,
            gpuFormat: "rgba8unorm",
            baseFormat: "RGBA",
            width: 0, height: 0, depth: 1, layers: 1,
            levelCount: 0,
            dirty: true,
            viewCache: new Map(),
            sampler: {
                minFilter: GL.NEAREST_MIPMAP_LINEAR,
                magFilter: GL.LINEAR,
                wrapS: GL.REPEAT, wrapT: GL.REPEAT, wrapR: GL.REPEAT,
                minLod: -1000, maxLod: 1000,
                baseLevel: 0, maxLevel: 1000,
                lodBias: 0,
                maxAnisotropy: 1,
                borderColor: new Float32Array([0, 0, 0, 0]),
                compareMode: GL.NONE, compareFunc: GL.LEQUAL,
                depthTextureMode: GL.LUMINANCE,
            },
            generateMipmapHint: false,
            immutableSampleCount: 1,
        };
    }

    function createBuffer(name) {
        return { name, size: 0, usage: GL.STATIC_DRAW, gpuBuffer: null,
                 shadow: null, target: 0, mapped: false };
    }

    function createProgram(name) {
        return {
            name, shaders: new Set(),
            linked: false, log: "",
            bindAttribLocations: new Map(),
            compiled: { vertex: null, fragment: null },
            link: null,                 // translator link result
            variants: new Map(),        // variant key -> {link, modules}
            uniformData: null,          // Uint8Array mirroring the block
            uniformByName: new Map(),
            uniformByLocation: new Map(),
            samplerUnits: new Map(),    // sampler name -> texture unit
            serial: 1,
            validateLog: "",
        };
    }

    function createFramebuffer(name) {
        return {
            name,
            color: new Array(MAX_DRAW_BUFFERS).fill(null),
            depth: null, stencil: null,
            width: 0, height: 0,
        };
    }

    function createRenderbuffer(name) {
        return { name, width: 0, height: 0, internalFormat: GL.RGBA4,
                 samples: 1, gpuTexture: null, gpuFormat: "rgba8unorm" };
    }

    /* ================================================================== */
    /* Executor                                                           */
    /* ================================================================== */

    class GLWebGPUExecutor {
        constructor(canvas, options) {
            this.canvas = canvas;
            this.options = options || {};
            this.host = this.options.host ||
                (gpuHost ? gpuHost.acquire(canvas, this.options.hostOptions) : null);
            // Mirrored for backwards-compatible diagnostics; query paths use
            // the per-HGLRC fields in createContextState.
            this.arbErrorString = "";
            this.arbErrorPosition = -1;
            this.device = null;
            this.deviceFeatures = { bc: false };
            this.presenterToken = 0;

            this.shareGroups = new Map();
            this.contexts = new Map();
            this.nextContextId = 1;
            this.nextShareGroupId = 1;
            this.current = null;

            this.framebuffers = new Map();  // per context id -> Map(name -> fbo)
            this.queries = new Map();

            this.pipelineCache = new Map();
            this.bindGroupCache = new Map();
            this.samplerCache = new Map();
            this.moduleCache = new Map();
            this.shaderCache = new Map();   // link cache key -> link result
            this.indexExpansionCache = new Map();

            this.backBuffer = null;
            this.backBufferView = null;
            this.depthTarget = null;
            this.backBufferWidth = 0;
            this.backBufferHeight = 0;

            this.encoder = null;
            this.pass = null;
            this.passTarget = null;
            this.recordedOps = 0;
            this.flushThreshold = Math.max(1024, this.options.flushThreshold || 16384);

            this.uniformRing = null;
            this.uniformStaging = null;
            this.uniformCursor = 0;
            this.uniformCapacity = Math.max(64 * 1024,
                this.options.uniformRingBytes || UNIFORM_RING_BYTES);
            this.vertexRing = null;
            this.vertexStaging = null;
            this.vertexCursor = 0;
            this.vertexCapacity = Math.max(256 * 1024,
                this.options.vertexRingBytes || VERTEX_RING_BYTES);

            this.pending = [];
            this.busy = false;
            this.heartbeat = 0;
            this.warned = Object.create(null);
            this.stats = {
                batches: 0, commands: 0, draws: 0, immediateBatches: 0,
                pipelines: 0, bindGroups: 0, shaderLinks: 0, shaderCacheHits: 0,
                textureUploads: 0, textureBytes: 0,
                refusals: 0, refusalsByOp: Object.create(null),
                expandedIndices: 0, dxtDecodes: 0,
                nonUniformSamples: 0,
            };

            this.surface = { hwnd: 0, x: 0, y: 0, width: 0, height: 0,
                             visible: false };
            this.handlers = buildHandlerTable();
            installResourceHandlers(this.handlers);
            installProgramHandlers(this.handlers);
            installDrawHandlers(this.handlers);
            this.args = new Float64Array(32);
        }

        /* ---- lifecycle ---- */

        initialize() {
            if (this.readyPromise) return this.readyPromise;
            this.readyPromise = (async () => {
                if (!this.host) throw new Error("no WebGPU host available");
                await this.host.initialize();
                this.device = this.host.device;
                this.deviceFeatures = this.host.deviceFeatures ||
                    { bc: false };
                this.limits = this.host.limits || {};
                this.host.onDeviceLost(() => this.onDeviceLost());
                this.uniformRing = this.device.createBuffer({
                    label: "GL uniform ring",
                    size: this.uniformCapacity,
                    usage: BUFFER_USAGE_UNIFORM | BUFFER_USAGE_COPY_DST,
                });
                this.uniformStaging = new Uint8Array(this.uniformCapacity);
                this.vertexRing = this.device.createBuffer({
                    label: "GL vertex ring",
                    size: this.vertexCapacity,
                    usage: BUFFER_USAGE_VERTEX | BUFFER_USAGE_INDEX |
                        BUFFER_USAGE_COPY_DST,
                });
                this.vertexStaging = new Uint8Array(this.vertexCapacity);
                this.fallbackTexture = this.device.createTexture({
                    label: "GL incomplete-texture fallback",
                    size: { width: 1, height: 1, depthOrArrayLayers: 1 },
                    format: "rgba8unorm",
                    usage: TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
                });
                // GL samples an incomplete texture as opaque black, not white:
                // the D3D9 path's white fallback would make missing textures
                // invisible instead of obvious.
                this.device.queue.writeTexture({ texture: this.fallbackTexture },
                    new Uint8Array([0, 0, 0, 255]),
                    { bytesPerRow: 4, rowsPerImage: 1 },
                    { width: 1, height: 1, depthOrArrayLayers: 1 });
                this.fallbackView = this.fallbackTexture.createView();
                this.fallbackSampler = this.device.createSampler({});
                this.fallbackComparisonSampler = this.device.createSampler({
                    compare: "less-equal",
                });
                return this;
            })().catch(error => {
                this.failed = error;
                console.error("[gl-webgpu] initialization failed", error);
                throw error;
            });
            return this.readyPromise;
        }

        onDeviceLost() {
            // Every GPU object belonged to the old device. The GL state machine
            // does not: it is plain JavaScript, so the resources rebuild from
            // the shadow copies the next time each is used.
            this.device = this.host.device;
            this.pipelineCache.clear();
            if (this.pixelPipelineCache) this.pixelPipelineCache.clear();
            if (this.accumPipelineCache) this.accumPipelineCache.clear();
            if (this.accumClearPipelineCache) this.accumClearPipelineCache.clear();
            if (this.clearPipelineCache) this.clearPipelineCache.clear();
            this.bindGroupCache.clear();
            this.samplerCache.clear();
            this.moduleCache.clear();
            for (const group of this.shareGroups.values()) {
                for (const texture of group.textures.values()) {
                    texture.gpuTexture = null;
                    texture.viewCache.clear();
                    texture.dirty = true;
                }
                for (const buffer of group.buffers.values())
                    buffer.gpuBuffer = null;
                for (const program of group.programs.values())
                    program.variants.clear();
            }
            this.backBuffer = null;
            this.depthTarget = null;
            this.accumBuffer = null;
            this.uniformRing = null;
            this.vertexRing = null;
            this.readyPromise = null;
            return this.initialize();
        }

        /* ---- diagnostics ---- */

        warnOnce(key, message, details) {
            if (this.warned[key]) return;
            this.warned[key] = true;
            console.warn("[gl-webgpu] " + message, details || {});
        }

        /*
         * A command the host cannot execute is reported once with enough
         * context to find it, counted, and -- where GL defines an error for the
         * situation -- reflected in glGetError. Silently ignoring it is what
         * made the D3D8 and DirectDraw migrations expensive to debug.
         */
        refuse(op, reason, details, glError) {
            ++this.stats.refusals;
            const key = op + ":" + reason;
            this.stats.refusalsByOp[key] = (this.stats.refusalsByOp[key] || 0) + 1;
            if (glError && this.current) this.setError(glError);
            if (this.warned[key]) return;
            this.warned[key] = true;
            console.error("[gl-webgpu] refused " + op + ": " + reason,
                details || {});
        }

        setError(code) {
            const state = this.current;
            if (!state) return;
            // GL keeps the first error until it is read, which is what makes
            // glGetError usable for bisecting a bad call.
            if (state.error === GL.NO_ERROR) state.error = code;
        }

        getStats() {
            return { ...this.stats,
                contexts: this.contexts.size,
                pipelines: this.pipelineCache.size,
                bindGroups: this.bindGroupCache.size,
                shaders: this.shaderCache.size };
        }

        /* ---- context management ---- */

        makeCurrent(payload) {
            const view = new DataView(payload.buffer, payload.byteOffset,
                payload.byteLength);
            const hwnd = payload.byteLength >= 4 ? view.getUint32(0, true) : 0;
            const x = payload.byteLength >= 8 ? view.getInt32(4, true) : 0;
            const y = payload.byteLength >= 12 ? view.getInt32(8, true) : 0;
            const width = payload.byteLength >= 16 ? view.getUint32(12, true) : 0;
            const height = payload.byteLength >= 20 ? view.getUint32(16, true) : 0;
            const protocolId = payload.byteLength >= 24 ? view.getUint32(20, true) : 0;
            const protocolShareGroup = payload.byteLength >= 28 ?
                view.getUint32(24, true) : 0;
            const contextKey = protocolId ? "context:" + protocolId : "hwnd:" + hwnd;

            let context = this.contexts.get(contextKey);
            if (!context) {
                const groupKey = protocolShareGroup ?
                    "guest:" + protocolShareGroup : "private:" + this.nextShareGroupId;
                let group = this.shareGroups.get(groupKey);
                if (!group) {
                    group = createShareGroup(this.nextShareGroupId++);
                    group.protocolId = protocolShareGroup;
                    this.shareGroups.set(groupKey, group);
                }
                context = createContextState(this.nextContextId++, group);
                context.hwnd = hwnd;
                context.protocolId = protocolId;
                context.contextKey = contextKey;
                context.shareGroupKey = groupKey;
                this.contexts.set(contextKey, context);
                this.framebuffers.set(context.id, new Map());
            }
            context.hwnd = hwnd;
            this.current = context;
            if (width && height) {
                if (!context.viewport.width) {
                    context.viewport = { x: 0, y: 0, width, height };
                    context.scissor = { x: 0, y: 0, width, height, set: false };
                }
                this.resizeSurface(width, height);
            }
            this.surface = { hwnd, x, y, width, height, visible: true };
            this.notifySurface("current");
        }

        releaseCurrent() {
            this.finishFrame(false);
            this.current = null;
        }

        destroyContext(payload) {
            this.finishFrame(false);
            let targets;
            if (payload && payload.byteLength >= 4) {
                const id = new DataView(payload.buffer, payload.byteOffset,
                    payload.byteLength).getUint32(0, true);
                const context = this.contexts.get("context:" + id);
                targets = context ? [context] : [];
            } else {
                // A payload-less destroy is process teardown from an older guest.
                targets = [...this.contexts.values()];
            }
            for (const context of targets) {
                this.contexts.delete(context.contextKey || ("hwnd:" + context.hwnd));
                this.framebuffers.delete(context.id);
                if (this.current === context) this.current = null;
            }
            for (const [key, group] of this.shareGroups) {
                const live = [...this.contexts.values()].some(context =>
                    context.shareGroup === group);
                if (!live) {
                    for (const texture of group.textures.values())
                        this.retire(texture.gpuTexture);
                    for (const buffer of group.buffers.values())
                        this.retire(buffer.gpuBuffer);
                    for (const rb of group.renderbuffers.values())
                        this.retire(rb.gpuTexture);
                    this.shareGroups.delete(key);
                }
            }
            if (!this.current) {
                this.surface = { ...this.surface, visible: false };
                this.notifySurface("hide");
            }
        }

        resetForReplay() {
            this.finishFrame(false);
            const release = object => {
                if (!object || typeof object.destroy !== "function") return;
                try { object.destroy(); } catch (ignored) { /* already gone */ }
            };
            for (const group of this.shareGroups.values()) {
                for (const texture of group.textures.values())
                    release(texture.gpuTexture);
                for (const buffer of group.buffers.values())
                    release(buffer.gpuBuffer);
                for (const rb of group.renderbuffers.values())
                    release(rb.gpuTexture);
            }
            release(this.backBuffer);
            release(this.depthTarget);
            if (this.accumBuffer) {
                release(this.accumBuffer.textures && this.accumBuffer.textures[0]);
                release(this.accumBuffer.textures && this.accumBuffer.textures[1]);
            }
            for (const texture of this.logicTargetTextures || []) release(texture);
            this.releaseRetired();

            this.shareGroups.clear();
            this.contexts.clear();
            this.framebuffers.clear();
            this.queries.clear();
            this.nextContextId = 1;
            this.nextShareGroupId = 1;
            this.current = null;
            this.pipelineCache.clear();
            this.bindGroupCache.clear();
            this.samplerCache.clear();
            this.moduleCache.clear();
            this.shaderCache.clear();
            this.indexExpansionCache.clear();
            if (this.ffCache) this.ffCache.clear();
            if (this.pixelPipelineCache) this.pixelPipelineCache.clear();
            if (this.accumPipelineCache) this.accumPipelineCache.clear();
            if (this.accumClearPipelineCache) this.accumClearPipelineCache.clear();
            if (this.clearPipelineCache) this.clearPipelineCache.clear();
            this.backBuffer = null;
            this.backBufferView = null;
            this.depthTarget = null;
            this.depthTargetView = null;
            this.accumBuffer = null;
            this.logicTargetTextures = [];
            this.logicTargetViews = [];
            this.pendingClear = null;
            this.vertexCursor = 0;
            this.uniformCursor = 0;
            this.surface = { hwnd: 0, x: 0, y: 0, width: 0, height: 0,
                             visible: false };
            this.notifySurface("hide");
        }

        notifySurface(reason) {
            const callback = this.options.onSurface;
            if (typeof callback === "function")
                callback({ ...this.surface, sessionKey: "gl" }, reason);
        }

        resizeSurface(width, height) {
            if (width === this.backBufferWidth && height === this.backBufferHeight)
                return;
            this.backBufferWidth = width;
            this.backBufferHeight = height;
            this.backBuffer = null;
            this.depthTarget = null;
            if (this.host) this.host.resizeCanvas(width, height);
        }

        /* ---- batch execution ---- */

        /*
         * Synchronous by construction.
         *
         * The guest is blocked inside DeviceIoControl while its port write runs
         * this code, and it reads the answer to glGetError, glGetIntegerv or
         * glGetUniformLocation straight out of the record as soon as the write
         * returns. Deferring the batch to a microtask would make every one of
         * those read stale memory -- which is why the authoritative GL state
         * lives here and why nothing on this path awaits the GPU (plan 4.8).
         *
         * Before the device exists there is nothing to be synchronous about, so
         * batches queue and replay in order once initialize() resolves. The
         * page creates the executor at load time precisely so that window is
         * empty by the time a guest runs.
         */
        submit(bytes, metadata) {
            if (!this.device) {
                this.pending.push({ bytes: bytes.slice(),
                                    metadata: metadata || {} });
                if (!this.draining) {
                    this.draining = true;
                    this.initialize().then(() => {
                        this.draining = false;
                        const queued = this.pending;
                        this.pending = [];
                        for (const item of queued)
                            this.executeBatch(item.bytes, item.metadata);
                    }).catch(() => {
                        this.draining = false;
                        this.pending.length = 0;
                    });
                }
                return;
            }
            this.executeBatch(bytes, metadata || {});
        }

        executeBatch(bytes, metadata) {
            ++this.stats.batches;
            const view = new DataView(bytes.buffer, bytes.byteOffset,
                bytes.byteLength);
            let offset = 0;
            const end = bytes.byteLength;
            while (offset < end) {
                if (offset + 4 > end) {
                    this.refuse("batch", "truncated record header",
                        { remaining: end - offset });
                    break;
                }
                const fn = view.getUint16(offset, true);
                let size = view.getUint16(offset + 2, true);
                offset += 4;
                if (size === 0xFFFF) {
                    if (offset + 4 > end) {
                        this.refuse("batch", "truncated extended length", { fn });
                        break;
                    }
                    size = view.getUint32(offset, true);
                    offset += 4;
                }
                if (offset + size > end) {
                    this.refuse("batch", "truncated record",
                        { fn, size, remaining: end - offset });
                    break;
                }
                ++this.stats.commands;
                try {
                    this.dispatch(fn, bytes, view, offset, size, metadata);
                } catch (error) {
                    if (error instanceof GLStreamError) {
                        /*
                         * A malformed record is a wire disagreement between
                         * the guest and this decoder, and "refused op40:
                         * client array blocks are truncated {}" says nothing
                         * about which of the two is wrong. The name and the
                         * first bytes do: the payload header is right there,
                         * and against openglproxy's struct it is usually one
                         * read to see whose offset moved.
                         */
                        this.refuse("op" + fn, error.message, {
                            name: constants.NAME_BY_OPCODE[fn] || "?",
                            size,
                            head: hexPreview(bytes, offset, size),
                        });
                    } else {
                        throw error;
                    }
                }
                offset += size;
            }
            this.beat(metadata);
        }

        dispatch(fn, bytes, view, offset, size, metadata) {

            if (fn === CTRL.MAKE_CURRENT)
                return this.makeCurrent(bytes.subarray(offset, offset + size));
            if (fn === CTRL.RELEASE_CURRENT) return this.releaseCurrent();
            if (fn === CTRL.DESTROY_CONTEXT)
                return this.destroyContext(bytes.subarray(offset, offset + size));

            const handler = this.handlers[fn];
            if (!handler) {
                this.refuse("op" + fn, "no handler",
                    { name: constants.NAME_BY_OPCODE[fn] || "?" });
                return;
            }
            if (!this.current && handler.needsContext !== false) {
                if (!this.contexts.size &&
                        isContextFreeTeardownNoop(fn, view, offset, size))
                    return;
                this.warnOnce("no-context",
                    "a GL command arrived with no current context; it is ignored",
                    { fn, name: constants.NAME_BY_OPCODE[fn] || "?" });
                return;
            }
            if (handler.signature) {
                const count = wire.decodeArgs(handler.signature, view, offset,
                    size, this.args);
                if (count < 0) {
                    this.refuse("op" + fn, "payload too short for signature",
                        { name: constants.NAME_BY_OPCODE[fn], size });
                    return;
                }
                return handler.call(this, this.args, bytes, view, offset, size,
                    metadata);
            }
            return handler.call(this, null, bytes, view, offset, size, metadata);
        }

        beat(metadata) {
            // The guest's readback timeout watches this counter rather than the
            // clock: a host thousands of batches behind is slow, not broken,
            // and the two need different reactions (plan 6.2).
            this.heartbeat = (this.heartbeat + 1) >>> 0;
            const write = this.options.writeGuestMemory;
            if (typeof write !== "function" || !metadata ||
                    metadata.responseBase === undefined)
                return;
            const bytes = new Uint8Array(4);
            new DataView(bytes.buffer).setUint32(0, this.heartbeat, true);
            try {
                write(metadata.responseBase + GLWG_HEARTBEAT_OFFSET, bytes);
            } catch (error) {
                this.warnOnce("heartbeat", "could not write the heartbeat counter",
                    { message: String(error) });
            }
        }
    }

    /* ================================================================== */
    /* Opcode handlers                                                    */
    /* ================================================================== */

    /*
     * One table, built once. A 217-way switch degenerates into a linear
     * comparison chain in V8, and this runs tens of thousands of times per
     * frame; a table also turns "which opcodes are unimplemented" into an
     * array scan, which is what feeds COVERAGE.md.
     */
    function buildHandlerTable() {
        const table = new Array(256).fill(null);

        const define = (name, handler, options) => {
            const opcode = GLFN[name];
            if (opcode === undefined)
                throw new Error("unknown opcode name " + name);
            const signature = SIGNATURES[name];
            if (signature && signature[1].length) handler.signature = signature[1];
            if (options && options.needsContext === false)
                handler.needsContext = false;
            handler.glName = signature ? signature[0] : name;
            table[opcode] = handler;
        };

        /* ---- framebuffer-wide state ---- */

        define("VIEWPORT", function(a) {
            const s = this.current;
            s.viewport = { x: a[0] | 0, y: a[1] | 0,
                           width: Math.max(0, a[2] | 0),
                           height: Math.max(0, a[3] | 0) };
            // GL's viewport origin is the lower left and so, after the clip
            // space flip, is WebGPU's: no conversion (plan 4.3).
        });

        define("SCISSOR", function(a) {
            const s = this.current;
            s.scissor = { x: a[0] | 0, y: a[1] | 0,
                          width: Math.max(0, a[2] | 0),
                          height: Math.max(0, a[3] | 0), set: true };
        });

        define("DEPTH_RANGE", function(a) {
            this.current.depthRange = { near: clamp(a[0], 0, 1),
                                        far: clamp(a[1], 0, 1) };
        });

        define("CLEAR_COLOR", function(a) {
            this.current.clearColor.set([a[0], a[1], a[2], a[3]]);
        });
        define("CLEAR_DEPTH", function(a) { this.current.clearDepth = clamp(a[0], 0, 1); });
        define("CLEAR_STENCIL", function(a) { this.current.clearStencil = a[0] | 0; });
        define("CLEAR_ACCUM", function(a) {
            this.current.clearAccum.set([a[0], a[1], a[2], a[3]]);
        });
        define("CLEAR", function(a) { this.clearBuffers(a[0] >>> 0); });

        /* ---- matrices ---- */

        define("MATRIX_MODE", function(a) { this.current.matrixMode = a[0] >>> 0; });
        define("LOAD_IDENTITY", function() { identity4(this.topMatrix()); this.matrixChanged(); });
        define("PUSH_MATRIX", function() { this.pushMatrix(); });
        define("POP_MATRIX", function() { this.popMatrix(); });
        define("TRANSLATEF", function(a) {
            const m = identity4(SCRATCH_A);
            m[12] = a[0]; m[13] = a[1]; m[14] = a[2];
            this.multiplyTop(m);
        });
        define("SCALEF", function(a) {
            const m = identity4(SCRATCH_A);
            m[0] = a[0]; m[5] = a[1]; m[10] = a[2];
            this.multiplyTop(m);
        });
        define("ROTATEF", function(a) {
            const angle = a[0] * Math.PI / 180;
            let x = a[1], y = a[2], z = a[3];
            const len = Math.sqrt(x * x + y * y + z * z);
            if (len === 0) return;
            x /= len; y /= len; z /= len;
            const c = Math.cos(angle), s = Math.sin(angle), t = 1 - c;
            const m = identity4(SCRATCH_A);
            m[0] = t*x*x + c;   m[4] = t*x*y - s*z; m[8]  = t*x*z + s*y;
            m[1] = t*x*y + s*z; m[5] = t*y*y + c;   m[9]  = t*y*z - s*x;
            m[2] = t*x*z - s*y; m[6] = t*y*z + s*x; m[10] = t*z*z + c;
            this.multiplyTop(m);
        });
        define("FRUSTUM", function(a) {
            const [l, r, b, t, n, f] = [a[0], a[1], a[2], a[3], a[4], a[5]];
            const m = identity4(SCRATCH_A);
            m[0] = 2 * n / (r - l); m[5] = 2 * n / (t - b);
            m[8] = (r + l) / (r - l); m[9] = (t + b) / (t - b);
            m[10] = -(f + n) / (f - n); m[11] = -1;
            m[14] = -2 * f * n / (f - n); m[15] = 0;
            m[1] = 0; m[2] = 0; m[3] = 0; m[4] = 0; m[6] = 0; m[7] = 0;
            m[12] = 0; m[13] = 0;
            this.multiplyTop(m);
        });
        define("ORTHO", function(a) {
            const [l, r, b, t, n, f] = [a[0], a[1], a[2], a[3], a[4], a[5]];
            const m = identity4(SCRATCH_A);
            m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n);
            m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b);
            m[14] = -(f + n) / (f - n);
            this.multiplyTop(m);
        });
        define("LOAD_MATRIXF", function(a, bytes, view, offset, size) {
            if (size < 64) throw new GLStreamError("glLoadMatrixf needs 16 floats");
            const m = this.topMatrix();
            for (let i = 0; i < 16; ++i) m[i] = view.getFloat32(offset + i * 4, true);
            this.matrixChanged();
        });
        define("MULT_MATRIXF", function(a, bytes, view, offset, size) {
            if (size < 64) throw new GLStreamError("glMultMatrixf needs 16 floats");
            const m = SCRATCH_A;
            for (let i = 0; i < 16; ++i) m[i] = view.getFloat32(offset + i * 4, true);
            this.multiplyTop(m);
        });

        /* ---- enables and simple raster state ---- */

        define("ENABLE", function(a) { this.setCapability(a[0] >>> 0, true); });
        define("DISABLE", function(a) { this.setCapability(a[0] >>> 0, false); });
        define("DEPTH_FUNC", function(a) { this.current.depthFunc = a[0] >>> 0; });
        define("DEPTH_MASK", function(a) { this.current.depthMask = a[0] !== 0; });
        define("COLOR_MASK", function(a) {
            this.current.colorMask = [a[0] !== 0, a[1] !== 0, a[2] !== 0, a[3] !== 0];
        });
        define("SHADE_MODEL", function(a) { this.current.shadeModel = a[0] >>> 0; });
        define("CULL_FACE", function(a) { this.current.cullFace = a[0] >>> 0; });
        define("FRONT_FACE", function(a) { this.current.frontFace = a[0] >>> 0; });
        define("LINE_WIDTH", function(a) { this.current.lineWidth = Math.max(0, a[0]); });
        define("POINT_SIZE", function(a) {
            this.current.pointSize = Math.max(0, a[0]);
            this.current.point.size = this.current.pointSize;
        });
        define("POLYGON_MODE", function(a) {
            const face = a[0] >>> 0, mode = a[1] >>> 0;
            const s = this.current;
            if (face === GL.FRONT || face === GL.FRONT_AND_BACK) s.polygonMode.front = mode;
            if (face === GL.BACK || face === GL.FRONT_AND_BACK) s.polygonMode.back = mode;
        });
        define("POLYGON_OFFSET", function(a) {
            this.current.polygonOffset = { factor: a[0], units: a[1] };
        });
        define("LINE_STIPPLE", function(a) {
            this.current.lineStipple = { factor: clamp(a[0] | 0, 1, 256),
                                         pattern: a[1] & 0xffff };
        });
        define("LOGIC_OP", function(a) { this.current.logicOp = a[0] >>> 0; });
        define("HINT", function() { /* hints have no effect here */ });
        define("SAMPLE_COVERAGE", function(a) {
            this.current.sampleCoverage = { value: clamp(a[0], 0, 1),
                                            invert: a[1] !== 0 };
        });

        /* ---- blending, alpha test, stencil ---- */

        define("BLEND_FUNC", function(a) {
            const b = this.current.blend;
            b.srcRGB = b.srcAlpha = a[0] >>> 0;
            b.dstRGB = b.dstAlpha = a[1] >>> 0;
        });
        define("BLEND_FUNC_SEPARATE", function(a) {
            const b = this.current.blend;
            b.srcRGB = a[0] >>> 0; b.dstRGB = a[1] >>> 0;
            b.srcAlpha = a[2] >>> 0; b.dstAlpha = a[3] >>> 0;
        });
        define("BLEND_EQUATION", function(a) {
            const b = this.current.blend;
            b.equationRGB = b.equationAlpha = a[0] >>> 0;
        });
        define("BLEND_EQUATION_SEPARATE", function(a) {
            const b = this.current.blend;
            b.equationRGB = a[0] >>> 0; b.equationAlpha = a[1] >>> 0;
        });
        define("BLEND_COLOR", function(a) {
            this.current.blend.color.set([a[0], a[1], a[2], a[3]]);
        });
        define("ALPHA_FUNC", function(a) {
            this.current.alphaFunc = { func: a[0] >>> 0, ref: clamp(a[1], 0, 1) };
        });
        define("STENCIL_FUNC", function(a) {
            for (const face of ["front", "back"]) {
                const f = this.current.stencil[face];
                f.func = a[0] >>> 0; f.ref = a[1] | 0; f.valueMask = a[2] >>> 0;
            }
        });
        define("STENCIL_FUNC_SEPARATE", function(a) {
            const which = a[0] >>> 0;
            for (const face of facesOf(which)) {
                const f = this.current.stencil[face];
                f.func = a[1] >>> 0; f.ref = a[2] | 0; f.valueMask = a[3] >>> 0;
            }
        });
        define("STENCIL_OP", function(a) {
            for (const face of ["front", "back"]) {
                const f = this.current.stencil[face];
                f.fail = a[0] >>> 0; f.zfail = a[1] >>> 0; f.zpass = a[2] >>> 0;
            }
        });
        define("STENCIL_OP_SEPARATE", function(a) {
            for (const face of facesOf(a[0] >>> 0)) {
                const f = this.current.stencil[face];
                f.fail = a[1] >>> 0; f.zfail = a[2] >>> 0; f.zpass = a[3] >>> 0;
            }
        });
        define("STENCIL_MASK", function(a) {
            this.current.stencil.front.writeMask = a[0] >>> 0;
            this.current.stencil.back.writeMask = a[0] >>> 0;
        });
        define("STENCIL_MASK_SEPARATE", function(a) {
            for (const face of facesOf(a[0] >>> 0))
                this.current.stencil[face].writeMask = a[1] >>> 0;
        });

        /* ---- current vertex attributes and immediate mode ---- */

        define("BEGIN", function(a) { this.beginImmediate(a[0] >>> 0); });
        define("END", function() { this.endImmediate(); });
        define("COLOR4F", function(a) {
            this.current.current.color.set([a[0], a[1], a[2], a[3]]);
        });
        define("SECONDARY_COLOR3F", function(a) {
            this.current.current.secondaryColor.set([a[0], a[1], a[2], 1]);
        });
        define("NORMAL3F", function(a) {
            this.current.current.normal.set([a[0], a[1], a[2]]);
        });
        define("FOG_COORDF", function(a) { this.current.current.fogCoord = a[0]; });
        define("TEX_COORD2F", function(a) {
            this.current.current.texCoord[this.current.clientActiveTexture]
                .set([a[0], a[1], 0, 1]);
        });
        define("TEX_COORD4F", function(a) {
            this.current.current.texCoord[this.current.clientActiveTexture]
                .set([a[0], a[1], a[2], a[3]]);
        });
        define("MULTI_TEX_COORD4F", function(a) {
            const unit = (a[0] >>> 0) - GL.TEXTURE0;
            if (unit < 0 || unit >= MAX_TEXTURE_COORDS)
                return this.refuse("glMultiTexCoord", "texture unit out of range",
                    { unit }, GL.INVALID_ENUM);
            this.current.current.texCoord[unit].set([a[1], a[2], a[3], a[4]]);
        });
        define("VERTEX3F", function(a) { this.immediateVertex(a[0], a[1], a[2], 1); });
        define("VERTEX4F", function(a) { this.immediateVertex(a[0], a[1], a[2], a[3]); });
        define("RASTER_POS4F", function(a) { this.setRasterPos(a[0], a[1], a[2], a[3]); });
        define("WINDOW_POS3F", function(a) {
            // glWindowPos bypasses the transform: the value is already in
            // window coordinates and the position is always valid.
            const s = this.current;
            s.current.rasterPos.set([a[0], a[1], clamp(a[2], 0, 1), 1]);
            s.current.rasterValid = true;
        });
        define("VERTEX_ATTRIB4F", function(a) {
            const index = a[0] >>> 0;
            if (index >= MAX_VERTEX_ATTRIBS)
                return this.refuse("glVertexAttrib4f", "index out of range",
                    { index }, GL.INVALID_VALUE);
            this.current.genericAttribs[index].value.set([a[1], a[2], a[3], a[4]]);
        });

        /* ---- lighting and material ---- */

        define("LIGHT_MODELF", function(a) { this.setLightModel(a[0] >>> 0, [a[1]]); });
        define("LIGHT_MODELI", function(a) { this.setLightModel(a[0] >>> 0, [a[1]]); });
        define("LIGHT_MODELFV", function(a) {
            this.setLightModel(a[0] >>> 0, [a[2], a[3], a[4], a[5]], a[1] >>> 0);
        });
        define("LIGHT_MODELIV", function(a) {
            this.setLightModel(a[0] >>> 0,
                [a[2] / 2147483647, a[3] / 2147483647,
                 a[4] / 2147483647, a[5] / 2147483647], a[1] >>> 0);
        });
        define("LIGHTF", function(a) {
            this.setLight(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("LIGHTI", function(a) {
            this.setLight(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("LIGHTFV", function(a) {
            this.setLight(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });
        define("LIGHTIV", function(a) {
            this.setLight(a[0] >>> 0, a[1] >>> 0,
                [a[3] / 2147483647, a[4] / 2147483647,
                 a[5] / 2147483647, a[6] / 2147483647]);
        });
        define("MATERIALF", function(a) {
            this.setMaterial(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("MATERIALI", function(a) {
            this.setMaterial(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("MATERIALFV", function(a) {
            this.setMaterial(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });
        define("MATERIALIV", function(a) {
            this.setMaterial(a[0] >>> 0, a[1] >>> 0,
                [a[3] / 2147483647, a[4] / 2147483647,
                 a[5] / 2147483647, a[6] / 2147483647]);
        });
        define("COLOR_MATERIAL", function(a) {
            this.current.colorMaterial.face = a[0] >>> 0;
            this.current.colorMaterial.mode = a[1] >>> 0;
        });

        /* ---- fog ---- */

        define("FOGF", function(a) { this.setFog(a[0] >>> 0, [a[1]]); });
        define("FOGI", function(a) { this.setFog(a[0] >>> 0, [a[1]]); });
        define("FOGFV", function(a) {
            this.setFog(a[0] >>> 0, [a[2], a[3], a[4], a[5]]);
        });

        /* ---- clip planes ---- */

        define("CLIP_PLANE", function(a) {
            const index = (a[0] >>> 0) - GL.CLIP_PLANE0;
            if (index < 0 || index >= MAX_CLIP_PLANES)
                return this.refuse("glClipPlane", "plane index out of range",
                    { index }, GL.INVALID_ENUM);
            // GL specifies the plane in object coordinates and stores it
            // transformed by the inverse modelview at call time, so a later
            // modelview change does not move the plane.
            const s = this.current;
            const inv = invert4(this.topOf(GL.MODELVIEW), SCRATCH_B);
            const t = transpose4(inv, SCRATCH_C);
            const plane = new Float32Array([a[1], a[2], a[3], a[4]]);
            transformPoint4(t, plane, s.clipPlanes[index]);
        });

        /* ---- texture unit selection and environment ---- */

        define("ACTIVE_TEXTURE", function(a) {
            const unit = (a[0] >>> 0) - GL.TEXTURE0;
            if (unit < 0 || unit >= MAX_TEXTURE_UNITS)
                return this.refuse("glActiveTexture", "unit out of range",
                    { unit }, GL.INVALID_ENUM);
            this.current.activeTexture = unit;
        });
        define("CLIENT_ACTIVE_TEXTURE", function(a) {
            const unit = (a[0] >>> 0) - GL.TEXTURE0;
            if (unit < 0 || unit >= MAX_TEXTURE_COORDS)
                return this.refuse("glClientActiveTexture", "unit out of range",
                    { unit }, GL.INVALID_ENUM);
            this.current.clientActiveTexture = unit;
        });
        define("TEX_ENVI", function(a) {
            this.setTexEnv(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("TEX_ENVF", function(a) {
            this.setTexEnv(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("TEX_ENVIV", function(a) {
            this.setTexEnv(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });
        define("TEX_ENVFV", function(a) {
            this.setTexEnv(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });
        define("TEX_GENI", function(a) {
            this.setTexGen(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("TEX_GENF", function(a) {
            this.setTexGen(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("TEX_GENIV", function(a) {
            this.setTexGen(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });
        define("TEX_GENFV", function(a) {
            this.setTexGen(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });

        /* ---- pixel store and transfer ---- */

        define("PIXEL_STOREI", function(a) { this.setPixelStore(a[0] >>> 0, a[1] | 0); });
        define("PIXEL_TRANSFERF", function(a) { this.setPixelTransfer(a[0] >>> 0, a[1]); });
        define("PIXEL_TRANSFERI", function(a) { this.setPixelTransfer(a[0] >>> 0, a[1]); });
        define("PIXEL_ZOOM", function(a) {
            this.current.pixelZoom = { x: a[0], y: a[1] };
        });

        /* ---- point parameters ---- */

        define("POINT_PARAMETERF", function(a) {
            this.setPointParameter(a[0] >>> 0, [a[1]]);
        });
        define("POINT_PARAMETERI", function(a) {
            this.setPointParameter(a[0] >>> 0, [a[1]]);
        });
        define("POINT_PARAMETERFV", function(a) {
            this.setPointParameter(a[0] >>> 0, [a[1], a[2], a[3]]);
        });
        define("POINT_PARAMETERIV", function(a, bytes, view, offset, size) {
            if (size < 8) throw new GLStreamError("glPointParameteriv is too short");
            const pname = view.getUint32(offset, true);
            const values = [];
            for (let i = 4; i + 4 <= size; i += 4)
                values.push(view.getInt32(offset + i, true));
            this.setPointParameter(pname, values);
        });

        /* ---- client array enables ---- */

        define("ENABLE_CLIENT_STATE", function(a) {
            this.setClientState(a[0] >>> 0, true);
        });
        define("DISABLE_CLIENT_STATE", function(a) {
            this.setClientState(a[0] >>> 0, false);
        });
        define("ENABLE_VERTEX_ATTRIB_ARRAY", function(a) {
            const index = a[0] >>> 0;
            if (index < MAX_VERTEX_ATTRIBS)
                this.current.genericAttribs[index].enabled = true;
        });
        define("DISABLE_VERTEX_ATTRIB_ARRAY", function(a) {
            const index = a[0] >>> 0;
            if (index < MAX_VERTEX_ATTRIBS)
                this.current.genericAttribs[index].enabled = false;
        });

        /* ---- attribute stacks ---- */
        define("PUSH_ATTRIB", function(a) {
            const s = this.current;
            if (s.attribStack.length >= 16) {
                this.setError(GL.STACK_OVERFLOW);
                return;
            }
            const mask = a[0] >>> 0;
            const fields = fieldsForServerAttrib(mask);
            s.attribStack.push({ mask, fields, state: snapshotFields(s, fields),
                enableState: mask & ATTRIB.ENABLE ? captureEnableState(s) : null });
        });
        define("POP_ATTRIB", function() {
            const s = this.current;
            if (!s.attribStack.length) {
                this.setError(GL.STACK_UNDERFLOW);
                return;
            }
            const snapshot = s.attribStack.pop();
            restoreFields(s, snapshot.state, snapshot.fields);
            if (snapshot.enableState) restoreEnableState(s, snapshot.enableState);
            this.endPass();
            this.invalidateBindGroups();
        });
        define("PUSH_CLIENT_ATTRIB", function(a) {
            const s = this.current;
            if (s.clientAttribStack.length >= 16) {
                this.setError(GL.STACK_OVERFLOW);
                return;
            }
            const mask = a[0] >>> 0;
            const fields = [];
            if (mask & CLIENT_ATTRIB.PIXEL_STORE)
                fields.push("pixelStore", "pixelPackBuffer", "pixelUnpackBuffer");
            if (mask & CLIENT_ATTRIB.VERTEX_ARRAY)
                fields.push("clientActiveTexture", "arrays", "arrayBuffer",
                    "elementArrayBuffer", "genericAttribs");
            s.clientAttribStack.push({ mask, fields, state: snapshotFields(s, fields) });
        });
        define("POP_CLIENT_ATTRIB", function() {
            const s = this.current;
            if (!s.clientAttribStack.length) {
                this.setError(GL.STACK_UNDERFLOW);
                return;
            }
            const snapshot = s.clientAttribStack.pop();
            restoreFields(s, snapshot.state, snapshot.fields);
        });

        /* ---- frame boundaries ---- */

        define("FLUSH", function() { this.flushFrame(); });
        define("FINISH", function(a, bytes, view, offset, size, metadata) {
            return this.onFinish(bytes, view, offset, size, metadata);
        });

        return table;
    }

    function facesOf(which) {
        if (which === GL.FRONT) return ["front"];
        if (which === GL.BACK) return ["back"];
        return ["front", "back"];
    }

    const SCRATCH_A = new Float32Array(16);
    const SCRATCH_B = new Float32Array(16);
    const SCRATCH_C = new Float32Array(16);

    /* Shared by the handler-installing functions below. */
    function definer(table) {
        return (name, handler, options) => {
            const opcode = GLFN[name];
            if (opcode === undefined)
                throw new Error("unknown opcode name " + name);
            const signature = SIGNATURES[name];
            if (signature && signature[1].length) handler.signature = signature[1];
            if (options && options.needsContext === false)
                handler.needsContext = false;
            handler.glName = signature ? signature[0] : name;
            table[opcode] = handler;
        };
    }

    /* Reads a name array record: {count u32, names u32[count]}. */
    function readNameArray(view, offset, size) {
        if (size < 4) throw new GLStreamError("name array record is too short");
        const count = view.getUint32(offset, true);
        if (4 + count * 4 > size)
            throw new GLStreamError("name array record is truncated");
        const names = new Uint32Array(count);
        for (let i = 0; i < count; ++i)
            names[i] = view.getUint32(offset + 4 + i * 4, true);
        return names;
    }

    function readCString(bytes, offset, length) {
        let end = offset;
        const limit = offset + length;
        while (end < limit && bytes[end] !== 0) ++end;
        let text = "";
        for (let i = offset; i < end; ++i) text += String.fromCharCode(bytes[i]);
        return text;
    }

    /*
     * Writes `text` NUL-terminated into a guest buffer of `capacity` bytes and
     * returns the character count GL wants reported -- which excludes the
     * terminator, the same convention openglproxy's own copy_gl_string_result
     * uses when it answers one of these queries without the host.
     */
    function writeCString(bytes, offset, capacity, text) {
        if (capacity <= 0) return 0;
        const written = Math.min(text.length, capacity - 1);
        for (let i = 0; i < written; ++i)
            bytes[offset + i] = text.charCodeAt(i) & 0xff;
        bytes[offset + written] = 0;
        return written;
    }

    function installResourceHandlers(table) {
        const define = definer(table);

        /* ---- textures ---- */

        define("GEN_TEXTURES", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names) {
                if (!name) continue;
                if (!group.textures.has(name))
                    group.textures.set(name, createTexture(name));
            }
        });

        define("DELETE_TEXTURES", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names) {
                const texture = group.textures.get(name);
                if (!texture) continue;
                this.retire(texture.gpuTexture);
                group.textures.delete(name);
                // GL rebinds the default texture on every unit that held it,
                // which is what keeps a later draw from sampling a dead object.
                for (const unit of this.current.textureUnits) {
                    for (const target of Object.keys(unit.bindings))
                        if (unit.bindings[target] === name) unit.bindings[target] = 0;
                }
                this.invalidateBindGroups();
            }
        });

        define("BIND_TEXTURE", function(a) {
            const target = a[0] >>> 0, name = a[1] >>> 0;
            const unit = this.current.textureUnits[this.current.activeTexture];
            unit.bindings[target] = name;
            if (name) {
                const group = this.current.shareGroup;
                let texture = group.textures.get(name);
                if (!texture) {
                    texture = createTexture(name);
                    group.textures.set(name, texture);
                }
                if (!texture.target) texture.target = target;
            }
            this.invalidateBindGroups();
        });

        define("TEX_PARAMETERI", function(a) {
            this.setTexParameter(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("TEX_PARAMETERF", function(a) {
            this.setTexParameter(a[0] >>> 0, a[1] >>> 0, [a[2]]);
        });
        define("TEX_PARAMETERIV", function(a) {
            this.setTexParameter(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });
        define("TEX_PARAMETERFV", function(a) {
            this.setTexParameter(a[0] >>> 0, a[1] >>> 0, [a[3], a[4], a[5], a[6]]);
        });

        /*
         * glTexImage*: {target, level, internalFormat, width, height, depth,
         * border, format, type, dataSize} then dataSize bytes. The 1D and 2D
         * forms omit the dimensions they do not have; all three are decoded
         * through one function because everything after the header is identical.
         */
        const texImage = dimensions => function(a, bytes, view, offset, size) {
            const header = 4 * (7 + dimensions);
            if (size < header)
                throw new GLStreamError("glTexImage record is too short");
            let at = offset;
            const target = view.getUint32(at, true); at += 4;
            const level = view.getInt32(at, true); at += 4;
            const internalFormat = view.getUint32(at, true); at += 4;
            const width = view.getInt32(at, true); at += 4;
            const height = dimensions >= 2 ? view.getInt32(at, true) : 1;
            if (dimensions >= 2) at += 4;
            const depth = dimensions >= 3 ? view.getInt32(at, true) : 1;
            if (dimensions >= 3) at += 4;
            at += 4;                            // border
            const format = view.getUint32(at, true); at += 4;
            const type = view.getUint32(at, true); at += 4;
            const dataSize = view.getUint32(at, true); at += 4;
            const data = dataSize ?
                bytes.subarray(at, at + dataSize) :
                null;
            this.texImage(target, level, internalFormat, width, height, depth,
                format, type, data, dataSize);
        };
        define("TEX_IMAGE_1D", texImage(1));
        define("TEX_IMAGE_2D", texImage(2));
        define("TEX_IMAGE_3D", texImage(3));

        const texSubImage = dimensions => function(a, bytes, view, offset, size) {
            const header = 4 * (5 + dimensions * 2);
            if (size < header)
                throw new GLStreamError("glTexSubImage record is too short");
            let at = offset;
            const target = view.getUint32(at, true); at += 4;
            const level = view.getInt32(at, true); at += 4;
            const x = view.getInt32(at, true); at += 4;
            const y = dimensions >= 2 ? view.getInt32(at, true) : 0;
            if (dimensions >= 2) at += 4;
            const z = dimensions >= 3 ? view.getInt32(at, true) : 0;
            if (dimensions >= 3) at += 4;
            const width = view.getInt32(at, true); at += 4;
            const height = dimensions >= 2 ? view.getInt32(at, true) : 1;
            if (dimensions >= 2) at += 4;
            const depth = dimensions >= 3 ? view.getInt32(at, true) : 1;
            if (dimensions >= 3) at += 4;
            const format = view.getUint32(at, true); at += 4;
            const type = view.getUint32(at, true); at += 4;
            const dataSize = view.getUint32(at, true); at += 4;
            const data = dataSize ?
                bytes.subarray(at, at + dataSize) :
                null;
            this.texSubImage(target, level, x, y, z, width, height, depth,
                format, type, data, dataSize);
        };
        define("TEX_SUB_IMAGE_1D", texSubImage(1));
        define("TEX_SUB_IMAGE_2D", texSubImage(2));
        define("TEX_SUB_IMAGE_3D", texSubImage(3));

        const compressedTexImage = dimensions => function(a, bytes, view, offset, size) {
            let at = offset;
            const target = view.getUint32(at, true); at += 4;
            const level = view.getInt32(at, true); at += 4;
            const internalFormat = view.getUint32(at, true); at += 4;
            const width = view.getInt32(at, true); at += 4;
            const height = dimensions >= 2 ? view.getInt32(at, true) : 1;
            if (dimensions >= 2) at += 4;
            const depth = dimensions >= 3 ? view.getInt32(at, true) : 1;
            if (dimensions >= 3) at += 4;
            at += 4;                            // border
            const dataSize = view.getUint32(at, true); at += 4;
            if (at - offset + dataSize > size)
                throw new GLStreamError("compressed texture record is truncated");
            const data = bytes.subarray(at, at + dataSize);
            this.compressedTexImage(target, level, internalFormat, width, height,
                depth, data);
        };
        define("COMPRESSED_TEX_IMAGE_1D", compressedTexImage(1));
        define("COMPRESSED_TEX_IMAGE_2D", compressedTexImage(2));
        define("COMPRESSED_TEX_IMAGE_3D", compressedTexImage(3));

        const compressedTexSubImage = dimensions => function(a, bytes, view, offset, size) {
            // A partial update of a compressed level must land on block
            // boundaries; GL requires it and WebGPU enforces it, so a
            // misaligned rectangle is refused rather than silently rounded.
            let at = offset;
            const target = view.getUint32(at, true); at += 4;
            const level = view.getInt32(at, true); at += 4;
            const x = view.getInt32(at, true); at += 4;
            const y = dimensions >= 2 ? view.getInt32(at, true) : 0;
            if (dimensions >= 2) at += 4;
            const z = dimensions >= 3 ? view.getInt32(at, true) : 0;
            if (dimensions >= 3) at += 4;
            const width = view.getInt32(at, true); at += 4;
            const height = dimensions >= 2 ? view.getInt32(at, true) : 1;
            if (dimensions >= 2) at += 4;
            const depth = dimensions >= 3 ? view.getInt32(at, true) : 1;
            if (dimensions >= 3) at += 4;
            const format = view.getUint32(at, true); at += 4;
            const dataSize = view.getUint32(at, true); at += 4;
            if (at - offset + dataSize > size)
                throw new GLStreamError("compressed subimage record is truncated");
            const data = bytes.subarray(at, at + dataSize);
            this.compressedTexSubImage(target, level, x, y, z, width, height,
                depth, format, data);
        };
        define("COMPRESSED_TEX_SUB_IMAGE_1D", compressedTexSubImage(1));
        define("COMPRESSED_TEX_SUB_IMAGE_2D", compressedTexSubImage(2));
        define("COMPRESSED_TEX_SUB_IMAGE_3D", compressedTexSubImage(3));

        define("GENERATE_MIPMAP", function(a) {
            this.generateMipmap(a[0] >>> 0);
        });

        define("COPY_TEX_IMAGE_2D", function(a) {
            this.copyTexImage(a[0] >>> 0, a[1] | 0, a[2] >>> 0, a[3] | 0, a[4] | 0,
                a[5] | 0, a[6] | 0);
        });
        define("COPY_TEX_IMAGE_1D", function(a) {
            this.copyTexImage(a[0] >>> 0, a[1] | 0, a[2] >>> 0, a[3] | 0, a[4] | 0,
                a[5] | 0, 1);
        });
        define("COPY_TEX_SUB_IMAGE_2D", function(a) {
            this.copyTexSubImage(a[0] >>> 0, a[1] | 0, a[2] | 0, a[3] | 0, 0,
                a[4] | 0, a[5] | 0, a[6] | 0, a[7] | 0);
        });
        define("COPY_TEX_SUB_IMAGE_1D", function(a) {
            this.copyTexSubImage(a[0] >>> 0, a[1] | 0, a[2] | 0, 0, 0,
                a[3] | 0, a[4] | 0, a[5] | 0, 1);
        });
        define("COPY_TEX_SUB_IMAGE_3D", function(a) {
            this.copyTexSubImage(a[0] >>> 0, a[1] | 0, a[2] | 0, a[3] | 0, a[4] | 0,
                a[5] | 0, a[6] | 0, a[7] | 0, a[8] | 0);
        });

        /* ---- buffers ---- */

        define("GEN_BUFFERS", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names)
                if (name && !group.buffers.has(name))
                    group.buffers.set(name, createBuffer(name));
        });
        define("DELETE_BUFFERS", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names) {
                const buffer = group.buffers.get(name);
                if (!buffer) continue;
                this.retire(buffer.gpuBuffer);
                group.buffers.delete(name);
                if (this.current.arrayBuffer === name) this.current.arrayBuffer = 0;
                if (this.current.elementArrayBuffer === name)
                    this.current.elementArrayBuffer = 0;
            }
        });
        define("BIND_BUFFER", function(a) {
            const target = a[0] >>> 0, name = a[1] >>> 0;
            const group = this.current.shareGroup;
            if (name && !group.buffers.has(name))
                group.buffers.set(name, createBuffer(name));
            if (target === GL.ELEMENT_ARRAY_BUFFER)
                this.current.elementArrayBuffer = name;
            else if (target === GL.PIXEL_PACK_BUFFER)
                this.current.pixelPackBuffer = name;
            else if (target === GL.PIXEL_UNPACK_BUFFER)
                this.current.pixelUnpackBuffer = name;
            else if (target === GL.ARRAY_BUFFER)
                this.current.arrayBuffer = name;
            else
                return this.refuse("glBindBuffer", "unknown buffer target",
                    { target }, GL.INVALID_ENUM);
        });
        /*
         * {target, size, usage, data_size} then the contents. The header is
         * four words: reading the data from 12 shifts every vertex in the
         * buffer by one attribute and draws geometry that is subtly wrong
         * rather than reporting anything.
         */
        define("BUFFER_DATA", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("glBufferData record is short");
            const target = view.getUint32(offset, true);
            const byteCount = view.getUint32(offset + 4, true);
            const usage = view.getUint32(offset + 8, true);
            const dataSize = Math.min(view.getUint32(offset + 12, true), size - 16);
            const data = dataSize ?
                bytes.subarray(offset + 16,
                    offset + 16 + Math.min(dataSize, byteCount)) :
                null;
            this.bufferData(target, byteCount, usage, data);
        });
        /* {target, offset, size, data_size} then the contents. */
        define("BUFFER_SUB_DATA", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("glBufferSubData record is short");
            const target = view.getUint32(offset, true);
            const dstOffset = view.getUint32(offset + 4, true);
            const byteCount = Math.min(view.getUint32(offset + 8, true), size - 16);
            const data = bytes.subarray(offset + 16,
                offset + 16 + byteCount);
            this.bufferSubData(target, dstOffset, data);
        });

        /* ---- VBO-backed array pointers ---- */

        const pointerVBO = which => function(a, bytes, view, offset, size) {
            if (size < 16)
                throw new GLStreamError("VBO pointer record is short");
            const s = this.current;
            const array = {
                enabled: true,
                buffer: s.arrayBuffer,
                size: view.getInt32(offset, true),
                type: view.getUint32(offset + 4, true),
                stride: view.getInt32(offset + 8, true),
                offset: view.getUint32(offset + 12, true),
                normalized: which === "color" || which === "secondaryColor" ||
                    which === "normal",
                fromVBO: true,
            };
            this.setArrayPointer(which, array);
        };
        define("VERTEX_POINTER_VBO", pointerVBO("vertex"));
        define("COLOR_POINTER_VBO", pointerVBO("color"));
        define("NORMAL_POINTER_VBO", pointerVBO("normal"));
        define("SECONDARY_COLOR_POINTER_VBO", pointerVBO("secondaryColor"));
        define("FOG_COORD_POINTER_VBO", pointerVBO("fogCoord"));
        define("TEX_COORD_POINTER_VBO", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("VBO pointer record is short");
            const s = this.current;
            this.setArrayPointer("texCoord" + s.clientActiveTexture, {
                enabled: true, buffer: s.arrayBuffer,
                size: view.getInt32(offset, true),
                type: view.getUint32(offset + 4, true),
                stride: view.getInt32(offset + 8, true),
                offset: view.getUint32(offset + 12, true),
                normalized: false, fromVBO: true,
            });
        });
        define("VERTEX_ATTRIB_POINTER_VBO", function(a, bytes, view, offset, size) {
            if (size < 24) throw new GLStreamError("attrib pointer record is short");
            const index = view.getUint32(offset, true);
            if (index >= MAX_VERTEX_ATTRIBS)
                return this.refuse("glVertexAttribPointer", "index out of range",
                    { index }, GL.INVALID_VALUE);
            this.setArrayPointer("generic" + index, {
                enabled: true, buffer: this.current.arrayBuffer,
                size: view.getInt32(offset + 4, true),
                type: view.getUint32(offset + 8, true),
                normalized: view.getUint32(offset + 12, true) !== 0,
                stride: view.getInt32(offset + 16, true),
                offset: view.getUint32(offset + 20, true),
                fromVBO: true,
            });
        });

        /* ---- framebuffer objects ---- */

        define("GEN_FRAMEBUFFERS", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const map = this.framebuffers.get(this.current.id);
            for (const name of names)
                if (name && !map.has(name)) map.set(name, createFramebuffer(name));
        });
        define("DELETE_FRAMEBUFFERS", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const map = this.framebuffers.get(this.current.id);
            for (const name of names) {
                map.delete(name);
                if (this.current.drawFramebuffer === name)
                    this.current.drawFramebuffer = 0;
                if (this.current.readFramebuffer === name)
                    this.current.readFramebuffer = 0;
            }
        });
        define("BIND_FRAMEBUFFER", function(a) {
            const target = a[0] >>> 0, name = a[1] >>> 0;
            const map = this.framebuffers.get(this.current.id);
            if (name && !map.has(name)) map.set(name, createFramebuffer(name));
            // Changing the render target ends the pass: WebGPU fixes the
            // attachments when the pass begins.
            this.endPass();
            if (target === GL.READ_FRAMEBUFFER) {
                this.current.readFramebuffer = name;
            } else if (target === GL.DRAW_FRAMEBUFFER) {
                this.current.drawFramebuffer = name;
            } else {
                this.current.drawFramebuffer = name;
                this.current.readFramebuffer = name;
            }
        });
        define("FRAMEBUFFER_TEXTURE", function(a) {
            this.framebufferTexture(a[0] >>> 0, a[1] >>> 0, a[2] >>> 0,
                a[3] >>> 0, a[4] | 0, a[5] | 0);
        });
        define("FRAMEBUFFER_RENDERBUFFER", function(a) {
            this.framebufferRenderbuffer(a[0] >>> 0, a[1] >>> 0, a[2] >>> 0,
                a[3] >>> 0);
        });
        define("GEN_RENDERBUFFERS", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names)
                if (name && !group.renderbuffers.has(name))
                    group.renderbuffers.set(name, createRenderbuffer(name));
        });
        define("DELETE_RENDERBUFFERS", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names) {
                const rb = group.renderbuffers.get(name);
                if (rb) this.retire(rb.gpuTexture);
                group.renderbuffers.delete(name);
                if (this.current.renderbuffer === name) this.current.renderbuffer = 0;
            }
        });
        define("BIND_RENDERBUFFER", function(a) {
            const name = a[1] >>> 0;
            const group = this.current.shareGroup;
            if (name && !group.renderbuffers.has(name))
                group.renderbuffers.set(name, createRenderbuffer(name));
            this.current.renderbuffer = name;
        });
        define("RENDERBUFFER_STORAGE", function(a) {
            this.renderbufferStorage(a[1] >>> 0, a[2] | 0, a[3] | 0);
        });
        define("DRAW_BUFFER", function(a) {
            this.endPass();
            this.current.drawBuffers = [a[0] >>> 0];
        });
        define("READ_BUFFER", function(a) { this.current.readBuffer = a[0] >>> 0; });
        define("DRAW_BUFFERS", function(a, bytes, view, offset, size) {
            const count = size >= 4 ? view.getUint32(offset, true) : 0;
            const buffers = [];
            for (let i = 0; i < count && 4 + i * 4 + 4 <= size; ++i)
                buffers.push(view.getUint32(offset + 4 + i * 4, true));
            this.endPass();
            this.current.drawBuffers = buffers.length ? buffers : [GL.NONE];
        });
        define("BLIT_FRAMEBUFFER", function(a) {
            this.blitFramebuffer(a[0] | 0, a[1] | 0, a[2] | 0, a[3] | 0,
                a[4] | 0, a[5] | 0, a[6] | 0, a[7] | 0, a[8] >>> 0, a[9] >>> 0);
        });

        /* ---- occlusion queries ---- */

        define("GEN_QUERIES", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            for (const name of names)
                if (name && !this.queries.has(name))
                    this.queries.set(name, { name, result: 0, ready: true,
                                             active: false, slot: -1 });
        });
        define("DELETE_QUERIES", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            for (const name of names) this.queries.delete(name);
        });
        define("BEGIN_QUERY", function(a) { this.beginQuery(a[0] >>> 0, a[1] >>> 0); });
        define("END_QUERY", function(a) { this.endQuery(a[0] >>> 0); });
    }

    /* The guest observes these in the record it submitted, so the values are
     * part of the wire contract and not an internal detail. */
    const SYNC_STATUS_PENDING = 0;
    const SYNC_STATUS_OK = 1;
    const SYNC_STATUS_FAILED = 2;
    const READ_PIXELS_HEADER_SIZE = 32;

    /* openglproxy's V86GL_PROGRAM_PARAMETER_ENV / _LOCAL. */
    const PROGRAM_PARAMETER_ENV = 1;
    const PROGRAM_PARAMETER_LOCAL = 2;

    /*
     * V86GL_LOCATION_KIND_* and V86GL_ACTIVE_KIND_*. Uniform is 1 in both --
     * getting the pair the other way round is invisible until an application
     * asks for a uniform location and is handed an attribute's.
     */
    const LOCATION_KIND_ATTRIB = 2;
    const ACTIVE_KIND_ATTRIB = 2;

    /*
     * ARB program environment and local parameters are context state that the
     * extension lets an application set before -- or without ever -- handing
     * the target a program that compiles, so the storage is allocated with the
     * program object rather than at the end of a successful assemble.
     */
    function arbParameterStorage(program, kind) {
        if (!program.env) {
            program.env = new Float32Array(
                arbProgram.MAX_PROGRAM_PARAMETERS * 4);
            program.local = new Float32Array(
                arbProgram.MAX_PROGRAM_PARAMETERS * 4);
        }
        return kind === PROGRAM_PARAMETER_LOCAL ? program.local : program.env;
    }

    function installProgramHandlers(table) {
        const define = definer(table);

        define("CREATE_PROGRAM", function(a) {
            const name = a[0] >>> 0;
            const group = this.current.shareGroup;
            if (!group.programs.has(name))
                group.programs.set(name, createProgram(name));
        });
        define("CREATE_SHADER", function(a) {
            const name = a[0] >>> 0, type = a[1] >>> 0;
            const group = this.current.shareGroup;
            group.shaders.set(name, {
                name, type, source: "", compiled: null,
                stage: type === GL.VERTEX_SHADER ? "vertex" : "fragment",
            });
        });
        define("DELETE_PROGRAM", function(a) {
            this.current.shareGroup.programs.delete(a[0] >>> 0);
        });
        define("DELETE_SHADER", function(a) {
            this.current.shareGroup.shaders.delete(a[0] >>> 0);
        });
        define("ATTACH_SHADER", function(a) {
            const program = this.current.shareGroup.programs.get(a[0] >>> 0);
            if (program) program.shaders.add(a[1] >>> 0);
        });
        define("DETACH_SHADER", function(a) {
            const program = this.current.shareGroup.programs.get(a[0] >>> 0);
            if (program) program.shaders.delete(a[1] >>> 0);
        });
        define("SHADER_SOURCE", function(a, bytes, view, offset, size) {
            if (size < 8) throw new GLStreamError("glShaderSource record is short");
            const name = view.getUint32(offset, true);
            const length = view.getUint32(offset + 4, true);
            if (8 + length > size)
                throw new GLStreamError("glShaderSource record is truncated");
            const shader = this.current.shareGroup.shaders.get(name);
            if (!shader)
                return this.refuse("glShaderSource", "unknown shader",
                    { name }, GL.INVALID_VALUE);
            shader.source = readCString(bytes, offset + 8, length);
            shader.compiled = null;
        });
        define("COMPILE_SHADER", function(a) {
            const shader = this.current.shareGroup.shaders.get(a[0] >>> 0);
            if (!shader)
                return this.refuse("glCompileShader", "unknown shader",
                    { name: a[0] }, GL.INVALID_VALUE);
            shader.compiled = translator.compileShader(shader.source,
                shader.stage, this.options.translator || {});
        });
        define("LINK_PROGRAM", function(a) { this.linkProgram(a[0] >>> 0); });
        define("USE_PROGRAM", function(a) {
            this.current.currentProgram = a[0] >>> 0;
        });
        define("VALIDATE_PROGRAM", function(a) {
            const program = this.current.shareGroup.programs.get(a[0] >>> 0);
            if (program)
                program.validateLog = program.linked ? "" :
                    "program is not linked";
        });
        define("BIND_ATTRIB_LOCATION", function(a, bytes, view, offset, size) {
            if (size < 12) throw new GLStreamError("bind-attrib record is short");
            const programName = view.getUint32(offset, true);
            const location = view.getInt32(offset + 4, true);
            const nameLength = view.getUint32(offset + 8, true);
            const program = this.current.shareGroup.programs.get(programName);
            if (!program) return;
            const attribute = readCString(bytes, offset + 12, nameLength);
            // Takes effect at the next link, exactly as GL specifies.
            program.bindAttribLocations.set(attribute, location);
        });
        define("MAP_UNIFORM_LOCATION", function() { /* answered by QUERY_LOCATION */ });
        define("MAP_ATTRIB_LOCATION", function() { /* answered by QUERY_LOCATION */ });
        define("INVALIDATE_PROGRAM_LOCATIONS", function(a) {
            const program = this.current.shareGroup.programs.get(a[0] >>> 0);
            if (program) ++program.serial;
        });

        define("UNIFORM_FV", function(a, bytes, view, offset, size) {
            this.setUniformVector(view, offset, size, bytes, false);
        });
        define("UNIFORM_IV", function(a, bytes, view, offset, size) {
            this.setUniformVector(view, offset, size, bytes, true);
        });
        define("UNIFORM_MATRIX_FV", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("uniform-matrix record is short");
            const location = view.getInt32(offset, true);
            const dimension = view.getInt32(offset + 4, true);
            const count = view.getInt32(offset + 8, true);
            const transpose = view.getUint32(offset + 12, true) !== 0;
            this.setUniformMatrix(location, dimension, dimension, count,
                transpose, view, offset + 16, size - 16);
        });
        define("UNIFORM_MATRIX_RECT_FV", function(a, bytes, view, offset, size) {
            if (size < 20) throw new GLStreamError("uniform-matrix record is short");
            const location = view.getInt32(offset, true);
            const columns = view.getInt32(offset + 4, true);
            const rows = view.getInt32(offset + 8, true);
            const count = view.getInt32(offset + 12, true);
            const transpose = view.getUint32(offset + 16, true) !== 0;
            this.setUniformMatrix(location, columns, rows, count, transpose,
                view, offset + 20, size - 20);
        });

        /* ---- synchronous queries -------------------------------------- *
         *
         * These are answered in place, before the record's handler returns,
         * because the guest is blocked inside DeviceIoControl waiting for the
         * status word. That is only possible because the authoritative GL
         * state lives here: none of them touches the GPU (plan 4.8).
         */

        define("QUERY_ERROR", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("query-error record is short");
            const state = this.current;
            const error = state ? state.error : GL.NO_ERROR;
            if (state) state.error = GL.NO_ERROR;
            view.setUint32(offset, SYNC_STATUS_OK, true);
            view.setUint32(offset + 4, error, true);
        }, { needsContext: false });

        define("QUERY_INTEGER", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("query-integer record is short");
            const pname = view.getUint32(offset, true);
            const value = this.queryInteger(pname);
            if (value === null) {
                view.setUint32(offset + 4, SYNC_STATUS_FAILED, true);
                return;
            }
            view.setUint32(offset + 4, SYNC_STATUS_OK, true);
            view.setUint32(offset + 8, value >>> 0, true);
        }, { needsContext: false });

        define("QUERY_GL_STRING", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("query-string record is short");
            const pname = view.getUint32(offset, true);
            const capacity = view.getUint32(offset + 12, true);
            const text = this.queryString(pname);
            if (text === null || 16 + capacity > size) {
                view.setUint32(offset + 4, SYNC_STATUS_FAILED, true);
                return;
            }
            const encoded = [];
            for (let i = 0; i < text.length; ++i) encoded.push(text.charCodeAt(i) & 0xff);
            encoded.push(0);
            view.setUint32(offset + 4, SYNC_STATUS_OK, true);
            view.setUint32(offset + 8, encoded.length, true);
            const limit = Math.min(encoded.length, capacity);
            for (let i = 0; i < limit; ++i)
                bytes[offset + 16 + i] = encoded[i];
        }, { needsContext: false });

        define("QUERY_LOCATION", function(a, bytes, view, offset, size) {
            if (size < 32) throw new GLStreamError("query-location record is short");
            const kind = view.getUint32(offset, true);
            const programName = view.getUint32(offset + 4, true);
            const nameLength = view.getUint32(offset + 28, true);
            if (32 + nameLength > size) {
                view.setUint32(offset + 12, SYNC_STATUS_FAILED, true);
                return;
            }
            const name = readCString(bytes, offset + 32, nameLength);
            const info = this.queryLocation(kind, programName, name);
            if (!info) {
                view.setUint32(offset + 12, SYNC_STATUS_FAILED, true);
                return;
            }
            view.setUint32(offset + 12, SYNC_STATUS_OK, true);
            view.setUint32(offset + 16, info.location >>> 0, true);
            view.setUint32(offset + 20, info.type >>> 0, true);
            view.setUint32(offset + 24, info.size >>> 0, true);
        });

        define("QUERY_UNIFORM", function(a, bytes, view, offset, size) {
            if (size < 32) throw new GLStreamError("query-uniform record is short");
            const programName = view.getUint32(offset, true);
            const location = view.getInt32(offset + 4, true);
            const valueKind = view.getUint32(offset + 8, true);
            const values = this.readUniform(programName, location, valueKind);
            if (!values || !values.length || values.length > 16) {
                view.setUint32(offset + 12, SYNC_STATUS_FAILED, true);
                return;
            }
            view.setUint32(offset + 12, SYNC_STATUS_OK, true);
            view.setUint32(offset + 16, values.length, true);
            for (let i = 0; i < values.length; ++i) {
                if (valueKind === 2) view.setInt32(offset + 32 + i * 4, values[i] | 0, true);
                else view.setFloat32(offset + 32 + i * 4, values[i], true);
            }
        });

        define("QUERY_OBJECT_IV", function(a, bytes, view, offset, size) {
            if (size < 24) throw new GLStreamError("query-object record is short");
            const kind = view.getUint32(offset, true);
            const name = view.getUint32(offset + 4, true);
            const pname = view.getUint32(offset + 8, true);
            const value = this.queryObjectiv(kind, name, pname);
            if (value === null) {
                view.setUint32(offset + 12, SYNC_STATUS_FAILED, true);
                return;
            }
            view.setUint32(offset + 12, SYNC_STATUS_OK, true);
            view.setUint32(offset + 16, value >>> 0, true);
        });

        /*
         * {kind, name, buf_size, status, length, data_size} then the text.
         * The header is six words, so the string starts at 24 and not at 20:
         * openglproxy reads it back at sizeof(request), and four bytes of slip
         * here hand glGetShaderInfoLog a log that is missing its first word.
         */
        define("QUERY_OBJECT_LOG", function(a, bytes, view, offset, size) {
            if (size < 24) throw new GLStreamError("query-log record is short");
            const kind = view.getUint32(offset, true);
            const name = view.getUint32(offset + 4, true);
            const capacity = view.getUint32(offset + 8, true);
            const text = this.queryObjectLog(kind, name);
            if (text === null || 24 + capacity > size) {
                view.setUint32(offset + 12, SYNC_STATUS_FAILED, true);
                return;
            }
            view.setUint32(offset + 12, SYNC_STATUS_OK, true);
            view.setUint32(offset + 16,
                writeCString(bytes, offset + 24, capacity,
                    text), true);
        });

        /*
         * {kind, program, index, buf_size, status, length, size, type,
         * data_size, reserved} then the name -- ten words, so the name starts
         * at 40. length comes before size and type, which is the order
         * glGetActiveUniform's own out-parameters are in.
         */
        define("QUERY_ACTIVE", function(a, bytes, view, offset, size) {
            if (size < 40) throw new GLStreamError("query-active record is short");
            const kind = view.getUint32(offset, true);
            const programName = view.getUint32(offset + 4, true);
            const index = view.getUint32(offset + 8, true);
            const capacity = view.getUint32(offset + 12, true);
            const info = this.queryActive(kind, programName, index);
            if (!info || 40 + capacity > size) {
                view.setUint32(offset + 16, SYNC_STATUS_FAILED, true);
                return;
            }
            view.setUint32(offset + 16, SYNC_STATUS_OK, true);
            view.setUint32(offset + 20,
                writeCString(bytes, offset + 40, capacity,
                    info.name), true);
            view.setUint32(offset + 24, info.size >>> 0, true);
            view.setUint32(offset + 28, info.type >>> 0, true);
        });

        define("CHECK_FRAMEBUFFER_STATUS", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("fbo-status record is short");
            const target = view.getUint32(offset, true);
            const status = this.checkFramebufferStatus(target);
            view.setUint32(offset + 4, SYNC_STATUS_OK, true);
            view.setUint32(offset + 8, status >>> 0, true);
        });

        /* ---- ARB assembly programs ---- */

        define("GEN_PROGRAMS_ARB", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            const group = this.current.shareGroup;
            for (const name of names)
                if (name && !group.arbPrograms.has(name))
                    group.arbPrograms.set(name, {
                        name, target: 0, source: "", compiled: null,
                        env: null, local: null, variants: new Map(),
                    });
        });
        define("DELETE_PROGRAMS_ARB", function(a, bytes, view, offset, size) {
            const names = readNameArray(view, offset, size);
            for (const name of names)
                this.current.shareGroup.arbPrograms.delete(name);
        });
        define("BIND_PROGRAM_ARB", function(a) {
            const target = a[0] >>> 0, name = a[1] >>> 0;
            const group = this.current.shareGroup;
            if (name && !group.arbPrograms.has(name))
                group.arbPrograms.set(name, { name, target, source: "",
                    compiled: null, env: null, local: null,
                    variants: new Map() });
            if (target === GL.VERTEX_PROGRAM_ARB) this.current.arbVertexProgram = name;
            else this.current.arbFragmentProgram = name;
        });
        define("PROGRAM_STRING_ARB", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("ARB program record is short");
            const target = view.getUint32(offset, true);
            const length = view.getInt32(offset + 8, true);
            if (length < 0 || 16 + length > size)
                throw new GLStreamError("ARB program record is truncated");
            const source = readCString(bytes, offset + 16, length);
            this.programStringARB(target, source);
        });
        define("PROGRAM_PARAMETER_FV_ARB", function(a, bytes, view, offset, size) {
            this.programParameterARB(view, offset, size, false);
        });
        define("PROGRAM_PARAMETER_DV_ARB", function(a, bytes, view, offset, size) {
            this.programParameterARB(view, offset, size, true);
        });
        define("QUERY_PROGRAM_IV_ARB", function(a, bytes, view, offset, size) {
            if (size < 16) throw new GLStreamError("ARB query record is short");
            const target = view.getUint32(offset, true);
            const pname = view.getUint32(offset + 4, true);
            const value = this.queryProgramivARB(target, pname);
            view.setUint32(offset + 8, value === null ?
                SYNC_STATUS_FAILED : SYNC_STATUS_OK, true);
            if (value !== null) view.setUint32(offset + 12, value >>> 0, true);
        });
        define("QUERY_PROGRAM_PARAMETER_FV_ARB", function(a, bytes, view, offset, size) {
            this.queryProgramParameterARB(view, offset, size, false);
        });
        define("QUERY_PROGRAM_PARAMETER_DV_ARB", function(a, bytes, view, offset, size) {
            this.queryProgramParameterARB(view, offset, size, true);
        });
        /*
         * {target, pname, status, length, data_size, reserved} then the
         * program text at 24. glGetProgramStringARB has no buffer size of its
         * own -- the caller is expected to have asked GL_PROGRAM_LENGTH_ARB
         * first -- so data_size is what the guest allocated and the text is
         * written without a terminator, exactly as the extension specifies.
         */
        define("QUERY_PROGRAM_STRING_ARB", function(a, bytes, view, offset, size) {
            if (size < 24) throw new GLStreamError("ARB string record is short");
            const target = view.getUint32(offset, true);
            const capacity = view.getUint32(offset + 16, true);
            const program = this.arbProgramFor(target);
            if (!program || 24 + capacity > size) {
                view.setUint32(offset + 8, SYNC_STATUS_FAILED, true);
                return;
            }
            const text = program.source;
            const written = Math.min(text.length, capacity);
            view.setUint32(offset + 8, SYNC_STATUS_OK, true);
            view.setUint32(offset + 12, written, true);
            for (let i = 0; i < written; ++i)
                bytes[offset + 24 + i] = text.charCodeAt(i) & 0xff;
        });
    }

    /* ================================================================== */
    /* Draw records                                                       */
    /* ================================================================== */

    /*
     * A client-array block is {enabled, size, type, stride, dataSize} followed
     * by dataSize bytes; a draw record carries thirteen of them in a fixed
     * order -- vertex, colour, normal, eight texture coordinates, secondary
     * colour, fog coordinate -- because the guest packs every enabled array
     * into the same record rather than tracking pointers across calls.
     */
    const ARRAY_BLOCK_NAMES = ["vertex", "color", "normal",
        "texCoord0", "texCoord1", "texCoord2", "texCoord3",
        "texCoord4", "texCoord5", "texCoord6", "texCoord7",
        "secondaryColor", "fogCoord"];

    function parseArrayBlocks(bytes, view, offset, end, count) {
        const blocks = [];
        let at = offset;
        for (let i = 0; i < count; ++i) {
            if (at + 20 > end) return null;
            const enabled = view.getUint32(at, true) !== 0;
            const size = view.getInt32(at + 4, true);
            const type = view.getUint32(at + 8, true);
            const stride = view.getInt32(at + 12, true);
            const dataSize = view.getUint32(at + 16, true);
            at += 20;
            if (at + dataSize > end) return null;
            blocks.push({
                enabled, size: enabled ? size : 0, type: enabled ? type : 0,
                stride: enabled ? stride : 0,
                data: enabled && dataSize ?
                    bytes.subarray(at, at + dataSize) : null,
            });
            at += dataSize;
        }
        return { blocks, offset: at };
    }

    function parseGenericBlocks(bytes, view, offset, end, count) {
        const blocks = [];
        let at = offset;
        for (let i = 0; i < count; ++i) {
            if (at + 28 > end) return null;
            const index = view.getUint32(at, true);
            const normalized = view.getUint32(at + 4, true) !== 0;
            const enabled = view.getUint32(at + 8, true) !== 0;
            const size = view.getInt32(at + 12, true);
            const type = view.getUint32(at + 16, true);
            const stride = view.getInt32(at + 20, true);
            const dataSize = view.getUint32(at + 24, true);
            at += 28;
            if (at + dataSize > end) return null;
            blocks.push({
                index, normalized, enabled, size, type, stride,
                data: enabled && dataSize ?
                    bytes.subarray(at, at + dataSize) : null,
            });
            at += dataSize;
        }
        return { blocks, offset: at };
    }

    function installDrawHandlers(table) {
        const define = definer(table);

        const decodePackedDraw = (indexed, gl2) => function(a, bytes, view, offset, size) {
            const end = offset + size;
            let at = offset;
            const mode = view.getUint32(at, true); at += 4;
            const count = view.getInt32(at, true); at += 4;
            let indexType = 0;
            let indexDataSize = 0;
            if (indexed) {
                indexType = view.getUint32(at, true); at += 4;
                indexDataSize = view.getUint32(at, true); at += 4;
            }
            let blocksAt;
            let texUnitCount = 0;
            let hasSecondary = false;
            let hasFog = false;
            let genericCount = 0;
            let arrayCount;
            /*
             * Where the index data starts depends on the layout, which is why
             * it cannot be sliced while reading the fixed header. In the
             * multitexture layout the guest writes its own header -- magic,
             * the encoded array set, the client-active unit, and for the GL2
             * opcodes a generic-attribute count -- *between* the fixed header
             * and the indices. Slicing at the end of the fixed header instead
             * read the magic word as the first two indices: every
             * glDrawElements drew the wrong triangles, and did it quietly,
             * because the block offset below was computed correctly and
             * nothing downstream range-checks an index.
             */
            const magicAt = indexed ? offset + 16 : offset + 8;
            const magic = magicAt + 4 <= end ? view.getUint32(magicAt, true) : 0;
            let indexAt;
            if (magic === CLIENT_ARRAY_MT_MAGIC) {
                const encoded = view.getUint32(magicAt + 4, true);
                hasSecondary = (encoded & CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT) !== 0;
                hasFog = (encoded & CLIENT_ARRAY_MT_FOG_COORD_BIT) !== 0;
                texUnitCount = encoded &
                    ~(CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT | CLIENT_ARRAY_MT_FOG_COORD_BIT);
                if (texUnitCount > MAX_TEXTURE_COORDS)
                    throw new GLStreamError("too many texture-coordinate arrays");
                let headerEnd = magicAt + 12;      // magic, count, clientActive
                if (gl2) {
                    genericCount = view.getUint32(headerEnd, true);
                    headerEnd += 4;
                }
                indexAt = headerEnd;
                arrayCount = 3 + texUnitCount + (hasSecondary ? 1 : 0) +
                    (hasFog ? 1 : 0);
            } else {
                // The pre-multitexture layout: four blocks in vertex, colour,
                // texcoord, normal order, with the indices straight after the
                // fixed header. No shipping guest emits it any more, but the
                // state journal may still hold one.
                indexAt = offset + 16;
                arrayCount = 4;
            }

            let indexData = null;
            if (indexed) {
                if (indexAt + indexDataSize > end)
                    throw new GLStreamError("index data is truncated");
                indexData = indexDataSize ?
                    bytes.subarray(indexAt, indexAt + indexDataSize) : null;
            }
            blocksAt = indexed ? indexAt + indexDataSize :
                (magic === CLIENT_ARRAY_MT_MAGIC ? indexAt : offset + 8);

            const parsed = parseArrayBlocks(bytes, view, blocksAt, end, arrayCount);
            if (!parsed) throw new GLStreamError("client array blocks are truncated");
            let generic = null;
            if (genericCount) {
                generic = parseGenericBlocks(bytes, view, parsed.offset, end,
                    genericCount);
                if (!generic) throw new GLStreamError("generic attribute blocks are truncated");
            }

            const arrays = Object.create(null);
            if (magic === CLIENT_ARRAY_MT_MAGIC) {
                const order = ["vertex", "color", "normal"];
                for (let i = 0; i < texUnitCount; ++i) order.push("texCoord" + i);
                if (hasSecondary) order.push("secondaryColor");
                if (hasFog) order.push("fogCoord");
                order.forEach((name, i) => {
                    if (parsed.blocks[i] && parsed.blocks[i].enabled)
                        arrays[name] = parsed.blocks[i];
                });
            } else {
                ["vertex", "color", "texCoord0", "normal"].forEach((name, i) => {
                    if (parsed.blocks[i] && parsed.blocks[i].enabled)
                        arrays[name] = parsed.blocks[i];
                });
            }
            if (generic) {
                for (const block of generic.blocks)
                    if (block.enabled) arrays["generic" + block.index] = block;
            }

            this.drawPacked(mode, count, arrays, indexed ? {
                type: indexType, data: indexData,
            } : null);
        };

        define("DRAW_ARRAYS", decodePackedDraw(false, false));
        define("DRAW_ARRAYS_GL2", decodePackedDraw(false, true));
        define("DRAW_ELEMENTS", decodePackedDraw(true, false));
        define("DRAW_ELEMENTS_GL2", decodePackedDraw(true, true));

        /* The VBO-direct path: every enabled array lives in a buffer object,
         * so nothing but the parameters travels. This is the path M2's VBO
         * promotion makes the common one. */
        define("DRAW_ARRAYS_DIRECT", function(a) {
            this.drawFromBuffers(a[0] >>> 0, a[1] | 0, a[2] | 0, null);
        });
        define("DRAW_ELEMENTS_DIRECT", function(a, bytes, view, offset, size) {
            if (size < 24) throw new GLStreamError("direct draw record is short");
            this.drawFromBuffers(view.getUint32(offset, true), 0,
                view.getInt32(offset + 12, true), {
                    type: view.getUint32(offset + 16, true),
                    bufferOffset: view.getUint32(offset + 20, true),
                });
        });
        define("DRAW_RANGE_ELEMENTS_DIRECT", function(a, bytes, view, offset, size) {
            if (size < 24) throw new GLStreamError("direct draw record is short");
            this.drawFromBuffers(view.getUint32(offset, true), 0,
                view.getInt32(offset + 12, true), {
                    type: view.getUint32(offset + 16, true),
                    bufferOffset: view.getUint32(offset + 20, true),
                });
        });
        define("MULTI_DRAW_ARRAYS_DIRECT", function(a, bytes, view, offset, size) {
            if (size < 8) throw new GLStreamError("multi-draw record is short");
            const mode = view.getUint32(offset, true);
            const primcount = view.getUint32(offset + 4, true);
            for (let i = 0; i < primcount; ++i) {
                const at = offset + 8 + i * 8;
                if (at + 8 > offset + size) break;
                this.drawFromBuffers(mode, view.getInt32(at, true),
                    view.getInt32(at + 4, true), null);
            }
        });
        define("MULTI_DRAW_ELEMENTS_DIRECT", function(a, bytes, view, offset, size) {
            if (size < 12) throw new GLStreamError("multi-draw record is short");
            const mode = view.getUint32(offset, true);
            const type = view.getUint32(offset + 4, true);
            const primcount = view.getUint32(offset + 8, true);
            for (let i = 0; i < primcount; ++i) {
                const at = offset + 12 + i * 8;
                if (at + 8 > offset + size) break;
                this.drawFromBuffers(mode, 0, view.getInt32(at, true), {
                    type, bufferOffset: view.getUint32(at + 4, true),
                });
            }
        });

        /* ---- pixel rectangles ---- */

        define("READ_PIXELS", function(a, bytes, view, offset, size, metadata) {
            this.readPixels(bytes, view, offset, size, metadata);
        });
        define("DRAW_PIXELS", function(a, bytes, view, offset, size) {
            this.drawPixels(bytes, view, offset, size);
        });
        define("BITMAP", function(a, bytes, view, offset, size) {
            this.drawBitmap(bytes, view, offset, size);
        });
        define("COPY_PIXELS", function(a) {
            this.copyPixels(a[0] | 0, a[1] | 0, a[2] | 0, a[3] | 0, a[4] >>> 0);
        });
        define("POLYGON_STIPPLE", function(a, bytes, view, offset, size) {
            if (size < 128) throw new GLStreamError("polygon stipple is short");
            this.current.polygonStipple.set(
                bytes.subarray(offset, offset + 128));
            this.polygonStippleTexture = null;
        });
        const pixelMap = () => function(a, bytes, view, offset, size) {
            // Pixel maps only affect glDrawPixels/glReadPixels colour transfer,
            // which the executor applies on the CPU; the table is stored so the
            // path stays exact when GL_MAP_COLOR is on.
            if (size < 8) throw new GLStreamError("pixel map record is short");
            const map = view.getUint32(offset, true);
            const count = view.getUint32(offset + 4, true);
            const values = new Float32Array(count);
            for (let i = 0; i < count && 8 + i * 4 + 4 <= size; ++i)
                values[i] = view.getFloat32(offset + 8 + i * 4, true);
            this.pixelMaps = this.pixelMaps || Object.create(null);
            this.pixelMaps[map] = values;
        };
        define("PIXEL_MAPFV", pixelMap());
        define("PIXEL_MAPUIV", pixelMap());
        define("PIXEL_MAPUSV", pixelMap());
        define("ACCUM", function(a) { this.accum(a[0] >>> 0, a[1]); });

        define("QUERY_OBJECT_BATCH", function(a, bytes, view, offset, size, metadata) {
            this.queryObjectBatch(bytes, view, offset, size, metadata);
        });
    }

    /* ================================================================== */
    /* Executor: state                                                    */
    /* ================================================================== */

    Object.assign(GLWebGPUExecutor.prototype, {

        topOf(mode) {
            const s = this.current;
            if (mode === GL.TEXTURE)
                return s.textureStacks[s.activeTexture][
                    s.textureStacks[s.activeTexture].length - 1];
            const stack = s.stacks[mode] || s.stacks[GL.MODELVIEW];
            return stack[stack.length - 1];
        },

        topMatrix() { return this.topOf(this.current.matrixMode); },

        stackFor(mode) {
            const s = this.current;
            if (mode === GL.TEXTURE) return s.textureStacks[s.activeTexture];
            return s.stacks[mode] || s.stacks[GL.MODELVIEW];
        },

        pushMatrix() {
            const stack = this.stackFor(this.current.matrixMode);
            const depth = this.current.matrixMode === GL.MODELVIEW ?
                MODELVIEW_STACK_DEPTH : OTHER_STACK_DEPTH;
            if (stack.length >= depth)
                return this.refuse("glPushMatrix", "matrix stack overflow",
                    { mode: this.current.matrixMode }, GL.STACK_OVERFLOW);
            stack.push(new Float32Array(stack[stack.length - 1]));
        },

        popMatrix() {
            const stack = this.stackFor(this.current.matrixMode);
            if (stack.length <= 1)
                return this.refuse("glPopMatrix", "matrix stack underflow",
                    { mode: this.current.matrixMode }, GL.STACK_UNDERFLOW);
            stack.pop();
            this.matrixChanged();
        },

        multiplyTop(m) {
            const top = this.topMatrix();
            multiply4(top, m, SCRATCH_B);
            top.set(SCRATCH_B);
            this.matrixChanged();
        },

        /* The eye-space copies of every light are captured when glLight* is
         * called, per the GL spec, so a later modelview change must not move
         * them -- but a modelview change *does* change eye-space geometry, so
         * the derived state is rebuilt lazily rather than here. */
        matrixChanged() {
            this.derivedDirty = true;
        },

        setCapability(cap, value) {
            const s = this.current;
            const unit = s.textureUnits[s.activeTexture];
            switch (cap) {
            case GL.TEXTURE_1D: case GL.TEXTURE_2D:
            case GL.TEXTURE_3D: case GL.TEXTURE_CUBE_MAP:
                if (value) unit.enabledTargets.add(cap);
                else unit.enabledTargets.delete(cap);
                return;
            case GL.TEXTURE_GEN_S: case GL.TEXTURE_GEN_T:
            case GL.TEXTURE_GEN_R: case GL.TEXTURE_GEN_Q:
                unit.texGen[cap - GL.TEXTURE_GEN_S].enabled = value;
                return;
            case GL.POINT_SPRITE:
                unit.env.pointSprite = value;
                if (value) s.enabled.add(cap); else s.enabled.delete(cap);
                return;
            default: break;
            }
            if (cap >= GL.LIGHT0 && cap < GL.LIGHT0 + MAX_LIGHTS) {
                s.lights[cap - GL.LIGHT0].enabled = value;
                return;
            }
            if (cap >= GL.CLIP_PLANE0 && cap < GL.CLIP_PLANE0 + MAX_CLIP_PLANES) {
                if (value) s.enabled.add(cap); else s.enabled.delete(cap);
                return;
            }
            if (cap === GL.COLOR_MATERIAL) s.colorMaterial.enabled = value;
            if (value) s.enabled.add(cap); else s.enabled.delete(cap);
        },

        isEnabled(cap) { return this.current.enabled.has(cap); },

        setLightModel(pname, values, count) {
            const model = this.current.lightModel;
            switch (pname) {
            case GL.LIGHT_MODEL_AMBIENT:
                model.ambient.set(values.slice(0, 4));
                return;
            case GL.LIGHT_MODEL_TWO_SIDE: model.twoSide = values[0] !== 0; return;
            case GL.LIGHT_MODEL_LOCAL_VIEWER: model.localViewer = values[0] !== 0; return;
            case GL.LIGHT_MODEL_COLOR_CONTROL: model.colorControl = values[0] >>> 0; return;
            default:
                this.refuse("glLightModel", "unknown parameter",
                    { pname, count }, GL.INVALID_ENUM);
            }
        },

        setLight(lightEnum, pname, values) {
            const index = lightEnum - GL.LIGHT0;
            if (index < 0 || index >= MAX_LIGHTS)
                return this.refuse("glLight", "light index out of range",
                    { index }, GL.INVALID_ENUM);
            const light = this.current.lights[index];
            switch (pname) {
            case GL.AMBIENT: light.ambient.set(values.slice(0, 4)); return;
            case GL.DIFFUSE: light.diffuse.set(values.slice(0, 4)); return;
            case GL.SPECULAR: light.specular.set(values.slice(0, 4)); return;
            case GL.POSITION: {
                light.position.set(values.slice(0, 4));
                // GL transforms the position by the modelview in force *now*.
                transformPoint4(this.topOf(GL.MODELVIEW), light.position,
                    light.eyePosition);
                this.derivedDirty = true;
                return;
            }
            case GL.SPOT_DIRECTION: {
                light.spotDirection.set(values.slice(0, 3));
                const dir = new Float32Array([values[0], values[1], values[2], 0]);
                const eye = transformPoint4(this.topOf(GL.MODELVIEW), dir,
                    new Float32Array(4));
                light.eyeSpotDirection.set([eye[0], eye[1], eye[2]]);
                this.derivedDirty = true;
                return;
            }
            case GL.SPOT_EXPONENT: light.spotExponent = clamp(values[0], 0, 128); return;
            case GL.SPOT_CUTOFF: light.spotCutoff = values[0]; return;
            case GL.CONSTANT_ATTENUATION: light.constantAttenuation = values[0]; return;
            case GL.LINEAR_ATTENUATION: light.linearAttenuation = values[0]; return;
            case GL.QUADRATIC_ATTENUATION: light.quadraticAttenuation = values[0]; return;
            default:
                this.refuse("glLight", "unknown parameter", { pname },
                    GL.INVALID_ENUM);
            }
        },

        setMaterial(face, pname, values) {
            const targets = [];
            if (face === GL.FRONT || face === GL.FRONT_AND_BACK)
                targets.push(this.current.material.front);
            if (face === GL.BACK || face === GL.FRONT_AND_BACK)
                targets.push(this.current.material.back);
            if (!targets.length)
                return this.refuse("glMaterial", "invalid face", { face },
                    GL.INVALID_ENUM);
            for (const material of targets) {
                switch (pname) {
                case GL.AMBIENT: material.ambient.set(values.slice(0, 4)); break;
                case GL.DIFFUSE: material.diffuse.set(values.slice(0, 4)); break;
                case GL.SPECULAR: material.specular.set(values.slice(0, 4)); break;
                case GL.EMISSION: material.emission.set(values.slice(0, 4)); break;
                case GL.SHININESS: material.shininess = clamp(values[0], 0, 128); break;
                case GL.AMBIENT_AND_DIFFUSE:
                    material.ambient.set(values.slice(0, 4));
                    material.diffuse.set(values.slice(0, 4));
                    break;
                default:
                    this.refuse("glMaterial", "unknown parameter", { pname },
                        GL.INVALID_ENUM);
                    return;
                }
            }
            this.derivedDirty = true;
        },

        setFog(pname, values) {
            const fog = this.current.fog;
            switch (pname) {
            case GL.FOG_MODE: fog.mode = values[0] >>> 0; return;
            case GL.FOG_DENSITY: fog.density = values[0]; return;
            case GL.FOG_START: fog.start = values[0]; return;
            case GL.FOG_END: fog.end = values[0]; return;
            case GL.FOG_COLOR: fog.color.set(values.slice(0, 4)); return;
            case GL.FOG_COORD_SRC: fog.coordSource = values[0] >>> 0; return;
            case GL.FOG_INDEX: return;
            default:
                this.refuse("glFog", "unknown parameter", { pname },
                    GL.INVALID_ENUM);
            }
        },

        setTexEnv(target, pname, values) {
            const s = this.current;
            const unit = s.textureUnits[s.activeTexture];
            if (target === GL.POINT_SPRITE) {
                if (pname === GL.COORD_REPLACE) unit.env.pointSprite = values[0] !== 0;
                return;
            }
            if (target === GL.TEXTURE_FILTER_CONTROL) {
                if (pname === GL.TEXTURE_LOD_BIAS) unit.lodBias = values[0];
                return;
            }
            const env = unit.env;
            switch (pname) {
            case GL.TEXTURE_ENV_MODE: env.mode = values[0] >>> 0; return;
            case GL.TEXTURE_ENV_COLOR: env.color.set(values.slice(0, 4)); return;
            case GL.COMBINE_RGB: env.combineRGB = values[0] >>> 0; return;
            case GL.COMBINE_ALPHA: env.combineAlpha = values[0] >>> 0; return;
            case GL.SRC0_RGB: env.srcRGB[0] = values[0] >>> 0; return;
            case GL.SRC1_RGB: env.srcRGB[1] = values[0] >>> 0; return;
            case GL.SRC2_RGB: env.srcRGB[2] = values[0] >>> 0; return;
            case GL.SRC0_ALPHA: env.srcAlpha[0] = values[0] >>> 0; return;
            case GL.SRC1_ALPHA: env.srcAlpha[1] = values[0] >>> 0; return;
            case GL.SRC2_ALPHA: env.srcAlpha[2] = values[0] >>> 0; return;
            case GL.OPERAND0_RGB: env.operandRGB[0] = values[0] >>> 0; return;
            case GL.OPERAND1_RGB: env.operandRGB[1] = values[0] >>> 0; return;
            case GL.OPERAND2_RGB: env.operandRGB[2] = values[0] >>> 0; return;
            case GL.OPERAND0_ALPHA: env.operandAlpha[0] = values[0] >>> 0; return;
            case GL.OPERAND1_ALPHA: env.operandAlpha[1] = values[0] >>> 0; return;
            case GL.OPERAND2_ALPHA: env.operandAlpha[2] = values[0] >>> 0; return;
            case GL.RGB_SCALE: env.rgbScale = clamp(Math.round(values[0]), 1, 4); return;
            case GL.ALPHA_SCALE: env.alphaScale = clamp(Math.round(values[0]), 1, 4); return;
            default:
                this.refuse("glTexEnv", "unknown parameter", { pname },
                    GL.INVALID_ENUM);
            }
        },

        setTexGen(coord, pname, values) {
            const s = this.current;
            const unit = s.textureUnits[s.activeTexture];
            const index = coord - GL.S;
            if (index < 0 || index > 3)
                return this.refuse("glTexGen", "invalid coordinate", { coord },
                    GL.INVALID_ENUM);
            const gen = unit.texGen[index];
            switch (pname) {
            case GL.TEXTURE_GEN_MODE: gen.mode = values[0] >>> 0; return;
            case GL.OBJECT_PLANE: gen.objectPlane.set(values.slice(0, 4)); return;
            case GL.EYE_PLANE: {
                // Like glClipPlane, the eye plane is stored transformed by the
                // inverse modelview so the shader can dot it with the eye-space
                // position directly.
                const inv = invert4(this.topOf(GL.MODELVIEW), SCRATCH_B);
                const t = transpose4(inv, SCRATCH_C);
                transformPoint4(t, new Float32Array(values.slice(0, 4)),
                    gen.eyePlane);
                return;
            }
            default:
                this.refuse("glTexGen", "unknown parameter", { pname },
                    GL.INVALID_ENUM);
            }
        },

        setTexParameter(target, pname, values) {
            const texture = this.boundTexture(target);
            if (!texture)
                return this.refuse("glTexParameter", "no texture bound",
                    { target }, GL.INVALID_OPERATION);
            const sampler = texture.sampler;
            switch (pname) {
            case GL.TEXTURE_MIN_FILTER: sampler.minFilter = values[0] >>> 0; break;
            case GL.TEXTURE_MAG_FILTER: sampler.magFilter = values[0] >>> 0; break;
            case GL.TEXTURE_WRAP_S: sampler.wrapS = values[0] >>> 0; break;
            case GL.TEXTURE_WRAP_T: sampler.wrapT = values[0] >>> 0; break;
            case GL.TEXTURE_WRAP_R: sampler.wrapR = values[0] >>> 0; break;
            case GL.TEXTURE_MIN_LOD: sampler.minLod = values[0]; break;
            case GL.TEXTURE_MAX_LOD: sampler.maxLod = values[0]; break;
            case GL.TEXTURE_BASE_LEVEL: sampler.baseLevel = values[0] | 0; break;
            case GL.TEXTURE_MAX_LEVEL: sampler.maxLevel = values[0] | 0; break;
            case GL.TEXTURE_LOD_BIAS: sampler.lodBias = values[0]; break;
            case GL.TEXTURE_BORDER_COLOR:
                sampler.borderColor.set(values.slice(0, 4));
                break;
            case GL.TEXTURE_MAX_ANISOTROPY_EXT:
                sampler.maxAnisotropy = clamp(Math.round(values[0]), 1, 16);
                break;
            case GL.TEXTURE_COMPARE_MODE: sampler.compareMode = values[0] >>> 0; break;
            case GL.TEXTURE_COMPARE_FUNC: sampler.compareFunc = values[0] >>> 0; break;
            case GL.DEPTH_TEXTURE_MODE: sampler.depthTextureMode = values[0] >>> 0; break;
            case GL.GENERATE_MIPMAP: texture.generateMipmapHint = values[0] !== 0; break;
            case GL.TEXTURE_PRIORITY: break;
            default:
                return this.refuse("glTexParameter", "unknown parameter",
                    { pname }, GL.INVALID_ENUM);
            }
            texture.viewCache.clear();
            this.invalidateBindGroups();
        },

        setPixelStore(pname, value) {
            const store = this.current.pixelStore;
            switch (pname) {
            case GL.UNPACK_ALIGNMENT: store.unpackAlignment = value; return;
            case GL.UNPACK_ROW_LENGTH: store.unpackRowLength = value; return;
            case GL.UNPACK_SKIP_PIXELS: store.unpackSkipPixels = value; return;
            case GL.UNPACK_SKIP_ROWS: store.unpackSkipRows = value; return;
            case GL.UNPACK_SKIP_IMAGES: store.unpackSkipImages = value; return;
            case GL.UNPACK_IMAGE_HEIGHT: store.unpackImageHeight = value; return;
            case GL.UNPACK_SWAP_BYTES: store.unpackSwapBytes = value !== 0; return;
            case GL.UNPACK_LSB_FIRST: store.unpackLsbFirst = value !== 0; return;
            case GL.PACK_ALIGNMENT: store.packAlignment = value; return;
            case GL.PACK_ROW_LENGTH: store.packRowLength = value; return;
            case GL.PACK_SKIP_PIXELS: store.packSkipPixels = value; return;
            case GL.PACK_SKIP_ROWS: store.packSkipRows = value; return;
            case GL.PACK_SWAP_BYTES: store.packSwapBytes = value !== 0; return;
            case GL.PACK_LSB_FIRST: store.packLsbFirst = value !== 0; return;
            default:
                this.refuse("glPixelStorei", "unknown parameter", { pname },
                    GL.INVALID_ENUM);
            }
        },

        setPixelTransfer(pname, value) {
            const t = this.current.pixelTransfer;
            switch (pname) {
            case GL.RED_SCALE: t.redScale = value; return;
            case GL.GREEN_SCALE: t.greenScale = value; return;
            case GL.BLUE_SCALE: t.blueScale = value; return;
            case GL.ALPHA_SCALE: t.alphaScale = value; return;
            case GL.RED_BIAS: t.redBias = value; return;
            case GL.GREEN_BIAS: t.greenBias = value; return;
            case GL.BLUE_BIAS: t.blueBias = value; return;
            case GL.ALPHA_BIAS: t.alphaBias = value; return;
            case GL.MAP_COLOR: t.mapColor = value !== 0; return;
            case GL.POST_COLOR_MATRIX_RED_SCALE_SGI:
            case GL.POST_COLOR_MATRIX_GREEN_SCALE_SGI:
            case GL.POST_COLOR_MATRIX_BLUE_SCALE_SGI:
            case GL.POST_COLOR_MATRIX_ALPHA_SCALE_SGI:
                t.postColorMatrixScale[pname -
                    GL.POST_COLOR_MATRIX_RED_SCALE_SGI] = value;
                return;
            case GL.POST_COLOR_MATRIX_RED_BIAS_SGI:
            case GL.POST_COLOR_MATRIX_GREEN_BIAS_SGI:
            case GL.POST_COLOR_MATRIX_BLUE_BIAS_SGI:
            case GL.POST_COLOR_MATRIX_ALPHA_BIAS_SGI:
                t.postColorMatrixBias[pname -
                    GL.POST_COLOR_MATRIX_RED_BIAS_SGI] = value;
                return;
            default: return;    // depth/stencil scales have no effect here
            }
        },

        setPointParameter(pname, values) {
            const point = this.current.point;
            switch (pname) {
            case GL.POINT_SIZE_MIN: point.sizeMin = values[0]; return;
            case GL.POINT_SIZE_MAX: point.sizeMax = values[0]; return;
            case GL.POINT_FADE_THRESHOLD_SIZE: point.fadeThreshold = values[0]; return;
            case GL.POINT_DISTANCE_ATTENUATION:
                point.attenuation.set([values[0], values[1] || 0, values[2] || 0]);
                return;
            case GL.POINT_SPRITE_COORD_ORIGIN:
                point.spriteCoordOrigin = values[0] >>> 0;
                return;
            default:
                this.refuse("glPointParameter", "unknown parameter", { pname },
                    GL.INVALID_ENUM);
            }
        },

        setClientState(cap, value) {
            const s = this.current;
            const name = {
                [GL.VERTEX_ARRAY]: "vertex",
                [GL.COLOR_ARRAY]: "color",
                [GL.NORMAL_ARRAY]: "normal",
                [GL.SECONDARY_COLOR_ARRAY]: "secondaryColor",
                [GL.FOG_COORD_ARRAY]: "fogCoord",
                [GL.EDGE_FLAG_ARRAY]: "edgeFlag",
                [GL.INDEX_ARRAY]: "index",
            }[cap];
            if (name) {
                this.arrayState(name).enabled = value;
                return;
            }
            if (cap === GL.TEXTURE_COORD_ARRAY) {
                this.arrayState("texCoord" + s.clientActiveTexture).enabled = value;
                return;
            }
            this.refuse("glEnableClientState", "unknown array", { cap },
                GL.INVALID_ENUM);
        },

        arrayState(name) {
            const s = this.current;
            s.arrays = s.arrays || Object.create(null);
            if (!s.arrays[name])
                s.arrays[name] = { enabled: false, buffer: 0, size: 4,
                                   type: GL.FLOAT, stride: 0, offset: 0,
                                   normalized: false, fromVBO: false };
            return s.arrays[name];
        },

        setArrayPointer(name, array) {
            Object.assign(this.arrayState(name), array);
        },

        boundTexture(target) {
            const s = this.current;
            const unit = s.textureUnits[s.activeTexture];
            let key = target;
            // A cube face parameter names the cube map itself.
            if (target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X &&
                    target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z)
                key = GL.TEXTURE_CUBE_MAP;
            const name = unit.bindings[key];
            if (!name) return null;
            return s.shareGroup.textures.get(name) || null;
        },

        invalidateBindGroups() {
            this.bindGroupsDirty = true;
        },

        retire(object) {
            if (object && typeof object.destroy === "function") {
                // Destroying inside a recorded frame would invalidate commands
                // already encoded, so the object is released after the frame
                // that may still reference it.
                this.retired = this.retired || [];
                this.retired.push(object);
            }
        },

        releaseRetired() {
            if (!this.retired || !this.retired.length) return;
            for (const object of this.retired) {
                try { object.destroy(); } catch (error) { /* already gone */ }
            }
            this.retired.length = 0;
        },
    });

    /* ================================================================== */
    /* Executor: textures                                                 */
    /* ================================================================== */

    const CUBE_FACE_INDEX = {};
    for (let i = 0; i < 6; ++i)
        CUBE_FACE_INDEX[GL.TEXTURE_CUBE_MAP_POSITIVE_X + i] = i;

    Object.assign(GLWebGPUExecutor.prototype, {

        textureTargetKind(target) {
            if (target === GL.TEXTURE_1D) return "1D";
            if (target === GL.TEXTURE_3D) return "3D";
            if (CUBE_FACE_INDEX[target] !== undefined ||
                    target === GL.TEXTURE_CUBE_MAP) return "Cube";
            return "2D";
        },

        /*
         * Levels are stored on the CPU and the GPU texture is (re)built lazily.
         *
         * GL lets an application define level 3 before level 0 and change a
         * level's size after the fact, neither of which a GPUTexture allows:
         * its size and mip count are fixed at creation. Keeping the authored
         * levels means the texture can be rebuilt whenever the shape changes,
         * which is also what makes device loss recoverable without the guest
         * noticing.
         */
        texImage(target, level, internalFormat, width, height, depth,
                format, type, data) {
            const texture = this.boundTexture(target);
            if (!texture)
                return this.refuse("glTexImage", "no texture bound", { target },
                    GL.INVALID_OPERATION);
            const kind = this.textureTargetKind(target);
            texture.target = kind === "Cube" ? GL.TEXTURE_CUBE_MAP : target;
            const storage = storageFormatFor(internalFormat, this.deviceFeatures);
            const face = CUBE_FACE_INDEX[target] || 0;

            let pixels = null;
            let pixelLayout = {
                bytesPerTexel: storage.palettised ? 1 :
                    (storage.gpu === "rgba16float" ? 8 :
                        (storage.gpu === "rgba32float" ? 16 :
                            (storage.gpu === "depth16unorm" ? 2 : 4))),
                componentType: storage.gpu === "rgba16float" ? "f16" :
                    (storage.gpu === "depth16unorm" ? "u16" :
                        (storage.gpu === "rgba32float" || storage.depth ? "f32" : "u8")),
                componentCount: storage.palettised || storage.depth ? 1 : 4,
            };
            if (data && data.byteLength) {
                const converted = convertPixelsForStorage(data, 0, width, height,
                    depth, format, type, storage, {
                        rowLength: this.current.pixelStore.unpackRowLength,
                        skipPixels: this.current.pixelStore.unpackSkipPixels,
                        skipRows: this.current.pixelStore.unpackSkipRows,
                        skipImages: this.current.pixelStore.unpackSkipImages,
                        imageHeight: this.current.pixelStore.unpackImageHeight,
                        alignment: this.current.pixelStore.unpackAlignment,
                    });
                if (!converted)
                    return this.refuse("glTexImage", "unsupported pixel format",
                        { format, type, internalFormat }, GL.INVALID_ENUM);
                pixels = converted.pixels;
                pixelLayout = converted;
            }

            this.storeLevel(texture, kind, level, face, {
                width, height, depth,
                gpuFormat: storage.gpu, base: storage.base,
                compressed: false, pixels,
                bytesPerTexel: pixelLayout.bytesPerTexel,
                componentType: pixelLayout.componentType,
                componentCount: pixelLayout.componentCount,
            });
            ++this.stats.textureUploads;
            if (pixels) this.stats.textureBytes += pixels.byteLength;
            if (texture.generateMipmapHint && level === 0)
                this.generateMipmapFor(texture);
        },

        texSubImage(target, level, x, y, z, width, height, depth, format, type,
                data) {
            const texture = this.boundTexture(target);
            if (!texture)
                return this.refuse("glTexSubImage", "no texture bound", { target },
                    GL.INVALID_OPERATION);
            const kind = this.textureTargetKind(target);
            const face = CUBE_FACE_INDEX[target] || 0;
            const slot = this.levelSlot(texture, kind, level, face);
            if (!slot || !slot.pixels)
                return this.refuse("glTexSubImage", "level has no storage",
                    { level, face }, GL.INVALID_OPERATION);
            if (slot.compressed)
                return this.refuse("glTexSubImage",
                    "uncompressed update of a compressed level", { level },
                    GL.INVALID_OPERATION);
            const storage = { gpu: slot.gpuFormat, base: slot.base,
                float: slot.componentType === "f16" ||
                    (slot.componentType === "f32" && slot.componentCount === 4),
                depth: slot.base === "DEPTH",
                palettised: slot.base === "COLOR_INDEX" };
            const converted = convertPixelsForStorage(data, 0, width, height,
                depth, format, type, storage, {
                    rowLength: this.current.pixelStore.unpackRowLength,
                    skipPixels: this.current.pixelStore.unpackSkipPixels,
                    skipRows: this.current.pixelStore.unpackSkipRows,
                    skipImages: this.current.pixelStore.unpackSkipImages,
                    imageHeight: this.current.pixelStore.unpackImageHeight,
                    alignment: this.current.pixelStore.unpackAlignment,
                });
            if (!converted)
                return this.refuse("glTexSubImage", "unsupported pixel format",
                    { format, type }, GL.INVALID_ENUM);
            // Patch the shadow copy; the GPU upload happens with the rest of
            // the level so a run of subimage calls costs one writeTexture.
            for (let sz = 0; sz < depth; ++sz) {
                for (let sy = 0; sy < height; ++sy) {
                    const bytesPerTexel = slot.bytesPerTexel || 4;
                    const dstRow = (((z + sz) * slot.height + (y + sy)) *
                        slot.width + x) * bytesPerTexel;
                    const srcRow = ((sz * height + sy) * width) * bytesPerTexel;
                    if (dstRow < 0 ||
                            dstRow + width * bytesPerTexel > slot.pixels.length)
                        continue;
                    slot.pixels.set(
                        converted.pixels.subarray(srcRow,
                            srcRow + width * bytesPerTexel), dstRow);
                }
            }
            texture.dirty = true;
            ++this.stats.textureUploads;
        },

        compressedTexImage(target, level, internalFormat, width, height, depth,
                data) {
            const texture = this.boundTexture(target);
            if (!texture)
                return this.refuse("glCompressedTexImage", "no texture bound",
                    { target }, GL.INVALID_OPERATION);
            const kind = this.textureTargetKind(target);
            texture.target = kind === "Cube" ? GL.TEXTURE_CUBE_MAP : target;
            const storage = storageFormatFor(internalFormat, this.deviceFeatures);
            const face = CUBE_FACE_INDEX[target] || 0;
            if (storage.decodeDXT) {
                // No BC support on this adapter: decode deterministically so
                // the picture is identical and only the memory differs.
                ++this.stats.dxtDecodes;
                const pixels = decodeDXT(storage.decodeDXT, data, width, height);
                this.storeLevel(texture, kind, level, face, {
                    width, height, depth: 1, gpuFormat: "rgba8unorm",
                    base: storage.base, compressed: false, pixels,
                    bytesPerTexel: 4, componentType: "u8", componentCount: 4,
                });
                return;
            }
            this.storeLevel(texture, kind, level, face, {
                width, height, depth,
                gpuFormat: storage.gpu, base: storage.base,
                compressed: true, blockBytes: storage.blockBytes,
                pixels: data.slice(),
            });
        },

        compressedTexSubImage(target, level, x, y, z, width, height, depth,
                format, data) {
            const texture = this.boundTexture(target);
            if (!texture) return;
            const kind = this.textureTargetKind(target);
            const face = CUBE_FACE_INDEX[target] || 0;
            const slot = this.levelSlot(texture, kind, level, face);
            if (!slot)
                return this.refuse("glCompressedTexSubImage", "level has no storage",
                    { level }, GL.INVALID_OPERATION);
            if (!slot.compressed) {
                const storage = storageFormatFor(format, this.deviceFeatures);
                if (!storage.decodeDXT) {
                    return this.refuse("glCompressedTexSubImage",
                        "compressed update of an uncompressed level", { level },
                        GL.INVALID_OPERATION);
                }
                ++this.stats.dxtDecodes;
                const pixels = decodeDXT(storage.decodeDXT, data, width, height);
                for (let sy = 0; sy < height; ++sy) {
                    const dst = ((y + sy) * slot.width + x) * 4;
                    const src = sy * width * 4;
                    if (dst < 0 || dst + width * 4 > slot.pixels.length) continue;
                    slot.pixels.set(pixels.subarray(src, src + width * 4), dst);
                }
                texture.dirty = true;
                return;
            }
            if ((x & 3) || (y & 3))
                return this.refuse("glCompressedTexSubImage",
                    "the rectangle is not block aligned", { x, y },
                    GL.INVALID_OPERATION);
            const blocksPerRow = Math.ceil(slot.width / 4);
            const srcBlocksPerRow = Math.ceil(width / 4);
            const blockBytes = slot.blockBytes;
            for (let by = 0; by < Math.ceil(height / 4); ++by) {
                const dst = (((y >> 2) + by) * blocksPerRow + (x >> 2)) * blockBytes;
                const src = by * srcBlocksPerRow * blockBytes;
                if (dst + srcBlocksPerRow * blockBytes > slot.pixels.length) break;
                slot.pixels.set(
                    data.subarray(src, src + srcBlocksPerRow * blockBytes), dst);
            }
            texture.dirty = true;
        },

        levelSlot(texture, kind, level, face) {
            const layer = kind === "Cube" ? face : 0;
            const list = texture.levels[layer];
            return list ? list[level] : null;
        },

        storeLevel(texture, kind, level, face, slot) {
            const layer = kind === "Cube" ? face : 0;
            if (!texture.levels[layer]) texture.levels[layer] = [];
            texture.levels[layer][level] = slot;
            texture.kind = kind;
            texture.baseFormat = slot.base;
            texture.gpuFormat = slot.gpuFormat;
            texture.dirty = true;
            texture.viewCache.clear();
            this.invalidateBindGroups();
        },

        /*
         * GL's completeness rules: every mip level from base to max must exist
         * with the right halved size and the same format, or the texture
         * samples as opaque black. Games rely on this to disable a texture by
         * simply not uploading its mips, so it is a behaviour and not a
         * validation nicety.
         */
        textureIsComplete(texture) {
            if (!texture || !texture.levels.length) return false;
            const layers = texture.kind === "Cube" ? 6 : 1;
            const base = texture.levels[0] && texture.levels[0][0];
            if (!base) return false;
            const mipmapped = isMipmapFilter(texture.sampler.minFilter);
            const levelCount = mipmapped ?
                fullMipLevelCount(base.width, base.height, base.depth) : 1;
            for (let layer = 0; layer < layers; ++layer) {
                const list = texture.levels[layer];
                if (!list) return false;
                for (let level = 0; level < levelCount; ++level) {
                    const slot = list[level];
                    if (!slot) return false;
                    if (slot.gpuFormat !== base.gpuFormat) return false;
                    if (slot.width !== Math.max(1, base.width >> level) ||
                            slot.height !== Math.max(1, base.height >> level))
                        return false;
                }
                if (texture.kind === "Cube" &&
                        (base.width !== base.height)) return false;
            }
            return true;
        },

        /* Rebuilds the GPUTexture from the authored levels when anything about
         * the shape changed. */
        ensureTextureUploaded(texture) {
            if (!texture) return null;
            const base = texture.levels[0] && texture.levels[0][0];
            if (!base) return null;
            const layers = texture.kind === "Cube" ? 6 : 1;
            const mipmapped = isMipmapFilter(texture.sampler.minFilter);
            const levelCount = mipmapped ?
                Math.min(fullMipLevelCount(base.width, base.height, base.depth),
                    (texture.levels[0] || []).length) : 1;
            const wanted = {
                width: base.width, height: base.height,
                depth: texture.kind === "3D" ? base.depth : 1,
                layers, levelCount: Math.max(1, levelCount),
                format: base.gpuFormat,
            };
            const shapeChanged = !texture.gpuTexture ||
                texture.gpuWidth !== wanted.width ||
                texture.gpuHeight !== wanted.height ||
                texture.gpuDepth !== wanted.depth ||
                texture.gpuLayers !== wanted.layers ||
                texture.gpuLevels !== wanted.levelCount ||
                texture.gpuFormat !== wanted.format;
            if (!shapeChanged && !texture.dirty) return texture.gpuTexture;

            if (shapeChanged) {
                const oldTexture = texture.gpuTexture;
                const oldWidth = texture.gpuWidth || 0;
                const oldHeight = texture.gpuHeight || 0;
                const oldDepth = texture.gpuDepth || 1;
                const oldLayers = texture.gpuLayers || 1;
                const oldLevels = texture.gpuLevels || 0;
                const oldFormat = texture.gpuFormat;
                texture.viewCache.clear();
                const dimension = texture.kind === "3D" ? "3d" : "2d";
                texture.gpuTexture = this.device.createTexture({
                    label: "GL texture " + texture.name,
                    size: {
                        width: wanted.width, height: wanted.height,
                        depthOrArrayLayers: texture.kind === "3D" ?
                            wanted.depth : wanted.layers,
                    },
                    dimension,
                    mipLevelCount: wanted.levelCount,
                    format: wanted.format,
                    usage: TEXTURE_USAGE_TEXTURE_BINDING | TEXTURE_USAGE_COPY_DST |
                        TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_RENDER_ATTACHMENT,
                });
                texture.gpuWidth = wanted.width;
                texture.gpuHeight = wanted.height;
                texture.gpuDepth = wanted.depth;
                texture.gpuLayers = wanted.layers;
                texture.gpuLevels = wanted.levelCount;
                texture.gpuFormat = wanted.format;

                // A level produced by CopyTexImage/FBO rendering has no CPU
                // shadow.  Growing the texture to add mip levels must preserve
                // those GPU-authored texels before the old allocation retires.
                if (oldTexture && oldFormat === wanted.format &&
                        oldWidth === wanted.width && oldHeight === wanted.height &&
                        oldDepth === wanted.depth) {
                    this.endPass();
                    const encoder = this.ensureEncoder();
                    const copyLevels = Math.min(oldLevels, wanted.levelCount);
                    const copyLayers = Math.min(oldLayers, wanted.layers);
                    for (let layer = 0; layer < copyLayers; ++layer) {
                        const list = texture.levels[layer] || [];
                        for (let level = 0; level < copyLevels; ++level) {
                            const slot = list[level];
                            if (!slot || slot.pixels) continue;
                            encoder.copyTextureToTexture(
                                { texture: oldTexture, mipLevel: level,
                                  origin: { x: 0, y: 0,
                                    z: texture.kind === "3D" ? 0 : layer } },
                                { texture: texture.gpuTexture, mipLevel: level,
                                  origin: { x: 0, y: 0,
                                    z: texture.kind === "3D" ? 0 : layer } },
                                { width: Math.max(1, wanted.width >> level),
                                  height: Math.max(1, wanted.height >> level),
                                  depthOrArrayLayers: texture.kind === "3D" ?
                                    Math.max(1, wanted.depth >> level) : 1 });
                        }
                    }
                }
                this.retire(oldTexture);
            }

            for (let layer = 0; layer < layers; ++layer) {
                const list = texture.levels[layer] || [];
                for (let level = 0; level < wanted.levelCount; ++level) {
                    const slot = list[level];
                    if (!slot || !slot.pixels) continue;
                    this.writeTextureLevel(texture, slot, level, layer);
                }
            }
            texture.dirty = false;
            return texture.gpuTexture;
        },

        writeTextureLevel(texture, slot, level, layer) {
            const bytesPerRow = slot.compressed ?
                Math.ceil(slot.width / 4) * slot.blockBytes :
                slot.width * (slot.bytesPerTexel || 4);
            const rowsPerImage = slot.compressed ?
                Math.ceil(slot.height / 4) : slot.height;
            const origin = texture.kind === "3D" ?
                { x: 0, y: 0, z: 0 } : { x: 0, y: 0, z: layer };
            this.device.queue.writeTexture(
                { texture: texture.gpuTexture, mipLevel: level, origin },
                slot.pixels,
                { offset: 0, bytesPerRow, rowsPerImage },
                {
                    width: slot.width, height: slot.height,
                    depthOrArrayLayers: texture.kind === "3D" ? slot.depth : 1,
                });
        },

        /*
         * WebGPU has no mipmap generator, so each level is produced by a
         * full-screen draw from the one above. Box filtering by bilinear
         * sampling at the half-texel centres is exactly the 2x2 average the GL
         * spec describes for power-of-two levels.
         */
        generateMipmap(target) {
            const texture = this.boundTexture(target);
            if (!texture)
                return this.refuse("glGenerateMipmap", "no texture bound",
                    { target }, GL.INVALID_OPERATION);
            this.generateMipmapFor(texture);
        },

        generateMipmapFor(texture) {
            const layers = texture.kind === "Cube" ? 6 : 1;
            let needsGPUGeneration = false;
            for (let layer = 0; layer < layers; ++layer) {
                const list = texture.levels[layer];
                if (!list || !list[0]) continue;
                const base = list[0];
                if (base.compressed) {
                    this.refuse("glGenerateMipmap",
                        "a compressed level cannot be downsampled",
                        { name: texture.name }, GL.INVALID_OPERATION);
                    continue;
                }
                const levels = fullMipLevelCount(base.width, base.height, base.depth);
                let source = base;
                for (let level = 1; level < levels; ++level) {
                    const width = Math.max(1, source.width >> 1);
                    const height = Math.max(1, source.height >> 1);
                    const depth = texture.kind === "3D" ?
                        Math.max(1, source.depth >> 1) : 1;
                    const pixels = source.pixels ?
                        downsampleBoxStorage(source, width, height, depth) : null;
                    const slot = {
                        width, height, depth, gpuFormat: base.gpuFormat,
                        base: base.base, compressed: false, pixels,
                        bytesPerTexel: base.bytesPerTexel || 4,
                        componentType: base.componentType || "u8",
                        componentCount: base.componentCount || 4,
                    };
                    list[level] = slot;
                    source = slot;
                    if (!pixels) needsGPUGeneration = true;
                }
                list.length = levels;
            }
            texture.dirty = true;
            texture.viewCache.clear();
            this.invalidateBindGroups();
            if (needsGPUGeneration)
                this.generateTextureMipmapsGPU(texture);
        },
    });

    function isMipmapFilter(filter) {
        return filter === GL.NEAREST_MIPMAP_NEAREST ||
            filter === GL.LINEAR_MIPMAP_NEAREST ||
            filter === GL.NEAREST_MIPMAP_LINEAR ||
            filter === GL.LINEAR_MIPMAP_LINEAR;
    }

    function fullMipLevelCount(width, height, depth) {
        return Math.floor(Math.log2(Math.max(width, height, depth || 1))) + 1;
    }

    function downsampleBox(src, srcW, srcH, srcD, dstW, dstH, dstD) {
        const out = new Uint8Array(dstW * dstH * dstD * 4);
        const xScale = srcW / dstW, yScale = srcH / dstH;
        const zScale = (srcD || 1) / (dstD || 1);
        for (let z = 0; z < dstD; ++z) {
            for (let y = 0; y < dstH; ++y) {
                for (let x = 0; x < dstW; ++x) {
                    const x0 = Math.floor(x * xScale), y0 = Math.floor(y * yScale);
                    const z0 = Math.floor(z * zScale);
                    const x1 = Math.min(srcW - 1, x0 + (xScale > 1 ? 1 : 0));
                    const y1 = Math.min(srcH - 1, y0 + (yScale > 1 ? 1 : 0));
                    const z1 = Math.min((srcD || 1) - 1, z0 + (zScale > 1 ? 1 : 0));
                    const dst = ((z * dstH + y) * dstW + x) * 4;
                    for (let c = 0; c < 4; ++c) {
                        let sum = 0;
                        let n = 0;
                        for (const sz of z0 === z1 ? [z0] : [z0, z1]) {
                            for (const sy of y0 === y1 ? [y0] : [y0, y1]) {
                                for (const sx of x0 === x1 ? [x0] : [x0, x1]) {
                                    sum += src[((sz * srcH + sy) * srcW + sx) * 4 + c];
                                    ++n;
                                }
                            }
                        }
                        out[dst + c] = Math.round(sum / n);
                    }
                }
            }
        }
        return out;
    }

    function downsampleBoxStorage(source, dstW, dstH, dstD) {
        const srcW = source.width, srcH = source.height, srcD = source.depth || 1;
        const componentType = source.componentType || "u8";
        const componentCount = source.componentCount || 4;
        const bytesPerTexel = source.bytesPerTexel || 4;
        if (componentType === "u8" && componentCount === 4)
            return downsampleBox(source.pixels, srcW, srcH, srcD,
                dstW, dstH, dstD);
        const out = new Uint8Array(dstW * dstH * dstD * bytesPerTexel);
        const srcView = new DataView(source.pixels.buffer,
            source.pixels.byteOffset, source.pixels.byteLength);
        const dstView = new DataView(out.buffer);
        const read = (texel, component) => {
            const at = texel * bytesPerTexel;
            if (componentType === "f16")
                return halfToFloat(srcView.getUint16(at + component * 2, true));
            if (componentType === "f32")
                return srcView.getFloat32(at + component * 4, true);
            if (componentType === "u16")
                return srcView.getUint16(at + component * 2, true);
            return srcView.getUint8(at + component);
        };
        const write = (texel, component, value) => {
            const at = texel * bytesPerTexel;
            if (componentType === "f16")
                dstView.setUint16(at + component * 2, floatToHalf(value), true);
            else if (componentType === "f32")
                dstView.setFloat32(at + component * 4, value, true);
            else if (componentType === "u16")
                dstView.setUint16(at + component * 2, Math.round(value), true);
            else dstView.setUint8(at + component, Math.round(value));
        };
        const xScale = srcW / dstW, yScale = srcH / dstH, zScale = srcD / dstD;
        for (let z = 0; z < dstD; ++z) for (let y = 0; y < dstH; ++y)
            for (let x = 0; x < dstW; ++x) {
                const x0 = Math.floor(x * xScale), y0 = Math.floor(y * yScale);
                const z0 = Math.floor(z * zScale);
                const x1 = Math.min(srcW - 1, x0 + (xScale > 1 ? 1 : 0));
                const y1 = Math.min(srcH - 1, y0 + (yScale > 1 ? 1 : 0));
                const z1 = Math.min(srcD - 1, z0 + (zScale > 1 ? 1 : 0));
                const destination = (z * dstH + y) * dstW + x;
                for (let c = 0; c < componentCount; ++c) {
                    let sum = 0, samples = 0;
                    for (const sz of z0 === z1 ? [z0] : [z0, z1])
                        for (const sy of y0 === y1 ? [y0] : [y0, y1])
                            for (const sx of x0 === x1 ? [x0] : [x0, x1]) {
                                sum += read((sz * srcH + sy) * srcW + sx, c);
                                ++samples;
                            }
                    write(destination, c, sum / samples);
                }
            }
        return out;
    }

    /* ================================================================== */
    /* Executor: buffers and programs                                     */
    /* ================================================================== */

    Object.assign(GLWebGPUExecutor.prototype, {

        bufferFor(target) {
            const s = this.current;
            const name = target === GL.ELEMENT_ARRAY_BUFFER ? s.elementArrayBuffer :
                (target === GL.PIXEL_PACK_BUFFER ? s.pixelPackBuffer :
                    (target === GL.PIXEL_UNPACK_BUFFER ? s.pixelUnpackBuffer :
                        s.arrayBuffer));
            if (!name) return null;
            return s.shareGroup.buffers.get(name) || null;
        },

        bufferData(target, byteCount, usage, data) {
            const buffer = this.bufferFor(target);
            if (!buffer)
                return this.refuse("glBufferData", "no buffer bound", { target },
                    GL.INVALID_OPERATION);
            buffer.size = byteCount;
            buffer.usage = usage;
            buffer.target = target;
            buffer.shadow = new Uint8Array(byteCount);
            if (data) buffer.shadow.set(data.subarray(0,
                Math.min(data.byteLength, byteCount)));
            this.retire(buffer.gpuBuffer);
            // The same buffer may be bound as vertex data now and as indices
            // later, and GL never says which; asking for both costs nothing.
            buffer.gpuBuffer = byteCount ? this.device.createBuffer({
                label: "GL buffer " + buffer.name,
                size: alignUp(Math.max(4, byteCount), 4),
                usage: BUFFER_USAGE_VERTEX | BUFFER_USAGE_INDEX |
                    BUFFER_USAGE_COPY_DST | BUFFER_USAGE_COPY_SRC,
            }) : null;
            if (buffer.gpuBuffer && byteCount)
                this.device.queue.writeBuffer(buffer.gpuBuffer, 0, buffer.shadow,
                    0, alignUp(byteCount, 4) <= buffer.shadow.byteLength ?
                        byteCount & ~3 : byteCount - (byteCount & 3));
        },

        bufferSubData(target, offset, data) {
            const buffer = this.bufferFor(target);
            if (!buffer || !buffer.shadow)
                return this.refuse("glBufferSubData", "no buffer storage",
                    { target }, GL.INVALID_OPERATION);
            if (offset + data.byteLength > buffer.shadow.byteLength)
                return this.refuse("glBufferSubData", "update is out of range",
                    { offset, size: data.byteLength }, GL.INVALID_VALUE);
            buffer.shadow.set(data, offset);
            if (buffer.gpuBuffer) {
                // writeBuffer wants 4-byte alignment at both ends; a ragged
                // update is widened to the enclosing aligned span, which is
                // safe because the shadow copy holds the true contents.
                const start = offset & ~3;
                const end = alignUp(offset + data.byteLength, 4);
                const clampedEnd = Math.min(end, buffer.shadow.byteLength);
                const span = clampedEnd - start;
                if (span > 0)
                    this.device.queue.writeBuffer(buffer.gpuBuffer, start,
                        buffer.shadow, start, span & ~3 || span);
            }
        },

        /* ---- programs ---- */

        linkProgram(name) {
            const program = this.current.shareGroup.programs.get(name);
            if (!program)
                return this.refuse("glLinkProgram", "unknown program", { name },
                    GL.INVALID_VALUE);
            program.linked = false;
            program.log = "";
            program.variants.clear();
            program.compiled = { vertex: null, fragment: null };
            for (const shaderName of program.shaders) {
                const shader = this.current.shareGroup.shaders.get(shaderName);
                if (!shader) continue;
                if (!shader.compiled)
                    shader.compiled = translator.compileShader(shader.source,
                        shader.stage, this.options.translator || {});
                if (!shader.compiled.ok) {
                    program.log = shader.compiled.log;
                    return;
                }
                program.compiled[shader.stage] = shader.compiled;
            }
            if (!program.compiled.vertex || !program.compiled.fragment) {
                program.log = "ERROR: a program needs both a vertex and a " +
                    "fragment shader";
                return;
            }
            // The base link (no pipeline variant) settles the uniform layout
            // and the reflection, which every variant then shares -- so
            // glGetUniformLocation answers the same value no matter which
            // variant a later draw happens to need.
            const link = this.linkVariant(program, {});
            if (!link.ok) {
                program.log = link.log;
                return;
            }
            program.link = link;
            program.linked = true;
            program.log = link.log;
            this.buildUniformStorage(program, link.reflection);
            ++program.serial;
        },

        variantKeyFor(variant) {
            return [variant.alphaTest || "-", variant.clipPlaneCount || 0,
                    variant.pointSprite ? "p" : "-",
                    variant.pointCoordLowerLeft ? "l" : "u",
                    variant.polygonStipple ? "s" : "-",
                    "o" + (variant.logicOp || 0).toString(16),
                    variant.twoSided ? "2" : "-",
                    variant.flatShading ? "f" : "-",
                    variant.colorTargets || 1].join("|");
        },

        linkVariant(program, variant) {
            const options = {
                bindAttribLocations: program.bindAttribLocations,
                variant,
                maxColorTargets: variant.colorTargets,
                nonUniformSampleStrategy:
                    this.options.nonUniformSampleStrategy || "grad",
            };
            const key = translator.hashString(
                program.name + "|" + this.variantKeyFor(variant) + "|" +
                (program.compiled.vertex.sourceHash >>> 0) + "|" +
                (program.compiled.fragment.sourceHash >>> 0)).toString(16);
            const cached = this.shaderCache.get(key);
            if (cached) {
                ++this.stats.shaderCacheHits;
                return cached;
            }
            const link = translator.linkProgram(program.compiled.vertex,
                program.compiled.fragment, options);
            if (link.ok) {
                ++this.stats.shaderLinks;
                this.stats.nonUniformSamples += link.stats.nonUniformSamples;
                this.shaderCache.set(key, link);
            }
            return link;
        },

        buildUniformStorage(program, reflection) {
            program.uniformData = new Uint8Array(
                Math.max(16, reflection.uniformBlockBytes));
            program.uniformFloats = new Float32Array(program.uniformData.buffer);
            program.uniformInts = new Int32Array(program.uniformData.buffer);
            program.uniformByName.clear();
            program.uniformByLocation.clear();
            for (const uniform of reflection.uniforms) {
                program.uniformByName.set(uniform.name, uniform);
                for (let i = 0; i < Math.max(1, uniform.arraySize); ++i)
                    program.uniformByLocation.set(uniform.location + i,
                        { uniform, element: i });
            }
            program.samplerUnits.clear();
            for (const sampler of reflection.samplers) {
                program.uniformByName.set(sampler.name, sampler);
                for (let i = 0; i < Math.max(1, sampler.arraySize); ++i) {
                    program.uniformByLocation.set(sampler.location + i,
                        { sampler, element: i });
                    program.samplerUnits.set(sampler.name +
                        (sampler.arraySize ? "[" + i + "]" : ""), 0);
                }
            }
        },

        currentProgramObject() {
            const s = this.current;
            if (!s.currentProgram) return null;
            const program = s.shareGroup.programs.get(s.currentProgram);
            return program && program.linked ? program : null;
        },

        setUniformVector(view, offset, size, bytes, integer) {
            const program = this.currentProgramObject();
            if (!program) return;
            const location = view.getInt32(offset, true);
            const components = view.getInt32(offset + 4, true);
            const count = view.getInt32(offset + 8, true);
            if (components < 1 || components > 4 || count < 0) return;
            const entry = program.uniformByLocation.get(location);
            if (!entry) return;

            if (entry.sampler) {
                // glUniform1i on a sampler names a texture unit; it is a
                // binding change, not a value in the uniform block.
                const unit = view.getInt32(offset + 12, true);
                const key = entry.sampler.name +
                    (entry.sampler.arraySize ? "[" + entry.element + "]" : "");
                program.samplerUnits.set(key, unit | 0);
                this.invalidateBindGroups();
                return;
            }

            const uniform = entry.uniform;
            const stride = uniform.arraySize ?
                uniform.arrayStrideBytes : 0;
            for (let i = 0; i < count; ++i) {
                const element = entry.element + i;
                if (uniform.arraySize && element >= uniform.arraySize) break;
                const base = uniform.offsetBytes + element * stride;
                for (let c = 0; c < components; ++c) {
                    const src = offset + 12 + (i * components + c) * 4;
                    if (src + 4 > offset + size) return;
                    const at = (base >> 2) + c;
                    if (integer || uniform.baseType !== "float")
                        program.uniformInts[at] = view.getInt32(src, true);
                    else
                        program.uniformFloats[at] = view.getFloat32(src, true);
                }
            }
            ++program.serial;
        },

        setUniformMatrix(location, columns, rows, count, transpose, view, offset,
                size) {
            const program = this.currentProgramObject();
            if (!program) return;
            const entry = program.uniformByLocation.get(location);
            if (!entry || !entry.uniform) return;
            const uniform = entry.uniform;
            const columnStride = uniform.matrixColumnStrideBytes ||
                (rows === 2 ? 8 : 16);
            const stride = uniform.arraySize ? uniform.arrayStrideBytes : 0;
            for (let m = 0; m < count; ++m) {
                const element = entry.element + m;
                if (uniform.arraySize && element >= uniform.arraySize) break;
                const base = uniform.offsetBytes + element * stride;
                for (let c = 0; c < columns; ++c) {
                    for (let r = 0; r < rows; ++r) {
                        // GL's default is column-major; transpose means the
                        // caller handed rows first.
                        const sourceIndex = transpose ?
                            (r * columns + c) : (c * rows + r);
                        const src = offset + (m * columns * rows + sourceIndex) * 4;
                        if (src + 4 > offset + size) return;
                        const at = (base + c * columnStride) / 4 + r;
                        program.uniformFloats[at] = view.getFloat32(src, true);
                    }
                }
            }
            ++program.serial;
        },

        /* ---- synchronous query answers ---- */

        queryLocation(kind, programName, name) {
            const program = this.current.shareGroup.programs.get(programName);
            if (!program || !program.linked || !program.link) return null;
            const reflection = program.link.reflection;
            if (kind === LOCATION_KIND_ATTRIB) {
                // glGetAttribLocation
                const attribute = reflection.attributes.find(a => a.name === name);
                if (!attribute) return { location: -1, type: 0, size: 0 };
                return { location: attribute.location,
                         type: glslTypeEnum(attribute.glslType),
                         size: 1 };
            }
            // glGetUniformLocation. GL says name and name[0] are the same
            // location, and name[i] is location + i.
            let lookupName = name;
            let element = 0;
            const match = /^(.*)\[(\d+)\]$/.exec(name);
            if (match) { lookupName = match[1]; element = parseInt(match[2], 10); }
            const uniform = program.uniformByName.get(lookupName);
            if (!uniform) return { location: -1, type: 0, size: 0 };
            if (uniform.arraySize && element >= uniform.arraySize)
                return { location: -1, type: 0, size: 0 };
            return {
                location: uniform.location + element,
                type: glslTypeEnum(uniform.glslType ||
                    samplerTypeName(uniform)),
                size: uniform.arraySize || 1,
            };
        },

        readUniform(programName, location, valueKind) {
            const program = this.current.shareGroup.programs.get(programName);
            if (!program || !program.linked) return null;
            const entry = program.uniformByLocation.get(location);
            if (!entry) return null;
            if (entry.sampler) {
                const key = entry.sampler.name +
                    (entry.sampler.arraySize ? "[" + entry.element + "]" : "");
                return [program.samplerUnits.get(key) || 0];
            }
            const uniform = entry.uniform;
            const stride = uniform.arraySize ? uniform.arrayStrideBytes : 0;
            const base = uniform.offsetBytes + entry.element * stride;
            const values = [];
            if (uniform.kind === "matrix") {
                const columnStride = uniform.matrixColumnStrideBytes;
                for (let c = 0; c < uniform.columns; ++c)
                    for (let r = 0; r < uniform.rows; ++r)
                        values.push(program.uniformFloats[
                            (base + c * columnStride) / 4 + r]);
            } else {
                for (let c = 0; c < uniform.components; ++c) {
                    values.push(valueKind === 2 || uniform.baseType !== "float" ?
                        program.uniformInts[(base >> 2) + c] :
                        program.uniformFloats[(base >> 2) + c]);
                }
            }
            return values;
        },

        queryObjectiv(kind, name, pname) {
            const group = this.current.shareGroup;
            if (kind === 1) {                     // shader
                const shader = group.shaders.get(name);
                if (!shader) return null;
                switch (pname) {
                case GL.COMPILE_STATUS:
                    return shader.compiled && shader.compiled.ok ? 1 : 0;
                case GL.INFO_LOG_LENGTH:
                    return shader.compiled ? shader.compiled.log.length + 1 : 1;
                case GL.SHADER_SOURCE_LENGTH: return shader.source.length + 1;
                case GL.SHADER_TYPE: return shader.type;
                case GL.DELETE_STATUS: return 0;
                default: return 0;
                }
            }
            if (kind === 2) {                     // program
                const program = group.programs.get(name);
                if (!program) return null;
                const reflection = program.link ? program.link.reflection : null;
                switch (pname) {
                case GL.LINK_STATUS: return program.linked ? 1 : 0;
                case GL.VALIDATE_STATUS: return program.linked ? 1 : 0;
                case GL.INFO_LOG_LENGTH: return program.log.length + 1;
                case GL.ATTACHED_SHADERS: return program.shaders.size;
                case GL.ACTIVE_UNIFORMS:
                    return reflection ?
                        reflection.uniforms.length + reflection.samplers.length : 0;
                case GL.ACTIVE_ATTRIBUTES:
                    return reflection ? reflection.attributes.length : 0;
                case GL.ACTIVE_UNIFORM_MAX_LENGTH:
                    return reflection ? maxNameLength(reflection.uniforms,
                        reflection.samplers) : 1;
                case GL.ACTIVE_ATTRIBUTE_MAX_LENGTH:
                    return reflection ? maxNameLength(reflection.attributes) : 1;
                case GL.DELETE_STATUS: return 0;
                default: return 0;
                }
            }
            // Occlusion query object.
            const query = this.queries.get(name);
            if (!query) return null;
            if (pname === GL.QUERY_RESULT_AVAILABLE) return query.ready ? 1 : 0;
            if (pname === GL.QUERY_RESULT) return query.result >>> 0;
            return 0;
        },

        queryObjectLog(kind, name) {
            const group = this.current.shareGroup;
            if (kind === 1) {
                const shader = group.shaders.get(name);
                return shader ? (shader.compiled ? shader.compiled.log : "") : null;
            }
            const program = group.programs.get(name);
            return program ? program.log : null;
        },

        /* openglproxy's V86GL_ACTIVE_KIND_UNIFORM is 1 and _ATTRIB is 2. */
        queryActive(kind, programName, index) {
            const program = this.current.shareGroup.programs.get(programName);
            if (!program || !program.link) return null;
            const reflection = program.link.reflection;
            if (kind === ACTIVE_KIND_ATTRIB) {
                const attribute = reflection.attributes[index];
                if (!attribute) return null;
                return { name: attribute.name, size: 1,
                         type: glslTypeEnum(attribute.glslType) };
            }
            const all = reflection.uniforms.concat(reflection.samplers);
            const uniform = all[index];
            if (!uniform) return null;
            return {
                name: uniform.arraySize ? uniform.name + "[0]" : uniform.name,
                size: uniform.arraySize || 1,
                type: glslTypeEnum(uniform.glslType || samplerTypeName(uniform)),
            };
        },
    });

    function maxNameLength(...lists) {
        let longest = 0;
        for (const list of lists)
            for (const entry of list)
                longest = Math.max(longest, entry.name.length);
        return longest + 1;
    }

    function samplerTypeName(sampler) {
        if (!sampler.dim) return "sampler2D";
        return "sampler" + sampler.dim + (sampler.shadow ? "Shadow" : "");
    }

    /* GLSL type names to the GL_* enums glGetActiveUniform reports. */
    const GLSL_TYPE_ENUMS = {
        float: GL.FLOAT, vec2: GL.FLOAT_VEC2, vec3: GL.FLOAT_VEC3,
        vec4: GL.FLOAT_VEC4,
        int: GL.INT, ivec2: GL.INT_VEC2, ivec3: GL.INT_VEC3, ivec4: GL.INT_VEC4,
        bool: GL.BOOL, bvec2: GL.BOOL_VEC2, bvec3: GL.BOOL_VEC3,
        bvec4: GL.BOOL_VEC4,
        mat2: GL.FLOAT_MAT2, mat3: GL.FLOAT_MAT3, mat4: GL.FLOAT_MAT4,
        mat2x3: GL.FLOAT_MAT2x3, mat2x4: GL.FLOAT_MAT2x4,
        mat3x2: GL.FLOAT_MAT3x2, mat3x4: GL.FLOAT_MAT3x4,
        mat4x2: GL.FLOAT_MAT4x2, mat4x3: GL.FLOAT_MAT4x3,
        sampler1D: GL.SAMPLER_1D, sampler2D: GL.SAMPLER_2D,
        sampler3D: GL.SAMPLER_3D, samplerCube: GL.SAMPLER_CUBE,
        sampler1DShadow: GL.SAMPLER_1D_SHADOW,
        sampler2DShadow: GL.SAMPLER_2D_SHADOW,
    };

    function glslTypeEnum(name) {
        return GLSL_TYPE_ENUMS[name] || 0;
    }

    /* ================================================================== */
    /* Executor: vertex assembly and drawing                              */
    /* ================================================================== */

    /* GL vertex types that WebGPU can consume without conversion. Anything
     * else is widened to float32 on the CPU, because the generated shaders
     * declare every attribute as a float vector. */
    const DIRECT_VERTEX_FORMATS = {
        [GL.FLOAT]: { 1: "float32", 2: "float32x2", 3: "float32x3", 4: "float32x4" },
        [GL.HALF_FLOAT]: { 2: "float16x2", 4: "float16x4" },
    };
    const DIRECT_NORMALIZED_FORMATS = {
        [GL.UNSIGNED_BYTE]: { 4: "unorm8x4" },
        [GL.BYTE]: { 4: "snorm8x4" },
        [GL.UNSIGNED_SHORT]: { 2: "unorm16x2", 4: "unorm16x4" },
        [GL.SHORT]: { 2: "snorm16x2", 4: "snorm16x4" },
    };

    function componentBytes(type) {
        switch (type) {
        case GL.BYTE: case GL.UNSIGNED_BYTE: return 1;
        case GL.SHORT: case GL.UNSIGNED_SHORT: case GL.HALF_FLOAT: return 2;
        case GL.INT: case GL.UNSIGNED_INT: case GL.FLOAT: return 4;
        case GL.DOUBLE: return 8;
        default: return 0;
        }
    }

    function readComponent(view, at, type, normalized) {
        switch (type) {
        case GL.FLOAT: return view.getFloat32(at, true);
        case GL.DOUBLE: return view.getFloat64(at, true);
        case GL.BYTE: {
            const v = view.getInt8(at);
            return normalized ? Math.max(v / 127, -1) : v;
        }
        case GL.UNSIGNED_BYTE: {
            const v = view.getUint8(at);
            return normalized ? v / 255 : v;
        }
        case GL.SHORT: {
            const v = view.getInt16(at, true);
            return normalized ? Math.max(v / 32767, -1) : v;
        }
        case GL.UNSIGNED_SHORT: {
            const v = view.getUint16(at, true);
            return normalized ? v / 65535 : v;
        }
        case GL.INT: {
            const v = view.getInt32(at, true);
            return normalized ? Math.max(v / 2147483647, -1) : v;
        }
        case GL.UNSIGNED_INT: {
            const v = view.getUint32(at, true);
            return normalized ? v / 4294967295 : v;
        }
        default: return 0;
        }
    }

    Object.assign(GLWebGPUExecutor.prototype, {

        /* ---- immediate mode ---- */

        /*
         * glBegin/glEnd arrives one record per vertex, so the accumulator here
         * is where a Half-Life frame's twenty thousand vertices actually land.
         * Each vertex snapshots GL's *current* colour, normal and texture
         * coordinates, which is the whole semantic of immediate mode.
         */
        beginImmediate(mode) {
            const s = this.current;
            if (s.immediate)
                return this.refuse("glBegin", "glBegin inside glBegin", { mode },
                    GL.INVALID_OPERATION);
            if (PRIMITIVE_TOPOLOGY[mode] === undefined)
                return this.refuse("glBegin", "unknown primitive mode", { mode },
                    GL.INVALID_ENUM);
            s.immediate = {
                mode, count: 0,
                data: this.immediateBuffer || (this.immediateBuffer =
                    new Float32Array(65536)),
                stride: 0,
                used: { color: false, normal: false, secondaryColor: false,
                        fogCoord: false,
                        texCoord: new Array(MAX_TEXTURE_COORDS).fill(false),
                        generic: [] },
            };
            const used = s.immediate.used;
            const program = this.currentProgramObject();
            const arbVertex = s.enabled.has(GL.VERTEX_PROGRAM_ARB);
            // Immediate vertices must satisfy the active shader interface, not
            // stale client-array enables. Legacy renderers commonly keep a
            // normal array enabled while drawing glBegin/glEnd UI geometry;
            // conversely an ARB vertex program may read vertex.normal with
            // fixed-function lighting disabled. In both cases deriving this
            // layout only from GL_LIGHTING/client-array state leaves
            // @location(2) unbound.
            for (const entry of this.wantedAttributes(program)) {
                let source = entry.source;
                if (source.indexOf("generic") === 0 && entry.alias &&
                        (entry.alias === "vertex" || entry.alias === "normal" ||
                         entry.alias === "color" ||
                         entry.alias === "secondaryColor" ||
                         entry.alias === "fogCoord" ||
                         entry.alias.indexOf("texCoord") === 0))
                    source = entry.alias;
                if (source === "color") used.color = true;
                else if (source === "normal") used.normal = true;
                else if (source === "secondaryColor") used.secondaryColor = true;
                else if (source === "fogCoord") used.fogCoord = true;
                else if (source.indexOf("texCoord") === 0) {
                    const unit = Number(source.substring(8));
                    if (unit >= 0 && unit < MAX_TEXTURE_COORDS)
                        used.texCoord[unit] = true;
                } else if (source.indexOf("generic") === 0) {
                    const index = Number(source.substring(7));
                    if (index >= 0 && index < MAX_VERTEX_ATTRIBS &&
                            !used.generic.some(item =>
                                item.location === entry.location)) {
                        used.generic.push({
                            index, location: entry.location,
                            components: Math.min(4,
                                Math.max(1, entry.components || 4)),
                        });
                    }
                }
            }
            // The fixed pipeline always consumes current colour even when no
            // colour command occurs in the batch. Programmable shaders carry
            // it only when reflection above says they actually read it.
            if (!program && !arbVertex) used.color = true;
        },

        immediateVertex(x, y, z, w) {
            const s = this.current;
            const batch = s.immediate;
            if (!batch)
                return this.refuse("glVertex", "glVertex outside glBegin", {},
                    GL.INVALID_OPERATION);
            const used = batch.used;
            for (let unit = 0; unit < MAX_TEXTURE_COORDS; ++unit)
                if (s.textureUnits[unit].enabledTargets.size)
                    used.texCoord[unit] = true;
            if (s.enabled.has(GL.LIGHTING)) used.normal = true;
            if (s.enabled.has(GL.COLOR_SUM) ||
                    s.lightModel.colorControl === GL.SEPARATE_SPECULAR_COLOR)
                used.secondaryColor = true;
            if (s.enabled.has(GL.FOG) && s.fog.coordSource === GL.FOG_COORD)
                used.fogCoord = true;

            const stride = this.immediateStride(used);
            const need = (batch.count + 1) * stride;
            if (need > batch.data.length) {
                const grown = new Float32Array(Math.max(need * 2,
                    batch.data.length * 2));
                grown.set(batch.data.subarray(0, batch.count * batch.stride));
                batch.data = grown;
                this.immediateBuffer = grown;
            }
            if (stride !== batch.stride && batch.count) {
                // A later vertex enabled an attribute the earlier ones did not
                // carry. Re-laying out the batch keeps one buffer per glEnd,
                // which is worth more than the rare copy.
                this.relayoutImmediate(batch, stride);
            }
            batch.stride = stride;
            let at = batch.count * stride;
            const data = batch.data;
            const current = s.current;
            data[at++] = x; data[at++] = y; data[at++] = z; data[at++] = w;
            if (used.color) {
                data[at++] = current.color[0]; data[at++] = current.color[1];
                data[at++] = current.color[2]; data[at++] = current.color[3];
            }
            if (used.normal) {
                data[at++] = current.normal[0]; data[at++] = current.normal[1];
                data[at++] = current.normal[2];
            }
            if (used.secondaryColor) {
                data[at++] = current.secondaryColor[0];
                data[at++] = current.secondaryColor[1];
                data[at++] = current.secondaryColor[2];
                data[at++] = current.secondaryColor[3];
            }
            if (used.fogCoord) data[at++] = current.fogCoord;
            for (let unit = 0; unit < MAX_TEXTURE_COORDS; ++unit) {
                if (!used.texCoord[unit]) continue;
                const tc = current.texCoord[unit];
                data[at++] = tc[0]; data[at++] = tc[1];
                data[at++] = tc[2]; data[at++] = tc[3];
            }
            for (const item of used.generic) {
                const value = s.genericAttribs[item.index].value;
                for (let c = 0; c < item.components; ++c)
                    data[at++] = value[c];
            }
            ++batch.count;
        },

        immediateStride(used) {
            let stride = 4;
            if (used.color) stride += 4;
            if (used.normal) stride += 3;
            if (used.secondaryColor) stride += 4;
            if (used.fogCoord) stride += 1;
            for (let unit = 0; unit < MAX_TEXTURE_COORDS; ++unit)
                if (used.texCoord[unit]) stride += 4;
            for (const item of used.generic) stride += item.components;
            return stride;
        },

        relayoutImmediate(batch, stride) {
            const grown = new Float32Array(
                Math.max(batch.data.length, (batch.count + 1) * stride * 2));
            for (let i = 0; i < batch.count; ++i)
                grown.set(batch.data.subarray(i * batch.stride,
                    i * batch.stride + batch.stride), i * stride);
            batch.data = grown;
            this.immediateBuffer = grown;
        },

        endImmediate() {
            const s = this.current;
            const batch = s.immediate;
            if (!batch || !batch.count) {
                s.immediate = null;
                return;
            }
            ++this.stats.immediateBatches;

            const attributes = [];
            let offset = 0;
            const push = (location, components) => {
                attributes.push({ location, components, offsetFloats: offset });
                offset += components;
            };
            push(translator.COMPAT_ATTRIBUTE_LOCATIONS.gl_Vertex, 4);
            if (batch.used.color)
                push(translator.COMPAT_ATTRIBUTE_LOCATIONS.gl_Color, 4);
            if (batch.used.normal)
                push(translator.COMPAT_ATTRIBUTE_LOCATIONS.gl_Normal, 3);
            if (batch.used.secondaryColor)
                push(translator.COMPAT_ATTRIBUTE_LOCATIONS.gl_SecondaryColor, 4);
            if (batch.used.fogCoord)
                push(translator.COMPAT_ATTRIBUTE_LOCATIONS.gl_FogCoord, 1);
            for (let unit = 0; unit < MAX_TEXTURE_COORDS; ++unit)
                if (batch.used.texCoord[unit])
                    push(translator.COMPAT_ATTRIBUTE_LOCATIONS.gl_MultiTexCoord0 +
                        unit, 4);
            for (const item of batch.used.generic)
                push(item.location, item.components);

            const bytes = new Uint8Array(batch.data.buffer, 0,
                batch.count * batch.stride * 4);
            const slice = this.uploadVertices(bytes);
            if (!slice) {
                s.immediate = null;
                return;
            }
            try {
                this.issueDraw({
                    mode: batch.mode,
                    vertexCount: batch.count,
                    buffers: [{
                        gpuBuffer: this.vertexRing,
                        baseOffset: slice.offset,
                        stride: batch.stride * 4,
                        attributes: attributes.map(a => ({
                            location: a.location,
                            format: "float32" + (a.components > 1 ?
                                "x" + a.components : ""),
                            offset: a.offsetFloats * 4,
                        })),
                    }],
                    index: null,
                });
            } finally {
                s.immediate = null;
            }
        },

        /* ---- packed client arrays ---- */

        drawPacked(mode, count, arrays, indexInfo) {
            if (count <= 0) return;
            if (PRIMITIVE_TOPOLOGY[mode] === undefined)
                return this.refuse("glDrawArrays", "unknown primitive mode",
                    { mode }, GL.INVALID_ENUM);
            const program = this.currentProgramObject();
            const wanted = this.wantedAttributes(program);
            let sourceIndices = null;
            let packedVertexCount = count;
            let drawIndexInfo = indexInfo;
            if (indexInfo && indexInfo.data) {
                const indices = readIndices(indexInfo.type, indexInfo.data, count);
                const maxIndex = maximumIndex(indices);
                const expandIndexedPoints = mode === GL.POINTS &&
                    (this.current.point.size !== 1 ||
                        this.current.enabled.has(GL.POINT_SPRITE));
                if (expandIndexedPoints) {
                    sourceIndices = indices;
                    packedVertexCount = count;
                    drawIndexInfo = null;
                } else {
                    packedVertexCount = maxIndex + 1;
                }
            }

            const arbVertexOn = this.current.enabled.has(GL.VERTEX_PROGRAM_ARB);
            if (!program && !arbVertexOn &&
                    (!arrays.vertex || !arrays.vertex.data))
                return this.refuse("glDrawArrays",
                    "the fixed pipeline needs a vertex array", { mode },
                    GL.INVALID_OPERATION);

            // Every array is widened to float32 and interleaved into one ring
            // slice. Packing costs a pass over the data, but it is the only
            // shape that handles GL's whole type matrix, and the arrays already
            // arrived as a CPU copy from the guest.
            const layout = [];
            let stride = 0;
            for (const entry of wanted) {
                const source = this.resolveAttributeSource(arrays, entry,
                    block => !!(block && block.data));
                const block = arrays[source];
                const constant = (!block || !block.data) ?
                    this.currentAttributeValue(source, entry.location) : null;
                if ((!block || !block.data) && !constant) continue;
                const components = entry.components || (block ?
                    Math.min(4, Math.max(1, block.size || 4)) : constant.length);
                layout.push({ location: entry.location, source,
                              block, constant, components, offsetFloats: stride });
                stride += components;
            }
            if (!layout.length)
                return this.refuse("glDrawArrays", "no vertex array is enabled",
                    { mode }, GL.INVALID_OPERATION);

            const packed = new Float32Array(stride * packedVertexCount);
            for (const item of layout) {
                const block = item.block;
                if (!block || !block.data) {
                    for (let v = 0; v < packedVertexCount; ++v) {
                        const dst = v * stride + item.offsetFloats;
                        for (let c = 0; c < item.components; ++c)
                            packed[dst + c] = c < item.constant.length ?
                                item.constant[c] : (c === 3 ? 1 : 0);
                    }
                    continue;
                }
                const view = new DataView(block.data.buffer,
                    block.data.byteOffset, block.data.byteLength);
                const size = Math.max(1, block.size || item.components);
                const elementBytes = componentBytes(block.type);
                if (!elementBytes) {
                    this.refuse("glDrawArrays", "unsupported array type",
                        { type: block.type }, GL.INVALID_ENUM);
                    return;
                }
                const srcStride = block.stride || size * elementBytes;
                const normalized = this.arrayIsNormalized(item.source, block);
                for (let v = 0; v < packedVertexCount; ++v) {
                    const sourceVertex = sourceIndices ? sourceIndices[v] : v;
                    const src = sourceVertex * srcStride;
                    const dst = v * stride + item.offsetFloats;
                    for (let c = 0; c < item.components; ++c) {
                        if (c < size && src + (c + 1) * elementBytes <=
                                block.data.byteLength) {
                            packed[dst + c] = readComponent(view,
                                src + c * elementBytes, block.type, normalized);
                        } else {
                            packed[dst + c] = c === 3 ? 1 : 0;
                        }
                    }
                }
            }

            const slice = this.uploadVertices(new Uint8Array(packed.buffer));
            if (!slice) return;
            const buffers = [{
                gpuBuffer: this.vertexRing,
                baseOffset: slice.offset,
                stride: stride * 4,
                attributes: layout.map(item => ({
                    location: item.location,
                    format: "float32" + (item.components > 1 ?
                        "x" + item.components : ""),
                    offset: item.offsetFloats * 4,
                })),
            }];

            let index = null;
            if (drawIndexInfo && drawIndexInfo.data) {
                index = this.uploadIndices(mode, drawIndexInfo.type, drawIndexInfo.data,
                    count);
                if (!index) return;
            }
            this.issueDraw({ mode, vertexCount: count, buffers, index });
        },

        /* Which of the two aliased slots this attribute's array actually
         * arrived in. `usable` differs between the packed and VBO paths, so
         * the caller decides what counts as present. */
        resolveAttributeSource(arrays, entry, usable) {
            if (!entry.alias || !arrays) return entry.source;
            if (usable(arrays[entry.source])) return entry.source;
            if (usable(arrays[entry.alias])) return entry.alias;
            return entry.source;
        },

        arrayIsNormalized(source, block) {
            if (source.indexOf("generic") === 0) return !!block.normalized;
            return source === "color" || source === "secondaryColor" ||
                source === "normal";
        },

        currentAttributeValue(source, location) {
            const s = this.current;
            if (source === "color") return s.current.color;
            if (source === "secondaryColor") return s.current.secondaryColor;
            if (source === "normal") return s.current.normal;
            if (source === "fogCoord") return [s.current.fogCoord];
            const tex = /^texCoord(\d+)$/.exec(source);
            if (tex) return s.current.texCoord[Number(tex[1])] || null;
            if (source.indexOf("generic") === 0) {
                const index = Number(source.substring(7));
                return s.genericAttribs[index] ? s.genericAttribs[index].value : null;
            }
            if (location >= 0 && location < s.genericAttribs.length)
                return s.genericAttribs[location].value;
            return null;
        },

        /* Which attributes the current pipeline wants, and where each comes
         * from in the packed draw record. */
        wantedAttributes(program) {
            const ATTR = translator.COMPAT_ATTRIBUTE_LOCATIONS;
            if (program && program.link) {
                return program.link.reflection.attributes.map(a => ({
                    location: a.location,
                    components: a.components,
                    source: attributeSourceName(a.name, a.location),
                }));
            }
            const s = this.current;
            if (s.enabled.has(GL.VERTEX_PROGRAM_ARB)) {
                const arb = this.arbProgramFor(GL.VERTEX_PROGRAM_ARB);
                if (arb && arb.compiled && arb.compiled.ok) {
                    return arb.compiled.reflection.attributes.map(a => {
                        const source = attributeSourceName(a.name || a.field,
                            a.location);
                        // Only the ARB path carries an alias: GL defines the
                        // aliasing for vertex program mode, and a GLSL program
                        // names its own attributes.
                        return { location: a.location,
                                 components: a.components, source,
                                 alias: aliasedAttributeSource(source) };
                    });
                }
            }
            /* Derive the fixed-function input layout from the same signature
             * that generates VSIn. Warcraft keeps several client arrays
             * enabled across unlit UI passes; consulting those enables in the
             * shader but semantic state here made VSIn require location 2
             * while the vertex layout omitted it, invalidating the pipeline.
             * One signature is now authoritative for both sides. */
            const attributes = this.fixedFunctionSignature(
                this.currentVariant()).attributes;
            const wanted = [{ location: ATTR.gl_Vertex, components: 4,
                              source: "vertex" }];
            if (attributes.color)
                wanted.push({ location: ATTR.gl_Color, components: 4,
                              source: "color" });
            if (attributes.normal)
                wanted.push({ location: ATTR.gl_Normal, components: 3,
                              source: "normal" });
            if (attributes.secondaryColor)
                wanted.push({ location: ATTR.gl_SecondaryColor, components: 4,
                              source: "secondaryColor" });
            if (attributes.fogCoord)
                wanted.push({ location: ATTR.gl_FogCoord, components: 1,
                              source: "fogCoord" });
            for (let unit = 0; unit < MAX_TEXTURE_COORDS; ++unit) {
                if (!attributes.texCoord[unit]) continue;
                wanted.push({ location: ATTR.gl_MultiTexCoord0 + unit,
                              components: attributes.texCoord[unit].components,
                              source: "texCoord" + unit });
            }
            return wanted;
        },

        /* ---- VBO-resident draws ---- */

        drawFromBuffers(mode, first, count, indexInfo) {
            if (count <= 0) return;
            const s = this.current;
            if (indexInfo && mode === GL.POINTS &&
                    (s.point.size !== 1 || s.enabled.has(GL.POINT_SPRITE)))
                return this.drawFromShadowBuffers(mode, first, count, indexInfo,
                    this.wantedAttributes(this.currentProgramObject()));
            const program = this.currentProgramObject();
            const wanted = this.wantedAttributes(program);
            const buffers = [];
            const byBuffer = new Map();
            const constants = [];

            for (const entry of wanted) {
                const source = this.resolveAttributeSource(s.arrays, entry,
                    a => !!(a && a.enabled && a.buffer));
                const array = s.arrays && s.arrays[source];
                if (!array || !array.enabled || !array.buffer) {
                    const value = this.currentAttributeValue(source,
                        entry.location);
                    if (value) {
                        constants.push({ entry, value });
                        continue;
                    }
                    return this.drawFromShadowBuffers(mode, first, count,
                        indexInfo, wanted);
                }
                const buffer = s.shareGroup.buffers.get(array.buffer);
                if (!buffer || !buffer.gpuBuffer) continue;
                const size = Math.max(1, array.size || entry.components);
                const format = vertexFormatFor(array.type, size, array.normalized);
                if (!format) {
                    // A type WebGPU cannot read directly; fall back to packing
                    // it out of the shadow copy.
                    return this.drawFromShadowBuffers(mode, first, count,
                        indexInfo, wanted);
                }
                const stride = array.stride ||
                    size * componentBytes(array.type);
                if (stride % 4 !== 0 || array.offset % 4 !== 0)
                    return this.drawFromShadowBuffers(mode, first, count,
                        indexInfo, wanted);
                const shaderComponents = entry.components || size;
                const attributeBytes = size * componentBytes(array.type);
                /*
                 * GL widens a size-2/3 array to the four-component attribute
                 * consumed by an ARB program, supplying (0, 1) for the absent
                 * components. WebGPU vertex formats do not perform that GL
                 * conversion, and their attribute offset must describe one
                 * element inside arrayStride. Pack these legal GL layouts from
                 * the buffer's shadow copy instead of creating a mismatched or
                 * invalid WebGPU vertex layout. The glview 1.5 test uses vec3
                 * positions and vec2 texture coordinates with vec4 ARB inputs.
                 */
                if ((s.enabled.has(GL.VERTEX_PROGRAM_ARB) &&
                            size !== shaderComponents) ||
                        array.offset >= stride ||
                        array.offset + attributeBytes > stride)
                    return this.drawFromShadowBuffers(mode, first, count,
                        indexInfo, wanted);
                let group = byBuffer.get(buffer.gpuBuffer);
                if (!group) {
                    group = { gpuBuffer: buffer.gpuBuffer, stride,
                              baseOffset: 0, attributes: [] };
                    byBuffer.set(buffer.gpuBuffer, group);
                    buffers.push(group);
                }
                if (group.stride !== stride) {
                    // Two arrays in one buffer with different strides need two
                    // bindings; WebGPU allows eight, which is enough in
                    // practice, and packing covers the rest.
                    const extra = { gpuBuffer: buffer.gpuBuffer, stride,
                                    baseOffset: 0, attributes: [] };
                    buffers.push(extra);
                    extra.attributes.push({ location: entry.location, format,
                                            offset: array.offset });
                    continue;
                }
                group.attributes.push({ location: entry.location, format,
                                        offset: array.offset });
            }

            if (constants.length) {
                let components = 0;
                for (const item of constants)
                    components += item.entry.components || item.value.length;
                const data = new Float32Array(components);
                const attributes = [];
                let at = 0;
                for (const item of constants) {
                    const count = item.entry.components || item.value.length;
                    for (let c = 0; c < count; ++c)
                        data[at + c] = c < item.value.length ? item.value[c] :
                            (c === 3 ? 1 : 0);
                    attributes.push({ location: item.entry.location,
                        format: "float32" + (count > 1 ? "x" + count : ""),
                        offset: at * 4 });
                    at += count;
                }
                const slice = this.uploadVertices(new Uint8Array(data.buffer));
                if (!slice) return;
                buffers.push({ gpuBuffer: this.vertexRing,
                    baseOffset: slice.offset, stride: components * 4,
                    stepMode: "instance", attributes });
            }

            if (!buffers.length)
                return this.drawFromShadowBuffers(mode, first, count, indexInfo,
                    wanted);
            const maxBuffers = (this.limits && this.limits.maxVertexBuffers) || 8;
            if (buffers.length > maxBuffers)
                return this.drawFromShadowBuffers(mode, first, count, indexInfo,
                    wanted);

            let index = null;
            if (indexInfo) {
                const buffer = s.shareGroup.buffers.get(s.elementArrayBuffer);
                if (!buffer || !buffer.shadow)
                    return this.refuse("glDrawElements",
                        "no element array buffer is bound", {},
                        GL.INVALID_OPERATION);
                const elementBytes = indexInfo.type === GL.UNSIGNED_BYTE ? 1 :
                    (indexInfo.type === GL.UNSIGNED_SHORT ? 2 : 4);
                const data = buffer.shadow.subarray(indexInfo.bufferOffset,
                    indexInfo.bufferOffset + count * elementBytes);
                index = this.uploadIndices(mode, indexInfo.type, data, count);
                if (!index) return;
            }
            this.issueDraw({ mode, vertexCount: count, firstVertex: first,
                             buffers, index });
        },

        /* The fallback when a VBO's layout is not something WebGPU can bind
         * directly: read it out of the shadow copy and pack, which is exactly
         * what the client-array path does. */
        drawFromShadowBuffers(mode, first, count, indexInfo, wanted) {
            const s = this.current;
            const arrays = Object.create(null);
            let arrayVertexCount = count;
            let indices = null;
            if (indexInfo) {
                const buffer = s.shareGroup.buffers.get(s.elementArrayBuffer);
                if (buffer && buffer.shadow) {
                    const elementBytes = indexInfo.type === GL.UNSIGNED_BYTE ? 1 :
                        (indexInfo.type === GL.UNSIGNED_SHORT ? 2 : 4);
                    indices = {
                        type: indexInfo.type,
                        data: buffer.shadow.subarray(indexInfo.bufferOffset,
                            indexInfo.bufferOffset + count * elementBytes),
                    };
                    const values = readIndices(indexInfo.type, indices.data, count);
                    arrayVertexCount = values.length ? maximumIndex(values) + 1 : 0;
                }
            }
            for (const entry of wanted) {
                const source = this.resolveAttributeSource(s.arrays, entry,
                    a => !!(a && a.enabled && a.buffer));
                const array = s.arrays && s.arrays[source];
                if (!array || !array.enabled) continue;
                const buffer = array.buffer ?
                    s.shareGroup.buffers.get(array.buffer) : null;
                if (!buffer || !buffer.shadow) continue;
                const size = Math.max(1, array.size || entry.components);
                const stride = array.stride || size * componentBytes(array.type);
                const start = array.offset + first * stride;
                arrays[source] = {
                    enabled: true, size, type: array.type, stride,
                    normalized: array.normalized,
                    data: buffer.shadow.subarray(start,
                        Math.min(buffer.shadow.byteLength,
                            start + arrayVertexCount * stride)),
                };
            }
            this.drawPacked(mode, count, arrays, indices);
        },

        /* ---- ring uploads ---- */

        uploadVertices(bytes) {
            const size = alignUp(bytes.byteLength, 4);
            if (size > this.vertexCapacity) {
                this.refuse("draw", "vertex data exceeds the ring capacity",
                    { size, capacity: this.vertexCapacity }, GL.OUT_OF_MEMORY);
                return null;
            }
            if (this.vertexCursor + size > this.vertexCapacity) {
                // Wrapping mid-frame would overwrite data an already recorded
                // draw still points at, so the frame is submitted first.
                this.flushFrame();
                this.vertexCursor = 0;
            }
            const offset = this.vertexCursor;
            this.vertexStaging.set(bytes, offset);
            this.device.queue.writeBuffer(this.vertexRing, offset,
                this.vertexStaging, offset, size);
            this.vertexCursor = offset + size;
            return { offset, size };
        },

        /*
         * Index upload, including the expansion of the primitive modes WebGPU
         * does not have. The expanded index array for a given (mode, count) is
         * cached because a Half-Life frame issues thousands of GL_QUADS draws
         * whose index pattern depends on nothing but the vertex count.
         */
        uploadIndices(mode, type, data, count) {
            const source = readIndices(type, data, count);
            let indices;
            if (needsIndexExpansion(mode)) {
                indices = expandIndexArray(mode, source);
                ++this.stats.expandedIndices;
            } else {
                indices = readIndices(type, data, count);
            }
            if (!indices.length) return null;
            const bytes = new Uint8Array(indices.buffer, indices.byteOffset,
                indices.byteLength);
            const slice = this.uploadVertices(bytes);
            if (!slice) return null;
            return { offset: slice.offset, count: indices.length,
                     format: "uint32", cpu: indices, source };
        },

        /* Draw with no index buffer still needs one when the mode expands. */
        expansionIndices(mode, count) {
            const key = mode + ":" + count;
            let cached = this.indexExpansionCache.get(key);
            if (!cached) {
                cached = expandIndices(mode, count, 0);
                if (this.indexExpansionCache.size > 4096)
                    this.indexExpansionCache.clear();
                this.indexExpansionCache.set(key, cached);
            }
            return cached;
        },
    });

    /*
     * ARB_vertex_program section 2.14.3 aliases the conventional vertex
     * attributes onto generic slots 0-5 and 8-15: glVertexPointer's array and
     * glVertexAttribPointerARB(0, ...) name the same attribute, and a program
     * may spell it either way. The guest sends whichever the application used,
     * so an attribute has to accept its alias -- otherwise a program written
     * against vertex.position and fed generic attribute 0 read the generic
     * attribute's (0, 0, 0, 1) default for every vertex and drew nothing, with
     * no GL error to say why.
     */
    const GENERIC_ATTRIB_ALIAS = {
        vertex: 0, weight: 1, normal: 2, color: 3,
        secondaryColor: 4, fogCoord: 5,
    };
    for (let unit = 0; unit < MAX_TEXTURE_COORDS; ++unit)
        GENERIC_ATTRIB_ALIAS["texCoord" + unit] = 8 + unit;

    const CONVENTIONAL_ATTRIB_ALIAS = {};
    for (const name of Object.keys(GENERIC_ATTRIB_ALIAS))
        CONVENTIONAL_ATTRIB_ALIAS["generic" + GENERIC_ATTRIB_ALIAS[name]] = name;

    function aliasedAttributeSource(source) {
        if (source.indexOf("generic") === 0)
            return CONVENTIONAL_ATTRIB_ALIAS[source] || null;
        const index = GENERIC_ATTRIB_ALIAS[source];
        return index === undefined ? null : "generic" + index;
    }

    function attributeSourceName(name, location) {
        const ATTR_SOURCES = {
            gl_Vertex: "vertex", gl_Normal: "normal", gl_Color: "color",
            gl_SecondaryColor: "secondaryColor", gl_FogCoord: "fogCoord",
        };
        if (ATTR_SOURCES[name]) return ATTR_SOURCES[name];
        const texMatch = /^gl_MultiTexCoord(\d)$/.exec(name);
        if (texMatch) return "texCoord" + texMatch[1];
        // Generic arrays are selected by attribute index, not by the spelling
        // of the shader variable. glBindAttribLocation may assign any user name
        // to a location, while the guest record necessarily carries that index.
        return "generic" + location;
    }

    function vertexFormatFor(type, size, normalized) {
        if (normalized) {
            const table = DIRECT_NORMALIZED_FORMATS[type];
            return table ? table[size] || null : null;
        }
        const table = DIRECT_VERTEX_FORMATS[type];
        return table ? table[size] || null : null;
    }

    function readIndices(type, data, count) {
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        const out = new Uint32Array(count);
        for (let i = 0; i < count; ++i) {
            switch (type) {
            case GL.UNSIGNED_BYTE:
                if (i < data.byteLength) out[i] = view.getUint8(i);
                break;
            case GL.UNSIGNED_SHORT:
                if (i * 2 + 2 <= data.byteLength) out[i] = view.getUint16(i * 2, true);
                break;
            default:
                if (i * 4 + 4 <= data.byteLength) out[i] = view.getUint32(i * 4, true);
                break;
            }
        }
        return out;
    }

    function maximumIndex(indices) {
        let maximum = 0;
        for (let i = 0; i < indices.length; ++i)
            if (indices[i] > maximum) maximum = indices[i];
        return maximum;
    }

    /* ================================================================== */
    /* Executor: pipeline state and the draw itself                       */
    /* ================================================================== */

    Object.assign(GLWebGPUExecutor.prototype, {

        /*
         * GL's front-face convention is inverted here and nowhere else.
         *
         * The vertex shader negates clip-space Y so that framebuffer row 0 is
         * GL's bottom row (plan 4.3); that flip reverses the winding of every
         * triangle, so GL_CCW has to be reported to WebGPU as "cw". Keeping
         * this in one function is deliberate: a second place that flips winding
         * would cancel this one out somewhere and nowhere else.
         */
        gpuFrontFace(state) {
            return state.frontFace === GL.CCW ? "cw" : "ccw";
        },

        gpuCullMode(state) {
            if (!state.enabled.has(GL.CULL_FACE)) return "none";
            if (state.cullFace === GL.FRONT_AND_BACK) return "none";  // see below
            return state.cullFace === GL.FRONT ? "front" : "back";
        },

        /* The set of render targets a draw writes, resolved from the bound
         * framebuffer. */
        currentTargets() {
            const s = this.current;
            if (!s.drawFramebuffer) {
                const view = this.ensureBackBuffer();
                return {
                    color: [{ view, format: this.backBufferFormat,
                              texture: this.backBuffer, mipLevel: 0,
                              origin: { x: 0, y: 0, z: 0 } }],
                    depth: this.ensureDepthTarget(),
                    depthFormat: "depth24plus-stencil8",
                    width: this.backBufferWidth, height: this.backBufferHeight,
                    isDefault: true,
                };
            }
            const fbo = this.framebuffers.get(s.id).get(s.drawFramebuffer);
            if (!fbo) return null;
            const color = [];
            for (const buffer of s.drawBuffers) {
                if (buffer === GL.NONE) { color.push(null); continue; }
                const index = buffer >= GL.COLOR_ATTACHMENT0 ?
                    buffer - GL.COLOR_ATTACHMENT0 : 0;
                const attachment = fbo.color[index];
                if (!attachment) { color.push(null); continue; }
                const resolved = this.resolveAttachment(attachment);
                color.push(resolved);
            }
            if (!color.length) color.push(null);
            // WebGPU exposes depth and stencil as one render-pass attachment.
            // A stencil-only FBO therefore still needs to supply that view.
            const depthRecord = fbo.depth || fbo.stencil;
            const depth = depthRecord ? this.resolveAttachment(depthRecord) : null;
            return {
                color,
                depth: depth ? depth.view : null,
                depthFormat: depth ? depth.format : null,
                width: fbo.width, height: fbo.height,
                isDefault: false,
            };
        },

        resolveAttachment(attachment) {
            if (attachment.kind === "texture") {
                const texture = this.current.shareGroup.textures.get(attachment.name);
                if (!texture) return null;
                const gpuTexture = this.ensureTextureUploaded(texture);
                if (!gpuTexture) return null;
                return {
                    view: gpuTexture.createView({
                        baseMipLevel: attachment.level, mipLevelCount: 1,
                        baseArrayLayer: attachment.layer, arrayLayerCount: 1,
                        dimension: "2d",
                    }),
                    format: texture.gpuFormat,
                    texture: gpuTexture,
                    mipLevel: attachment.level,
                    origin: { x: 0, y: 0, z: attachment.layer },
                };
            }
            const rb = this.current.shareGroup.renderbuffers.get(attachment.name);
            if (!rb || !rb.gpuTexture) return null;
            return { view: rb.gpuTexture.createView(), format: rb.gpuFormat,
                     texture: rb.gpuTexture, mipLevel: 0,
                     origin: { x: 0, y: 0, z: 0 } };
        },

        ensureBackBuffer() {
            const width = Math.max(1, this.backBufferWidth);
            const height = Math.max(1, this.backBufferHeight);
            if (this.backBuffer && this.backBufferView) return this.backBufferView;
            this.retire(this.backBuffer);
            this.backBufferFormat = (this.host && this.host.format) || "bgra8unorm";
            this.backBuffer = this.device.createTexture({
                label: "GL back buffer",
                size: { width, height, depthOrArrayLayers: 1 },
                format: this.backBufferFormat,
                usage: TEXTURE_USAGE_RENDER_ATTACHMENT | TEXTURE_USAGE_COPY_SRC |
                    TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
            });
            this.backBufferView = this.backBuffer.createView();
            return this.backBufferView;
        },

        ensureDepthTarget() {
            const width = Math.max(1, this.backBufferWidth);
            const height = Math.max(1, this.backBufferHeight);
            if (this.depthTarget && this.depthWidth === width &&
                    this.depthHeight === height)
                return this.depthTargetView;
            this.retire(this.depthTarget);
            this.depthTarget = this.device.createTexture({
                label: "GL depth-stencil",
                size: { width, height, depthOrArrayLayers: 1 },
                format: "depth24plus-stencil8",
                usage: TEXTURE_USAGE_RENDER_ATTACHMENT,
            });
            this.depthTargetView = this.depthTarget.createView();
            this.depthWidth = width;
            this.depthHeight = height;
            return this.depthTargetView;
        },

        /* ---- render pass management ----
         *
         * The pass is opened lazily so that a glClear arriving before the first
         * draw becomes a loadOp instead of a full-screen quad. That is the
         * shape of almost every GL frame, so the common case costs nothing.
         */
        ensureEncoder() {
            if (!this.encoder)
                this.encoder = this.device.createCommandEncoder({ label: "GL frame" });
            return this.encoder;
        },

        ensurePass() {
            if (this.pass) return this.pass;
            const targets = this.currentTargets();
            if (!targets) return null;
            const encoder = this.ensureEncoder();
            const colorAttachments = targets.color.map(target => target ? {
                view: target.view,
                loadOp: this.pendingClear && this.pendingClear.color ?
                    "clear" : "load",
                clearValue: this.pendingClear && this.pendingClear.color ?
                    this.pendingClear.color : undefined,
                storeOp: "store",
            } : null);
            const descriptor = {
                label: "GL pass",
                colorAttachments,
            };
            if (targets.depth) {
                descriptor.depthStencilAttachment = {
                    view: targets.depth,
                    depthLoadOp: this.pendingClear && this.pendingClear.depth !== undefined ?
                        "clear" : "load",
                    depthClearValue: this.pendingClear && this.pendingClear.depth !== undefined ?
                        this.pendingClear.depth : undefined,
                    depthStoreOp: "store",
                    stencilLoadOp: this.pendingClear && this.pendingClear.stencil !== undefined ?
                        "clear" : "load",
                    stencilClearValue: this.pendingClear &&
                        this.pendingClear.stencil !== undefined ?
                        this.pendingClear.stencil : undefined,
                    stencilStoreOp: "store",
                };
            }
            if (this.activeOcclusionQuerySet)
                descriptor.occlusionQuerySet = this.activeOcclusionQuerySet;
            // A pass can only run occlusion queries if it was *created* with
            // the query set, so beginQuery has to know whether this one was.
            this.passHasOcclusionQuerySet = !!this.activeOcclusionQuerySet;
            this.pass = encoder.beginRenderPass(descriptor);
            this.passTargets = targets;
            this.pendingClear = null;
            this.passStateApplied = false;
            return this.pass;
        },

        endPass() {
            if (this.pass) {
                this.pass.end();
                this.pass = null;
                this.passTargets = null;
                this.passHasOcclusionQuerySet = false;
            }
        },

        flushFrame() {
            this.endPass();
            const resolved = this.resolvePendingQueries();
            if (this.encoder) {
                this.device.queue.submit([this.encoder.finish()]);
                this.encoder = null;
                this.recordedOps = 0;
                this.releaseRetired();
            }
            if (resolved) this.readQueryResults(resolved);
        },

        /*
         * Occlusion results only exist after the GPU has run the pass, so they
         * follow glReadPixels' shape: resolve into a buffer alongside the work
         * that produced them, then map it once the submission lands. Until the
         * map completes glGetQueryObjectuiv keeps answering NOT AVAILABLE,
         * which is what the guest's own spin on GL_QUERY_RESULT_AVAILABLE is
         * waiting for -- so a query that is never resolved is not a stale
         * number, it is a guest that spins forever.
         *
         * Every query outstanding at the flush resolves in one pair of buffers.
         * Cube 2 retires two thousand of them at a time, and one resolve of
         * slots [0, high] costs the same as one of slot 3.
         */
        resolvePendingQueries() {
            const pending = this.pendingQueries;
            if (!pending || !pending.length || !this.activeOcclusionQuerySet)
                return null;
            this.pendingQueries = [];
            let high = 0;
            for (const query of pending)
                if (query.slot >= 0 && query.slot + 1 > high) high = query.slot + 1;
            if (!high) return null;
            // resolveQuerySet writes a u64 per slot; a mapped buffer cannot
            // also be a resolve target, hence the copy into staging.
            const byteLength = alignUp(high * 8, 256);
            const resolveBuffer = this.device.createBuffer({
                label: "GL occlusion resolve",
                size: byteLength,
                usage: BUFFER_USAGE_QUERY_RESOLVE | BUFFER_USAGE_COPY_SRC,
            });
            const staging = this.device.createBuffer({
                label: "GL occlusion readback",
                size: byteLength,
                usage: BUFFER_USAGE_COPY_DST | BUFFER_USAGE_MAP_READ,
            });
            const encoder = this.ensureEncoder();
            encoder.resolveQuerySet(this.activeOcclusionQuerySet, 0, high,
                resolveBuffer, 0);
            encoder.copyBufferToBuffer(resolveBuffer, 0, staging, 0, byteLength);
            // The slots are free again for the next frame's queries; the
            // results are already on their way out of the set.
            this.nextQuerySlot = 0;
            return { queries: pending, resolveBuffer, staging, count: high };
        },

        readQueryResults(resolved) {
            const { queries, resolveBuffer, staging } = resolved;
            staging.mapAsync(1 /* GPUMapMode.READ */).then(() => {
                const words = new Uint32Array(staging.getMappedRange().slice(0));
                staging.unmap();
                for (const query of queries) {
                    if (query.slot < 0) { query.ready = true; continue; }
                    const low = words[query.slot * 2];
                    const high = words[query.slot * 2 + 1];
                    // D-07: WebGPU answers "did any sample pass", not how many.
                    // A visible query reports a saturated count rather than 1,
                    // because callers threshold on it far more often than they
                    // compare it to an expected sample total.
                    query.result = (low || high) ? OCCLUSION_VISIBLE_SAMPLES : 0;
                    query.ready = true;
                }
            }).catch(error => {
                this.warnOnce("occlusion", "occlusion query readback failed",
                    { message: String(error) });
                // Never leave the guest spinning on AVAILABLE for a result the
                // GPU will not deliver: report zero samples, but report it.
                for (const query of queries) {
                    query.result = 0;
                    query.ready = true;
                }
            }).then(() => {
                try { staging.destroy(); } catch (ignored) { /* already gone */ }
                try { resolveBuffer.destroy(); } catch (ignored) { /* gone */ }
            });
        },

        finishFrame(present) {
            this.endPass();
            if (present) this.presentToCanvas();
            this.flushFrame();
            this.vertexCursor = 0;
            this.uniformCursor = 0;
        },

        /*
         * glClear. An unrestricted clear becomes the next pass's load
         * operation. Scissor and write masks cannot be represented by loadOp,
         * so those cases use a full-screen triangle with exact colour, depth
         * and stencil write masks.
         */
        clearBuffers(mask) {
            const s = this.current;
            const gpuMask = mask & (GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT |
                GL.STENCIL_BUFFER_BIT);
            const maskedColor = !!(mask & GL.COLOR_BUFFER_BIT) &&
                s.colorMask.some(value => !value);
            const maskedDepth = !!(mask & GL.DEPTH_BUFFER_BIT) && !s.depthMask;
            const maskedStencil = !!(mask & GL.STENCIL_BUFFER_BIT) &&
                (s.stencil.front.writeMask >>> 0) !== 0xffffffff;
            if (gpuMask && ((s.enabled.has(GL.SCISSOR_TEST) && s.scissor.set) ||
                    maskedColor || maskedDepth || maskedStencil)) {
                if (mask & GL.ACCUM_BUFFER_BIT) this.clearAccumBuffer();
                this.clearBuffersWithDraw(gpuMask);
                return;
            }
            const pending = this.pendingClear || {};
            if (mask & GL.COLOR_BUFFER_BIT) {
                pending.color = {
                    r: s.clearColor[0], g: s.clearColor[1],
                    b: s.clearColor[2], a: s.clearColor[3],
                };
            }
            if (mask & GL.DEPTH_BUFFER_BIT) pending.depth = s.clearDepth;
            if (mask & GL.STENCIL_BUFFER_BIT) pending.stencil = s.clearStencil;
            if (mask & GL.ACCUM_BUFFER_BIT) this.clearAccumBuffer();
            if (!(mask & (GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT |
                    GL.STENCIL_BUFFER_BIT)))
                return;
            this.endPass();
            this.pendingClear = pending;
            // Opening the pass immediately makes the clear happen even if no
            // draw follows, which is what a frame that only clears expects.
            this.ensurePass();
        },

        clearBuffersWithDraw(mask) {
            const s = this.current;
            this.endPass();
            const targets = this.currentTargets();
            if (!targets) return;
            const clearColor = !!(mask & GL.COLOR_BUFFER_BIT);
            const clearDepth = !!(mask & GL.DEPTH_BUFFER_BIT) && s.depthMask &&
                !!targets.depth;
            const clearStencil = !!(mask & GL.STENCIL_BUFFER_BIT) &&
                (s.stencil.front.writeMask >>> 0) !== 0 && !!targets.depth &&
                targets.depthFormat && targets.depthFormat.indexOf("stencil") >= 0;
            if (!clearColor && !clearDepth && !clearStencil) return;
            const pipeline = this.clearPipeline(targets, clearColor, clearDepth,
                clearStencil);
            if (!pipeline) return;
            const pass = this.ensureEncoder().beginRenderPass({
                label: "GL masked/scissored clear",
                colorAttachments: targets.color.map(target => target ? {
                    view: target.view, loadOp: "load", storeOp: "store",
                } : null),
                ...(targets.depth ? { depthStencilAttachment: {
                    view: targets.depth,
                    depthLoadOp: "load", depthStoreOp: "store",
                    stencilLoadOp: "load", stencilStoreOp: "store",
                } } : {}),
            });
            this.applyAccumScissor(pass, targets);
            if (clearStencil) pass.setStencilReference(s.clearStencil >>> 0);
            pass.setPipeline(pipeline);
            pass.draw(3, 1, 0, 0);
            pass.end();
        },

        clearPipeline(targets, clearColor, clearDepth, clearStencil) {
            const s = this.current;
            const colorFormats = targets.color.map(target => target ?
                target.format : "-").join(",");
            const key = [colorFormats, targets.depthFormat || "-",
                clearColor ? s.colorMask.map(v => v ? 1 : 0).join("") : "-",
                clearDepth ? 1 : 0, clearStencil ?
                    (s.stencil.front.writeMask >>> 0) : 0,
                [...s.clearColor], s.clearDepth].join("|");
            this.clearPipelineCache = this.clearPipelineCache || new Map();
            let pipeline = this.clearPipelineCache.get(key);
            if (pipeline) return pipeline;
            const colorFields = clearColor ? targets.color.map((target, i) =>
                target ? "    @location(" + i + ") color" + i +
                    " : vec4<f32>," : "").join("\n") : "";
            const depthField = clearDepth ?
                "    @builtin(frag_depth) depth : f32," : "";
            const colorWrites = clearColor ? targets.color.map((target, i) =>
                target ? "    out.color" + i + " = vec4<f32>(" +
                    Array.from(s.clearColor).map(v => Number(v).toPrecision(9))
                        .join(", ") + ");" : "").join("\n") : "";
            const depthWrite = clearDepth ? "    out.depth = " +
                Number(s.clearDepth).toPrecision(9) + ";" : "";
            const fragmentCode = colorFields || depthField ? `
struct ClearOut {
${colorFields}
${depthField}
}
@fragment fn fs_main() -> ClearOut {
    var out : ClearOut;
${colorWrites}
${depthWrite}
    return out;
}` : "@fragment fn fs_main() {}";
            const code = `
@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}
${fragmentCode}`;
            const module = this.shaderModule(code);
            const descriptor = {
                label: "GL clear pipeline",
                layout: "auto",
                vertex: { module, entryPoint: "vs_main" },
                fragment: { module, entryPoint: "fs_main",
                    targets: targets.color.map(target => target ? {
                        format: target.format,
                        writeMask: clearColor ?
                            (s.colorMask[0] ? 1 : 0) |
                            (s.colorMask[1] ? 2 : 0) |
                            (s.colorMask[2] ? 4 : 0) |
                            (s.colorMask[3] ? 8 : 0) : 0,
                    } : null) },
                primitive: { topology: "triangle-list" },
            };
            if (targets.depthFormat) {
                descriptor.depthStencil = {
                    format: targets.depthFormat,
                    depthWriteEnabled: clearDepth,
                    depthCompare: "always",
                    ...(clearStencil ? {
                        stencilFront: { compare: "always", failOp: "keep",
                            depthFailOp: "keep", passOp: "replace" },
                        stencilBack: { compare: "always", failOp: "keep",
                            depthFailOp: "keep", passOp: "replace" },
                        stencilReadMask: 0xffffffff,
                        stencilWriteMask: s.stencil.front.writeMask >>> 0,
                    } : {}),
                };
            }
            try {
                pipeline = this.device.createRenderPipeline(descriptor);
            } catch (error) {
                this.refuse("glClear", "could not create masked clear pipeline",
                    { message: String(error) }, GL.INVALID_OPERATION);
                return null;
            }
            this.clearPipelineCache.set(key, pipeline);
            return pipeline;
        },

        clearAccumBuffer() {
            this.endPass();
            const targets = this.currentTargets();
            if (!targets) return;
            const accum = this.ensureAccumBuffer(targets);
            const scissored = this.current.enabled.has(GL.SCISSOR_TEST) &&
                this.current.scissor.set;
            const pass = this.ensureEncoder().beginRenderPass({
                label: "GL accumulation clear",
                colorAttachments: [{
                    view: accum.currentView,
                    loadOp: scissored ? "load" : "clear",
                    clearValue: {
                        r: this.current.clearAccum[0],
                        g: this.current.clearAccum[1],
                        b: this.current.clearAccum[2],
                        a: this.current.clearAccum[3],
                    },
                    storeOp: "store",
                }],
            });
            this.applyAccumScissor(pass, targets);
            if (scissored) {
                const pipeline = this.accumClearPipeline();
                if (pipeline) {
                    pass.setPipeline(pipeline);
                    pass.draw(3, 1, 0, 0);
                }
            }
            pass.end();
        },

        /* ---- the draw ---- */

        issueDraw(request) {
            const s = this.current;
            if (s.renderMode !== GL.RENDER) return;   // GL_SELECT/GL_FEEDBACK
            if (s.enabled.has(GL.CULL_FACE) && s.cullFace === GL.FRONT_AND_BACK)
                return;
            let index = request.index;
            if (isPolygonPrimitive(request.mode)) {
                const rasterMode = this.effectivePolygonMode();
                if (rasterMode === GL.LINE || rasterMode === GL.POINT) {
                    const rasterIndices = polygonRasterIndices(request.mode,
                        request.vertexCount, index && index.source, rasterMode);
                    if (!rasterIndices.length) return;
                    const slice = this.uploadVertices(new Uint8Array(
                        rasterIndices.buffer, rasterIndices.byteOffset,
                        rasterIndices.byteLength));
                    if (!slice) return;
                    index = { offset: slice.offset, count: rasterIndices.length,
                        format: "uint32", cpu: rasterIndices };
                    request = { ...request,
                        mode: rasterMode === GL.LINE ? GL.LINES : GL.POINTS };
                }
            }
            this.drawingPoints = request.mode === GL.POINTS;
            if (s.enabled.has(GL.STENCIL_TEST) && stencilMasksDiverge(s))
                this.warnOnce("stencil-masks",
                    "the front and back stencil masks differ; WebGPU has one " +
                    "pair for both faces, so the front face's are used " +
                    "(deviation D-02)", { front: s.stencil.front,
                                          back: s.stencil.back });
            const shaders = this.resolveShaders();
            if (!shaders) return;
            if (s.enabled.has(GL.COLOR_LOGIC_OP) && s.logicOp !== GL.COPY &&
                    !this.prepareLogicOpTargets()) return;
            const pass = this.ensurePass();
            if (!pass) {
                this.refuse("draw", "no render target is bound", {},
                    GL.INVALID_FRAMEBUFFER_OPERATION);
                return;
            }

            /* WebGPU only rasterises one-pixel points. GL point size and point
             * sprites are implemented as an instanced six-vertex quad: the
             * application's attributes advance once per point, while this
             * extra corner attribute advances for the six quad vertices. */
            const expandedPoints = request.mode === GL.POINTS && !index &&
                (s.point.size !== 1 || s.enabled.has(GL.POINT_SPRITE));
            if (expandedPoints) {
                const corners = new Float32Array([
                    -1, -1,  1, -1, -1,  1,
                    -1,  1,  1, -1,  1,  1,
                ]);
                const cornerSlice = this.uploadVertices(
                    new Uint8Array(corners.buffer));
                if (!cornerSlice) return;
                request = {
                    ...request,
                    expandedPoints: true,
                    buffers: request.buffers.map(buffer => ({
                        ...buffer,
                        stepMode: "instance",
                    })).concat([{
                        gpuBuffer: this.vertexRing,
                        baseOffset: cornerSlice.offset,
                        stride: 8,
                        stepMode: "vertex",
                        attributes: [{
                            location: translator.POINT_CORNER_LOCATION,
                            format: "float32x2",
                            offset: 0,
                        }],
                    }]),
                };
            }

            if (!index && needsIndexExpansion(request.mode)) {
                const expanded = this.expansionIndices(request.mode,
                    request.vertexCount);
                if (!expanded.length) return;
                const slice = this.uploadVertices(new Uint8Array(expanded.buffer,
                    expanded.byteOffset, expanded.byteLength));
                if (!slice) return;
                index = { offset: slice.offset, count: expanded.length,
                          format: "uint32", cpu: expanded };
            }

            const pipeline = this.ensurePipeline(shaders, request, index);
            if (!pipeline) return;
            const bindGroups = this.buildBindGroups(pipeline, shaders);
            if (!bindGroups) return;

            pass.setPipeline(pipeline);
            this.applyPassState(pass);
            for (const entry of bindGroups)
                pass.setBindGroup(entry.index, entry.group);
            request.buffers.forEach((buffer, i) => {
                pass.setVertexBuffer(i, buffer.gpuBuffer, buffer.baseOffset);
            });
            if (index) {
                pass.setIndexBuffer(this.vertexRing, index.format, index.offset);
                pass.drawIndexed(index.count, 1, 0, request.firstVertex || 0, 0);
            } else if (expandedPoints) {
                pass.draw(6, request.vertexCount, 0, request.firstVertex || 0);
            } else {
                pass.draw(request.vertexCount, 1, request.firstVertex || 0, 0);
            }
            ++this.stats.draws;
            if (++this.recordedOps >= this.flushThreshold) {
                // A frame's draw count is unbounded; flushing keeps both the
                // command buffer and the ring allocations proportional to work
                // done rather than to the frame's length.
                this.endPass();
                this.flushFrame();
            }
        },

        applyPassState(pass) {
            const s = this.current;
            const targets = this.passTargets;
            const width = targets ? targets.width : this.backBufferWidth;
            const height = targets ? targets.height : this.backBufferHeight;
            const vp = s.viewport;
            const x = clamp(vp.x, 0, width);
            const y = clamp(vp.y, 0, height);
            pass.setViewport(x, y,
                Math.max(0, Math.min(vp.width, width - x)),
                Math.max(0, Math.min(vp.height, height - y)),
                s.depthRange.near, s.depthRange.far);
            if (s.enabled.has(GL.SCISSOR_TEST) && s.scissor.set) {
                const sx = clamp(s.scissor.x, 0, width);
                const sy = clamp(s.scissor.y, 0, height);
                pass.setScissorRect(sx, sy,
                    Math.max(0, Math.min(s.scissor.width, width - sx)),
                    Math.max(0, Math.min(s.scissor.height, height - sy)));
            } else {
                pass.setScissorRect(0, 0, width, height);
            }
            if (s.enabled.has(GL.STENCIL_TEST))
                pass.setStencilReference(s.stencil.front.ref >>> 0);
            if (s.enabled.has(GL.BLEND))
                pass.setBlendConstant({
                    r: s.blend.color[0], g: s.blend.color[1],
                    b: s.blend.color[2], a: s.blend.color[3],
                });
        },

        prepareLogicOpTargets() {
            // Consume a pending loadOp clear before taking the destination
            // snapshot, then continue in a second pass that samples only the
            // copy. Sampling the live attachment would be a WebGPU feedback
            // loop and is rejected by validation.
            const initialPass = this.ensurePass();
            if (!initialPass) {
                this.refuse("draw", "no render target is bound", {},
                    GL.INVALID_FRAMEBUFFER_OPERATION);
                return false;
            }
            this.endPass();
            const targets = this.currentTargets();
            if (!targets) return false;
            const width = Math.max(1, targets.width);
            const height = Math.max(1, targets.height);
            this.logicTargetTextures = this.logicTargetTextures || [];
            this.logicTargetViews = [];
            const encoder = this.ensureEncoder();
            let fallbackView = null;
            for (let i = 0; i < targets.color.length; ++i) {
                const source = targets.color[i];
                if (!source || !source.texture) {
                    this.logicTargetViews.push(fallbackView || this.fallbackView);
                    continue;
                }
                let copy = this.logicTargetTextures[i];
                const key = source.format + ":" + width + "x" + height;
                if (!copy || copy.glLogicKey !== key) {
                    this.retire(copy);
                    copy = this.device.createTexture({
                        label: "GL logic-op destination copy " + i,
                        size: { width, height, depthOrArrayLayers: 1 },
                        format: source.format,
                        usage: TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
                    });
                    copy.glLogicKey = key;
                    this.logicTargetTextures[i] = copy;
                }
                encoder.copyTextureToTexture({
                    texture: source.texture,
                    mipLevel: source.mipLevel || 0,
                    origin: source.origin || { x: 0, y: 0, z: 0 },
                }, { texture: copy },
                { width, height, depthOrArrayLayers: 1 });
                const view = copy.createView();
                if (!fallbackView) fallbackView = view;
                this.logicTargetViews.push(view);
            }
            if (fallbackView) {
                for (let i = 0; i < this.logicTargetViews.length; ++i)
                    if (this.logicTargetViews[i] === this.fallbackView)
                        this.logicTargetViews[i] = fallbackView;
            }
            return true;
        },

        effectivePolygonMode() {
            const s = this.current;
            const front = s.polygonMode.front;
            const back = s.polygonMode.back;
            if (front === back) return front;
            if (s.enabled.has(GL.CULL_FACE)) {
                if (s.cullFace === GL.FRONT) return back;
                if (s.cullFace === GL.BACK) return front;
            }
            this.warnOnce("two-face-polygon-mode",
                "front and back polygon modes differ without face culling; " +
                "the front mode is used for both faces",
                { front, back });
            return front;
        },
    });

    /* ================================================================== */
    /* Executor: shader selection, pipelines and bind groups              */
    /* ================================================================== */

    Object.assign(GLWebGPUExecutor.prototype, {

        /* The pipeline variant state GL still applies around a programmable
         * pipeline: alpha test, user clip planes, point sprites, two-sided
         * colour and flat shading all change the generated WGSL. */
        currentVariant() {
            const s = this.current;
            let clipPlaneCount = 0;
            for (let i = 0; i < MAX_CLIP_PLANES; ++i)
                if (s.enabled.has(GL.CLIP_PLANE0 + i)) clipPlaneCount = i + 1;
            return {
                alphaTest: s.enabled.has(GL.ALPHA_TEST) ?
                    (ALPHA_TEST_NAMES[s.alphaFunc.func] || "always") : "always",
                clipPlaneCount,
                pointSprite: this.drawingPoints &&
                    (s.enabled.has(GL.POINT_SPRITE) || s.point.size !== 1),
                pointCoordLowerLeft:
                    s.point.spriteCoordOrigin === GL.LOWER_LEFT,
                polygonStipple: s.enabled.has(GL.POLYGON_STIPPLE),
                logicOp: s.enabled.has(GL.COLOR_LOGIC_OP) ? s.logicOp : 0,
                twoSided: s.lightModel.twoSide,
                flatShading: s.shadeModel === GL.FLAT,
                colorTargets: this.colorTargetCount(),
            };
        },

        colorTargetCount() {
            const s = this.current;
            if (!s.drawFramebuffer) return 1;
            return Math.max(1, s.drawBuffers.length);
        },

        arbShaders() {
            const s = this.current;
            const vertexOn = s.enabled.has(GL.VERTEX_PROGRAM_ARB);
            const fragmentOn = s.enabled.has(GL.FRAGMENT_PROGRAM_ARB);
            if (!vertexOn && !fragmentOn) return null;
            if (s.enabled.has(GL.COLOR_LOGIC_OP)) {
                this.refuse("draw", "colour logic operations cannot be injected " +
                    "into an ARB assembly fragment program", { op: s.logicOp }, 0);
                return null;
            }
            const vertex = vertexOn ? this.arbProgramFor(GL.VERTEX_PROGRAM_ARB) : null;
            const fragment = fragmentOn ?
                this.arbProgramFor(GL.FRAGMENT_PROGRAM_ARB) : null;
            if ((vertexOn && (!vertex || !vertex.compiled || !vertex.compiled.ok)) ||
                    (fragmentOn && (!fragment || !fragment.compiled ||
                        !fragment.compiled.ok))) {
                this.refuse("draw", "an enabled ARB program has not compiled",
                    {}, GL.INVALID_OPERATION);
                return null;
            }
            if (vertexOn && !fragmentOn)
                return this.arbVertexFixedFragmentShaders(vertex);
            if (!vertexOn && fragmentOn) {
                this.refuse("draw", "a fixed vertex stage paired with an ARB " +
                    "fragment program is not implemented", {}, 0);
                return null;
            }
            const stateFields = [...new Set([
                ...vertex.compiled.reflection.stateFields,
                ...fragment.compiled.reflection.stateFields])];
            return {
                kind: "arb",
                key: "arb" + vertex.name + ":" + fragment.name,
                wgslVertex: vertex.compiled.wgsl,
                wgslFragment: fragment.compiled.wgsl,
                stateFields,
                stateFloats: stateLayout.buildLayout(stateFields).floats,
                uniformBytes: arbProgram.MAX_PROGRAM_PARAMETERS * 4 * 4 * 2,
                colorTargets: 1,
                arbVertex: vertex, arbFragment: fragment,
                textures: fragment.compiled.reflection.textures.map(entry => ({
                    unit: entry.unit,
                    textureBinding: entry.unit * 2,
                    samplerBinding: entry.unit * 2 + 1,
                    target: entry.target === "CUBE" ? "Cube" : entry.target,
                    shadow: false,
                })),
            };
        },

        arbVertexFixedFragmentShaders(vertex) {
            const variant = this.currentVariant();
            if (variant.clipPlaneCount || variant.pointSprite) {
                this.refuse("draw", "ARB vertex + fixed fragment does not yet " +
                    "support clip planes or point sprites",
                    { clipPlaneCount: variant.clipPlaneCount,
                      pointSprite: variant.pointSprite }, 0);
                return null;
            }

            const signature = this.fixedFunctionSignature(variant);
            signature.varyingInterface = "arb";
            // The two modules share one compact GLState block. Generate the
            // fixed stage with the ARB stage's fields included so every field
            // retains the same canonical byte offset in both modules.
            signature.extraStateFields =
                vertex.compiled.reflection.stateFields.slice();
            const fixedKey = fixedFunction.signatureKey(signature);
            let fixed = this.ffCache && this.ffCache.get(fixedKey);
            if (!fixed) {
                fixed = fixedFunction.generate(signature);
                this.ffCache = this.ffCache || new Map();
                this.ffCache.set(fixedKey, fixed);
            }

            const forcedTexCoords = fixed.textures.map(entry => entry.unit);
            const variantKey = fixed.key + "|tc=" + forcedTexCoords.join(",");
            vertex.variants = vertex.variants || new Map();
            let compiledVertex = vertex.variants.get(variantKey);
            if (!compiledVertex) {
                compiledVertex = arbProgram.compileARBProgram(vertex.source, {
                    forceStateFields: fixed.stateFields,
                    forceVertexTexCoords: forcedTexCoords,
                    forceFlatVaryings: variant.flatShading ? [0, 1] : [],
                });
                if (!compiledVertex.ok) {
                    this.refuse("draw", "the mixed ARB vertex variant failed " +
                        "to compile", { log: compiledVertex.log },
                        GL.INVALID_OPERATION);
                    return null;
                }
                vertex.variants.set(variantKey, compiledVertex);
            }

            return {
                kind: "arb",
                key: "arb-vp-ff:" + vertex.name + ":" + variantKey,
                wgslVertex: compiledVertex.wgsl,
                wgslFragment: fixed.wgslFragment,
                stateFields: fixed.stateFields,
                stateFloats: fixed.stateFloats,
                uniformBytes: arbProgram.MAX_PROGRAM_PARAMETERS * 4 * 4 * 2,
                colorTargets: fixed.colorTargets,
                arbVertex: vertex,
                arbFragment: null,
                textures: fixed.textures,
            };
        },

        resolveShaders() {
            if (this.current.enabled.has(GL.VERTEX_PROGRAM_ARB) ||
                    this.current.enabled.has(GL.FRAGMENT_PROGRAM_ARB))
                return this.arbShaders();
            const program = this.currentProgramObject();
            const variant = this.currentVariant();
            if (program) {
                const key = this.variantKeyFor(variant);
                let entry = program.variants.get(key);
                if (!entry) {
                    const link = this.linkVariant(program, variant);
                    if (!link.ok) {
                        this.refuse("draw", "the program variant failed to link",
                            { program: program.name, log: link.log },
                            GL.INVALID_OPERATION);
                        return null;
                    }
                    entry = { link, key };
                    program.variants.set(key, entry);
                }
                return {
                    kind: "program", key: "p" + program.name + "|" + key,
                    program,
                    wgslVertex: entry.link.wgslVertex,
                    wgslFragment: entry.link.wgslFragment,
                    reflection: entry.link.reflection,
                    stateFields: entry.link.reflection.stateFields,
                    stateFloats: entry.link.reflection.stateFloats,
                    uniformBytes: entry.link.reflection.uniformBlockBytes,
                    colorTargets: entry.link.reflection.colorTargets,
                };
            }
            const signature = this.fixedFunctionSignature(variant);
            const key = fixedFunction.signatureKey(signature);
            let generated = this.ffCache && this.ffCache.get(key);
            if (!generated) {
                generated = fixedFunction.generate(signature);
                this.ffCache = this.ffCache || new Map();
                this.ffCache.set(key, generated);
            }
            return {
                kind: "ff", key,
                wgslVertex: generated.wgslVertex,
                wgslFragment: generated.wgslFragment,
                stateFields: generated.stateFields,
                stateFloats: generated.stateFloats,
                uniformBytes: 16,
                textures: generated.textures,
                colorTargets: generated.colorTargets,
            };
        },

        fixedFunctionSignature(variant) {
            const s = this.current;

            const texture = [];
            for (let unit = 0; unit < MAX_TEXTURE_UNITS; ++unit) {
                const state = s.textureUnits[unit];
                const target = this.effectiveTarget(state);
                if (!target) { texture.push({ enabled: false }); continue; }
                const object = this.textureObjectFor(unit, target.enumValue);
                const complete = this.textureIsComplete(object);
                texture.push({
                    enabled: true,
                    target: target.kind,
                    shadow: !!(object && object.sampler.compareMode ===
                        GL.COMPARE_R_TO_TEXTURE),
                    format: complete && object ? object.baseFormat : "RGBA",
                    matrix: !isIdentity(this.textureMatrixOf(unit)),
                    texGen: state.texGen.map(gen => gen.enabled ?
                        (TEXGEN_MODE_NAMES[gen.mode] || "EYE") : null),
                    env: this.texEnvSignature(state.env),
                });
            }
            const texGenNeedsNormal = texture.some(unit => unit.enabled &&
                unit.texGen.some(mode => mode === "SPHERE" ||
                    mode === "REFLECTION" || mode === "NORMAL"));

            return {
                attributes: {
                    position: { components: 4 },
                    normal: s.enabled.has(GL.LIGHTING) || texGenNeedsNormal,
                    // Array draws use GL's current values when an array is
                    // disabled. The draw assembler supplies those values as a
                    // constant-rate vertex buffer, so the fixed shader must
                    // consume the attribute rather than bake white/defaults.
                    color: true,
                    secondaryColor: s.enabled.has(GL.COLOR_SUM) ||
                        s.lightModel.colorControl === GL.SEPARATE_SPECULAR_COLOR,
                    fogCoord: s.enabled.has(GL.FOG) &&
                        s.fog.coordSource === GL.FOG_COORD,
                    texCoord: Array.from({ length: MAX_TEXTURE_COORDS },
                        (unused, unit) => texture[unit] && texture[unit].enabled ?
                            { components: 4 } : null),
                },
                lighting: {
                    enabled: s.enabled.has(GL.LIGHTING),
                    twoSide: s.lightModel.twoSide,
                    localViewer: s.lightModel.localViewer,
                    separateSpecular:
                        s.lightModel.colorControl === GL.SEPARATE_SPECULAR_COLOR,
                    normalMode: s.enabled.has(GL.NORMALIZE) ? "normalize" :
                        (s.enabled.has(GL.RESCALE_NORMAL) ? "rescale" : "none"),
                    colorMaterial: {
                        enabled: s.colorMaterial.enabled,
                        face: FACE_NAMES[s.colorMaterial.face] || "FRONT_AND_BACK",
                        mode: COLOR_MATERIAL_MODES[s.colorMaterial.mode] ||
                            "AMBIENT_AND_DIFFUSE",
                    },
                    lights: s.lights.map(light => ({
                        enabled: light.enabled,
                        positional: light.position[3] !== 0,
                        spot: light.spotCutoff !== 180,
                    })),
                },
                fog: {
                    enabled: s.enabled.has(GL.FOG),
                    mode: s.fog.mode === GL.EXP ? "exp" :
                        (s.fog.mode === GL.EXP2 ? "exp2" : "linear"),
                    coordSource: s.fog.coordSource === GL.FOG_COORD ?
                        "coord" : "depth",
                },
                texture,
                alphaTest: variant.alphaTest,
                clipPlaneCount: variant.clipPlaneCount,
                pointSprite: variant.pointSprite,
                pointCoordLowerLeft: variant.pointCoordLowerLeft,
                polygonStipple: variant.polygonStipple,
                logicOp: variant.logicOp,
                flatShading: variant.flatShading,
                colorTargets: variant.colorTargets,
            };
        },

        texEnvSignature(env) {
            const mode = TEXENV_MODE_NAMES[env.mode] || "MODULATE";
            if (mode !== "COMBINE") return {
                mode,
                coordReplace: !!env.pointSprite,
            };
            return {
                mode,
                coordReplace: !!env.pointSprite,
                combineRGB: COMBINE_RGB_NAMES[env.combineRGB] || "MODULATE",
                combineAlpha: COMBINE_RGB_NAMES[env.combineAlpha] || "MODULATE",
                srcRGB: env.srcRGB.map(v => COMBINE_SOURCE_NAMES[v] || "TEXTURE"),
                srcAlpha: env.srcAlpha.map(v => COMBINE_SOURCE_NAMES[v] || "TEXTURE"),
                operandRGB: env.operandRGB.map(v => OPERAND_NAMES[v] || "SRC_COLOR"),
                operandAlpha: env.operandAlpha.map(v =>
                    OPERAND_NAMES[v] || "SRC_ALPHA"),
                rgbScale: env.rgbScale,
                alphaScale: env.alphaScale,
            };
        },

        effectiveTarget(unitState) {
            for (const [enumValue, kind] of TARGET_PRIORITY)
                if (unitState.enabledTargets.has(enumValue))
                    return { enumValue, kind };
            return null;
        },

        textureObjectFor(unit, target) {
            const s = this.current;
            const name = s.textureUnits[unit].bindings[target];
            if (!name) return null;
            return s.shareGroup.textures.get(name) || null;
        },

        textureMatrixOf(unit) {
            const stack = this.current.textureStacks[unit];
            return stack[stack.length - 1];
        },

        /* ---- pipelines ---- */

        ensurePipeline(shaders, request, index) {
            const s = this.current;
            const targets = this.passTargets;
            const topology = PRIMITIVE_TOPOLOGY[request.mode];
            const strip = topology === "triangle-strip" || topology === "line-strip";
            const layoutKey = request.buffers.map(b =>
                b.stride + "@" + b.attributes.map(a =>
                    a.location + ":" + a.format + ":" + a.offset).join(",")).join("|");
            const signature = [
                shaders.key,
                layoutKey,
                request.expandedPoints ? "triangle-list" :
                    (needsIndexExpansion(request.mode) ? "triangle-list" : topology),
                strip && index ? "u32" : "-",
                this.gpuFrontFace(s), this.gpuCullMode(s),
                s.enabled.has(GL.DEPTH_TEST) ? s.depthFunc : "off",
                s.depthMask ? 1 : 0,
                s.enabled.has(GL.STENCIL_TEST) ? stencilKey(s) : "off",
                s.enabled.has(GL.COLOR_LOGIC_OP) ?
                    "logic:" + s.logicOp.toString(16) :
                    (s.enabled.has(GL.BLEND) ? blendKey(s) : "off"),
                s.colorMask.map(v => v ? 1 : 0).join(""),
                polygonOffsetKey(s),
                s.enabled.has(GL.SAMPLE_ALPHA_TO_COVERAGE) ? "a2c" : "-",
                sampleCoverageMask(s),
                targets ? targets.color.map(c => c ? c.format : "-").join(",") : "-",
                targets ? (targets.depthFormat || "-") : "-",
                s.enabled.has(GL.DEPTH_CLAMP) ? "clamp" : "-",
            ].join("|");

            let pipeline = this.pipelineCache.get(signature);
            if (pipeline) return pipeline;

            const vertexModule = this.shaderModule(shaders.wgslVertex);
            const fragmentModule = this.shaderModule(shaders.wgslFragment);
            const buffers = request.buffers.map(buffer => ({
                arrayStride: buffer.stride,
                stepMode: buffer.stepMode || "vertex",
                attributes: buffer.attributes.map(a => ({
                    shaderLocation: a.location,
                    format: a.format,
                    offset: a.offset,
                })),
            }));
            const colorTargets = (targets ? targets.color : [null]).map(target =>
                target ? {
                    format: target.format,
                    blend: s.enabled.has(GL.BLEND) &&
                        !s.enabled.has(GL.COLOR_LOGIC_OP) ?
                        gpuBlendState(s) : undefined,
                    writeMask: (s.colorMask[0] ? 1 : 0) | (s.colorMask[1] ? 2 : 0) |
                        (s.colorMask[2] ? 4 : 0) | (s.colorMask[3] ? 8 : 0),
                } : null);

            const descriptor = {
                label: "GL pipeline",
                layout: "auto",
                vertex: { module: vertexModule, entryPoint: "vs_main", buffers },
                fragment: { module: fragmentModule, entryPoint: "fs_main",
                            targets: colorTargets },
                primitive: {
                    topology: request.expandedPoints ? "triangle-list" :
                        (needsIndexExpansion(request.mode) ?
                        (request.mode === GL.LINE_LOOP ? "line-strip" : "triangle-list") :
                        topology),
                    frontFace: this.gpuFrontFace(s),
                    cullMode: this.gpuCullMode(s),
                    ...(strip && index ? { stripIndexFormat: "uint32" } : {}),
                    ...(s.enabled.has(GL.DEPTH_CLAMP) &&
                        this.deviceFeatures.depthClipControl ?
                        { unclippedDepth: true } : {}),
                },
                multisample: {
                    count: 1,
                    mask: sampleCoverageMask(s),
                    // WebGPU validation only permits alpha-to-coverage on a
                    // multisampled target. D-06 documents the current 1x path.
                    alphaToCoverageEnabled: false,
                },
            };
            if (targets && targets.depthFormat) {
                descriptor.depthStencil = {
                    format: targets.depthFormat,
                    depthWriteEnabled: !!(s.enabled.has(GL.DEPTH_TEST) ?
                        s.depthMask : false),
                    depthCompare: s.enabled.has(GL.DEPTH_TEST) ?
                        (COMPARE_FUNCTIONS[s.depthFunc] || "less") : "always",
                    ...(targets.depthFormat.indexOf("stencil") >= 0 ?
                        gpuStencilState(s) : {}),
                    depthBias: s.enabled.has(GL.POLYGON_OFFSET_FILL) ?
                        Math.round(s.polygonOffset.units) : 0,
                    depthBiasSlopeScale: s.enabled.has(GL.POLYGON_OFFSET_FILL) ?
                        s.polygonOffset.factor : 0,
                };
            }

            try {
                pipeline = this.device.createRenderPipeline(descriptor);
            } catch (error) {
                this.refuse("pipeline", "could not create the render pipeline",
                    { message: String(error), signature }, GL.INVALID_OPERATION);
                return null;
            }
            ++this.stats.pipelines;
            if (this.pipelineCache.size > 4096) this.pipelineCache.clear();
            this.pipelineCache.set(signature, pipeline);
            pipeline.glShaders = shaders;
            return pipeline;
        },

        shaderModule(wgsl) {
            let module = this.moduleCache.get(wgsl);
            if (!module) {
                module = this.device.createShaderModule({ code: wgsl });
                this.moduleCache.set(wgsl, module);
            }
            return module;
        },

        /* ---- bind groups ---- */

        /*
         * Group 1 holds the two uniform buffers, group 2 the textures and
         * samplers. The layouts come from the pipeline's automatic layout, so a
         * group the shader does not declare is simply not created rather than
         * bound empty -- asking for getBindGroupLayout on an absent group is an
         * error, and a fixed-function draw with no textures has no group 2.
         */
        buildBindGroups(pipeline, shaders) {
            const layout = stateLayout.buildLayout(shaders.stateFields);
            const stateSlice = this.writeStateUniforms(layout, shaders);
            if (!stateSlice) return null;
            // binding 1 is the GLSL program's uniform block, or the ARB
            // vertex program's parameters; binding 2 only ever holds an ARB
            // fragment program's.
            const bindingSlices = shaders.kind === "arb" ?
                this.writeARBParameters(shaders) :
                { 1: shaders.kind === "program" ?
                    this.writeProgramUniforms(shaders) :
                    this.writeEmptyUniforms() };
            if (!bindingSlices || (shaders.kind !== "arb" && !bindingSlices[1]))
                return null;

            const groups = [];
            try {
                const shaderText = shaders.wgslVertex + "\n" +
                    shaders.wgslFragment;
                const uniformEntries = [];
                if (shaderText.indexOf("@group(1) @binding(0)") >= 0) {
                    uniformEntries.push({ binding: 0,
                        resource: { buffer: this.uniformRing,
                            offset: stateSlice.offset, size: stateSlice.size } });
                }
                for (const binding of [1, 2]) {
                    if (shaderText.indexOf("@group(1) @binding(" + binding +
                            ")") < 0)
                        continue;
                    const slice = bindingSlices[binding];
                    if (!slice)
                        return this.refuse("draw", "a shader reads a uniform " +
                            "binding nothing was written for",
                            { binding, shader: shaders.key },
                            GL.INVALID_OPERATION);
                    uniformEntries.push({ binding,
                        resource: { buffer: this.uniformRing,
                            offset: slice.offset, size: slice.size } });
                }
                if (uniformEntries.length) {
                    groups.push({
                        index: 1,
                        group: this.device.createBindGroup({
                            layout: pipeline.getBindGroupLayout(1),
                            entries: uniformEntries,
                        }),
                    });
                }
                const textureEntries = this.textureBindEntries(shaders);
                if (textureEntries.length) {
                    groups.push({
                        index: 2,
                        group: this.device.createBindGroup({
                            layout: pipeline.getBindGroupLayout(2),
                            entries: textureEntries,
                        }),
                    });
                }
                if (this.current.enabled.has(GL.COLOR_LOGIC_OP) &&
                        this.current.logicOp !== GL.COPY) {
                    const views = this.logicTargetViews || [];
                    groups.push({
                        index: 3,
                        group: this.device.createBindGroup({
                            layout: pipeline.getBindGroupLayout(3),
                            entries: Array.from({ length: shaders.colorTargets || 1 },
                                (unused, binding) => ({
                                    binding,
                                    resource: views[binding] || views[0] ||
                                        this.fallbackView,
                                })),
                        }),
                    });
                }
            } catch (error) {
                this.refuse("draw", "could not build the bind groups",
                    { message: String(error), shader: shaders.key },
                    GL.INVALID_OPERATION);
                return null;
            }
            ++this.stats.bindGroups;
            return groups;
        },

        textureBindEntries(shaders) {
            const entries = [];
            if (shaders.kind === "ff" || shaders.kind === "arb") {
                for (const item of shaders.textures) {
                    const unitState = this.current.textureUnits[item.unit];
                    const target = this.effectiveTarget(unitState);
                    const object = target ?
                        this.textureObjectFor(item.unit, target.enumValue) : null;
                    entries.push({ binding: item.textureBinding,
                                   resource: this.textureViewFor(object, item) });
                    entries.push({ binding: item.samplerBinding,
                                   resource: this.samplerFor(object, item.shadow) });
                }
                return entries;
            }
            const program = shaders.program;
            for (const sampler of shaders.reflection.samplers) {
                const count = Math.max(1, sampler.arraySize);
                for (let i = 0; i < count; ++i) {
                    const key = sampler.name +
                        (sampler.arraySize ? "[" + i + "]" : "");
                    const unit = program.samplerUnits.get(key) || 0;
                    const unitState = this.current.textureUnits[
                        clamp(unit, 0, MAX_TEXTURE_UNITS - 1)];
                    const targetEnum = samplerTargetEnum(sampler.dim);
                    const object = unitState ?
                        (this.current.shareGroup.textures.get(
                            unitState.bindings[targetEnum]) || null) : null;
                    entries.push({ binding: sampler.binding + i * 2,
                                   resource: this.textureViewFor(object,
                                       { target: sampler.dim, shadow: sampler.shadow }) });
                    entries.push({ binding: sampler.binding + i * 2 + 1,
                                   resource: this.samplerFor(object, sampler.shadow) });
                }
            }
            return entries;
        },

        textureViewFor(object, item) {
            if (!object || !this.textureIsComplete(object)) return this.fallbackView;
            const gpuTexture = this.ensureTextureUploaded(object);
            if (!gpuTexture) return this.fallbackView;
            const kind = item.target === "Cube" || object.kind === "Cube" ?
                "cube" : (object.kind === "3D" ? "3d" : "2d");
            const key = kind + ":" + object.sampler.baseLevel + ":" +
                object.sampler.maxLevel;
            let view = object.viewCache.get(key);
            if (!view) {
                const baseMipLevel = clamp(object.sampler.baseLevel, 0,
                    object.gpuLevels - 1);
                const mipLevelCount = clamp(
                    object.sampler.maxLevel - baseMipLevel + 1, 1,
                    object.gpuLevels - baseMipLevel);
                view = gpuTexture.createView({
                    dimension: kind,
                    baseMipLevel, mipLevelCount,
                });
                object.viewCache.set(key, view);
            }
            return view;
        },

        samplerFor(object, shadow) {
            if (!object) return shadow ? this.fallbackComparisonSampler :
                this.fallbackSampler;
            const sampler = object.sampler;
            const key = [
                sampler.minFilter, sampler.magFilter,
                sampler.wrapS, sampler.wrapT, sampler.wrapR,
                sampler.maxAnisotropy, sampler.minLod, sampler.maxLod,
                shadow ? sampler.compareFunc : "-",
            ].join("|");
            let gpuSampler = this.samplerCache.get(key);
            if (!gpuSampler) {
                const minLinear = sampler.minFilter === GL.LINEAR ||
                    sampler.minFilter === GL.LINEAR_MIPMAP_NEAREST ||
                    sampler.minFilter === GL.LINEAR_MIPMAP_LINEAR;
                const mipLinear = sampler.minFilter === GL.NEAREST_MIPMAP_LINEAR ||
                    sampler.minFilter === GL.LINEAR_MIPMAP_LINEAR;
                const magLinear = sampler.magFilter === GL.LINEAR;
                // WebGPU rejects anisotropy unless all three filters are
                // linear, so it is clamped rather than the sampler refused.
                const anisotropy = (minLinear && magLinear && mipLinear) ?
                    sampler.maxAnisotropy : 1;
                gpuSampler = this.device.createSampler({
                    addressModeU: ADDRESS_MODES[sampler.wrapS] || "repeat",
                    addressModeV: ADDRESS_MODES[sampler.wrapT] || "repeat",
                    addressModeW: ADDRESS_MODES[sampler.wrapR] || "repeat",
                    magFilter: magLinear ? "linear" : "nearest",
                    minFilter: minLinear ? "linear" : "nearest",
                    mipmapFilter: mipLinear ? "linear" : "nearest",
                    lodMinClamp: Math.max(0, sampler.minLod),
                    lodMaxClamp: Math.min(32, sampler.maxLod),
                    maxAnisotropy: anisotropy,
                    ...(shadow ? {
                        compare: COMPARE_FUNCTIONS[sampler.compareFunc] ||
                            "less-equal",
                    } : {}),
                });
                this.samplerCache.set(key, gpuSampler);
            }
            return gpuSampler;
        },
    });

    function samplerTargetEnum(dim) {
        switch (dim) {
        case "1D": return GL.TEXTURE_1D;
        case "3D": return GL.TEXTURE_3D;
        case "Cube": return GL.TEXTURE_CUBE_MAP;
        default: return GL.TEXTURE_2D;
        }
    }

    function isIdentity(m) {
        for (let i = 0; i < 16; ++i) {
            const expected = (i % 5 === 0) ? 1 : 0;
            if (m[i] !== expected) return false;
        }
        return true;
    }

    function stencilKey(s) {
        const f = s.stencil.front, b = s.stencil.back;
        return [f.func, f.valueMask, f.writeMask, f.fail, f.zfail, f.zpass,
                b.func, b.valueMask, b.writeMask, b.fail, b.zfail, b.zpass].join(",");
    }

    function blendKey(s) {
        const b = s.blend;
        return [b.srcRGB, b.dstRGB, b.srcAlpha, b.dstAlpha,
                b.equationRGB, b.equationAlpha].join(",");
    }

    function polygonOffsetKey(s) {
        return s.enabled.has(GL.POLYGON_OFFSET_FILL) ?
            s.polygonOffset.factor + ":" + s.polygonOffset.units : "-";
    }

    function gpuBlendState(s) {
        const b = s.blend;
        return {
            color: {
                srcFactor: BLEND_FACTORS[b.srcRGB] || "one",
                dstFactor: BLEND_FACTORS[b.dstRGB] || "zero",
                operation: BLEND_EQUATIONS[b.equationRGB] || "add",
            },
            alpha: {
                srcFactor: BLEND_FACTORS[b.srcAlpha] || "one",
                dstFactor: BLEND_FACTORS[b.dstAlpha] || "zero",
                operation: BLEND_EQUATIONS[b.equationAlpha] || "add",
            },
        };
    }

    /*
     * WebGPU has one stencil read mask and one write mask for both faces; GL
     * has a pair each. When they differ the front face's win, and the
     * divergence is reported once -- deviation D-02.
     */
    function gpuStencilState(s) {
        const f = s.stencil.front, b = s.stencil.back;
        const face = state => ({
            compare: COMPARE_FUNCTIONS[state.func] || "always",
            failOp: STENCIL_OPERATIONS[state.fail] || "keep",
            depthFailOp: STENCIL_OPERATIONS[state.zfail] || "keep",
            passOp: STENCIL_OPERATIONS[state.zpass] || "keep",
        });
        return {
            stencilFront: face(f),
            stencilBack: face(b),
            stencilReadMask: f.valueMask >>> 0,
            stencilWriteMask: f.writeMask >>> 0,
        };
    }

    function stencilMasksDiverge(s) {
        const f = s.stencil.front, b = s.stencil.back;
        return f.valueMask !== b.valueMask || f.writeMask !== b.writeMask;
    }

    /* ================================================================== */
    /* Executor: uniform upload                                           */
    /* ================================================================== */

    Object.assign(GLWebGPUExecutor.prototype, {

        allocateUniform(byteCount) {
            // WebGPU requires a uniform binding offset to be 256-aligned.
            const size = alignUp(Math.max(16, byteCount), 256);
            if (size > this.uniformCapacity) {
                this.refuse("draw", "uniform block exceeds the ring capacity",
                    { size }, GL.OUT_OF_MEMORY);
                return null;
            }
            if (this.uniformCursor + size > this.uniformCapacity) {
                this.flushFrame();
                this.uniformCursor = 0;
            }
            const offset = this.uniformCursor;
            this.uniformCursor = offset + size;
            return { offset, size };
        },

        /*
         * The GL state block. Only the fields the shader actually reads are
         * uploaded, so a 2D blit costs sixteen floats rather than the seven
         * hundred the full table would.
         */
        writeStateUniforms(layout, shaders) {
            const bytes = layout.floats * 4;
            const slice = this.allocateUniform(bytes);
            if (!slice) return null;
            const floats = new Float32Array(this.uniformStaging.buffer,
                slice.offset, layout.floats);
            const snapshot = this.stateSnapshot(shaders);
            stateLayout.writeLayout(layout, snapshot, floats, 0);
            this.device.queue.writeBuffer(this.uniformRing, slice.offset,
                this.uniformStaging, slice.offset, slice.size);
            return slice;
        },

        writeEmptyUniforms() {
            const slice = this.allocateUniform(16);
            if (!slice) return null;
            const zeros = new Float32Array(this.uniformStaging.buffer,
                slice.offset, 4);
            zeros.fill(0);
            this.device.queue.writeBuffer(this.uniformRing, slice.offset,
                this.uniformStaging, slice.offset, slice.size);
            return slice;
        },

        /*
         * env then local for one program, in the shape its generated shader
         * declares. Each stage gets its own block: program.env and
         * program.local are per-program namespaces, so a bound vertex and
         * fragment program hold different values at the same index, and
         * folding them into one block left the vertex stage reading the
         * fragment program's parameters -- silently, since nothing about the
         * draw is invalid.
         */
        writeARBProgramParameters(program) {
            const count = arbProgram.MAX_PROGRAM_PARAMETERS * 4;
            const slice = this.allocateUniform(count * 4 * 2);
            if (!slice) return null;
            const floats = new Float32Array(this.uniformStaging.buffer,
                slice.offset, count * 2);
            floats.set(program.env.subarray(0, count), 0);
            floats.set(program.local.subarray(0, count), count);
            this.device.queue.writeBuffer(this.uniformRing, slice.offset,
                this.uniformStaging, slice.offset, slice.size);
            return slice;
        },

        /* One slice per stage that declared a parameter block, keyed by the
         * binding its shader reads. */
        writeARBParameters(shaders) {
            const slices = {};
            const stages = [["vertex", shaders.arbVertex, shaders.wgslVertex],
                            ["fragment", shaders.arbFragment, shaders.wgslFragment]];
            for (const [stage, program, wgsl] of stages) {
                const binding = arbProgram.PARAMETER_BINDING[stage];
                if (wgsl.indexOf("@group(1) @binding(" + binding + ")") < 0)
                    continue;
                if (!program || !program.env || !program.local)
                    return this.refuse("draw", "ARB parameter storage is missing",
                        { stage }, GL.INVALID_OPERATION);
                const slice = this.writeARBProgramParameters(program);
                if (!slice) return null;
                slices[binding] = slice;
            }
            return slices;
        },

        writeProgramUniforms(shaders) {
            const program = shaders.program;
            const bytes = Math.max(16, shaders.uniformBytes);
            const slice = this.allocateUniform(bytes);
            if (!slice) return null;
            this.uniformStaging.set(
                program.uniformData.subarray(0, Math.min(bytes,
                    program.uniformData.byteLength)), slice.offset);
            this.device.queue.writeBuffer(this.uniformRing, slice.offset,
                this.uniformStaging, slice.offset, slice.size);
            return slice;
        },

        /*
         * Builds the plain-object view of GL state that gl_state_layout.js
         * writes from. The derived quantities -- the scene colour, the light
         * products, each light's half vector -- are computed here rather than
         * in the shader, because they depend only on state the CPU already has
         * and would otherwise be recomputed per vertex.
         */
        stateSnapshot(shaders) {
            const s = this.current;
            const snapshot = this.snapshot || (this.snapshot = {
                matrices: {
                    modelview: new Float32Array(16),
                    projection: new Float32Array(16),
                    mvp: new Float32Array(16),
                    normal: new Float32Array(9),
                    modelviewInverse: new Float32Array(16),
                    modelviewTranspose: new Float32Array(16),
                    modelviewInverseTranspose: new Float32Array(16),
                    projectionInverse: new Float32Array(16),
                    projectionTranspose: new Float32Array(16),
                    projectionInverseTranspose: new Float32Array(16),
                    mvpInverse: new Float32Array(16),
                    mvpTranspose: new Float32Array(16),
                    mvpInverseTranspose: new Float32Array(16),
                    texture: Array.from({ length: MAX_TEXTURE_COORDS },
                        () => new Float32Array(16)),
                    textureInverse: Array.from({ length: MAX_TEXTURE_COORDS },
                        () => new Float32Array(16)),
                    textureTranspose: Array.from({ length: MAX_TEXTURE_COORDS },
                        () => new Float32Array(16)),
                    textureInverseTranspose: Array.from(
                        { length: MAX_TEXTURE_COORDS }, () => new Float32Array(16)),
                },
                lights: Array.from({ length: MAX_LIGHTS }, () => ({
                    ambient: new Float32Array(4), diffuse: new Float32Array(4),
                    specular: new Float32Array(4), position: new Float32Array(4),
                    halfVector: new Float32Array(4),
                    spotDirection: new Float32Array(3),
                    spotExponent: 0, spotCutoff: 180, spotCosCutoff: -1,
                    constantAttenuation: 1, linearAttenuation: 0,
                    quadraticAttenuation: 0,
                })),
                material: { front: null, back: null },
                lightModel: { ambient: new Float32Array(4) },
                derived: {
                    frontSceneColor: new Float32Array(4),
                    backSceneColor: new Float32Array(4),
                    frontLightProduct: Array.from({ length: MAX_LIGHTS }, () => ({
                        ambient: new Float32Array(4),
                        diffuse: new Float32Array(4),
                        specular: new Float32Array(4),
                    })),
                    backLightProduct: Array.from({ length: MAX_LIGHTS }, () => ({
                        ambient: new Float32Array(4),
                        diffuse: new Float32Array(4),
                        specular: new Float32Array(4),
                    })),
                },
                fog: { color: new Float32Array(4), density: 1, start: 0, end: 1 },
                texEnvColor: Array.from({ length: MAX_TEXTURE_UNITS },
                    () => new Float32Array(4)),
                texGen: Array.from({ length: MAX_TEXTURE_UNITS }, () =>
                    Array.from({ length: 4 }, () => ({
                        objectPlane: new Float32Array(4),
                        eyePlane: new Float32Array(4),
                    }))),
                clipPlanes: Array.from({ length: MAX_CLIP_PLANES },
                    () => new Float32Array(4)),
                depthRange: { near: 0, far: 1 },
                point: { size: 1, sizeMin: 0, sizeMax: 64, fadeThreshold: 1,
                         attenuation: new Float32Array(3) },
                alphaRef: 0,
                viewport: { x: 0, y: 0, width: 1, height: 1 },
                lineWidth: 1,
                lineStipple: { pattern: 0xffff, factor: 1 },
                polygonStippleEnabled: false,
                polygonStipple: new Uint8Array(128),
            });

            const modelview = this.topOf(GL.MODELVIEW);
            const projection = this.topOf(GL.PROJECTION);
            snapshot.matrices.modelview.set(modelview);
            snapshot.matrices.projection.set(projection);
            multiply4(projection, modelview, snapshot.matrices.mvp);
            normalMatrixOf(modelview, snapshot.matrices.normal);

            // The inverse and transpose variants are only produced when a
            // shader named one; they are four 4x4 inversions each otherwise.
            const wants = new Set(shaders.stateFields);
            if (wants.has("modelviewInverse") || wants.has("modelviewInverseTranspose")) {
                invert4(modelview, snapshot.matrices.modelviewInverse);
                if (wants.has("modelviewInverseTranspose"))
                    transpose4(snapshot.matrices.modelviewInverse,
                        snapshot.matrices.modelviewInverseTranspose);
            }
            if (wants.has("modelviewTranspose"))
                transpose4(modelview, snapshot.matrices.modelviewTranspose);
            if (wants.has("projectionInverse") || wants.has("projectionInverseTranspose")) {
                invert4(projection, snapshot.matrices.projectionInverse);
                if (wants.has("projectionInverseTranspose"))
                    transpose4(snapshot.matrices.projectionInverse,
                        snapshot.matrices.projectionInverseTranspose);
            }
            if (wants.has("projectionTranspose"))
                transpose4(projection, snapshot.matrices.projectionTranspose);
            if (wants.has("mvpInverse") || wants.has("mvpInverseTranspose")) {
                invert4(snapshot.matrices.mvp, snapshot.matrices.mvpInverse);
                if (wants.has("mvpInverseTranspose"))
                    transpose4(snapshot.matrices.mvpInverse,
                        snapshot.matrices.mvpInverseTranspose);
            }
            if (wants.has("mvpTranspose"))
                transpose4(snapshot.matrices.mvp, snapshot.matrices.mvpTranspose);

            for (let i = 0; i < MAX_TEXTURE_COORDS; ++i) {
                const m = this.textureMatrixOf(i);
                snapshot.matrices.texture[i].set(m);
                if (wants.has("textureMatrixInverse") ||
                        wants.has("textureMatrixInverseTranspose")) {
                    invert4(m, snapshot.matrices.textureInverse[i]);
                    if (wants.has("textureMatrixInverseTranspose"))
                        transpose4(snapshot.matrices.textureInverse[i],
                            snapshot.matrices.textureInverseTranspose[i]);
                }
                if (wants.has("textureMatrixTranspose"))
                    transpose4(m, snapshot.matrices.textureTranspose[i]);
            }

            for (let i = 0; i < MAX_LIGHTS; ++i) {
                const light = s.lights[i];
                const out = snapshot.lights[i];
                out.ambient.set(light.ambient);
                out.diffuse.set(light.diffuse);
                out.specular.set(light.specular);
                out.position.set(light.eyePosition);
                out.spotDirection.set(light.eyeSpotDirection);
                out.spotExponent = light.spotExponent;
                out.spotCutoff = light.spotCutoff;
                out.spotCosCutoff = light.spotCutoff === 180 ? -1 :
                    Math.cos(light.spotCutoff * Math.PI / 180);
                out.constantAttenuation = light.constantAttenuation;
                out.linearAttenuation = light.linearAttenuation;
                out.quadraticAttenuation = light.quadraticAttenuation;
                // gl_LightSource[i].halfVector, for the infinite-viewer case
                // the GL spec defines it in: normalize(normalize(P) + (0,0,1)).
                const px = light.eyePosition[0], py = light.eyePosition[1];
                const pz = light.eyePosition[2];
                const len = Math.sqrt(px * px + py * py + pz * pz) || 1;
                const hx = px / len, hy = py / len, hz = pz / len + 1;
                const hlen = Math.sqrt(hx * hx + hy * hy + hz * hz) || 1;
                out.halfVector[0] = hx / hlen;
                out.halfVector[1] = hy / hlen;
                out.halfVector[2] = hz / hlen;
                out.halfVector[3] = 0;
            }

            snapshot.material.front = s.material.front;
            snapshot.material.back = s.material.back;
            snapshot.lightModel.ambient.set(s.lightModel.ambient);

            for (const [face, key] of [["front", "frontSceneColor"],
                    ["back", "backSceneColor"]]) {
                const material = s.material[face];
                const out = snapshot.derived[key];
                for (let c = 0; c < 3; ++c)
                    out[c] = material.emission[c] +
                        material.ambient[c] * s.lightModel.ambient[c];
                out[3] = material.diffuse[3];
            }
            for (const [face, key] of [["front", "frontLightProduct"],
                    ["back", "backLightProduct"]]) {
                const material = s.material[face];
                for (let i = 0; i < MAX_LIGHTS; ++i) {
                    const light = s.lights[i];
                    const product = snapshot.derived[key][i];
                    for (let c = 0; c < 4; ++c) {
                        product.ambient[c] = material.ambient[c] * light.ambient[c];
                        product.diffuse[c] = material.diffuse[c] * light.diffuse[c];
                        product.specular[c] = material.specular[c] * light.specular[c];
                    }
                }
            }

            snapshot.fog.color.set(s.fog.color);
            snapshot.fog.density = s.fog.density;
            snapshot.fog.start = s.fog.start;
            snapshot.fog.end = s.fog.end;

            for (let i = 0; i < MAX_TEXTURE_UNITS; ++i) {
                snapshot.texEnvColor[i].set(s.textureUnits[i].env.color);
                for (let c = 0; c < 4; ++c) {
                    snapshot.texGen[i][c].objectPlane.set(
                        s.textureUnits[i].texGen[c].objectPlane);
                    snapshot.texGen[i][c].eyePlane.set(
                        s.textureUnits[i].texGen[c].eyePlane);
                }
            }
            for (let i = 0; i < MAX_CLIP_PLANES; ++i)
                snapshot.clipPlanes[i].set(s.clipPlanes[i]);

            snapshot.depthRange.near = s.depthRange.near;
            snapshot.depthRange.far = s.depthRange.far;
            snapshot.point.size = s.point.size;
            snapshot.point.sizeMin = s.point.sizeMin;
            snapshot.point.sizeMax = s.point.sizeMax;
            snapshot.point.fadeThreshold = s.point.fadeThreshold;
            snapshot.point.attenuation.set(s.point.attenuation);
            snapshot.alphaRef = s.alphaFunc.ref;
            snapshot.viewport.x = s.viewport.x;
            snapshot.viewport.y = s.viewport.y;
            snapshot.viewport.width = Math.max(1, s.viewport.width);
            snapshot.viewport.height = Math.max(1, s.viewport.height);
            snapshot.lineWidth = s.lineWidth;
            snapshot.lineStipple.pattern = s.lineStipple.pattern;
            snapshot.lineStipple.factor = s.lineStipple.factor;
            snapshot.polygonStippleEnabled = s.enabled.has(GL.POLYGON_STIPPLE);
            snapshot.polygonStipple.set(s.polygonStipple);
            return snapshot;
        },
    });

    /* ================================================================== */
    /* Executor: present, framebuffers, readback, queries                 */
    /* ================================================================== */

    const PRESENT_WGSL = `
struct VSOut {
    @builtin(position) position : vec4<f32>,
    @location(0) uv : vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> VSOut {
    // One oversized triangle covers the target with no vertex buffer.
    var out : VSOut;
    let x = f32((index << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(index & 2u) * 2.0 - 1.0;
    out.position = vec4<f32>(x, y, 0.0, 1.0);
    // The back buffer's row 0 is GL's bottom row (plan 4.3), so presenting it
    // is where the image is turned the right way up -- once, here.
    out.uv = vec2<f32>(x * 0.5 + 0.5, y * 0.5 + 0.5);
    return out;
}

@group(0) @binding(0) var source : texture_2d<f32>;
@group(0) @binding(1) var sourceSampler : sampler;

@fragment
fn fs_main(inp : VSOut) -> @location(0) vec4<f32> {
    return textureSample(source, sourceSampler, inp.uv);
}
`;

    const COLOR_COPY_WGSL = `
struct CopyParams {
    sourceOrigin : vec2<i32>,
    destinationOrigin : vec2<i32>,
}
@group(0) @binding(0) var sourceTexture : texture_2d<f32>;
@group(0) @binding(1) var<uniform> params : CopyParams;
@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}
@fragment fn fs_main(@builtin(position) p : vec4<f32>) -> @location(0) vec4<f32> {
    let source = params.sourceOrigin + vec2<i32>(p.xy) - params.destinationOrigin;
    return textureLoad(sourceTexture, source, 0);
}
`;

    const MIPMAP_WGSL = `
@group(0) @binding(0) var sourceTexture : texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}
@fragment fn fs_main(@builtin(position) p : vec4<f32>) -> @location(0) vec4<f32> {
    let dimensions = vec2<i32>(textureDimensions(sourceTexture));
    let maximum = dimensions - vec2<i32>(1);
    let origin = vec2<i32>(p.xy) * 2;
    let a = textureLoad(sourceTexture, min(origin, maximum), 0);
    let b = textureLoad(sourceTexture,
        min(origin + vec2<i32>(1, 0), maximum), 0);
    let c = textureLoad(sourceTexture,
        min(origin + vec2<i32>(0, 1), maximum), 0);
    let d = textureLoad(sourceTexture,
        min(origin + vec2<i32>(1, 1), maximum), 0);
    return (a + b + c + d) * 0.25;
}
`;

    const COLOR_BLIT_WGSL = `
struct BlitParams {
    sourceOrigin : vec2<f32>,
    sourceExtent : vec2<f32>,
    destinationOrigin : vec2<f32>,
    destinationExtent : vec2<f32>,
}
@group(0) @binding(0) var sourceTexture : texture_2d<f32>;
@group(0) @binding(1) var sourceSampler : sampler;
@group(0) @binding(2) var<uniform> params : BlitParams;
@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}
@fragment fn fs_main(@builtin(position) p : vec4<f32>) -> @location(0) vec4<f32> {
    let relative = (p.xy - params.destinationOrigin) / params.destinationExtent;
    let sourcePosition = params.sourceOrigin + relative * params.sourceExtent;
    let dimensions = vec2<f32>(textureDimensions(sourceTexture));
    return textureSampleLevel(sourceTexture, sourceSampler,
        sourcePosition / dimensions, 0.0);
}
`;

    Object.assign(GLWebGPUExecutor.prototype, {

        colorCopyPipeline(format) {
            this.colorCopyPipelines = this.colorCopyPipelines || new Map();
            let pipeline = this.colorCopyPipelines.get(format);
            if (pipeline) return pipeline;
            const module = this.shaderModule(COLOR_COPY_WGSL);
            try {
                pipeline = this.device.createRenderPipeline({
                    label: "GL colour format copy", layout: "auto",
                    vertex: { module, entryPoint: "vs_main" },
                    fragment: { module, entryPoint: "fs_main",
                                targets: [{ format }] },
                    primitive: { topology: "triangle-list" },
                });
            } catch (error) {
                this.refuse("glCopyTexImage", "copy target is not renderable",
                    { format, message: String(error) }, GL.INVALID_OPERATION);
                return null;
            }
            this.colorCopyPipelines.set(format, pipeline);
            return pipeline;
        },

        mipmapPipeline(format) {
            this.mipmapPipelines = this.mipmapPipelines || new Map();
            let pipeline = this.mipmapPipelines.get(format);
            if (pipeline) return pipeline;
            const module = this.shaderModule(MIPMAP_WGSL);
            try {
                pipeline = this.device.createRenderPipeline({
                    label: "GL mipmap generator", layout: "auto",
                    vertex: { module, entryPoint: "vs_main" },
                    fragment: { module, entryPoint: "fs_main",
                                targets: [{ format }] },
                    primitive: { topology: "triangle-list" },
                });
            } catch (error) {
                this.refuse("glGenerateMipmap", "texture format is not renderable",
                    { format, message: String(error) }, GL.INVALID_OPERATION);
                return null;
            }
            this.mipmapPipelines.set(format, pipeline);
            return pipeline;
        },

        colorBlitPipeline(format) {
            this.colorBlitPipelines = this.colorBlitPipelines || new Map();
            let pipeline = this.colorBlitPipelines.get(format);
            if (pipeline) return pipeline;
            const module = this.shaderModule(COLOR_BLIT_WGSL);
            try {
                pipeline = this.device.createRenderPipeline({
                    label: "GL colour blit", layout: "auto",
                    vertex: { module, entryPoint: "vs_main" },
                    fragment: { module, entryPoint: "fs_main",
                                targets: [{ format }] },
                    primitive: { topology: "triangle-list" },
                });
            } catch (error) {
                this.refuse("glBlitFramebuffer", "blit target is not renderable",
                    { format, message: String(error) }, GL.INVALID_OPERATION);
                return null;
            }
            this.colorBlitPipelines.set(format, pipeline);
            return pipeline;
        },

        blitColorTexture(source, destination, sx0, sy0, sx1, sy1,
                dx0, dy0, dx1, dy1, filter) {
            const pipeline = this.colorBlitPipeline(destination.format);
            if (!pipeline) return false;
            const uniform = this.allocateUniform(32);
            if (!uniform) return false;
            new Float32Array(this.uniformStaging.buffer, uniform.offset, 8).set([
                sx0, sy0, sx1 - sx0, sy1 - sy0,
                dx0, dy0, dx1 - dx0, dy1 - dy0,
            ]);
            this.device.queue.writeBuffer(this.uniformRing, uniform.offset,
                this.uniformStaging, uniform.offset, uniform.size);
            const sampler = this.device.createSampler({
                minFilter: filter === GL.LINEAR ? "linear" : "nearest",
                magFilter: filter === GL.LINEAR ? "linear" : "nearest",
            });
            const sourceView = source.view || source.texture.createView({
                dimension: "2d", baseMipLevel: source.mipLevel || 0,
                mipLevelCount: 1, baseArrayLayer: source.layer || 0,
                arrayLayerCount: 1,
            });
            const left = Math.min(dx0, dx1), top = Math.min(dy0, dy1);
            const width = Math.abs(dx1 - dx0), height = Math.abs(dy1 - dy0);
            this.endPass();
            const pass = this.ensureEncoder().beginRenderPass({
                label: "GL scaled colour blit",
                colorAttachments: [{ view: destination.view,
                    loadOp: "load", storeOp: "store" }],
            });
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, this.device.createBindGroup({
                layout: pipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: sourceView },
                    { binding: 1, resource: sampler },
                    { binding: 2, resource: { buffer: this.uniformRing,
                        offset: uniform.offset, size: uniform.size } },
                ],
            }));
            pass.setViewport(left, top, width, height, 0, 1);
            pass.setScissorRect(left, top, width, height);
            pass.draw(3);
            pass.end();
            return true;
        },

        generateTextureMipmapsGPU(texture) {
            const base = texture.levels[0] && texture.levels[0][0];
            if (!base) return false;
            if (texture.kind === "3D" || base.compressed ||
                    base.base === "DEPTH" || base.base === "COLOR_INDEX") {
                for (const list of texture.levels)
                    if (list) list.length = Math.min(1, list.length);
                return this.refuse("glGenerateMipmap",
                    "GPU-authored mipmaps are unsupported for this texture type",
                    { kind: texture.kind, format: base.gpuFormat },
                    GL.INVALID_OPERATION);
            }
            const pipeline = this.mipmapPipeline(base.gpuFormat);
            if (!pipeline) return false;
            const gpuTexture = this.ensureTextureUploaded(texture);
            if (!gpuTexture) return false;
            const layers = texture.kind === "Cube" ? 6 : 1;
            this.endPass();
            for (let layer = 0; layer < layers; ++layer) {
                const list = texture.levels[layer] || [];
                for (let level = 1; level < list.length; ++level) {
                    const sourceView = gpuTexture.createView({
                        dimension: "2d", baseMipLevel: level - 1,
                        mipLevelCount: 1, baseArrayLayer: layer,
                        arrayLayerCount: 1,
                    });
                    const destinationView = gpuTexture.createView({
                        dimension: "2d", baseMipLevel: level,
                        mipLevelCount: 1, baseArrayLayer: layer,
                        arrayLayerCount: 1,
                    });
                    const pass = this.ensureEncoder().beginRenderPass({
                        label: "GL mipmap level " + level,
                        colorAttachments: [{ view: destinationView,
                            loadOp: "clear", storeOp: "store",
                            clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
                    });
                    pass.setPipeline(pipeline);
                    pass.setBindGroup(0, this.device.createBindGroup({
                        layout: pipeline.getBindGroupLayout(0),
                        entries: [{ binding: 0, resource: sourceView }],
                    }));
                    pass.draw(3);
                    pass.end();
                }
            }
            texture.dirty = false;
            return true;
        },

        copyColorTexture(source, destination, sourceX, sourceY,
                destinationX, destinationY, width, height, loadOp) {
            if (!source || !destination || width <= 0 || height <= 0) return false;
            const pipeline = this.colorCopyPipeline(destination.format);
            if (!pipeline) return false;
            const uniform = this.allocateUniform(16);
            if (!uniform) return false;
            const values = new Int32Array(this.uniformStaging.buffer,
                uniform.offset, 4);
            values.set([sourceX, sourceY, destinationX, destinationY]);
            this.device.queue.writeBuffer(this.uniformRing, uniform.offset,
                this.uniformStaging, uniform.offset, uniform.size);
            const sourceView = source.view || source.texture.createView({
                dimension: "2d", baseMipLevel: source.mipLevel || 0,
                mipLevelCount: 1, baseArrayLayer: source.layer || 0,
                arrayLayerCount: 1,
            });
            this.endPass();
            const pass = this.ensureEncoder().beginRenderPass({
                label: "GL colour copy pass",
                colorAttachments: [{ view: destination.view,
                    loadOp: loadOp || "load", storeOp: "store",
                    clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
            });
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, this.device.createBindGroup({
                layout: pipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: sourceView },
                    { binding: 1, resource: { buffer: this.uniformRing,
                        offset: uniform.offset, size: uniform.size } },
                ],
            }));
            pass.setViewport(destinationX, destinationY, width, height, 0, 1);
            pass.setScissorRect(destinationX, destinationY, width, height);
            pass.draw(3);
            pass.end();
            return true;
        },

        presentToCanvas() {
            if (!this.host || !this.host.context) return false;
            if (!this.presenterToken)
                this.presenterToken = this.host.claimPresenter("opengl");
            if (!this.host.canPresent(this.presenterToken)) return false;
            if (!this.backBuffer) return false;
            let swapTexture;
            try {
                swapTexture = this.host.context.getCurrentTexture();
            } catch (error) {
                this.warnOnce("present", "could not acquire the swap-chain texture",
                    { message: String(error) });
                return false;
            }
            if (!this.presentPipeline) {
                const module = this.shaderModule(PRESENT_WGSL);
                this.presentPipeline = this.device.createRenderPipeline({
                    label: "GL present",
                    layout: "auto",
                    vertex: { module, entryPoint: "vs_main" },
                    fragment: { module, entryPoint: "fs_main",
                                targets: [{ format: this.host.format }] },
                    primitive: { topology: "triangle-list" },
                });
                this.presentSampler = this.device.createSampler({
                    magFilter: "linear", minFilter: "linear",
                });
            }
            const encoder = this.ensureEncoder();
            const pass = encoder.beginRenderPass({
                label: "GL present pass",
                colorAttachments: [{
                    view: swapTexture.createView(),
                    loadOp: "clear",
                    clearValue: { r: 0, g: 0, b: 0, a: 1 },
                    storeOp: "store",
                }],
            });
            pass.setPipeline(this.presentPipeline);
            pass.setBindGroup(0, this.device.createBindGroup({
                layout: this.presentPipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: this.backBufferView },
                    { binding: 1, resource: this.presentSampler },
                ],
            }));
            pass.draw(3);
            pass.end();
            return true;
        },

        onSwapBuffers() {
            this.finishFrame(true);
        },

        /* ---- framebuffer objects ---- */

        framebufferTexture(target, attachment, textarget, textureName, level, layer) {
            const s = this.current;
            const name = target === GL.READ_FRAMEBUFFER ?
                s.readFramebuffer : s.drawFramebuffer;
            const fbo = this.framebuffers.get(s.id).get(name);
            if (!fbo)
                return this.refuse("glFramebufferTexture2D",
                    "no framebuffer object is bound", { target },
                    GL.INVALID_OPERATION);
            const face = CUBE_FACE_INDEX[textarget];
            const record = textureName ? {
                kind: "texture", name: textureName, level: level | 0,
                layer: face !== undefined ? face : (layer | 0),
            } : null;
            this.attachTo(fbo, attachment, record);
            this.endPass();
        },

        framebufferRenderbuffer(target, attachment, rbTarget, rbName) {
            const s = this.current;
            const name = target === GL.READ_FRAMEBUFFER ?
                s.readFramebuffer : s.drawFramebuffer;
            const fbo = this.framebuffers.get(s.id).get(name);
            if (!fbo)
                return this.refuse("glFramebufferRenderbuffer",
                    "no framebuffer object is bound", { target },
                    GL.INVALID_OPERATION);
            void rbTarget;
            this.attachTo(fbo, attachment,
                rbName ? { kind: "renderbuffer", name: rbName, level: 0, layer: 0 } :
                    null);
            this.endPass();
        },

        attachTo(fbo, attachment, record) {
            if (attachment === GL.DEPTH_ATTACHMENT) fbo.depth = record;
            else if (attachment === GL.STENCIL_ATTACHMENT) fbo.stencil = record;
            else if (attachment === GL.DEPTH_STENCIL_ATTACHMENT) {
                fbo.depth = record;
                fbo.stencil = record;
            } else {
                const index = attachment - GL.COLOR_ATTACHMENT0;
                if (index < 0 || index >= MAX_DRAW_BUFFERS)
                    return this.refuse("glFramebufferTexture2D",
                        "attachment out of range", { attachment },
                        GL.INVALID_ENUM);
                fbo.color[index] = record;
            }
            const size = this.attachmentSize(record);
            if (size) { fbo.width = size.width; fbo.height = size.height; }
        },

        attachmentSize(record) {
            if (!record) return null;
            if (record.kind === "texture") {
                const texture = this.current.shareGroup.textures.get(record.name);
                const slot = texture && texture.levels[record.layer] &&
                    texture.levels[record.layer][record.level];
                return slot ? { width: slot.width, height: slot.height } : null;
            }
            const rb = this.current.shareGroup.renderbuffers.get(record.name);
            return rb ? { width: rb.width, height: rb.height } : null;
        },

        attachmentFormat(record) {
            if (!record) return null;
            if (record.kind === "texture") {
                const texture = this.current.shareGroup.textures.get(record.name);
                const slot = texture && texture.levels[record.layer] &&
                    texture.levels[record.layer][record.level];
                return slot ? slot.gpuFormat : null;
            }
            const rb = this.current.shareGroup.renderbuffers.get(record.name);
            return rb ? rb.gpuFormat : null;
        },

        renderbufferStorage(internalFormat, width, height) {
            const s = this.current;
            const rb = s.shareGroup.renderbuffers.get(s.renderbuffer);
            if (!rb)
                return this.refuse("glRenderbufferStorage",
                    "no renderbuffer is bound", {}, GL.INVALID_OPERATION);
            const storage = storageFormatFor(internalFormat, this.deviceFeatures);
            rb.width = Math.max(1, width);
            rb.height = Math.max(1, height);
            rb.internalFormat = internalFormat;
            // WebGPU has no renderbuffer: a texture with RENDER_ATTACHMENT is
            // the same thing with a different name.
            const stencilFormat = internalFormat === GL.STENCIL_INDEX8_EXT ||
                internalFormat === GL.DEPTH24_STENCIL8;
            rb.gpuFormat = stencilFormat ? "depth24plus-stencil8" : storage.gpu;
            this.retire(rb.gpuTexture);
            rb.gpuTexture = this.device.createTexture({
                label: "GL renderbuffer " + rb.name,
                size: { width: rb.width, height: rb.height, depthOrArrayLayers: 1 },
                format: rb.gpuFormat,
                usage: TEXTURE_USAGE_RENDER_ATTACHMENT | TEXTURE_USAGE_COPY_SRC |
                    TEXTURE_USAGE_TEXTURE_BINDING,
            });
        },

        /*
         * GL's completeness rules, answered from the executor's own state --
         * no GPU round trip, so the guest's blocking call returns immediately.
         */
        checkFramebufferStatus(target) {
            const s = this.current;
            const name = target === GL.READ_FRAMEBUFFER ?
                s.readFramebuffer : s.drawFramebuffer;
            if (!name) return GL.FRAMEBUFFER_COMPLETE;
            const fbo = this.framebuffers.get(s.id).get(name);
            if (!fbo) return GL.FRAMEBUFFER_UNDEFINED || 0x8219;
            let width = 0, height = 0, any = false;
            const check = record => {
                if (!record) return true;
                const size = this.attachmentSize(record);
                if (!size || !size.width || !size.height) return false;
                if (!any) { width = size.width; height = size.height; any = true; }
                else if (size.width !== width || size.height !== height)
                    return false;
                return true;
            };
            for (const attachment of fbo.color)
                if (!check(attachment)) return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            if (!check(fbo.depth))
                return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            if (!check(fbo.stencil))
                return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            if (!any) return GL.FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;

            for (const attachment of fbo.color) {
                const format = this.attachmentFormat(attachment);
                if (format && format.indexOf("depth") === 0)
                    return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            const depthFormat = this.attachmentFormat(fbo.depth);
            if (depthFormat && depthFormat.indexOf("depth") !== 0)
                return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            const stencilFormat = this.attachmentFormat(fbo.stencil);
            if (stencilFormat && stencilFormat.indexOf("stencil") < 0)
                return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;

            if (fbo.depth && fbo.stencil &&
                    (fbo.depth.kind !== fbo.stencil.kind ||
                     fbo.depth.name !== fbo.stencil.name ||
                     fbo.depth.level !== fbo.stencil.level ||
                     fbo.depth.layer !== fbo.stencil.layer))
                return GL.FRAMEBUFFER_UNSUPPORTED_EXT;

            if (target !== GL.READ_FRAMEBUFFER) {
                for (const buffer of s.drawBuffers) {
                    if (buffer === GL.NONE) continue;
                    const index = buffer >= GL.COLOR_ATTACHMENT0 ?
                        buffer - GL.COLOR_ATTACHMENT0 : 0;
                    if (!fbo.color[index])
                        return GL.FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT;
                }
            }
            if (target !== GL.DRAW_FRAMEBUFFER && s.readBuffer !== GL.NONE) {
                const index = s.readBuffer >= GL.COLOR_ATTACHMENT0 ?
                    s.readBuffer - GL.COLOR_ATTACHMENT0 : 0;
                if (!fbo.color[index])
                    return GL.FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT;
            }
            fbo.width = width;
            fbo.height = height;
            return GL.FRAMEBUFFER_COMPLETE;
        },

        blitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter) {
            // A same-size, same-format colour blit is a texture copy. Scaling,
            // axis reversal, or format conversion is implemented by a draw.
            // Depth/stencil copies require aspect-typed pipelines and are
            // rejected explicitly instead of producing a WebGPU validation
            // error that invalidates the whole command buffer.
            if (!(mask & GL.COLOR_BUFFER_BIT) ||
                    (mask & ~GL.COLOR_BUFFER_BIT) !== 0)
                return this.refuse("glBlitFramebuffer",
                    "only colour blits are implemented", { mask }, 0);
            const width = Math.abs(sx1 - sx0), height = Math.abs(sy1 - sy0);
            const dstWidth = Math.abs(dx1 - dx0), dstHeight = Math.abs(dy1 - dy0);
            const source = this.readAttachmentView();
            const target = this.currentTargets();
            if (!source || !target || !target.color[0]) return;
            const destination = target.color[0];
            const straight = sx1 >= sx0 && sy1 >= sy0 &&
                dx1 >= dx0 && dy1 >= dy0;
            if (straight && width === dstWidth && height === dstHeight &&
                    source.format === destination.format &&
                    source.texture !== destination.texture) {
                this.endPass();
                this.ensureEncoder().copyTextureToTexture(
                    { texture: source.texture, mipLevel: source.mipLevel || 0,
                      origin: { x: sx0, y: sy0, z: source.layer || 0 } },
                    { texture: destination.texture || this.backBuffer,
                      mipLevel: destination.mipLevel || 0,
                      origin: { x: dx0, y: dy0,
                        z: destination.origin ? destination.origin.z : 0 } },
                    { width, height, depthOrArrayLayers: 1 });
                return;
            }
            this.blitColorTexture(source, destination,
                sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, filter);
        },

        readAttachmentView() {
            const s = this.current;
            if (!s.readFramebuffer)
                return this.backBuffer ? { texture: this.backBuffer,
                    format: this.backBufferFormat,
                    width: this.backBufferWidth,
                    height: this.backBufferHeight } : null;
            const fbo = this.framebuffers.get(s.id).get(s.readFramebuffer);
            if (!fbo) return null;
            const index = s.readBuffer >= GL.COLOR_ATTACHMENT0 ?
                s.readBuffer - GL.COLOR_ATTACHMENT0 : 0;
            const record = fbo.color[index];
            if (!record) return null;
            if (record.kind === "texture") {
                const texture = s.shareGroup.textures.get(record.name);
                const gpuTexture = this.ensureTextureUploaded(texture);
                return gpuTexture ? { texture: gpuTexture,
                    format: texture.gpuFormat,
                    mipLevel: record.level, layer: record.layer,
                    view: gpuTexture.createView({
                        dimension: "2d", baseMipLevel: record.level,
                        mipLevelCount: 1, baseArrayLayer: record.layer,
                        arrayLayerCount: 1,
                    }),
                    width: Math.max(1, texture.gpuWidth >> record.level),
                    height: Math.max(1, texture.gpuHeight >> record.level) } : null;
            }
            const rb = s.shareGroup.renderbuffers.get(record.name);
            return rb && rb.gpuTexture ? { texture: rb.gpuTexture,
                view: rb.gpuTexture.createView(),
                format: rb.gpuFormat, width: rb.width, height: rb.height } : null;
        },

        copyTexImage(target, level, internalFormat, x, y, width, height) {
            const source = this.readAttachmentView();
            if (!source)
                return this.refuse("glCopyTexImage2D", "no readable attachment",
                    {}, GL.INVALID_FRAMEBUFFER_OPERATION);
            const texture = this.boundTexture(target);
            if (!texture) return;
            const storage = storageFormatFor(internalFormat, this.deviceFeatures);
            const kind = this.textureTargetKind(target);
            if (kind === "3D" || storage.depth || storage.palettised ||
                    storage.compressed)
                return this.refuse("glCopyTexImage2D",
                    "the requested target format cannot receive a colour copy",
                    { target, internalFormat, format: storage.gpu },
                    GL.INVALID_OPERATION);
            texture.target = kind === "Cube" ? GL.TEXTURE_CUBE_MAP : target;
            const bytesPerTexel = storage.gpu === "rgba16float" ? 8 :
                (storage.gpu === "rgba32float" ? 16 : 4);
            const componentType = storage.gpu === "rgba16float" ? "f16" :
                (storage.gpu === "rgba32float" ? "f32" : "u8");
            this.storeLevel(texture, kind, level, CUBE_FACE_INDEX[target] || 0, {
                width, height, depth: 1, gpuFormat: storage.gpu,
                base: storage.base, compressed: false,
                // The level is authored on the GPU.  Keeping pixels null is
                // important: a later lazy upload must not overwrite the copy
                // with a zero-filled CPU shadow.
                pixels: null, bytesPerTexel, componentType, componentCount: 4,
            });
            this.ensureTextureUploaded(texture);
            // Framebuffer row y is GL's row y after the clip-space flip, so the
            // copy is direct rather than mirrored.
            const view = texture.gpuTexture.createView({
                dimension: "2d", baseMipLevel: level, mipLevelCount: 1,
                baseArrayLayer: CUBE_FACE_INDEX[target] || 0,
                arrayLayerCount: 1,
            });
            this.copyColorTexture(source, { view, format: storage.gpu },
                x, y, 0, 0, width, height, "clear");
        },

        copyTexSubImage(target, level, xoffset, yoffset, zoffset, x, y, width,
                height) {
            const source = this.readAttachmentView();
            if (!source)
                return this.refuse("glCopyTexSubImage", "no readable attachment",
                    {}, GL.INVALID_FRAMEBUFFER_OPERATION);
            const texture = this.boundTexture(target);
            if (!texture) return;
            const kind = this.textureTargetKind(target);
            const layer = CUBE_FACE_INDEX[target] || 0;
            const slot = this.levelSlot(texture, kind, level, layer);
            if (!slot)
                return this.refuse("glCopyTexSubImage",
                    "destination level has no storage", { level, layer },
                    GL.INVALID_OPERATION);
            if (kind === "3D" || slot.compressed || slot.base === "DEPTH" ||
                    slot.base === "COLOR_INDEX")
                return this.refuse("glCopyTexSubImage",
                    "the destination format cannot receive a colour copy",
                    { target, format: slot.gpuFormat }, GL.INVALID_OPERATION);
            const gpuTexture = this.ensureTextureUploaded(texture);
            if (!gpuTexture) return;
            void zoffset;
            const view = gpuTexture.createView({
                dimension: "2d", baseMipLevel: level, mipLevelCount: 1,
                baseArrayLayer: layer, arrayLayerCount: 1,
            });
            if (this.copyColorTexture(source,
                    { view, format: slot.gpuFormat }, x, y,
                    xoffset, yoffset, width, height, "load")) {
                // The CPU copy is no longer authoritative after a framebuffer
                // copy.  Mipmap generation below can continue entirely on GPU.
                slot.pixels = null;
                texture.dirty = false;
            }
        },

        /* ---- glReadPixels ----
         *
         * The only place the GL path genuinely needs a GPU round trip. The
         * guest spins on the status word in the record it submitted, so the
         * answer is written back into that record when the mapping resolves --
         * and the heartbeat keeps a busy host from being mistaken for a dead
         * one (plan 6.2).
         */
        readPixels(bytes, view, offset, size, metadata) {
            if (size < READ_PIXELS_HEADER_SIZE) {
                throw new GLStreamError("glReadPixels record is too short");
            }
            const x = view.getInt32(offset, true);
            const y = view.getInt32(offset + 4, true);
            const width = view.getInt32(offset + 8, true);
            const height = view.getInt32(offset + 12, true);
            const format = view.getUint32(offset + 16, true);
            const type = view.getUint32(offset + 20, true);
            const dataSize = view.getUint32(offset + 24, true);
            view.setUint32(offset + 28, SYNC_STATUS_PENDING, true);

            const source = this.readAttachmentView();
            if (!source || width <= 0 || height <= 0) {
                view.setUint32(offset + 28, SYNC_STATUS_FAILED, true);
                return;
            }
            const bytesPerRow = alignUp(width * 4, 256);
            const staging = this.device.createBuffer({
                label: "GL readback",
                size: bytesPerRow * height,
                usage: BUFFER_USAGE_COPY_DST | BUFFER_USAGE_MAP_READ,
            });
            this.endPass();
            const encoder = this.ensureEncoder();
            encoder.copyTextureToBuffer(
                { texture: source.texture, origin: { x, y, z: 0 } },
                { buffer: staging, bytesPerRow, rowsPerImage: height },
                { width, height, depthOrArrayLayers: 1 });
            this.flushFrame();

            const writeGuest = this.options.writeGuestMemory;
            const recordOffset = offset;
            staging.mapAsync(1 /* GPUMapMode.READ */).then(() => {
                const mapped = new Uint8Array(staging.getMappedRange());
                const out = new Uint8Array(dataSize);
                const packed = packReadback(mapped, bytesPerRow, width, height,
                    format, type, source.format, dataSize);
                out.set(packed.subarray(0, dataSize));
                staging.unmap();
                staging.destroy();
                // Two ways home: the payload view when it aliases guest memory,
                // and the bridge's writer when it does not. Doing both is
                // harmless and makes the path work in either wiring.
                if (bytes.byteLength >= recordOffset + READ_PIXELS_HEADER_SIZE +
                        dataSize) {
                    bytes.set(out, recordOffset + READ_PIXELS_HEADER_SIZE);
                    new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
                        .setUint32(recordOffset + 28, SYNC_STATUS_OK, true);
                }
                if (typeof writeGuest === "function") {
                    const payload = new Uint8Array(4 + dataSize);
                    payload.set(out, 4);
                    // Status last: the guest observing OK must already see the
                    // pixels, and memory is copied in increasing order.
                    writeGuest(recordOffset + READ_PIXELS_HEADER_SIZE, out,
                        metadata);
                    const status = new Uint8Array(4);
                    new DataView(status.buffer).setUint32(0, SYNC_STATUS_OK, true);
                    writeGuest(recordOffset + 28, status, metadata);
                }
            }).catch(error => {
                this.warnOnce("readback", "glReadPixels mapping failed",
                    { message: String(error) });
                try { staging.destroy(); } catch (ignored) { /* already gone */ }
            });
        },

        /* ---- occlusion queries ---- */

        beginQuery(target, name) {
            if (target !== GL.SAMPLES_PASSED && target !== GL.ANY_SAMPLES_PASSED)
                return this.refuse("glBeginQuery", "unsupported query target",
                    { target }, GL.INVALID_ENUM);
            const query = this.queries.get(name);
            if (!query)
                return this.refuse("glBeginQuery", "unknown query object",
                    { name }, GL.INVALID_OPERATION);
            this.ensureOcclusionQuerySet();
            if (this.activeQuery)
                return this.refuse("glBeginQuery", "a query is already active",
                    { name }, GL.INVALID_OPERATION);
            query.slot = this.nextQuerySlot++;
            if (query.slot >= this.occlusionCapacity) {
                query.slot = -1;
                return this.refuse("glBeginQuery",
                    "the occlusion query set is full for this frame",
                    { name }, 0);
            }
            query.ready = false;
            query.result = 0;
            this.activeQuery = query;
            // The set is created lazily, so the pass that is already open on
            // the first glBeginQuery of a session was built without it. Ending
            // it makes ensurePass rebuild one that carries it; the rebuilt
            // pass loads the attachments it just stored, so nothing is lost.
            if (this.pass && !this.passHasOcclusionQuerySet) this.endPass();
            const pass = this.ensurePass();
            if (pass) pass.beginOcclusionQuery(query.slot);
        },

        endQuery(target) {
            void target;
            const query = this.activeQuery;
            this.activeQuery = null;
            if (!query) return;
            if (this.pass) this.pass.endOcclusionQuery();
            this.pendingQueries = this.pendingQueries || [];
            this.pendingQueries.push(query);
        },

        ensureOcclusionQuerySet() {
            if (this.activeOcclusionQuerySet) return;
            this.occlusionCapacity = 4096;
            this.activeOcclusionQuerySet = this.device.createQuerySet({
                type: "occlusion", count: this.occlusionCapacity,
            });
            this.nextQuerySlot = 0;
        },

        /*
         * Cube 2 keeps a pool of two thousand query objects and asks for them
         * all at once, so the batch resolves every pending query in one
         * readback rather than one per object.
         */
        queryObjectBatch(bytes, view, offset, size) {
            const headerSize = 16;
            const entrySize = 12;
            if (size < headerSize) throw new GLStreamError("query batch is short");
            const count = view.getUint32(offset, true);
            if (headerSize + count * entrySize > size) {
                view.setUint32(offset + 8, SYNC_STATUS_FAILED, true);
                return;
            }
            // Results already resolved are answered immediately; the rest keep
            // their last value, which GL permits for a query that is not ready.
            for (let i = 0; i < count; ++i) {
                const at = offset + headerSize + i * entrySize;
                const name = view.getUint32(at, true);
                const query = this.queries.get(name);
                view.setUint32(at + 4, query && query.ready ? 1 : 0, true);
                view.setUint32(at + 8, query ? query.result >>> 0 : 0, true);
            }
            view.setUint32(offset + 8, SYNC_STATUS_OK, true);
        },

        onFinish(bytes, view, offset, size) {
            this.flushFrame();
            if (size >= 4) view.setUint32(offset, SYNC_STATUS_OK, true);
        },

        /* ---- legacy pixel rectangles ---- */

        drawPixels(bytes, view, offset, size) {
            if (size < 20) throw new GLStreamError("glDrawPixels record is short");
            const width = view.getInt32(offset, true);
            const height = view.getInt32(offset + 4, true);
            const format = view.getUint32(offset + 8, true);
            const type = view.getUint32(offset + 12, true);
            const dataSize = view.getUint32(offset + 16, true);
            if (width <= 0 || height <= 0 || !this.current.current.rasterValid)
                return;
            if (20 + dataSize > size)
                throw new GLStreamError("glDrawPixels data is truncated");
            const source = bytes.subarray(offset + 20, offset + 20 + dataSize);
            const store = this.current.pixelStore;
            const rgba = convertPixels(source, 0, width, height, 1,
                format, type, "RGBA", {
                    alignment: store.unpackAlignment,
                    rowLength: store.unpackRowLength,
                    skipPixels: store.unpackSkipPixels,
                    skipRows: store.unpackSkipRows,
                    imageHeight: store.unpackImageHeight,
                    skipImages: store.unpackSkipImages,
                });
            if (!rgba)
                return this.refuse("glDrawPixels", "unsupported or truncated pixel data",
                    { width, height, format, type }, GL.INVALID_ENUM);
            this.applyPixelTransfer(rgba);
            this.drawPixelRGBA(rgba, width, height,
                this.current.current.rasterPos[0],
                this.current.current.rasterPos[1], false);
        },

        drawBitmap(bytes, view, offset, size) {
            if (size < 28) throw new GLStreamError("glBitmap record is short");
            const width = view.getInt32(offset, true);
            const height = view.getInt32(offset + 4, true);
            const xorig = view.getFloat32(offset + 8, true);
            const yorig = view.getFloat32(offset + 12, true);
            const xmove = view.getFloat32(offset + 16, true);
            const ymove = view.getFloat32(offset + 20, true);
            const dataSize = view.getUint32(offset + 24, true);
            const s = this.current;
            if (28 + dataSize > size)
                throw new GLStreamError("glBitmap data is truncated");
            if (s.current.rasterValid && width > 0 && height > 0 && dataSize) {
                const source = bytes.subarray(offset + 28,
                    offset + 28 + dataSize);
                const rgba = this.expandBitmap(source, width, height);
                this.drawPixelRGBA(rgba, width, height,
                    s.current.rasterPos[0] - xorig,
                    s.current.rasterPos[1] - yorig, true);
            }
            if (s.current.rasterValid) {
                s.current.rasterPos[0] += xmove;
                s.current.rasterPos[1] += ymove;
            }
        },

        copyPixels(x, y, width, height, type) {
            if (width <= 0 || height <= 0 || !this.current.current.rasterValid)
                return;
            if (type !== GL.COLOR)
                return this.refuse("glCopyPixels",
                    "depth and stencil pixel copies need typed attachment copies",
                    { type }, GL.INVALID_OPERATION);
            const source = this.readAttachmentView();
            if (!source) return this.refuse("glCopyPixels",
                "no readable colour attachment", {},
                GL.INVALID_FRAMEBUFFER_OPERATION);
            this.endPass();
            const texture = this.device.createTexture({
                label: "GL copy-pixels source",
                size: { width, height, depthOrArrayLayers: 1 },
                format: source.format,
                usage: TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
            });
            this.ensureEncoder().copyTextureToTexture(
                { texture: source.texture, origin: { x, y, z: 0 } },
                { texture }, { width, height, depthOrArrayLayers: 1 });
            this.drawPixelTexture(texture, texture.createView(), width, height,
                this.current.current.rasterPos[0],
                this.current.current.rasterPos[1], false);
            this.retire(texture);
        },

        applyPixelTransfer(rgba) {
            const transfer = this.current.pixelTransfer;
            const maps = this.pixelMaps || Object.create(null);
            const scales = [transfer.redScale, transfer.greenScale,
                transfer.blueScale, transfer.alphaScale];
            const biases = [transfer.redBias, transfer.greenBias,
                transfer.blueBias, transfer.alphaBias];
            const matrix = this.topOf(GL.COLOR);
            const mapNames = [GL.PIXEL_MAP_R_TO_R, GL.PIXEL_MAP_G_TO_G,
                GL.PIXEL_MAP_B_TO_B, GL.PIXEL_MAP_A_TO_A];
            for (let at = 0; at < rgba.length; at += 4) {
                const input = [0, 0, 0, 0];
                for (let c = 0; c < 4; ++c) {
                    input[c] = clamp(rgba[at + c] / 255 * scales[c] +
                        biases[c], 0, 1);
                }
                const transformed = [
                    matrix[0] * input[0] + matrix[4] * input[1] +
                        matrix[8] * input[2] + matrix[12] * input[3],
                    matrix[1] * input[0] + matrix[5] * input[1] +
                        matrix[9] * input[2] + matrix[13] * input[3],
                    matrix[2] * input[0] + matrix[6] * input[1] +
                        matrix[10] * input[2] + matrix[14] * input[3],
                    matrix[3] * input[0] + matrix[7] * input[1] +
                        matrix[11] * input[2] + matrix[15] * input[3],
                ];
                for (let c = 0; c < 4; ++c) {
                    let value = clamp(transformed[c] *
                        transfer.postColorMatrixScale[c] +
                        transfer.postColorMatrixBias[c], 0, 1);
                    const map = transfer.mapColor ? maps[mapNames[c]] : null;
                    if (map && map.length) {
                        const index = clamp(Math.round(value * (map.length - 1)),
                            0, map.length - 1);
                        value = clamp(map[index], 0, 1);
                    }
                    rgba[at + c] = Math.round(value * 255);
                }
            }
        },

        expandBitmap(source, width, height) {
            const store = this.current.pixelStore;
            const rowLength = store.unpackRowLength || width;
            const rowBytes = Math.ceil(rowLength / 8);
            const stride = alignUp(rowBytes, store.unpackAlignment || 1);
            const skip = store.unpackSkipPixels || 0;
            const rgba = new Uint8Array(width * height * 4);
            const color = this.current.current.color;
            for (let row = 0; row < height; ++row) {
                const rowStart = (store.unpackSkipRows + row) * stride;
                for (let x = 0; x < width; ++x) {
                    const bit = skip + x;
                    const byte = source[rowStart + (bit >> 3)] || 0;
                    const mask = store.unpackLsbFirst ? 1 << (bit & 7) :
                        0x80 >> (bit & 7);
                    if (!(byte & mask)) continue;
                    const at = (row * width + x) * 4;
                    rgba[at] = Math.round(clamp(color[0], 0, 1) * 255);
                    rgba[at + 1] = Math.round(clamp(color[1], 0, 1) * 255);
                    rgba[at + 2] = Math.round(clamp(color[2], 0, 1) * 255);
                    rgba[at + 3] = Math.round(clamp(color[3], 0, 1) * 255);
                }
            }
            return rgba;
        },

        drawPixelRGBA(rgba, width, height, x, y, discardZeroAlpha) {
            const texture = this.device.createTexture({
                label: "GL pixel rectangle",
                size: { width, height, depthOrArrayLayers: 1 },
                format: "rgba8unorm",
                usage: TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
            });
            this.device.queue.writeTexture({ texture }, rgba,
                { bytesPerRow: width * 4, rowsPerImage: height },
                { width, height, depthOrArrayLayers: 1 });
            this.drawPixelTexture(texture, texture.createView(), width, height,
                x, y, discardZeroAlpha);
            this.retire(texture);
        },

        drawPixelTexture(texture, textureView, width, height, x, y,
                discardZeroAlpha) {
            const s = this.current;
            const vp = s.viewport;
            if (!vp.width || !vp.height) return;
            const x0 = x, y0 = y;
            const x1 = x + width * s.pixelZoom.x;
            const y1 = y + height * s.pixelZoom.y;
            const ndcX = value => (value - vp.x) * 2 / vp.width - 1;
            // The normal GL vertex path negates clip-space Y. Expressing the
            // window coordinate directly therefore uses the same negated map.
            const ndcY = value => 1 - (value - vp.y) * 2 / vp.height;
            const range = s.depthRange.far - s.depthRange.near;
            const z = Math.abs(range) > 1e-12 ?
                (s.current.rasterPos[2] - s.depthRange.near) / range : 0;
            const vertices = new Float32Array([
                ndcX(x0), ndcY(y0), z, 1, 0, 0,
                ndcX(x1), ndcY(y0), z, 1, 1, 0,
                ndcX(x0), ndcY(y1), z, 1, 0, 1,
                ndcX(x0), ndcY(y1), z, 1, 0, 1,
                ndcX(x1), ndcY(y0), z, 1, 1, 0,
                ndcX(x1), ndcY(y1), z, 1, 1, 1,
            ]);
            const slice = this.uploadVertices(new Uint8Array(vertices.buffer));
            if (!slice) return;
            const pass = this.ensurePass();
            if (!pass) return;
            const pipeline = this.pixelPipeline(discardZeroAlpha);
            if (!pipeline) return;
            pass.setPipeline(pipeline);
            this.applyPassState(pass);
            pass.setVertexBuffer(0, this.vertexRing, slice.offset);
            pass.setBindGroup(0, this.device.createBindGroup({
                layout: pipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: textureView },
                    { binding: 1, resource: this.pixelSampler ||
                        (this.pixelSampler = this.device.createSampler({
                            minFilter: "nearest", magFilter: "nearest",
                        })) },
                ],
            }));
            pass.draw(6, 1, 0, 0);
            ++this.stats.draws;
            void texture;
        },

        pixelPipeline(discardZeroAlpha) {
            const s = this.current;
            const targets = this.passTargets;
            if (!targets) return null;
            const alpha = s.enabled.has(GL.ALPHA_TEST) ?
                (ALPHA_TEST_NAMES[s.alphaFunc.func] || "always") : "always";
            const key = [discardZeroAlpha ? 1 : 0, alpha, s.alphaFunc.ref,
                s.enabled.has(GL.DEPTH_TEST) ? s.depthFunc : "off",
                s.depthMask ? 1 : 0,
                s.enabled.has(GL.STENCIL_TEST) ? stencilKey(s) : "off",
                s.enabled.has(GL.BLEND) ? blendKey(s) : "off",
                s.colorMask.map(v => v ? 1 : 0).join(""),
                targets.color.map(c => c ? c.format : "-").join(","),
                targets.depthFormat || "-"].join("|");
            this.pixelPipelineCache = this.pixelPipelineCache || new Map();
            let pipeline = this.pixelPipelineCache.get(key);
            if (pipeline) return pipeline;
            const conditions = [];
            if (discardZeroAlpha) conditions.push("color.a <= 0.0");
            if (alpha === "never") conditions.push("true");
            else if (alpha !== "always") {
                const op = {
                    less: "<", equal: "==", lequal: "<=", greater: ">",
                    notequal: "!=", gequal: ">=",
                }[alpha];
                if (op) conditions.push("!(color.a " + op + " " +
                    Number(s.alphaFunc.ref).toPrecision(9) + ")");
            }
            const outputFields = targets.color.map((target, i) => target ?
                "    @location(" + i + ") color" + i + " : vec4<f32>," : "").join("\n");
            const outputWrites = targets.color.map((target, i) => target ?
                "    out.color" + i + " = color;" : "").join("\n");
            const code = `
struct VSIn { @location(0) position : vec4<f32>, @location(1) uv : vec2<f32>, }
struct Varying { @builtin(position) position : vec4<f32>, @location(0) uv : vec2<f32>, }
@vertex fn vs_main(v : VSIn) -> Varying {
    var out : Varying; out.position = v.position; out.uv = v.uv; return out;
}
@group(0) @binding(0) var pixels : texture_2d<f32>;
@group(0) @binding(1) var pixelSampler : sampler;
struct PixelOut {
${outputFields}
}
@fragment fn fs_main(v : Varying) -> PixelOut {
    let color = textureSample(pixels, pixelSampler, v.uv);
    ${conditions.length ? "if (" + conditions.join(" || ") + ") { discard; }" : ""}
    var out : PixelOut;
${outputWrites}
    return out;
}`;
            const colorTargets = targets.color.map(target => target ? {
                format: target.format,
                blend: s.enabled.has(GL.BLEND) ? gpuBlendState(s) : undefined,
                writeMask: (s.colorMask[0] ? 1 : 0) | (s.colorMask[1] ? 2 : 0) |
                    (s.colorMask[2] ? 4 : 0) | (s.colorMask[3] ? 8 : 0),
            } : null);
            const module = this.shaderModule(code);
            const descriptor = {
                label: "GL pixel rectangle pipeline",
                layout: "auto",
                vertex: { module, entryPoint: "vs_main", buffers: [{
                    arrayStride: 24, stepMode: "vertex", attributes: [
                        { shaderLocation: 0, format: "float32x4", offset: 0 },
                        { shaderLocation: 1, format: "float32x2", offset: 16 },
                    ],
                }] },
                fragment: { module, entryPoint: "fs_main", targets: colorTargets },
                primitive: { topology: "triangle-list", cullMode: "none" },
                multisample: { count: 1 },
            };
            if (targets.depthFormat) {
                descriptor.depthStencil = {
                    format: targets.depthFormat,
                    depthWriteEnabled: !!(s.enabled.has(GL.DEPTH_TEST) && s.depthMask),
                    depthCompare: s.enabled.has(GL.DEPTH_TEST) ?
                        (COMPARE_FUNCTIONS[s.depthFunc] || "less") : "always",
                    ...(targets.depthFormat.indexOf("stencil") >= 0 &&
                        s.enabled.has(GL.STENCIL_TEST) ? gpuStencilState(s) : {}),
                };
            }
            try {
                pipeline = this.device.createRenderPipeline(descriptor);
            } catch (error) {
                this.refuse("glDrawPixels", "could not create pixel pipeline",
                    { message: String(error) }, GL.INVALID_OPERATION);
                return null;
            }
            this.pixelPipelineCache.set(key, pipeline);
            return pipeline;
        },

        accum(op, value) {
            const ACCUM = 0x0100, LOAD = 0x0101, RETURN = 0x0102;
            const MULT = 0x0103, ADD = 0x0104;
            if (![ACCUM, LOAD, RETURN, MULT, ADD].includes(op))
                return this.refuse("glAccum", "invalid accumulation operation",
                    { op }, GL.INVALID_ENUM);
            this.endPass();
            const targets = this.currentTargets();
            if (!targets || !targets.color[0])
                return this.refuse("glAccum", "no colour buffer is bound", {},
                    GL.INVALID_OPERATION);
            const accum = this.ensureAccumBuffer(targets);
            const returning = op === RETURN;
            const destination = returning ? targets.color[0].view : accum.nextView;
            const targetFormat = returning ? targets.color[0].format : "rgba16float";
            const pipeline = this.accumPipeline(op, targetFormat);
            if (!pipeline) return;
            const params = this.allocateUniform(16);
            if (!params) return;
            new Float32Array(this.uniformStaging.buffer, params.offset, 4)
                .set([value, 0, 0, 0]);
            this.device.queue.writeBuffer(this.uniformRing, params.offset,
                this.uniformStaging, params.offset, params.size);
            const pass = this.ensureEncoder().beginRenderPass({
                label: "GL accumulation operation",
                colorAttachments: [{ view: destination, loadOp: "load",
                    storeOp: "store" }],
            });
            this.applyAccumScissor(pass, targets);
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, this.device.createBindGroup({
                layout: pipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: accum.currentView },
                    { binding: 1, resource: returning ? this.fallbackView :
                        targets.color[0].view },
                    { binding: 2, resource: { buffer: this.uniformRing,
                        offset: params.offset, size: 16 } },
                ],
            }));
            pass.draw(3, 1, 0, 0);
            pass.end();
            if (!returning) {
                const texture = accum.currentTexture;
                accum.currentTexture = accum.nextTexture;
                accum.currentView = accum.nextView;
                accum.nextTexture = texture;
                accum.nextView = texture.createView();
            }
        },

        ensureAccumBuffer(targets) {
            const width = Math.max(1, targets.width);
            const height = Math.max(1, targets.height);
            const key = this.current.id + ":" + this.current.drawFramebuffer +
                ":" + width + "x" + height;
            if (this.accumBuffer && this.accumBuffer.key === key)
                return this.accumBuffer;
            if (this.accumBuffer) {
                this.retire(this.accumBuffer.currentTexture);
                this.retire(this.accumBuffer.nextTexture);
            }
            const create = label => this.device.createTexture({
                label,
                size: { width, height, depthOrArrayLayers: 1 },
                format: "rgba16float",
                usage: TEXTURE_USAGE_RENDER_ATTACHMENT |
                    TEXTURE_USAGE_TEXTURE_BINDING | TEXTURE_USAGE_COPY_SRC |
                    TEXTURE_USAGE_COPY_DST,
            });
            const currentTexture = create("GL accumulation buffer A");
            const nextTexture = create("GL accumulation buffer B");
            this.accumBuffer = {
                key, width, height,
                currentTexture, currentView: currentTexture.createView(),
                nextTexture, nextView: nextTexture.createView(),
            };
            return this.accumBuffer;
        },

        applyAccumScissor(pass, targets) {
            const s = this.current;
            if (s.enabled.has(GL.SCISSOR_TEST) && s.scissor.set) {
                const x = clamp(s.scissor.x, 0, targets.width);
                const y = clamp(s.scissor.y, 0, targets.height);
                pass.setScissorRect(x, y,
                    Math.max(0, Math.min(s.scissor.width, targets.width - x)),
                    Math.max(0, Math.min(s.scissor.height, targets.height - y)));
            } else {
                pass.setScissorRect(0, 0, targets.width, targets.height);
            }
        },

        accumPipeline(op, targetFormat) {
            this.accumPipelineCache = this.accumPipelineCache || new Map();
            const returning = op === 0x0102;
            const mask = returning ? this.current.colorMask.map(v => v ? 1 : 0)
                .join("") : "1111";
            const key = op + "|" + targetFormat + "|" + mask;
            let pipeline = this.accumPipelineCache.get(key);
            if (pipeline) return pipeline;
            const expression = {
                0x0100: "oldAccum + framebuffer * params.value.x",
                0x0101: "framebuffer * params.value.x",
                0x0102: "oldAccum * params.value.x",
                0x0103: "oldAccum * params.value.x",
                0x0104: "oldAccum + vec4<f32>(params.value.x)",
            }[op];
            const code = `
struct Params { value : vec4<f32>, }
@group(0) @binding(0) var accumulation : texture_2d<f32>;
@group(0) @binding(1) var colorBuffer : texture_2d<f32>;
@group(0) @binding(2) var<uniform> params : Params;
@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}
@fragment fn fs_main(@builtin(position) p : vec4<f32>) -> @location(0) vec4<f32> {
    let xy = vec2<i32>(p.xy);
    let oldAccum = textureLoad(accumulation, xy, 0);
    let framebuffer = textureLoad(colorBuffer, xy, 0);
    return ${expression};
}`;
            const module = this.shaderModule(code);
            const writeMask = returning ?
                (this.current.colorMask[0] ? 1 : 0) |
                (this.current.colorMask[1] ? 2 : 0) |
                (this.current.colorMask[2] ? 4 : 0) |
                (this.current.colorMask[3] ? 8 : 0) : 15;
            try {
                pipeline = this.device.createRenderPipeline({
                    label: "GL accumulation pipeline",
                    layout: "auto",
                    vertex: { module, entryPoint: "vs_main" },
                    fragment: { module, entryPoint: "fs_main",
                        targets: [{ format: targetFormat, writeMask }] },
                    primitive: { topology: "triangle-list" },
                });
            } catch (error) {
                this.refuse("glAccum", "could not create accumulation pipeline",
                    { message: String(error) }, GL.INVALID_OPERATION);
                return null;
            }
            this.accumPipelineCache.set(key, pipeline);
            return pipeline;
        },

        accumClearPipeline() {
            const color = Array.from(this.current.clearAccum)
                .map(value => Number(value).toPrecision(9));
            const key = color.join(",");
            this.accumClearPipelineCache = this.accumClearPipelineCache || new Map();
            let pipeline = this.accumClearPipelineCache.get(key);
            if (pipeline) return pipeline;
            const code = `
@vertex fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(${color.join(", ")});
}`;
            const module = this.shaderModule(code);
            pipeline = this.device.createRenderPipeline({
                label: "GL accumulation clear pipeline", layout: "auto",
                vertex: { module, entryPoint: "vs_main" },
                fragment: { module, entryPoint: "fs_main",
                    targets: [{ format: "rgba16float" }] },
                primitive: { topology: "triangle-list" },
            });
            this.accumClearPipelineCache.set(key, pipeline);
            return pipeline;
        },
        setRasterPos(x, y, z, w) {
            // The raster position is transformed like a vertex; keeping it is
            // cheap and glWindowPos-based code depends on reading it back.
            const s = this.current;
            const clip = transformPoint4(this.topOf(GL.PROJECTION),
                transformPoint4(this.topOf(GL.MODELVIEW),
                    new Float32Array([x, y, z, w]), new Float32Array(4)),
                new Float32Array(4));
            s.current.rasterValid = clip[3] !== 0;
            if (!s.current.rasterValid) return;
            const ndcX = clip[0] / clip[3], ndcY = clip[1] / clip[3];
            const ndcZ = clip[2] / clip[3];
            s.current.rasterPos[0] = s.viewport.x +
                (ndcX * 0.5 + 0.5) * s.viewport.width;
            s.current.rasterPos[1] = s.viewport.y +
                (ndcY * 0.5 + 0.5) * s.viewport.height;
            s.current.rasterPos[2] = clamp((ndcZ * 0.5 + 0.5), 0, 1);
            s.current.rasterPos[3] = 1 / clip[3];
        },
        /*
         * glProgramStringARB compiles immediately, the way the extension
         * specifies: the program's error state and GL_PROGRAM_ERROR_POSITION_ARB
         * are readable right afterwards, and a game that checks them expects an
         * answer rather than a promise.
         */
        programStringARB(target, source) {
            const program = this.arbProgramFor(target);
            if (!program)
                return this.refuse("glProgramStringARB", "no ARB program bound",
                    { target }, GL.INVALID_OPERATION);
            program.source = source;
            program.target = target;
            program.compiled = arbProgram.compileARBProgram(source, {});
            if (program.variants) program.variants.clear();
            if (!program.compiled.ok) {
                /*
                 * A program the *application* wrote wrongly is not a host
                 * refusal: the extension defines exactly how to report it, and
                 * an app that submits a deliberately bad program to check its
                 * error handling -- which is what a conformance viewer does --
                 * is behaving correctly. Counting it in stats.refusals would
                 * make that counter useless as "how much of this frame did the
                 * host fail to draw". So it takes GL's own channel: the error,
                 * the error string and the error position, and one warning
                 * rather than one error.
                 */
                this.arbErrorString = program.compiled.log;
                this.arbErrorPosition = program.compiled.errorPosition >= 0 ?
                    program.compiled.errorPosition : 0;
                this.current.arbErrorString = this.arbErrorString;
                this.current.arbErrorPosition = this.arbErrorPosition;
                this.setError(GL.INVALID_OPERATION);
                this.warnOnce("arbProgramCompile",
                    "an ARB program did not compile; the app can read the " +
                    "reason from GL_PROGRAM_ERROR_STRING_ARB",
                    { target, log: program.compiled.log,
                      position: this.arbErrorPosition });
                return;
            }
            this.arbErrorString = "";
            this.arbErrorPosition = -1;
            this.current.arbErrorString = "";
            this.current.arbErrorPosition = -1;
            arbParameterStorage(program, PROGRAM_PARAMETER_ENV);
            ++this.stats.shaderLinks;
        },

        /*
         * {kind, target, index, count} then count*4 floats, or {kind, target,
         * index, reserved} then four doubles. openglproxy sends the parameter
         * kind first -- V86GL_PROGRAM_PARAMETER_ENV is 1 and _LOCAL is 2 --
         * and the target second.
         */
        programParameterARB(view, offset, size, doublePrecision) {
            const stride = doublePrecision ? 8 : 4;
            if (size < 16 + 4 * stride)
                throw new GLStreamError("ARB parameter record is short");
            const kind = view.getUint32(offset, true);
            const target = view.getUint32(offset + 4, true);
            const index = view.getUint32(offset + 8, true);
            // The double-precision record carries exactly one parameter; the
            // float one carries glProgramEnvParameters4fvEXT's whole run.
            const count = doublePrecision ? 1 :
                Math.max(1, view.getUint32(offset + 12, true));
            const program = this.arbProgramFor(target);
            if (!program)
                return this.refuse("glProgramParameterARB",
                    "no ARB program is bound", { target },
                    GL.INVALID_OPERATION);
            if (index >= arbProgram.MAX_PROGRAM_PARAMETERS ||
                    count > arbProgram.MAX_PROGRAM_PARAMETERS - index)
                return this.refuse("glProgramParameterARB",
                    "parameter index is beyond the advertised count",
                    { index, count }, GL.INVALID_VALUE);
            if (16 + count * 4 * stride > size)
                throw new GLStreamError("ARB parameter record is truncated");
            const storage = arbParameterStorage(program, kind);
            for (let i = 0; i < count * 4; ++i) {
                const at = offset + 16 + i * stride;
                storage[index * 4 + i] = doublePrecision ?
                    view.getFloat64(at, true) : view.getFloat32(at, true);
            }
        },

        queryProgramivARB(target, pname) {
            const program = this.arbProgramFor(target);
            switch (pname) {
            case GL.PROGRAM_LENGTH_ARB:
                return program ? program.source.length : 0;
            case GL.PROGRAM_FORMAT_ARB:
                return GL.PROGRAM_FORMAT_ASCII_ARB;
            case GL.PROGRAM_BINDING_ARB:
                return target === GL.VERTEX_PROGRAM_ARB ?
                    this.current.arbVertexProgram : this.current.arbFragmentProgram;
            case GL.PROGRAM_INSTRUCTIONS_ARB:
            case GL.PROGRAM_NATIVE_INSTRUCTIONS_ARB:
                return program && program.compiled && program.compiled.ok ?
                    program.compiled.reflection.instructionCount : 0;
            case GL.PROGRAM_TEMPORARIES_ARB:
            case GL.PROGRAM_NATIVE_TEMPORARIES_ARB:
                return program && program.compiled && program.compiled.ok ?
                    program.compiled.reflection.temporaryCount : 0;
            case GL.PROGRAM_PARAMETERS_ARB:
            case GL.PROGRAM_NATIVE_PARAMETERS_ARB:
            case GL.MAX_PROGRAM_PARAMETERS_ARB:
            case GL.MAX_PROGRAM_NATIVE_PARAMETERS_ARB:
            case GL.MAX_PROGRAM_ENV_PARAMETERS_ARB:
            case GL.MAX_PROGRAM_LOCAL_PARAMETERS_ARB:
                return arbProgram.MAX_PROGRAM_PARAMETERS;
            case GL.MAX_PROGRAM_INSTRUCTIONS_ARB:
            case GL.MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB:
                return 4096;
            case GL.MAX_PROGRAM_TEMPORARIES_ARB:
            case GL.MAX_PROGRAM_NATIVE_TEMPORARIES_ARB:
                return arbProgram.MAX_TEMPORARIES;
            case GL.PROGRAM_UNDER_NATIVE_LIMITS_ARB:
                return program && program.compiled && program.compiled.ok ? 1 : 0;
            case GL.PROGRAM_ERROR_POSITION_ARB:
                // -1 means "the last ProgramString succeeded"; otherwise the
                // character offset the parser stopped at.
                return this.current && this.current.arbErrorPosition !== undefined ?
                    this.current.arbErrorPosition : -1;
            default:
                return 0;
            }
        },

        /* {kind, target, index, status, data_size, reserved} then the four
         * components at 24 -- the read-back twin of programParameterARB. */
        queryProgramParameterARB(view, offset, size, doublePrecision) {
            if (size < 24) throw new GLStreamError("ARB query record is short");
            const kind = view.getUint32(offset, true);
            const target = view.getUint32(offset + 4, true);
            const index = view.getUint32(offset + 8, true);
            const program = this.arbProgramFor(target);
            const stride = doublePrecision ? 8 : 4;
            if (!program || index >= arbProgram.MAX_PROGRAM_PARAMETERS ||
                    24 + 4 * stride > size) {
                view.setUint32(offset + 12, SYNC_STATUS_FAILED, true);
                return;
            }
            const storage = arbParameterStorage(program, kind);
            view.setUint32(offset + 12, SYNC_STATUS_OK, true);
            for (let c = 0; c < 4; ++c) {
                const at = offset + 24 + c * stride;
                if (doublePrecision) view.setFloat64(at, storage[index * 4 + c], true);
                else view.setFloat32(at, storage[index * 4 + c], true);
            }
        },
        arbProgramFor(target) {
            const s = this.current;
            const name = target === GL.VERTEX_PROGRAM_ARB ?
                s.arbVertexProgram : s.arbFragmentProgram;
            return name ? s.shareGroup.arbPrograms.get(name) || null : null;
        },
    });

    /*
     * Repacks a 256-aligned readback into the format the guest asked for.
     * GL's rows run bottom-up in the same direction the framebuffer does here,
     * so no vertical flip is involved -- which is the point of flipping in the
     * vertex shader instead.
     */
    function packReadback(mapped, bytesPerRow, width, height, format, type,
            sourceFormat, capacity) {
        const bgra = sourceFormat.indexOf("bgra") === 0;
        const components = SOURCE_COMPONENTS[format] || 4;
        const out = new Uint8Array(capacity);
        let dst = 0;
        for (let row = 0; row < height; ++row) {
            let src = row * bytesPerRow;
            for (let x = 0; x < width; ++x) {
                const r = mapped[src + (bgra ? 2 : 0)];
                const g = mapped[src + 1];
                const b = mapped[src + (bgra ? 0 : 2)];
                const a = mapped[src + 3];
                if (type === GL.UNSIGNED_BYTE) {
                    switch (format) {
                    case GL.RGBA: out[dst] = r; out[dst+1] = g; out[dst+2] = b; out[dst+3] = a; break;
                    case GL.BGRA: out[dst] = b; out[dst+1] = g; out[dst+2] = r; out[dst+3] = a; break;
                    case GL.RGB: out[dst] = r; out[dst+1] = g; out[dst+2] = b; break;
                    case GL.BGR: out[dst] = b; out[dst+1] = g; out[dst+2] = r; break;
                    case GL.ALPHA: out[dst] = a; break;
                    case GL.LUMINANCE: out[dst] = r; break;
                    default: out[dst] = r; break;
                    }
                    dst += components;
                } else if (type === GL.UNSIGNED_SHORT_5_6_5) {
                    const packed = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    out[dst] = packed & 0xff;
                    out[dst + 1] = packed >> 8;
                    dst += 2;
                } else {
                    out[dst] = r; out[dst+1] = g; out[dst+2] = b; out[dst+3] = a;
                    dst += 4;
                }
                src += 4;
                if (dst >= capacity) return out;
            }
        }
        return out;
    }

    /* ================================================================== */
    /* Executor: glGet* answers and capability reporting                  */
    /* ================================================================== */

    /*
     * The extension string and every GL_MAX_* are decided by what this
     * executor actually implements against this adapter -- never guessed.
     * Advertising a capability nothing backs is the mistake the D3D8 path made
     * once and paid for; see plan section 4.14.
     */
    Object.assign(GLWebGPUExecutor.prototype, {

        extensionString() {
            const list = [
                "GL_ARB_multitexture", "GL_ARB_texture_env_add",
                "GL_ARB_texture_env_combine", "GL_ARB_texture_env_dot3",
                "GL_ARB_texture_env_crossbar", "GL_ARB_texture_cube_map",
                "GL_ARB_texture_border_clamp", "GL_ARB_texture_mirrored_repeat",
                "GL_ARB_texture_compression", "GL_SGI_color_matrix",
                "GL_ARB_texture_non_power_of_two", "GL_ARB_depth_texture",
                "GL_ARB_shadow", "GL_ARB_point_parameters", "GL_ARB_point_sprite",
                "GL_ARB_transpose_matrix", "GL_ARB_window_pos",
                "GL_ARB_vertex_buffer_object", "GL_ARB_occlusion_query",
                "GL_ARB_multisample", "GL_ARB_shader_objects",
                "GL_ARB_vertex_shader", "GL_ARB_fragment_shader",
                "GL_ARB_shading_language_100", "GL_ARB_draw_buffers",
                "GL_EXT_texture3D", "GL_EXT_texture_lod_bias",
                "GL_EXT_texture_edge_clamp", "GL_EXT_blend_color",
                "GL_EXT_blend_minmax", "GL_EXT_blend_subtract",
                "GL_EXT_blend_func_separate", "GL_EXT_blend_equation_separate",
                "GL_EXT_stencil_wrap", "GL_EXT_stencil_two_side",
                "GL_EXT_secondary_color", "GL_EXT_fog_coord",
                "GL_EXT_packed_pixels", "GL_EXT_rescale_normal",
                "GL_EXT_separate_specular_color", "GL_EXT_bgra",
                "GL_EXT_framebuffer_object", "GL_EXT_framebuffer_blit",
                "GL_EXT_paletted_texture", "GL_EXT_generate_mipmap",
                "GL_SGIS_generate_mipmap", "GL_SGIS_texture_edge_clamp",
                "GL_SGIS_texture_lod", "GL_NV_blend_square",
            ];
            // Only advertised when the adapter really has the BC formats: the
            // CPU decode path keeps the picture right, but a guest that sees
            // S3TC will hand us compressed data forever, and saying so honestly
            // lets it choose.
            if (this.deviceFeatures.bc)
                list.push("GL_EXT_texture_compression_s3tc");
            if (this.deviceFeatures.float32Filterable)
                list.push("GL_ARB_texture_float", "GL_ARB_half_float_pixel");
            if (this.deviceFeatures.depthClipControl)
                list.push("GL_ARB_depth_clamp");
            list.push("GL_EXT_texture_filter_anisotropic");
            return list.join(" ");
        },

        queryString(pname) {
            switch (pname) {
            case GL.VENDOR: return "Anthropic v86gl";
            case GL.RENDERER: return "v86 WebGPU bridge";
            case GL.VERSION: return "2.1 (v86gl/WebGPU)";
            case GL.SHADING_LANGUAGE_VERSION: return "1.20";
            case GL.EXTENSIONS: return this.extensionString();
            case GL.PROGRAM_ERROR_STRING_ARB:
                // Empty after a successful ProgramString, which is what the
                // extension specifies -- not null, which would leave the guest
                // reporting "unsupported query" for a legal one.
                return this.current ? (this.current.arbErrorString || "") : "";
            default: return null;
            }
        },

        queryInteger(pname) {
            const limits = this.limits || {};
            const s = this.current;
            switch (pname) {
            case 0x76380001: // private v86gl host-capability bitset
                return 0x1 | 0x2 |
                    (this.deviceFeatures.float32Filterable ? 0x4 : 0) | 0x8;
            case GL.MAX_TEXTURE_SIZE:
                return Math.min(16384, limits.maxTextureDimension2D || 8192);
            case GL.MAX_3D_TEXTURE_SIZE:
                return Math.min(2048, limits.maxTextureDimension3D || 2048);
            case GL.MAX_CUBE_MAP_TEXTURE_SIZE:
                return Math.min(16384, limits.maxTextureDimension2D || 8192);
            case GL.MAX_TEXTURE_UNITS: return MAX_TEXTURE_UNITS;
            case GL.MAX_TEXTURE_COORDS: return MAX_TEXTURE_COORDS;
            case GL.MAX_TEXTURE_IMAGE_UNITS: return 16;
            case GL.MAX_VERTEX_TEXTURE_IMAGE_UNITS: return 16;
            case GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS: return 24;
            case GL.MAX_VERTEX_ATTRIBS: return MAX_VERTEX_ATTRIBS;
            case GL.MAX_VARYING_FLOATS:
                return translator.MAX_INTER_STAGE_SLOTS * 4;
            case GL.MAX_VERTEX_UNIFORM_COMPONENTS: return 1024;
            case GL.MAX_FRAGMENT_UNIFORM_COMPONENTS: return 1024;
            case GL.MAX_DRAW_BUFFERS:
                return Math.min(MAX_DRAW_BUFFERS, limits.maxColorAttachments || 8);
            case GL.MAX_COLOR_ATTACHMENTS:
                return Math.min(MAX_DRAW_BUFFERS, limits.maxColorAttachments || 8);
            case GL.MAX_RENDERBUFFER_SIZE:
                return Math.min(16384, limits.maxTextureDimension2D || 8192);
            case GL.MAX_LIGHTS: return MAX_LIGHTS;
            case GL.MAX_CLIP_PLANES: return MAX_CLIP_PLANES;
            case GL.MAX_MODELVIEW_STACK_DEPTH: return MODELVIEW_STACK_DEPTH;
            case GL.MAX_PROJECTION_STACK_DEPTH: return OTHER_STACK_DEPTH;
            case GL.MAX_TEXTURE_STACK_DEPTH: return OTHER_STACK_DEPTH;
            case GL.MAX_TEXTURE_MAX_ANISOTROPY_EXT: return 16;
            case GL.MAX_ELEMENTS_VERTICES: return 65536;
            case GL.MAX_ELEMENTS_INDICES: return 65536;
            case GL.SAMPLE_BUFFERS: return 0;
            // GL_SAMPLES is zero when the selected framebuffer has no
            // multisample buffer; one is the raster sample count, not the GL
            // query value for this pixel format.
            case GL.SAMPLES: return 0;
            case GL.SUBPIXEL_BITS: return 8;
            case GL.RED_BITS: case GL.GREEN_BITS:
            case GL.BLUE_BITS: case GL.ALPHA_BITS: return 8;
            case GL.DEPTH_BITS: return 24;
            case GL.STENCIL_BITS: return 8;
            case GL.MAX_VIEWPORT_DIMS:
                return Math.min(16384, limits.maxTextureDimension2D || 8192);
            case GL.NUM_COMPRESSED_TEXTURE_FORMATS:
                return this.deviceFeatures.bc ? 4 : 0;
            case GL.PROGRAM_ERROR_POSITION_ARB:
                // This is the glGetIntegerv spelling, which is the one the
                // extension actually defines and the one the guest uses. It
                // must be -1 when the last glProgramStringARB succeeded: the
                // generic default of 0 reads as "an error at character 0", so
                // a conformance viewer reports a failure on a good program.
                return s && s.arbErrorPosition !== undefined ?
                    s.arbErrorPosition : -1;
            default: break;
            }
            if (!s) return null;
            switch (pname) {
            case GL.VIEWPORT: return s.viewport.width;
            case GL.ACTIVE_TEXTURE: return GL.TEXTURE0 + s.activeTexture;
            case GL.CLIENT_ACTIVE_TEXTURE:
                return GL.TEXTURE0 + s.clientActiveTexture;
            case GL.CURRENT_PROGRAM: return s.currentProgram;
            case GL.ARRAY_BUFFER_BINDING: return s.arrayBuffer;
            case GL.ELEMENT_ARRAY_BUFFER_BINDING: return s.elementArrayBuffer;
            case GL.PIXEL_PACK_BUFFER_BINDING: return s.pixelPackBuffer;
            case GL.PIXEL_UNPACK_BUFFER_BINDING: return s.pixelUnpackBuffer;
            case GL.FRAMEBUFFER_BINDING: return s.drawFramebuffer;
            case GL.RENDERBUFFER_BINDING: return s.renderbuffer;
            case GL.TEXTURE_BINDING_2D:
                return s.textureUnits[s.activeTexture].bindings[GL.TEXTURE_2D] || 0;
            case GL.MATRIX_MODE: return s.matrixMode;
            case GL.DEPTH_FUNC: return s.depthFunc;
            case GL.CULL_FACE_MODE: return s.cullFace;
            case GL.FRONT_FACE: return s.frontFace;
            case GL.SHADE_MODEL: return s.shadeModel;
            case GL.BLEND_SRC: return s.blend.srcRGB;
            case GL.BLEND_DST: return s.blend.dstRGB;
            case GL.ALPHA_TEST_FUNC: return s.alphaFunc.func;
            case GL.STENCIL_FUNC: return s.stencil.front.func;
            case GL.STENCIL_REF: return s.stencil.front.ref;
            case GL.STENCIL_VALUE_MASK: return s.stencil.front.valueMask;
            case GL.STENCIL_WRITEMASK: return s.stencil.front.writeMask;
            case GL.PACK_ALIGNMENT: return s.pixelStore.packAlignment;
            case GL.UNPACK_ALIGNMENT: return s.pixelStore.unpackAlignment;
            case GL.RENDER_MODE: return s.renderMode;
            default: return 0;
            }
        },
    });

    /* ================================================================== */
    /* Public API                                                         */
    /* ================================================================== */

    function installGLWebGPUExecutor(canvas, options) {
        const executor = new GLWebGPUExecutor(canvas, options);
        // Starting acquisition now rather than on the first batch matters:
        // the guest's synchronous queries are answered inside the port write
        // that delivered them, so the device has to already exist by then.
        executor.initialize().catch(() => { /* reported by initialize */ });
        return executor;
    }

    const api = {
        EXECUTOR_REVISION,
        GLWebGPUExecutor, installGLWebGPUExecutor,
        // Exported for the test suite.
        expandIndices, expandIndexArray, expandedIndexCount,
        convertPixels, decodeDXT, storageFormatFor, readSourceTexel,
        applyBaseFormat, normalMatrixOf, multiply4, invert4, transpose4,
        createContextState, buildHandlerTable,
        PRIMITIVE_TOPOLOGY, BLEND_FACTORS, COMPARE_FUNCTIONS,
        STENCIL_OPERATIONS, ADDRESS_MODES,
        GLWG_RESPONSE_REGION_BYTES, GLWG_QUERY_REGION_BYTES,
        GLWG_READBACK_REGION_OFFSET, GLWG_HEARTBEAT_OFFSET,
        GLWG_RESPONSE_PENDING, GLWG_RESPONSE_OK, GLWG_RESPONSE_FAILED,
        SYNC_STATUS_PENDING, SYNC_STATUS_OK, SYNC_STATUS_FAILED,
    };
    global.installGLWebGPUExecutor = installGLWebGPUExecutor;
    global.GLWebGPUExecutorModule = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
