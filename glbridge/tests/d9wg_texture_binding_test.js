"use strict";
// Reuse the actual device/target construction and teardown harness so texture
// bindings are checked against the same live device and resource graph.
process.env.D9WG_TEST_TEXTURE_BINDINGS = "1";
if (process.env.D9WG_TEXTURE_BINDING_SOURCE) process.env.D9WG_TARGET_BINDING_SOURCE = process.env.D9WG_TEXTURE_BINDING_SOURCE;
require("./d9wg_target_binding_test.js");
