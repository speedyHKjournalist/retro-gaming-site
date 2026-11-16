"use strict";
process.env.D9WG_TEST_LEGACY_FVF_BINDINGS="1";
if(process.env.D8WG_FVF8_CORE_SOURCE)process.env.D9WG_TARGET_BINDING_SOURCE=process.env.D8WG_FVF8_CORE_SOURCE;
require("./d9wg_target_binding_test.js");
