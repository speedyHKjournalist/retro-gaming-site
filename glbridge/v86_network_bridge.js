// v86 net0 UDP -> OpenGL bridge.
// Guest opengl32.dll packs GL calls, this file decodes them and forwards them
// to a gl4es wasm/js module. gl4es owns the OpenGL fixed pipeline -> WebGL
// translation; this file only does packet parsing, canvas placement, and calls.

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

    const VGL_UDP_PORT = 46000;

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
    };

    function u16(a, o) { return a[o] | (a[o + 1] << 8); }
    function be16(a, o) { return (a[o] << 8) | a[o + 1]; }
    function u32(a, o) { return (a[o] | (a[o + 1] << 8) | (a[o + 2] << 16) | (a[o + 3] << 24)) >>> 0; }
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

        withHeapU32(values, callback) {
            const bytes = new Uint8Array(values.length * 4);
            const view = new DataView(bytes.buffer);
            for (let i = 0; i < values.length; i++) {
                view.setUint32(i * 4, values[i] >>> 0, true);
            }

            return this.withHeapBytes(bytes, callback);
        }

        glCall(fn, p) {
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
            case GLFN_TEX_COORD2F:
                this.callGL("TexCoord2f", [f32(p, 0), f32(p, 4)], ["number", "number"]);
                break;
            default:
                this.warnMissing("GL function id " + fn);
                break;
            }
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

        present() {
            this.callGL("Flush", [], []);
        }

        releaseCurrent() {
            this.callOptional(["v86glReleaseCurrent", "_v86glReleaseCurrent"], [], []);
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

        callOptional(names, args, argTypes) {
            const module = this.module;
            if (!module) {
                return false;
            }

            for (let i = 0; i < names.length; i++) {
                const fn = module[names[i]];
                if (typeof fn === "function") {
                    fn.apply(module, args);
                    return true;
                }
            }

            if (typeof module.ccall === "function") {
                const cName = names[0].charAt(0) === "_" ? names[0].slice(1) : names[0];
                try {
                    module.ccall(cName, null, argTypes || [], args || []);
                    return true;
                } catch (err) {
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
            this.packetCount = 0;
            this.byteCount = 0;
            this.chunkedCalls = Object.create(null);
            this.surface = { hwnd: 0, x: 0, y: 0, width: 0, height: 0 };
            this.container = canvas.parentElement;
            this.screenCanvas = this.findScreenCanvas();
            this.renderer = null;

            this.setRendererFromOptions();
            emulator.add_listener("net0-send", packet => this.pushEthernetFrame(packet));
            this.log("installed net0 UDP listener");
        }

        setRendererFromOptions() {
            const module = this.options.gl4es || global.GL4ES || global.gl4es;

            if (module && typeof module.then === "function") {
                module.then(resolved => this.setGL4ES(resolved));
                return;
            }

            if (module) {
                this.setGL4ES(module);
                return;
            }

            throw new Error("gl4es module is required: pass { gl4es } or set window.GL4ES/window.gl4es before installing v86gl");
        }

        setGL4ES(module) {
            this.renderer = new Gl4esRenderer(this.canvas, module, (...args) => this.log(...args));
            this.renderer.resize(this.surface.width || this.canvas.width || 640, this.surface.height || this.canvas.height || 480);
            this.log("using gl4es renderer");

            const pending = this.pendingPackets.splice(0);
            for (let i = 0; i < pending.length; i++) {
                this.dispatch(pending[i][0], pending[i][1]);
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

        pushEthernetFrame(packet) {
            const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);
            const ethHeaderSize = 14;

            if (bytes.length < ethHeaderSize) {
                return;
            }

            const etherType = be16(bytes, 12);
            if (etherType !== 0x0800) {
                return;
            }

            const ipOffset = ethHeaderSize;
            const version = bytes[ipOffset] >> 4;
            const ihl = (bytes[ipOffset] & 0x0F) * 4;

            if (version !== 4 || ihl < 20 || bytes.length < ipOffset + ihl + 8) {
                return;
            }

            const totalLength = be16(bytes, ipOffset + 2);
            const protocol = bytes[ipOffset + 9];
            if (protocol !== 17) {
                return;
            }

            const udpOffset = ipOffset + ihl;
            const srcPort = be16(bytes, udpOffset);
            const dstPort = be16(bytes, udpOffset + 2);
            if (srcPort !== VGL_UDP_PORT && dstPort !== VGL_UDP_PORT) {
                return;
            }

            const udpLength = be16(bytes, udpOffset + 4);
            if (udpLength < 8) {
                return;
            }

            const payloadOffset = udpOffset + 8;
            const ipEnd = Math.min(bytes.length, ipOffset + totalLength);
            const udpEnd = Math.min(ipEnd, udpOffset + udpLength);
            if (payloadOffset + 12 > udpEnd) {
                return;
            }

            if (bytes[payloadOffset] !== 0x56 ||
                bytes[payloadOffset + 1] !== 0x47 ||
                bytes[payloadOffset + 2] !== 0x4C ||
                bytes[payloadOffset + 3] !== 0x31) {
                return;
            }

            this.pushBytes(bytes.subarray(payloadOffset, udpEnd));
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

                const payload = this.buf.slice(12, total);
                this.buf.splice(0, total);
                this.packetCount++;
                this.log("packet", this.packetCount, OP_NAMES[op] || op, "payload", size, "bytes");
                this.dispatch(op, payload);
            }
        }

        dispatch(op, p) {
            if (!this.renderer) {
                this.pendingPackets.push([op, p]);
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
                this.dispatchGLFrame(p, true);
                break;
            case OP_GL_BATCH:
                this.dispatchGLFrame(p, false);
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
                this.present();
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

        dispatchGLFrame(p, shouldPresent) {
            let offset = 0;
            let commands = 0;
            const renderer = this.requireRenderer();

            while (offset + 4 <= p.length) {
                const fn = u16(p, offset);
                const size = u16(p, offset + 2);
                offset += 4;

                if (offset + size > p.length) {
                    console.warn("[v86gl] truncated GL frame command", fn, size, p.length - offset);
                    break;
                }

                const args = p.slice(offset, offset + size);
                offset += size;
                commands++;

                if (fn === GLFN_VIEWPORT && args.length >= 16) {
                    this.resize(i32(args, 8), i32(args, 12));
                }

                renderer.glCall(fn, args);
            }

            this.log("frame", commands, "commands", p.length, "bytes");
            if (shouldPresent) {
                this.present();
            }
        }

        dispatchGLChunk(p) {
            if (p.length < 16) {
                return;
            }

            const uploadId = u32(p, 0);
            const fn = u16(p, 4);
            const chunkSize = u16(p, 6);
            const totalSize = u32(p, 8);
            const offset = u32(p, 12);
            if (offset + chunkSize > totalSize || 16 + chunkSize > p.length) {
                console.warn("[v86gl] invalid GL chunk", uploadId, fn, offset, chunkSize, totalSize);
                return;
            }

            let upload = this.chunkedCalls[uploadId];
            if (!upload) {
                upload = {
                    fn,
                    totalSize,
                    received: 0,
                    data: new Uint8Array(totalSize),
                };
                this.chunkedCalls[uploadId] = upload;
            }

            if (upload.fn !== fn || upload.totalSize !== totalSize) {
                console.warn("[v86gl] mismatched GL chunk", uploadId, fn, totalSize);
                delete this.chunkedCalls[uploadId];
                return;
            }

            upload.data.set(p.slice(16, 16 + chunkSize), offset);
            upload.received += chunkSize;

            if (upload.received >= upload.totalSize) {
                delete this.chunkedCalls[uploadId];
                this.log("chunked gl", GLFN_NAMES[fn] || fn, upload.totalSize, "bytes");
                this.requireRenderer().glCall(fn, upload.data);
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

            this.canvas.style.position = "absolute";
            this.canvas.style.left = left + "px";
            this.canvas.style.top = top + "px";
            this.canvas.style.setProperty("width", width + "px", "important");
            this.canvas.style.setProperty("height", height + "px", "important");
            this.canvas.style.setProperty("max-width", "none", "important");
            this.canvas.style.setProperty("max-height", "none", "important");
        }

        present() {
            this.positionCanvas();
            this.requireRenderer().present();
            this.canvas.style.display = "block";
        }

        releaseCurrent() {
            this.requireRenderer().releaseCurrent();
            this.canvas.style.display = "none";
            this.log("released current context");
        }
    }

    global.installV86GLNetworkBridge = function(emulator, canvas, options) {
        return new V86GLNetworkBridge(emulator, canvas, options);
    };
})(typeof window !== "undefined" ? window : globalThis);
