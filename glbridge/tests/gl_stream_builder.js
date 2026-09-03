// Builds OpenGL command records the way openglproxy/opengl32_proxy.c does.
//
// The tests need to speak the guest's wire format exactly, so this encodes
// from the same signature table gl_executor.js decodes with. Anything the
// table cannot describe -- textures, draws, shader source -- gets an explicit
// builder here, and those are precisely the records worth hand-checking.

"use strict";

const wire = require("../gl-webgpu/gl_wire.js");
const constants = require("../gl-webgpu/gl_constants.js");

const GL = constants.GL;
const GLFN = constants.GLFN;

class GLStream {
    constructor() {
        this.buffer = new Uint8Array(4096);
        this.length = 0;
    }

    reserve(extra) {
        if (this.length + extra <= this.buffer.byteLength) return;
        let size = this.buffer.byteLength * 2;
        while (size < this.length + extra) size *= 2;
        const grown = new Uint8Array(size);
        grown.set(this.buffer.subarray(0, this.length));
        this.buffer = grown;
    }

    /*
     * Returns a view of the payload *inside* the assembled stream, because a
     * synchronous query is answered by writing into the record the guest
     * submitted. A test holding a detached copy would never see the answer,
     * which is the same mistake a bridge that copies the batch would make.
     */
    record(opcode, payloadBytes) {
        const payload = payloadBytes || new Uint8Array(0);
        const extended = payload.byteLength > 0xFFFE;
        this.reserve((extended ? 8 : 4) + payload.byteLength);
        const view = new DataView(this.buffer.buffer);
        view.setUint16(this.length, opcode, true);
        if (extended) {
            view.setUint16(this.length + 2, 0xFFFF, true);
            view.setUint32(this.length + 4, payload.byteLength, true);
            this.length += 8;
        } else {
            view.setUint16(this.length + 2, payload.byteLength, true);
            this.length += 4;
        }
        const at = this.length;
        this.buffer.set(payload, at);
        this.length += payload.byteLength;
        this.lastPayload = { offset: at, length: payload.byteLength };
        return this;
    }

    /* The payload of the record just appended, as a live view. */
    payloadView() {
        const { offset, length } = this.lastPayload;
        const stream = this;
        return {
            get bytes() {
                return stream.buffer.subarray(offset, offset + length);
            },
            get view() {
                return new DataView(stream.buffer.buffer, offset, length);
            },
        };
    }

    /* Encodes from the declarative signature, which is what keeps the tests
     * honest about the layout the executor expects. */
    call(name, ...args) {
        const signature = wire.SIGNATURES[name];
        if (!signature) throw new Error("no signature for " + name);
        const types = signature[1];
        const payload = new Uint8Array(wire.payloadBytes(types));
        const view = new DataView(payload.buffer);
        let offset = 0;
        for (let i = 0; i < types.length; ++i) {
            const value = args[i] === undefined ? 0 : args[i];
            switch (types[i]) {
            case "i": view.setInt32(offset, value | 0, true); offset += 4; break;
            case "u": view.setUint32(offset, value >>> 0, true); offset += 4; break;
            case "f": view.setFloat32(offset, value, true); offset += 4; break;
            default: view.setFloat64(offset, value, true); offset += 8; break;
            }
        }
        return this.record(GLFN[name], payload);
    }

    makeCurrent(hwnd, x, y, width, height, contextId, shareGroup) {
        const extended = contextId !== undefined || shareGroup !== undefined;
        const payload = new Uint8Array(extended ? 28 : 20);
        const view = new DataView(payload.buffer);
        view.setUint32(0, hwnd, true);
        view.setInt32(4, x, true);
        view.setInt32(8, y, true);
        view.setUint32(12, width, true);
        view.setUint32(16, height, true);
        if (extended) {
            view.setUint32(20, (contextId || 0) >>> 0, true);
            view.setUint32(24, (shareGroup || 0) >>> 0, true);
        }
        return this.record(constants.CTRL.MAKE_CURRENT, payload);
    }

    names(opcode, list) {
        const payload = new Uint8Array(4 + list.length * 4);
        const view = new DataView(payload.buffer);
        view.setUint32(0, list.length, true);
        list.forEach((name, i) => view.setUint32(4 + i * 4, name, true));
        return this.record(opcode, payload);
    }

