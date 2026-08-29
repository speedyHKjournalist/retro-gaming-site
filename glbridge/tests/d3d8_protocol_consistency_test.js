"use strict";

// The D3D8 guest frontend speaks D9WG (see d3d8proxy/d3d8_protocol.h for why),
// so what needs guarding is no longer "does d3d8_protocol.h agree with
// d3d8_executor.js" -- that pairing no longer exists. It is the translation
// boundary itself:
//
//   1. every opcode the D3D8 guest emits is one the D3D9 executor decodes,
//   2. every D9WG payload struct it fills exists in d3d9_protocol.h,
//   3. the D3D8 -> D3D9 mapping tables agree with the executor's own D3D9
//      constants, and
//   4. the render states the guest deliberately drops really are ones the
//      executor has no code for.
//
// (1) and (4) are the ones worth having. A guest that emits an opcode the host
// ignores, or drops a state the host would have honoured, produces a wrong
// picture and no error anywhere -- which is precisely the failure mode a
// translation layer invites.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");

const guest = read("d3d8proxy/d3d8_proxy.c");
const bridge = read("d3d8proxy/d3d8_protocol.h");
const protocol = read("d3d9proxy/d3d9_protocol.h");
const executor = read("d3d9-webgpu/d3d9_executor.js");

// ---- the guest no longer owns a protocol of its own ----------------------

assert.match(bridge, /#include "\.\.\/d3d9proxy\/d3d9_protocol\.h"/,
    "the D3D8 bridge header must include the D9WG protocol it speaks");
assert.doesNotMatch(guest, /\bV86GL_CTRL_D3D8_BATCH\b/,
    "the D3D8 guest must submit on the D3D9 transport record");
assert.match(guest, /\bV86GL_CTRL_D3D9_BATCH\b/,
    "the D3D8 guest must submit on the D3D9 transport record");
assert.doesNotMatch(guest, /\bD8WG_OP_\w+/,
    "no D8WG opcode may survive in the D3D8 guest");

// ---- (1) every emitted opcode is decoded ---------------------------------

function protocolOpcodes() {
    const block = protocol.match(/enum D9WGOpcode \{([\s\S]*?)\n\};/);
    assert.ok(block, "d3d9_protocol.h must define enum D9WGOpcode");
    const values = new Map();
    for (const line of block[1].split("\n")) {
        const match = line.match(/^\s*(D9WG_OP_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)/);
        if (match) values.set(match[1], Number.parseInt(match[2], 0));
    }
    return values;
}

const opcodeValues = protocolOpcodes();

// Opcodes named in the executor's dispatch table, resolved through its own
// `const OP_* = <n>` declarations rather than by trusting the names to match.
function executorDecodedOpcodes() {
    const table = executor.match(/this\._handlers = \{([\s\S]*?)\n {12}\};/);
    assert.ok(table, "d3d9_executor.js must build a _handlers dispatch table");
    const constants = new Map();
    for (const match of executor.matchAll(
            /const\s+(OP_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)/g))
        constants.set(match[1], Number.parseInt(match[2], 0));
    const decoded = new Set();
    for (const match of table[1].matchAll(/\[(OP_\w+)\]/g)) {
        assert.ok(constants.has(match[1]),
            "dispatch table names undeclared " + match[1]);
        decoded.add(constants.get(match[1]));
    }
    return decoded;
}

const decoded = executorDecodedOpcodes();
const emitted = new Set();
for (const match of guest.matchAll(/\b(D9WG_OP_\w+)\b/g)) {
    assert.ok(opcodeValues.has(match[1]),
        "the D3D8 guest emits " + match[1] + ", which d3d9_protocol.h does " +
        "not define");
    emitted.add(match[1]);
}
assert.ok(emitted.size > 20,
    "expected the D3D8 guest to emit a substantial command set, saw " +
    emitted.size);
for (const name of [...emitted].sort()) {
    assert.ok(decoded.has(opcodeValues.get(name)),
        "the D3D8 guest emits " + name + " but d3d9_executor.js does not " +
        "decode it -- the command would be silently dropped");
}

