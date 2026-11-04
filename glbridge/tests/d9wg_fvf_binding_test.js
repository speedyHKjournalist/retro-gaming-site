"use strict";
process.env.D9WG_TEST_FVF_BINDINGS = "1";
if (process.env.D9WG_FVF_BINDING_SOURCE) process.env.D9WG_TARGET_BINDING_SOURCE = process.env.D9WG_FVF_BINDING_SOURCE;
require("./d9wg_target_binding_test.js");
