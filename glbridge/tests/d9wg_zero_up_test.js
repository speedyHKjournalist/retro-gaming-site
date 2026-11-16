"use strict";
process.env.D9WG_TEST_ZERO_UP = "1";
if (process.env.D9WG_ZERO_UP_SOURCE) process.env.D9WG_TARGET_BINDING_SOURCE = process.env.D9WG_ZERO_UP_SOURCE;
require("./d9wg_target_binding_test.js");
