"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const proxy = fs.readFileSync(
    path.join(root, "winproxy", "opengl32_proxy.c"), "utf8");
const bridge = fs.readFileSync(
    path.join(root, "libglwasm", "gl4es_bridge.c"), "utf8");

assert.match(proxy, /static BOOL grow_attrib_locations\(void\)/,
    "the guest attribute table must grow past Cube 2's first 64 programs");
assert.match(proxy, /static BOOL grow_uniform_locations\(void\)/,
    "the guest uniform table must grow instead of dropping later variants");
assert.doesNotMatch(proxy,
    /static AttribLocationState g_attrib_locations\s*\[/,
    "the guest attribute table must not regress to a fixed array");
assert.doesNotMatch(proxy,
    /static UniformLocationState g_uniform_locations\s*\[/,
    "the guest uniform table must not regress to a fixed array");

assert.match(bridge,
    /static int v86gl_map_location\(V86GLLocationMap\*\* maps/,
    "the host mapping helper must report allocation failure");
assert.match(bridge, /realloc\(\s*\*maps,/,
    "the host location maps must grow on demand");
assert.match(bridge,
    /guest_program == g_current_guest_program\s*&&\s*\n\s*g_attrib_locations\[i\]\.guest == guest/,
    "attribute lookup must be isolated by the current program");
assert.doesNotMatch(bridge,
    /static V86GLLocationMap g_attrib_locations\s*\[/,
    "the host attribute map must not regress to a fixed array");
assert.doesNotMatch(bridge,
    /static V86GLLocationMap g_uniform_locations\s*\[/,
    "the host uniform map must not regress to a fixed array");

console.log("location map source regression tests passed");
