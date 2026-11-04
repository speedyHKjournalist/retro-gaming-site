"use strict";
process.env.D9WG_TEST_DECLARATION_FVF = "1";
if (process.env.D9WG_DECLARATION_FVF_SOURCE) process.env.D9WG_TARGET_BINDING_SOURCE = process.env.D9WG_DECLARATION_FVF_SOURCE;
require("./d9wg_target_binding_test.js");