    texImage2D(target, level, internalFormat, width, height, format, type, data) {
        const bytes = data || new Uint8Array(0);
        const payload = new Uint8Array(36 + bytes.byteLength);
        const view = new DataView(payload.buffer);
        let at = 0;
        for (const value of [target, level, internalFormat, width, height, 0,
                format, type, bytes.byteLength]) {
            view.setUint32(at, value >>> 0, true);
            at += 4;
        }
        payload.set(bytes, at);
        return this.record(GLFN.TEX_IMAGE_2D, payload.subarray(0, at + bytes.byteLength));
    }

    drawPixels(width, height, format, type, data) {
        const bytes = data || new Uint8Array(0);
        const payload = new Uint8Array(20 + bytes.byteLength);
        const view = new DataView(payload.buffer);
        view.setInt32(0, width, true);
        view.setInt32(4, height, true);
        view.setUint32(8, format, true);
        view.setUint32(12, type, true);
        view.setUint32(16, bytes.byteLength, true);
        payload.set(bytes, 20);
        return this.record(GLFN.DRAW_PIXELS, payload);
    }

    bitmap(width, height, xorig, yorig, xmove, ymove, data) {
        const bytes = data || new Uint8Array(0);
        const payload = new Uint8Array(28 + bytes.byteLength);
        const view = new DataView(payload.buffer);
        view.setInt32(0, width, true);
        view.setInt32(4, height, true);
        view.setFloat32(8, xorig, true);
        view.setFloat32(12, yorig, true);
        view.setFloat32(16, xmove, true);
        view.setFloat32(20, ymove, true);
        view.setUint32(24, bytes.byteLength, true);
        payload.set(bytes, 28);
        return this.record(GLFN.BITMAP, payload);
    }

    /*
     * The packed client-array draw. Thirteen blocks in the guest's order --
     * vertex, colour, normal, eight texture coordinates, secondary colour, fog
     * coordinate -- behind the 'CAMT' header.
     */
    drawArrays(mode, count, arrays) {
        const order = ["vertex", "color", "normal",
            "texCoord0", "texCoord1", "texCoord2", "texCoord3",
            "texCoord4", "texCoord5", "texCoord6", "texCoord7",
            "secondaryColor", "fogCoord"];
        const blocks = order.map(name => arrays[name] || null);
        let size = 20;
        for (const block of blocks)
            size += 20 + (block && block.data ? block.data.byteLength : 0);
        const payload = new Uint8Array(size);
        const view = new DataView(payload.buffer);
        view.setUint32(0, mode, true);
        view.setInt32(4, count, true);
        view.setUint32(8, 0x544D4143, true);
        view.setUint32(12, (8 | 0x80000000 | 0x40000000) >>> 0, true);
        view.setUint32(16, 0, true);
        let at = 20;
        for (const block of blocks) {
            const data = block && block.data ? block.data : null;
            view.setUint32(at, block ? 1 : 0, true);
            view.setInt32(at + 4, block ? block.size : 0, true);
            view.setUint32(at + 8, block ? block.type : 0, true);
            view.setInt32(at + 12, block ? (block.stride || 0) : 0, true);
            view.setUint32(at + 16, data ? data.byteLength : 0, true);
            at += 20;
            if (data) { payload.set(data, at); at += data.byteLength; }
        }
        return this.record(GLFN.DRAW_ARRAYS, payload);
    }

