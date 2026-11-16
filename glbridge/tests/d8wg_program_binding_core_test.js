"use strict";
process.env.D9WG_TEST_LEGACY_PROGRAM_BINDINGS="1";
if(process.env.D8WG_PROGRAM_BINDING_CORE_SOURCE)process.env.D9WG_TARGET_BINDING_SOURCE=process.env.D8WG_PROGRAM_BINDING_CORE_SOURCE;
require('./d9wg_target_binding_test.js');
