#!/usr/bin/env node
// Ties the DirectDraw group's three declarations together: the wire structs in
// d3d9proxy/d3d9_protocol.h, the guest DLL that fills them, and the host module
// that decodes them.
//
// Every field here is read by byte offset on one side and written by struct
// member on the other, so an inserted field or a reordered pair is a silent
// misdecode: the host reads a rectangle as a handle and skips the blit, or
// worse, reads a handle as a rectangle and blits somewhere plausible. Nothing
// about that looks like a protocol error at runtime, which is why it is
// checked here instead.

"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const protocolHeader = fs.readFileSync(
    path.resolve(__dirname, "../d3d9proxy/d3d9_protocol.h"), "utf8");
const translationHeader = fs.readFileSync(
    path.resolve(__dirname, "../ddrawproxy/ddraw_protocol.h"), "utf8");
const guestSource = fs.readFileSync(
    path.resolve(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8");
const exportsFile = fs.readFileSync(
    path.resolve(__dirname, "../ddrawproxy/ddraw.def"), "utf8");
const hostSource = fs.readFileSync(
    require("./host_paths.js").hostPath("d3d9-webgpu/ddraw_ops.js"), "utf8");
const executorSource = fs.readFileSync(
    require("./host_paths.js").hostPath("d3d9-webgpu/d3d9_executor.js"), "utf8");
const ddraw = require(require("./host_paths.js").hostPath("d3d9-webgpu/ddraw_ops.js"));

const failures = [];
let passed = 0;

function check(name, body) {
    try { body(); ++passed; }
    catch (error) { failures.push({ name, error }); }
}

// ---- struct layouts, computed from the header ----

// Every field in the DirectDraw group is a 4-byte scalar or an array of them,
// which is what keeps the layout expressible this way.
function structLayout(name) {
    const match = protocolHeader.match(
        new RegExp("typedef struct " + name + " \\{([\\s\\S]*?)\\} " + name + ";"));
    assert.ok(match, "struct " + name + " is not in d3d9_protocol.h");
    const layout = new Map();
    const components = new Set();
    let offset = 0;
    for (const raw of match[1].split("\n")) {
        const line = raw.replace(/\/\*[\s\S]*?\*\//g, "").trim();
        const field = line.match(
            /^(uint32_t|int32_t|float)\s+([A-Za-z0-9_]+)(\[(\d+)\])?;$/);
        if (!field) continue;
        const count = field[4] ? Number(field[4]) : 1;
        layout.set(field[2], offset);
        // An array field is read one component at a time, so every component
        // is a legitimate offset to see in the decoder.
        for (let index = 0; index < count; ++index)
            components.add(offset + index * 4);
        offset += 4 * count;
    }
    layout.set("__size", offset);
    layout.components = components;
    return layout;
}

const BLT = structLayout("D9WGDDBlt");
const COLOR_KEY = structLayout("D9WGDDSetColorKey");
const SURFACE_PALETTE = structLayout("D9WGDDSetSurfacePalette");
const DISPLAY_MODE = structLayout("D9WGDDSetDisplayMode");
const OVERLAY = structLayout("D9WGDDUpdateOverlay");
const READBACK = structLayout("D9WGReadbackSurface");
const SESSION_END = structLayout("D9WGSessionEnd");

check("the wire structs are the sizes their compile-time assertions claim",
        () => {
    const assertions = {
        D9WGDDBlt: 80, D9WGDDSetColorKey: 24,
        D9WGDDSetSurfacePalette: 16, D9WGDDSetDisplayMode: 32,
        D9WGDDUpdateOverlay: 52, D9WGReadbackSurface: 52,
        D9WGSessionEnd: 8,
    };
    for (const [name, size] of Object.entries(assertions)) {
        assert.match(protocolHeader,
            new RegExp("sizeof\\(" + name + "\\) == " + size),
            name + " has no compile-time size assertion for " + size);
    }
    assert.equal(BLT.get("__size"), 80);
    assert.equal(COLOR_KEY.get("__size"), 24);
    assert.equal(SURFACE_PALETTE.get("__size"), 16);
    assert.equal(DISPLAY_MODE.get("__size"), 32);
    assert.equal(OVERLAY.get("__size"), 52);
    assert.equal(READBACK.get("__size"), 52);
    assert.equal(SESSION_END.get("__size"), 8);
});

// ---- the host decodes at the offsets the header defines ----

function handlerSource(name) {
    const start = hostSource.indexOf("        " + name + "(bytes, view, offset");
    assert.ok(start >= 0, "ddraw_ops.js has no handler named " + name);
    const end = hostSource.indexOf("\n        },", start);
    assert.ok(end > start, name + " has no recognisable end");
    return hostSource.slice(start, end);
}

// Reads written as `view.getUint32(offset + 32, true)`, plus the bare
// `view.getUint32(offset, true)` that is field zero.
function offsetsRead(source) {
    const offsets = new Set();
    for (const match of source.matchAll(/view\.get(?:Uint32|Int32|Float32)\(offset(?:\s*\+\s*(\d+))?\s*,/g))
        offsets.add(match[1] ? Number(match[1]) : 0);
    return offsets;
}

function assertReadsFields(handler, layout, fields) {
    const read = offsetsRead(handlerSource(handler));
    for (const field of fields) {
        const offset = layout.get(field);
        assert.notEqual(offset, undefined,
            field + " is not a field of the struct " + handler + " decodes");
        assert.ok(read.has(offset),
            handler + " never reads " + field + " (offset " + offset + ")");
    }
    for (const offset of read) {
        assert.ok(layout.components.has(offset),
            handler + " reads offset " + offset +
            ", which is not the start of any field or array component");
        assert.ok(offset < layout.get("__size"),
            handler + " reads past the end of its payload at " + offset);
    }
}

check("onDDBlt decodes every field of D9WGDDBlt at its own offset", () => {
    assertReadsFields("onDDBlt", BLT, ["device_handle", "source_handle",
        "source_level", "source_face", "source_rect", "destination_handle",
        "destination_level", "destination_face", "flags",
        "destination_rect", "fill_color"]);
    // The rectangles are four consecutive int32s read one by one, so their
    // interior offsets have to be accepted as well as their starts.
    const read = offsetsRead(handlerSource("onDDBlt"));
    for (const base of [BLT.get("source_rect"), BLT.get("destination_rect")])
        for (let index = 0; index < 4; ++index)
            assert.ok(read.has(base + index * 4),
                "rectangle component at " + (base + index * 4) + " is not read");
});

check("onDDSetColorKey decodes the key and its presence flag", () => {
    assertReadsFields("onDDSetColorKey", COLOR_KEY,
        ["surface_handle", "key_kind", "color_low", "color_high", "present"]);
});

check("onDDSetSurfacePalette decodes the surface and the slot", () => {
    assertReadsFields("onDDSetSurfacePalette", SURFACE_PALETTE,
        ["surface_handle", "palette_index"]);
});

check("onDDSetDisplayMode decodes the mode and how it was really applied",
        () => {
    assertReadsFields("onDDSetDisplayMode", DISPLAY_MODE,
        ["device_handle", "width", "height", "bits_per_pixel", "refresh_rate",
         "cooperative_flags", "guest_mode_changed"]);
});

check("onDDUpdateOverlay decodes identity, rectangles, state and destination",
        () => {
    assertReadsFields("onDDUpdateOverlay", OVERLAY,
        ["surface_handle", "overlay_id", "source_rect", "destination_rect",
         "flags", "z_order", "destination_handle"]);
    const read = offsetsRead(handlerSource("onDDUpdateOverlay"));
    for (const base of [OVERLAY.get("source_rect"),
            OVERLAY.get("destination_rect")])
        for (let index = 0; index < 4; ++index)
            assert.ok(read.has(base + index * 4),
                "overlay rectangle component at " +
                (base + index * 4) + " is not read");
});

check("GetDC preserves the channel masks of true-colour DirectDraw surfaces",
        () => {
    const start = guestSource.indexOf(
        "static HRESULT WINAPI surface_GetDC(IDirectDrawSurface7 *iface");
    const end = guestSource.indexOf(
        "static HRESULT WINAPI surface_ReleaseDC", start);
    assert.ok(start >= 0 && end > start, "surface_GetDC was not found");
    const getDC = guestSource.slice(start, end);
    assert.match(getDC, /biCompression\s*=\s*BI_BITFIELDS/,
        "16/32-bit GDI surfaces must not use the implicit RGB555 layout");
    for (const [index, channel] of [
        [0, "dwRBitMask"], [1, "dwGBitMask"], [2, "dwBBitMask"],
    ]) {
        assert.match(getDC,
            new RegExp("info\\.table\\.masks\\[" + index +
                "\\]\\s*=\\s*" +
                "surface->desc\\.ddpfPixelFormat\\." + channel),
            "the DIB does not inherit " + channel + " from the surface");
    }
});

// ---- opcode and flag values agree across all three files ----

function headerConstant(text, name) {
    const match = text.match(new RegExp("#define\\s+" + name + "\\s+([^\\n]+)"));
    assert.ok(match, name + " is not defined");
    return match[1].trim();
}

function headerEnum(name) {
    const match = protocolHeader.match(
        new RegExp("\\b" + name + "\\s*=\\s*(0x[0-9a-fA-F]+|\\d+)"));
    assert.ok(match, name + " is not in the opcode enum");
    return Number(match[1]);
}

check("the opcode numbers match between the header and the host module", () => {
    assert.equal(ddraw.OP_DD_BLT, headerEnum("D9WG_OP_DD_BLT"));
    assert.equal(ddraw.OP_DD_SET_COLOR_KEY,
        headerEnum("D9WG_OP_DD_SET_COLOR_KEY"));
    assert.equal(ddraw.OP_DD_SET_SURFACE_PALETTE,
        headerEnum("D9WG_OP_DD_SET_SURFACE_PALETTE"));
    assert.equal(ddraw.OP_DD_SET_DISPLAY_MODE,
        headerEnum("D9WG_OP_DD_SET_DISPLAY_MODE"));
    assert.equal(ddraw.OP_DD_UPDATE_OVERLAY,
        headerEnum("D9WG_OP_DD_UPDATE_OVERLAY"));
    // They must also sit in their own block, clear of the D3D9 groups.
    for (const opcode of [ddraw.OP_DD_BLT, ddraw.OP_DD_SET_COLOR_KEY,
            ddraw.OP_DD_SET_SURFACE_PALETTE, ddraw.OP_DD_SET_DISPLAY_MODE,
            ddraw.OP_DD_UPDATE_OVERLAY])
        assert.ok(opcode >= 0x500 && opcode < 0x600,
            "the DirectDraw group lives at 0x500, not " + opcode.toString(16));
});

check("the blit flag bits match between the header and the host module", () => {
    const bits = {
        D9WG_DDBLT_KEY_SOURCE: ddraw.DDBLT_KEY_SOURCE,
        D9WG_DDBLT_KEY_DESTINATION: ddraw.DDBLT_KEY_DESTINATION,
        D9WG_DDBLT_MIRROR_X: ddraw.DDBLT_MIRROR_X,
        D9WG_DDBLT_MIRROR_Y: ddraw.DDBLT_MIRROR_Y,
        D9WG_DDBLT_COLOR_FILL: ddraw.DDBLT_COLOR_FILL,
        D9WG_DDBLT_DEPTH_FILL: ddraw.DDBLT_DEPTH_FILL,
        D9WG_DDBLT_FILTER_LINEAR: ddraw.DDBLT_FILTER_LINEAR,
    };
    for (const [name, value] of Object.entries(bits)) {
        const definition = headerConstant(protocolHeader, name);
        const shift = Number(definition.match(/1u\s*<<\s*(\d+)/)[1]);
        assert.equal(value, 1 << shift, name + " disagrees with the header");
    }
});

check("the overlay flag bits match between the header and host module", () => {
    const bits = {
        D9WG_DDOVER_SHOW: ddraw.DDOVER_SHOW,
        D9WG_DDOVER_HIDE: ddraw.DDOVER_HIDE,
        D9WG_DDOVER_KEY_SOURCE: ddraw.DDOVER_KEY_SOURCE,
        D9WG_DDOVER_KEY_DESTINATION: ddraw.DDOVER_KEY_DESTINATION,
        D9WG_DDOVER_MIRROR_X: ddraw.DDOVER_MIRROR_X,
        D9WG_DDOVER_MIRROR_Y: ddraw.DDOVER_MIRROR_Y,
        D9WG_DDOVER_KEY_SOURCE_OVERRIDE:
            ddraw.DDOVER_KEY_SOURCE_OVERRIDE,
        D9WG_DDOVER_KEY_DESTINATION_OVERRIDE:
            ddraw.DDOVER_KEY_DESTINATION_OVERRIDE,
    };
    for (const [name, value] of Object.entries(bits)) {
        const definition = headerConstant(protocolHeader, name);
        const shift = Number(definition.match(/1u\s*<<\s*(\d+)/)[1]);
        assert.equal(value, 1 << shift, name + " disagrees with the header");
    }
});

check("the indexed-storage usage bit is the same number everywhere", () => {
    assert.match(headerConstant(protocolHeader, "D9WG_USAGE_DDRAW_INDEXED"),
        /0x80000000u/);
    assert.match(executorSource,
        /const D9WG_USAGE_DDRAW_INDEXED = 0x80000000;/,
        "the executor's copy of the usage bit has drifted");
    assert.ok(guestSource.includes("D9WG_USAGE_DDRAW_INDEXED"),
        "the guest never marks a palettised surface as indexed");
});

check("the protocol minor version is 7 in the header, guest and executor",
        () => {
    assert.match(headerConstant(protocolHeader, "D9WG_VERSION_MINOR"), /^7u$/);
    assert.match(executorSource, /const D9WG_VERSION_MINOR = 7;/);
    // The guest gets its version from the header it includes, so the check
    // there is that it includes it rather than restating it.
    assert.ok(guestSource.includes("d3d9_protocol.h") ||
        translationHeader.includes("d3d9_protocol.h"),
        "the guest must take the version from the protocol header");
});

// ---- the DLL exports every entry point it defines ----

check("every exported name has a definition, and every definition is exported",
        () => {
    const exported = new Set();
    for (const line of exportsFile.split("\n")) {
        const match = line.trim().match(/^([A-Za-z0-9_]+)=([A-Za-z0-9_]+)@(\d+)$/);
        if (match) exported.add(match[1]);
    }
    assert.ok(exported.size >= 20,
        "the export list is suspiciously short: " + exported.size);
    for (const name of exported)
        assert.ok(new RegExp("\\bWINAPI\\s+" + name + "\\s*\\(").test(guestSource),
            name + " is exported but not defined in ddraw_proxy.c");

    // The retail DLL's undocumented exports are the ones a title imports
    // statically; missing one means the game fails to load rather than
    // failing to render, so their presence is pinned here.
    for (const required of ["DirectDrawCreate", "DirectDrawCreateEx",
            "DirectDrawCreateClipper", "DirectDrawEnumerateA",
            "DirectDrawEnumerateExA", "DllGetClassObject", "DllCanUnloadNow",
            "AcquireDDThreadLock", "ReleaseDDThreadLock",
            "D3DParseUnknownCommand", "SetAppCompatData", "GetOLEThunkData"])
        assert.ok(exported.has(required),
            required + " is missing from ddraw.def; a title that imports it " +
            "would fail to load the DLL at all");
});

check("the guest emits the DirectDraw group by name, not by number", () => {
    for (const opcode of ["D9WG_OP_DD_BLT", "D9WG_OP_DD_SET_COLOR_KEY",
            "D9WG_OP_DD_SET_SURFACE_PALETTE", "D9WG_OP_DD_SET_DISPLAY_MODE",
            "D9WG_OP_DD_UPDATE_OVERLAY"])
        assert.ok(guestSource.includes(opcode),
            "the guest never emits " + opcode);
    assert.ok(!/emit_command\(\s*0x5[0-9a-fA-F][0-9a-fA-F]/.test(guestSource),
        "an opcode is spelled as a literal, which the header cannot keep in " +
        "step");
});

check("DirectDraw teardown reaches both normal and process-wide host paths",
        () => {
    assert.equal(headerEnum("D9WG_OP_SESSION_END"), 13);
    assert.match(executorSource, /const OP_SESSION_END = 13;/);
    assert.match(executorSource,
        /\[OP_SESSION_END\]: this\.onSessionEnd/,
        "the executor must dispatch process teardown");
    assert.match(guestSource,
        /destroy\.resource_kind\s*=\s*0;[\s\S]{0,500}?D9WG_OP_DESTROY_RESOURCE/,
        "the last DirectDraw reference must emit device destruction");
    assert.match(guestSource,
        /DLL_PROCESS_DETACH[\s\S]{0,500}?emit_session_end\(\)/,
        "DLL unload must close the process session even with live COM objects");
    assert.match(guestSource,
        /emit_command\(D9WG_OP_SESSION_END,\s*&end,\s*sizeof\(end\)\)/,
        "SESSION_END must carry the current process id through the protocol");
});

check("the guest rides the D3D9 batch function code, not one of its own",
        () => {
    assert.ok(guestSource.includes("V86GL_CTRL_D3D9_BATCH"),
        "the DirectDraw frontend must submit on 0xFFE1 like the D3D8 one");
    assert.ok(!guestSource.includes("V86GL_CTRL_D3D8_BATCH"));
});

check("mip and cube subresources keep their level and face on every wire path",
        () => {
    assert.match(guestSource, /update\.level\s*=\s*surface->mip_level/);
    assert.match(guestSource, /update\.z\s*=\s*surface->cube_face/);
    assert.match(guestSource, /command\.level\s*=\s*surface->mip_level/);
    assert.match(guestSource, /command\.face\s*=\s*surface->cube_face/);
    assert.match(guestSource,
        /storage->create_opcode = D9WG_OP_CREATE_TEXTURE_CUBE;[\s\S]*?storage->create\.texture_cube = cube_create;/,
        "a cube surface must record a cube create with the cube payload");
    assert.match(guestSource,
        /emit_command\(storage->create_opcode, &storage->create,\s*storage->create_bytes\)/,
        "the recorded create is what reaches the wire");
    assert.match(guestSource, /update\.height\s*=\s*bottom\s*-\s*top/,
        "a DXT upload carries logical texel height, not its block-row count");

    const readbackStart = executorSource.indexOf("async onReadbackSurface(");
    assert.ok(readbackStart >= 0, "the host has no readback handler");
    const readbackEnd = executorSource.indexOf("\n        // A \"frame\"", readbackStart);
    const readback = executorSource.slice(readbackStart, readbackEnd);
    assert.match(readback,
        /const face = length >= 52 \? view\.getUint32\(offset \+ 48, true\) : 0/,
        "52-byte requests must select a cube face while old requests use face 0");
    assert.match(readback,
        /origin: \{ x: 0, y: firstRow, z: face \}/,
        "the selected face must reach copyTextureToBuffer");
});

check("complex DirectDraw textures share one host allocation", () => {
    assert.match(guestSource,
        /struct DDTextureStorage \{[\s\S]*?LONG ref;[\s\S]*?uint32_t handle;/);
    assert.match(guestSource,
        /surface_build_mip_chain\(root,[\s\S]*?root->storage/,
        "mip children must share their root's storage");
    assert.match(guestSource, /root->cube_faces\[face\] = face_surface/);
    assert.match(guestSource,
        /InterlockedDecrement\(&storage->ref\) != 0/,
        "only the last face/mip may destroy the host cube texture");
});

check("advanced DirectDraw surface features are real core paths, not stubs",
        () => {
    assert.match(guestSource,
        /ddraw_DuplicateSurface[\s\S]*?surface_memory_addref\(duplicate->memory\)/,
        "DuplicateSurface must share the CPU pixel allocation");
    assert.match(guestSource,
        /ddraw_DuplicateSurface[\s\S]*?original->storage/,
        "DuplicateSurface must share the host texture allocation");
    assert.match(guestSource,
        /surface_SetSurfaceDesc[\s\S]*?surface_memory_create\([\s\S]*?FALSE\)/,
        "SetSurfaceDesc must install client-owned memory");
    assert.match(guestSource,
        /command\.overlay_id\s*=\s*surface->overlay_id/,
        "overlay commands need an identity distinct from shared pixels");
    assert.match(guestSource,
        /caps->dwCKeyCaps[\s\S]*?DDCKEYCAPS_DESTBLT[\s\S]*?DDCKEYCAPS_DESTOVERLAY/,
        "destination blit/overlay keys must be advertised");
    assert.match(guestSource,
        /tag##_SetSurfaceDesc[\s\S]*?surface_SetSurfaceDesc\(SURFACE7\(iface\),\s*&description2/,
        "Surface3/4 SetSurfaceDesc thunks must forward the widened descriptor");
    assert.match(guestSource,
        /tag##_DuplicateSurface[\s\S]*?ddraw_DuplicateSurface/,
        "DirectDraw1-4 DuplicateSurface thunks must reach the v7 core");
    assert.match(guestSource,
        /copy_caps_to_caller[\s\S]*?caller_size\s*<\s*sizeof\(caps\)[\s\S]*?CopyMemory\(destination,\s*&caps,\s*copy_size\)/,
        "GetCaps must copy only the caller-sized prefix for old DDCAPS layouts");
    assert.match(guestSource,
        /destination->dwSize\s*=\s*caller_size/,
        "GetCaps must preserve the DDCAPS version requested by the caller");
    assert.doesNotMatch(guestSource,
        /(?:driver|emulation)->dwSize\s*!=\s*sizeof\(DDCAPS\)/,
        "GetCaps must not reject pre-DX7 DDCAPS sizes");
    assert.match(guestSource,
        /object_display_size[\s\S]*?GetSystemMetrics\(SM_CXSCREEN\)[\s\S]*?GetSystemMetrics\(SM_CYSCREEN\)/,
        "a windowless normal-mode primary must fall back to the desktop size");
    assert.match(guestSource,
        /if \(caps & DDSCAPS_PRIMARYSURFACE\) \{\s*object_display_size\(object,\s*&width,\s*&height\)/,
        "CreateSurface must resolve a primary size without requiring exclusive mode");
    assert.doesNotMatch(guestSource,
        /if \(!width \|\| !height\) return DDERR_NOEXCLUSIVEMODE/,
        "a normal-mode primary must not require SetDisplayMode");
    for (const removedStub of ["UNSUPPORTED(\"DuplicateSurface",
            "UNSUPPORTED(\"SetSurfaceDesc", "UNSUPPORTED(\"UpdateOverlay\""])
        assert.ok(!guestSource.includes(removedStub),
            removedStub + " is still present as a refusal");
});

// The transport is demand-started and single-client, so "not open yet" is an
// ordinary state, not a fatal one. It reached a caller once as
// DDERR_OUTOFVIDEOMEMORY out of CreateSurface, with nothing written down
// anywhere, which is the combination these checks exist to prevent.
check("a closed transport is retried, recorded, and never fails CreateSurface",
        () => {
    assert.doesNotMatch(guestSource, /g_transport_failed/,
        "a transport failure must not latch the DLL off permanently");
    assert.match(guestSource,
        /GetTickCount\(\) - g_transport_retry_tick < DDWG_TRANSPORT_RETRY_MS/,
        "a failed open must be retried after a backoff");
    assert.match(guestSource,
        /\+\+g_transport_generation;/,
        "a successful open must start a new resource generation");
    assert.match(guestSource,
        /if \(emit_command\(D9WG_OP_HELLO[\s\S]*?g_hello_generation = g_transport_generation;/,
        "HELLO must be resent when it was dropped by a closed transport");
    assert.match(guestSource,
        /surface_create\(DDrawObject[\s\S]*?if \(!emit_surface_create\(surface\)\)\s*surface->upload_pending = TRUE;/,
        "CreateSurface must return a surface when only the host texture failed");
    assert.doesNotMatch(guestSource,
        /if \(!emit_surface_create\(surface\)\) \{\s*surface_Release/,
        "a missing host texture must not destroy the DirectDraw surface");
    assert.match(guestSource,
        /record_surface_storage_create\(surface, level_count, cube\);\s*return ensure_surface_storage\(surface\);/,
        "the create must be recorded before it is attempted, so it can be replayed");
    for (const site of ["emit_surface_upload", "emit_blt", "emit_blt_to_screen",
            "emit_overlay_state"])
        assert.ok(new RegExp(site + "[\\s\\S]{0,1400}?(?:ensure_surface_storage|prepare_surface_for_host)\\(")
                .test(guestSource),
            site + " must build a missing host texture before naming its handle");
    assert.match(guestSource,
        /CreateFileA\(g_log_path, FILE_APPEND_DATA/,
        "a transport failure must be written somewhere the guest can read it");
    assert.match(guestSource,
        /could not open \\\\\\\\\.\\\\v86gl \(Win32 error %lu\)/,
        "the log line must name the Win32 error behind a failed open");
});

if (failures.length) {
    for (const failure of failures) {
        console.error("FAIL " + failure.name);
        console.error("     " + (failure.error && failure.error.message));
    }
    console.error(failures.length + " failed, " + passed + " passed");
    process.exit(1);
}
console.log(passed + " ddraw protocol consistency checks passed");
