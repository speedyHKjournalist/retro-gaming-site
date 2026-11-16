"use strict";
process.env.D9WG_TEST_INDEXED8="1";
if(process.env.D8WG_INDEXED_CORE_SOURCE)process.env.D9WG_BOUND_DRAW_SOURCE=process.env.D8WG_INDEXED_CORE_SOURCE;
require('./d9wg_bound_draw_test.js');
