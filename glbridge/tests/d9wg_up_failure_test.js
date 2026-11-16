"use strict";
process.env.D9WG_TEST_UP_FAILURE = "1";
if (process.env.D9WG_UP_FAILURE_SOURCE) process.env.D9WG_TARGET_BINDING_SOURCE = process.env.D9WG_UP_FAILURE_SOURCE;
require("./d9wg_target_binding_test.js");