    /*
     * The indexed twin of drawArrays. It exists because the suite had none:
     * the multitexture header sits between the fixed header and the indices,
     * and with no indexed fixture nothing noticed that the executor was
     * slicing its indices from the wrong offset and drawing the magic word.
     */
    drawElements(mode, indices, indexType, arrays) {
        const order = ["vertex", "color", "normal",
            "texCoord0", "texCoord1", "texCoord2", "texCoord3",
            "texCoord4", "texCoord5", "texCoord6", "texCoord7",
            "secondaryColor", "fogCoord"];
        const blocks = order.map(name => arrays[name] || null);
        const count = indexType === GL.UNSIGNED_SHORT ? indices.byteLength / 2 :
            (indexType === GL.UNSIGNED_INT ? indices.byteLength / 4 :
                indices.byteLength);
        let size = 28 + indices.byteLength;
        for (const block of blocks)
            size += 20 + (block && block.data ? block.data.byteLength : 0);
        const payload = new Uint8Array(size);
        const view = new DataView(payload.buffer);
        view.setUint32(0, mode, true);
        view.setInt32(4, count, true);
        view.setUint32(8, indexType, true);
        view.setUint32(12, indices.byteLength, true);
        view.setUint32(16, 0x544D4143, true);
        view.setUint32(20, (8 | 0x80000000 | 0x40000000) >>> 0, true);
        view.setUint32(24, 0, true);
        let at = 28;
        payload.set(indices, at); at += indices.byteLength;
        for (const block of blocks) {
            const data = block && block.data ? block.data : null;
            view.setUint32(at, block ? 1 : 0, true);
            view.setInt32(at + 4, block ? block.size : 0, true);
            view.setUint32(at + 8, block ? block.type : 0, true);
            view.setInt32(at + 12, block ? (block.stride || 0) : 0, true);
            view.setUint32(at + 16, data ? data.byteLength : 0, true);
            at += 20;
            if (data) { payload.set(data, at); at += data.byteLength; }
        }
        return this.record(GLFN.DRAW_ELEMENTS, payload);
    }

    /*
     * The GL2 variants. The guest switches to these the moment any generic
     * vertex attribute array is enabled: same layout, plus a generic-attribute
     * count in the header and a block per enabled attribute after the
     * conventional ones. Each generic block is seven words -- index,
     * normalized, enabled, size, type, stride, byte count -- then its data.
     */
    drawElementsGL2(mode, indices, indexType, arrays, generics) {
        return this.packedGL2Draw(mode, indices, indexType, arrays,
            generics || []);
    }

    drawArraysGL2(mode, count, arrays, generics) {
        return this.packedGL2Draw(mode, count, null, arrays, generics || []);
    }

    packedGL2Draw(mode, indicesOrCount, indexType, arrays, generics) {
        const order = ["vertex", "color", "normal",
            "texCoord0", "texCoord1", "texCoord2", "texCoord3",
            "texCoord4", "texCoord5", "texCoord6", "texCoord7",
            "secondaryColor", "fogCoord"];
        const indexed = indexType !== null;
        const indices = indexed ? indicesOrCount : null;
        const count = indexed ?
            (indexType === GL.UNSIGNED_SHORT ? indices.byteLength / 2 :
                (indexType === GL.UNSIGNED_INT ? indices.byteLength / 4 :
                    indices.byteLength)) : indicesOrCount;
        const blocks = order.map(name => arrays[name] || null);
        const headerBytes = indexed ? 32 : 24;
        let size = headerBytes + (indexed ? indices.byteLength : 0);
        for (const block of blocks)
            size += 20 + (block && block.data ? block.data.byteLength : 0);
        for (const generic of generics)
            size += 28 + (generic.data ? generic.data.byteLength : 0);
        const payload = new Uint8Array(size);
        const view = new DataView(payload.buffer);
        view.setUint32(0, mode, true);
        view.setInt32(4, count, true);
        let at = 8;
        if (indexed) {
            view.setUint32(at, indexType, true);
            view.setUint32(at + 4, indices.byteLength, true);
            at += 8;
        }
        view.setUint32(at, 0x544D4143, true);
        view.setUint32(at + 4, (8 | 0x80000000 | 0x40000000) >>> 0, true);
        view.setUint32(at + 8, 0, true);
        view.setUint32(at + 12, generics.length, true);
        at += 16;
        if (indexed) { payload.set(indices, at); at += indices.byteLength; }
        for (const block of blocks) {
            const data = block && block.data ? block.data : null;
            view.setUint32(at, block ? 1 : 0, true);
            view.setInt32(at + 4, block ? block.size : 0, true);
            view.setUint32(at + 8, block ? block.type : 0, true);
            view.setInt32(at + 12, block ? (block.stride || 0) : 0, true);
            view.setUint32(at + 16, data ? data.byteLength : 0, true);
            at += 20;
            if (data) { payload.set(data, at); at += data.byteLength; }
        }
        for (const generic of generics) {
            const data = generic.data || null;
            view.setUint32(at, generic.index, true);
            view.setUint32(at + 4, generic.normalized ? 1 : 0, true);
            view.setUint32(at + 8, 1, true);
            view.setInt32(at + 12, generic.size, true);
            view.setUint32(at + 16, generic.type, true);
            view.setInt32(at + 20, generic.stride || 0, true);
            view.setUint32(at + 24, data ? data.byteLength : 0, true);
            at += 28;
            if (data) { payload.set(data, at); at += data.byteLength; }
        }
        return this.record(indexed ? GLFN.DRAW_ELEMENTS_GL2 :
            GLFN.DRAW_ARRAYS_GL2, payload);
    }
    shaderSource(shader, source) {
        const text = source + "\0";
        const payload = new Uint8Array(8 + text.length);
        const view = new DataView(payload.buffer);
        view.setUint32(0, shader, true);
        view.setUint32(4, text.length, true);
        for (let i = 0; i < text.length; ++i)
            payload[8 + i] = text.charCodeAt(i) & 0xff;
        return this.record(GLFN.SHADER_SOURCE, payload);
    }

