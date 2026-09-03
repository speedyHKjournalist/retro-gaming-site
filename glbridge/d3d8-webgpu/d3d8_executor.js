// D8WG high-level Direct3D 8 command executor.
//
// The guest DLL keeps COM objects, shadow state, Lock/Unlock memory and batching
// inside Windows XP. This host owns only WebGPU resources and immutable cache
// objects. It intentionally starts with the Maple-relevant pre-transformed
// vertex path instead of translating through WineD3D/OpenGL/gl4es.

(function(global) {
    "use strict";

    /*
     * The device and the canvas configuration belong to webgpu_host.js. See the
     * same note in d3d9_executor.js: a GPUCanvasContext belongs to whichever
     * device last configured it, so a backend that brings its own device makes
     * every other backend's swap-chain texture unusable. This path also used to
     * request a device with no optional features at all, which would have left
     * a shared device without BCn had it won the race.
     */
    /*
     * Resolved when an executor is constructed, not when this file is
     * evaluated: the page's script order is not this module's to depend on,
     * and reading global.V86GPUHost at load time silently yields null when
     * webgpu_host.js happens to come later -- which reads downstream as
     * "WebGPU is unavailable" rather than as the load-order mistake it is.
     */
    function resolveGPUHost() {
        if (global.V86GPUHost) return global.V86GPUHost;
        try {
            return typeof require === "function" ?
                require("../webgpu_host.js") : null;
        } catch (error) {
            return null;
        }
    }

    const D8WG_MAGIC = 0x47573844; // "D8WG"
    const D8WG_CHECKPOINT_MAGIC = 0x43533844; // "D8SC"
    const D8WG_CHECKPOINT_VERSION = 1;
    const D8WG_VERSION_MAJOR = 1;
    const D8WG_VERSION_MINOR = 7;
    const D8WG_BATCH_HEADER_BYTES = 32;
    const D8WG_COMMAND_HEADER_BYTES = 16;

    const OP_HELLO = 1;
    const OP_CREATE_DEVICE = 2;
    const OP_RESET = 3;
    const OP_PRESENT = 4;
    const OP_CLEAR = 5;
    const OP_BEGIN_SCENE = 6;
    const OP_END_SCENE = 7;
    const OP_UPDATE_SURFACE = 8;
    const OP_CREATE_BUFFER = 0x100;
    const OP_UPDATE_BUFFER = 0x101;
    const OP_DESTROY_RESOURCE = 0x103;
    const OP_CREATE_TEXTURE = 0x110;
    const OP_UPDATE_TEXTURE = 0x111;
    const OP_CREATE_VERTEX_SHADER = 0x120;
    const OP_CREATE_PIXEL_SHADER = 0x121;
    const OP_SET_RENDER_STATE = 0x200;
    const OP_SET_TEXTURE_STAGE_STATE = 0x201;
    const OP_SET_TEXTURE = 0x202;
    const OP_SET_VIEWPORT = 0x203;
    const OP_SET_TRANSFORM = 0x204;
    const OP_SET_MATERIAL = 0x205;
    const OP_SET_LIGHT = 0x206;
    const OP_LIGHT_ENABLE = 0x207;
    const OP_SET_STREAM_SOURCE = 0x208;
    const OP_SET_INDICES = 0x209;
    const OP_SET_VERTEX_FORMAT = 0x20A;
    const OP_SET_RENDER_TARGET = 0x20B;
    const OP_SET_VERTEX_SHADER = 0x20C;
    const OP_SET_PIXEL_SHADER = 0x20D;
    const OP_SET_VERTEX_SHADER_CONSTANT = 0x20E;
    const OP_SET_PIXEL_SHADER_CONSTANT = 0x20F;
    const OP_DRAW_PRIMITIVE = 0x300;
    const OP_DRAW_INDEXED_PRIMITIVE = 0x301;
    const OP_DRAW_PRIMITIVE_UP = 0x302;
    const OP_DRAW_INDEXED_PRIMITIVE_UP = 0x303;

    const RESOURCE_BUFFER_VERTEX = 1;
    const RESOURCE_BUFFER_INDEX = 2;
    const RESOURCE_TEXTURE_2D = 3;
    const RESOURCE_VERTEX_SHADER = 4;
    const RESOURCE_PIXEL_SHADER = 5;
    const D8WG_MAX_VS_CONSTANTS = 96;
    const D8WG_MAX_PS_CONSTANTS = 8;
    const D3DUSAGE_RENDERTARGET = 0x1;
    const D3DFMT_A8R8G8B8 = 21;
    const D3DFMT_X8R8G8B8 = 22;
    const D3DFMT_R5G6B5 = 23;
    const D3DFMT_X1R5G5B5 = 24;
    const D3DFMT_A1R5G5B5 = 25;
    const D3DFMT_A4R4G4B4 = 26;
    const D3DFMT_A8 = 28;
    const D3DFMT_L8 = 50;
    const D3DFMT_DXT1 = 0x31545844;
    const D3DFMT_DXT3 = 0x33545844;
    const D3DFMT_DXT5 = 0x35545844;
    const D3DFMT_INDEX16 = 101;
    const D3DFMT_INDEX32 = 102;
    const D3DCLEAR_TARGET = 0x1;
    const D3DCLEAR_ZBUFFER = 0x2;
    const D3DCLEAR_STENCIL = 0x4;
    const D3DFVF_POSITION_MASK = 0x00E;
    const D3DFVF_XYZ = 0x002;
    const D3DFVF_XYZRHW = 0x004;
    const D3DFVF_NORMAL = 0x010;
    const D3DFVF_PSIZE = 0x020;
    const D3DFVF_DIFFUSE = 0x040;
    const D3DFVF_SPECULAR = 0x080;
    const D3DFVF_TEXCOUNT_MASK = 0xF00;
    const D3DFVF_TEXCOUNT_SHIFT = 8;

    const D3DRS_ALPHATESTENABLE = 15;
    const D3DRS_SHADEMODE = 9;
    const D3DRS_ZENABLE = 7;
    const D3DRS_ZWRITEENABLE = 14;
    const D3DRS_SRCBLEND = 19;
    const D3DRS_DESTBLEND = 20;
    const D3DRS_CULLMODE = 22;
    const D3DRS_ZFUNC = 23;
    const D3DRS_ALPHAREF = 24;
    const D3DRS_ALPHAFUNC = 25;
    const D3DRS_ALPHABLENDENABLE = 27;
    const D3DRS_FOGENABLE = 28;
    const D3DRS_SPECULARENABLE = 29;
    const D3DRS_FOGCOLOR = 34;
    const D3DRS_FOGTABLEMODE = 35;
    const D3DRS_FOGSTART = 36;
    const D3DRS_FOGEND = 37;
    const D3DRS_FOGDENSITY = 38;
    const D3DRS_ZBIAS = 47;
    const D3DRS_RANGEFOGENABLE = 48;
    const D3DRS_STENCILENABLE = 52;
    const D3DRS_STENCILFAIL = 53;
    const D3DRS_STENCILZFAIL = 54;
    const D3DRS_STENCILPASS = 55;
    const D3DRS_STENCILFUNC = 56;
    const D3DRS_STENCILREF = 57;
    const D3DRS_STENCILMASK = 58;
    const D3DRS_STENCILWRITEMASK = 59;
    const D3DRS_TEXTUREFACTOR = 60;
    const D3DRS_FOGVERTEXMODE = 140;
    const D3DRS_LIGHTING = 137;
    const D3DRS_AMBIENT = 139;
    const D3DRS_COLORVERTEX = 141;
    const D3DRS_LOCALVIEWER = 142;
    const D3DRS_NORMALIZENORMALS = 143;
    const D3DRS_DIFFUSEMATERIALSOURCE = 145;
    const D3DRS_SPECULARMATERIALSOURCE = 146;
    const D3DRS_AMBIENTMATERIALSOURCE = 147;
    const D3DRS_EMISSIVEMATERIALSOURCE = 148;
    const D3DRS_COLORWRITEENABLE = 168;
    const D3DRS_BLENDOP = 171;
    const D3DCULL_NONE = 1;
    const D3DCULL_CCW = 3;

    const D3DTSS_COLOROP = 1;
    const D3DTSS_COLORARG1 = 2;
    const D3DTSS_COLORARG2 = 3;
    const D3DTSS_ALPHAOP = 4;
    const D3DTSS_ALPHAARG1 = 5;
    const D3DTSS_ALPHAARG2 = 6;
    const D3DTSS_TEXCOORDINDEX = 11;
    const D3DTSS_ADDRESSU = 13;
    const D3DTSS_ADDRESSV = 14;
    const D3DTSS_MAGFILTER = 16;
    const D3DTSS_MINFILTER = 17;
    const D3DTSS_MIPFILTER = 18;
    const D3DTSS_MAXMIPLEVEL = 20;
    const D3DTSS_MAXANISOTROPY = 21;
    const D3DTSS_TEXTURETRANSFORMFLAGS = 24;
    const D3DTSS_COLORARG0 = 26;
    const D3DTSS_ALPHAARG0 = 27;
    const D3DTSS_RESULTARG = 28;

    const D3DTOP_DISABLE = 1;
    const D3DTA_SELECTMASK = 0xF;
    const D3DTA_DIFFUSE = 0;
    const D3DTA_CURRENT = 1;
    const D3DTA_TEXTURE = 2;
    const D3DTA_TFACTOR = 3;
    const D3DTA_SPECULAR = 4;
    const D3DTA_TEMP = 5;
    const D3DTA_COMPLEMENT = 0x10;
    const D3DTA_ALPHAREPLICATE = 0x20;

    const BUFFER_USAGE_COPY_SRC = 0x04;
    const BUFFER_USAGE_COPY_DST = 0x08;
    const BUFFER_USAGE_INDEX = 0x10;
    const BUFFER_USAGE_VERTEX = 0x20;
    const BUFFER_USAGE_UNIFORM = 0x40;
    const TEXTURE_USAGE_COPY_DST = 0x02;
    const TEXTURE_USAGE_TEXTURE_BINDING = 0x04;
    const TEXTURE_USAGE_RENDER_ATTACHMENT = 0x10;
    const TRANSIENT_BUFFER_BYTES = 16 * 1024 * 1024;
    const D3DLOCK_DISCARD = 0x2000;

    // Stage 7: uniform ring. Uniform data is suballocated from one persistent
    // ring buffer and bound with a dynamic offset, instead of minting a fresh
    // GPUBuffer per state change. WebGPU requires a dynamic uniform offset to
    // be a multiple of minUniformBufferOffsetAlignment; 256 is the spec's
    // maximum allowed value for that limit, so it is safe on every adapter.
    const UNIFORM_OFFSET_ALIGNMENT = 256;
    const FIXED_UNIFORM_BYTES = 1392;
    const SHADER_UNIFORM_BYTES =
        (D8WG_MAX_VS_CONSTANTS + D8WG_MAX_PS_CONSTANTS) * 16;
    const UNIFORM_RING_BYTES = 4 * 1024 * 1024;

    function alignUniform(value) {
        return (value + (UNIFORM_OFFSET_ALIGNMENT - 1)) &
            ~(UNIFORM_OFFSET_ALIGNMENT - 1);
    }

    // Which render states actually appear in the fixed-function uniform block
    // (see uniformFor / SurfaceUniforms). Everything else is pipeline state and
    // must not invalidate the packed uniform slot. Indexed by D3DRENDERSTATETYPE
    // for an O(1) integer test on the hot command path.
    const UNIFORM_RENDER_STATES = new Uint8Array(256);
    for (const renderState of [
        D3DRS_TEXTUREFACTOR, D3DRS_ALPHAREF, D3DRS_FOGCOLOR, D3DRS_FOGSTART,
        D3DRS_FOGEND, D3DRS_FOGDENSITY, D3DRS_AMBIENT,
    ]) UNIFORM_RENDER_STATES[renderState] = 1;

    // Reused across every setBindGroup so the hot draw path allocates nothing.
    // WebGPU's (data, start, length) overload reads the offsets out of this
    // array synchronously, so a single shared instance is safe.
    const DYNAMIC_OFFSETS = new Uint32Array(1);
    let uniformRingSerial = 0;

    function u16(bytes, offset) {
        return bytes[offset] | bytes[offset + 1] << 8;
    }

    function u32(bytes, offset) {
        return (bytes[offset] | bytes[offset + 1] << 8 |
            bytes[offset + 2] << 16 | bytes[offset + 3] << 24) >>> 0;
    }

    function i32(bytes, offset) {
        return u32(bytes, offset) | 0;
    }

    function f32(bytes, offset) {
        return new DataView(bytes.buffer, bytes.byteOffset + offset, 4)
            .getFloat32(0, true);
    }

    function align4(value) {
        return (value + 3) & ~3;
    }

    function d3dColor(value) {
        return {
            r: ((value >>> 16) & 0xFF) / 255,
            g: ((value >>> 8) & 0xFF) / 255,
            b: (value & 0xFF) / 255,
            a: ((value >>> 24) & 0xFF) / 255,
        };
    }

    function primitiveInfo(type, primitiveCount) {
        switch (type >>> 0) {
        case 1: return { topology: "point-list", vertices: primitiveCount };
        case 2: return { topology: "line-list", vertices: primitiveCount * 2 };
        case 3: return { topology: "line-strip", vertices: primitiveCount + 1 };
        case 4: return { topology: "triangle-list", vertices: primitiveCount * 3 };
        case 5: return { topology: "triangle-strip", vertices: primitiveCount + 2 };
        case 6: return {
            topology: "triangle-list",
            vertices: primitiveCount + 2,
            fan: true,
            convertedIndices: primitiveCount * 3,
        };
        default: return null;
        }
    }

    function indexFormatInfo(format) {
        if ((format >>> 0) === D3DFMT_INDEX16) {
            return { webgpu: "uint16", bytes: 2, ArrayType: Uint16Array };
        }
        if ((format >>> 0) === D3DFMT_INDEX32) {
            return { webgpu: "uint32", bytes: 4, ArrayType: Uint32Array };
        }
        return null;
    }

    function checkedDataRange(bytes, offset, byteCount, label) {
        if (offset > bytes.length || byteCount > bytes.length - offset) {
            throw new Error(label + " range is outside its D8WG batch");
        }
        return bytes.subarray(offset, offset + byteCount);
    }

    function padded4(data) {
        if ((data.byteLength & 3) === 0) return data;
        const result = new Uint8Array(align4(data.byteLength));
        result.set(data);
        return result;
    }

    function textureFormatInfo(format) {
        switch (format >>> 0) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
            return { blockWidth: 1, blockHeight: 1, blockBytes: 4 };
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_A4R4G4B4:
            return { blockWidth: 1, blockHeight: 1, blockBytes: 2 };
        case D3DFMT_L8:
        case D3DFMT_A8:
            return { blockWidth: 1, blockHeight: 1, blockBytes: 1 };
        case D3DFMT_DXT1:
            return { blockWidth: 4, blockHeight: 4, blockBytes: 8, dxt: 1 };
        case D3DFMT_DXT3:
            return { blockWidth: 4, blockHeight: 4, blockBytes: 16, dxt: 3 };
        case D3DFMT_DXT5:
            return { blockWidth: 4, blockHeight: 4, blockBytes: 16, dxt: 5 };
        default:
            return null;
        }
    }

    function expand5(value) { return value * 255 / 31 | 0; }
    function expand6(value) { return value * 255 / 63 | 0; }

    function dxtColours(source, offset, allowTransparent) {
        const c0 = source[offset] | source[offset + 1] << 8;
        const c1 = source[offset + 2] | source[offset + 3] << 8;
        const first = [expand5(c0 >>> 11), expand6((c0 >>> 5) & 63),
            expand5(c0 & 31), 255];
        const second = [expand5(c1 >>> 11), expand6((c1 >>> 5) & 63),
            expand5(c1 & 31), 255];
        const colours = [first, second];
        if (allowTransparent && c0 <= c1) {
            colours.push(first.map((value, i) => i === 3 ? 255 :
                (value + second[i]) / 2 | 0));
            colours.push([0, 0, 0, 0]);
        } else {
            colours.push(first.map((value, i) => i === 3 ? 255 :
                (2 * value + second[i]) / 3 | 0));
            colours.push(first.map((value, i) => i === 3 ? 255 :
                (value + 2 * second[i]) / 3 | 0));
        }
        return colours;
    }

    function dxt5Alphas(source, offset) {
        const a0 = source[offset];
        const a1 = source[offset + 1];
        const values = [a0, a1];
        if (a0 > a1) {
            for (let i = 1; i <= 6; i++)
                values.push(((7 - i) * a0 + i * a1) / 7 | 0);
        } else {
            for (let i = 1; i <= 4; i++)
                values.push(((5 - i) * a0 + i * a1) / 5 | 0);
            values.push(0, 255);
        }
        return values;
    }

    function decodeDXT(format, source, width, height, rowPitch) {
        const info = textureFormatInfo(format);
        const output = new Uint8Array(width * height * 4);
        const blockColumns = Math.ceil(width / 4);
        const blockRows = Math.ceil(height / 4);
        for (let by = 0; by < blockRows; by++) {
            for (let bx = 0; bx < blockColumns; bx++) {
                const block = by * rowPitch + bx * info.blockBytes;
                const colourOffset = block + (info.dxt === 1 ? 0 : 8);
                const colours = dxtColours(source, colourOffset,
                    info.dxt === 1);
                const colourBits = u32(source, colourOffset + 4);
                const alphaValues = info.dxt === 5 ? dxt5Alphas(source, block) : null;
                for (let py = 0; py < 4; py++) {
                    for (let px = 0; px < 4; px++) {
                        const x = bx * 4 + px;
                        const y = by * 4 + py;
                        if (x >= width || y >= height) continue;
                        const pixel = py * 4 + px;
                        const colour = colours[(colourBits >>> (pixel * 2)) & 3];
                        const destination = (y * width + x) * 4;
                        output[destination] = colour[0];
                        output[destination + 1] = colour[1];
                        output[destination + 2] = colour[2];
                        if (info.dxt === 3) {
                            const packed = source[block + (pixel >>> 1)];
                            const nibble = pixel & 1 ? packed >>> 4 : packed & 15;
                            output[destination + 3] = nibble * 17;
                        } else if (info.dxt === 5) {
                            const bit = pixel * 3;
                            const byte = bit >>> 3;
                            const shift = bit & 7;
                            const packed = source[block + 2 + byte] |
                                (source[block + 3 + byte] || 0) << 8;
                            output[destination + 3] = alphaValues[(packed >>> shift) & 7];
                        } else {
                            output[destination + 3] = colour[3];
                        }
                    }
                }
            }
        }
        return output;
    }

    function decodeTextureUpload(format, source, width, height, rowPitch) {
        const info = textureFormatInfo(format);
        if (!info) throw new Error("unsupported D3D8 texture format " + format);
        if (info.dxt) return decodeDXT(format, source, width, height, rowPitch);
        const output = new Uint8Array(width * height * 4);
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const sourceOffset = y * rowPitch + x * info.blockBytes;
                const destination = (y * width + x) * 4;
                let r, g, b, a;
                if (format === D3DFMT_A8R8G8B8 || format === D3DFMT_X8R8G8B8) {
                    b = source[sourceOffset];
                    g = source[sourceOffset + 1];
                    r = source[sourceOffset + 2];
                    a = format === D3DFMT_A8R8G8B8 ? source[sourceOffset + 3] : 255;
                } else if (format === D3DFMT_L8) {
                    r = g = b = source[sourceOffset]; a = 255;
                } else if (format === D3DFMT_A8) {
                    r = g = b = 255; a = source[sourceOffset];
                } else {
                    const value = source[sourceOffset] | source[sourceOffset + 1] << 8;
                    if (format === D3DFMT_R5G6B5) {
                        r = expand5(value >>> 11); g = expand6((value >>> 5) & 63);
                        b = expand5(value & 31); a = 255;
                    } else if (format === D3DFMT_A4R4G4B4) {
                        a = (value >>> 12) * 17; r = ((value >>> 8) & 15) * 17;
                        g = ((value >>> 4) & 15) * 17; b = (value & 15) * 17;
                    } else {
                        a = format === D3DFMT_A1R5G5B5 && (value & 0x8000) ? 255 :
                            format === D3DFMT_X1R5G5B5 ? 255 : 0;
                        r = expand5((value >>> 10) & 31);
                        g = expand5((value >>> 5) & 31);
                        b = expand5(value & 31);
                    }
                }
                output[destination] = r;
                output[destination + 1] = g;
                output[destination + 2] = b;
                output[destination + 3] = a;
            }
        }
        return output;
    }

    function parseFVF(fvf, stride) {
        const position = fvf & D3DFVF_POSITION_MASK;
        if (position !== D3DFVF_XYZ && position !== D3DFVF_XYZRHW)
            return null;
        const pretransformed = position === D3DFVF_XYZRHW;
        const attributes = [{ shaderLocation: 0, offset: 0,
            format: pretransformed ? "float32x4" : "float32x3" }];
        let offset = pretransformed ? 16 : 12;
        const result = { attributes, diffuse: false, specular: false,
            normal: false, pointSize: false,
            texDims: [], minimumStride: offset, pretransformed };
        if (fvf & D3DFVF_NORMAL) {
            if (pretransformed) return null;
            attributes.push({ shaderLocation: 5, offset,
                format: "float32x3" });
            result.normal = true;
            offset += 12;
        }
        if (fvf & D3DFVF_PSIZE) {
            attributes.push({ shaderLocation: 6, offset, format: "float32" });
            result.pointSize = true;
            offset += 4;
        }
        if (fvf & D3DFVF_DIFFUSE) {
            attributes.push({ shaderLocation: 1, offset, format: "unorm8x4" });
            result.diffuse = true;
            offset += 4;
        }
        if (fvf & D3DFVF_SPECULAR) {
            attributes.push({ shaderLocation: 2, offset, format: "unorm8x4" });
            result.specular = true;
            offset += 4;
        }
        const textureCount = (fvf & D3DFVF_TEXCOUNT_MASK) >>>
            D3DFVF_TEXCOUNT_SHIFT;
        if (textureCount > 2) return null;
        for (let stage = 0; stage < textureCount; stage++) {
            const code = (fvf >>> (16 + stage * 2)) & 3;
            const dimensions = [2, 3, 4, 1][code];
            result.texDims.push(dimensions);
            attributes.push({ shaderLocation: 3 + stage, offset,
                format: dimensions === 1 ? "float32" : "float32x" + dimensions });
            offset += dimensions * 4;
        }
        result.minimumStride = offset;
        return stride >= offset ? result : null;
    }

    function textureArgument(argument, stage) {
        let expression;
        switch (argument & D3DTA_SELECTMASK) {
        case D3DTA_DIFFUSE: expression = "input.diffuse"; break;
        case D3DTA_CURRENT: expression = "current"; break;
        case D3DTA_TEXTURE: expression = "stage" + stage + "Texture"; break;
        case D3DTA_TFACTOR: expression = "textureFactor"; break;
        case D3DTA_SPECULAR: expression = "input.specular"; break;
        case D3DTA_TEMP: expression = "temporary"; break;
        default: expression = "current"; break;
        }
        if (argument & D3DTA_COMPLEMENT)
            expression = "(vec4<f32>(1.0) - " + expression + ")";
        if (argument & D3DTA_ALPHAREPLICATE)
            expression = "(" + expression + ").aaaa";
        return expression;
    }

    function textureOperation(operation, one, two, zero, stage) {
        const texture = "stage" + stage + "Texture";
        switch (operation >>> 0) {
        case 2: return one;
        case 3: return two;
        case 4: return "(" + one + " * " + two + ")";
        case 5: return "(" + one + " * " + two + " * 2.0)";
        case 6: return "(" + one + " * " + two + " * 4.0)";
        case 7: return "(" + one + " + " + two + ")";
        case 8: return "(" + one + " + " + two + " - vec4<f32>(0.5))";
        case 9: return "((" + one + " + " + two + " - vec4<f32>(0.5)) * 2.0)";
        case 10: return "(" + one + " - " + two + ")";
        case 11: return "(" + one + " + " + two + " * (vec4<f32>(1.0) - " + one + "))";
        case 12: return "mix(" + two + ", " + one + ", input.diffuse.aaaa)";
        case 13: return "mix(" + two + ", " + one + ", " + texture + ".aaaa)";
        case 14: return "mix(" + two + ", " + one + ", textureFactor.aaaa)";
        case 15: return "(" + one + " + " + two + " * (vec4<f32>(1.0) - " + texture + ".aaaa))";
        case 16: return "mix(" + two + ", " + one + ", current.aaaa)";
        case 18: return "vec4<f32>(" + one + ".rgb + " + one + ".aaa * " + two + ".rgb, " + one + ".a)";
        case 19: return "vec4<f32>(" + one + ".rgb * " + two + ".rgb + " + one + ".aaa, " + one + ".a)";
        case 20: return "vec4<f32>(" + one + ".rgb + (vec3<f32>(1.0) - " + one + ".aaa) * " + two + ".rgb, " + one + ".a)";
        case 21: return "vec4<f32>((vec3<f32>(1.0) - " + one + ".rgb) * " + two + ".rgb + " + one + ".aaa, " + one + ".a)";
        case 24: return "vec4<f32>(vec3<f32>(dot((" + one + ".rgb - vec3<f32>(0.5)) * 2.0, (" + two + ".rgb - vec3<f32>(0.5)) * 2.0)), 1.0)";
        case 25: return "(" + one + " * " + two + " + " + zero + ")";
        case 26: return "mix(" + two + ", " + one + ", " + zero + ")";
        default: return one;
        }
    }

    function alphaTestDiscard(func) {
        const alpha = "round(clamp(current.a, 0.0, 1.0) * 255.0)";
        const ref = "surface.alpha_ref";
        switch (func >>> 0) {
        case 1: return "true";
        case 2: return alpha + " >= " + ref;
        case 3: return alpha + " != " + ref;
        case 4: return alpha + " > " + ref;
        case 5: return alpha + " <= " + ref;
        case 6: return alpha + " == " + ref;
        case 7: return alpha + " < " + ref;
        case 8: return "false";
        default: return "false";
        }
    }

    function compareFunction(value) {
        return ({ 1: "never", 2: "less", 3: "equal", 4: "less-equal",
            5: "greater", 6: "not-equal", 7: "greater-equal",
            8: "always" })[value >>> 0] || "always";
    }

    function stencilOperation(value) {
        return ({ 1: "keep", 2: "zero", 3: "replace",
            4: "increment-clamp", 5: "decrement-clamp", 6: "invert",
            7: "increment-wrap", 8: "decrement-wrap" })[value >>> 0] || "keep";
    }

    function dwordFloat(value) {
        const bits = new Uint32Array(1);
        bits[0] = value >>> 0;
        return new Float32Array(bits.buffer)[0];
    }

    function blendFactor(value) {
        return ({ 1: "zero", 2: "one", 3: "src", 4: "one-minus-src",
            5: "src-alpha", 6: "one-minus-src-alpha", 7: "dst-alpha",
            8: "one-minus-dst-alpha", 9: "dst", 10: "one-minus-dst",
            11: "src-alpha-saturated", 12: "src-alpha",
            13: "one-minus-src-alpha" })[value >>> 0] || "one";
    }

    function blendState(state) {
        let source = state.renderStates[D3DRS_SRCBLEND] >>> 0;
        let destination = state.renderStates[D3DRS_DESTBLEND] >>> 0;
        if (source === 12) {
            source = 5;
            destination = 6;
        } else if (source === 13) {
            source = 6;
            destination = 5;
        }
        return {
            color: { operation: blendOperation(
                state.renderStates[D3DRS_BLENDOP]), srcFactor: blendFactor(source),
                dstFactor: blendFactor(destination) },
            alpha: { operation: blendOperation(
                state.renderStates[D3DRS_BLENDOP]), srcFactor: blendFactor(source),
                dstFactor: blendFactor(destination) },
        };
    }

    function blendOperation(value) {
        return ({ 1: "add", 2: "subtract", 3: "reverse-subtract",
            4: "min", 5: "max" })[value >>> 0] || "add";
    }

    function materialSource(state, renderState, uniformName) {
        if (!state.renderStates[D3DRS_COLORVERTEX])
            return "surface." + uniformName;
        switch (state.renderStates[renderState] >>> 0) {
        case 1: return "vertex_diffuse";
        case 2: return "vertex_specular";
        default: return "surface." + uniformName;
        }
    }

    function fixedFunctionShader(state, layout) {
        const inputs = ["    @location(0) position: " +
            (layout.pretransformed ? "vec4<f32>," : "vec3<f32>,")];
        if (layout.normal)
            inputs.push("    @location(5) normal: vec3<f32>,");
        if (layout.diffuse) inputs.push("    @location(1) diffuse_bgra: vec4<f32>,");
        if (layout.specular) inputs.push("    @location(2) specular_bgra: vec4<f32>,");
        for (let stage = 0; stage < layout.texDims.length; stage++) {
            const dimensions = layout.texDims[stage];
            inputs.push("    @location(" + (3 + stage) + ") tex" + stage +
                ": " + (dimensions === 1 ? "f32" : "vec" + dimensions + "<f32>") + ",");
        }
        const vertexAssignments = [
            "    let vertex_diffuse = " + (layout.diffuse ?
                "input.diffuse_bgra.bgra;" : "vec4<f32>(1.0);"),
            "    let vertex_specular = " + (layout.specular ?
                "input.specular_bgra.bgra;" : "vec4<f32>(0.0);"),
            "    output.diffuse = vertex_diffuse;",
            "    output.specular = vertex_specular;",
        ];
        for (let stage = 0; stage < 2; stage++) {
            const texcoordIndex = state.textureStageStates[stage]
                [D3DTSS_TEXCOORDINDEX] >>> 0;
            const coordinateSet = texcoordIndex & 0xFFFF;
            const dimensions = coordinateSet < layout.texDims.length ?
                layout.texDims[coordinateSet] : 0;
            const inputName = "input.tex" + coordinateSet;
            const generated = texcoordIndex & 0xFFFF0000;
            let source = !dimensions ? "vec4<f32>(0.0, 0.0, 0.0, 1.0)" :
                dimensions === 1 ? "vec4<f32>(" + inputName +
                    ", 0.0, 0.0, 1.0)" :
                dimensions === 2 ? "vec4<f32>(" + inputName +
                    ", 0.0, 1.0)" :
                dimensions === 3 ? "vec4<f32>(" + inputName +
                    ", 1.0)" : inputName;
            if (generated === 0x20000 && !layout.pretransformed)
                source = "eye_position";
            else if (generated === 0x10000 && layout.normal)
                source = "vec4<f32>(eye_normal, 1.0)";
            else if (generated === 0x30000 && layout.normal)
                source = "vec4<f32>(reflect(normalize(eye_position.xyz), eye_normal), 1.0)";
            const transformFlags = state.textureStageStates[stage]
                [D3DTSS_TEXTURETRANSFORMFLAGS] >>> 0;
            if (transformFlags & 0xFF) {
                const transformed = "transformed_tex" + stage;
                vertexAssignments.push("    let " + transformed +
                    " = surface.texture_transforms[" + stage + "] * " +
                    source + ";");
                if (transformFlags & 0x100) {
                    const component = (transformFlags & 0xFF) >= 4 ? "w" :
                        (transformFlags & 0xFF) === 3 ? "z" : "y";
                    vertexAssignments.push("    output.tex" + stage + " = " +
                        transformed + ".xy / max(0.000001, abs(" +
                        transformed + "." + component + ")) * sign(" +
                        transformed + "." + component + ");");
                } else {
                    vertexAssignments.push("    output.tex" + stage +
                        " = " + transformed + ".xy;");
                }
            } else {
                vertexAssignments.push("    output.tex" + stage + " = " +
                    source + ".xy;");
            }
        }
        if (state.renderStates[D3DRS_LIGHTING] && layout.normal) {
            const materialDiffuse = materialSource(state,
                D3DRS_DIFFUSEMATERIALSOURCE, "material_diffuse");
            const materialSpecular = materialSource(state,
                D3DRS_SPECULARMATERIALSOURCE, "material_specular");
            const materialAmbient = materialSource(state,
                D3DRS_AMBIENTMATERIALSOURCE, "material_ambient");
            const materialEmissive = materialSource(state,
                D3DRS_EMISSIVEMATERIALSOURCE, "material_emissive");
            const viewer = state.renderStates[D3DRS_LOCALVIEWER] ?
                "normalize(-eye_position.xyz)" : "vec3<f32>(0.0, 0.0, -1.0)";
            vertexAssignments.push(`
    let active_material_diffuse = ${materialDiffuse};
    let active_material_specular = ${materialSpecular};
    let active_material_ambient = ${materialAmbient};
    let active_material_emissive = ${materialEmissive};
    let viewer_direction = ${viewer};
    var lit_diffuse = active_material_emissive +
        active_material_ambient * surface.global_ambient;
    var lit_specular = vec4<f32>(0.0);
    for (var light_index: u32 = 0u; light_index < 8u; light_index++) {
        let light = surface.lights[light_index];
        if (light.spot_angles_enabled.z > 0.5) {
            let eye_light_direction = normalize((surface.view *
                vec4<f32>(light.direction_range.xyz, 0.0)).xyz);
            var to_light = -eye_light_direction;
            var attenuation = 1.0;
            if (light.position_type.w < 2.5) {
                let eye_light_position = (surface.view *
                    vec4<f32>(light.position_type.xyz, 1.0)).xyz;
                let delta = eye_light_position - eye_position.xyz;
                let distance = length(delta);
                to_light = delta / max(distance, 0.000001);
                attenuation = select(0.0, 1.0 / max(0.000001,
                    light.attenuation_falloff.x +
                    light.attenuation_falloff.y * distance +
                    light.attenuation_falloff.z * distance * distance),
                    distance <= light.direction_range.w);
                if (light.position_type.w > 1.5) {
                    let rho = dot(-to_light, eye_light_direction);
                    let outer = cos(light.spot_angles_enabled.y * 0.5);
                    let inner = cos(light.spot_angles_enabled.x * 0.5);
                    attenuation *= pow(clamp((rho - outer) /
                        max(0.000001, inner - outer), 0.0, 1.0),
                        max(0.0, light.attenuation_falloff.w));
                }
            }
            let n_dot_l = max(dot(eye_normal, to_light), 0.0);
            lit_diffuse += attenuation *
                (active_material_ambient * light.ambient +
                 active_material_diffuse * light.diffuse * n_dot_l);
            if (n_dot_l > 0.0 && surface.material_params.x > 0.0) {
                let halfway = normalize(to_light + viewer_direction);
                let specular_factor = pow(max(dot(eye_normal, halfway), 0.0),
                    surface.material_params.x);
                lit_specular += attenuation * active_material_specular *
                    light.specular * specular_factor;
            }
        }
    }
    output.diffuse = vec4<f32>(clamp(lit_diffuse.rgb,
        vec3<f32>(0.0), vec3<f32>(1.0)), active_material_diffuse.a);
    output.specular = clamp(lit_specular, vec4<f32>(0.0), vec4<f32>(1.0));`);
        }
        const fragment = [
            "    let textureFactor = surface.texture_factor;",
            "    var current = input.diffuse;",
            "    var temporary = vec4<f32>(0.0);",
            "    let stage0Texture = textureSample(texture0, sampler0, input.tex0);",
            "    let stage1Texture = textureSample(texture1, sampler1, input.tex1);",
        ];
        for (let stage = 0; stage < 2; stage++) {
            const values = state.textureStageStates[stage];
            const colorOp = values[D3DTSS_COLOROP] >>> 0;
            if (colorOp === D3DTOP_DISABLE) break;
            const color1 = textureArgument(values[D3DTSS_COLORARG1], stage);
            const color2 = textureArgument(values[D3DTSS_COLORARG2], stage);
            const color0 = textureArgument(values[D3DTSS_COLORARG0], stage);
            const alphaOp = values[D3DTSS_ALPHAOP] >>> 0;
            const alpha1 = textureArgument(values[D3DTSS_ALPHAARG1], stage);
            const alpha2 = textureArgument(values[D3DTSS_ALPHAARG2], stage);
            const alpha0 = textureArgument(values[D3DTSS_ALPHAARG0], stage);
            fragment.push("    let stage" + stage + "Color = " +
                textureOperation(colorOp, color1, color2, color0, stage) + ";");
            fragment.push("    let stage" + stage + "Alpha = " +
                (alphaOp === D3DTOP_DISABLE ? "current" :
                    textureOperation(alphaOp, alpha1, alpha2, alpha0, stage)) + ";");
            const destination = (values[D3DTSS_RESULTARG] & D3DTA_SELECTMASK) ===
                D3DTA_TEMP ? "temporary" : "current";
            fragment.push("    " + destination + " = clamp(vec4<f32>(stage" +
                stage + "Color.rgb, stage" + stage + "Alpha.a), " +
                "vec4<f32>(0.0), vec4<f32>(1.0));");
        }
        if (state.renderStates[D3DRS_SPECULARENABLE])
            fragment.push("    current = vec4<f32>(clamp(current.rgb + input.specular.rgb, vec3<f32>(0.0), vec3<f32>(1.0)), current.a);");
        if (state.renderStates[D3DRS_ALPHATESTENABLE])
            fragment.push("    if (" + alphaTestDiscard(
                state.renderStates[D3DRS_ALPHAFUNC]) + ") { discard; }");
        if (state.renderStates[D3DRS_FOGENABLE]) {
            const fogMode = state.renderStates[D3DRS_FOGTABLEMODE] ||
                state.renderStates[D3DRS_FOGVERTEXMODE];
            if (fogMode === 1)
                fragment.push("    let fog_factor = clamp(exp(-surface.fog_params.z * input.fog_depth), 0.0, 1.0);");
            else if (fogMode === 2)
                fragment.push("    let fog_factor = clamp(exp(-pow(surface.fog_params.z * input.fog_depth, 2.0)), 0.0, 1.0);");
            else
                fragment.push("    let fog_factor = clamp((surface.fog_params.y - input.fog_depth) / max(0.000001, surface.fog_params.y - surface.fog_params.x), 0.0, 1.0);");
            fragment.push("    current = vec4<f32>(mix(surface.fog_color.rgb, current.rgb, fog_factor), current.a);");
        }
        fragment.push("    return current;");
        const positionAssignment = layout.pretransformed ? `
    let pixel = input.position.xy - vec2<f32>(0.5, 0.5);
    let rhw = select(1.0, input.position.w, abs(input.position.w) > 0.000001);
    let clipW = 1.0 / rhw;
    output.position = vec4<f32>((pixel.x * surface.inverse_size.x * 2.0 - 1.0) * clipW,
        (1.0 - pixel.y * surface.inverse_size.y * 2.0) * clipW,
        clamp(input.position.z, 0.0, 1.0) * clipW, clipW);` : `
    let world_position = surface.world * vec4<f32>(input.position, 1.0);
    let eye_position = surface.view * world_position;
    output.position = surface.projection * eye_position;
    output.fog_depth = ${state.renderStates[D3DRS_RANGEFOGENABLE] ?
        "length(eye_position.xyz)" : "abs(eye_position.z)"};`;
        const normalAssignment = layout.normal ? `
    let eye_normal_value = (surface.view * surface.world *
        vec4<f32>(input.normal, 0.0)).xyz;
    let eye_normal = ${state.renderStates[D3DRS_NORMALIZENORMALS] ?
        "normalize(eye_normal_value)" : "eye_normal_value"};` : "";
        const interpolation = state.renderStates[D3DRS_SHADEMODE] === 1 ?
            " @interpolate(flat)" : "";
        return `
struct LightUniform {
    diffuse: vec4<f32>, specular: vec4<f32>, ambient: vec4<f32>,
    position_type: vec4<f32>, direction_range: vec4<f32>,
    attenuation_falloff: vec4<f32>, spot_angles_enabled: vec4<f32>,
};
struct SurfaceUniforms {
    size: vec2<f32>, inverse_size: vec2<f32>,
    texture_factor: vec4<f32>, alpha_ref: f32,
    padding0: f32, padding1: f32, padding2: f32,
    world: mat4x4<f32>, view: mat4x4<f32>, projection: mat4x4<f32>,
    fog_color: vec4<f32>, fog_params: vec4<f32>,
    material_diffuse: vec4<f32>, material_ambient: vec4<f32>,
    material_specular: vec4<f32>, material_emissive: vec4<f32>,
    material_params: vec4<f32>, global_ambient: vec4<f32>,
    lights: array<LightUniform, 8>,
    texture_transforms: array<mat4x4<f32>, 2>,
};
@group(0) @binding(0) var<uniform> surface: SurfaceUniforms;
@group(0) @binding(1) var texture0: texture_2d<f32>;
@group(0) @binding(2) var sampler0: sampler;
@group(0) @binding(3) var texture1: texture_2d<f32>;
@group(0) @binding(4) var sampler1: sampler;
struct VSInput {
${inputs.join("\n")}
};
struct VSOutput {
    @builtin(position) position: vec4<f32>,
    @location(0)${interpolation} diffuse: vec4<f32>,
    @location(1)${interpolation} specular: vec4<f32>,
    @location(2) tex0: vec2<f32>,
    @location(3) tex1: vec2<f32>,
    @location(4) fog_depth: f32,
};
@vertex fn vs_main(input: VSInput) -> VSOutput {
    var output: VSOutput;
${positionAssignment}
${layout.pretransformed ? "    output.fog_depth = input.position.z;" : ""}
${normalAssignment}
${vertexAssignments.join("\n")}
    return output;
}
@fragment fn fs_main(input: VSOutput) -> @location(0) vec4<f32> {
${fragment.join("\n")}
}
`;
    }

    // ---- Stage 6: D3D8 shader model 1.x bytecode -> WGSL translation ----
    //
    // Token layout constants mirror the real D3D8 SDK bit patterns exactly
    // (see d3d8types.h) since guest-compiled shader bytecode is genuine
    // Microsoft-format SM1.x tokens, not a project-invented encoding. The
    // guest (d3d8_proxy.c: validate_shader_body) enforces the identical
    // supported-instruction table before a shader is ever created, so this
    // translator should never actually see an unsupported opcode from a
    // well-behaved guest build -- but it re-checks independently rather than
    // trusting the wire, per the "host validates before executing" rule.
    const D3DSI_OPCODE_MASK = 0x0000FFFF;
    const D3DSI_COMMENTSIZE_SHIFT = 16;
    const D3DSI_COMMENTSIZE_MASK = 0x7FFF << D3DSI_COMMENTSIZE_SHIFT;
    const D3DSP_REGNUM_MASK = 0x000007FF;
    const D3DSP_DSTMOD_SHIFT = 20;
    const D3DSP_DSTMOD_MASK = 0xF << D3DSP_DSTMOD_SHIFT;
    const D3DSPDM_SATURATE = 1;
    const D3DSP_DSTSHIFT_SHIFT = 24;
    const D3DSP_DSTSHIFT_MASK = 0xF << D3DSP_DSTSHIFT_SHIFT;
    const D3DSP_REGTYPE_SHIFT = 28;
    const D3DSP_REGTYPE_MASK = 0x7 << D3DSP_REGTYPE_SHIFT;
    const D3DSPR_TEMP = 0 << D3DSP_REGTYPE_SHIFT;
    const D3DSPR_INPUT = 1 << D3DSP_REGTYPE_SHIFT;
    const D3DSPR_CONST = 2 << D3DSP_REGTYPE_SHIFT;
    const D3DSPR_TEXTURE = 3 << D3DSP_REGTYPE_SHIFT; // == D3DSPR_ADDR
    const D3DSPR_RASTOUT = 4 << D3DSP_REGTYPE_SHIFT;
    const D3DSPR_ATTROUT = 5 << D3DSP_REGTYPE_SHIFT;
    const D3DSPR_TEXCRDOUT = 6 << D3DSP_REGTYPE_SHIFT;
    const D3DSP_SWIZZLE_SHIFT = 16;
    const D3DSP_SRCMOD_SHIFT = 24;
    const D3DSP_SRCMOD_MASK = 0xF << D3DSP_SRCMOD_SHIFT;

    const D3DSIO_NOP = 0, D3DSIO_MOV = 1, D3DSIO_ADD = 2, D3DSIO_SUB = 3,
        D3DSIO_MAD = 4, D3DSIO_MUL = 5, D3DSIO_RCP = 6, D3DSIO_RSQ = 7,
        D3DSIO_DP3 = 8, D3DSIO_DP4 = 9, D3DSIO_MIN = 10, D3DSIO_MAX = 11,
        D3DSIO_SLT = 12, D3DSIO_SGE = 13, D3DSIO_EXP = 14, D3DSIO_LOG = 15,
        D3DSIO_LIT = 16, D3DSIO_DST = 17, D3DSIO_LRP = 18, D3DSIO_FRC = 19,
        D3DSIO_TEXCOORD = 64, D3DSIO_TEXKILL = 65, D3DSIO_TEX = 66,
        D3DSIO_EXPP = 78, D3DSIO_LOGP = 79, D3DSIO_CND = 80, D3DSIO_DEF = 81,
        D3DSIO_CMP = 88, D3DSIO_PHASE = 0xFFFD, D3DSIO_COMMENT = 0xFFFE,
        D3DSIO_END = 0xFFFF;

    const D3DVSD_TOKENTYPESHIFT = 29;
    const D3DVSD_TOKEN_STREAMDATA = 2;
    const D3DVSD_TOKEN_END = 7;
    const D3DVSD_DATATYPESHIFT = 16;
    const D3DVSD_VERTEXREGMASK = 0x1F;
    const D3DVSD_SKIPFLAG = 0x10000000;

    // Number of DWORD parameter tokens following the opcode token. DEF's
    // four trailing tokens are raw float32 immediates, not registers,
    // flagged via isDef. Returns null for anything outside the Stage 6
    // supported instruction set for this shader type/version -- matches
    // shader_opcode_supported() in d3d8_proxy.c exactly.
    function shaderOpcodeInfo(opcode, isPixelShader, minor) {
        switch (opcode) {
        case D3DSIO_NOP: return { operandWords: 0 };
        case D3DSIO_MOV: return { operandWords: 2 };
        case D3DSIO_ADD: case D3DSIO_SUB: case D3DSIO_MUL:
        case D3DSIO_MIN: case D3DSIO_MAX: case D3DSIO_DP3: case D3DSIO_DP4:
            return { operandWords: 3 };
        case D3DSIO_MAD: return { operandWords: 4 };
        case D3DSIO_RCP: case D3DSIO_RSQ: case D3DSIO_EXP: case D3DSIO_LOG:
        case D3DSIO_LIT: case D3DSIO_FRC: case D3DSIO_EXPP: case D3DSIO_LOGP:
            return isPixelShader ? null : { operandWords: 2 };
        case D3DSIO_SLT: case D3DSIO_SGE: case D3DSIO_DST:
            return isPixelShader ? null : { operandWords: 3 };
        case D3DSIO_DEF: return { operandWords: 5, isDef: true };
        case D3DSIO_LRP:
            return isPixelShader ? { operandWords: 4 } : null;
        case D3DSIO_CND:
            return (isPixelShader && minor <= 3) ? { operandWords: 4 } : null;
        case D3DSIO_CMP:
            return (isPixelShader && minor >= 2) ? { operandWords: 4 } : null;
        case D3DSIO_TEXCOORD:
            return isPixelShader ? { operandWords: 1 } : null;
        case D3DSIO_TEX:
            return isPixelShader ?
                { operandWords: minor >= 4 ? 2 : 1 } : null;
        case D3DSIO_TEXKILL:
            return isPixelShader ? { operandWords: 1 } : null;
        case D3DSIO_PHASE:
            return (isPixelShader && minor === 4) ? { operandWords: 0 } : null;
        default:
            return null;
        }
    }

    function regType(token) { return (token & D3DSP_REGTYPE_MASK) >>> 0; }
    function regNum(token) { return (token & D3DSP_REGNUM_MASK) >>> 0; }
    function dstWriteMaskBits(token) { return (token >>> 16) & 0xF; }
    function dstSaturates(token) {
        return ((token & D3DSP_DSTMOD_MASK) >>> D3DSP_DSTMOD_SHIFT) ===
            D3DSPDM_SATURATE;
    }
    function dstShiftAmount(token) {
        let shift = (token & D3DSP_DSTSHIFT_MASK) >>> D3DSP_DSTSHIFT_SHIFT;
        if (shift >= 8) shift -= 16; // 4-bit two's-complement nibble
        return shift;
    }
    function srcSwizzleByte(token) { return (token >>> D3DSP_SWIZZLE_SHIFT) & 0xFF; }
    function srcModifierBits(token) {
        return (token & D3DSP_SRCMOD_MASK) >>> D3DSP_SRCMOD_SHIFT;
    }

    function swizzleSuffix(swizzleByte) {
        const letters = "xyzw";
        let suffix = "";
        for (let i = 0; i < 4; i++)
            suffix += letters[(swizzleByte >>> (i * 2)) & 3];
        return suffix;
    }

    function writeMaskSuffix(mask) {
        let suffix = "";
        if (mask & 1) suffix += "x";
        if (mask & 2) suffix += "y";
        if (mask & 4) suffix += "z";
        if (mask & 8) suffix += "w";
        return suffix;
    }

    function applySourceModifier(expr, modifier) {
        switch (modifier) {
        case 0x0: return expr; // D3DSPSM_NONE
        case 0x1: return "(-" + expr + ")"; // D3DSPSM_NEG
        case 0x2: return "(" + expr + " - vec4<f32>(0.5))"; // D3DSPSM_BIAS
        case 0x6: return "(vec4<f32>(1.0) - " + expr + ")"; // D3DSPSM_COMP
        case 0xB: return "abs(" + expr + ")"; // D3DSPSM_ABS
        default:
            throw new Error("unsupported D3D8 source register modifier 0x" +
                modifier.toString(16));
        }
    }

    function applyDestModifiers(resultExpr, dstToken) {
        const shift = dstShiftAmount(dstToken);
        const shiftFactor = { 0: null, 1: 2, 2: 4, 3: 8,
            "-1": 0.5, "-2": 0.25, "-3": 0.125 }[shift];
        if (shift !== 0) {
            if (shiftFactor === undefined)
                throw new Error("unsupported D3D8 destination shift " + shift);
            resultExpr = "(" + resultExpr + " * " + shiftFactor + ")";
        }
        if (dstSaturates(dstToken))
            resultExpr = "clamp(" + resultExpr +
                ", vec4<f32>(0.0), vec4<f32>(1.0))";
        return resultExpr;
    }

    // Walks one D3D8 SM1.x token stream starting right after the version
    // token. `registerExpr(token)` returns the WGSL base expression (before
    // swizzle/modifier) for a register-encoded operand token; throwing from
    // it (e.g. an undeclared vertex input, an out-of-range constant) aborts
    // translation the same way an unsupported opcode does. `emitInstruction`
    // receives (opcode, dstToken, [sourceExprStrings], rawTokens) and pushes
    // WGSL statement(s) for one instruction; DEF is intercepted before this
    // callback runs. Returns nothing; throws on anything outside the Stage 6
    // supported instruction set or a truncated/malformed stream.
    function walkShaderBody(tokens, isPixelShader, minor, registerExpr,
            emitInstruction) {
        const defConstants = new Map();
        // Pre-pass: shader-embedded constants (DEF) override whatever the
        // application sets via Set{Vertex,Pixel}ShaderConstant for that
        // register, per D3D8 semantics, so the main pass needs to know about
        // them before it ever emits a read of a constant register.
        for (let offset = 0; offset < tokens.length;) {
            const token = tokens[offset] >>> 0;
            if (token === D3DSIO_END) break;
            const opcode = token & D3DSI_OPCODE_MASK;
            if (opcode === D3DSIO_COMMENT) {
                offset += 1 + ((token & D3DSI_COMMENTSIZE_MASK) >>>
                    D3DSI_COMMENTSIZE_SHIFT);
                continue;
            }
            const info = shaderOpcodeInfo(opcode, isPixelShader, minor);
            if (!info)
                throw new Error("unsupported D3D8 shader opcode 0x" +
                    opcode.toString(16));
            if (offset + 1 + info.operandWords > tokens.length)
                throw new Error("truncated D3D8 shader instruction 0x" +
                    opcode.toString(16));
            if (opcode === D3DSIO_DEF) {
                const dst = tokens[offset + 1] >>> 0;
                defConstants.set(regNum(dst), [
                    dwordFloat(tokens[offset + 2] >>> 0),
                    dwordFloat(tokens[offset + 3] >>> 0),
                    dwordFloat(tokens[offset + 4] >>> 0),
                    dwordFloat(tokens[offset + 5] >>> 0),
                ]);
            }
            offset += 1 + info.operandWords;
        }

        const constExpr = (token) => {
            if (regType(token) === D3DSPR_CONST) {
                const literal = defConstants.get(regNum(token));
                if (literal)
                    return "vec4<f32>(" + literal.map(v =>
                        Number.isFinite(v) ? v.toExponential() : "0.0")
                        .join(", ") + ")";
            }
            return registerExpr(token);
        };

        for (let offset = 0; offset < tokens.length;) {
            const token = tokens[offset] >>> 0;
            if (token === D3DSIO_END) return;
            const opcode = token & D3DSI_OPCODE_MASK;
            if (opcode === D3DSIO_COMMENT) {
                offset += 1 + ((token & D3DSI_COMMENTSIZE_MASK) >>>
                    D3DSI_COMMENTSIZE_SHIFT);
                continue;
            }
            const info = shaderOpcodeInfo(opcode, isPixelShader, minor);
            if (!info)
                throw new Error("unsupported D3D8 shader opcode 0x" +
                    opcode.toString(16));
            if (offset + 1 + info.operandWords > tokens.length)
                throw new Error("truncated D3D8 shader instruction 0x" +
                    opcode.toString(16));
            if (opcode !== D3DSIO_DEF && opcode !== D3DSIO_NOP &&
                    opcode !== D3DSIO_PHASE) {
                const dstToken = tokens[offset + 1] >>> 0;
                const sourceCount = info.operandWords - 1;
                const sources = [];
                for (let i = 0; i < sourceCount; i++) {
                    const srcToken = tokens[offset + 2 + i] >>> 0;
                    const base = constExpr(srcToken) + "." +
                        swizzleSuffix(srcSwizzleByte(srcToken));
                    sources.push(applySourceModifier(base,
                        srcModifierBits(srcToken)));
                }
                emitInstruction(opcode, dstToken, sources, tokens, offset);
            }
            offset += 1 + info.operandWords;
        }
        // Running off the end of the token array terminates the body just
        // like an explicit D3DSIO_END. The D8WG wire format carries an exact
        // instruction_token_count and the guest strips the trailing END
        // sentinel before sending (see d3d8_proxy.c), so the array bounds --
        // not a sentinel search -- are what actually delimit the stream.
        // Truncation is still caught, by the per-instruction bounds check
        // above.
    }

    function instructionExpression(opcode, sources) {
        const [s0, s1, s2] = sources;
        switch (opcode) {
        case D3DSIO_MOV: return s0;
        case D3DSIO_ADD: return "(" + s0 + " + " + s1 + ")";
        case D3DSIO_SUB: return "(" + s0 + " - " + s1 + ")";
        case D3DSIO_MUL: return "(" + s0 + " * " + s1 + ")";
        case D3DSIO_MAD: return "(" + s0 + " * " + s1 + " + " + s2 + ")";
        case D3DSIO_RCP: return "vec4<f32>(1.0 / (" + s0 + ").x)";
        case D3DSIO_RSQ:
            return "vec4<f32>(inverseSqrt(max(abs((" + s0 + ").x), 1e-12)))";
        case D3DSIO_DP3:
            return "vec4<f32>(dot((" + s0 + ").xyz, (" + s1 + ").xyz))";
        case D3DSIO_DP4: return "vec4<f32>(dot(" + s0 + ", " + s1 + "))";
        case D3DSIO_MIN: return "min(" + s0 + ", " + s1 + ")";
        case D3DSIO_MAX: return "max(" + s0 + ", " + s1 + ")";
        case D3DSIO_SLT:
            return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + s0 + " < " + s1 + ")";
        case D3DSIO_SGE:
            return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + s0 + " >= " + s1 + ")";
        case D3DSIO_EXP: case D3DSIO_EXPP:
            return "vec4<f32>(exp2((" + s0 + ").x))";
        case D3DSIO_LOG: case D3DSIO_LOGP:
            return "vec4<f32>(log2(max(abs((" + s0 + ").x), 1e-12)))";
        case D3DSIO_FRC: return "fract(" + s0 + ")";
        case D3DSIO_DST:
            return "vec4<f32>(1.0, (" + s0 + ").y * (" + s1 + ").y, (" +
                s0 + ").z, (" + s1 + ").w)";
        case D3DSIO_LIT: {
            const n = "(" + s0 + ")";
            return "vec4<f32>(1.0, max(" + n + ".x, 0.0), " +
                "select(0.0, pow(max(" + n + ".y, 0.0), clamp(" + n +
                ".w, -128.0, 128.0)), " + n + ".x > 0.0 && " + n +
                ".y > 0.0), 1.0)";
        }
        case D3DSIO_LRP: return "mix(" + s2 + ", " + s1 + ", " + s0 + ")";
        case D3DSIO_CND:
            return "select(" + s2 + ", " + s1 + ", " + s0 + " > vec4<f32>(0.5))";
        case D3DSIO_CMP:
            return "select(" + s2 + ", " + s1 + ", " + s0 + " >= vec4<f32>(0.0))";
        default:
            throw new Error("no WGSL expression for opcode 0x" +
                opcode.toString(16));
        }
    }

    function emitDestinationWrite(lines, destBaseExpr, dstToken, resultExpr) {
        const mask = dstWriteMaskBits(dstToken);
        if (!mask) return;
        resultExpr = applyDestModifiers(resultExpr, dstToken);
        const suffix = writeMaskSuffix(mask);
        if (suffix === "xyzw") {
            lines.push(destBaseExpr + " = " + resultExpr + ";");
        } else {
            lines.push(destBaseExpr + "." + suffix + " = (" + resultExpr +
                ")." + suffix + ";");
        }
    }

    // Parses a D3DVSD_* vertex declaration into a WebGPU vertex buffer
    // layout keyed by vertex-shader input register number. Per this
    // project's convention (matching the reference d3d8_caps_audit_test.c
    // audit_shader_pipeline() shader, which reads v0/v1 from a declaration
    // whose D3DVSD_REG entries use the D3DVSDE_POSITION=0/D3DVSDE_DIFFUSE=5
    // symbolic slots), a bound vertex shader's input registers are numbered
    // sequentially by the D3DVSD_REG entry's position in the declaration
    // stream, not by the raw D3DVSDE_* register field -- that field is a
    // fixed-function semantic hint the guest also uses for its own shadow
    // bookkeeping, but it is not the shader-visible register index. Only
    // D3DVSD_REG entries in stream 0 are honored -- D3DVSD_SKIP, multiple
    // streams, and the tessellator token type are outside Stage 6's scope
    // and rejected outright rather than silently ignored, so a declaration
    // that needs them fails shader creation instead of producing a shader
    // that reads garbage vertex data.
    function parseVertexDeclaration(tokens) {
        const attributes = [];
        const registers = new Map();
        let cursor = 0;
        let nextRegister = 0;
        for (let i = 0; i < tokens.length; i++) {
            const token = tokens[i] >>> 0;
            const type = (token >>> D3DVSD_TOKENTYPESHIFT) & 0x7;
            if (type !== D3DVSD_TOKEN_STREAMDATA)
                continue;
            if (token & D3DVSD_SKIPFLAG)
                throw new Error("D3DVSD_SKIP is not supported in Stage 6");
            const register = nextRegister++;
            const dataType = (token >>> D3DVSD_DATATYPESHIFT) & 0xF;
            const shapes = {
                0: { format: "float32", size: 4, wgslType: "f32", extend: 3 },
                1: { format: "float32x2", size: 8, wgslType: "vec2<f32>", extend: 2 },
                2: { format: "float32x3", size: 12, wgslType: "vec3<f32>", extend: 1 },
                3: { format: "float32x4", size: 16, wgslType: "vec4<f32>", extend: 0 },
                4: { format: "unorm8x4", size: 4, wgslType: "vec4<f32>", extend: 0 },
            };
            const shape = shapes[dataType];
            if (!shape)
                throw new Error("unsupported D3DVSD data type " + dataType +
                    " in Stage 6 vertex declaration");
            attributes.push({ shaderLocation: register, offset: cursor,
                format: shape.format });
            registers.set(register, shape);
            cursor += shape.size;
        }
        if (!attributes.length)
            throw new Error("vertex declaration has no D3DVSD_REG entries");
        return { attributes, registers, stride: cursor };
    }

    function vertexShaderWgsl(codeTokens, declaration) {
        const minor = codeTokens[0] & 0xFF;
        if (((codeTokens[0] >>> 16) & 0xFFFF) !== 0xFFFE || minor !== 1)
            throw new Error("unsupported vertex shader version token");
        const registerExpr = (token) => {
            const type = regType(token);
            const num = regNum(token);
            switch (type) {
            case D3DSPR_TEMP: return "r" + num;
            case D3DSPR_INPUT: {
                const shape = declaration.registers.get(num);
                if (!shape)
                    throw new Error("vertex shader reads undeclared input v" + num);
                if (shape.extend === 0) return "input.v" + num;
                const pad = ["1.0", "0.0, 1.0", "0.0, 0.0, 1.0"][shape.extend - 1];
                return "vec4<f32>(input.v" + num + ", " + pad + ")";
            }
            case D3DSPR_CONST:
                if (num >= D8WG_MAX_VS_CONSTANTS)
                    throw new Error("vertex shader constant c" + num +
                        " is out of range");
                return "constants.vs[" + num + "]";
            case D3DSPR_RASTOUT:
                if (num !== 0)
                    throw new Error("unsupported vertex shader oRastOut index " + num);
                return "output.position";
            case D3DSPR_ATTROUT:
                if (num > 1)
                    throw new Error("unsupported vertex shader oD index " + num);
                return num === 0 ? "output.diffuse" : "output.specular";
            case D3DSPR_TEXCRDOUT:
                if (num > 7)
                    throw new Error("unsupported vertex shader oT index " + num);
                return "output.texcoord" + num;
            default:
                throw new Error("unsupported vertex shader register type 0x" +
                    type.toString(16));
            }
        };
        const temps = new Set();
        const body = [];
        walkShaderBody(codeTokens.slice(1), false, 1, registerExpr,
            (opcode, dstToken, sources) => {
                if (regType(dstToken) === D3DSPR_TEMP)
                    temps.add("r" + regNum(dstToken));
                const expr = instructionExpression(opcode, sources);
                emitDestinationWrite(body, registerExpr(dstToken), dstToken,
                    expr);
            });
        const inputFields = [...declaration.registers.entries()]
            .sort((a, b) => a[0] - b[0])
            .map(([register, shape]) => "    @location(" + register + ") v" +
                register + ": " + shape.wgslType + ",").join("\n");
        const tempDecls = [...temps].map(name =>
            "    var " + name + ": vec4<f32>;").join("\n");
        return `
struct VSInput {
${inputFields}
};
struct VSOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) diffuse: vec4<f32>,
    @location(1) specular: vec4<f32>,
    @location(2) texcoord0: vec4<f32>,
    @location(3) texcoord1: vec4<f32>,
    @location(4) texcoord2: vec4<f32>,
    @location(5) texcoord3: vec4<f32>,
    @location(6) texcoord4: vec4<f32>,
    @location(7) texcoord5: vec4<f32>,
    @location(8) texcoord6: vec4<f32>,
    @location(9) texcoord7: vec4<f32>,
};
struct D8WGShaderConstants {
    vs: array<vec4<f32>, ${D8WG_MAX_VS_CONSTANTS}>,
    ps: array<vec4<f32>, ${D8WG_MAX_PS_CONSTANTS}>,
};
@group(0) @binding(0) var<uniform> constants: D8WGShaderConstants;
@vertex fn vs_main(input: VSInput) -> VSOutput {
    var output: VSOutput;
    output.position = vec4<f32>(0.0);
    output.diffuse = vec4<f32>(1.0);
    output.specular = vec4<f32>(0.0);
    output.texcoord0 = vec4<f32>(0.0);
    output.texcoord1 = vec4<f32>(0.0);
    output.texcoord2 = vec4<f32>(0.0);
    output.texcoord3 = vec4<f32>(0.0);
    output.texcoord4 = vec4<f32>(0.0);
    output.texcoord5 = vec4<f32>(0.0);
    output.texcoord6 = vec4<f32>(0.0);
    output.texcoord7 = vec4<f32>(0.0);
${tempDecls}
${body.join("\n")}
    return output;
}
`;
    }

    function pixelShaderWgsl(codeTokens) {
        const minor = codeTokens[0] & 0xFF;
        if (((codeTokens[0] >>> 16) & 0xFFFF) !== 0xFFFF || minor < 1 || minor > 4)
            throw new Error("unsupported pixel shader version token");
        const stageOf = (num) => {
            if (num > 1)
                throw new Error("texture stage t" + num +
                    " exceeds this backend's 2-stage binding limit");
            return num;
        };
        const registerExpr = (token) => {
            const type = regType(token);
            const num = regNum(token);
            switch (type) {
            case D3DSPR_TEMP: return "r" + num;
            case D3DSPR_INPUT:
                if (num > 1)
                    throw new Error("unsupported pixel shader v index " + num);
                return num === 0 ? "input.diffuse" : "input.specular";
            case D3DSPR_CONST:
                if (num >= D8WG_MAX_PS_CONSTANTS)
                    throw new Error("pixel shader constant c" + num +
                        " is out of range");
                return "constants.ps[" + num + "]";
            case D3DSPR_TEXTURE:
                stageOf(num);
                return "t" + num;
            default:
                throw new Error("unsupported pixel shader register type 0x" +
                    type.toString(16));
            }
        };
        const temps = new Set();
        const texRegisters = new Set();
        const body = [];
        walkShaderBody(codeTokens.slice(1), true, minor, registerExpr,
            (opcode, dstToken, sources, tokens, offset) => {
                const dstType = regType(dstToken);
                const dstNum = regNum(dstToken);
                if (dstType === D3DSPR_TEMP) temps.add("r" + dstNum);
                if (dstType === D3DSPR_TEXTURE) texRegisters.add(dstNum);
                if (opcode === D3DSIO_TEXCOORD) {
                    const stage = stageOf(dstNum);
                    texRegisters.add(dstNum);
                    body.push("    t" + dstNum + " = vec4<f32>(input.texcoord" +
                        stage + ".xy, 0.0, 1.0);");
                    return;
                }
                if (opcode === D3DSIO_TEX) {
                    texRegisters.add(dstNum);
                    if (minor >= 4) {
                        const srcToken = tokens[offset + 2] >>> 0;
                        const srcStage = stageOf(regNum(srcToken));
                        body.push("    r" + dstNum + " = textureSample(stage" +
                            srcStage + "Texture, stage" + srcStage +
                            "Sampler, t" + srcStage + ".xy);");
                    } else {
                        const stage = stageOf(dstNum);
                        body.push("    t" + dstNum + " = textureSample(stage" +
                            stage + "Texture, stage" + stage +
                            "Sampler, t" + stage + ".xy);");
                    }
                    return;
                }
                if (opcode === D3DSIO_TEXKILL) {
                    const stage = stageOf(dstNum);
                    texRegisters.add(dstNum);
                    body.push("    if (any(vec3<f32>(t" + stage +
                        ".x, t" + stage + ".y, t" + stage +
                        ".z) < vec3<f32>(0.0))) { discard; }");
                    return;
                }
                const expr = instructionExpression(opcode, sources);
                emitDestinationWrite(body, registerExpr(dstToken), dstToken,
                    expr);
            });
        // r0 is always the implicit ps.1.x output register (its final value
        // becomes the fragment color) whether or not the shader body ever
        // names it explicitly, so it must be declared exactly once even for
        // a (degenerate) shader that never writes r0.
        temps.add("r0");
        const tempDecls = [...temps].map(name =>
            "    var " + name + ": vec4<f32>;").join("\n");
        const texDecls = [...texRegisters].sort().map(num =>
            "    var t" + num + ": vec4<f32> = vec4<f32>(input.texcoord" +
            num + ".xy, 0.0, 1.0);").join("\n");
        return `
struct D8WGShaderConstants {
    vs: array<vec4<f32>, ${D8WG_MAX_VS_CONSTANTS}>,
    ps: array<vec4<f32>, ${D8WG_MAX_PS_CONSTANTS}>,
};
@group(0) @binding(0) var<uniform> constants: D8WGShaderConstants;
@group(0) @binding(1) var stage0Texture: texture_2d<f32>;
@group(0) @binding(2) var stage0Sampler: sampler;
@group(0) @binding(3) var stage1Texture: texture_2d<f32>;
@group(0) @binding(4) var stage1Sampler: sampler;
@fragment fn fs_main(input: VSOutput) -> @location(0) vec4<f32> {
${texDecls}
${tempDecls}
${body.join("\n")}
    return r0;
}
`;
    }

    // Combines a translated vertex shader and pixel shader into one WGSL
    // module sharing the VSOutput/D8WGShaderConstants definitions. Either
    // half may be null (no vertex/pixel shader bound uses the fixed-function
    // path instead -- see D3D8WebGPUExecutor.pipelineFor), but Stage 6 only
    // wires the combination where both are real shaders, matching the
    // Maple-relevant usage the doc anticipates (real VS + real PS together).
    function shaderPipelineWgsl(vertexResource, declaration, pixelResource) {
        const vs = vertexShaderWgsl(vertexResource.codeTokens, declaration);
        const ps = pixelShaderWgsl(pixelResource.codeTokens);
        // pixelShaderWgsl's `input: VSOutput` refers to the struct defined
        // in vs; strip its duplicate D8WGShaderConstants block so the
        // combined module only declares each binding once.
        const psBody = ps.replace(
            /struct D8WGShaderConstants[\s\S]*?@binding\(0\) var<uniform> constants: D8WGShaderConstants;\n/,
            "");
        return vs + psBody;
    }

    function identityMatrix() {
        return new Float32Array([
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        ]);
    }

    function freshDeviceState(handle, surface) {
        const renderStates = new Uint32Array(256);
        const textureStageStates = Array.from({ length: 8 },
            () => new Uint32Array(32));
        renderStates[7] = 1; // D3DRS_ZENABLE = D3DZB_TRUE
        renderStates[14] = 1; // D3DRS_ZWRITEENABLE
        renderStates[D3DRS_ZFUNC] = 4; // D3DCMP_LESSEQUAL
        renderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
        renderStates[137] = 1; // D3DRS_LIGHTING
        renderStates[9] = 2; // D3DRS_SHADEMODE = GOURAUD
        renderStates[D3DRS_ALPHAFUNC] = 8; // ALWAYS
        renderStates[D3DRS_SRCBLEND] = 2; // ONE
        renderStates[D3DRS_DESTBLEND] = 1; // ZERO
        renderStates[D3DRS_BLENDOP] = 1; // ADD
        renderStates[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
        renderStates[D3DRS_COLORWRITEENABLE] = 0xF;
        renderStates[D3DRS_FOGEND] = 0x3F800000;
        renderStates[D3DRS_FOGDENSITY] = 0x3F800000;
        renderStates[D3DRS_STENCILFAIL] = 1; // KEEP
        renderStates[D3DRS_STENCILZFAIL] = 1;
        renderStates[D3DRS_STENCILPASS] = 1;
        renderStates[D3DRS_STENCILFUNC] = 8; // ALWAYS
        renderStates[D3DRS_STENCILMASK] = 0xFFFFFFFF;
        renderStates[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
        renderStates[D3DRS_COLORVERTEX] = 1;
        renderStates[D3DRS_LOCALVIEWER] = 1;
        renderStates[D3DRS_DIFFUSEMATERIALSOURCE] = 1; // COLOR1
        renderStates[D3DRS_SPECULARMATERIALSOURCE] = 2; // COLOR2
        renderStates[D3DRS_AMBIENTMATERIALSOURCE] = 0; // MATERIAL
        renderStates[D3DRS_EMISSIVEMATERIALSOURCE] = 0; // MATERIAL
        for (let stage = 0; stage < 8; stage++) {
            textureStageStates[stage][D3DTSS_COLOROP] =
                stage === 0 ? 4 : D3DTOP_DISABLE; // MODULATE
            textureStageStates[stage][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
            textureStageStates[stage][D3DTSS_COLORARG2] = D3DTA_CURRENT;
            textureStageStates[stage][D3DTSS_ALPHAOP] =
                stage === 0 ? 2 : D3DTOP_DISABLE; // SELECTARG1
            textureStageStates[stage][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
            textureStageStates[stage][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
            textureStageStates[stage][D3DTSS_TEXCOORDINDEX] = stage;
            textureStageStates[stage][D3DTSS_ADDRESSU] = 1; // WRAP
            textureStageStates[stage][D3DTSS_ADDRESSV] = 1;
            textureStageStates[stage][D3DTSS_MAGFILTER] = 1; // POINT
            textureStageStates[stage][D3DTSS_MINFILTER] = 1;
            textureStageStates[stage][D3DTSS_MIPFILTER] = 0;
            textureStageStates[stage][D3DTSS_MAXANISOTROPY] = 1;
            textureStageStates[stage][D3DTSS_RESULTARG] = D3DTA_CURRENT;
        }
        return {
            handle,
            surface,
            renderStates,
            textureStageStates,
            streams: Array.from({ length: 16 }, () => ({ handle: 0, stride: 0 })),
            indices: { handle: 0, baseVertex: 0 },
            textures: new Uint32Array(8),
            viewport: { x: 0, y: 0, width: surface.width,
                height: surface.height, minZ: 0, maxZ: 1 },
            transforms: {
                world: identityMatrix(),
                view: identityMatrix(),
                projection: identityMatrix(),
                textures: Array.from({ length: 8 }, identityMatrix),
            },
            material: {
                diffuse: [1, 1, 1, 1], ambient: [1, 1, 1, 1],
                specular: [0, 0, 0, 0], emissive: [0, 0, 0, 0], power: 0,
            },
            lights: Array.from({ length: 8 }, () => ({
                type: 0,
                diffuse: [0, 0, 0, 0], specular: [0, 0, 0, 0],
                ambient: [0, 0, 0, 0], position: [0, 0, 0],
                direction: [0, 0, 1], range: 0, falloff: 0,
                attenuation: [1, 0, 0], theta: 0, phi: 0, enabled: false,
            })),
            fvf: 0,
            inScene: false,
            renderTarget: { handle: 0, level: 0 },
            depthSurfaceEnabled: surface.autoDepthStencil,
            uniformSerial: 0,
            // Stage 7: the current uniform-ring slot for this device, and the
            // (serial, frame) it was packed for. -1 forces a repack.
            uniformSlotSerial: -1,
            uniformSlotFrame: -1,
            uniformSlotBuffer: null,
            uniformSlotOffset: 0,
            shaderConstantSlotSerial: -1,
            shaderConstantSlotFrame: -1,
            shaderConstantSlotBuffer: null,
            shaderConstantSlotOffset: 0,
            bindGroups: new Map(),
            // Stage 6: D3D8 shader model 1.x. vertexShader/pixelShader are
            // D8WG resource handles (0 = fixed-function / no pixel shader).
            vertexShader: 0,
            pixelShader: 0,
            vsConstants: new Float32Array(D8WG_MAX_VS_CONSTANTS * 4),
            psConstants: new Float32Array(D8WG_MAX_PS_CONSTANTS * 4),
            shaderConstantSerial: 0,
        };
    }

    class D3D8WebGPUExecutor {
        constructor(canvas, options) {
            if (!canvas) {
                throw new Error("D3D8 WebGPU canvas is required");
            }
            this.canvas = canvas;
            this.options = options || {};
            this.gpu = this.options.gpu ||
                (global.navigator && global.navigator.gpu);
            this.adapter = this.options.adapter || null;
            this.device = this.options.device || null;
            this.context = this.options.context || null;
            // Skipped when a device was supplied: that caller configures the
            // canvas itself. See the gpuHost note at the top of this file.
            this.host = this.options.host || (this.device ? null :
                ((host => host ? host.acquire(canvas, {
                    gpu: this.gpu,
                    adapter: this.adapter,
                    context: this.context,
                    format: this.options.format || null,
                    ...(this.options.hostOptions || {}),
                }) : null)(resolveGPUHost())));
            this.format = this.options.format || null;
            this.sessions = new Map();
            this.activeSession = null;
            this.maxSessions = Math.max(16, this.options.maxSessions || 128);
            this.sessionSerial = 0;
            // These aliases always point at the session of the current/most
            // recently executed batch. Keeping them preserves the public test
            // and diagnostics surface while all ownership is session-local.
            this.devices = new Map();
            this.resources = new Map();
            this.retiredDeviceHandles = new Set();
            this.retiredResourceHandles = new Set();
            this.pipelineCache = new Map();
            this.samplerCache = new Map();
            // Stage 6 shader pipelines use an explicit bind group layout
            // rather than layout:"auto" (see shaderBindGroupLayoutFor). Both
            // objects belong to the current GPUDevice and are dropped with the
            // pipeline/sampler caches when a device is lost.
            this.shaderBindGroupLayout = null;
            this.shaderPipelineLayout = null;
            this.maxPipelines = Math.max(32, this.options.maxPipelines || 512);
            this.maxSamplers = Math.max(8, this.options.maxSamplers || 64);
            this.maxBindGroups = Math.max(64,
                this.options.maxBindGroups || 1024);
            this.nextPipelineId = 1;
            this.fallbackTexture = null;
            this.fallbackView = null;
            this.transientBuffer = null;
            this.transientCapacity = Math.max(1024 * 1024,
                this.options.transientBufferBytes || TRANSIENT_BUFFER_BYTES);
            this.transientCursor = 0;
            // Stage 7: one persistent uniform ring per executor, suballocated
            // with dynamic offsets so a state change costs a ring write rather
            // than a new GPUBuffer plus a new bind group.
            this.uniformRing = null;
            this.uniformRingCapacity = Math.max(64 * 1024,
                this.options.uniformRingBytes || UNIFORM_RING_BYTES);
            this.uniformRingCursor = 0;
            // Reused packing scratch: the fixed-function uniform block is
            // rebuilt per distinct state, and allocating it per draw was a
            // measurable GC source.
            this.uniformScratch = new Float32Array(FIXED_UNIFORM_BYTES / 4);
            this.shaderConstantScratch =
                new Float32Array(SHADER_UNIFORM_BYTES / 4);
            this.fixedBindGroupLayout = null;
            this.fixedPipelineLayout = null;
            this.frameSerial = 0;
            this.frame = null;
            this.readyPromise = null;
            this.work = Promise.resolve();
            this.pendingSubmissions = 0;
            this.failed = null;
            this.warned = new Set();
            this.stats = {
                batches: 0,
                commands: 0,
                presents: 0,
                queueSubmits: 0,
                drawCalls: 0,
                indexedDrawCalls: 0,
                upDrawCalls: 0,
                fanConversions: 0,
                uploadBytes: 0,
                transientUploadBytes: 0,
                transientBufferCreations: 0,
                bufferOrphans: 0,
                pipelineCreations: 0,
                pipelineCacheEvictions: 0,
                uniformCacheEvictions: 0,
                // Stage 7 counters (doc 12.2). bindGroupCreations must reach
                // 0/frame in steady state; uniformRingOverflows staying at 0
                // means the ring is large enough for the workload.
                bindGroupCreations: 0,
                bindGroupHits: 0,
                pipelineHits: 0,
                uniformSlotReuses: 0,
                uniformUploadBytes: 0,
                uniformRingOverflows: 0,
                redundantStateWrites: 0,
                malformedBatches: 0,
                unsupportedCommands: 0,
                deviceRecoveries: 0,
                staleCommandsDropped: 0,
                rectangularClears: 0,
                deferredDestroys: 0,
            };
        }

        sessionKey(low, high) {
            return (high >>> 0).toString(16).padStart(8, "0") + ":" +
                (low >>> 0).toString(16).padStart(8, "0");
        }

        setActiveSession(session, finishForeignFrame) {
            if (finishForeignFrame && this.frame &&
                    this.frame.sessionKey !== session.key) {
                this.finishFrame(false);
            }
            this.activeSession = session;
            this.devices = session.devices;
            this.resources = session.resources;
            this.retiredDeviceHandles = session.retiredDeviceHandles;
            this.retiredResourceHandles = session.retiredResourceHandles;
            session.lastUsed = ++this.sessionSerial;
        }

        activateSession(low, high) {
            low >>>= 0;
            high >>>= 0;
            if (!low && !high)
                throw new Error("D8WG batch has an empty process session");
            const key = this.sessionKey(low, high);
            let session = this.sessions.get(key);
            if (!session) {
                session = {
                    key, low, high,
                    devices: new Map(),
                    resources: new Map(),
                    retiredDeviceHandles: new Set(),
                    retiredResourceHandles: new Set(),
                    helloSeen: false,
                    lastUsed: 0,
                };
                this.sessions.set(key, session);
            }
            this.setActiveSession(session, true);
            this.pruneSessions();
            return session;
        }

        pruneSessions() {
            if (this.sessions.size <= this.maxSessions) return;
            for (const [key, session] of this.sessions) {
                if (this.sessions.size <= this.maxSessions) break;
                if (session === this.activeSession || session.devices.size ||
                        session.resources.size) continue;
                this.sessions.delete(key);
            }
        }

        retireHandle(set, handle) {
            if (!handle) return;
            set.add(handle >>> 0);
            if (set.size > 8192)
                set.delete(set.values().next().value);
        }

        warnOnce(key, message, details) {
            if (this.warned.has(key)) return;
            this.warned.add(key);
            console.warn("[d3d8-webgpu] " + message, details || "");
        }

        initialize() {
            if (this.readyPromise) return this.readyPromise;
            this.readyPromise = (async () => {
                if (!this.device) {
                    if (!this.host) throw new Error("WebGPU is unavailable");
                    await this.host.initialize();
                    this.adapter = this.host.adapter;
                    this.device = this.host.device;
                    this.context = this.context || this.host.context;
                    this.format = this.format || this.host.format;
                }
                this.context = this.context ||
                    (this.canvas && typeof this.canvas.getContext === "function" ?
                        this.canvas.getContext("webgpu") : null);
                if (!this.context) throw new Error("could not acquire a WebGPU canvas context");
                this.format = this.format || (this.gpu &&
                    typeof this.gpu.getPreferredCanvasFormat === "function" ?
                    this.gpu.getPreferredCanvasFormat() : "bgra8unorm");
                this.configureContext();
                this.fallbackTexture = this.device.createTexture({
                    label: "D3D8 fallback white texture",
                    size: { width: 1, height: 1, depthOrArrayLayers: 1 },
                    format: "rgba8unorm",
                    usage: TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
                });
                this.fallbackView = this.fallbackTexture.createView();
                this.device.queue.writeTexture({ texture: this.fallbackTexture },
                    new Uint8Array([255, 255, 255, 255]),
                    { bytesPerRow: 4, rowsPerImage: 1 },
                    { width: 1, height: 1, depthOrArrayLayers: 1 });
                this.transientBuffer = this.device.createBuffer({
                    label: "D3D8 transient upload ring",
                    size: this.transientCapacity,
                    usage: BUFFER_USAGE_VERTEX | BUFFER_USAGE_INDEX |
                        BUFFER_USAGE_COPY_SRC | BUFFER_USAGE_COPY_DST,
                });
                this.uniformRing = this.device.createBuffer({
                    label: "D3D8 uniform ring",
                    size: this.uniformRingCapacity,
                    usage: BUFFER_USAGE_UNIFORM | BUFFER_USAGE_COPY_DST,
                });
                this.uniformRing._d8wgRingId = ++uniformRingSerial;
                this.uniformRingCursor = 0;
                if (this.device.lost && typeof this.device.lost.then === "function") {
                    this.device.lost.then(info => {
                        this.scheduleDeviceRecovery(info);
                    });
                }
                return this;
            })().catch(error => {
                this.failed = error;
                console.error("[d3d8-webgpu] initialization failed", error);
                throw error;
            });
            return this.readyPromise;
        }

        configureContext() {
            // Reconfiguring is how this path has always followed a device
            // reset's new back-buffer size. When the host owns the device it
            // must also own the configuration, or this call would hand the
            // canvas to a device the other backends do not share.
            if (this.host) {
                this.host.configureCanvas();
                return;
            }
            this.context.configure({
                device: this.device,
                format: this.format,
                alphaMode: "opaque",
            });
        }

        submit(bytes, metadata) {
            const owned = bytes instanceof Uint8Array ? bytes.slice() :
                new Uint8Array(bytes || []);
            this.pendingSubmissions++;
            this.work = this.work.then(() => this.initialize())
                .then(() => this.executeBatch(owned, metadata || {}))
                .catch(error => {
                    this.failed = error;
                    console.error("[d3d8-webgpu] batch failed", error, metadata || {});
                }).finally(() => { this.pendingSubmissions--; });
            return this.work;
        }

        idle() {
            return this.work;
        }

        discardFrame() {
            if (!this.frame) return;
            this.endPass();
            for (const buffer of this.frame.transientBuffers)
                buffer.destroy();
            const deferredDestroy = this.frame.deferredDestroy;
            this.frame = null;
            // The discarded encoder no longer references these objects, but
            // an earlier submitted frame still might. Retire them behind the
            // queue completion fence rather than destroying them immediately.
            this.scheduleGPUDestruction(deferredDestroy);
        }

        scheduleGPUDestruction(objects) {
            const pending = Array.from(new Set((objects || []).filter(
                object => object && typeof object.destroy === "function")));
            if (!pending.length) return;
            const destroy = () => {
                for (const object of pending) object.destroy();
            };
            this.stats.deferredDestroys += pending.length;
            if (this.device && this.device.queue &&
                    typeof this.device.queue.onSubmittedWorkDone === "function") {
                this.device.queue.onSubmittedWorkDone().then(destroy, destroy);
            } else {
                destroy();
            }
        }

        retireGPUObjects(...objects) {
            const pending = objects.filter(object => object &&
                typeof object.destroy === "function");
            if (!pending.length) return;
            if (this.frame)
                this.frame.deferredDestroy.push(...pending);
            else
                this.scheduleGPUDestruction(pending);
        }

        async recoverDevice(replacementDevice, info) {
            this.discardFrame();
            const checkpoint = this.serializeState();
            if (this.fallbackTexture) this.fallbackTexture.destroy();
            if (this.transientBuffer) this.transientBuffer.destroy();
            if (this.uniformRing) this.uniformRing.destroy();
            this.fallbackTexture = null;
            this.fallbackView = null;
            this.transientBuffer = null;
            this.uniformRing = null;
            this.uniformRingCursor = 0;
            this.fixedBindGroupLayout = null;
            this.fixedPipelineLayout = null;
            // GPU pipelines and samplers are owned by the lost GPUDevice and
            // must never be reused by the replacement device, even when their
            // logical D3D8 cache keys are identical.
            this.pipelineCache.clear();
            this.samplerCache.clear();
            this.shaderBindGroupLayout = null;
            this.shaderPipelineLayout = null;
            this.device = replacementDevice || null;
            if (!replacementDevice) this.adapter = null;
            this.readyPromise = null;
            this.failed = null;
            await this.initialize();
            this.restoreStateInitialized(checkpoint);
            this.stats.deviceRecoveries++;
            if (typeof this.options.onDeviceRecovered === "function")
                this.options.onDeviceRecovered(info || {});
        }

        scheduleDeviceRecovery(info, replacementDevice) {
            const message = info && info.message || "unknown reason";
            console.warn("[d3d8-webgpu] device lost; rebuilding resources", info);
            this.work = this.work.then(() =>
                this.recoverDevice(replacementDevice, info)).catch(error => {
                this.failed = new Error("WebGPU device recovery failed after " +
                    message + ": " + (error && error.message || error));
                console.error("[d3d8-webgpu] device recovery failed", error);
            });
            return this.work;
        }

        injectDeviceLoss(replacementDevice) {
            if (!replacementDevice)
                throw new Error("device-loss injection requires a replacement device");
            return this.scheduleDeviceRecovery({ reason: "injected",
                message: "test injection" }, replacementDevice);
        }

        parseSurface(bytes, offset) {
            const width = Math.max(1, u32(bytes, offset + 16));
            const height = Math.max(1, u32(bytes, offset + 20));
            return {
                sessionKey: this.activeSession.key,
                sessionIdLow: this.activeSession.low,
                sessionIdHigh: this.activeSession.high,
                hwnd: u32(bytes, offset + 4),
                x: i32(bytes, offset + 8),
                y: i32(bytes, offset + 12),
                width,
                height,
                displayWidth: width,
                displayHeight: height,
                visible: true,
                format: u32(bytes, offset + 24),
                windowed: !!u32(bytes, offset + 28),
                behaviorFlags: u32(bytes, offset + 32),
                autoDepthStencil: !!u32(bytes, offset + 36),
                autoDepthStencilFormat: u32(bytes, offset + 40),
            };
        }

        createSurfaceUniform(state) {
            // Surface size feeds the uniform block, so any cached ring slot is
            // stale. Bind groups do not embed the uniform contents (only the
            // ring buffer identity), but clearing them is cheap and keeps
            // resize behaviour obviously correct.
            state.uniformSlotSerial = -1;
            state.bindGroups.clear();
        }

        createDepthSurface(state) {
            this.retireGPUObjects(state.depthTexture);
            state.depthTexture = null;
            state.depthView = null;
            if (!state.surface.autoDepthStencil) return;
            state.depthTexture = this.device.createTexture({
                label: "D3D8 automatic depth-stencil " +
                    state.handle.toString(16),
                size: { width: state.surface.width,
                    height: state.surface.height, depthOrArrayLayers: 1 },
                sampleCount: 1,
                dimension: "2d",
                format: "depth24plus-stencil8",
                usage: TEXTURE_USAGE_RENDER_ATTACHMENT,
            });
            state.depthView = state.depthTexture.createView();
        }

        // Suballocate `byteCount` from the uniform ring and return the buffer
        // plus a 256-byte-aligned dynamic offset. The cursor resets at frame
        // start; a frame that outruns the ring falls back to a dedicated
        // buffer retired with that frame, so in-flight data is never
        // overwritten before its command buffer is submitted.
        allocateUniformSlot(byteCount) {
            const size = alignUniform(byteCount);
            const offset = alignUniform(this.uniformRingCursor);
            if (this.uniformRing &&
                    size <= this.uniformRingCapacity - offset) {
                this.uniformRingCursor = offset + size;
                return { buffer: this.uniformRing, offset };
            }
            const overflow = this.device.createBuffer({
                label: "D3D8 uniform ring overflow",
                size,
                usage: BUFFER_USAGE_UNIFORM | BUFFER_USAGE_COPY_DST,
            });
            overflow._d8wgRingId = ++uniformRingSerial;
            if (this.frame) this.frame.transientBuffers.push(overflow);
            else this.retireGPUObjects(overflow);
            this.stats.uniformRingOverflows++;
            return { buffer: overflow, offset: 0 };
        }

        // Packs the fixed-function uniform block into a reusable scratch array
        // and parks it in the uniform ring. Reuses the previous slot when no
        // state that feeds the block has changed since the last draw of this
        // frame, so a run of draws sharing one state costs a single upload.
        uniformFor(state) {
            if (state.uniformSlotSerial === state.uniformSerial &&
                    state.uniformSlotFrame === this.frameSerial &&
                    state.uniformSlotBuffer) {
                this.stats.uniformSlotReuses++;
                return { buffer: state.uniformSlotBuffer,
                    offset: state.uniformSlotOffset };
            }
            const values = this.uniformScratch;
            const width = Math.max(1, state.surface.width);
            const height = Math.max(1, state.surface.height);
            const factorValue = state.renderStates[D3DRS_TEXTUREFACTOR] >>> 0;
            values[0] = width;
            values[1] = height;
            values[2] = 1 / width;
            values[3] = 1 / height;
            values[4] = ((factorValue >>> 16) & 0xFF) / 255;
            values[5] = ((factorValue >>> 8) & 0xFF) / 255;
            values[6] = (factorValue & 0xFF) / 255;
            values[7] = ((factorValue >>> 24) & 0xFF) / 255;
            values[8] = state.renderStates[D3DRS_ALPHAREF] & 255;
            values[9] = 0; values[10] = 0; values[11] = 0;
            values.set(state.transforms.world, 12);
            values.set(state.transforms.view, 28);
            values.set(state.transforms.projection, 44);
            const fogColorValue = state.renderStates[D3DRS_FOGCOLOR] >>> 0;
            values[60] = ((fogColorValue >>> 16) & 0xFF) / 255;
            values[61] = ((fogColorValue >>> 8) & 0xFF) / 255;
            values[62] = (fogColorValue & 0xFF) / 255;
            values[63] = ((fogColorValue >>> 24) & 0xFF) / 255;
            values[64] = dwordFloat(state.renderStates[D3DRS_FOGSTART]);
            values[65] = dwordFloat(state.renderStates[D3DRS_FOGEND]);
            values[66] = dwordFloat(state.renderStates[D3DRS_FOGDENSITY]);
            values[67] = 0;
            values.set(state.material.diffuse, 68);
            values.set(state.material.ambient, 72);
            values.set(state.material.specular, 76);
            values.set(state.material.emissive, 80);
            values[84] = state.material.power;
            values[85] = 0; values[86] = 0; values[87] = 0;
            const ambientValue = state.renderStates[D3DRS_AMBIENT] >>> 0;
            values[88] = ((ambientValue >>> 16) & 0xFF) / 255;
            values[89] = ((ambientValue >>> 8) & 0xFF) / 255;
            values[90] = (ambientValue & 0xFF) / 255;
            values[91] = ((ambientValue >>> 24) & 0xFF) / 255;
            for (let index = 0; index < 8; index++) {
                const light = state.lights[index];
                const offset = 92 + index * 28;
                values.set(light.diffuse, offset);
                values.set(light.specular, offset + 4);
                values.set(light.ambient, offset + 8);
                values[offset + 12] = light.position[0];
                values[offset + 13] = light.position[1];
                values[offset + 14] = light.position[2];
                values[offset + 15] = light.type;
                values[offset + 16] = light.direction[0];
                values[offset + 17] = light.direction[1];
                values[offset + 18] = light.direction[2];
                values[offset + 19] = light.range;
                values[offset + 20] = light.attenuation[0];
                values[offset + 21] = light.attenuation[1];
                values[offset + 22] = light.attenuation[2];
                values[offset + 23] = light.falloff;
                values[offset + 24] = light.theta;
                values[offset + 25] = light.phi;
                values[offset + 26] = light.enabled ? 1 : 0;
                values[offset + 27] = 0;
            }
            values.set(state.transforms.textures[0], 316);
            values.set(state.transforms.textures[1], 332);
            const slot = this.allocateUniformSlot(FIXED_UNIFORM_BYTES);
            this.device.queue.writeBuffer(slot.buffer, slot.offset, values);
            this.stats.uniformUploadBytes += FIXED_UNIFORM_BYTES;
            state.uniformSlotSerial = state.uniformSerial;
            state.uniformSlotFrame = this.frameSerial;
            state.uniformSlotBuffer = slot.buffer;
            state.uniformSlotOffset = slot.offset;
            return slot;
        }

        createOrResetDevice(bytes, payloadOffset, reset, surfaceReason) {
            const handle = u32(bytes, payloadOffset);
            const surface = this.parseSurface(bytes, payloadOffset);
            let state = this.devices.get(handle);
            if (!state || !reset) {
                if (state) this.retireGPUObjects(state.depthTexture);
                state = freshDeviceState(handle, surface);
                this.devices.set(handle, state);
            } else {
                state.surface = surface;
                state.inScene = false;
                state.viewport = { x: 0, y: 0, width: surface.width,
                    height: surface.height, minZ: 0, maxZ: 1 };
            }
            this.canvas.width = surface.width;
            this.canvas.height = surface.height;
            this.configureContext();
            this.createSurfaceUniform(state);
            this.createDepthSurface(state);
            if (typeof this.options.onSurface === "function") {
                this.options.onSurface(surface,
                    surfaceReason || (reset ? "reset" : "create"));
            }
        }

        resetDevice(bytes, payloadOffset) {
            const oldHandle = u32(bytes, payloadOffset);
            const newHandle = u32(bytes, payloadOffset + 4);
            if (!oldHandle || !newHandle || oldHandle === newHandle)
                throw new Error("RESET has an invalid device epoch");
            if (!this.devices.has(oldHandle))
                throw new Error("RESET references an unknown old device");
            this.destroyResource(oldHandle);
            this.createOrResetDevice(bytes, payloadOffset + 4, false, "reset");
        }

        updateSurface(bytes, payloadOffset, state, reason) {
            const width = u32(bytes, payloadOffset + 16);
            const height = u32(bytes, payloadOffset + 20);
            const visible = width !== 0 && height !== 0;
            const hwnd = u32(bytes, payloadOffset + 4);
            const x = i32(bytes, payloadOffset + 8);
            const y = i32(bytes, payloadOffset + 12);
            const changed = state.surface.hwnd !== hwnd ||
                state.surface.x !== x || state.surface.y !== y ||
                state.surface.visible !== visible ||
                (visible && (state.surface.displayWidth !== width ||
                    state.surface.displayHeight !== height));
            state.surface = {
                ...state.surface,
                hwnd,
                x,
                y,
                displayWidth: visible ? width : state.surface.displayWidth,
                displayHeight: visible ? height : state.surface.displayHeight,
                visible,
            };
            if (changed && typeof this.options.onSurface === "function") {
                this.options.onSurface(state.surface, visible ? reason : "hide");
            }
            return visible;
        }

        endPass() {
            if (this.frame && this.frame.pass) {
                this.frame.pass.end();
                this.frame.pass = null;
            }
        }

        mirrorRenderTargetClear(state, color, rectangles) {
            const handle = state.renderTarget.handle >>> 0;
            if (!handle) return;
            const resource = this.resources.get(handle);
            const level = resource && resource.shadowLevels[
                state.renderTarget.level >>> 0];
            if (!resource || resource.kind !== RESOURCE_TEXTURE_2D || !level ||
                    (resource.format !== D3DFMT_A8R8G8B8 &&
                     resource.format !== D3DFMT_X8R8G8B8)) return;
            const b = color & 0xFF;
            const g = (color >>> 8) & 0xFF;
            const r = (color >>> 16) & 0xFF;
            const a = resource.format === D3DFMT_A8R8G8B8 ?
                (color >>> 24) & 0xFF : 0xFF;
            const areas = rectangles && rectangles.length ? rectangles : [{
                x1: 0, y1: 0, x2: level.width, y2: level.height,
            }];
            for (const area of areas) {
                const x1 = Math.max(0, Math.min(level.width, area.x1 | 0));
                const y1 = Math.max(0, Math.min(level.height, area.y1 | 0));
                const x2 = Math.max(x1, Math.min(level.width, area.x2 | 0));
                const y2 = Math.max(y1, Math.min(level.height, area.y2 | 0));
                for (let y = y1; y < y2; y++) {
                    let offset = y * level.rowPitch + x1 * 4;
                    for (let x = x1; x < x2; x++, offset += 4) {
                        level.data[offset] = b;
                        level.data[offset + 1] = g;
                        level.data[offset + 2] = r;
                        level.data[offset + 3] = a;
                    }
                }
            }
        }

        clearPipelineFor(state, flags) {
            const hasDepth = !!(state.depthView && state.depthSurfaceEnabled &&
                !state.renderTarget.handle);
            const targetFormat = state.renderTarget.handle ?
                "rgba8unorm" : this.format;
            const effectiveFlags = (flags & D3DCLEAR_TARGET) |
                (hasDepth ? flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL) : 0);
            const key = ["rect-clear", targetFormat, hasDepth ? 1 : 0,
                effectiveFlags].join(":");
            let pipeline = this.pipelineCache.get(key);
            if (pipeline) {
                this.pipelineCache.delete(key);
                this.pipelineCache.set(key, pipeline);
                return pipeline;
            }
            const module = this.device.createShaderModule({
                label: "D3D8 rectangular Clear shader",
                code: `
struct ClearData {
    color: vec4<f32>,
    depth: vec4<f32>,
};
@group(0) @binding(0) var<uniform> clearData: ClearData;
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};
@vertex fn vs_main(@builtin(vertex_index) index: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0));
    var output: VertexOutput;
    output.position = vec4<f32>(positions[index], clearData.depth.x, 1.0);
    return output;
}
@fragment fn fs_main() -> @location(0) vec4<f32> {
    return clearData.color;
}`,
            });
            pipeline = this.device.createRenderPipeline({
                label: "D3D8 rectangular Clear pipeline " + key,
                layout: "auto",
                vertex: { module, entryPoint: "vs_main" },
                fragment: {
                    module,
                    entryPoint: "fs_main",
                    targets: [{
                        format: targetFormat,
                        writeMask: effectiveFlags & D3DCLEAR_TARGET ? 0xF : 0,
                    }],
                },
                primitive: { topology: "triangle-list", cullMode: "none" },
                ...(hasDepth ? { depthStencil: {
                    format: "depth24plus-stencil8",
                    depthWriteEnabled: !!(effectiveFlags & D3DCLEAR_ZBUFFER),
                    depthCompare: "always",
                    stencilFront: effectiveFlags & D3DCLEAR_STENCIL ? {
                        compare: "always", failOp: "keep",
                        depthFailOp: "keep", passOp: "replace",
                    } : {},
                    stencilBack: effectiveFlags & D3DCLEAR_STENCIL ? {
                        compare: "always", failOp: "keep",
                        depthFailOp: "keep", passOp: "replace",
                    } : {},
                    stencilReadMask: 0xFF,
                    stencilWriteMask: effectiveFlags & D3DCLEAR_STENCIL ?
                        0xFF : 0,
                } } : {}),
            });
            pipeline._d8wgId = this.nextPipelineId++;
            if (this.pipelineCache.size >= this.maxPipelines) {
                this.pipelineCache.delete(this.pipelineCache.keys().next().value);
                this.stats.pipelineCacheEvictions++;
            }
            this.pipelineCache.set(key, pipeline);
            this.stats.pipelineCreations++;
            return pipeline;
        }

        clearRectangles(state, flags, color, depth, stencil, rectangles) {
            const target = state.renderTarget.handle ?
                this.resources.get(state.renderTarget.handle) : null;
            const level = target ? state.renderTarget.level >>> 0 : 0;
            const width = target ? Math.max(1, target.width >>> level) :
                state.surface.width;
            const height = target ? Math.max(1, target.height >>> level) :
                state.surface.height;
            const hasDepth = !!(state.depthView && state.depthSurfaceEnabled &&
                !state.renderTarget.handle);
            if (!(flags & D3DCLEAR_TARGET) && !hasDepth) return;

            const data = new Uint8Array(32);
            const values = new DataView(data.buffer);
            const clearColor = d3dColor(color);
            values.setFloat32(0, clearColor.r, true);
            values.setFloat32(4, clearColor.g, true);
            values.setFloat32(8, clearColor.b, true);
            values.setFloat32(12, clearColor.a, true);
            values.setFloat32(16, Math.max(0, Math.min(1, depth)), true);
            const pass = this.ensureFrame(state);
            const uniform = this.device.createBuffer({
                label: "D3D8 rectangular Clear uniforms",
                size: data.byteLength,
                usage: BUFFER_USAGE_UNIFORM | BUFFER_USAGE_COPY_DST,
            });
            this.device.queue.writeBuffer(uniform, 0, data);
            this.frame.transientBuffers.push(uniform);
            const pipeline = this.clearPipelineFor(state, flags);
            const group = this.device.createBindGroup({
                layout: pipeline.getBindGroupLayout(0),
                entries: [{ binding: 0, resource: {
                    buffer: uniform,
                    offset: 0,
                    size: data.byteLength,
                } }],
            });
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, group);
            if (hasDepth && flags & D3DCLEAR_STENCIL)
                pass.setStencilReference(stencil & 0xFF);
            for (const area of rectangles) {
                const x1 = Math.max(0, Math.min(width, area.x1 | 0));
                const y1 = Math.max(0, Math.min(height, area.y1 | 0));
                const x2 = Math.max(x1, Math.min(width, area.x2 | 0));
                const y2 = Math.max(y1, Math.min(height, area.y2 | 0));
                if (x2 === x1 || y2 === y1) continue;
                pass.setScissorRect(x1, y1, x2 - x1, y2 - y1);
                pass.draw(3, 1, 0, 0);
                this.stats.rectangularClears++;
            }
        }

        ensureFrame(state, clearOptions) {
            const targetHandle = state.renderTarget.handle >>> 0;
            if (this.frame && (this.frame.sessionKey !== this.activeSession.key ||
                    this.frame.deviceHandle !== state.handle ||
                    this.frame.targetHandle !== targetHandle)) {
                this.finishFrame(false);
            }
            if (!this.frame) {
                const encoder = this.device.createCommandEncoder({
                    label: "D3D8 frame",
                });
                const target = targetHandle ? this.resources.get(targetHandle) : null;
                if (targetHandle && (!target ||
                        target.kind !== RESOURCE_TEXTURE_2D ||
                        !(target.usage & D3DUSAGE_RENDERTARGET))) {
                    throw new Error("active render target is stale");
                }
                const view = target ? target.gpuTexture.createView({
                    baseMipLevel: state.renderTarget.level,
                    mipLevelCount: 1,
                }) : this.context.getCurrentTexture().createView();
                this.frame = {
                    sessionKey: this.activeSession.key,
                    deviceHandle: state.handle,
                    targetHandle,
                    state,
                    encoder,
                    view,
                    pass: null,
                    fresh: true,
                    transientBuffers: [],
                    deferredDestroy: [],
                };
                this.transientCursor = 0;
                // Both rings restart each frame; the previous frame's slots are
                // already encoded into a submitted command buffer.
                this.uniformRingCursor = 0;
                this.frameSerial++;
            }
            if (clearOptions !== undefined) this.endPass();
            if (!this.frame.pass) {
                const clear = clearOptions || {};
                const fresh = this.frame.fresh;
                const descriptor = {
                    label: "D3D8 color pass",
                    colorAttachments: [{
                        view: this.frame.view,
                        clearValue: clear.color || { r: 0, g: 0, b: 0, a: 1 },
                        loadOp: fresh || clear.color ? "clear" : "load",
                        storeOp: "store",
                    }],
                };
                if (state.depthView && state.depthSurfaceEnabled &&
                        !targetHandle) {
                    descriptor.depthStencilAttachment = {
                        view: state.depthView,
                        depthClearValue: clear.depth === undefined ? 1 :
                            Math.max(0, Math.min(1, clear.depth)),
                        depthLoadOp: fresh || clear.depth !== undefined ?
                            "clear" : "load",
                        depthStoreOp: "store",
                        stencilClearValue: clear.stencil === undefined ? 0 :
                            clear.stencil & 0xFF,
                        stencilLoadOp: fresh || clear.stencil !== undefined ?
                            "clear" : "load",
                        stencilStoreOp: "store",
                    };
                }
                this.frame.pass = this.frame.encoder.beginRenderPass(descriptor);
                this.frame.fresh = false;
            }
            return this.frame.pass;
        }

        finishFrame(notify) {
            if (!this.frame) return false;
            const state = this.frame.state;
            const transientBuffers = this.frame.transientBuffers;
            const deferredDestroy = this.frame.deferredDestroy;
            this.endPass();
            this.device.queue.submit([this.frame.encoder.finish()]);
            this.stats.queueSubmits++;
            this.frame = null;
            this.scheduleGPUDestruction(transientBuffers.concat(
                deferredDestroy));
            if (notify) {
                this.stats.presents++;
                if (typeof this.options.onPresent === "function") {
                    this.options.onPresent(state.surface, this.getStats());
                }
            }
            return true;
        }

        destroyResource(handle) {
            const resource = this.resources.get(handle);
            if (resource) {
                this.retireHandle(this.retiredResourceHandles, handle);
                this.retireGPUObjects(resource.gpuBuffer,
                    resource.gpuTexture);
                this.resources.delete(handle);
                if (resource.kind === RESOURCE_TEXTURE_2D) {
                    for (const device of this.devices.values())
                        device.bindGroups.clear();
                }
            }
            const state = this.devices.get(handle);
            if (state) {
                this.retireHandle(this.retiredDeviceHandles, handle);
                if (this.frame && this.frame.deviceHandle === handle) {
                    this.discardFrame();
                }
                this.retireGPUObjects(state.depthTexture);
                for (const [resourceHandle, child] of this.resources) {
                    if (child.deviceHandle !== handle) continue;
                    this.retireHandle(this.retiredResourceHandles,
                        resourceHandle);
                    this.retireGPUObjects(child.gpuBuffer,
                        child.gpuTexture);
                    this.resources.delete(resourceHandle);
                }
                this.devices.delete(handle);
                if (typeof this.options.onDestroy === "function") {
                    this.options.onDestroy(state.surface, "device");
                }
            }
        }

        pipelineFor(state, topology, stride, indexFormat) {
            // A real vertex shader handle always carries bit 0 (see
            // D8WG_SHADER_HANDLE_BASE); state.fvf stays whatever it was last
            // set to (unused) once a real shader is bound.
            if (state.vertexShader & 1)
                return this.shaderPipelineFor(state, topology, stride,
                    indexFormat);
            const vertexLayout = parseFVF(state.fvf >>> 0, stride >>> 0);
            if (!vertexLayout) return null;
            const cull = state.renderStates[D3DRS_CULLMODE] >>> 0;
            const blend = state.renderStates[D3DRS_ALPHABLENDENABLE] >>> 0;
            const stripIndexFormat = topology.endsWith("-strip") ?
                indexFormat : undefined;
            const stageKey = [];
            const shaderStates = [D3DTSS_COLOROP, D3DTSS_COLORARG1,
                D3DTSS_COLORARG2, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1,
                D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX,
                D3DTSS_COLORARG0, D3DTSS_ALPHAARG0, D3DTSS_RESULTARG,
                D3DTSS_TEXTURETRANSFORMFLAGS];
            for (let stage = 0; stage < 2; stage++) {
                for (const selector of shaderStates)
                    stageKey.push(state.textureStageStates[stage][selector] >>> 0);
            }
            const targetFormat = state.renderTarget.handle ?
                "rgba8unorm" : this.format;
            const key = [targetFormat, topology, stripIndexFormat || "none",
                state.fvf >>> 0, stride >>> 0,
                cull, blend,
                state.renderStates[D3DRS_SRCBLEND] >>> 0,
                state.renderStates[D3DRS_DESTBLEND] >>> 0,
                state.renderStates[D3DRS_BLENDOP] >>> 0,
                state.renderStates[D3DRS_SHADEMODE] >>> 0,
                state.renderStates[D3DRS_ALPHATESTENABLE] >>> 0,
                state.renderStates[D3DRS_ALPHAFUNC] >>> 0,
                state.renderStates[D3DRS_SPECULARENABLE] >>> 0,
                state.renderStates[D3DRS_FOGENABLE] >>> 0,
                state.renderStates[D3DRS_FOGTABLEMODE] >>> 0,
                state.renderStates[D3DRS_FOGVERTEXMODE] >>> 0,
                state.renderStates[D3DRS_RANGEFOGENABLE] >>> 0,
                state.renderStates[D3DRS_LIGHTING] >>> 0,
                state.renderStates[D3DRS_COLORVERTEX] >>> 0,
                state.renderStates[D3DRS_LOCALVIEWER] >>> 0,
                state.renderStates[D3DRS_NORMALIZENORMALS] >>> 0,
                state.renderStates[D3DRS_DIFFUSEMATERIALSOURCE] >>> 0,
                state.renderStates[D3DRS_SPECULARMATERIALSOURCE] >>> 0,
                state.renderStates[D3DRS_AMBIENTMATERIALSOURCE] >>> 0,
                state.renderStates[D3DRS_EMISSIVEMATERIALSOURCE] >>> 0,
                state.renderStates[D3DRS_COLORWRITEENABLE] >>> 0,
                state.depthView && state.depthSurfaceEnabled &&
                    !state.renderTarget.handle ? 1 : 0,
                state.renderStates[D3DRS_ZENABLE] >>> 0,
                state.renderStates[D3DRS_ZWRITEENABLE] >>> 0,
                state.renderStates[D3DRS_ZFUNC] >>> 0,
                state.renderStates[D3DRS_ZBIAS] >>> 0,
                state.renderStates[D3DRS_STENCILENABLE] >>> 0,
                state.renderStates[D3DRS_STENCILFAIL] >>> 0,
                state.renderStates[D3DRS_STENCILZFAIL] >>> 0,
                state.renderStates[D3DRS_STENCILPASS] >>> 0,
                state.renderStates[D3DRS_STENCILFUNC] >>> 0,
                state.renderStates[D3DRS_STENCILMASK] >>> 0,
                state.renderStates[D3DRS_STENCILWRITEMASK] >>> 0,
                ...stageKey].join(":");
            let pipeline = this.pipelineCache.get(key);
            if (pipeline) {
                this.pipelineCache.delete(key);
                this.pipelineCache.set(key, pipeline);
                return pipeline;
            }
            const shaderModule = this.device.createShaderModule({
                label: "D3D8 fixed-function shader " + this.nextPipelineId,
                code: fixedFunctionShader(state, vertexLayout),
            });
            pipeline = this.device.createRenderPipeline({
                label: "D3D8 fixed pipeline " + key,
                layout: (this.fixedBindGroupLayoutFor(),
                    this.fixedPipelineLayout),
                vertex: {
                    module: shaderModule,
                    entryPoint: "vs_main",
                    buffers: [{
                        arrayStride: stride,
                        stepMode: "vertex",
                        attributes: vertexLayout.attributes,
                    }],
                },
                fragment: {
                    module: shaderModule,
                    entryPoint: "fs_main",
                    targets: [{
                        format: targetFormat,
                        ...(blend ? { blend: blendState(state) } : {}),
                        writeMask: state.renderStates[D3DRS_COLORWRITEENABLE] & 0xF,
                    }],
                },
                primitive: {
                    topology,
                    ...(stripIndexFormat ?
                        { stripIndexFormat } : {}),
                    cullMode: cull === D3DCULL_NONE ? "none" : "back",
                    // The screen-space Y conversion flips winding.
                    frontFace: cull === D3DCULL_CCW ? "cw" : "ccw",
                },
                ...(state.depthView && state.depthSurfaceEnabled &&
                        !state.renderTarget.handle ? { depthStencil: {
                    format: "depth24plus-stencil8",
                    depthWriteEnabled: !!state.renderStates[D3DRS_ZENABLE] &&
                        !!state.renderStates[D3DRS_ZWRITEENABLE],
                    depthCompare: state.renderStates[D3DRS_ZENABLE] ?
                        compareFunction(state.renderStates[D3DRS_ZFUNC]) :
                        "always",
                    stencilFront: state.renderStates[D3DRS_STENCILENABLE] ? {
                        compare: compareFunction(
                            state.renderStates[D3DRS_STENCILFUNC]),
                        failOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILFAIL]),
                        depthFailOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILZFAIL]),
                        passOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILPASS]),
                    } : {},
                    stencilBack: state.renderStates[D3DRS_STENCILENABLE] ? {
                        compare: compareFunction(
                            state.renderStates[D3DRS_STENCILFUNC]),
                        failOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILFAIL]),
                        depthFailOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILZFAIL]),
                        passOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILPASS]),
                    } : {},
                    stencilReadMask: state.renderStates[D3DRS_STENCILENABLE] ?
                        state.renderStates[D3DRS_STENCILMASK] >>> 0 : 0,
                    stencilWriteMask: state.renderStates[D3DRS_STENCILENABLE] ?
                        state.renderStates[D3DRS_STENCILWRITEMASK] >>> 0 : 0,
                    depthBias: -Math.min(16,
                        state.renderStates[D3DRS_ZBIAS] >>> 0),
                } } : {}),
            });
            pipeline._d8wgId = this.nextPipelineId++;
            if (this.pipelineCache.size >= this.maxPipelines) {
                this.pipelineCache.delete(this.pipelineCache.keys().next().value);
                this.stats.pipelineCacheEvictions++;
            }
            this.pipelineCache.set(key, pipeline);
            this.stats.pipelineCreations++;
            return pipeline;
        }

        // Stage 6: D3D8 shader model 1.x pipeline path. Reuses the same
        // pipelineCache Map as the fixed-function path (pipelineFor above) --
        // the key's leading tag keeps the two namespaces from ever
        // colliding -- and the same cull/blend/depth/stencil/color-write
        // render-state fields, since those aren't shader-specific. Only the
        // vertex-layout/fragment-combiner half of the key differs: real
        // shader mode keys on the bound (vertexShader, pixelShader) handle
        // pair instead of (fvf, texture-stage-state). Handles are never
        // reused within a session (see allocate_shader_handle in
        // d3d8_proxy.c), so the raw handle number is already a stable cache
        // key with no separate generation counter needed.
        // Stage 6 shader pipelines cannot use layout:"auto". WebGPU derives an
        // automatic layout from the resources a shader *statically uses*, and
        // a translated SM1.x shader may legitimately use none of them -- a
        // passthrough `mov oPos, v0 / mov oD0, v1` reads no constant bank and
        // samples no texture, so the derived group(0) layout comes back empty
        // and every CreateBindGroup against it fails validation. (The
        // fixed-function shader never hits this because it unconditionally
        // samples both stage textures and reads surface.texture_factor.)
        // Declaring the layout explicitly keeps all five bindings present
        // regardless of shader content; WebGPU permits an explicit layout to
        // contain bindings the shader does not reference.
        // Builds the five-entry layout shared by both draw paths. binding 0 is
        // a dynamic-offset uniform so one persistent ring buffer can back
        // every state variant (Stage 7): the bind group then depends only on
        // the bound textures and samplers, not on the uniform contents, which
        // is what keeps steady-state bind group creation at zero.
        buildBindGroupLayout(label, minBindingSize) {
            const VERTEX = 1;
            const FRAGMENT = 2;
            return this.device.createBindGroupLayout({
                label,
                entries: [
                    { binding: 0, visibility: VERTEX | FRAGMENT,
                        buffer: { type: "uniform", hasDynamicOffset: true,
                            minBindingSize } },
                    { binding: 1, visibility: FRAGMENT,
                        texture: { sampleType: "float", viewDimension: "2d" } },
                    { binding: 2, visibility: FRAGMENT,
                        sampler: { type: "filtering" } },
                    { binding: 3, visibility: FRAGMENT,
                        texture: { sampleType: "float", viewDimension: "2d" } },
                    { binding: 4, visibility: FRAGMENT,
                        sampler: { type: "filtering" } },
                ],
            });
        }

        fixedBindGroupLayoutFor() {
            if (this.fixedBindGroupLayout) return this.fixedBindGroupLayout;
            this.fixedBindGroupLayout = this.buildBindGroupLayout(
                "D3D8 fixed-function bind group layout", FIXED_UNIFORM_BYTES);
            this.fixedPipelineLayout = this.device.createPipelineLayout({
                label: "D3D8 fixed-function pipeline layout",
                bindGroupLayouts: [this.fixedBindGroupLayout],
            });
            return this.fixedBindGroupLayout;
        }

        shaderBindGroupLayoutFor() {
            if (this.shaderBindGroupLayout) return this.shaderBindGroupLayout;
            this.shaderBindGroupLayout = this.buildBindGroupLayout(
                "D3D8 shader bind group layout", SHADER_UNIFORM_BYTES);
            this.shaderPipelineLayout = this.device.createPipelineLayout({
                label: "D3D8 shader pipeline layout",
                bindGroupLayouts: [this.shaderBindGroupLayout],
            });
            return this.shaderBindGroupLayout;
        }

        shaderPipelineFor(state, topology, stride, indexFormat) {
            const vertexResource = this.resources.get(state.vertexShader);
            const pixelResource = state.pixelShader &&
                this.resources.get(state.pixelShader);
            if (!vertexResource || vertexResource.kind !== RESOURCE_VERTEX_SHADER)
                throw new Error("draw with an unresolved vertex shader handle");
            if (!pixelResource || pixelResource.kind !== RESOURCE_PIXEL_SHADER)
                throw new Error("draw with an unresolved pixel shader handle");
            const declaration = vertexResource.declaration;
            if (stride < declaration.stride)
                throw new Error("vertex buffer stride is smaller than the " +
                    "bound vertex shader's declaration requires");
            const cull = state.renderStates[D3DRS_CULLMODE] >>> 0;
            const blend = state.renderStates[D3DRS_ALPHABLENDENABLE] >>> 0;
            const stripIndexFormat = topology.endsWith("-strip") ?
                indexFormat : undefined;
            const targetFormat = state.renderTarget.handle ?
                "rgba8unorm" : this.format;
            const key = ["shader", state.vertexShader, state.pixelShader,
                targetFormat, topology, stripIndexFormat || "none",
                stride >>> 0, cull, blend,
                state.renderStates[D3DRS_SRCBLEND] >>> 0,
                state.renderStates[D3DRS_DESTBLEND] >>> 0,
                state.renderStates[D3DRS_BLENDOP] >>> 0,
                state.renderStates[D3DRS_COLORWRITEENABLE] >>> 0,
                state.depthView && state.depthSurfaceEnabled &&
                    !state.renderTarget.handle ? 1 : 0,
                state.renderStates[D3DRS_ZENABLE] >>> 0,
                state.renderStates[D3DRS_ZWRITEENABLE] >>> 0,
                state.renderStates[D3DRS_ZFUNC] >>> 0,
                state.renderStates[D3DRS_ZBIAS] >>> 0,
                state.renderStates[D3DRS_STENCILENABLE] >>> 0,
                state.renderStates[D3DRS_STENCILFAIL] >>> 0,
                state.renderStates[D3DRS_STENCILZFAIL] >>> 0,
                state.renderStates[D3DRS_STENCILPASS] >>> 0,
                state.renderStates[D3DRS_STENCILFUNC] >>> 0,
                state.renderStates[D3DRS_STENCILMASK] >>> 0,
                state.renderStates[D3DRS_STENCILWRITEMASK] >>> 0].join(":");
            let pipeline = this.pipelineCache.get(key);
            if (pipeline) {
                this.pipelineCache.delete(key);
                this.pipelineCache.set(key, pipeline);
                return pipeline;
            }
            const shaderModule = this.device.createShaderModule({
                label: "D3D8 shader " + state.vertexShader.toString(16) +
                    "/" + state.pixelShader.toString(16),
                code: shaderPipelineWgsl(vertexResource, declaration,
                    pixelResource),
            });
            this.shaderBindGroupLayoutFor();
            pipeline = this.device.createRenderPipeline({
                label: "D3D8 shader pipeline " + key,
                layout: this.shaderPipelineLayout,
                vertex: {
                    module: shaderModule,
                    entryPoint: "vs_main",
                    buffers: [{
                        arrayStride: stride,
                        stepMode: "vertex",
                        attributes: declaration.attributes,
                    }],
                },
                fragment: {
                    module: shaderModule,
                    entryPoint: "fs_main",
                    targets: [{
                        format: targetFormat,
                        ...(blend ? { blend: blendState(state) } : {}),
                        writeMask: state.renderStates[D3DRS_COLORWRITEENABLE] & 0xF,
                    }],
                },
                primitive: {
                    topology,
                    ...(stripIndexFormat ? { stripIndexFormat } : {}),
                    cullMode: cull === D3DCULL_NONE ? "none" : "back",
                    frontFace: cull === D3DCULL_CCW ? "cw" : "ccw",
                },
                ...(state.depthView && state.depthSurfaceEnabled &&
                        !state.renderTarget.handle ? { depthStencil: {
                    format: "depth24plus-stencil8",
                    depthWriteEnabled: !!state.renderStates[D3DRS_ZENABLE] &&
                        !!state.renderStates[D3DRS_ZWRITEENABLE],
                    depthCompare: state.renderStates[D3DRS_ZENABLE] ?
                        compareFunction(state.renderStates[D3DRS_ZFUNC]) :
                        "always",
                    stencilFront: state.renderStates[D3DRS_STENCILENABLE] ? {
                        compare: compareFunction(
                            state.renderStates[D3DRS_STENCILFUNC]),
                        failOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILFAIL]),
                        depthFailOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILZFAIL]),
                        passOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILPASS]),
                    } : {},
                    stencilBack: state.renderStates[D3DRS_STENCILENABLE] ? {
                        compare: compareFunction(
                            state.renderStates[D3DRS_STENCILFUNC]),
                        failOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILFAIL]),
                        depthFailOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILZFAIL]),
                        passOp: stencilOperation(
                            state.renderStates[D3DRS_STENCILPASS]),
                    } : {},
                    stencilReadMask: state.renderStates[D3DRS_STENCILENABLE] ?
                        state.renderStates[D3DRS_STENCILMASK] >>> 0 : 0,
                    stencilWriteMask: state.renderStates[D3DRS_STENCILENABLE] ?
                        state.renderStates[D3DRS_STENCILWRITEMASK] >>> 0 : 0,
                    depthBias: -Math.min(16,
                        state.renderStates[D3DRS_ZBIAS] >>> 0),
                } } : {}),
            });
            pipeline._d8wgId = this.nextPipelineId++;
            if (this.pipelineCache.size >= this.maxPipelines) {
                this.pipelineCache.delete(this.pipelineCache.keys().next().value);
                this.stats.pipelineCacheEvictions++;
            }
            this.pipelineCache.set(key, pipeline);
            this.stats.pipelineCreations++;
            return pipeline;
        }

        // Constant-register uniforms for real-shader-mode draws. Packs both vs
        // and ps banks into one uniform-ring slot matching
        // D8WGShaderConstants' WGSL layout (vs: array<vec4<f32>, 96> then
        // ps: array<vec4<f32>, 8>), reusing the slot while the constants are
        // unchanged within a frame.
        shaderUniformFor(state) {
            if (state.shaderConstantSlotSerial === state.shaderConstantSerial &&
                    state.shaderConstantSlotFrame === this.frameSerial &&
                    state.shaderConstantSlotBuffer) {
                this.stats.uniformSlotReuses++;
                return { buffer: state.shaderConstantSlotBuffer,
                    offset: state.shaderConstantSlotOffset };
            }
            const values = this.shaderConstantScratch;
            values.set(state.vsConstants, 0);
            values.set(state.psConstants, D8WG_MAX_VS_CONSTANTS * 4);
            const slot = this.allocateUniformSlot(SHADER_UNIFORM_BYTES);
            this.device.queue.writeBuffer(slot.buffer, slot.offset, values);
            this.stats.uniformUploadBytes += SHADER_UNIFORM_BYTES;
            state.shaderConstantSlotSerial = state.shaderConstantSerial;
            state.shaderConstantSlotFrame = this.frameSerial;
            state.shaderConstantSlotBuffer = slot.buffer;
            state.shaderConstantSlotOffset = slot.offset;
            return slot;
        }

        shaderBindGroupFor(state, pipeline) {
            const uniforms = this.shaderUniformFor(state);
            const texture0 = this.textureViewFor(state, 0);
            const texture1 = this.textureViewFor(state, 1);
            const sampler0 = this.samplerFor(state, 0);
            const sampler1 = this.samplerFor(state, 1);
            // The uniform slot is addressed by dynamic offset, so it is
            // deliberately NOT part of the bind group identity (doc 9.7).
            // The ring buffer object itself is, because an overflow
            // allocation is a different GPUBuffer.
            const key = "s:" + uniforms.buffer._d8wgRingId + ":" +
                texture0.key + ":" + sampler0._d8wgKey + ":" +
                texture1.key + ":" + sampler1._d8wgKey;
            let group = state.bindGroups.get(key);
            if (group) {
                state.bindGroups.delete(key);
                state.bindGroups.set(key, group);
                this.stats.bindGroupHits++;
                return { group, offset: uniforms.offset };
            }
            group = this.device.createBindGroup({
                label: "D3D8 shader bind group",
                // The explicit layout, not pipeline.getBindGroupLayout(0):
                // see shaderBindGroupLayoutFor for why "auto" cannot work here.
                layout: this.shaderBindGroupLayoutFor(),
                entries: [
                    { binding: 0, resource: { buffer: uniforms.buffer,
                        offset: 0, size: SHADER_UNIFORM_BYTES } },
                    { binding: 1, resource: texture0.view },
                    { binding: 2, resource: sampler0 },
                    { binding: 3, resource: texture1.view },
                    { binding: 4, resource: sampler1 },
                ],
            });
            if (state.bindGroups.size >= this.maxBindGroups)
                state.bindGroups.delete(state.bindGroups.keys().next().value);
            state.bindGroups.set(key, group);
            this.stats.bindGroupCreations++;
            return { group, offset: uniforms.offset };
        }

        samplerFor(state, stage) {
            const values = state.textureStageStates[stage];
            const address = value => value === 1 ? "repeat" :
                value === 2 ? "mirror-repeat" : "clamp-to-edge";
            const min = values[D3DTSS_MINFILTER] === 1 ? "nearest" : "linear";
            const mag = values[D3DTSS_MAGFILTER] === 1 ? "nearest" : "linear";
            const mip = values[D3DTSS_MIPFILTER] === 2 ? "linear" : "nearest";
            let anisotropy = Math.max(1,
                Math.min(16, values[D3DTSS_MAXANISOTROPY] || 1));
            if (min !== "linear" || mag !== "linear" || mip !== "linear")
                anisotropy = 1;
            const key = [address(values[D3DTSS_ADDRESSU]),
                address(values[D3DTSS_ADDRESSV]), min, mag, mip,
                values[D3DTSS_MIPFILTER] === 0 ? 0 : 32,
                anisotropy].join(":");
            let sampler = this.samplerCache.get(key);
            if (!sampler) {
                sampler = this.device.createSampler({
                    addressModeU: address(values[D3DTSS_ADDRESSU]),
                    addressModeV: address(values[D3DTSS_ADDRESSV]),
                    addressModeW: "clamp-to-edge",
                    minFilter: min,
                    magFilter: mag,
                    mipmapFilter: mip,
                    maxLod: values[D3DTSS_MIPFILTER] === 0 ? 0 : 32,
                    maxAnisotropy: anisotropy,
                });
                sampler._d8wgKey = key;
                if (this.samplerCache.size >= this.maxSamplers)
                    this.samplerCache.delete(this.samplerCache.keys().next().value);
                this.samplerCache.set(key, sampler);
            } else {
                this.samplerCache.delete(key);
                this.samplerCache.set(key, sampler);
            }
            return sampler;
        }

        textureViewFor(state, stage) {
            const resource = this.resources.get(state.textures[stage]);
            if (!resource || resource.kind !== RESOURCE_TEXTURE_2D)
                return { resource: null, view: this.fallbackView, key: "fallback" };
            const baseLevel = Math.min(resource.levelCount - 1,
                state.textureStageStates[stage][D3DTSS_MAXMIPLEVEL] >>> 0);
            let view = resource.views.get(baseLevel);
            if (!view) {
                view = resource.gpuTexture.createView({
                    baseMipLevel: baseLevel,
                    mipLevelCount: resource.levelCount - baseLevel,
                });
                resource.views.set(baseLevel, view);
            }
            return { resource, view, key: resource.handle + "@" + baseLevel };
        }

        bindGroupFor(state, pipeline) {
            if (state.vertexShader & 1)
                return this.shaderBindGroupFor(state, pipeline);
            const uniforms = this.uniformFor(state);
            const texture0 = this.textureViewFor(state, 0);
            const texture1 = this.textureViewFor(state, 1);
            const sampler0 = this.samplerFor(state, 0);
            const sampler1 = this.samplerFor(state, 1);
            // Neither the pipeline nor the uniform contents belong in this key:
            // every fixed-function pipeline shares one explicit bind group
            // layout, and the uniform slot is reached by dynamic offset. What
            // remains is the ring buffer identity plus the bound textures and
            // samplers, which is what makes steady-state creation zero.
            const key = "f:" + uniforms.buffer._d8wgRingId + ":" +
                texture0.key + ":" + sampler0._d8wgKey + ":" +
                texture1.key + ":" + sampler1._d8wgKey;
            let group = state.bindGroups.get(key);
            if (group) {
                state.bindGroups.delete(key);
                state.bindGroups.set(key, group);
                this.stats.bindGroupHits++;
                return { group, offset: uniforms.offset };
            }
            group = this.device.createBindGroup({
                label: "D3D8 fixed-function bind group",
                layout: this.fixedBindGroupLayoutFor(),
                entries: [
                    { binding: 0, resource: { buffer: uniforms.buffer,
                        offset: 0, size: FIXED_UNIFORM_BYTES } },
                    { binding: 1, resource: texture0.view },
                    { binding: 2, resource: sampler0 },
                    { binding: 3, resource: texture1.view },
                    { binding: 4, resource: sampler1 },
                ],
            });
            if (state.bindGroups.size >= this.maxBindGroups)
                state.bindGroups.delete(state.bindGroups.keys().next().value);
            state.bindGroups.set(key, group);
            this.stats.bindGroupCreations++;
            return { group, offset: uniforms.offset };
        }

        validateGeometryState(state, stride) {
            if (state.vertexShader & 1) {
                const resource = this.resources.get(state.vertexShader);
                if (!resource || resource.kind !== RESOURCE_VERTEX_SHADER ||
                        stride < resource.declaration.stride) {
                    this.warnOnce("vs-" + state.vertexShader,
                        "draw with an unresolved/incompatible vertex shader",
                        "0x" + state.vertexShader.toString(16));
                    this.stats.unsupportedCommands++;
                    return false;
                }
                return true;
            }
            const layout = parseFVF(state.fvf >>> 0, stride >>> 0);
            if (!layout) {
                this.warnOnce("fvf-" + state.fvf,
                    "unsupported FVF in the WebGPU Maple 2D path",
                    "0x" + state.fvf.toString(16));
                this.stats.unsupportedCommands++;
                return false;
            }
            return true;
        }

        createTransientBuffer(data, usage, label) {
            if (!this.frame) throw new Error("transient buffer created outside a frame");
            const upload = padded4(data);
            let buffer = this.transientBuffer;
            let offset = align4(this.transientCursor);
            if (upload.byteLength > this.transientCapacity - offset) {
                buffer = this.device.createBuffer({
                    label: label + " overflow",
                    size: Math.max(4, upload.byteLength),
                    usage: usage | BUFFER_USAGE_COPY_SRC | BUFFER_USAGE_COPY_DST,
                });
                offset = 0;
                this.frame.transientBuffers.push(buffer);
                this.stats.transientBufferCreations++;
            } else {
                this.transientCursor = offset + upload.byteLength;
            }
            this.device.queue.writeBuffer(buffer, offset, upload);
            this.stats.transientUploadBytes += data.byteLength;
            return { buffer, offset, size: upload.byteLength };
        }

        preparePass(state, pass) {
            const x = Math.min(state.surface.width, state.viewport.x >>> 0);
            const y = Math.min(state.surface.height, state.viewport.y >>> 0);
            const width = Math.min(state.viewport.width >>> 0,
                state.surface.width - x);
            const height = Math.min(state.viewport.height >>> 0,
                state.surface.height - y);
            if (!width || !height) return false;
            if ((state.fvf & D3DFVF_POSITION_MASK) === D3DFVF_XYZ)
                pass.setViewport(x, y, width, height,
                    state.viewport.minZ, state.viewport.maxZ);
            else
                pass.setViewport(0, 0, state.surface.width,
                    state.surface.height, 0, 1);
            if (state.depthView)
                pass.setStencilReference(
                    state.renderStates[D3DRS_STENCILREF] & 0xFF);
            pass.setScissorRect(x, y, width, height);
            return true;
        }

        createTextureResource(bytes, payloadOffset, commandEnd) {
            if (commandEnd - payloadOffset < 32)
                throw new Error("short CREATE_TEXTURE");
            const deviceHandle = u32(bytes, payloadOffset);
            const handle = u32(bytes, payloadOffset + 4);
            const width = u32(bytes, payloadOffset + 8);
            const height = u32(bytes, payloadOffset + 12);
            const levelCount = u32(bytes, payloadOffset + 16);
            const format = u32(bytes, payloadOffset + 20);
            if (!this.devices.has(deviceHandle))
                throw new Error("CREATE_TEXTURE references an unknown device");
            if (!width || !height || !levelCount || !textureFormatInfo(format))
                throw new Error("invalid CREATE_TEXTURE dimensions/format");
            const maximumLevels = 1 + Math.floor(Math.log2(
                Math.max(width, height)));
            if (levelCount > maximumLevels)
                throw new Error("CREATE_TEXTURE has too many mip levels");
            this.destroyResource(handle);
            const gpuTexture = this.device.createTexture({
                label: "D3D8 texture " + handle.toString(16),
                size: { width, height, depthOrArrayLayers: 1 },
                mipLevelCount: levelCount,
                sampleCount: 1,
                dimension: "2d",
                format: "rgba8unorm",
                usage: TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING |
                    ((u32(bytes, payloadOffset + 24) & D3DUSAGE_RENDERTARGET) ?
                        TEXTURE_USAGE_RENDER_ATTACHMENT : 0),
            });
            const formatInfo = textureFormatInfo(format);
            const shadowLevels = [];
            for (let level = 0; level < levelCount; level++) {
                const levelWidth = Math.max(1, width >>> level);
                const levelHeight = Math.max(1, height >>> level);
                const columns = Math.ceil(levelWidth / formatInfo.blockWidth);
                const rows = Math.ceil(levelHeight / formatInfo.blockHeight);
                const rowPitch = columns * formatInfo.blockBytes;
                shadowLevels.push({
                    width: levelWidth,
                    height: levelHeight,
                    rowPitch,
                    rows,
                    data: new Uint8Array(rowPitch * rows),
                });
            }
            this.resources.set(handle, {
                handle,
                deviceHandle,
                kind: RESOURCE_TEXTURE_2D,
                width,
                height,
                levelCount,
                format,
                usage: u32(bytes, payloadOffset + 24),
                pool: u32(bytes, payloadOffset + 28),
                gpuTexture,
                views: new Map([[0, gpuTexture.createView()]]),
                shadowLevels,
            });
        }

        updateTextureResource(bytes, payloadOffset, commandEnd) {
            if (commandEnd - payloadOffset < 40)
                throw new Error("short UPDATE_TEXTURE");
            const resource = this.resources.get(u32(bytes, payloadOffset));
            if (!resource || resource.kind !== RESOURCE_TEXTURE_2D)
                throw new Error("UPDATE_TEXTURE references an unknown texture");
            const level = u32(bytes, payloadOffset + 4);
            const x = u32(bytes, payloadOffset + 8);
            const y = u32(bytes, payloadOffset + 12);
            const width = u32(bytes, payloadOffset + 16);
            const height = u32(bytes, payloadOffset + 20);
            const rowPitch = u32(bytes, payloadOffset + 24);
            const dataBytes = u32(bytes, payloadOffset + 28);
            const dataOffset = u32(bytes, payloadOffset + 32);
            if (level >= resource.levelCount || !width || !height)
                throw new Error("invalid UPDATE_TEXTURE mip/extent");
            const levelWidth = Math.max(1, resource.width >>> level);
            const levelHeight = Math.max(1, resource.height >>> level);
            if (x > levelWidth || width > levelWidth - x ||
                    y > levelHeight || height > levelHeight - y)
                throw new Error("UPDATE_TEXTURE rectangle exceeds its mip level");
            const format = textureFormatInfo(resource.format);
            const columns = Math.ceil(width / format.blockWidth);
            const rows = Math.ceil(height / format.blockHeight);
            const minimumRow = columns * format.blockBytes;
            const minimumBytes = rowPitch * (rows - 1) + minimumRow;
            if (rowPitch < minimumRow || dataBytes < minimumBytes)
                throw new Error("UPDATE_TEXTURE pitch/data is too small");
            if (format.dxt && ((x & 3) || (y & 3) ||
                    ((width & 3) && x + width !== levelWidth) ||
                    ((height & 3) && y + height !== levelHeight)))
                throw new Error("UPDATE_TEXTURE DXT rectangle is not block aligned");
            const source = checkedDataRange(bytes, dataOffset, dataBytes,
                "UPDATE_TEXTURE data");
            const shadow = resource.shadowLevels[level];
            const blockX = Math.floor(x / format.blockWidth);
            const blockY = Math.floor(y / format.blockHeight);
            for (let row = 0; row < rows; row++) {
                const sourceStart = row * rowPitch;
                const destinationStart = (blockY + row) * shadow.rowPitch +
                    blockX * format.blockBytes;
                shadow.data.set(source.subarray(sourceStart,
                    sourceStart + minimumRow), destinationStart);
            }
            const rgba = decodeTextureUpload(resource.format, source,
                width, height, rowPitch);
            const destination = {
                texture: resource.gpuTexture,
                mipLevel: level,
                origin: { x, y, z: 0 },
            };
            const extent = { width, height, depthOrArrayLayers: 1 };
            if (this.frame) {
                this.endPass();
                const packedPitch = width * 4;
                const uploadPitch = (packedPitch + 255) & ~255;
                const padded = new Uint8Array(uploadPitch * height);
                for (let row = 0; row < height; row++)
                    padded.set(rgba.subarray(row * packedPitch,
                        (row + 1) * packedPitch), row * uploadPitch);
                const staging = this.createTransientBuffer(padded,
                    BUFFER_USAGE_COPY_SRC, "D3D8 ordered texture upload");
                this.frame.encoder.copyBufferToTexture({
                    buffer: staging.buffer,
                    offset: staging.offset,
                    bytesPerRow: uploadPitch,
                    rowsPerImage: height,
                }, destination, extent);
            } else {
                this.device.queue.writeTexture(destination, rgba, {
                    bytesPerRow: width * 4,
                    rowsPerImage: height,
                }, extent);
            }
            this.stats.uploadBytes += dataBytes;
        }

        // Stage 6: D3D8 shader model 1.x. Both creators translate the
        // bytecode eagerly (discarding the WGSL text here; shaderPipelineFor
        // regenerates and caches it per pipeline variant) so a genuinely
        // unsupported/malformed shader fails CreateVertexShader/
        // CreatePixelShader itself rather than surfacing later at the first
        // draw that binds it -- matches the "illegal bytecode is rejected up
        // front" rule the guest-side validator already enforces.
        createVertexShaderResource(bytes, payloadOffset, commandEnd) {
            if (commandEnd - payloadOffset < 24)
                throw new Error("short CREATE_VERTEX_SHADER");
            const deviceHandle = u32(bytes, payloadOffset);
            const handle = u32(bytes, payloadOffset + 4);
            const declCount = u32(bytes, payloadOffset + 8);
            const declOffset = u32(bytes, payloadOffset + 12);
            const codeCount = u32(bytes, payloadOffset + 16);
            const codeOffset = u32(bytes, payloadOffset + 20);
            if (!this.devices.has(deviceHandle))
                throw new Error(
                    "CREATE_VERTEX_SHADER references an unknown device");
            if (!codeCount)
                throw new Error("CREATE_VERTEX_SHADER has an empty code blob");
            const declBytes = checkedDataRange(bytes, declOffset,
                declCount * 4, "CREATE_VERTEX_SHADER declaration");
            const codeBytes = checkedDataRange(bytes, codeOffset,
                codeCount * 4, "CREATE_VERTEX_SHADER code");
            const declTokens = [];
            for (let i = 0; i < declCount; i++)
                declTokens.push(u32(declBytes, i * 4));
            const codeTokens = [];
            for (let i = 0; i < codeCount; i++)
                codeTokens.push(u32(codeBytes, i * 4));
            const declaration = parseVertexDeclaration(declTokens);
            vertexShaderWgsl(codeTokens, declaration);
            this.destroyResource(handle);
            // Reset keeps app-visible shader handles stable (see
            // recreate_device_resources in d3d8_proxy.c), so a handle retired
            // with the previous device epoch is legitimately live again once
            // the guest re-creates it under the new one.
            this.retiredResourceHandles.delete(handle);
            this.resources.set(handle, {
                handle,
                deviceHandle,
                kind: RESOURCE_VERTEX_SHADER,
                codeTokens,
                // Kept verbatim so serializeSession can replay the original
                // CREATE_VERTEX_SHADER after a v86 save/load.
                declTokens,
                declaration,
            });
        }

        createPixelShaderResource(bytes, payloadOffset, commandEnd) {
            if (commandEnd - payloadOffset < 16)
                throw new Error("short CREATE_PIXEL_SHADER");
            const deviceHandle = u32(bytes, payloadOffset);
            const handle = u32(bytes, payloadOffset + 4);
            const codeCount = u32(bytes, payloadOffset + 8);
            const codeOffset = u32(bytes, payloadOffset + 12);
            if (!this.devices.has(deviceHandle))
                throw new Error(
                    "CREATE_PIXEL_SHADER references an unknown device");
            if (!codeCount)
                throw new Error("CREATE_PIXEL_SHADER has an empty code blob");
            const codeBytes = checkedDataRange(bytes, codeOffset,
                codeCount * 4, "CREATE_PIXEL_SHADER code");
            const codeTokens = [];
            for (let i = 0; i < codeCount; i++)
                codeTokens.push(u32(codeBytes, i * 4));
            pixelShaderWgsl(codeTokens);
            this.destroyResource(handle);
            this.retiredResourceHandles.delete(handle);
            this.resources.set(handle, {
                handle,
                deviceHandle,
                kind: RESOURCE_PIXEL_SHADER,
                codeTokens,
            });
        }

        sequentialFanIndices(vertexCount) {
            const use32 = vertexCount > 0xFFFF;
            const values = use32 ? new Uint32Array((vertexCount - 2) * 3) :
                new Uint16Array((vertexCount - 2) * 3);
            let output = 0;
            for (let vertex = 1; vertex + 1 < vertexCount; vertex++) {
                values[output++] = 0;
                values[output++] = vertex;
                values[output++] = vertex + 1;
            }
            this.stats.fanConversions++;
            return { data: new Uint8Array(values.buffer),
                format: use32 ? "uint32" : "uint16", count: values.length };
        }

        convertFanIndices(source, formatInfo, indexCount) {
            const values = new formatInfo.ArrayType((indexCount - 2) * 3);
            const view = new DataView(source.buffer, source.byteOffset,
                source.byteLength);
            const read = formatInfo.bytes === 2 ?
                offset => view.getUint16(offset, true) :
                offset => view.getUint32(offset, true);
            const centre = read(0);
            let output = 0;
            for (let index = 1; index + 1 < indexCount; index++) {
                values[output++] = centre;
                values[output++] = read(index * formatInfo.bytes);
                values[output++] = read((index + 1) * formatInfo.bytes);
            }
            this.stats.fanConversions++;
            return { data: new Uint8Array(values.buffer),
                format: formatInfo.webgpu, count: values.length };
        }

        drawPrimitive(bytes, payloadOffset) {
            const state = this.devices.get(u32(bytes, payloadOffset));
            if (!state) throw new Error("draw references an unknown D3D8 device");
            const primitive = primitiveInfo(u32(bytes, payloadOffset + 4),
                u32(bytes, payloadOffset + 12));
            if (!primitive) {
                this.warnOnce("primitive-" + u32(bytes, payloadOffset + 4),
                    "unsupported primitive topology", u32(bytes, payloadOffset + 4));
                this.stats.unsupportedCommands++;
                return;
            }
            const stream = state.streams[0];
            const resource = this.resources.get(stream.handle);
            if (!resource || resource.kind !== RESOURCE_BUFFER_VERTEX) {
                throw new Error("draw references an unknown vertex buffer");
            }
            if (!this.validateGeometryState(state, stream.stride)) return;
            const startVertex = u32(bytes, payloadOffset + 8);
            const availableVertices = Math.floor(resource.byteCount / stream.stride);
            if (startVertex > availableVertices ||
                primitive.vertices > availableVertices - startVertex) {
                throw new Error("draw vertex range exceeds its buffer");
            }
            const pipeline = this.pipelineFor(state, primitive.topology,
                stream.stride);
            if (!pipeline) return;
            const pass = this.ensureFrame(state);
            if (!this.preparePass(state, pass)) return;
            pass.setPipeline(pipeline);
            const binding = this.bindGroupFor(state, pipeline);
            DYNAMIC_OFFSETS[0] = binding.offset;
            pass.setBindGroup(0, binding.group, DYNAMIC_OFFSETS, 0, 1);
            pass.setVertexBuffer(0, resource.gpuBuffer);
            if (primitive.fan) {
                const fan = this.sequentialFanIndices(primitive.vertices);
                const indexBuffer = this.createTransientBuffer(fan.data,
                    BUFFER_USAGE_INDEX, "D3D8 triangle fan indices");
                pass.setIndexBuffer(indexBuffer.buffer, fan.format,
                    indexBuffer.offset, indexBuffer.size);
                pass.drawIndexed(fan.count, 1, 0, startVertex, 0);
            } else {
                pass.draw(primitive.vertices, 1, startVertex, 0);
            }
            this.stats.drawCalls++;
        }

        drawIndexedPrimitive(bytes, payloadOffset) {
            const state = this.devices.get(u32(bytes, payloadOffset));
            if (!state) throw new Error("indexed draw references an unknown device");
            const primitive = primitiveInfo(u32(bytes, payloadOffset + 4),
                u32(bytes, payloadOffset + 20));
            if (!primitive) throw new Error("invalid indexed primitive topology");
            const stream = state.streams[0];
            const vertexResource = this.resources.get(stream.handle);
            if (!vertexResource || vertexResource.kind !== RESOURCE_BUFFER_VERTEX) {
                throw new Error("indexed draw references an unknown vertex buffer");
            }
            const indexResource = this.resources.get(state.indices.handle);
            if (!indexResource || indexResource.kind !== RESOURCE_BUFFER_INDEX) {
                throw new Error("indexed draw references an unknown index buffer");
            }
            const formatInfo = indexFormatInfo(indexResource.format);
            if (!formatInfo) throw new Error("indexed draw uses an invalid index format");
            if (!this.validateGeometryState(state, stream.stride)) return;
            const startIndex = u32(bytes, payloadOffset + 16);
            const availableIndices = Math.floor(indexResource.byteCount /
                formatInfo.bytes);
            if (startIndex > availableIndices ||
                primitive.vertices > availableIndices - startIndex) {
                throw new Error("indexed draw range exceeds its index buffer");
            }
            const minVertex = u32(bytes, payloadOffset + 8);
            const vertexCount = u32(bytes, payloadOffset + 12);
            const availableVertices = Math.floor(vertexResource.byteCount /
                stream.stride);
            if (state.indices.baseVertex > 0x7FFFFFFF ||
                state.indices.baseVertex + minVertex > availableVertices ||
                vertexCount > availableVertices -
                    (state.indices.baseVertex + minVertex)) {
                throw new Error("indexed draw range exceeds its vertex buffer");
            }
            const pipeline = this.pipelineFor(state, primitive.topology,
                stream.stride, primitive.fan ? undefined : formatInfo.webgpu);
            if (!pipeline) return;
            const pass = this.ensureFrame(state);
            if (!this.preparePass(state, pass)) return;
            pass.setPipeline(pipeline);
            const binding = this.bindGroupFor(state, pipeline);
            DYNAMIC_OFFSETS[0] = binding.offset;
            pass.setBindGroup(0, binding.group, DYNAMIC_OFFSETS, 0, 1);
            pass.setVertexBuffer(0, vertexResource.gpuBuffer);
            if (primitive.fan) {
                const sourceOffset = startIndex * formatInfo.bytes;
                const sourceBytes = primitive.vertices * formatInfo.bytes;
                if (sourceOffset > indexResource.byteCount ||
                    sourceBytes > indexResource.byteCount - sourceOffset) {
                    throw new Error("triangle fan index range exceeds its buffer");
                }
                const fan = this.convertFanIndices(indexResource.shadow.subarray(
                    sourceOffset, sourceOffset + sourceBytes), formatInfo,
                    primitive.vertices);
                const indexBuffer = this.createTransientBuffer(fan.data,
                    BUFFER_USAGE_INDEX, "D3D8 converted indexed fan");
                pass.setIndexBuffer(indexBuffer.buffer, fan.format,
                    indexBuffer.offset, indexBuffer.size);
                pass.drawIndexed(fan.count, 1, 0, state.indices.baseVertex, 0);
            } else {
                pass.setIndexBuffer(indexResource.gpuBuffer, formatInfo.webgpu);
                pass.drawIndexed(primitive.vertices, 1, startIndex,
                    state.indices.baseVertex, 0);
            }
            this.stats.drawCalls++;
            this.stats.indexedDrawCalls++;
        }

        drawPrimitiveUP(bytes, payloadOffset) {
            const state = this.devices.get(u32(bytes, payloadOffset));
            if (!state) throw new Error("UP draw references an unknown device");
            const primitive = primitiveInfo(u32(bytes, payloadOffset + 4),
                u32(bytes, payloadOffset + 8));
            if (!primitive) throw new Error("invalid UP primitive topology");
            const stride = u32(bytes, payloadOffset + 12);
            const vertexCount = u32(bytes, payloadOffset + 16);
            const vertexBytes = u32(bytes, payloadOffset + 20);
            if (!stride || vertexCount !== primitive.vertices ||
                vertexCount > Math.floor(0xFFFFFFFF / stride) ||
                vertexCount * stride !== vertexBytes) {
                throw new Error("DRAW_PRIMITIVE_UP size metadata mismatch");
            }
            if (!this.validateGeometryState(state, stride)) return;
            const data = checkedDataRange(bytes, u32(bytes, payloadOffset + 24),
                vertexBytes, "DRAW_PRIMITIVE_UP vertex data");
            const pipeline = this.pipelineFor(state, primitive.topology, stride);
            if (!pipeline) return;
            const pass = this.ensureFrame(state);
            if (!this.preparePass(state, pass)) return;
            const vertexBuffer = this.createTransientBuffer(data,
                BUFFER_USAGE_VERTEX, "D3D8 DrawPrimitiveUP vertices");
            pass.setPipeline(pipeline);
            const binding = this.bindGroupFor(state, pipeline);
            DYNAMIC_OFFSETS[0] = binding.offset;
            pass.setBindGroup(0, binding.group, DYNAMIC_OFFSETS, 0, 1);
            pass.setVertexBuffer(0, vertexBuffer.buffer, vertexBuffer.offset,
                vertexBuffer.size);
            if (primitive.fan) {
                const fan = this.sequentialFanIndices(vertexCount);
                const indexBuffer = this.createTransientBuffer(fan.data,
                    BUFFER_USAGE_INDEX, "D3D8 UP triangle fan indices");
                pass.setIndexBuffer(indexBuffer.buffer, fan.format,
                    indexBuffer.offset, indexBuffer.size);
                pass.drawIndexed(fan.count, 1, 0, 0, 0);
            } else {
                pass.draw(vertexCount, 1, 0, 0);
            }
            this.stats.drawCalls++;
            this.stats.upDrawCalls++;
            state.streams[0] = { handle: 0, stride: 0 };
        }

        drawIndexedPrimitiveUP(bytes, payloadOffset) {
            const state = this.devices.get(u32(bytes, payloadOffset));
            if (!state) throw new Error("indexed UP draw references an unknown device");
            const primitive = primitiveInfo(u32(bytes, payloadOffset + 4),
                u32(bytes, payloadOffset + 16));
            if (!primitive) throw new Error("invalid indexed UP topology");
            const formatInfo = indexFormatInfo(u32(bytes, payloadOffset + 20));
            if (!formatInfo) throw new Error("invalid indexed UP format");
            const stride = u32(bytes, payloadOffset + 24);
            const indexCount = u32(bytes, payloadOffset + 28);
            const indexBytes = u32(bytes, payloadOffset + 32);
            const vertexBytes = u32(bytes, payloadOffset + 36);
            if (!stride || indexCount !== primitive.vertices ||
                indexCount > Math.floor(0xFFFFFFFF / formatInfo.bytes) ||
                indexCount * formatInfo.bytes !== indexBytes ||
                vertexBytes % stride !== 0) {
                throw new Error("DRAW_INDEXED_PRIMITIVE_UP size metadata mismatch");
            }
            const minVertex = u32(bytes, payloadOffset + 8);
            const vertexCount = u32(bytes, payloadOffset + 12);
            if (minVertex > 0xFFFFFFFF - vertexCount ||
                minVertex + vertexCount > Math.floor(0xFFFFFFFF / stride) ||
                (minVertex + vertexCount) * stride !== vertexBytes) {
                throw new Error("DRAW_INDEXED_PRIMITIVE_UP vertex range mismatch");
            }
            if (!this.validateGeometryState(state, stride)) return;
            let indexData = checkedDataRange(bytes,
                u32(bytes, payloadOffset + 40), indexBytes,
                "DRAW_INDEXED_PRIMITIVE_UP index data");
            const vertexData = checkedDataRange(bytes,
                u32(bytes, payloadOffset + 44), vertexBytes,
                "DRAW_INDEXED_PRIMITIVE_UP vertex data");
            const pipeline = this.pipelineFor(state, primitive.topology, stride,
                primitive.fan ? undefined : formatInfo.webgpu);
            if (!pipeline) return;
            const pass = this.ensureFrame(state);
            if (!this.preparePass(state, pass)) return;
            const vertexBuffer = this.createTransientBuffer(vertexData,
                BUFFER_USAGE_VERTEX, "D3D8 DrawIndexedPrimitiveUP vertices");
            let drawIndexCount = indexCount;
            let webgpuFormat = formatInfo.webgpu;
            if (primitive.fan) {
                const fan = this.convertFanIndices(indexData, formatInfo, indexCount);
                indexData = fan.data;
                drawIndexCount = fan.count;
                webgpuFormat = fan.format;
            }
            const indexBuffer = this.createTransientBuffer(indexData,
                BUFFER_USAGE_INDEX, "D3D8 DrawIndexedPrimitiveUP indices");
            pass.setPipeline(pipeline);
            const binding = this.bindGroupFor(state, pipeline);
            DYNAMIC_OFFSETS[0] = binding.offset;
            pass.setBindGroup(0, binding.group, DYNAMIC_OFFSETS, 0, 1);
            pass.setVertexBuffer(0, vertexBuffer.buffer, vertexBuffer.offset,
                vertexBuffer.size);
            pass.setIndexBuffer(indexBuffer.buffer, webgpuFormat,
                indexBuffer.offset, indexBuffer.size);
            pass.drawIndexed(drawIndexCount, 1, 0, 0, 0);
            this.stats.drawCalls++;
            this.stats.indexedDrawCalls++;
            this.stats.upDrawCalls++;
            state.streams[0] = { handle: 0, stride: 0 };
            state.indices = { handle: 0, baseVertex: 0 };
        }

        executeCommand(bytes, commandOffset, opcode, payloadOffset,
                commandEnd) {
            const firstHandle = commandEnd - payloadOffset >= 4 ?
                u32(bytes, payloadOffset) : 0;
            const resourceOpcode = opcode === OP_UPDATE_BUFFER ||
                opcode === OP_UPDATE_TEXTURE;
            if ((resourceOpcode &&
                    this.retiredResourceHandles.has(firstHandle)) ||
                    (opcode === OP_DESTROY_RESOURCE &&
                        (this.retiredResourceHandles.has(firstHandle) ||
                         this.retiredDeviceHandles.has(firstHandle))) ||
                    (opcode !== OP_CREATE_DEVICE && !resourceOpcode &&
                        opcode !== OP_DESTROY_RESOURCE &&
                        this.retiredDeviceHandles.has(firstHandle))) {
                if ((opcode === OP_CREATE_BUFFER || opcode === OP_CREATE_TEXTURE)
                        && commandEnd - payloadOffset >= 8) {
                    this.retireHandle(this.retiredResourceHandles,
                        u32(bytes, payloadOffset + 4));
                }
                this.stats.staleCommandsDropped++;
                return;
            }
            switch (opcode) {
            case OP_HELLO:
                if (commandEnd - payloadOffset < 16 ||
                        u32(bytes, payloadOffset) !== 32) {
                    throw new Error("only a 32-bit D8WG guest is supported");
                }
                if (u32(bytes, payloadOffset + 8) !== this.activeSession.low ||
                        u32(bytes, payloadOffset + 12) !==
                            this.activeSession.high) {
                    throw new Error("HELLO process session does not match its batch");
                }
                this.activeSession.helloSeen = true;
                break;
            case OP_CREATE_DEVICE:
                if (commandEnd - payloadOffset < 44) throw new Error("short CREATE_DEVICE");
                this.createOrResetDevice(bytes, payloadOffset, false);
                break;
            case OP_RESET:
                if (commandEnd - payloadOffset < 48) throw new Error("short RESET");
                this.resetDevice(bytes, payloadOffset);
                break;
            case OP_UPDATE_SURFACE: {
                if (commandEnd - payloadOffset < 24) {
                    throw new Error("short UPDATE_SURFACE");
                }
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) {
                    throw new Error("UPDATE_SURFACE references an unknown device");
                }
                this.updateSurface(bytes, payloadOffset, state, "move");
                break;
            }
            case OP_PRESENT: {
                if (commandEnd - payloadOffset < 24) throw new Error("short PRESENT");
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) throw new Error("PRESENT references an unknown device");
                this.updateSurface(bytes, payloadOffset, state, "present");
                this.ensureFrame(state);
                this.finishFrame(true);
                break;
            }
            case OP_CLEAR: {
                if (commandEnd - payloadOffset < 24) throw new Error("short CLEAR");
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) throw new Error("CLEAR references an unknown device");
                const flags = u32(bytes, payloadOffset + 4);
                const rectCount = u32(bytes, payloadOffset + 20);
                if (rectCount > Math.floor((commandEnd - payloadOffset - 24) /
                        16)) {
                    throw new Error("CLEAR rectangle array is truncated");
                }
                const rectangles = [];
                for (let index = 0; index < rectCount; index++) {
                    const offset = payloadOffset + 24 + index * 16;
                    rectangles.push({
                        x1: i32(bytes, offset),
                        y1: i32(bytes, offset + 4),
                        x2: i32(bytes, offset + 8),
                        y2: i32(bytes, offset + 12),
                    });
                }
                const color = u32(bytes, payloadOffset + 8);
                const depth = f32(bytes, payloadOffset + 12);
                const stencil = u32(bytes, payloadOffset + 16);
                const clear = {};
                if (flags & D3DCLEAR_TARGET) {
                    clear.color = d3dColor(color);
                    // Keep the canonical save-state shadow coherent for the
                    // supported A8/X8 lockable render-target formats.
                    this.mirrorRenderTargetClear(state, color, rectangles);
                }
                if (flags & D3DCLEAR_ZBUFFER)
                    clear.depth = depth;
                if (flags & D3DCLEAR_STENCIL)
                    clear.stencil = stencil;
                if ((flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) &&
                        !state.depthView) {
                    this.warnOnce("depth-clear-without-surface",
                        "depth/stencil Clear ignored because the device has no automatic depth surface");
                    delete clear.depth;
                    delete clear.stencil;
                }
                if (rectangles.length)
                    this.clearRectangles(state, flags, color, depth,
                        stencil, rectangles);
                else
                    this.ensureFrame(state, clear);
                break;
            }
            case OP_BEGIN_SCENE:
            case OP_END_SCENE: {
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) throw new Error("scene command references an unknown device");
                state.inScene = opcode === OP_BEGIN_SCENE;
                break;
            }
            case OP_CREATE_BUFFER: {
                if (commandEnd - payloadOffset < 32) throw new Error("short CREATE_BUFFER");
                const deviceHandle = u32(bytes, payloadOffset);
                const handle = u32(bytes, payloadOffset + 4);
                const kind = u32(bytes, payloadOffset + 8);
                const byteCount = u32(bytes, payloadOffset + 12);
                if (kind !== RESOURCE_BUFFER_VERTEX &&
                    kind !== RESOURCE_BUFFER_INDEX) {
                    throw new Error("unknown D8WG buffer kind " + kind);
                }
                if (!this.devices.has(deviceHandle))
                    throw new Error("CREATE_BUFFER references an unknown device");
                const format = u32(bytes, payloadOffset + 20);
                if (kind === RESOURCE_BUFFER_INDEX && !indexFormatInfo(format)) {
                    throw new Error("invalid D8WG index buffer format " + format);
                }
                this.destroyResource(handle);
                this.resources.set(handle, {
                    handle,
                    deviceHandle,
                    kind,
                    byteCount,
                    usage: u32(bytes, payloadOffset + 16),
                    pool: u32(bytes, payloadOffset + 24),
                    fvf: kind === RESOURCE_BUFFER_VERTEX ? format : 0,
                    format: kind === RESOURCE_BUFFER_INDEX ? format : 0,
                    shadow: new Uint8Array(align4(byteCount)),
                    gpuBuffer: this.device.createBuffer({
                        label: "D3D8 " + (kind === RESOURCE_BUFFER_VERTEX ?
                            "vertex" : "index") + " buffer " + handle.toString(16),
                        size: Math.max(4, align4(byteCount)),
                        usage: (kind === RESOURCE_BUFFER_VERTEX ?
                            BUFFER_USAGE_VERTEX : BUFFER_USAGE_INDEX) |
                            BUFFER_USAGE_COPY_DST,
                    }),
                });
                break;
            }
            case OP_UPDATE_BUFFER: {
                if (commandEnd - payloadOffset < 24) throw new Error("short UPDATE_BUFFER");
                const resource = this.resources.get(u32(bytes, payloadOffset));
                if (!resource) throw new Error("UPDATE_BUFFER references an unknown resource");
                const destination = u32(bytes, payloadOffset + 4);
                const byteCount = u32(bytes, payloadOffset + 8);
                const dataOffset = u32(bytes, payloadOffset + 12);
                const lockFlags = u32(bytes, payloadOffset + 16);
                if (dataOffset > bytes.length || byteCount > bytes.length - dataOffset ||
                    destination > resource.byteCount ||
                    byteCount > resource.byteCount - destination) {
                    throw new Error("UPDATE_BUFFER range is outside its batch/resource");
                }
                if (lockFlags & D3DLOCK_DISCARD) {
                    const previous = resource.gpuBuffer;
                    resource.shadow.fill(0);
                    resource.gpuBuffer = this.device.createBuffer({
                        label: "D3D8 orphaned " +
                            (resource.kind === RESOURCE_BUFFER_VERTEX ?
                                "vertex" : "index") + " buffer " +
                            resource.handle.toString(16),
                        size: Math.max(4, align4(resource.byteCount)),
                        usage: (resource.kind === RESOURCE_BUFFER_VERTEX ?
                            BUFFER_USAGE_VERTEX : BUFFER_USAGE_INDEX) |
                            BUFFER_USAGE_COPY_DST,
                    });
                    if (this.frame)
                        this.frame.transientBuffers.push(previous);
                    else
                        this.retireGPUObjects(previous);
                    this.stats.bufferOrphans++;
                }
                resource.shadow.set(
                    bytes.subarray(dataOffset, dataOffset + byteCount), destination);
                const alignedStart = destination & ~3;
                const alignedEnd = align4(destination + byteCount);
                const source = resource.shadow.subarray(alignedStart, alignedEnd);
                if (source.byteLength && this.frame) {
                    this.endPass();
                    const staging = this.createTransientBuffer(source,
                        BUFFER_USAGE_COPY_SRC,
                        "D3D8 ordered buffer upload staging");
                    this.frame.encoder.copyBufferToBuffer(staging.buffer,
                        staging.offset,
                        resource.gpuBuffer, alignedStart, source.byteLength);
                } else if (source.byteLength) {
                    this.device.queue.writeBuffer(resource.gpuBuffer,
                        alignedStart, source);
                }
                this.stats.uploadBytes += byteCount;
                break;
            }
            case OP_CREATE_TEXTURE:
                this.createTextureResource(bytes, payloadOffset, commandEnd);
                break;
            case OP_UPDATE_TEXTURE:
                this.updateTextureResource(bytes, payloadOffset, commandEnd);
                break;
            case OP_CREATE_VERTEX_SHADER:
                this.createVertexShaderResource(bytes, payloadOffset,
                    commandEnd);
                break;
            case OP_CREATE_PIXEL_SHADER:
                this.createPixelShaderResource(bytes, payloadOffset,
                    commandEnd);
                break;
            case OP_DESTROY_RESOURCE:
                if (commandEnd - payloadOffset < 8) {
                    throw new Error("short DESTROY_RESOURCE");
                }
                this.destroyResource(u32(bytes, payloadOffset));
                break;
            case OP_SET_RENDER_STATE: {
                const state = this.devices.get(u32(bytes, payloadOffset));
                const index = u32(bytes, payloadOffset + 4);
                if (!state || index >= state.renderStates.length) {
                    throw new Error("invalid SET_RENDER_STATE");
                }
                const value = u32(bytes, payloadOffset + 8);
                if (state.renderStates[index] === value) {
                    // Host-side dead-store elimination. The guest already
                    // suppresses redundant setters, but state-block replay and
                    // Reset restoration legitimately resend whole banks.
                    this.stats.redundantStateWrites++;
                    break;
                }
                state.renderStates[index] = value;
                // Only bump the uniform serial for render states the uniform
                // block actually contains; blend/cull/depth/stencil and the
                // rest are pipeline state and would otherwise force a
                // needless uniform repack and upload on every change.
                if (UNIFORM_RENDER_STATES[index]) state.uniformSerial++;
                break;
            }
            case OP_SET_TEXTURE_STAGE_STATE: {
                const state = this.devices.get(u32(bytes, payloadOffset));
                const stage = u32(bytes, payloadOffset + 4);
                const index = u32(bytes, payloadOffset + 8);
                if (!state || stage >= 8 || index >= 32) {
                    throw new Error("invalid SET_TEXTURE_STAGE_STATE");
                }
                state.textureStageStates[stage][index] = u32(bytes, payloadOffset + 12);
                break;
            }
            case OP_SET_TEXTURE: {
                if (commandEnd - payloadOffset < 16)
                    throw new Error("short SET_TEXTURE");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const stage = u32(bytes, payloadOffset + 4);
                const handle = u32(bytes, payloadOffset + 8);
                const resource = handle ? this.resources.get(handle) : null;
                if (!state || stage >= state.textures.length ||
                        (handle && (!resource ||
                            resource.kind !== RESOURCE_TEXTURE_2D ||
                            resource.deviceHandle !== state.handle)))
                    throw new Error("invalid SET_TEXTURE");
                state.textures[stage] = handle;
                break;
            }
            case OP_SET_VIEWPORT: {
                if (commandEnd - payloadOffset < 32)
                    throw new Error("short SET_VIEWPORT");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const width = u32(bytes, payloadOffset + 12);
                const height = u32(bytes, payloadOffset + 16);
                const minZ = f32(bytes, payloadOffset + 20);
                const maxZ = f32(bytes, payloadOffset + 24);
                if (!state || !width || !height || !Number.isFinite(minZ) ||
                        !Number.isFinite(maxZ) || minZ < 0 || maxZ > 1 ||
                        minZ > maxZ)
                    throw new Error("invalid SET_VIEWPORT");
                state.viewport = {
                    x: u32(bytes, payloadOffset + 4),
                    y: u32(bytes, payloadOffset + 8),
                    width,
                    height,
                    minZ,
                    maxZ,
                };
                break;
            }
            case OP_SET_TRANSFORM: {
                if (commandEnd - payloadOffset < 72)
                    throw new Error("short SET_TRANSFORM");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const transformState = u32(bytes, payloadOffset + 4);
                if (!state) throw new Error("invalid SET_TRANSFORM device");
                const matrix = new Float32Array(16);
                for (let index = 0; index < 16; index++) {
                    const value = f32(bytes, payloadOffset + 8 + index * 4);
                    if (!Number.isFinite(value))
                        throw new Error("SET_TRANSFORM contains a non-finite matrix");
                    matrix[index] = value;
                }
                if (transformState === 2)
                    state.transforms.view = matrix;
                else if (transformState === 3)
                    state.transforms.projection = matrix;
                else if (transformState >= 16 && transformState <= 23)
                    state.transforms.textures[transformState - 16] = matrix;
                else if (transformState >= 256 && transformState < 512) {
                    if (transformState === 256)
                        state.transforms.world = matrix;
                    else
                        this.warnOnce("world-transform-" + transformState,
                            "indexed world transform is stored but vertex blending is not yet enabled",
                            transformState);
                } else {
                    throw new Error("invalid D3D8 transform state " + transformState);
                }
                state.uniformSerial++;
                break;
            }
            case OP_SET_MATERIAL: {
                if (commandEnd - payloadOffset < 72)
                    throw new Error("short SET_MATERIAL");
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) throw new Error("invalid SET_MATERIAL device");
                const read = (offset, count) => Array.from({ length: count },
                    (_, index) => f32(bytes, payloadOffset + offset + index * 4));
                const values = read(4, 17);
                if (!values.every(Number.isFinite))
                    throw new Error("SET_MATERIAL contains a non-finite value");
                state.material = {
                    diffuse: values.slice(0, 4),
                    ambient: values.slice(4, 8),
                    specular: values.slice(8, 12),
                    emissive: values.slice(12, 16),
                    power: values[16],
                };
                state.uniformSerial++;
                break;
            }
            case OP_SET_LIGHT: {
                if (commandEnd - payloadOffset < 112)
                    throw new Error("short SET_LIGHT");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const index = u32(bytes, payloadOffset + 4);
                const type = u32(bytes, payloadOffset + 8);
                if (!state || index >= state.lights.length ||
                        type < 1 || type > 3)
                    throw new Error("invalid SET_LIGHT");
                const read = (offset, count) => Array.from({ length: count },
                    (_, item) => f32(bytes, payloadOffset + offset + item * 4));
                const values = read(12, 25);
                if (!values.every(Number.isFinite))
                    throw new Error("SET_LIGHT contains a non-finite value");
                const enabled = state.lights[index].enabled;
                state.lights[index] = {
                    type,
                    diffuse: values.slice(0, 4),
                    specular: values.slice(4, 8),
                    ambient: values.slice(8, 12),
                    position: values.slice(12, 15),
                    direction: values.slice(15, 18),
                    range: values[18], falloff: values[19],
                    attenuation: values.slice(20, 23),
                    theta: values[23], phi: values[24], enabled,
                };
                state.uniformSerial++;
                break;
            }
            case OP_LIGHT_ENABLE: {
                if (commandEnd - payloadOffset < 16)
                    throw new Error("short LIGHT_ENABLE");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const index = u32(bytes, payloadOffset + 4);
                if (!state || index >= state.lights.length)
                    throw new Error("invalid LIGHT_ENABLE");
                state.lights[index].enabled = !!u32(bytes, payloadOffset + 8);
                state.uniformSerial++;
                break;
            }
            case OP_SET_STREAM_SOURCE: {
                const state = this.devices.get(u32(bytes, payloadOffset));
                const stream = u32(bytes, payloadOffset + 4);
                if (!state || stream >= state.streams.length) {
                    throw new Error("invalid SET_STREAM_SOURCE");
                }
                state.streams[stream] = {
                    handle: u32(bytes, payloadOffset + 8),
                    stride: u32(bytes, payloadOffset + 12),
                };
                break;
            }
            case OP_SET_INDICES: {
                if (commandEnd - payloadOffset < 16) throw new Error("short SET_INDICES");
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) throw new Error("invalid SET_INDICES device");
                state.indices = {
                    handle: u32(bytes, payloadOffset + 4),
                    baseVertex: u32(bytes, payloadOffset + 8),
                };
                break;
            }
            case OP_SET_VERTEX_FORMAT: {
                const state = this.devices.get(u32(bytes, payloadOffset));
                if (!state) throw new Error("invalid SET_VERTEX_FORMAT");
                state.fvf = u32(bytes, payloadOffset + 4);
                break;
            }
            case OP_SET_RENDER_TARGET: {
                if (commandEnd - payloadOffset < 16)
                    throw new Error("short SET_RENDER_TARGET");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const handle = u32(bytes, payloadOffset + 4);
                const level = u32(bytes, payloadOffset + 8);
                const resource = handle ? this.resources.get(handle) : null;
                if (!state || (handle && (!resource ||
                        resource.deviceHandle !== state.handle ||
                        resource.kind !== RESOURCE_TEXTURE_2D ||
                        !(resource.usage & D3DUSAGE_RENDERTARGET) ||
                        level >= resource.levelCount)))
                    throw new Error("invalid SET_RENDER_TARGET");
                if (this.frame) this.finishFrame(false);
                state.renderTarget = { handle, level };
                state.depthSurfaceEnabled = !!u32(bytes, payloadOffset + 12);
                break;
            }
            case OP_SET_VERTEX_SHADER: {
                if (commandEnd - payloadOffset < 8)
                    throw new Error("short SET_VERTEX_SHADER");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const handle = u32(bytes, payloadOffset + 4);
                const resource = handle ? this.resources.get(handle) : null;
                if (!state || (handle && (!resource ||
                        resource.kind !== RESOURCE_VERTEX_SHADER ||
                        resource.deviceHandle !== state.handle)))
                    throw new Error("invalid SET_VERTEX_SHADER");
                state.vertexShader = handle;
                break;
            }
            case OP_SET_PIXEL_SHADER: {
                if (commandEnd - payloadOffset < 8)
                    throw new Error("short SET_PIXEL_SHADER");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const handle = u32(bytes, payloadOffset + 4);
                const resource = handle ? this.resources.get(handle) : null;
                if (!state || (handle && (!resource ||
                        resource.kind !== RESOURCE_PIXEL_SHADER ||
                        resource.deviceHandle !== state.handle)))
                    throw new Error("invalid SET_PIXEL_SHADER");
                state.pixelShader = handle;
                break;
            }
            case OP_SET_VERTEX_SHADER_CONSTANT:
            case OP_SET_PIXEL_SHADER_CONSTANT: {
                if (commandEnd - payloadOffset < 16)
                    throw new Error("short SET_SHADER_CONSTANT");
                const state = this.devices.get(u32(bytes, payloadOffset));
                const startRegister = u32(bytes, payloadOffset + 4);
                const vectorCount = u32(bytes, payloadOffset + 8);
                const dataOffset = u32(bytes, payloadOffset + 12);
                const isVertex = opcode === OP_SET_VERTEX_SHADER_CONSTANT;
                const maxRegisters = isVertex ?
                    D8WG_MAX_VS_CONSTANTS : D8WG_MAX_PS_CONSTANTS;
                if (!state || startRegister > maxRegisters ||
                        vectorCount > maxRegisters - startRegister)
                    throw new Error("invalid SET_SHADER_CONSTANT range");
                const data = checkedDataRange(bytes, dataOffset,
                    vectorCount * 16, "SET_SHADER_CONSTANT data");
                const target = isVertex ? state.vsConstants : state.psConstants;
                for (let i = 0; i < vectorCount * 4; i++)
                    target[startRegister * 4 + i] = f32(data, i * 4);
                state.shaderConstantSerial++;
                break;
            }
            case OP_DRAW_PRIMITIVE:
                if (commandEnd - payloadOffset < 16) throw new Error("short DRAW_PRIMITIVE");
                this.drawPrimitive(bytes, payloadOffset);
                break;
            case OP_DRAW_INDEXED_PRIMITIVE:
                if (commandEnd - payloadOffset < 24) {
                    throw new Error("short DRAW_INDEXED_PRIMITIVE");
                }
                this.drawIndexedPrimitive(bytes, payloadOffset);
                break;
            case OP_DRAW_PRIMITIVE_UP:
                if (commandEnd - payloadOffset < 32) {
                    throw new Error("short DRAW_PRIMITIVE_UP");
                }
                this.drawPrimitiveUP(bytes, payloadOffset);
                break;
            case OP_DRAW_INDEXED_PRIMITIVE_UP:
                if (commandEnd - payloadOffset < 48) {
                    throw new Error("short DRAW_INDEXED_PRIMITIVE_UP");
                }
                this.drawIndexedPrimitiveUP(bytes, payloadOffset);
                break;
            default:
                this.warnOnce("opcode-" + opcode,
                    "unsupported D8WG opcode", "0x" + opcode.toString(16));
                this.stats.unsupportedCommands++;
                break;
            }
            void commandOffset;
        }

        executeBatch(bytes, metadata) {
            if (bytes.length < D8WG_BATCH_HEADER_BYTES) {
                this.stats.malformedBatches++;
                throw new Error("D8WG batch header is truncated");
            }
            if (u32(bytes, 0) !== D8WG_MAGIC) {
                this.stats.malformedBatches++;
                throw new Error("D8WG magic mismatch");
            }
            if (u16(bytes, 4) !== D8WG_VERSION_MAJOR) {
                this.stats.malformedBatches++;
                throw new Error("unsupported D8WG major version " + u16(bytes, 4));
            }
            if (u16(bytes, 6) !== D8WG_VERSION_MINOR) {
                this.stats.malformedBatches++;
                throw new Error("unsupported D8WG minor version " + u16(bytes, 6));
            }
            const session = this.activateSession(u32(bytes, 24),
                u32(bytes, 28));
            const expectedCount = u32(bytes, 16);
            const commandBytes = u32(bytes, 20);
            if (commandBytes > bytes.length - D8WG_BATCH_HEADER_BYTES) {
                this.stats.malformedBatches++;
                throw new Error("D8WG command region is truncated");
            }
            const end = D8WG_BATCH_HEADER_BYTES + commandBytes;
            let offset = D8WG_BATCH_HEADER_BYTES;
            let decoded = 0;
            if (!session.helloSeen && expectedCount &&
                    u16(bytes, offset) !== OP_HELLO) {
                throw new Error("D8WG process session began without HELLO");
            }
            while (offset < end) {
                if (end - offset < D8WG_COMMAND_HEADER_BYTES) {
                    throw new Error("D8WG command header is truncated");
                }
                const opcode = u16(bytes, offset);
                const size = u32(bytes, offset + 4);
                if (size < D8WG_COMMAND_HEADER_BYTES || (size & 7) || size > end - offset) {
                    throw new Error("invalid D8WG command size " + size);
                }
                this.executeCommand(bytes, offset, opcode,
                    offset + D8WG_COMMAND_HEADER_BYTES, offset + size);
                offset += size;
                decoded++;
            }
            if (decoded !== expectedCount) {
                throw new Error("D8WG command count mismatch: expected " +
                    expectedCount + ", decoded " + decoded);
            }
            this.stats.batches++;
            this.stats.commands += decoded;
            if (this.options.trace) {
                console.info("[d3d8-webgpu] batch", {
                    frameId: u32(bytes, 8),
                    session: session.key,
                    flags: u32(bytes, 12),
                    commandCount: decoded,
                    commandBytes,
                    pci: metadata,
                });
            }
            return decoded;
        }

        serializeSession(session) {
            this.setActiveSession(session, false);
            const records = [];
            const makePayload = byteLength => new Uint8Array(byteLength);
            const putU32 = (payload, offset, value) =>
                new DataView(payload.buffer, payload.byteOffset,
                    payload.byteLength).setUint32(offset, value >>> 0, true);
            const putI32 = (payload, offset, value) =>
                new DataView(payload.buffer, payload.byteOffset,
                    payload.byteLength).setInt32(offset, value | 0, true);
            const putF32 = (payload, offset, value) =>
                new DataView(payload.buffer, payload.byteOffset,
                    payload.byteLength).setFloat32(offset, value, true);
            const u32Payload = (...values) => {
                const payload = makePayload(values.length * 4);
                values.forEach((value, index) => putU32(payload,
                    index * 4, value));
                return payload;
            };
            // dataOffsetField is either a single payload offset to patch with
            // the blob's batch-relative address, or an array of
            // {field, delta} for a payload that points at several regions
            // inside one concatenated blob (CREATE_VERTEX_SHADER).
            const add = (opcode, payload, blob, dataOffsetField) => {
                records.push({ opcode, payload, blob: blob || null,
                    dataOffsetField });
            };
            const tokenBlob = tokens => {
                const blob = makePayload(tokens.length * 4);
                tokens.forEach((token, index) =>
                    putU32(blob, index * 4, token));
                return blob;
            };
            const concatBlobs = (first, second) => {
                const blob = makePayload(first.byteLength + second.byteLength);
                blob.set(first, 0);
                blob.set(second, first.byteLength);
                return blob;
            };
            const addTransform = (handle, transformState, matrix) => {
                const payload = makePayload(72);
                putU32(payload, 0, handle);
                putU32(payload, 4, transformState);
                for (let index = 0; index < 16; index++)
                    putF32(payload, 8 + index * 4, matrix[index]);
                add(OP_SET_TRANSFORM, payload);
            };

            add(OP_HELLO, u32Payload(32, 0, session.low, session.high));

            for (const state of this.devices.values()) {
                const surface = state.surface;
                const create = makePayload(44);
                putU32(create, 0, state.handle);
                putU32(create, 4, surface.hwnd);
                putI32(create, 8, surface.x);
                putI32(create, 12, surface.y);
                putU32(create, 16, surface.width);
                putU32(create, 20, surface.height);
                putU32(create, 24, surface.format);
                putU32(create, 28, surface.windowed ? 1 : 0);
                putU32(create, 32, surface.behaviorFlags);
                putU32(create, 36, surface.autoDepthStencil ? 1 : 0);
                putU32(create, 40, surface.autoDepthStencilFormat);
                add(OP_CREATE_DEVICE, create);

                for (const resource of this.resources.values()) {
                    if (resource.deviceHandle !== state.handle) continue;
                    if (resource.kind === RESOURCE_BUFFER_VERTEX ||
                            resource.kind === RESOURCE_BUFFER_INDEX) {
                        add(OP_CREATE_BUFFER, u32Payload(state.handle,
                            resource.handle, resource.kind,
                            resource.byteCount, resource.usage,
                            resource.kind === RESOURCE_BUFFER_VERTEX ?
                                resource.fvf : resource.format,
                            resource.pool, 0));
                        add(OP_UPDATE_BUFFER, u32Payload(resource.handle, 0,
                            resource.byteCount, 0, 0, 0),
                            resource.shadow.subarray(0, resource.byteCount), 12);
                    } else if (resource.kind === RESOURCE_TEXTURE_2D) {
                        add(OP_CREATE_TEXTURE, u32Payload(state.handle,
                            resource.handle, resource.width, resource.height,
                            resource.levelCount, resource.format,
                            resource.usage, resource.pool));
                        resource.shadowLevels.forEach((level, levelIndex) => {
                            add(OP_UPDATE_TEXTURE, u32Payload(resource.handle,
                                levelIndex, 0, 0, level.width, level.height,
                                level.rowPitch, level.data.byteLength, 0, 0),
                                level.data, 32);
                        });
                    } else if (resource.kind === RESOURCE_VERTEX_SHADER) {
                        const declaration = tokenBlob(resource.declTokens);
                        const code = tokenBlob(resource.codeTokens);
                        add(OP_CREATE_VERTEX_SHADER, u32Payload(state.handle,
                            resource.handle, resource.declTokens.length, 0,
                            resource.codeTokens.length, 0),
                            concatBlobs(declaration, code),
                            [{ field: 12, delta: 0 },
                                { field: 20, delta: declaration.byteLength }]);
                    } else if (resource.kind === RESOURCE_PIXEL_SHADER) {
                        add(OP_CREATE_PIXEL_SHADER, u32Payload(state.handle,
                            resource.handle, resource.codeTokens.length, 0),
                            tokenBlob(resource.codeTokens), 12);
                    }
                }

                for (let index = 0; index < state.renderStates.length; index++)
                    add(OP_SET_RENDER_STATE, u32Payload(state.handle, index,
                        state.renderStates[index], 0));
                for (let stage = 0; stage < 8; stage++) {
                    for (let index = 0; index < 32; index++) {
                        add(OP_SET_TEXTURE_STAGE_STATE, u32Payload(state.handle,
                            stage, index,
                            state.textureStageStates[stage][index]));
                    }
                }
                const viewport = makePayload(32);
                putU32(viewport, 0, state.handle);
                putU32(viewport, 4, state.viewport.x);
                putU32(viewport, 8, state.viewport.y);
                putU32(viewport, 12, state.viewport.width);
                putU32(viewport, 16, state.viewport.height);
                putF32(viewport, 20, state.viewport.minZ);
                putF32(viewport, 24, state.viewport.maxZ);
                add(OP_SET_VIEWPORT, viewport);
                addTransform(state.handle, 256, state.transforms.world);
                addTransform(state.handle, 2, state.transforms.view);
                addTransform(state.handle, 3, state.transforms.projection);
                state.transforms.textures.forEach((matrix, index) =>
                    addTransform(state.handle, 16 + index, matrix));

                const material = makePayload(72);
                putU32(material, 0, state.handle);
                [state.material.diffuse, state.material.ambient,
                    state.material.specular, state.material.emissive]
                    .forEach((values, group) => values.forEach((value, index) =>
                        putF32(material, 4 + group * 16 + index * 4, value)));
                putF32(material, 68, state.material.power);
                add(OP_SET_MATERIAL, material);
                state.lights.forEach((light, index) => {
                    if (!light.type) return;
                    const payload = makePayload(112);
                    putU32(payload, 0, state.handle);
                    putU32(payload, 4, index);
                    putU32(payload, 8, light.type);
                    [light.diffuse, light.specular, light.ambient]
                        .forEach((values, group) => values.forEach(
                            (value, item) => putF32(payload,
                                12 + group * 16 + item * 4, value)));
                    light.position.forEach((value, item) =>
                        putF32(payload, 60 + item * 4, value));
                    light.direction.forEach((value, item) =>
                        putF32(payload, 72 + item * 4, value));
                    putF32(payload, 84, light.range);
                    putF32(payload, 88, light.falloff);
                    light.attenuation.forEach((value, item) =>
                        putF32(payload, 92 + item * 4, value));
                    putF32(payload, 104, light.theta);
                    putF32(payload, 108, light.phi);
                    add(OP_SET_LIGHT, payload);
                    if (light.enabled)
                        add(OP_LIGHT_ENABLE, u32Payload(state.handle,
                            index, 1, 0));
                });
                for (let stage = 0; stage < state.textures.length; stage++)
                    add(OP_SET_TEXTURE, u32Payload(state.handle, stage,
                        state.textures[stage], 0));
                state.streams.forEach((stream, index) =>
                    add(OP_SET_STREAM_SOURCE, u32Payload(state.handle, index,
                        stream.handle, stream.stride)));
                add(OP_SET_INDICES, u32Payload(state.handle,
                    state.indices.handle, state.indices.baseVertex, 0));
                add(OP_SET_VERTEX_FORMAT, u32Payload(state.handle, state.fvf));
                // Constant banks first: a restored shader must see the same
                // constants it had at save time before anything draws with it.
                const constantBlob = values => {
                    const blob = makePayload(values.byteLength);
                    values.forEach((value, index) =>
                        putF32(blob, index * 4, value));
                    return blob;
                };
                add(OP_SET_VERTEX_SHADER_CONSTANT, u32Payload(state.handle, 0,
                    D8WG_MAX_VS_CONSTANTS, 0),
                    constantBlob(state.vsConstants), 12);
                add(OP_SET_PIXEL_SHADER_CONSTANT, u32Payload(state.handle, 0,
                    D8WG_MAX_PS_CONSTANTS, 0),
                    constantBlob(state.psConstants), 12);
                if (state.vertexShader)
                    add(OP_SET_VERTEX_SHADER, u32Payload(state.handle,
                        state.vertexShader));
                if (state.pixelShader)
                    add(OP_SET_PIXEL_SHADER, u32Payload(state.handle,
                        state.pixelShader));
                add(OP_SET_RENDER_TARGET, u32Payload(state.handle,
                    state.renderTarget.handle, state.renderTarget.level,
                    state.depthSurfaceEnabled ? 1 : 0));
                if (state.inScene)
                    add(OP_BEGIN_SCENE, u32Payload(state.handle, 0));
            }

            let commandBytes = 0;
            for (const record of records) {
                record.size = (D8WG_COMMAND_HEADER_BYTES +
                    record.payload.byteLength +
                    (record.blob ? record.blob.byteLength : 0) + 7) & ~7;
                record.offset = D8WG_BATCH_HEADER_BYTES + commandBytes;
                commandBytes += record.size;
            }
            const result = new Uint8Array(D8WG_BATCH_HEADER_BYTES +
                commandBytes);
            const view = new DataView(result.buffer);
            view.setUint32(0, D8WG_MAGIC, true);
            view.setUint16(4, D8WG_VERSION_MAJOR, true);
            view.setUint16(6, D8WG_VERSION_MINOR, true);
            view.setUint32(16, records.length, true);
            view.setUint32(20, commandBytes, true);
            view.setUint32(24, session.low, true);
            view.setUint32(28, session.high, true);
            let sequence = 1;
            for (const record of records) {
                if (record.blob && record.dataOffsetField !== undefined) {
                    const blobBase = record.offset + D8WG_COMMAND_HEADER_BYTES +
                        record.payload.byteLength;
                    if (Array.isArray(record.dataOffsetField)) {
                        for (const { field, delta } of record.dataOffsetField)
                            putU32(record.payload, field, blobBase + delta);
                    } else {
                        putU32(record.payload, record.dataOffsetField, blobBase);
                    }
                }
                view.setUint16(record.offset, record.opcode, true);
                view.setUint32(record.offset + 4, record.size, true);
                view.setUint32(record.offset + 8, sequence++, true);
                result.set(record.payload,
                    record.offset + D8WG_COMMAND_HEADER_BYTES);
                if (record.blob) {
                    result.set(record.blob, record.offset +
                        D8WG_COMMAND_HEADER_BYTES + record.payload.byteLength);
                }
            }
            return result;
        }

        serializeState() {
            if (this.pendingSubmissions)
                throw new Error("D3D8 commands are still in flight; retry the save");
            if (this.frame)
                throw new Error("D3D8 has an unfinished frame; retry the save");

            const original = this.activeSession;
            const batches = [];
            for (const session of this.sessions.values()) {
                if (!session.devices.size && !session.resources.size) continue;
                batches.push(this.serializeSession(session));
            }
            if (original) this.setActiveSession(original, false);

            let byteLength = 16;
            for (const batch of batches)
                byteLength += 8 + ((batch.byteLength + 7) & ~7);
            const result = new Uint8Array(byteLength);
            const view = new DataView(result.buffer);
            view.setUint32(0, D8WG_CHECKPOINT_MAGIC, true);
            view.setUint16(4, D8WG_CHECKPOINT_VERSION, true);
            view.setUint32(8, batches.length, true);
            view.setUint32(12, byteLength, true);
            let offset = 16;
            for (const batch of batches) {
                view.setUint32(offset, batch.byteLength, true);
                result.set(batch, offset + 8);
                offset += 8 + ((batch.byteLength + 7) & ~7);
            }
            return result;
        }

        clearAllSessions() {
            this.discardFrame();
            for (const session of Array.from(this.sessions.values())) {
                this.setActiveSession(session, false);
                for (const handle of Array.from(session.devices.keys()))
                    this.destroyResource(handle);
                for (const handle of Array.from(session.resources.keys()))
                    this.destroyResource(handle);
            }
            this.sessions.clear();
            this.activeSession = null;
            this.devices = new Map();
            this.resources = new Map();
            this.retiredDeviceHandles = new Set();
            this.retiredResourceHandles = new Set();
        }

        restoreStateInitialized(checkpoint) {
            this.clearAllSessions();
            const bytes = checkpoint instanceof Uint8Array ? checkpoint :
                new Uint8Array(checkpoint || []);
            if (!bytes.byteLength) return;
            if (bytes.byteLength >= D8WG_BATCH_HEADER_BYTES &&
                    u32(bytes, 0) === D8WG_MAGIC) {
                this.executeBatch(bytes, { stateRestore: true });
                return;
            }
            if (bytes.byteLength < 16 ||
                    u32(bytes, 0) !== D8WG_CHECKPOINT_MAGIC ||
                    u16(bytes, 4) !== D8WG_CHECKPOINT_VERSION ||
                    u32(bytes, 12) !== bytes.byteLength) {
                throw new Error("invalid D3D8 multi-session checkpoint");
            }
            const count = u32(bytes, 8);
            let offset = 16;
            for (let index = 0; index < count; index++) {
                if (offset + 8 > bytes.byteLength)
                    throw new Error("truncated D3D8 checkpoint record");
                const size = u32(bytes, offset);
                if (size < D8WG_BATCH_HEADER_BYTES ||
                        size > bytes.byteLength - offset - 8) {
                    throw new Error("invalid D3D8 checkpoint record size");
                }
                this.executeBatch(bytes.subarray(offset + 8,
                    offset + 8 + size), { stateRestore: true, index });
                offset += 8 + ((size + 7) & ~7);
            }
            if (offset !== bytes.byteLength)
                throw new Error("D3D8 checkpoint has trailing data");
        }

        restoreState(checkpoint) {
            const owned = checkpoint instanceof Uint8Array ? checkpoint.slice() :
                new Uint8Array(checkpoint || []).slice();
            // Serialize restore against already accepted submissions. Callers may
            // invoke this on a brand-new executor, so acquire/configure WebGPU
            // before clearAllSessions() can mutate the live logical namespace.
            // Assign the guarded barrier to `work` immediately: any later submit
            // is forced behind the complete checkpoint replay.
            const operation = this.work.then(async () => {
                if (owned.byteLength) await this.initialize();
                this.restoreStateInitialized(owned);
                this.failed = null;
            });
            this.work = operation.catch(error => {
                this.failed = error;
            });
            return operation;
        }

        getStats() {
            let bindGroupsCached = 0;
            let devicesLive = 0;
            let resourcesLive = 0;
            let sessionsLive = 0;
            for (const session of this.sessions.values()) {
                devicesLive += session.devices.size;
                resourcesLive += session.resources.size;
                if (session.devices.size || session.resources.size)
                    sessionsLive++;
                for (const state of session.devices.values()) {
                    bindGroupsCached += state.bindGroups.size;
                }
            }
            return {
                ...this.stats,
                sessionsLive,
                sessionsTracked: this.sessions.size,
                devicesLive,
                resourcesLive,
                pipelinesCached: this.pipelineCache.size,
                samplersCached: this.samplerCache.size,
                bindGroupsCached,
            };
        }
    }

    global.D3D8WebGPUExecutor = D3D8WebGPUExecutor;
    global.installD3D8WebGPUExecutor = function(canvas, options) {
        return new D3D8WebGPUExecutor(canvas, options);
    };

    if (typeof module !== "undefined" && module.exports) {
        module.exports = {
            D3D8WebGPUExecutor,
            D8WG_MAGIC,
            D8WG_VERSION_MAJOR,
            D8WG_VERSION_MINOR,
            D8WG_BATCH_HEADER_BYTES,
            D8WG_COMMAND_HEADER_BYTES,
            // Stage 6: exposed for direct unit testing of the VS1.1/PS1.1-1.4
            // -> WGSL translator, independent of a real WebGPU device.
            shaderOpcodeInfo,
            parseVertexDeclaration,
            vertexShaderWgsl,
            pixelShaderWgsl,
            shaderPipelineWgsl,
        };
    }
})(typeof window !== "undefined" ? window : globalThis);
