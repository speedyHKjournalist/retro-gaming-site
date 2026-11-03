"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-ownership-mutants-"));
const cases = [
    ["DD4/7 surface creator not retained", "surface->owner_public_ref = version >= DD_VERSION_4;", "surface->owner_public_ref = version > DD_VERSION_7;"],
    ["DD1/2 palette changes public parent count", "palette->owner_public_ref = version >= DD_VERSION_4;", "palette->owner_public_ref = version >= 1;"],
    ["duplicate creator not retained", "duplicate->owner_public_ref = version >= DD_VERSION_4;", "duplicate->owner_public_ref = version > DD_VERSION_7;"],
    ["child allocation has no private hold", "InterlockedIncrement(&object->lifetime_ref);\n    if (public_ref)", "(void)object;\n    if (public_ref)"],
    ["public AddRef omits physical hold", "InterlockedIncrement(&object->lifetime_ref);\n    return (ULONG)InterlockedIncrement(&object->ref);", "return (ULONG)InterlockedIncrement(&object->ref);"],
    ["child private hold leaked", "    ddraw_lifetime_release(object);\n}\n\nstatic void d3d7_rebind", "    (void)object;\n}\n\nstatic void d3d7_rebind"],
    ["palette parent pin leaked", "ddraw_child_detach(owner, public_ref);\n    }\n    return (ULONG)(remaining", "ddraw_child_detach(owner, FALSE && public_ref);\n    }\n    return (ULONG)(remaining"],
    ["surface parent pin leaked", "ddraw_child_detach(owner, public_ref);\n}\n\n/*", "ddraw_child_detach(owner, FALSE && public_ref);\n}\n\n/*"],
    ["same palette released before retain", "if (palette) IDirectDrawPalette_AddRef(palette_iface);\n    if (surface->palette)\n        IDirectDrawPalette_Release((IDirectDrawPalette *)surface->palette);\n    surface->palette = palette;", "if (surface->palette)\n        IDirectDrawPalette_Release((IDirectDrawPalette *)surface->palette);\n    surface->palette = palette;\n    if (palette) IDirectDrawPalette_AddRef(palette_iface);"],
    ["same clipper released before retain", "if (clipper) IDirectDrawClipper_AddRef(clipper_iface);\n    if (surface->clipper)\n        IDirectDrawClipper_Release((IDirectDrawClipper *)surface->clipper);\n    surface->clipper = clipper;", "if (surface->clipper)\n        IDirectDrawClipper_Release((IDirectDrawClipper *)surface->clipper);\n    surface->clipper = clipper;\n    if (clipper) IDirectDrawClipper_AddRef(clipper_iface);"],
    ["implicit count zero frees ring member", "if (remaining == 0 && !surface->implicit)", "if (remaining == 0)"],
    ["root flip chain ignored", "if (surface->owns_flip_chain) {", "if (FALSE && surface->owns_flip_chain) {"],
    ["implicit flip child uses public Release", "if (walk->implicit) surface_destroy(walk);", "if (walk->implicit) surface_Release((IDirectDrawSurface7 *)&walk->vtbl7);"],
    ["implicit mip uses public Release", "if (mip->implicit) surface_destroy(mip);", "if (mip->implicit) surface_Release((IDirectDrawSurface7 *)&mip->vtbl7);"],
    ["implicit cube uses public Release", "if (attached->implicit) surface_destroy(attached);", "if (attached->implicit) surface_Release((IDirectDrawSurface7 *)&attached->vtbl7);"],
    ["soft flip attachment never releases", "else surface_Release((IDirectDrawSurface7 *)&walk->vtbl7);", "else (void)walk;"],
    ["implicit mip can detach and leak", "if (surface->mip_next == other)\n            return DDWG_TRACE_ERROR(DDERR_CANNOTDETACHSURFACE);", "if (FALSE && surface->mip_next == other)\n            return DDWG_TRACE_ERROR(DDERR_CANNOTDETACHSURFACE);"],
    ["partial flip allocation reported success", "surface_destroy(surface);\n                return DDWG_TRACE_ERROR(DDERR_OUTOFVIDEOMEMORY);", "break;"],
];
try {for (const [label, before, after] of cases) {assert.equal(source.split(before).length, 2, label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.replace(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_ownership_test.js")], {env: {...process.env, DDRAW_OWNERSHIP_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw ownership line/, "strict compilation then runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw ownership mutations: 18/18, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