    /* {target, size, usage, data_size} then the contents. */
    bufferData(target, byteCount, usage, data) {
        const bytes = data || new Uint8Array(0);
        const payload = new Uint8Array(16 + bytes.byteLength);
        const view = new DataView(payload.buffer);
        view.setUint32(0, target, true);
        view.setInt32(4, byteCount, true);
        view.setUint32(8, usage, true);
        view.setUint32(12, bytes.byteLength, true);
        payload.set(bytes, 16);
        return this.record(GLFN.BUFFER_DATA, payload);
    }

    /* The VBOPointerPayload the guest sends once the array lives in a buffer:
     * no data travels, only where in the bound buffer to read it. */
    pointerVBO(name, size, type, stride, offset) {
        const payload = new Uint8Array(16);
        const view = new DataView(payload.buffer);
        view.setInt32(0, size, true);
        view.setUint32(4, type, true);
        view.setInt32(8, stride, true);
        view.setUint32(12, offset, true);
        return this.record(GLFN[name], payload);
    }

    attribPointerVBO(index, size, type, normalized, stride, offset) {
        const payload = new Uint8Array(24);
        const view = new DataView(payload.buffer);
        view.setUint32(0, index, true);
        view.setInt32(4, size, true);
        view.setUint32(8, type, true);
        view.setUint32(12, normalized ? 1 : 0, true);
        view.setInt32(16, stride, true);
        view.setUint32(20, offset, true);
        return this.record(GLFN.VERTEX_ATTRIB_POINTER_VBO, payload);
    }

    drawElementsDirect(mode, count, type, offset, start, end) {
        const payload = new Uint8Array(24);
        const view = new DataView(payload.buffer);
        view.setUint32(0, mode, true);
        view.setUint32(4, start || 0, true);
        view.setUint32(8, end === undefined ? count : end, true);
        view.setInt32(12, count, true);
        view.setUint32(16, type, true);
        view.setUint32(20, offset, true);
        return this.record(GLFN.DRAW_ELEMENTS_DIRECT, payload);
    }

    /* {target, format, length, reserved} then the assembly text. */
    programStringARB(target, source) {
        const payload = new Uint8Array(16 + source.length);
        const view = new DataView(payload.buffer);
        view.setUint32(0, target, true);
        view.setUint32(4, GL.PROGRAM_FORMAT_ASCII_ARB, true);
        view.setInt32(8, source.length, true);
        for (let i = 0; i < source.length; ++i)
            payload[16 + i] = source.charCodeAt(i) & 0xff;
        return this.record(GLFN.PROGRAM_STRING_ARB, payload);
    }

    /* {kind, target, index, count} then count*4 floats -- the parameter kind
     * leads, and it is 1 for env and 2 for local. */
    programParameterARB(kind, target, index, values) {
        const payload = new Uint8Array(16 + values.length * 4);
        const view = new DataView(payload.buffer);
        view.setUint32(0, kind, true);
        view.setUint32(4, target, true);
        view.setUint32(8, index, true);
        view.setUint32(12, values.length / 4, true);
        values.forEach((value, i) => view.setFloat32(16 + i * 4, value, true));
        return this.record(GLFN.PROGRAM_PARAMETER_FV_ARB, payload);
    }

