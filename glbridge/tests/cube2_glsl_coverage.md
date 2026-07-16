# Cube 2 GLSL r6889 coverage

Source: official `data/glsl.cfg`, 112302 bytes, SHA-256
`cd8963aedfcfb2285e0a2750d7cb7384c1c5ac3fdd7b125b462ede400bee47a4`.

`cube2_glsl_browser_test.html` sends every covered pair through the real
guest-name mapped gl4es shader lifecycle: create, source, compile, attach,
bind standard Cube attributes, link, query status/log, and (for the complex
sky shader) reflect active uniforms.

| Cube shader source class | Coverage | Notes |
| --- | ---: | --- |
| Literal top-level `shader` pairs | 25/25 | Every pair with no `@` CubeScript substitution is in `cube2_glsl_direct_corpus_r6889.js`. |
| Literal top-level pairs with scalar macro substitution | 2/2 | `noglareblendworld` resolves `lmcoordscale`; `screenrect` resolves `screentexcoord 0`. |
| Literal `lazyshader` pairs | 11/11 | Prefab, underwater, lava, waterfall and glass families with no CubeScript substitution. |
| Atmosphere loop | 1 representative | The full non-glare branch is resolved and exercises the largest math/uniform set plus `min`/`max`. |
| Postprocess generators | Representative | `screenrect` covers the common full-screen vertex path. Blur/bloom/HSV/rotoscope bodies depend on nested CubeScript functions and runtime arguments. |
| World/bump generators | Representative only | Direct fog/depth/world shaders are covered. The 40+ `bumpshader` combinations depend on `btopt`, nested variants and runtime slot options. |
| Model/skinning generators | Not statically expanded | `modelshader`, `skelanimdefs` and `skelanim` depend on option strings and loop-generated animation variants. |
| Explosion/particle generators | Representative only | `particlenotexture` is literal; soft/soft8 and explosion variants depend on runtime depth settings. |
| Water generator | Literal subfamilies + representative | Literal underwater/lava/waterfall/glass pairs are covered; the main 18-name `watershader` family is runtime-expanded. |
| Movie/grass/caustic generators | Not statically expanded | These retain nested substitutions or runtime-selected branches. |

The non-expanded rows require either executing CubeScript itself or loading the
real game data through Sauerbraten. They are therefore explicit targets for
the final in-v86 game smoke test, not silently counted as corpus passes.
