"use strict";
process.env.D9WG_TEST_CUBE_BINDINGS = "1";
if (process.env.D9WG_CUBE_BINDING_SOURCE) process.env.D9WG_TARGET_BINDING_SOURCE = process.env.D9WG_CUBE_BINDING_SOURCE;
require("./d9wg_target_binding_test.js");