    queryProgramParameterARB(kind, target, index) {
        const payload = new Uint8Array(40);
        const view = new DataView(payload.buffer);
        view.setUint32(0, kind, true);
        view.setUint32(4, target, true);
        view.setUint32(8, index, true);
        view.setUint32(16, 16, true);
        this.record(GLFN.QUERY_PROGRAM_PARAMETER_FV_ARB, payload);
        return this.payloadView();
    }

    queryProgramStringARB(target, capacity) {
        const payload = new Uint8Array(24 + capacity);
        const view = new DataView(payload.buffer);
        view.setUint32(0, target, true);
        view.setUint32(4, GL.PROGRAM_STRING_ARB, true);
        view.setUint32(16, capacity, true);
        this.record(GLFN.QUERY_PROGRAM_STRING_ARB, payload);
        return this.payloadView();
    }

    queryObjectLog(kind, name, capacity) {
        const payload = new Uint8Array(24 + capacity);
        const view = new DataView(payload.buffer);
        view.setUint32(0, kind, true);
        view.setUint32(4, name, true);
        view.setUint32(8, capacity, true);
        view.setUint32(20, capacity, true);
        this.record(GLFN.QUERY_OBJECT_LOG, payload);
        return this.payloadView();
    }

    queryActive(kind, program, index, capacity) {
        const payload = new Uint8Array(40 + capacity);
        const view = new DataView(payload.buffer);
        view.setUint32(0, kind, true);
        view.setUint32(4, program, true);
        view.setUint32(8, index, true);
        view.setUint32(12, capacity, true);
        view.setUint32(32, capacity, true);
        this.record(GLFN.QUERY_ACTIVE, payload);
        return this.payloadView();
    }

    queryInteger(pname) {
        const payload = new Uint8Array(16);
        new DataView(payload.buffer).setUint32(0, pname, true);
        this.record(GLFN.QUERY_INTEGER, payload);
        return this.payloadView();
    }

    queryError() {
        const payload = new Uint8Array(16);
        this.record(GLFN.QUERY_ERROR, payload);
        return this.payloadView();
    }

    queryString(pname, capacity) {
        const payload = new Uint8Array(16 + capacity);
        const view = new DataView(payload.buffer);
        view.setUint32(0, pname, true);
        view.setUint32(12, capacity, true);
        this.record(GLFN.QUERY_GL_STRING, payload);
        return this.payloadView();
    }

    queryLocation(kind, program, name, capacity) {
        const text = name + "\0";
        const payload = new Uint8Array(32 + Math.max(capacity || 0, text.length));
        const view = new DataView(payload.buffer);
        view.setUint32(0, kind, true);
        view.setUint32(4, program, true);
        view.setUint32(28, text.length, true);
        for (let i = 0; i < text.length; ++i)
            payload[32 + i] = text.charCodeAt(i) & 0xff;
        this.record(GLFN.QUERY_LOCATION, payload);
        return this.payloadView();
    }

    queryObjectiv(kind, name, pname) {
        const payload = new Uint8Array(24);
        const view = new DataView(payload.buffer);
        view.setUint32(0, kind, true);
        view.setUint32(4, name, true);
        view.setUint32(8, pname, true);
        this.record(GLFN.QUERY_OBJECT_IV, payload);
        return this.payloadView();
    }

    uniformfv(location, components, count, values) {
        const payload = new Uint8Array(12 + values.length * 4);
        const view = new DataView(payload.buffer);
        view.setInt32(0, location, true);
        view.setInt32(4, components, true);
        view.setInt32(8, count, true);
        values.forEach((value, i) => view.setFloat32(12 + i * 4, value, true));
        return this.record(GLFN.UNIFORM_FV, payload);
    }

    uniformiv(location, components, count, values) {
        const payload = new Uint8Array(12 + values.length * 4);
        const view = new DataView(payload.buffer);
        view.setInt32(0, location, true);
        view.setInt32(4, components, true);
        view.setInt32(8, count, true);
        values.forEach((value, i) => view.setInt32(12 + i * 4, value | 0, true));
        return this.record(GLFN.UNIFORM_IV, payload);
    }

    bytes() {
        return this.buffer.subarray(0, this.length);
    }
}

module.exports = { GLStream, GL, GLFN };