// ---- (2) every payload struct it fills exists ----------------------------

const protocolStructs = new Set(
    [...protocol.matchAll(/typedef struct (D9WG\w+) \{/g)].map(m => m[1])
        .concat([...protocol.matchAll(/typedef D9WG\w+ (D9WG\w+);/g)]
            .map(m => m[1])));
for (const match of guest.matchAll(/\b(D9WG[A-Z]\w+)\b/g)) {
    assert.ok(protocolStructs.has(match[1]),
        "the D3D8 guest names " + match[1] + ", which is not a D9WG struct");
}

// ---- (3) the mapping tables agree with the executor's D3D9 constants -----

function bridgeNumber(name) {
    const match = bridge.match(new RegExp("#define\\s+" + name +
        "\\s+(0x[0-9A-Fa-f]+|\\d+)u?"));
    assert.ok(match, "d3d8_protocol.h must define " + name);
    return Number.parseInt(match[1], 0);
}

function executorNumber(name) {
    const match = executor.match(new RegExp("const\\s+" + name +
        "\\s*=\\s*(0x[0-9A-Fa-f]+|\\d+)"));
    assert.ok(match, "d3d9_executor.js must define " + name);
    return Number.parseInt(match[1], 0);
}

// D3D8 addressed samplers through SetTextureStageState; D3D9 split them out.
// If these drift, filtering and addressing silently land on the wrong state.
for (const [bridgeName, executorName] of [
    ["D3D9SAMP_ADDRESSU", "D3DSAMP_ADDRESSU"],
    ["D3D9SAMP_ADDRESSV", "D3DSAMP_ADDRESSV"],
    ["D3D9SAMP_ADDRESSW", "D3DSAMP_ADDRESSW"],
    ["D3D9SAMP_BORDERCOLOR", "D3DSAMP_BORDERCOLOR"],
    ["D3D9SAMP_MAGFILTER", "D3DSAMP_MAGFILTER"],
    ["D3D9SAMP_MINFILTER", "D3DSAMP_MINFILTER"],
    ["D3D9SAMP_MIPFILTER", "D3DSAMP_MIPFILTER"],
    ["D3D9SAMP_MAXANISOTROPY", "D3DSAMP_MAXANISOTROPY"],
]) {
    assert.equal(bridgeNumber(bridgeName), executorNumber(executorName),
        bridgeName + " must match the executor's " + executorName);
}

assert.equal(bridgeNumber("D3D9RS_DEPTHBIAS"),
    executorNumber("D3DRS_DEPTHBIAS"),
    "D3DRS_ZBIAS is translated to DEPTHBIAS; the target must match");

// The sampler split must be exhaustive in both directions: exactly the ten
// D3D8 stage states that became sampler states, and no others.
const samplerSplit = bridge.match(
    /d3d8_stage_state_to_sampler_state\(unsigned state\)\s*\{([\s\S]*?)\n\}/);
assert.ok(samplerSplit, "d3d8_protocol.h must define the sampler split");
const mappedStageStates = [...samplerSplit[1]
    .matchAll(/case (D3D8TSS_\w+): return (D3D9SAMP_\w+);/g)];
assert.equal(mappedStageStates.length, 10,
    "D3D8 moved exactly ten texture stage states into D3D9 sampler state");
assert.equal(new Set(mappedStageStates.map(m => m[2])).size, 10,
    "each D3D8 stage state must map to a distinct sampler state");

// ---- (4) dropped render states really are unimplemented ------------------

// The executor lists the render states it tracks; a state the guest drops must
// not appear there, or dropping it would be discarding something honoured.
const droppedStates = [...bridge.matchAll(/#define (D3D8RS_\w+) (\d+)u/g)]
    .map(m => ({ name: m[1], value: Number.parseInt(m[2], 10) }));
assert.ok(droppedStates.length >= 8,
    "expected the removed-render-state table to be present");

const dropSwitch = guest.match(
    /static BOOL emit_render_state\(D8Device \*device[\s\S]*?\n\}/);
assert.ok(dropSwitch, "the D3D8 guest must translate render states");
const droppedNames = new Set([...dropSwitch[0]
    .matchAll(/case (D3D8RS_\w+):\s*(?:\/\*[^\n]*\*\/\s*)?\n/g)].map(m => m[1]));
// ZBIAS is translated, not dropped, so it must not be in the fall-through set.
assert.ok(!droppedNames.has("D3D8RS_ZBIAS"),
    "D3DRS_ZBIAS is translated to DEPTHBIAS, not dropped");
assert.ok(droppedNames.size >= 7,
    "expected the deleted-in-D3D9 render states to be dropped explicitly");

const executorRenderStates = new Set(
    [...executor.matchAll(/const\s+(D3DRS_\w+)\s*=\s*(\d+)/g)]
        .map(m => Number.parseInt(m[2], 10)));
for (const name of droppedNames) {
    const entry = droppedStates.find(item => item.name === name);
    assert.ok(entry, name + " is dropped but not declared in d3d8_protocol.h");
    assert.ok(!executorRenderStates.has(entry.value),
        name + " (" + entry.value + ") is dropped by the D3D8 guest but the " +
        "executor tracks that render state number -- dropping it discards " +
        "behaviour the host implements");
}

// ---- every D3D8 format is accounted for, on both sides -----------------
//
// The guest decides what CheckDeviceFormat promises; the host decides what an
// upload actually becomes. A format present on one side and absent on the
// other is invisible until a game picks it: 3DMark 2001 probed
// D3DFMT_W11V11U10 twenty times per frame and quietly took its fallback path,
// and the same silence would hide any other hole. So this list is the
// complete D3DFORMAT enumeration from d3d8types.h, and every entry has to be
// somewhere.

const D3D8_TEXTURE_FORMATS = [
    "R8G8B8", "A8R8G8B8", "X8R8G8B8", "R5G6B5", "X1R5G5B5", "A1R5G5B5",
    "A4R4G4B4", "R3G3B2", "A8", "A8R3G3B2", "X4R4G4B4", "A2B10G10R10",
    "G16R16", "A8P8", "P8", "L8", "A8L8", "A4L4", "V8U8", "L6V5U5",
    "X8L8V8U8", "Q8W8V8U8", "V16U16", "W11V11U10", "A2W10V10U10",
    "UYVY", "YUY2", "DXT1", "DXT2", "DXT3", "DXT4", "DXT5",
];
// Buffer formats old capability scanners nonetheless probe as textures. The
// guest backs them deliberately; see texture_format_layout.
const D3D8_PROBED_AS_TEXTURES = ["INDEX16", "INDEX32"];
const D3D8_DEPTH_FORMATS = ["D16", "D24S8", "D32", "D15S1", "D24X8",
    "D24X4S4"];
// D3DFMT_UNKNOWN and D3DFMT_VERTEXDATA name no resource layout, and
// D3DFMT_D16_LOCKABLE is refused on purpose -- WebGPU cannot copy a
// depth24plus texture back to a buffer, so the one thing that format promises
// over D3DFMT_D16 is the one thing this backend cannot do.
const D3D8_DELIBERATELY_ABSENT = ["D16_LOCKABLE"];

const cFunction = (source, name) => {
    const match = source.match(
        new RegExp("\\n\\w[^\\n]*\\b" + name + "\\([\\s\\S]*?\\n\\}"));
    assert.ok(match, name + " is missing from the D3D8 guest");
    return match[0];
};

// D3D8's SetVertexShader(FVF) is both a declaration change and a switch back
// to fixed-function vertex processing. D9WG carries those as two D3D9-shaped
// states; omitting the explicit shader-null bind leaves the previous vs_1_x
// program alive and makes its reflected inputs disagree with the new FVF.
const setVertexShader = cFunction(guest, "device_set_vertex_shader");
assert.match(setVertexShader,
    /emit_set_shader\(D9WG_OP_SET_VERTEX_SHADER,\s*device,\s*0\)[\s\S]*emit_set_fvf/,
    "SetVertexShader(FVF) must unbind the old programmable vertex shader");

// The executor trusts the 64-bit hash carried by CREATE_*_SHADER as its cache
// key. A zero-filled D3D8 command aliases every distinct program to the first
// translated shader, producing both wildly transformed geometry and black
// objects once 3DMark switches programs between passes.
const shaderHash = cFunction(guest, "shader_bytecode_hash");
assert.match(shaderHash, /0x84222325u/,
    "the D3D8 guest must compute the protocol's FNV-1a shader hash");
for (const name of ["emit_create_vertex_shader", "emit_create_pixel_shader"]) {
    const emitShader = cFunction(guest, name);
    assert.match(emitShader, /command\.bytecode_hash_low\s*=\s*shader->hash_low/,
        name + " leaves the low shader-cache key zero");
    assert.match(emitShader, /command\.bytecode_hash_high\s*=\s*shader->hash_high/,
        name + " leaves the high shader-cache key zero");
}
for (const name of ["device_create_vertex_shader", "device_create_pixel_shader"])
    assert.match(cFunction(guest, name), /shader_bytecode_hash\(/,
        name + " never hashes the bytecode it uploads");

const textureLayout = cFunction(guest, "texture_format_layout");
for (const name of [...D3D8_TEXTURE_FORMATS, ...D3D8_PROBED_AS_TEXTURES]) {
    assert.ok(textureLayout.includes("D3DFMT_" + name + ":"),
        "D3DFMT_" + name + " has no block layout in the D3D8 guest, so " +
        "CheckDeviceFormat refuses it and CreateTexture cannot make one");
}

const gpuFormats = executor.match(
    /function formatToGPU\(format\)[\s\S]*?\n    \}/);
assert.ok(gpuFormats, "the D3D9 executor must map D3D formats to WebGPU ones");
for (const name of [...D3D8_TEXTURE_FORMATS, ...D3D8_PROBED_AS_TEXTURES]) {
    assert.ok(gpuFormats[0].includes("D3DFMT_" + name + ":"),
        "the D3D8 guest forwards D3DFMT_" + name + " but the host has no " +
        "WebGPU format for it, so every upload of one is discarded");
}

const depthFormats = cFunction(guest, "supported_depth_stencil_format");
for (const name of D3D8_DEPTH_FORMATS) {
    assert.ok(depthFormats.includes("D3DFMT_" + name),
        "D3DFMT_" + name + " is a D3D8 depth format the guest refuses; the " +
        "host satisfies every one of them with the same depth target");
}
for (const name of D3D8_DELIBERATELY_ABSENT) {
    assert.ok(!depthFormats.includes("D3DFMT_" + name),
        "D3DFMT_" + name + " is advertised but nothing implements it");
}

// A render target must be a format the host can both render to and pack back
// for a lockable target, or GetRenderTargetData/CopyRects returns garbage.
const renderTargets = cFunction(guest, "supported_render_target_format");
const readbackPack = executor.match(
    /function packGPUReadbackRow\([\s\S]*?\n    \}/);
assert.ok(readbackPack, "the D3D9 executor must pack readback rows");
for (const name of [...renderTargets.matchAll(/D3DFMT_(\w+)/g)]
        .map(match => match[1])) {
    assert.ok(readbackPack[0].includes("D3DFMT_" + name + ":"),
        "D3DFMT_" + name + " is offered as a render target but the host " +
        "cannot pack it back, so locking one throws instead of reading");
}

// D3D8 SetRenderTarget accepts a depth surface at least as large as the colour
// target and resets the viewport to the target's full dimensions. 3DMark 2001
// relies on both while entering its smaller advanced-pixel-shader target.
const setRenderTarget = cFunction(guest, "device_set_render_target");
assert.match(setRenderTarget,
    /depth_width\s*<\s*render_target_width/,
    "a larger depth surface must remain valid for a smaller render target");
assert.match(setRenderTarget,
    /depth_height\s*<\s*render_target_height/,
    "only a depth surface smaller than the render target is invalid");
assert.doesNotMatch(setRenderTarget,
    /depth_(?:width|height)\s*!=/,
    "SetRenderTarget must not require exact colour/depth dimensions");
// Both dimensions have to come from the surface's own resource: a
// texture-backed depth surface leaves `desc` zeroed, so reading it there made
// every such bind look smaller than the target and get refused.
assert.match(setRenderTarget,
    /depth_width\s*=[\s\S]*?depth->texture->levels\[depth->level\]\.width/,
    "the depth surface's size must come from its texture level when it has one");
assert.match(setRenderTarget,
    /viewport\.Width\s*=\s*render_target_width[\s\S]*?device_set_viewport/,
    "SetRenderTarget must reset and emit the viewport for the new target");

// A depth surface the app created has a host texture of its own and must be
// bound by that handle. Aliasing every depth surface onto the device's auto
// depth-stencil left an offscreen pass depth-testing against a buffer of the
// wrong size, which the host answers by dropping depth testing for the pass --
// nothing occludes anything.
assert.match(setRenderTarget, /depth->texture->handle/,
    "a depth surface backed by a texture must be bound by its own handle");
assert.match(cFunction(guest, "device_create_depth_surface"),
    /create_depth_stencil_texture\(/,
    "CreateDepthStencilSurface must allocate a host depth resource");

// ...and that reset has to survive its own validator. A viewport is clipped
// against the bound colour target, which an app may make larger than the
// display mode: 3DMark 2001's advanced pixel-shader test binds an offscreen
// target bigger than its 800x600 backbuffer, and validating the viewport
// against display_mode refused the very viewport SetRenderTarget installs --
// reaching the app as "Could not set render target - D3DERR_INVALIDCALL".
const setViewport = cFunction(guest, "device_set_viewport");
assert.doesNotMatch(setViewport, /display_mode/,
    "SetViewport must bound the viewport by the current render target, not " +
    "by the display mode");
assert.match(setViewport, /current_render_target_size\(/,
    "SetViewport must ask for the bound render target's size");

// ---- the guest validator may not be narrower than the translator --------
//
// CreateVertexShader/CreatePixelShader walk the token stream themselves, so
// the guest keeps its own opcode table. Every entry in it has to agree with
// the host's on two things: that the instruction is translated at all, and
// how many operand words follow it. The first kind of disagreement refuses a
// shader the backend could have run -- 3DMark 2001's every vertex shader is
// `m4x4 oPos, v0, c0`, and the guest refused `m4x4` while the host expanded
// it into dot products. The second is worse: a wrong operand count desyncs
// the walk over every instruction after it.

const pipeline = read("d3d9-webgpu/d3d9_shader_pipeline.js");

const hostOpcodeValues = new Map();   // NAME (upper) -> numeric opcode
for (const match of pipeline.matchAll(/(\w+):\s*(\d+)[,\s]/g)) {
    const name = match[1].toUpperCase();
    if (!hostOpcodeValues.has(name)) hostOpcodeValues.set(name, Number(match[2]));
}
const hostOperands = new Map();       // NAME (upper) -> dest + source count
for (const match of pipeline.matchAll(
        /\[OP\.(\w+)\]:\s*\[(\d+),\s*(\d+)\]/g)) {
    hostOperands.set(match[1].toUpperCase(),
        Number(match[2]) + Number(match[3]));
}
assert.ok(hostOperands.size > 40, "failed to parse the host operand table");
const hostRefused = new Set(
    [...(pipeline.match(/const UNSUPPORTED_OPS = new Set\(\[[\s\S]*?\]\)/) ||
        [""])[0].matchAll(/OP\.(\w+)/g)].map(match => match[1].toUpperCase()));
assert.ok(hostRefused.size >= 3, "failed to parse the host's refused opcodes");

// Instructions whose operand count is model-dependent on both sides; the
// guest spells that as a conditional rather than a literal, so compare only
// membership for these.
const MODEL_DEPENDENT = new Set(["TEX", "TEXCOORD"]);
// Shapes the host parser reads inline rather than through OPERANDS: the
// literal-carrying declarations, and the two whose shape operandShape()
// computes from the shader model.
const HOST_INLINE_SHAPES = new Set(["DEF", "DEFI", "DEFB", "DCL", "TEX",
    "SINCOS"]);
for (const name of HOST_INLINE_SHAPES) {
    assert.ok(new RegExp("OP\\." + name + "\\b").test(pipeline),
        name + " is listed as an inline host shape but the translator does " +
        "not mention it; this list has gone stale");
}

const guestOpcodes = new Map();
let pendingCases = [];
for (const line of cFunction(guest, "shader_opcode_supported").split("\n")) {
    const opcode = line.match(/case (D3DSIO_\w+):/);
    if (opcode) { pendingCases.push(opcode[1].slice("D3DSIO_".length)); continue; }
    const words = line.match(/\*operand_words = ([^;]+);/);
    if (!words) continue;
    const literal = words[1].trim().match(/^(\d+)u?$/);
    for (const name of pendingCases)
        guestOpcodes.set(name.toUpperCase(), literal ? Number(literal[1]) : null);
    pendingCases = [];
}
assert.ok(guestOpcodes.size > 25,
    "failed to parse the D3D8 guest opcode table");

for (const [name, words] of guestOpcodes) {
    assert.ok(hostOperands.has(name) || HOST_INLINE_SHAPES.has(name),
        "the D3D8 guest accepts " + name + " but the host translator has no " +
        "operand shape for it, so the shader decodes into nonsense");
    assert.ok(!hostRefused.has(name),
        "the D3D8 guest accepts " + name + " but the host refuses it; the " +
        "shader would be created and then silently skipped at draw time");
    if (words !== null && !MODEL_DEPENDENT.has(name) &&
            hostOperands.has(name))
        assert.equal(words, hostOperands.get(name),
            name + " has " + words + " operand words in the D3D8 guest and " +
            hostOperands.get(name) + " in the host translator");
}

// The other direction, for the D3D8 shader models only: an instruction both
// sides could handle must not be missing from the guest.
const D3D8_SM1_OPCODES = [
    "NOP", "MOV", "ADD", "SUB", "MAD", "MUL", "RCP", "RSQ", "DP3", "DP4",
    "MIN", "MAX", "SLT", "SGE", "EXP", "LOG", "LIT", "DST", "LRP", "FRC",
    "M4x4", "M4x3", "M3x4", "M3x3", "M3x2", "TEXCOORD", "TEXKILL", "TEX",
    "TEXBEM", "TEXBEML", "TEXREG2AR", "TEXREG2GB", "TEXM3x2PAD", "TEXM3x2TEX",
    "TEXM3x3PAD", "TEXM3x3TEX", "TEXM3x3SPEC", "TEXM3x3VSPEC", "EXPP", "LOGP",
    "CND", "DEF", "TEXREG2RGB", "TEXDP3TEX", "TEXDP3", "CMP", "PHASE",
].map(name => name.toUpperCase());
for (const name of D3D8_SM1_OPCODES) {
    if (hostRefused.has(name)) continue;
    assert.ok(guestOpcodes.has(name),
        "the host translates " + name + " but the D3D8 guest validator " +
        "refuses it, so no shader containing one can ever be created");
}

// ---- what a v# register means, on both sides ---------------------------
//
// vs_1_x carries no dcl_, so a v# register's meaning comes from D3D's fixed
// table. The guest applies that table to the *declaration* it converts
// (d3d8_vsd_register_usage); the host applies it to the *shader* it
// translates. The executor then pairs the two by (usage, usageIndex), so if
// the tables disagree by one entry, that attribute is silently never bound --
// and if they disagree everywhere, the draw is dropped with no attribute at
// all, which is what a black screen from every vertex-shader test looked
// like. Compare them by running both rather than by reading either.

const shaderPipeline = require("../d3d9-webgpu/d3d9_shader_pipeline.js");

const guestUsageValues = new Map();
for (const match of bridge.matchAll(/#define (D3D9DECLUSAGE_\w+) (\d+)u/g))
    guestUsageValues.set(match[1], Number(match[2]));
const guestRegisterValues = new Map();
for (const match of bridge.matchAll(/#define (D3D8_VSD_REG_\w+) (\d+)u/g))
    guestRegisterValues.set(match[1], Number(match[2]));
assert.ok(guestUsageValues.size >= 14 && guestRegisterValues.size >= 8,
    "failed to parse the D3D8 declaration constants");

// The guest's switch, as {register -> [usage, usageIndex]}, plus its two
// range rules for the texture-coordinate registers.
const guestRegisterUsage = register => {
    const body = cFunction(bridge, "d3d8_vsd_register_usage");
    let pendingRegister = null;
    for (const line of body.split("\n")) {
        const label = line.match(/case (D3D8_VSD_REG_\w+):/);
        if (label) { pendingRegister = guestRegisterValues.get(label[1]); continue; }
        const assignment = line.match(
            /\*usage = (D3D9DECLUSAGE_\w+); \*usage_index = (\d+)u;/);
        if (!assignment || pendingRegister === null) continue;
        if (pendingRegister === register)
            return [guestUsageValues.get(assignment[1]), Number(assignment[2])];
        pendingRegister = null;
    }
    const texcoord0 = guestRegisterValues.get("D3D8_VSD_REG_TEXCOORD0");
    const texcoord7 = guestRegisterValues.get("D3D8_VSD_REG_TEXCOORD7");
    if (register >= texcoord0 && register <= texcoord7)
        return [guestUsageValues.get("D3D9DECLUSAGE_TEXCOORD"),
            register - texcoord0];
    return [guestUsageValues.get("D3D9DECLUSAGE_TEXCOORD"),
        8 + (register & 7)];
};

// The host's, read out of a translated vs_1_1 that reads exactly one v#.
const hostRegisterUsage = register => {
    const instruction = opcode => (opcode & 0xffff) >>> 0;
    const regTypeBits = type => (((type & 7) << 28) | ((type & 0x18) << 8)) >>> 0;
    const REG = shaderPipeline.REGISTER;
    const tokens = new Uint32Array([
        0xfffe0101,
        instruction(shaderPipeline.OP.MOV),
        (0x80000000 | regTypeBits(REG.RASTOUT) | 0xf0000) >>> 0,
        (0x80000000 | register | regTypeBits(REG.INPUT) | (0xe4 << 16)) >>> 0,
        0x0000ffff,
    ]);
    const result = shaderPipeline.compileShader(tokens);
    assert.ok(result.ok, "vs_1_1 reading v" + register + " failed to translate");
    assert.equal(result.reflection.inputs.length, 1,
        "a vs_1_1 reading v" + register + " must reflect exactly that input; " +
        "reflecting none means no vertex attribute is ever bound to it");
    const input = result.reflection.inputs[0];
    return [input.usage, input.usageIndex];
};

for (let register = 0; register <= 15; ++register) {
    assert.deepEqual(hostRegisterUsage(register), guestRegisterUsage(register),
        "v" + register + " means different things to the D3D8 guest and the " +
        "shader translator, so a declaration binding it pairs with nothing");
}

console.log("d3d8_protocol_consistency_test: ok (" + emitted.size +
    " opcodes emitted, all decoded; " +
    (D3D8_TEXTURE_FORMATS.length + D3D8_PROBED_AS_TEXTURES.length +
        D3D8_DEPTH_FORMATS.length) + " formats and " + guestOpcodes.size +
    " shader opcodes present on both sides; " +
    "16 vs_1_x input registers agree)");
