"use strict";

/*
 * Every literal `shader ... [vertex] [fragment]` and
 * `lazyshader ... [vertex] [fragment]` pair in the official Cube 2 r6889
 * data/glsl.cfg whose two blocks contain no CubeScript substitution.
 * Extracted from the 112302-byte source snapshot with SHA-256:
 * cd8963aedfcfb2285e0a2750d7cb7384c1c5ac3fdd7b125b462ede400bee47a4
 *
 * The two other top-level shader pairs, noglareblendworld and screenrect,
 * require macro substitution; their resolved forms are covered separately.
 */
globalThis.CUBE2_DIRECT_GLSL_CORPUS_R6889 = Object.freeze([
  {
    "name": "null",
    "vertex": "attribute vec4 vvertex;\n    void main(void)\n    {\n        gl_Position = vvertex;\n    }",
    "fragment": "void main(void)\n    {\n        gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);\n    }"
  },
  {
    "name": "hud",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 hudmatrix;\n    varying vec2 texcoord0;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = hudmatrix * vvertex;\n        texcoord0 = vtexcoord0;\n        color = vcolor;\n    }",
    "fragment": "varying vec2 texcoord0;\n    varying vec4 color;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        gl_FragColor = color * texture2D(tex0, texcoord0);\n    }"
  },
  {
    "name": "hudnotexture",
    "vertex": "attribute vec4 vvertex, vcolor;\n    uniform mat4 hudmatrix;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = hudmatrix * vvertex;\n        color = vcolor;\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor = color;\n    }"
  },
  {
    "name": "hudrgb",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 hudmatrix;\n    varying vec2 texcoord0;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = hudmatrix * vvertex;\n        texcoord0 = vtexcoord0;\n        color = vcolor;\n    }",
    "fragment": "varying vec2 texcoord0;\n    varying vec4 color;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        gl_FragColor.rgb = color.rgb * texture2D(tex0, texcoord0).rgb;\n        gl_FragColor.a   = color.a;\n    }"
  },
  {
    "name": "texture",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 camprojmatrix;\n    varying vec2 texcoord0;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        texcoord0 = vtexcoord0;\n        color = vcolor;\n    }",
    "fragment": "varying vec2 texcoord0;\n    varying vec4 color;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        gl_FragColor = color * texture2D(tex0, texcoord0);\n    }"
  },
  {
    "name": "notexture",
    "vertex": "attribute vec4 vvertex, vcolor;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor = color;\n    }"
  },
  {
    "name": "cubemap",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec3 vtexcoord0;\n    varying vec3 texcoord0;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = vvertex;\n        texcoord0 = vtexcoord0;\n        color = vcolor;\n    }",
    "fragment": "varying vec3 texcoord0;\n    varying vec4 color;\n    uniform samplerCube tex0;\n    void main(void)\n    {\n        gl_FragColor = color * textureCube(tex0, texcoord0);\n    }"
  },
  {
    "name": "fogged",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 camprojmatrix;\n    varying vec2 texcoord0;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        texcoord0 = vtexcoord0;\n        color = vcolor;\n    }",
    "fragment": "varying vec2 texcoord0;\n    varying vec4 color;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        gl_FragColor = color * texture2D(tex0, texcoord0);\n    }"
  },
  {
    "name": "foggednotexture",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor = color;\n    }"
  },
  {
    "name": "fogoverlay",
    "vertex": "attribute vec4 vvertex, vcolor;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = vvertex;\n        color = vcolor;\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor = color;\n    }"
  },
  {
    "name": "nocolor",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    void main() { gl_Position = camprojmatrix * vvertex; }",
    "fragment": "void main() {}"
  },
  {
    "name": "bbquery",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    uniform vec3 bborigin, bbsize;\n    void main() { gl_Position = camprojmatrix * vec4(bborigin + vvertex.xyz*bbsize, vvertex.w); }",
    "fragment": "void main() {}"
  },
  {
    "name": "fogworld",
    "vertex": "//:water\n    attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n    }",
    "fragment": "uniform vec3 fogcolor;\n    void main(void)\n    {\n        gl_FragColor = vec4(fogcolor, 1.0);\n    }"
  },
  {
    "name": "noglareworld",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n    }",
    "fragment": "void main(void)\n    {\n        gl_FragColor = vec4(0.0);\n    }"
  },
  {
    "name": "noglarealphaworld",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n    }",
    "fragment": "uniform vec4 colorparams;\n    uniform sampler2D lightmap;\n    void main(void)\n    {\n        gl_FragColor.rgb = vec3(0.0);\n        gl_FragColor.a = colorparams.a;\n    }"
  },
  {
    "name": "depthfxworld",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    uniform vec4 depthscale, depthoffsets;\n    varying vec4 depthranges;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        depthranges = depthoffsets + gl_Position.w*depthscale;\n    }",
    "fragment": "varying vec4 depthranges;\n    void main(void)\n    {\n        gl_FragColor = depthranges;\n    }"
  },
  {
    "name": "depthfxsplitworld",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    uniform vec4 depthscale, depthoffsets;\n    varying vec4 depthranges;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        depthranges = depthoffsets + gl_Position.w*depthscale;\n    }",
    "fragment": "varying vec4 depthranges;\n    void main(void)\n    {\n        vec4 ranges = vec4(depthranges.x, fract(depthranges.yzw));\n        ranges.xy -= ranges.yz*vec2(0.00390625, 0.00390625);\n        gl_FragColor = ranges;\n    }"
  },
  {
    "name": "shadowmapreceiver",
    "vertex": "attribute vec4 vvertex;\n    uniform mat4 shadowmatrix;\n    uniform vec2 shadowmapbias;\n    varying vec4 shadowmapvals;\n    void main(void)\n    {\n        gl_Position = shadowmatrix * vvertex;\n        shadowmapvals = vec4(0.0, 0.0, shadowmapbias.y - gl_Position.z, 0.0);\n    }",
    "fragment": "varying vec4 shadowmapvals;\n    void main(void)\n    {\n        gl_FragColor = shadowmapvals;\n    }"
  },
  {
    "name": "particlenotexture",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    uniform mat4 camprojmatrix;\n    uniform vec4 colorscale;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor * colorscale;\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor = color;\n    }"
  },
  {
    "name": "blendbrush",
    "vertex": "attribute vec4 vvertex, vcolor;\n    uniform mat4 camprojmatrix;\n    uniform vec4 texgenS, texgenT;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        texcoord0 = vec2(dot(texgenS, vvertex), dot(texgenT, vvertex));\n    }",
    "fragment": "varying vec4 color;\n    varying vec2 texcoord0;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        gl_FragColor = texture2D(tex0, texcoord0).r * color;\n    }"
  },
  {
    "name": "overbrightdecal",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        texcoord0 = vtexcoord0;\n    }",
    "fragment": "//:fog vec3(0.5)\n    varying vec4 color;\n    varying vec2 texcoord0;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        vec4 diffuse = texture2D(tex0, texcoord0);\n        gl_FragColor = mix(color, diffuse, color.a);\n    }"
  },
  {
    "name": "saturatedecal",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        texcoord0 = vtexcoord0;\n    }",
    "fragment": "varying vec4 color;\n    varying vec2 texcoord0;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        vec4 diffuse = texture2D(tex0, texcoord0);\n        diffuse.rgb *= 2.0;\n        gl_FragColor = diffuse * color;\n    }"
  },
  {
    "name": "skybox",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 skymatrix;\n    varying vec2 texcoord0;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = skymatrix * vvertex;\n        texcoord0 = vtexcoord0;\n        color = vcolor;\n    }",
    "fragment": "varying vec2 texcoord0;\n    varying vec4 color;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        gl_FragColor = color * texture2D(tex0, texcoord0);\n    }"
  },
  {
    "name": "skyboxglare",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec2 vtexcoord0;\n    uniform mat4 skymatrix;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = skymatrix * vvertex;\n        color = vcolor;\n        texcoord0 = vtexcoord0;\n    }",
    "fragment": "varying vec4 color;\n    varying vec2 texcoord0;\n    uniform sampler2D tex0;\n    void main(void)\n    {\n        vec4 glare = texture2D(tex0, texcoord0) * color;\n        gl_FragColor.rgb = vec3(dot(glare.rgb, vec3(10.56, 10.88, 10.56)) - 30.4);\n        gl_FragColor.a = glare.a;\n    }"
  },
  {
    "name": "skyfog",
    "vertex": "attribute vec4 vvertex, vcolor;\n    uniform mat4 skymatrix;\n    varying vec4 color;\n    void main(void)\n    {\n        gl_Position = skymatrix * vvertex;\n        color = vcolor;\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor =\n            //:variantoverride vec4(color.rgb, color.a * color.a)\n            color\n            ;\n    }"
  },
  {
    "name": "prefab",
    "vertex": "attribute vec4 vvertex, vcolor;\n    attribute vec3 vnormal;\n    uniform mat4 prefabmatrix;\n    uniform mat3 prefabworld;\n    varying vec4 color;\n\n    void main(void)\n    {\n        gl_Position = prefabmatrix * vvertex;\n        color = vcolor;\n        color.rgb *= dot(prefabworld * vnormal, vec3(0.0, -0.447213595, 0.894427191));\n    }",
    "fragment": "varying vec4 color;\n    void main(void)\n    {\n        gl_FragColor = color;\n    }"
  },
  {
    "name": "waterglarefast",
    "vertex": "//:fog\n    attribute vec4 vvertex;\n    uniform mat4 camprojmatrix;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n    }",
    "fragment": "void main(void)\n    {\n        gl_FragColor = vec4(0.0);\n    }"
  },
  {
    "name": "underwater",
    "vertex": "attribute vec4 vvertex;\n    attribute vec3 vcolor;\n    uniform mat4 camprojmatrix;\n    varying vec3 color;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n    }",
    "fragment": "//:fogrgba vec4(vec3(0.0), 1.0)\n    uniform vec2 depth;\n    varying vec3 color;\n    void main(void)\n    {\n        gl_FragColor.rgb = 0.8*depth.x*color;\n        gl_FragColor.a = 0.5*depth.y;\n    }"
  },
  {
    "name": "lava",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec3 vnormal;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    uniform vec4 lavatexgen;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        vec2 tc = mix(vvertex.xz, vvertex.yy, abs(vnormal.xz));\n        texcoord0 = (tc + lavatexgen.zw) * lavatexgen.xy;\n    }",
    "fragment": "uniform sampler2D tex0;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_FragColor = color * texture2D(tex0, texcoord0) * 2.0;\n    }"
  },
  {
    "name": "lavaglare",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec3 vnormal;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    uniform vec4 lavatexgen;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vec4(vcolor.rgb*2.0 - 1.0, vcolor.a);\n        vec2 tc = mix(vvertex.xz, vvertex.yy, abs(vnormal.xz));\n        texcoord0 = (tc + lavatexgen.zw) * lavatexgen.xy;\n    }",
    "fragment": "uniform sampler2D tex0;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        vec4 glow = texture2D(tex0, texcoord0) * color;\n        float k = max(glow.r, max(glow.g, glow.b));\n        gl_FragColor = glow*k*k*32.0;\n    }"
  },
  {
    "name": "waterfall",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec3 vnormal;\n    uniform mat4 camprojmatrix;\n    varying vec4 color;\n    uniform vec4 waterfalltexgen;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        vec2 tc = mix(vvertex.xz, vvertex.yy, abs(vnormal.xz));\n        texcoord0 = (tc + waterfalltexgen.zw) * waterfalltexgen.xy;\n    }",
    "fragment": "uniform sampler2D tex0;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    void main(void)\n    {\n        gl_FragColor = color * texture2D(tex0, texcoord0);\n    }"
  },
  {
    "name": "waterfallrefract",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec3 vnormal;\n    uniform mat4 camprojmatrix;\n    uniform mat4 watermatrix;\n    varying vec4 color;\n    uniform vec4 waterfalltexgen;\n    varying vec2 texcoord0;\n    varying vec4 texcoord1;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        vec2 tc = mix(vvertex.xz, vvertex.yy, abs(vnormal.xz));\n        texcoord0 = (tc + waterfalltexgen.zw) * waterfalltexgen.xy;\n        texcoord1 = watermatrix * vvertex;\n    }",
    "fragment": "uniform vec2 dudvoffset;\n    uniform sampler2D tex0, tex2, tex4;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    varying vec4 texcoord1;\n    void main(void)\n    {\n        vec4 diffuse = texture2D(tex0, texcoord0);\n        vec2 dudv = texture2D(tex2, texcoord0 + 0.2*diffuse.xy + dudvoffset).xy;\n        vec4 refract = texture2DProj(tex4, texcoord1 + vec4(4.0*dudv, 0.0, 0.0));\n        gl_FragColor = mix(refract, color, diffuse);\n    }"
  },
  {
    "name": "waterfallenvrefract",
    "vertex": "//:fog\n    attribute vec4 vvertex, vcolor;\n    attribute vec3 vnormal;\n    uniform mat4 camprojmatrix;\n    uniform mat4 watermatrix;\n    uniform vec3 camera;\n    varying vec4 color;\n    uniform vec4 waterfalltexgen;\n    varying vec2 texcoord0;\n    varying vec4 texcoord1;\n    varying vec3 camdir;\n    varying mat3 world;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        camdir = camera - vvertex.xyz;\n        vec3 absnorm = abs(vnormal);\n        world = mat3(absnorm.yzx, -absnorm.zxy, vnormal);\n        vec2 tc = mix(vvertex.xz, vvertex.yy, absnorm.xz);\n        texcoord0 = (tc + waterfalltexgen.zw) * waterfalltexgen.xy;\n        texcoord1 = watermatrix * vvertex;\n    }",
    "fragment": "uniform vec2 dudvoffset;\n    uniform sampler2D tex0, tex1, tex2, tex4;\n    uniform samplerCube tex3;\n    varying vec4 color;\n    varying vec2 texcoord0;\n    varying vec4 texcoord1;\n    varying vec3 camdir;\n    varying mat3 world;\n    void main(void)\n    {\n        vec4 diffuse = texture2D(tex0, texcoord0);\n        vec2 dudv = texture2D(tex2, texcoord0 + 0.2*diffuse.xy + dudvoffset).xy;\n        vec3 normal = world * (texture2D(tex1, texcoord0 + 0.1*dudv).rgb*2.0 - 1.0);\n        vec4 refract = texture2DProj(tex4, texcoord1 + vec4(4.0*dudv, 0.0, 0.0));\n        vec3 camvec = normalize(camdir);\n        float invfresnel = dot(normal, camvec);\n        vec4 reflect = textureCube(tex3, 2.0*invfresnel*normal - camvec);\n        gl_FragColor = mix(mix(reflect, refract, 1.0 - 0.4*step(0.0, invfresnel)), color, diffuse);\n    }"
  },
  {
    "name": "waterfallenv",
    "vertex": "//:fog\n    attribute vec4 vvertex;\n    attribute vec3 vcolor, vnormal;\n    uniform mat4 camprojmatrix;\n    uniform vec3 camera;\n    uniform vec4 waterfalltexgen;\n    varying vec2 texcoord0;\n    varying vec3 color, camdir;\n    varying mat3 world;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        camdir = camera - vvertex.xyz;\n        vec3 absnorm = abs(vnormal);\n        world = mat3(absnorm.yzx, -absnorm.zxy, vnormal);\n        vec2 tc = mix(vvertex.xz, vvertex.yy, absnorm.xz);\n        texcoord0 = (tc + waterfalltexgen.zw) * waterfalltexgen.xy;\n    }",
    "fragment": "uniform vec2 dudvoffset;\n    uniform sampler2D tex0, tex1, tex2;\n    uniform samplerCube tex3;\n    varying vec2 texcoord0;\n    varying vec3 color, camdir;\n    varying mat3 world;\n    void main(void)\n    {\n        vec4 diffuse = texture2D(tex0, texcoord0);\n        vec2 dudv = texture2D(tex2, texcoord0 + 0.2*diffuse.xy + dudvoffset).xy;\n        vec3 normal = world * (texture2D(tex1, texcoord0 + 0.1*dudv).rgb*2.0 - 1.0);\n        vec3 camvec = normalize(camdir);\n        vec4 reflect = textureCube(tex3, 2.0*dot(normal, camvec)*normal - camvec);\n        gl_FragColor.rgb = mix(reflect.rgb, color, diffuse.rgb);\n        gl_FragColor.a = 0.25 + 0.75*diffuse.r;\n    }"
  },
  {
    "name": "glass",
    "vertex": "attribute vec4 vvertex;\n    attribute vec3 vcolor, vnormal;\n    uniform mat4 camprojmatrix;\n    uniform vec3 camera;\n    varying vec3 color, rvec, camdir, normal;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        normal = vnormal;\n        camdir = camera - vvertex.xyz;\n        rvec = 2.0*dot(camdir, vnormal) * vnormal - camdir;\n    }",
    "fragment": "//:fogrgba vec4(vec3(0.0), 1.0)\n    uniform samplerCube tex0;\n    varying vec3 color, rvec, camdir, normal;\n    void main(void)\n    {\n        vec3 camvec = normalize(camdir);\n        vec3 reflect = textureCube(tex0, rvec).rgb;\n\n        float invfresnel = max(dot(camvec, normal), 0.70);\n        gl_FragColor.rgb = mix(reflect, color*0.05, invfresnel);\n        gl_FragColor.a = invfresnel * 0.95;\n    }"
  },
  {
    "name": "glassfast",
    "vertex": "attribute vec4 vvertex;\n    attribute vec3 vcolor, vnormal;\n    uniform mat4 camprojmatrix;\n    uniform vec3 camera;\n    varying vec3 color, rvec;\n    void main(void)\n    {\n        gl_Position = camprojmatrix * vvertex;\n        color = vcolor;\n        vec3 camdir = camera - vvertex.xyz;\n        rvec = 2.0*dot(camdir, vnormal) * vnormal - camdir;\n    }",
    "fragment": "//:fogrgba vec4(vec3(0.0), 1.0)\n    uniform samplerCube tex0;\n    varying vec3 color, rvec;\n    void main(void)\n    {\n        vec3 reflect = textureCube(tex0, rvec).rgb;\n        const float invfresnel = 0.75;\n        gl_FragColor.rgb = mix(reflect, color*0.05, invfresnel);\n        gl_FragColor.a = invfresnel * 0.95;\n    }"
  }
]);
