"use strict";
process.env.D9WG_TEST_SNAPSHOT_BLOCK8="1";
if(process.env.D8WG_SNAPSHOT_BLOCK_CORE_SOURCE)process.env.D9WG_TARGET_BINDING_SOURCE=process.env.D8WG_SNAPSHOT_BLOCK_CORE_SOURCE;
require('./d9wg_target_binding_test.js');
