globalThis.V86GL_BUILD_REVISION = "8cc48a9a2b75d0296e91";
// The OpenGL protocol, once (docs/opengl-webgpu-plan.md C1): the VGL2 records
// openglproxy's opengl32.dll writes and gl_executor.js reads -- every opcode,
// the control records, the flat argument lists, the fixed headers of the
// hand-written records and the constants both sides use. The guest's
// gl_protocol.h and gl_opcode_names.h are generated from this file
// (tools/gen_gl_protocol_header.mjs), with an assert of every record header's
// size and every field's offset.
//
// A record is u16 opcode, u16 size (0xFFFF: a u32 size follows), then the
// payload. A flat record's payload is its argument list (i = int32, u = uint32,
// f = float32, d = float64); a hand-written record starts with a fixed header
// and carries strings, arrays or pixels after it, decoded by the handler.
(function(global) {
    "use strict";

    // [name, opcode, GL entry point, argument codes] for a flat record,
    // [name, opcode, null, "type:field ..."] for a fixed header, [name, opcode]
    // for a record whose layout only its handler knows yet. A name list
    // (GEN_*, DELETE_*) is its count, then that many u32 names; DRAW_BUFFERS
    // is its count, then the buffers.
    const OPCODES = [
        ["VIEWPORT", 1, "Viewport", "iiii"],
        ["CLEAR_COLOR", 2, "ClearColor", "ffff"],
        ["CLEAR", 3, "Clear", "u"],
        ["BEGIN", 4, "Begin", "u"],
        ["END", 5, "End", ""],
        ["COLOR4F", 6, "Color4f", "ffff"],
        ["VERTEX3F", 7, "Vertex3f", "fff"],
        ["FLUSH", 8, "Flush", ""],
        ["FINISH", 9, "Finish", ""],
        ["MATRIX_MODE", 10, "MatrixMode", "u"],
        ["LOAD_IDENTITY", 11, "LoadIdentity", ""],
        ["FRUSTUM", 12, "Frustum", "dddddd"],
        ["ORTHO", 13, "Ortho", "dddddd"],
        ["TRANSLATEF", 14, "Translatef", "fff"],
        ["ROTATEF", 15, "Rotatef", "ffff"],
        ["SCALEF", 16, "Scalef", "fff"],
        ["PUSH_MATRIX", 17, "PushMatrix", ""],
        ["POP_MATRIX", 18, "PopMatrix", ""],
        ["ENABLE", 19, "Enable", "u"],
        ["DISABLE", 20, "Disable", "u"],
        ["DEPTH_FUNC", 21, "DepthFunc", "u"],
        ["CLEAR_DEPTH", 22, "ClearDepth", "d"],
        ["SHADE_MODEL", 23, "ShadeModel", "u"],
        ["CULL_FACE", 24, "CullFace", "u"],
        ["FRONT_FACE", 25, "FrontFace", "u"],
        ["GEN_TEXTURES", 26, null, "u:count"],
        ["DELETE_TEXTURES", 27, null, "u:count"],
        ["BIND_TEXTURE", 28, "BindTexture", "uu"],
        ["TEX_IMAGE_2D", 29, null, "u:target i:level i:internalformat i:width i:height i:border u:format u:type u:data_size"],
        ["TEX_SUB_IMAGE_2D", 30, null, "u:target i:level i:xoffset i:yoffset i:width i:height u:format u:type u:data_size"],
        ["TEX_PARAMETERI", 31, "TexParameteri", "uui"],
        ["TEX_PARAMETERF", 32, "TexParameterf", "uuf"],
        ["PIXEL_STOREI", 33, "PixelStorei", "ui"],
        ["TEX_ENVI", 34, "TexEnvi", "uui"],
        ["TEX_ENVF", 35, "TexEnvf", "uuf"],
        ["TEX_COORD2F", 36, "TexCoord2f", "ff"],
        ["ENABLE_CLIENT_STATE", 37, "EnableClientState", "u"],
        ["DISABLE_CLIENT_STATE", 38, "DisableClientState", "u"],
        ["DRAW_ARRAYS", 39, null, "u:mode i:count"], // optional MT header, then array blocks
        ["DRAW_ELEMENTS", 40, null, "u:mode i:count u:type u:index_data_size"],
        ["BLEND_FUNC", 41, "BlendFunc", "uu"],
        ["ALPHA_FUNC", 42, "AlphaFunc", "uf"],
        ["DEPTH_MASK", 43, "DepthMask", "u"],
        ["COLOR_MASK", 44, "ColorMask", "uuuu"],
        ["SCISSOR", 45, "Scissor", "iiii"],
        ["LINE_WIDTH", 46, "LineWidth", "f"],
        ["POLYGON_MODE", 47, "PolygonMode", "uu"],
        ["ACTIVE_TEXTURE", 48, "ActiveTexture", "u"],
        ["CLIENT_ACTIVE_TEXTURE", 49, "ClientActiveTexture", "u"],
        ["MULTI_TEX_COORD4F", 50, "MultiTexCoord4f", "uffff"],
        ["NORMAL3F", 51, "Normal3f", "fff"],
        ["FOGF", 52, "Fogf", "uf"],
        ["FOGI", 53, "Fogi", "ui"],
        ["FOGFV", 54, "Fogfv4", "uuffff"],
        ["MATERIALF", 55, "Materialf", "uuf"],
        ["MATERIALI", 56, "Materiali", "uui"],
        ["MATERIALFV", 57, "Materialfv4", "uuuffff"],
        ["MATERIALIV", 58, "Materialiv4", "uuuiiii"],
        ["TEX_ENVIV", 59, "TexEnviv4", "uuuiiii"],
        ["TEX_ENVFV", 60, "TexEnvfv4", "uuuffff"],
        ["LIGHTF", 61, "Lightf", "uuf"],
        ["LIGHTI", 62, "Lighti", "uui"],
        ["LIGHTFV", 63, "Lightfv4", "uuuffff"],
        ["LIGHTIV", 64, "Lightiv4", "uuuiiii"],
        ["LIGHT_MODELF", 65, "LightModelf", "uf"],
        ["LIGHT_MODELI", 66, "LightModeli", "ui"],
        ["LIGHT_MODELFV", 67, "LightModelfv4", "uuffff"],
        ["LIGHT_MODELIV", 68, "LightModeliv4", "uuiiii"],
        ["LOAD_MATRIXF", 69, null, "f:values[16]"],
        ["MULT_MATRIXF", 70, null, "f:values[16]"],
        ["DEPTH_RANGE", 71, "DepthRange", "dd"],
        ["CLEAR_STENCIL", 72, "ClearStencil", "i"],
        ["STENCIL_FUNC", 73, "StencilFunc", "uiu"],
        ["STENCIL_MASK", 74, "StencilMask", "u"],
        ["STENCIL_OP", 75, "StencilOp", "uuu"],
        ["HINT", 76, "Hint", "uu"],
        ["POLYGON_OFFSET", 77, "PolygonOffset", "ff"],
        ["TEX_PARAMETERIV", 78, "TexParameteriv4", "uuuiiii"],
        ["TEX_PARAMETERFV", 79, "TexParameterfv4", "uuuffff"],
        ["TEX_GENI", 80, "TexGeni", "uui"],
        ["TEX_GENF", 81, "TexGenf", "uuf"],
        ["TEX_GENIV", 82, "TexGeniv4", "uuuiiii"],
        ["TEX_GENFV", 83, "TexGenfv4", "uuuffff"],
        ["CLIP_PLANE", 84, "ClipPlane4d", "udddd"],
        ["COLOR_MATERIAL", 85, "ColorMaterial", "uu"],
        ["PUSH_ATTRIB", 86, "PushAttrib", "u"],
        ["POP_ATTRIB", 87, "PopAttrib", ""],
        ["PUSH_CLIENT_ATTRIB", 88, "PushClientAttrib", "u"],
        ["POP_CLIENT_ATTRIB", 89, "PopClientAttrib", ""],
        ["DRAW_BUFFER", 90, "DrawBuffer", "u"],
        ["READ_BUFFER", 91, "ReadBuffer", "u"],
        ["COPY_TEX_IMAGE_2D", 92, "CopyTexImage2D", "uiuiiiii"],
        ["COPY_TEX_SUB_IMAGE_2D", 93, "CopyTexSubImage2D", "uiiiiiii"],
        ["READ_PIXELS", 94, null, "i:x i:y i:width i:height u:format u:type u:data_size u:status"],
        ["BLEND_COLOR", 95, "BlendColor", "ffff"],
        ["BLEND_EQUATION", 96, "BlendEquation", "u"],
        ["BLEND_FUNC_SEPARATE", 97, "BlendFuncSeparate", "uuuu"],
        ["SAMPLE_COVERAGE", 98, "SampleCoverage", "fu"],
        ["GENERATE_MIPMAP", 99, "GenerateMipmap", "u"],
        ["FOG_COORDF", 100, "FogCoordf", "f"],
        ["SECONDARY_COLOR3F", 101, "SecondaryColor3f", "fff"],
        ["POINT_PARAMETERF", 102, "PointParameterf", "uf"],
        ["POINT_PARAMETERFV", 103, "PointParameterfv3", "ufff"],
        ["COMPRESSED_TEX_IMAGE_2D", 104, null, "u:target i:level u:internalformat i:width i:height i:depth i:border u:image_size"],
        ["TEX_IMAGE_3D", 105, null, "u:target i:level i:internalformat i:width i:height i:depth i:border u:format u:type u:data_size"],
        ["TEX_SUB_IMAGE_3D", 106, null, "u:target i:level i:xoffset i:yoffset i:zoffset i:width i:height i:depth u:format u:type u:data_size"],
        ["COMPRESSED_TEX_IMAGE_1D", 107, null, "u:target i:level u:internalformat i:width i:height i:depth i:border u:image_size"],
        ["COMPRESSED_TEX_IMAGE_3D", 108, null, "u:target i:level u:internalformat i:width i:height i:depth i:border u:image_size"],
        ["COMPRESSED_TEX_SUB_IMAGE_1D", 109, null, "u:target i:level i:xoffset i:yoffset i:zoffset i:width i:height i:depth u:format u:image_size"],
        ["COMPRESSED_TEX_SUB_IMAGE_2D", 110, null, "u:target i:level i:xoffset i:yoffset i:zoffset i:width i:height i:depth u:format u:image_size"],
        ["COMPRESSED_TEX_SUB_IMAGE_3D", 111, null, "u:target i:level i:xoffset i:yoffset i:zoffset i:width i:height i:depth u:format u:image_size"],
        ["WINDOW_POS3F", 112, "WindowPos3f", "fff"],
        ["POINT_PARAMETERI", 113, "PointParameteri", "ui"],
        ["POINT_PARAMETERIV", 114, "PointParameteriv3", "uiii"],
        ["POINT_SIZE", 115, "PointSize", "f"],
        ["LINE_STIPPLE", 116, "LineStipple", "iu"],
        ["LOGIC_OP", 117, "LogicOp", "u"],
        ["PIXEL_TRANSFERF", 118, "PixelTransferf", "uf"],
        ["PIXEL_TRANSFERI", 119, "PixelTransferi", "ui"],
        ["PIXEL_ZOOM", 120, "PixelZoom", "ff"],
        ["DRAW_PIXELS", 121, null, "i:width i:height u:format u:type u:data_size"],
        ["BITMAP", 122, null, "i:width i:height f:xorig f:yorig f:xmove f:ymove u:data_size"],
        ["COPY_PIXELS", 123, "CopyPixels", "iiiiu"],
        ["TEX_IMAGE_1D", 124, null, "u:target i:level i:internalformat i:width i:border u:format u:type u:data_size"],
        ["TEX_SUB_IMAGE_1D", 125, null, "u:target i:level i:xoffset i:width u:format u:type u:data_size"],
        ["COPY_TEX_IMAGE_1D", 126, "CopyTexImage1D", "uiuiiii"],
        ["COPY_TEX_SUB_IMAGE_1D", 127, "CopyTexSubImage1D", "uiiiii"],
        ["CLEAR_ACCUM", 128, "ClearAccum", "ffff"],
        ["ACCUM", 129, "Accum", "uf"],
        ["POLYGON_STIPPLE", 130, null, "u:mask[32]"],
        ["PIXEL_MAPFV", 131, null, "u:map i:count"],
        ["PIXEL_MAPUIV", 132, null, "u:map i:count"],
        ["PIXEL_MAPUSV", 133, null, "u:map i:count"],
        ["TEX_COORD4F", 134, "TexCoord4f", "ffff"],
        ["VERTEX4F", 135, "Vertex4f", "ffff"],
        ["RASTER_POS4F", 136, "RasterPos4f", "ffff"],
        ["BLEND_EQUATION_SEPARATE", 137, "BlendEquationSeparate", "uu"],
        ["DRAW_BUFFERS", 138, null, "u:count"],
        ["STENCIL_OP_SEPARATE", 139, "StencilOpSeparate", "uuuu"],
        ["STENCIL_FUNC_SEPARATE", 140, "StencilFuncSeparate", "uuiu"],
        ["STENCIL_MASK_SEPARATE", 141, "StencilMaskSeparate", "uu"],
        ["CREATE_PROGRAM", 142, "CreateProgramMapped", "u"],
        ["CREATE_SHADER", 143, "CreateShaderMapped", "uu"],
        ["DELETE_PROGRAM", 144, "DeleteProgramMapped", "u"],
        ["DELETE_SHADER", 145, "DeleteShaderMapped", "u"],
        ["ATTACH_SHADER", 146, "AttachShaderMapped", "uu"],
        ["DETACH_SHADER", 147, "DetachShaderMapped", "uu"],
        ["SHADER_SOURCE", 148, null, "u:shader u:length"],
        ["COMPILE_SHADER", 149, "CompileShaderMapped", "u"],
        ["LINK_PROGRAM", 150, "LinkProgramMapped", "u"],
        ["USE_PROGRAM", 151, "UseProgramMapped", "u"],
        ["VALIDATE_PROGRAM", 152, "ValidateProgramMapped", "u"],
        ["BIND_ATTRIB_LOCATION", 153, null, "u:program i:location u:name_length"],
        // Retired location-map records: their obsolete payload is ignored.
        ["MAP_UNIFORM_LOCATION", 154, "MapUniformLocation", ""],
        ["MAP_ATTRIB_LOCATION", 155, "MapAttribLocation", ""],
        ["UNIFORM_FV", 156, null, "i:location i:components i:count"],
        ["UNIFORM_IV", 157, null, "i:location i:components i:count"],
        ["UNIFORM_MATRIX_FV", 158, null, "i:location i:dimension i:count u:transpose"],
        ["VERTEX_ATTRIB4F", 159, "VertexAttrib4fMapped", "uffff"],
        ["ENABLE_VERTEX_ATTRIB_ARRAY", 160, "EnableVertexAttribArrayMapped", "u"],
        ["DISABLE_VERTEX_ATTRIB_ARRAY", 161, "DisableVertexAttribArrayMapped", "u"],
        ["DRAW_ARRAYS_GL2", 162, null, "u:mode i:count"],
        ["DRAW_ELEMENTS_GL2", 163, null, "u:mode i:count u:type u:index_data_size"],
        ["UNIFORM_MATRIX_RECT_FV", 164, null, "i:location i:columns i:rows i:count u:transpose"],
        ["GEN_FRAMEBUFFERS", 165, null, "u:count"],
        ["DELETE_FRAMEBUFFERS", 166, null, "u:count"],
        ["BIND_FRAMEBUFFER", 167, "BindFramebufferMapped", "uu"],
        ["FRAMEBUFFER_TEXTURE", 168, "FramebufferTextureMapped", "uuuuii"],
        ["FRAMEBUFFER_RENDERBUFFER", 169, "FramebufferRenderbufferMapped", "uuuu"],
        ["GEN_RENDERBUFFERS", 170, null, "u:count"],
        ["DELETE_RENDERBUFFERS", 171, null, "u:count"],
        ["BIND_RENDERBUFFER", 172, "BindRenderbufferMapped", "uu"],
        ["RENDERBUFFER_STORAGE", 173, "RenderbufferStorageMapped", "uuii"],
        ["QUERY_OBJECT_IV", 174, null, "u:object_kind u:name u:pname u:status i:value u:reserved"],
        ["QUERY_OBJECT_LOG", 175, null, "u:object_kind u:name u:buf_size u:status u:length u:data_size"],
        ["CHECK_FRAMEBUFFER_STATUS", 176, null, "u:target u:status u:result u:reserved"],
        ["QUERY_ACTIVE", 177, null, "u:active_kind u:program u:index u:buf_size u:status u:length i:size u:type u:data_size u:reserved"],
        ["GEN_QUERIES", 178, null, "u:count"],
        ["DELETE_QUERIES", 179, null, "u:count"],
        ["BEGIN_QUERY", 180, "BeginQueryMapped", "uu"],
        ["END_QUERY", 181, "EndQueryMapped", "u"],
        ["GEN_PROGRAMS_ARB", 182, null, "u:count"],
        ["DELETE_PROGRAMS_ARB", 183, null, "u:count"],
        ["BIND_PROGRAM_ARB", 184, "BindProgramARBMapped", "uu"],
        ["PROGRAM_STRING_ARB", 185, null, "u:target u:format i:length u:reserved"],
        ["PROGRAM_PARAMETER_FV_ARB", 186, null, "u:parameter_kind u:target u:index u:count"],
        ["PROGRAM_PARAMETER_DV_ARB", 187, null, "u:parameter_kind u:target u:index u:reserved d:values[4]"],
        ["QUERY_PROGRAM_IV_ARB", 188, null, "u:target u:pname u:status i:value"],
        ["QUERY_PROGRAM_PARAMETER_FV_ARB", 189, null, "u:parameter_kind u:target u:index u:status u:data_size u:reserved"],
        ["QUERY_PROGRAM_PARAMETER_DV_ARB", 190, null, "u:parameter_kind u:target u:index u:status u:data_size u:reserved"],
        ["QUERY_PROGRAM_STRING_ARB", 191, null, "u:target u:pname u:status u:length u:data_size u:reserved"],
        ["QUERY_GL_STRING", 192, null, "u:name u:status u:length u:data_size"],
        ["QUERY_INTEGER", 193, null, "u:pname u:status i:value u:reserved"],
        ["GEN_BUFFERS", 194, null, "u:count"],
        ["DELETE_BUFFERS", 195, null, "u:count"],
        ["BIND_BUFFER", 196, "BindBufferMapped", "uu"],
        ["BUFFER_DATA", 197, null, "u:target i:size u:usage u:data_size"],
        ["BUFFER_SUB_DATA", 198, null, "u:target i:offset i:size u:data_size"],
        ["VERTEX_POINTER_VBO", 199, null, "i:size u:type i:stride u:offset"],
        ["COLOR_POINTER_VBO", 200, null, "i:size u:type i:stride u:offset"],
        ["TEX_COORD_POINTER_VBO", 201, null, "i:size u:type i:stride u:offset"],
        ["NORMAL_POINTER_VBO", 202, null, "i:size u:type i:stride u:offset"],
        ["SECONDARY_COLOR_POINTER_VBO", 203, null, "i:size u:type i:stride u:offset"],
        ["FOG_COORD_POINTER_VBO", 204, null, "i:size u:type i:stride u:offset"],
        ["VERTEX_ATTRIB_POINTER_VBO", 205, null, "u:index i:size u:type u:normalized i:stride u:offset"],
        ["DRAW_ARRAYS_DIRECT", 206, "DrawArraysDirect", "uii"],
        ["DRAW_ELEMENTS_DIRECT", 207, null, "u:mode u:start u:end i:count u:type u:offset"],
        ["DRAW_RANGE_ELEMENTS_DIRECT", 208, null, "u:mode u:start u:end i:count u:type u:offset"],
        ["MULTI_DRAW_ARRAYS_DIRECT", 209, null, "u:mode i:primcount"],
        ["MULTI_DRAW_ELEMENTS_DIRECT", 210, null, "u:mode u:type i:primcount"],
        ["QUERY_ERROR", 211, null, "u:status u:value u:reserved0 u:reserved1"],
        ["QUERY_LOCATION", 212, null, "u:location_kind u:program i:guest_location u:status i:result u:value_type i:value_count u:name_length"],
        ["QUERY_UNIFORM", 213, null, "u:program i:location u:value_kind u:status i:value_count u:data_size u:reserved0 u:reserved1"],
        ["INVALIDATE_PROGRAM_LOCATIONS", 214, "InvalidateProgramLocations", "u"],
        ["COPY_TEX_SUB_IMAGE_3D", 215, "CopyTexSubImage3D", "uiiiiiiii"],
        ["QUERY_OBJECT_BATCH", 216, null, "u:count u:object_kind u:status u:reserved"],
        ["BLIT_FRAMEBUFFER", 217, "BlitFramebuffer", "iiiiiiiiuu"],
        ["EDGE_FLAG", 218, "EdgeFlag", "u"],
        // CPU-side state queries. The data are already converted to the
        // requested GL type, stored as doubles (including exact 32-bit ints).
        ["QUERY_STATE", 219, null, "u:pname u:value_kind u:status u:count u:data_size u:error"],
        ["QUERY_PARAMETER", 220, null, "u:pname u:value_kind u:status u:count u:data_size u:error u:query_kind u:target u:index u:reserved"],
        ["PRIORITIZE_TEXTURES", 221, null, "i:count"], // then count pairs of u32 name, f32 priority
        // A CPU link failure preserves the previously installed executable.
        ["PROGRAM_LINK_FAILED", 222, "ProgramLinkFailed", "u"],
        ["QUERY_BUFFER_DATA", 223, null, "u:target i:offset i:size i:range_size u:status u:data_size u:error u:buffer"],
        ["QUERY_BUFFER_MAP", 224, null, "u:target u:access u:pointer u:status u:error u:reserved"],
        ["QUERY_BUFFER_UNMAP", 225, null, "u:target u:status u:result u:error"],
        ["INDEX_POINTER_VBO", 226, null, "i:size u:type i:stride u:offset"],
        ["EDGE_FLAG_POINTER_VBO", 227, null, "i:size u:type i:stride u:offset"],
        ["QUERY_NAMES", 228, null, "u:object_kind i:count u:status u:error u:data_size u:reserved"],
        ["FRAMEBUFFER_TEXTURE_1D", 229, "FramebufferTexture1DMapped", "uuuuii"],
        ["FRAMEBUFFER_TEXTURE_2D", 230, "FramebufferTexture2DMapped", "uuuuii"],
        ["FRAMEBUFFER_TEXTURE_3D", 231, "FramebufferTexture3DMapped", "uuuuii"],
        ["BIND_FRAMEBUFFER_EXT", 232, "BindFramebufferEXT", "uu"],
        // Metadata reserves a CPU-validated image read; bounded chunks keep
        // large textures independent of the guest DMA arena's size.
        ["QUERY_TEXTURE_IMAGE", 233, null, "u:target i:level u:format u:type u:compressed u:mode u:pointer u:offset u:data_size u:span u:ticket u:status u:error u:width u:height u:depth u:context_id"],
        ["QUERY_POLYGON_STIPPLE", 234, null, "u:mode u:pointer u:status u:error u:row_stride u:start u:row_bytes u:bit_offset u:lsb_first u:pbo u:span u:data_size"],
        ["POLYGON_STIPPLE_BITMAP", 235, null, "u:data_size u:status u:error u:reserved"],
        ["LIST_COMMAND", 236, null, "u:operation u:name u:value"],
        ["QUERY_LIST", 237, null, "u:operation u:name i:range u:status u:error u:result"],
        ["LIST_CALLS", 238, null, "u:count u:data_size"],
        ["QUERY_RENDER_MODE", 239, null, "u:operation i:size u:type u:pointer u:offset u:data_size u:status u:error i:result u:ticket u:total"],
        ["NAME_COMMAND", 240, null, "u:operation u:name f:value"],
        ["RESOLVED_RASTER_STATE", 241, null, "u:valid f:position[4] f:color[4]"],
        ["CAPTURE_VERTICES", 242, null, "u:job u:first u:count u:total"],
        ["CAPTURE_RASTER_VERTEX", 243, null, "u:job u:reserved"],
        ["RESOLVED_RASTER_ALL", 244, null, "u:valid f:position[4] f:color[4] f:secondary[4] f:distance f:fog f:coords[32]"],
        ["MAP_EVALUATOR", 245, null, "u:dimensions u:target i:uorder i:vorder i:ustride i:vstride u:data_size u:reserved d:domain[4]"],
        ["EVALUATOR_GRID", 246, null, "u:dimensions i:un i:vn u:reserved d:domain[4]"],
        ["EVAL_COORD", 247, null, "u:dimensions u:operation d:values[2]"],
        ["EVAL_MESH", 248, null, "u:dimensions u:mode i:indices[4]"],
        ["QUERY_EVALUATOR", 249, null, "u:pname u:value_kind u:status u:count u:data_size u:error u:target u:reserved"],
        ["RESOLVED_EVAL_VERTEX", 250, null, "f:position[4] f:color[4] f:normal[4] f:texcoord[4]"],
        ["RESOLVED_PROGRAM_INTERFACE", 251, null, "u:program u:object_id u:link_generation u:entry_count u:data_size u:initializer_count"],
        ["INDEX_STATE", 252, null, "u:operation u:mask d:value"],
        ["ACTIVE_STENCIL_FACE", 253, "ActiveStencilFaceEXT", "u"],
        ["PROGRAM_LINK_VALIDATION", 254, null, "u:operation u:job u:program u:object_id u:link_generation u:data_size"],
        ["UPLOAD_CHUNK", 255, null, "u:token u:opcode u:total_size u:offset u:data_size"],
        ["IMMEDIATE_VERTEX_BLOCK", 256, null, "u:run_count u:data_size"],
        ["QUERY_ARRAY_LOCK", 257, null, "u:operation i:first i:count u:status u:error"],
    ];

    // Composite records' optional headers and repeated elements. The common
    // prefixes above remain readable in pre-multitexture snapshot journals.
    const COMPOSITES = [
        ["PACKED_DRAW_ARRAYS_MT", "u:mode i:count u:magic u:tex_unit_count u:client_active_texture"],
        ["PACKED_DRAW_ARRAYS_GL2_MT", "u:mode i:count u:magic u:tex_unit_count u:client_active_texture u:generic_attrib_count"],
        ["PACKED_DRAW_ELEMENTS_MT", "u:mode i:count u:type u:index_data_size u:magic u:tex_unit_count u:client_active_texture"],
        ["PACKED_DRAW_ELEMENTS_GL2_MT", "u:mode i:count u:type u:index_data_size u:magic u:tex_unit_count u:client_active_texture u:generic_attrib_count"],
        ["CLIENT_ARRAY_BLOCK", "u:enabled i:size u:type i:stride u:data_size"],
        ["GENERIC_ATTRIB_BLOCK", "u:index u:normalized u:enabled i:size u:type i:stride u:data_size"],
        ["IMMEDIATE_VERTEX_RUN", "u:attribute_bytes u:vertex_count"],
        ["MULTI_DRAW_ARRAYS_ENTRY", "i:first i:count"],
        ["MULTI_DRAW_ELEMENTS_ENTRY", "i:count u:offset"],
        ["QUERY_OBJECT_BATCH_ENTRY", "u:name u:available u:result"],
        ["PROGRAM_INTERFACE_ENTRY", "u:location_kind i:location u:value_type i:value_count u:name_length"],
    ];

    // Control records, outside the GLFN numbering: [name, code, layout]
    const CONTROL = [
        // the context the commands that follow are for, and its window; an
        // older guest's payload may stop short (the fields after read as 0)
        // (format: its window's pixel format, V86GL_FORMAT_*)
        ["MAKE_CURRENT", 0xFFF0, "u:hwnd i:x i:y u:width u:height u:context_id u:share_group u:format u:caps_profile"],
        ["RELEASE_CURRENT", 0xFFF1],
        // one context; with no payload, every context (an older guest's
        // process teardown)
        ["DESTROY_CONTEXT", 0xFFF2, "u:context_id"],
        // What the guest's window manager says about the drawing window: shown,
        // minimised, covered by other windows entirely or in part (the parts
        // that show follow). See windowState() in gl_executor.js.
        ["WINDOW_STATE", 0xFFF3],
        // Present a window independently of the calling thread's current RC.
        // status is an asynchronous host response after interval refresh ticks.
        ["SWAP_WINDOW", 0xFFF4, "u:hwnd u:interval u:status"],
    ];
    const CTRL = Object.fromEntries(CONTROL.map(([name, code]) => [name, code]));

    // The constants of the protocol, as the guest header defines them
    // ([name, value, comment]; a string value is written as it is)
    const DEFINES = [
        ["V86GL_CTRL_MAKE_CURRENT", CTRL.MAKE_CURRENT],
        ["V86GL_CTRL_RELEASE_CURRENT", CTRL.RELEASE_CURRENT],
        ["V86GL_CTRL_DESTROY_CONTEXT", CTRL.DESTROY_CONTEXT],
        ["V86GL_CTRL_WINDOW_STATE", CTRL.WINDOW_STATE, "the drawing window as the guest's window manager sees it"],
        ["V86GL_CTRL_SWAP_WINDOW", CTRL.SWAP_WINDOW],
        ["V86GL_MAX_SWAP_INTERVAL", 4],
        ["V86GL_WINDOW_IS_WINDOW", "(1u << 0)", "its flags: D9WGWindowState's (d3d9proxy/d3d9_protocol.h)"],
        ["V86GL_WINDOW_VISIBLE", "(1u << 1)"],
        ["V86GL_WINDOW_ICONIC", "(1u << 2)"],
        ["V86GL_WINDOW_OCCLUDED", "(1u << 6)"],
        ["V86GL_WINDOW_REGION", "(1u << 7)"],
        ["V86GL_FORMAT_SINGLE_BUFFER", "(1u << 0)", "MAKE_CURRENT's format: no back buffer"],
        ["V86GL_FORMAT_SAMPLES_SHIFT", 8, "and the samples per pixel (0 or 1: one) in bits 8-15"],
        ["V86GL_FORMAT_SAMPLES_MASK", 0xFF00],
        ["V86GL_CAPS_PROFILE_UNRESOLVED", 0, "old guests retain the saved capability strings"],
        ["V86GL_CAPS_PROFILE_WINED3D_GL15", 1],
        ["V86GL_CAPS_PROFILE_WINED3D_ARB", 2],
        ["V86GL_CAPS_PROFILE_GL21_NO_FBO", 3],
        ["V86GL_CAPS_PROFILE_GL21_FBO_FFP", 4],
        ["V86GL_CAPS_PROFILE_GL21", 5],
        ["V86GL_EXTENDED_RECORD_SIZE", 0xFFFF, "a record size of this is followed by a u32 size"],
        ["V86GL_MAX_UPLOAD_BYTES", 256 * 1024 * 1024, "bounded CPU assembly of an upload larger than the arena"],
        ["V86GL_IMMEDIATE_BLOCK_BYTES", 64 * 1024],
        // CPU validation replaces only the opcode of a rejected GL record.
        // Keep its size/payload so every inline response retains its address.
        // 0x8eoo: e = GL error - 0x500 (0..6), oo = original opcode (1..255).
        ["V86GL_REJECTED_RECORD_BIT", 0x8000],
        ["V86GL_REJECTED_RECORD_MASK", 0xF800],
        ["V86GL_REJECTED_ERROR_SHIFT", 8],
        ["V86GL_REJECTED_ERROR_MASK", 0x0700],
        ["V86GL_REJECTED_ERROR_BASE", 0x0500],
        ["V86GL_REJECTED_OPCODE_MASK", 0x00FF],
        // Preserve the original eight-bit tags in old journals; new opcodes
        // use 0x9eoo with a nine-bit source opcode and error bits 9-11.
        ["V86GL_MAX_OPCODE", 0x01FF],
        ["V86GL_REJECTED_EXTENDED_BIT", 0x9000],
        ["V86GL_REJECTED_EXTENDED_MASK", 0xF000],
        ["V86GL_REJECTED_EXTENDED_ERROR_SHIFT", 9],
        ["V86GL_REJECTED_EXTENDED_ERROR_MASK", 0x0E00],
        ["V86GL_RESPONSE_REGION_BYTES", 4 * 1024 * 1024],
        ["V86GL_HEARTBEAT_BYTES", 16, "reserved at the end of the arena, outside the GL command stream"],
        ["V86GL_HEARTBEAT_OFFSET", 4 * 1024 * 1024 - 16],
        ["V86GL_ASYNC_HOST_TIMEOUT_MS", 5000],
        ["V86GL_ASYNC_SILENCE_MS", 6000, "longer than the host's own readback deadline"],
        ["V86GL_READ_PIXELS_HEADER_SIZE", 32],
        ["V86GL_READ_PIXELS_STATUS_PENDING", 0],
        ["V86GL_READ_PIXELS_STATUS_OK", 1],
        ["V86GL_READ_PIXELS_STATUS_FAILED", 2],
        ["V86GL_SYNC_QUERY_STATUS_PENDING", 0],
        ["V86GL_SYNC_QUERY_STATUS_OK", 1],
        ["V86GL_SYNC_QUERY_STATUS_FAILED", 2],
        ["V86GL_STATE_QUERY_UNSUPPORTED", 3, "state not migrated to the CPU shadow yet"],
        ["V86GL_STIPPLE_SOURCE_PBO_BIT", 1],
        ["V86GL_STIPPLE_CAPTURE_FAILURE_BIT", 2, "capture-only setter query stored a deferred list error"],
        ["V86GL_STATE_INTEGER", 1],
        ["V86GL_STATE_FLOAT", 2],
        ["V86GL_STATE_DOUBLE", 3],
        ["V86GL_STATE_BOOLEAN", 4],
        ["V86GL_STATE_IS_ENABLED", 5, "glIsEnabled: capability validation and one boolean answer"],
        ["V86GL_STATE_UNSIGNED_SHORT", 6, "pixel map unsigned short output"],
        ["V86GL_STATE_UNSIGNED_INT", 7, "pixel map unsigned integer output"],
        ["V86GL_MAX_TYPED_QUERY_VALUES", 256],
        ["V86GL_MAX_EVAL_ORDER", 8],
        ["V86GL_MAX_EVALUATOR_VALUES", 256],
        ["V86GL_PARAMETER_LIGHT", 1],
        ["V86GL_PARAMETER_MATERIAL", 2],
        ["V86GL_PARAMETER_TEX_ENV", 3],
        ["V86GL_PARAMETER_TEX_GEN", 4],
        ["V86GL_PARAMETER_CLIP_PLANE", 5],
        ["V86GL_PARAMETER_PIXEL_MAP", 6],
        ["V86GL_PARAMETER_IS_OBJECT", 7],
        ["V86GL_PARAMETER_FRAMEBUFFER_ATTACHMENT", 8],
        ["V86GL_PARAMETER_RENDERBUFFER", 9],
        ["V86GL_PARAMETER_TEXTURE", 10],
        ["V86GL_PARAMETER_TEXTURE_LEVEL", 11],
        ["V86GL_PARAMETER_BUFFER", 12],
        ["V86GL_PARAMETER_BUFFER_POINTER", 13],
        ["V86GL_PARAMETER_VERTEX_ATTRIB", 14],
        ["V86GL_PARAMETER_ARRAY_POINTER", 15],
        ["V86GL_PARAMETER_QUERY_TARGET", 16],
        ["V86GL_PARAMETER_ATTACHED_SHADERS", 17],
        ["V86GL_PARAMETER_TEXTURE_RESIDENCY", 18],
        ["V86GL_TEXT_SHADER_SOURCE", 0x10001],
        ["V86GL_OBJECT_KIND_SHADER", 1],
        ["V86GL_OBJECT_KIND_PROGRAM", 2],
        ["V86GL_OBJECT_KIND_QUERY", 3],
        ["V86GL_OBJECT_KIND_TEXTURE", 4],
        ["V86GL_OBJECT_KIND_RENDERBUFFER", 5],
        ["V86GL_OBJECT_KIND_FRAMEBUFFER", 6],
        ["V86GL_OBJECT_KIND_BUFFER", 7],
        ["V86GL_OBJECT_KIND_ARB_PROGRAM", 8],
        ["V86GL_QUERY_VISIBLE_SAMPLES", 0x7FFFFFFF],
        ["V86GL_ACTIVE_KIND_UNIFORM", 1],
        ["V86GL_ACTIVE_KIND_ATTRIB", 2],
        ["V86GL_LOCATION_KIND_UNIFORM", 1],
        ["V86GL_LOCATION_KIND_ATTRIB", 2],
        ["V86GL_PROGRAM_PARAMETER_ENV", 1],
        ["V86GL_PROGRAM_PARAMETER_LOCAL", 2],
        ["V86GL_QUERY_HOST_CAPABILITIES", 0x76380001,
            "QUERY_INTEGER's private host-capability bitset: optional extensions are only advertised when the " +
            "renderer backs them"],
        ["V86GL_HOST_CAP_WEBGL2", 0x1],
        ["V86GL_HOST_CAP_COLOR_BUFFER_FLOAT", 0x2],
        ["V86GL_HOST_CAP_TEXTURE_FLOAT_LINEAR", 0x4],
        ["V86GL_HOST_CAP_ANISOTROPY", 0x8],
        ["CLIENT_ARRAY_MT_MAGIC", 0x544D4143, "'CAMT' little-endian"],
        ["CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT", 0x80000000],
        ["CLIENT_ARRAY_MT_FOG_COORD_BIT", 0x40000000],
    ];

    // DEFINES by name, as numbers
    const C = {};
    for (const [name, value] of DEFINES) {
        const shift = typeof value === "string" && value.match(/^\(1u << (\d+)\)$/);
        if (typeof value !== "number" && !shift) throw new Error("GL protocol: cannot evaluate " + name);
        C[name] = typeof value === "number" ? value : (1 << +shift[1]) >>> 0;
    }

    const BYTES = { i: 4, u: 4, f: 4, d: 8 };
    const C_TYPES = { i: "int32_t", u: "uint32_t", f: "float", d: "double" };

    const GLFN = {};
    const NAME_BY_OPCODE = [];
    // flat records: name -> [GL entry point, argument codes]
    const SIGNATURES = {};
    // fixed headers: name -> { size, fields: { field: { type, offset, count, bytes } } }
    const RECORDS = {};
    // name -> { field: offset }
    const F = {};
    function describe(name, layout) {
        if (RECORDS[name]) throw new Error("GL protocol: " + name + " is described twice");
        const fields = {};
        let offset = 0;
        for (const word of layout.split(" ")) {
            const [type, spec] = word.split(":");
            const [, field, count] = spec.match(/^(\w+)(?:\[(\d+)\])?$/);
            const n = count ? +count : 1;
            // natural alignment, as a C compiler lays the struct out
            offset = offset + BYTES[type] - 1 & ~(BYTES[type] - 1);
            fields[field] = { type, offset, count: n, bytes: BYTES[type] * n };
            offset += BYTES[type] * n;
        }
        RECORDS[name] = { size: offset, fields };
        F[name] = Object.fromEntries(Object.entries(fields).map(([field, f]) => [field, f.offset]));
    }
    for (const [name, opcode, glName, layout] of OPCODES) {
        if (GLFN[name] !== undefined || NAME_BY_OPCODE[opcode] !== undefined)
            throw new Error("GL protocol: " + name + " or opcode " + opcode + " is listed twice");
        GLFN[name] = opcode;
        NAME_BY_OPCODE[opcode] = name;
        if (glName) SIGNATURES[name] = [glName, layout];
        else if (layout) describe(name, layout);
    }
    for (const [name, , layout] of CONTROL) if (layout) describe(name, layout);
    for (const [name, layout] of COMPOSITES) describe(name, layout);

    function payloadBytes(types) {
        let n = 0;
        for (const t of types) n += BYTES[t];
        return n;
    }

    /* Decodes one flat argument list into `out`, returning the count. The
     * caller supplies the array so a hot draw path does not allocate. */
    function decodeArgs(types, view, offset, length, out) {
        let cursor = offset;
        const end = offset + length;
        for (let i = 0; i < types.length; ++i) {
            const t = types[i];
            const size = BYTES[t];
            if (cursor + size > end) return -1;
            switch (t) {
            case "i": out[i] = view.getInt32(cursor, true); break;
            case "u": out[i] = view.getUint32(cursor, true); break;
            case "f": out[i] = view.getFloat32(cursor, true); break;
            default: out[i] = view.getFloat64(cursor, true); break;
            }
            cursor += size;
        }
        return types.length;
    }

    const READ = {
        i: (view, at) => view.getInt32(at, true), u: (view, at) => view.getUint32(at, true),
        f: (view, at) => view.getFloat32(at, true), d: (view, at) => view.getFloat64(at, true),
    };
    const WRITE = {
        i: (view, at, v) => view.setInt32(at, v, true), u: (view, at, v) => view.setUint32(at, v >>> 0, true),
        f: (view, at, v) => view.setFloat32(at, v, true), d: (view, at, v) => view.setFloat64(at, v, true),
    };

    // A fixed header as an object (arrays as arrays)
    function decodeRecord(name, view, offset) {
        const out = {};
        for (const [field, f] of Object.entries(RECORDS[name].fields)) {
            const one = i => READ[f.type](view, offset + f.offset + i * BYTES[f.type]);
            out[field] = f.count === 1 ? one(0) : Array.from({ length: f.count }, (_, i) => one(i));
        }
        return out;
    }

    // A fixed header from an object (absent fields 0), for tests and encoders
    function encodeRecord(name, values) {
        const bytes = new Uint8Array(RECORDS[name].size);
        const view = new DataView(bytes.buffer);
        for (const [field, f] of Object.entries(RECORDS[name].fields)) {
            const value = values[field];
            if (value === undefined) continue;
            (f.count === 1 ? [value] : value).forEach((v, i) => WRITE[f.type](view, f.offset + i * BYTES[f.type], v));
        }
        return bytes;
    }

    // One flat record's payload from its arguments
    function encodeArgs(name, args) {
        const types = SIGNATURES[name][1];
        const bytes = new Uint8Array(payloadBytes(types));
        const view = new DataView(bytes.buffer);
        let at = 0;
        [...types].forEach((t, i) => { WRITE[t](view, at, args[i]); at += BYTES[t]; });
        return bytes;
    }

    function encodeProgramInterface(fields, entries, initializers = []) {
        const text = new TextEncoder(), rows = entries.map(entry => ({ entry, name: text.encode(entry.name) })), header = RECORDS.RESOLVED_PROGRAM_INTERFACE.size,
            dataSize = rows.reduce((sum, row) => sum + RECORDS.PROGRAM_INTERFACE_ENTRY.size + Math.ceil(row.name.length / 4) * 4, 0) +
                initializers.reduce((sum, row) => sum + 8 + row.values.length * 8, 0), bytes = new Uint8Array(header + dataSize);
        bytes.set(encodeRecord("RESOLVED_PROGRAM_INTERFACE", { ...fields, entry_count: rows.length, data_size: dataSize, initializer_count: initializers.length }));
        let at = header;
        for (const { entry, name } of rows) {
            bytes.set(encodeRecord("PROGRAM_INTERFACE_ENTRY", { ...entry, name_length: name.length }), at); at += RECORDS.PROGRAM_INTERFACE_ENTRY.size;
            bytes.set(name, at); at += Math.ceil(name.length / 4) * 4;
        }
        const view = new DataView(bytes.buffer);
        for (const row of initializers) {
            view.setInt32(at, row.location, true);view.setUint32(at + 4, row.values.length, true);at += 8;
            for (const value of row.values) {view.setFloat64(at, value, true);at += 8;}
        }
        return bytes;
    }

    function decodeProgramInterface(view, offset, size) {
        const header = RECORDS.RESOLVED_PROGRAM_INTERFACE.size, rowSize = RECORDS.PROGRAM_INTERFACE_ENTRY.size;
        if (size < header) throw new Error("short GL program interface");
        const r = decodeRecord("RESOLVED_PROGRAM_INTERFACE", view, offset), entries = [], initializers = [], text = new TextDecoder("utf-8", { fatal: true });
        if (!r.object_id || !r.link_generation || r.entry_count > 32768 || r.initializer_count > 32768 || r.data_size !== size - header) throw new Error("invalid GL program interface header");
        let at = offset + header;
        for (let i = 0; i < r.entry_count; ++i) {
            if (at + rowSize > offset + size) throw new Error("short GL program interface entry");
            const entry = decodeRecord("PROGRAM_INTERFACE_ENTRY", view, at); at += rowSize;
            if (![C.V86GL_LOCATION_KIND_UNIFORM, C.V86GL_LOCATION_KIND_ATTRIB].includes(entry.location_kind) || entry.location < 0 || entry.value_count < 1 ||
                entry.name_length > offset + size - at) throw new Error("invalid GL program interface entry");
            entry.name = text.decode(new Uint8Array(view.buffer, view.byteOffset + at, entry.name_length));
            at += Math.ceil(entry.name_length / 4) * 4; entries.push(entry);
        }
        for (let i = 0; i < r.initializer_count; ++i) {
            if (at + 8 > offset + size) throw new Error("short GL uniform initializer");
            const location = view.getInt32(at, true), count = view.getUint32(at + 4, true);at += 8;
            if (location < 0 || count < 1 || count > 16 || at + count * 8 > offset + size) throw new Error("invalid GL uniform initializer");
            const values = [];
            for (let k = 0; k < count; ++k) {values.push(view.getFloat64(at, true));at += 8;}
            initializers.push({location, values});
        }
        if (at !== offset + size) throw new Error("invalid GL program interface span");
        return { ...r, entries, initializers };
    }

    function isRejected(opcode) {
        return (opcode & C.V86GL_REJECTED_RECORD_MASK) === C.V86GL_REJECTED_RECORD_BIT ||
            (opcode & C.V86GL_REJECTED_EXTENDED_MASK) === C.V86GL_REJECTED_EXTENDED_BIT;
    }
    function rejectionError(opcode) {
        if ((opcode & C.V86GL_REJECTED_EXTENDED_MASK) === C.V86GL_REJECTED_EXTENDED_BIT)
            return C.V86GL_REJECTED_ERROR_BASE + ((opcode & C.V86GL_REJECTED_EXTENDED_ERROR_MASK) >>> C.V86GL_REJECTED_EXTENDED_ERROR_SHIFT);
        return C.V86GL_REJECTED_ERROR_BASE + ((opcode & C.V86GL_REJECTED_ERROR_MASK) >>> C.V86GL_REJECTED_ERROR_SHIFT);
    }
    function rejectedSourceOpcode(opcode) {
        return opcode & ((opcode & C.V86GL_REJECTED_EXTENDED_MASK) === C.V86GL_REJECTED_EXTENDED_BIT ? C.V86GL_MAX_OPCODE : C.V86GL_REJECTED_OPCODE_MASK);
    }
    function rejectedOpcode(opcode, error) {
        if (!Number.isInteger(opcode) || opcode < 1 || opcode > C.V86GL_MAX_OPCODE ||
                !Number.isInteger(error) || error < 0x500 || error > 0x506)
            throw new RangeError("GL rejected-record opcode or error is out of range");
        if (opcode > C.V86GL_REJECTED_OPCODE_MASK) return C.V86GL_REJECTED_EXTENDED_BIT |
            (error - C.V86GL_REJECTED_ERROR_BASE) << C.V86GL_REJECTED_EXTENDED_ERROR_SHIFT | opcode;
        return C.V86GL_REJECTED_RECORD_BIT | (error - C.V86GL_REJECTED_ERROR_BASE) << C.V86GL_REJECTED_ERROR_SHIFT | opcode;
    }

    const api = { OPCODES, CONTROL, CTRL, DEFINES, C, BYTES, C_TYPES, GLFN, NAME_BY_OPCODE, SIGNATURES, RECORDS, F,
        payloadBytes, decodeArgs, decodeRecord, encodeRecord, encodeArgs, encodeProgramInterface, decodeProgramInterface, isRejected, rejectionError, rejectedOpcode, rejectedSourceOpcode };
    global.GLProtocol = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The GL enum values the guest forwards, generated from
// openglproxy/opengl32_proxy.c's #defines; the wire opcodes and control
// records come from gl_protocol.js.
//
// See docs/opengl-webgpu-plan.md C1.

(function(global) {
    "use strict";

    // The opcodes and control records are the protocol's (gl_protocol.js)
    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const GLFN = protocol.GLFN;

    const CTRL = protocol.CTRL;

    const GL = {
        FALSE: 0,
        TRUE: 1,
        NO_ERROR: 0,
        ZERO: 0,
        ONE: 1,
        NONE: 0,
        VENDOR: 0x1F00,
        RENDERER: 0x1F01,
        VERSION: 0x1F02,
        EXTENSIONS: 0x1F03,
        ACCUM_BUFFER_BIT: 0x00000200,
        COLOR_BUFFER_BIT: 0x00004000,
        DEPTH_BUFFER_BIT: 0x00000100,
        TRANSFORM_BIT: 0x00001000,
        STENCIL_BUFFER_BIT: 0x00000400,
        NEVER: 0x0200,
        LESS: 0x0201,
        EQUAL: 0x0202,
        LEQUAL: 0x0203,
        GREATER: 0x0204,
        NOTEQUAL: 0x0205,
        GEQUAL: 0x0206,
        ALWAYS: 0x0207,
        POINTS: 0x0000,
        LINES: 0x0001,
        LINE_LOOP: 0x0002,
        LINE_STRIP: 0x0003,
        TRIANGLES: 0x0004,
        TRIANGLE_STRIP: 0x0005,
        TRIANGLE_FAN: 0x0006,
        QUADS: 0x0007,
        QUAD_STRIP: 0x0008,
        POLYGON: 0x0009,
        FRONT_LEFT: 0x0400,
        FRONT: 0x0404,
        LEFT: 0x0406,
        BACK: 0x0405,
        FRONT_AND_BACK: 0x0408,
        CW: 0x0900,
        CCW: 0x0901,
        CURRENT_COLOR: 0x0B00,
        CURRENT_INDEX: 0x0B01,
        CURRENT_NORMAL: 0x0B02,
        CURRENT_TEXTURE_COORDS: 0x0B03,
        POINT_SMOOTH: 0x0B10,
        POINT_SIZE: 0x0B11,
        POINT_SIZE_RANGE: 0x0B12,
        POINT_SIZE_GRANULARITY: 0x0B13,
        LINE_WIDTH_GRANULARITY: 0x0B23,
        ALIASED_POINT_SIZE_RANGE: 0x846D,
        AUX_BUFFERS: 0x0C00,
        INDEX_MODE: 0x0C30,
        INDEX_BITS: 0x0D51,
        ACCUM_RED_BITS: 0x0D58, ACCUM_GREEN_BITS: 0x0D59, ACCUM_BLUE_BITS: 0x0D5A, ACCUM_ALPHA_BITS: 0x0D5B,
        TRANSPOSE_MODELVIEW_MATRIX: 0x84E3, TRANSPOSE_PROJECTION_MATRIX: 0x84E4,
        TRANSPOSE_TEXTURE_MATRIX: 0x84E5, TRANSPOSE_COLOR_MATRIX: 0x84E6,
        LINE_SMOOTH: 0x0B20,
        LINE_WIDTH: 0x0B21,
        LINE_WIDTH_RANGE: 0x0B22,
        LINE_STIPPLE: 0x0B24,
        LINE_STIPPLE_PATTERN: 0x0B25,
        LINE_STIPPLE_REPEAT: 0x0B26,
        LIST_BASE: 0x0B32,
        POLYGON_MODE: 0x0B40,
        CULL_FACE: 0x0B44,
        CULL_FACE_MODE: 0x0B45,
        FRONT_FACE: 0x0B46,
        LIGHTING: 0x0B50,
        SHADE_MODEL: 0x0B54,
        FOG: 0x0B60,
        FOG_INDEX: 0x0B61,
        FOG_DENSITY: 0x0B62,
        FOG_START: 0x0B63,
        FOG_END: 0x0B64,
        FOG_MODE: 0x0B65,
        FOG_COLOR: 0x0B66,
        DEPTH_RANGE: 0x0B70,
        DEPTH_TEST: 0x0B71,
        DEPTH_WRITEMASK: 0x0B72,
        DEPTH_CLEAR_VALUE: 0x0B73,
        DEPTH_FUNC: 0x0B74,
        ACCUM_CLEAR_VALUE: 0x0B80,
        STENCIL_TEST: 0x0B90,
        STENCIL_CLEAR_VALUE: 0x0B91,
        STENCIL_FUNC: 0x0B92,
        STENCIL_VALUE_MASK: 0x0B93,
        STENCIL_FAIL: 0x0B94,
        STENCIL_PASS_DEPTH_FAIL: 0x0B95,
        STENCIL_PASS_DEPTH_PASS: 0x0B96,
        STENCIL_REF: 0x0B97,
        STENCIL_WRITEMASK: 0x0B98,
        ALPHA_TEST: 0x0BC0,
        ALPHA_TEST_FUNC: 0x0BC1,
        ALPHA_TEST_REF: 0x0BC2,
        DITHER: 0x0BD0,
        BLEND_DST: 0x0BE0,
        BLEND_SRC: 0x0BE1,
        BLEND: 0x0BE2,
        BLEND_DST_RGB: 0x80C8,
        BLEND_SRC_RGB: 0x80C9,
        BLEND_DST_ALPHA: 0x80CA,
        BLEND_SRC_ALPHA: 0x80CB,
        BLEND_EQUATION_RGB: 0x8009,
        BLEND_EQUATION_ALPHA: 0x883D,
        LOGIC_OP: 0x0BF1,
        INDEX_LOGIC_OP: 0x0BF1,
        COLOR_LOGIC_OP: 0x0BF2,
        SCISSOR_BOX: 0x0C10,
        SCISSOR_TEST: 0x0C11,
        INDEX_CLEAR_VALUE: 0x0C20,
        INDEX_WRITEMASK: 0x0C21,
        COLOR_CLEAR_VALUE: 0x0C22,
        COLOR_WRITEMASK: 0x0C23,
        DOUBLEBUFFER: 0x0C32,
        STEREO: 0x0C33,
        RENDER_MODE: 0x0C40,
        RGBA_MODE: 0x0C31,
        FEEDBACK_BUFFER_POINTER: 0x0DF0,
        FEEDBACK_BUFFER_SIZE: 0x0DF1,
        FEEDBACK_BUFFER_TYPE: 0x0DF2,
        SELECTION_BUFFER_POINTER: 0x0DF3,
        SELECTION_BUFFER_SIZE: 0x0DF4,
        PERSPECTIVE_CORRECTION_HINT: 0x0C50,
        POINT_SMOOTH_HINT: 0x0C51,
        LINE_SMOOTH_HINT: 0x0C52,
        POLYGON_SMOOTH_HINT: 0x0C53,
        FOG_HINT: 0x0C54,
        TEXTURE_GEN_S: 0x0C60,
        TEXTURE_GEN_T: 0x0C61,
        TEXTURE_GEN_R: 0x0C62,
        TEXTURE_GEN_Q: 0x0C63,
        MAX_LIGHTS: 0x0D31,
        MAX_EVAL_ORDER: 0x0D30,
        MAX_CLIP_PLANES: 0x0D32,
        MAX_TEXTURE_SIZE: 0x0D33,
        MAX_RECTANGLE_TEXTURE_SIZE: 0x84F8,
        TEXTURE_RECTANGLE: 0x84F5,
        TEXTURE_BINDING_RECTANGLE: 0x84F6,
        PROXY_TEXTURE_RECTANGLE: 0x84F7,
        MAX_3D_TEXTURE_SIZE: 0x8073,
        MAX_CUBE_MAP_TEXTURE_SIZE: 0x851C,
        MAX_PIXEL_MAP_TABLE: 0x0D34,
        MAX_ATTRIB_STACK_DEPTH: 0x0D35,
        MAX_MODELVIEW_STACK_DEPTH: 0x0D36,
        MAX_NAME_STACK_DEPTH: 0x0D37,
        MAX_PROJECTION_STACK_DEPTH: 0x0D38,
        MAX_TEXTURE_STACK_DEPTH: 0x0D39,
        MAX_VIEWPORT_DIMS: 0x0D3A,
        MAX_CLIENT_ATTRIB_STACK_DEPTH: 0x0D3B,
        SUBPIXEL_BITS: 0x0D50,
        RED_BITS: 0x0D52,
        GREEN_BITS: 0x0D53,
        BLUE_BITS: 0x0D54,
        ALPHA_BITS: 0x0D55,
        DEPTH_BITS: 0x0D56,
        STENCIL_BITS: 0x0D57,
        MATRIX_MODE: 0x0BA0,
        VIEWPORT: 0x0BA2,
        MODELVIEW_MATRIX: 0x0BA6,
        PROJECTION_MATRIX: 0x0BA7,
        TEXTURE_MATRIX: 0x0BA8,
        MODELVIEW_STACK_DEPTH: 0x0BA3,
        PROJECTION_STACK_DEPTH: 0x0BA4,
        TEXTURE_STACK_DEPTH: 0x0BA5,
        COLOR_MATRIX_SGI: 0x80B1,
        COLOR_MATRIX_STACK_DEPTH_SGI: 0x80B2,
        MAX_COLOR_MATRIX_STACK_DEPTH_SGI: 0x80B3,
        POST_COLOR_MATRIX_RED_SCALE_SGI: 0x80B4,
        POST_COLOR_MATRIX_GREEN_SCALE_SGI: 0x80B5,
        POST_COLOR_MATRIX_BLUE_SCALE_SGI: 0x80B6,
        POST_COLOR_MATRIX_ALPHA_SCALE_SGI: 0x80B7,
        POST_COLOR_MATRIX_RED_BIAS_SGI: 0x80B8,
        POST_COLOR_MATRIX_GREEN_BIAS_SGI: 0x80B9,
        POST_COLOR_MATRIX_BLUE_BIAS_SGI: 0x80BA,
        POST_COLOR_MATRIX_ALPHA_BIAS_SGI: 0x80BB,
        ATTRIB_STACK_DEPTH: 0x0BB0,
        CLIENT_ATTRIB_STACK_DEPTH: 0x0BB1,
        UNPACK_ALIGNMENT: 0x0CF5,
        PACK_ALIGNMENT: 0x0D05,
        TEXTURE_1D: 0x0DE0,
        TEXTURE_2D: 0x0DE1,
        TEXTURE_3D: 0x806F,
        TEXTURE_WRAP_R: 0x8072,
        TEXTURE_BINDING_1D: 0x8068,
        POLYGON_OFFSET_FILL: 0x8037,
        POLYGON_OFFSET_POINT: 0x2A01,
        POLYGON_OFFSET_LINE: 0x2A02,
        TEXTURE_BINDING_2D: 0x8069,
        TEXTURE_BINDING_3D: 0x806A,
        TEXTURE_DEPTH: 0x8071,
        RESCALE_NORMAL: 0x803A,
        LIGHT_MODEL_COLOR_CONTROL: 0x81F8,
        SINGLE_COLOR: 0x81F9,
        SEPARATE_SPECULAR_COLOR: 0x81FA,
        CLAMP_TO_EDGE: 0x812F,
        CONSTANT_COLOR: 0x8001,
        ONE_MINUS_CONSTANT_COLOR: 0x8002,
        CONSTANT_ALPHA: 0x8003,
        ONE_MINUS_CONSTANT_ALPHA: 0x8004,
        FUNC_ADD: 0x8006,
        MIN: 0x8007,
        MAX: 0x8008,
        FUNC_SUBTRACT: 0x800A,
        FUNC_REVERSE_SUBTRACT: 0x800B,
        TEXTURE_CUBE_MAP: 0x8513,
        TEXTURE_CUBE_MAP_POSITIVE_X: 0x8515,
        TEXTURE_CUBE_MAP_NEGATIVE_X: 0x8516,
        TEXTURE_CUBE_MAP_POSITIVE_Y: 0x8517,
        TEXTURE_CUBE_MAP_NEGATIVE_Y: 0x8518,
        TEXTURE_CUBE_MAP_POSITIVE_Z: 0x8519,
        TEXTURE_CUBE_MAP_NEGATIVE_Z: 0x851A,
        PROXY_TEXTURE_CUBE_MAP: 0x851B,
        PROXY_TEXTURE_1D: 0x8063,
        PROXY_TEXTURE_2D: 0x8064,
        PROXY_TEXTURE_3D: 0x8070,
        TEXTURE_BINDING_CUBE_MAP: 0x8514,
        SAMPLE_COVERAGE: 0x80A0,
        SAMPLE_COVERAGE_VALUE: 0x80AA,
        SAMPLE_COVERAGE_INVERT: 0x80AB,
        DOT3_RGB: 0x86AE,
        DOT3_RGBA: 0x86AF,
        COMPRESSED_ALPHA: 0x84E9,
        COMPRESSED_LUMINANCE: 0x84EA,
        COMPRESSED_LUMINANCE_ALPHA: 0x84EB,
        COMPRESSED_INTENSITY: 0x84EC,
        COMPRESSED_RGB: 0x84ED,
        COMPRESSED_RGBA: 0x84EE,
        TEXTURE_COMPRESSION_HINT: 0x84EF,
        TEXTURE_COMPRESSED_IMAGE_SIZE: 0x86A0,
        TEXTURE_COMPRESSED: 0x86A1,
        NUM_COMPRESSED_TEXTURE_FORMATS: 0x86A2,
        COMPRESSED_TEXTURE_FORMATS: 0x86A3,
        COMPRESSED_RGB_S3TC_DXT1_EXT: 0x83F0,
        COMPRESSED_RGBA_S3TC_DXT1_EXT: 0x83F1,
        COMPRESSED_RGBA_S3TC_DXT3_EXT: 0x83F2,
        COMPRESSED_RGBA_S3TC_DXT5_EXT: 0x83F3,
        COMPRESSED_SRGB_S3TC_DXT1_EXT: 0x8C4C,
        COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: 0x8C4D,
        COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: 0x8C4E,
        COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: 0x8C4F,
        GENERATE_MIPMAP: 0x8191,
        GENERATE_MIPMAP_HINT: 0x8192,
        TEXTURE_COMPARE_MODE_ARB: 0x884C,
        TEXTURE_COMPARE_FUNC_ARB: 0x884D,
        COMPARE_R_TO_TEXTURE_ARB: 0x884E,
        DEPTH_TEXTURE_MODE: 0x884B,
        DEPTH_COMPONENT16: 0x81A5,
        DEPTH_COMPONENT24: 0x81A6,
        DEPTH_COMPONENT32: 0x81A7,
        DEPTH_COMPONENT32F: 0x8CAC,
        DEPTH_STENCIL: 0x84F9,
        UNSIGNED_INT_24_8: 0x84FA,
        DEPTH24_STENCIL8: 0x88F0,
        DEPTH32F_STENCIL8: 0x8CAD,
        FLOAT_32_UNSIGNED_INT_24_8_REV: 0x8DAD,
        FOG_COORDINATE_SOURCE_EXT: 0x8450,
        FOG_COORDINATE_EXT: 0x8451,
        FRAGMENT_DEPTH_EXT: 0x8452,
        CURRENT_FOG_COORDINATE_EXT: 0x8453,
        FOG_COORDINATE_ARRAY_TYPE_EXT: 0x8454,
        FOG_COORDINATE_ARRAY_STRIDE_EXT: 0x8455,
        FOG_COORDINATE_ARRAY_POINTER_EXT: 0x8456,
        FOG_COORDINATE_ARRAY_EXT: 0x8457,
        COLOR_SUM_EXT: 0x8458,
        CURRENT_SECONDARY_COLOR_EXT: 0x8459,
        SECONDARY_COLOR_ARRAY_SIZE_EXT: 0x845A,
        SECONDARY_COLOR_ARRAY_TYPE_EXT: 0x845B,
        SECONDARY_COLOR_ARRAY_STRIDE_EXT: 0x845C,
        SECONDARY_COLOR_ARRAY_POINTER_EXT: 0x845D,
        SECONDARY_COLOR_ARRAY_EXT: 0x845E,
        POINT_SIZE_MIN_ARB: 0x8126,
        POINT_SIZE_MAX_ARB: 0x8127,
        POINT_FADE_THRESHOLD_SIZE_ARB: 0x8128,
        POINT_DISTANCE_ATTENUATION_ARB: 0x8129,
        INCR_WRAP_EXT: 0x8507,
        DECR_WRAP_EXT: 0x8508,
        MIRRORED_REPEAT_ARB: 0x8370,
        POINT_SPRITE_ARB: 0x8861,
        COORD_REPLACE_ARB: 0x8862,
        ARRAY_BUFFER_ARB: 0x8892,
        ELEMENT_ARRAY_BUFFER_ARB: 0x8893,
        ARRAY_BUFFER_BINDING_ARB: 0x8894,
        ELEMENT_ARRAY_BUFFER_BINDING_ARB: 0x8895,
        PIXEL_PACK_BUFFER: 0x88EB,
        ARRAY_ELEMENT_LOCK_FIRST_EXT: 0x81A8,
        ARRAY_ELEMENT_LOCK_COUNT_EXT: 0x81A9,
        CLIENT_PIXEL_STORE_BIT: 0x00000001,
        CLIENT_VERTEX_ARRAY_BIT: 0x00000002,
        CLIENT_ALL_ATTRIB_BITS: 0xFFFFFFFF,
        PIXEL_UNPACK_BUFFER: 0x88EC,
        PIXEL_PACK_BUFFER_BINDING: 0x88ED,
        PIXEL_UNPACK_BUFFER_BINDING: 0x88EF,
        STREAM_DRAW_ARB: 0x88E0,
        STATIC_DRAW_ARB: 0x88E4,
        DYNAMIC_DRAW_ARB: 0x88E8,
        READ_ONLY_ARB: 0x88B8,
        WRITE_ONLY_ARB: 0x88B9,
        READ_WRITE_ARB: 0x88BA,
        BUFFER_SIZE_ARB: 0x8764,
        BUFFER_USAGE_ARB: 0x8765,
        BUFFER_ACCESS_ARB: 0x88BB,
        BUFFER_MAPPED_ARB: 0x88BC,
        BUFFER_MAP_POINTER_ARB: 0x88BD,
        SAMPLES_PASSED: 0x8914,
        QUERY_COUNTER_BITS: 0x8864,
        CURRENT_QUERY: 0x8865,
        QUERY_RESULT: 0x8866,
        QUERY_RESULT_AVAILABLE: 0x8867,
        ANY_SAMPLES_PASSED: 0x8C2F,
        ANY_SAMPLES_PASSED_CONSERVATIVE: 0x8D6A,
        VERTEX_ARRAY_SIZE: 0x807A,
        VERTEX_ARRAY_TYPE: 0x807B,
        VERTEX_ARRAY_STRIDE: 0x807C,
        VERTEX_ARRAY_POINTER: 0x808E,
        NORMAL_ARRAY_TYPE: 0x807E,
        NORMAL_ARRAY_STRIDE: 0x807F,
        NORMAL_ARRAY_POINTER: 0x808F,
        COLOR_ARRAY_SIZE: 0x8081,
        COLOR_ARRAY_TYPE: 0x8082,
        COLOR_ARRAY_STRIDE: 0x8083,
        COLOR_ARRAY_POINTER: 0x8090,
        INDEX_ARRAY: 0x8077,
        INDEX_ARRAY_TYPE: 0x8085,
        INDEX_ARRAY_STRIDE: 0x8086,
        INDEX_ARRAY_POINTER: 0x8091,
        TEXTURE_COORD_ARRAY_SIZE: 0x8088,
        TEXTURE_COORD_ARRAY_TYPE: 0x8089,
        TEXTURE_COORD_ARRAY_STRIDE: 0x808A,
        TEXTURE_COORD_ARRAY_POINTER: 0x8092,
        EDGE_FLAG_ARRAY: 0x8079,
        EDGE_FLAG_ARRAY_STRIDE: 0x808C,
        EDGE_FLAG_ARRAY_POINTER: 0x8093,
        ALIASED_LINE_WIDTH_RANGE: 0x846E,
        ACTIVE_TEXTURE_ARB: 0x84E0,
        CLIENT_ACTIVE_TEXTURE_ARB: 0x84E1,
        MAX_TEXTURE_UNITS_ARB: 0x84E2,
        TEXTURE0_ARB: 0x84C0,
        TEXTURE1_ARB: 0x84C1,
        TEXTURE2_ARB: 0x84C2,
        TEXTURE3_ARB: 0x84C3,
        TEXTURE4_ARB: 0x84C4,
        TEXTURE5_ARB: 0x84C5,
        TEXTURE6_ARB: 0x84C6,
        TEXTURE7_ARB: 0x84C7,
        COMBINE_ARB: 0x8570,
        MAX_TEXTURE_MAX_ANISOTROPY_EXT: 0x84FF,
        EXP: 0x0800,
        EXP2: 0x0801,
        KEEP: 0x1E00,
        REPLACE: 0x1E01,
        INCR: 0x1E02,
        DECR: 0x1E03,
        AMBIENT: 0x1200,
        DIFFUSE: 0x1201,
        SPECULAR: 0x1202,
        POSITION: 0x1203,
        SPOT_DIRECTION: 0x1204,
        SPOT_EXPONENT: 0x1205,
        SPOT_CUTOFF: 0x1206,
        CONSTANT_ATTENUATION: 0x1207,
        LINEAR_ATTENUATION: 0x1208,
        QUADRATIC_ATTENUATION: 0x1209,
        EMISSION: 0x1600,
        SHININESS: 0x1601,
        AMBIENT_AND_DIFFUSE: 0x1602,
        COLOR_INDEXES: 0x1603,
        LIGHT_MODEL_LOCAL_VIEWER: 0x0B51,
        LIGHT_MODEL_TWO_SIDE: 0x0B52,
        LIGHT_MODEL_AMBIENT: 0x0B53,
        LIGHT0: 0x4000,
        LIGHT1: 0x4001,
        LIGHT2: 0x4002,
        LIGHT3: 0x4003,
        LIGHT4: 0x4004,
        LIGHT5: 0x4005,
        LIGHT6: 0x4006,
        LIGHT7: 0x4007,
        MODULATE: 0x2100,
        DECAL: 0x2101,
        S: 0x2000,
        T: 0x2001,
        R: 0x2002,
        Q: 0x2003,
        OBJECT_LINEAR: 0x2401,
        EYE_LINEAR: 0x2400,
        SPHERE_MAP: 0x2402,
        TEXTURE_GEN_MODE: 0x2500,
        OBJECT_PLANE: 0x2501,
        EYE_PLANE: 0x2502,
        TEXTURE_ENV: 0x2300,
        TEXTURE_ENV_MODE: 0x2200,
        TEXTURE_ENV_COLOR: 0x2201,
        TEXTURE_MAG_FILTER: 0x2800,
        TEXTURE_MIN_FILTER: 0x2801,
        TEXTURE_WRAP_S: 0x2802,
        TEXTURE_WRAP_T: 0x2803,
        REPEAT: 0x2901,
        TEXTURE_BORDER_COLOR: 0x1004,
        TEXTURE_WIDTH: 0x1000,
        TEXTURE_HEIGHT: 0x1001,
        TEXTURE_COMPONENTS: 0x1003,
        TEXTURE_INTERNAL_FORMAT: 0x1003,
        TEXTURE_BORDER: 0x1005,
        TEXTURE_RED_SIZE: 0x805C,
        TEXTURE_GREEN_SIZE: 0x805D,
        TEXTURE_BLUE_SIZE: 0x805E,
        TEXTURE_ALPHA_SIZE: 0x805F,
        TEXTURE_LUMINANCE_SIZE: 0x8060,
        TEXTURE_INTENSITY_SIZE: 0x8061,
        TEXTURE_DEPTH_SIZE: 0x884A,
        POLYGON_OFFSET_FACTOR: 0x8038,
        POLYGON_OFFSET_UNITS: 0x2A00,
        CURRENT_EDGE_FLAG: 0x0B43, EDGE_FLAG: 0x0B43,
        CURRENT_RASTER_POSITION: 0x0B07,
        CURRENT_RASTER_POSITION_VALID: 0x0B08,
        CURRENT_RASTER_DISTANCE: 0x0B09,
        CURRENT_RASTER_COLOR: 0x0B04,
        CURRENT_RASTER_INDEX: 0x0B05,
        CURRENT_RASTER_TEXTURE_COORDS: 0x0B06,
        CURRENT_RASTER_SECONDARY_COLOR: 0x845F,
        COEFF: 0x0A00,
        ORDER: 0x0A01,
        DOMAIN: 0x0A02,
        MAP1_GRID_DOMAIN: 0x0DD0, MAP1_GRID_SEGMENTS: 0x0DD1,
        MAP2_GRID_DOMAIN: 0x0DD2, MAP2_GRID_SEGMENTS: 0x0DD3,
        MAP1_COLOR_4: 0x0D90,
        MAP1_INDEX: 0x0D91,
        MAP1_NORMAL: 0x0D92,
        MAP1_TEXTURE_COORD_1: 0x0D93,
        MAP1_TEXTURE_COORD_2: 0x0D94,
        MAP1_TEXTURE_COORD_3: 0x0D95,
        MAP1_TEXTURE_COORD_4: 0x0D96,
        MAP1_VERTEX_3: 0x0D97,
        MAP1_VERTEX_4: 0x0D98,
        MAP2_COLOR_4: 0x0DB0,
        MAP2_INDEX: 0x0DB1,
        MAP2_NORMAL: 0x0DB2,
        MAP2_TEXTURE_COORD_1: 0x0DB3,
        MAP2_TEXTURE_COORD_2: 0x0DB4,
        MAP2_TEXTURE_COORD_3: 0x0DB5,
        MAP2_TEXTURE_COORD_4: 0x0DB6,
        MAP2_VERTEX_3: 0x0DB7,
        MAP2_VERTEX_4: 0x0DB8,
        PIXEL_MAP_I_TO_I: 0x0C70,
        PIXEL_MAP_S_TO_S: 0x0C71,
        PIXEL_MAP_I_TO_R: 0x0C72,
        PIXEL_MAP_I_TO_G: 0x0C73,
        PIXEL_MAP_I_TO_B: 0x0C74,
        PIXEL_MAP_I_TO_A: 0x0C75,
        PIXEL_MAP_R_TO_R: 0x0C76,
        PIXEL_MAP_G_TO_G: 0x0C77,
        PIXEL_MAP_B_TO_B: 0x0C78,
        PIXEL_MAP_A_TO_A: 0x0C79,
        PIXEL_MAP_I_TO_I_SIZE: 0x0CB0,
        PIXEL_MAP_S_TO_S_SIZE: 0x0CB1,
        PIXEL_MAP_I_TO_R_SIZE: 0x0CB2,
        PIXEL_MAP_I_TO_G_SIZE: 0x0CB3,
        PIXEL_MAP_I_TO_B_SIZE: 0x0CB4,
        PIXEL_MAP_I_TO_A_SIZE: 0x0CB5,
        PIXEL_MAP_R_TO_R_SIZE: 0x0CB6,
        PIXEL_MAP_G_TO_G_SIZE: 0x0CB7,
        PIXEL_MAP_B_TO_B_SIZE: 0x0CB8,
        PIXEL_MAP_A_TO_A_SIZE: 0x0CB9,
        NEAREST: 0x2600,
        LINEAR: 0x2601,
        NEAREST_MIPMAP_NEAREST: 0x2700,
        LINEAR_MIPMAP_NEAREST: 0x2701,
        NEAREST_MIPMAP_LINEAR: 0x2702,
        LINEAR_MIPMAP_LINEAR: 0x2703,
        FLAT: 0x1D00,
        SMOOTH: 0x1D01,
        RENDER: 0x1C00,
        FEEDBACK: 0x1C01,
        SELECT: 0x1C02,
        "2D": 0x0600,
        "3D": 0x0601,
        "3D_COLOR": 0x0602,
        "3D_COLOR_TEXTURE": 0x0603,
        "4D_COLOR_TEXTURE": 0x0604,
        POINT_TOKEN: 0x0701,
        LINE_TOKEN: 0x0702,
        LINE_RESET_TOKEN: 0x0707,
        POLYGON_TOKEN: 0x0703,
        BITMAP_TOKEN: 0x0704,
        DRAW_PIXEL_TOKEN: 0x0705,
        COPY_PIXEL_TOKEN: 0x0706,
        PASS_THROUGH_TOKEN: 0x0700,
        DONT_CARE: 0x1100,
        FASTEST: 0x1101,
        NICEST: 0x1102,
        POINT: 0x1B00,
        LINE: 0x1B01,
        FILL: 0x1B02,
        MODELVIEW: 0x1700,
        PROJECTION: 0x1701,
        TEXTURE: 0x1702,
        COMPILE: 0x1300,
        COMPILE_AND_EXECUTE: 0x1301,
        TEXTURE_PRIORITY: 0x8066,
        TEXTURE_RESIDENT: 0x8067,
        TEXTURE_MIN_LOD: 0x813A,
        TEXTURE_MAX_LOD: 0x813B,
        TEXTURE_BASE_LEVEL: 0x813C,
        TEXTURE_MAX_LEVEL: 0x813D,
        COLOR_MATERIAL: 0x0B57,
        COLOR_MATERIAL_FACE: 0x0B55,
        COLOR_MATERIAL_PARAMETER: 0x0B56,
        CLIP_PLANE0: 0x3000,
        CLIP_PLANE1: 0x3001,
        CLIP_PLANE2: 0x3002,
        CLIP_PLANE3: 0x3003,
        CLIP_PLANE4: 0x3004,
        CLIP_PLANE5: 0x3005,
        BYTE: 0x1400,
        UNSIGNED_BYTE: 0x1401,
        SHORT: 0x1402,
        UNSIGNED_SHORT: 0x1403,
        INT: 0x1404,
        UNSIGNED_INT: 0x1405,
        FLOAT: 0x1406,
        "2_BYTES": 0x1407,
        "3_BYTES": 0x1408,
        "4_BYTES": 0x1409,
        DOUBLE: 0x140A,
        HALF_FLOAT_ARB: 0x140B,
        RED: 0x1903,
        GREEN: 0x1904,
        BLUE: 0x1905,
        ALPHA: 0x1906,
        RGB: 0x1907,
        RGBA: 0x1908,
        LUMINANCE: 0x1909,
        LUMINANCE_ALPHA: 0x190A,
        DEPTH_COMPONENT: 0x1902,
        RGB8: 0x8051,
        RGBA8: 0x8058,
        RGBA16: 0x805B,
        SRGB: 0x8C40,
        SRGB8: 0x8C41,
        SRGB_ALPHA: 0x8C42,
        SRGB8_ALPHA8: 0x8C43,
        SLUMINANCE_ALPHA: 0x8C44,
        SLUMINANCE8_ALPHA8: 0x8C45,
        SLUMINANCE: 0x8C46,
        SLUMINANCE8: 0x8C47,
        COMPRESSED_SRGB: 0x8C48,
        COMPRESSED_SRGB_ALPHA: 0x8C49,
        COMPRESSED_SLUMINANCE: 0x8C4A,
        COMPRESSED_SLUMINANCE_ALPHA: 0x8C4B,
        FRAMEBUFFER_SRGB: 0x8DB9,
        RGBA32F_ARB: 0x8814,
        RGB32F_ARB: 0x8815,
        ALPHA32F_ARB: 0x8816,
        LUMINANCE32F_ARB: 0x8818,
        LUMINANCE_ALPHA32F_ARB: 0x8819,
        RGBA16F_ARB: 0x881A,
        RGB16F_ARB: 0x881B,
        ALPHA16F_ARB: 0x881C,
        LUMINANCE16F_ARB: 0x881E,
        LUMINANCE_ALPHA16F_ARB: 0x881F,
        PACK_SWAP_BYTES: 0x0D00,
        PACK_LSB_FIRST: 0x0D01,
        PACK_ROW_LENGTH: 0x0D02,
        PACK_SKIP_ROWS: 0x0D03,
        PACK_SKIP_PIXELS: 0x0D04,
        PACK_SKIP_IMAGES: 0x806B,
        PACK_IMAGE_HEIGHT: 0x806C,
        BGR_EXT: 0x80E0,
        BGRA_EXT: 0x80E1,
        UNSIGNED_SHORT_4_4_4_4: 0x8033,
        UNSIGNED_SHORT_5_5_5_1: 0x8034,
        UNSIGNED_SHORT_5_6_5: 0x8363,
        UNSIGNED_SHORT_5_6_5_REV: 0x8364,
        UNSIGNED_SHORT_4_4_4_4_REV: 0x8365,
        UNSIGNED_SHORT_1_5_5_5_REV: 0x8366,
        UNSIGNED_INT_8_8_8_8: 0x8035,
        UNSIGNED_INT_8_8_8_8_REV: 0x8367,
        COLOR: 0x1800,
        DEPTH: 0x1801,
        STENCIL: 0x1802,
        UNPACK_SWAP_BYTES: 0x0CF0,
        UNPACK_LSB_FIRST: 0x0CF1,
        UNPACK_ROW_LENGTH: 0x0CF2,
        UNPACK_SKIP_ROWS: 0x0CF3,
        UNPACK_SKIP_PIXELS: 0x0CF4,
        UNPACK_SKIP_IMAGES: 0x806D,
        UNPACK_IMAGE_HEIGHT: 0x806E,
        INVALID_ENUM: 0x0500,
        INVALID_VALUE: 0x0501,
        INVALID_OPERATION: 0x0502,
        OUT_OF_MEMORY: 0x0505,
        INVALID_FRAMEBUFFER_OPERATION_EXT: 0x0506,
        VERTEX_ARRAY: 0x8074,
        NORMAL_ARRAY: 0x8075,
        COLOR_ARRAY: 0x8076,
        TEXTURE_COORD_ARRAY: 0x8078,
        VERTEX_ATTRIB_ARRAY_ENABLED: 0x8622,
        VERTEX_ATTRIB_ARRAY_SIZE: 0x8623,
        VERTEX_ATTRIB_ARRAY_STRIDE: 0x8624,
        VERTEX_ATTRIB_ARRAY_TYPE: 0x8625,
        CURRENT_VERTEX_ATTRIB: 0x8626,
        VERTEX_PROGRAM_POINT_SIZE: 0x8642,
        VERTEX_PROGRAM_TWO_SIDE: 0x8643,
        VERTEX_ATTRIB_ARRAY_POINTER: 0x8645,
        VERTEX_ATTRIB_ARRAY_NORMALIZED: 0x886A,
        VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: 0x889F,
        MAX_VERTEX_ATTRIBS: 0x8869,
        MAX_TEXTURE_COORDS: 0x8871,
        MAX_TEXTURE_IMAGE_UNITS: 0x8872,
        MAX_FRAGMENT_UNIFORM_COMPONENTS: 0x8B49,
        MAX_VERTEX_UNIFORM_COMPONENTS: 0x8B4A,
        MAX_VARYING_FLOATS: 0x8B4B,
        MAX_VERTEX_TEXTURE_IMAGE_UNITS: 0x8B4C,
        MAX_COMBINED_TEXTURE_IMAGE_UNITS: 0x8B4D,
        FRAGMENT_SHADER: 0x8B30,
        VERTEX_SHADER: 0x8B31,
        SHADER_TYPE: 0x8B4F,
        FLOAT_VEC2: 0x8B50,
        FLOAT_VEC3: 0x8B51,
        FLOAT_VEC4: 0x8B52,
        INT_VEC2: 0x8B53,
        INT_VEC3: 0x8B54,
        INT_VEC4: 0x8B55,
        BOOL: 0x8B56,
        BOOL_VEC2: 0x8B57,
        BOOL_VEC3: 0x8B58,
        BOOL_VEC4: 0x8B59,
        FLOAT_MAT2: 0x8B5A,
        FLOAT_MAT3: 0x8B5B,
        FLOAT_MAT4: 0x8B5C,
        SAMPLER_1D: 0x8B5D,
        SAMPLER_2D: 0x8B5E,
        SAMPLER_3D: 0x8B5F,
        SAMPLER_CUBE: 0x8B60,
        SAMPLER_1D_SHADOW: 0x8B61,
        SAMPLER_2D_SHADOW: 0x8B62,
        DELETE_STATUS: 0x8B80,
        COMPILE_STATUS: 0x8B81,
        LINK_STATUS: 0x8B82,
        VALIDATE_STATUS: 0x8B83,
        INFO_LOG_LENGTH: 0x8B84,
        ATTACHED_SHADERS: 0x8B85,
        ACTIVE_UNIFORMS: 0x8B86,
        ACTIVE_UNIFORM_MAX_LENGTH: 0x8B87,
        SHADER_SOURCE_LENGTH: 0x8B88,
        ACTIVE_ATTRIBUTES: 0x8B89,
        ACTIVE_ATTRIBUTE_MAX_LENGTH: 0x8B8A,
        FRAGMENT_SHADER_DERIVATIVE_HINT: 0x8B8B,
        SHADING_LANGUAGE_VERSION: 0x8B8C,
        CURRENT_PROGRAM: 0x8B8D,
        PROGRAM_OBJECT_ARB: 0x8B40,
        SHADER_OBJECT_ARB: 0x8B48,
        OBJECT_TYPE_ARB: 0x8B4E,
        OBJECT_SUBTYPE_ARB: 0x8B4F,
        OBJECT_DELETE_STATUS_ARB: 0x8B80,
        OBJECT_COMPILE_STATUS_ARB: 0x8B81,
        OBJECT_LINK_STATUS_ARB: 0x8B82,
        OBJECT_VALIDATE_STATUS_ARB: 0x8B83,
        OBJECT_INFO_LOG_LENGTH_ARB: 0x8B84,
        OBJECT_ATTACHED_OBJECTS_ARB: 0x8B85,
        OBJECT_ACTIVE_UNIFORMS_ARB: 0x8B86,
        OBJECT_ACTIVE_UNIFORM_MAX_LENGTH_ARB: 0x8B87,
        OBJECT_SHADER_SOURCE_LENGTH_ARB: 0x8B88,
        POINT_SPRITE_COORD_ORIGIN: 0x8CA0,
        LOWER_LEFT: 0x8CA1,
        UPPER_LEFT: 0x8CA2,
        STENCIL_BACK_FUNC: 0x8800,
        STENCIL_BACK_FAIL: 0x8801,
        STENCIL_BACK_PASS_DEPTH_FAIL: 0x8802,
        STENCIL_BACK_PASS_DEPTH_PASS: 0x8803,
        STENCIL_BACK_REF: 0x8CA3,
        STENCIL_BACK_VALUE_MASK: 0x8CA4,
        STENCIL_BACK_WRITEMASK: 0x8CA5,
        MAX_DRAW_BUFFERS: 0x8824,
        DRAW_BUFFER0: 0x8825,
        DRAW_BUFFER15: 0x8834,
        STENCIL_TEST_TWO_SIDE_EXT: 0x8910,
        CURRENT_MATRIX_ARB: 0x8641,
        TRANSPOSE_CURRENT_MATRIX_ARB: 0x88B7,
        CURRENT_MATRIX_STACK_DEPTH_ARB: 0x8640,
        MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB: 0x862E,
        MATRIX0_ARB: 0x88C0,
        MATRIX31_ARB: 0x88DF,
        ACTIVE_STENCIL_FACE_EXT: 0x8911,
        VERTEX_PROGRAM_ARB: 0x8620,
        PROGRAM_LENGTH_ARB: 0x8627,
        PROGRAM_STRING_ARB: 0x8628,
        PROGRAM_ERROR_POSITION_ARB: 0x864B,
        PROGRAM_BINDING_ARB: 0x8677,
        MAX_VERTEX_ATTRIBS_ARB: 0x8869,
        PROGRAM_ERROR_STRING_ARB: 0x8874,
        PROGRAM_FORMAT_ASCII_ARB: 0x8875,
        PROGRAM_FORMAT_ARB: 0x8876,
        PROGRAM_INSTRUCTIONS_ARB: 0x88A0,
        MAX_PROGRAM_INSTRUCTIONS_ARB: 0x88A1,
        PROGRAM_NATIVE_INSTRUCTIONS_ARB: 0x88A2,
        MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB: 0x88A3,
        PROGRAM_TEMPORARIES_ARB: 0x88A4,
        MAX_PROGRAM_TEMPORARIES_ARB: 0x88A5,
        PROGRAM_NATIVE_TEMPORARIES_ARB: 0x88A6,
        MAX_PROGRAM_NATIVE_TEMPORARIES_ARB: 0x88A7,
        PROGRAM_PARAMETERS_ARB: 0x88A8,
        MAX_PROGRAM_PARAMETERS_ARB: 0x88A9,
        PROGRAM_NATIVE_PARAMETERS_ARB: 0x88AA,
        MAX_PROGRAM_NATIVE_PARAMETERS_ARB: 0x88AB,
        PROGRAM_ATTRIBS_ARB: 0x88AC,
        MAX_PROGRAM_ATTRIBS_ARB: 0x88AD,
        PROGRAM_NATIVE_ATTRIBS_ARB: 0x88AE,
        MAX_PROGRAM_NATIVE_ATTRIBS_ARB: 0x88AF,
        PROGRAM_ADDRESS_REGISTERS_ARB: 0x88B0,
        MAX_PROGRAM_ADDRESS_REGISTERS_ARB: 0x88B1,
        PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB: 0x88B2,
        MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB: 0x88B3,
        MAX_PROGRAM_LOCAL_PARAMETERS_ARB: 0x88B4,
        MAX_PROGRAM_ENV_PARAMETERS_ARB: 0x88B5,
        PROGRAM_UNDER_NATIVE_LIMITS_ARB: 0x88B6,
        MAX_PROGRAM_MATRICES_ARB: 0x862F,
        FRAGMENT_PROGRAM_ARB: 0x8804,
        PROGRAM_ALU_INSTRUCTIONS_ARB: 0x8805,
        PROGRAM_TEX_INSTRUCTIONS_ARB: 0x8806,
        PROGRAM_TEX_INDIRECTIONS_ARB: 0x8807,
        PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB: 0x8808,
        PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB: 0x8809,
        PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB: 0x880A,
        MAX_PROGRAM_ALU_INSTRUCTIONS_ARB: 0x880B,
        MAX_PROGRAM_TEX_INSTRUCTIONS_ARB: 0x880C,
        MAX_PROGRAM_TEX_INDIRECTIONS_ARB: 0x880D,
        MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB: 0x880E,
        MAX_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB: 0x880F,
        MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB: 0x8810,
        MAX_TEXTURE_COORDS_ARB: 0x8871,
        MAX_TEXTURE_IMAGE_UNITS_ARB: 0x8872,
        MAX_RENDERBUFFER_SIZE_EXT: 0x84E8,
        FRAMEBUFFER_BINDING_EXT: 0x8CA6,
        RENDERBUFFER_BINDING_EXT: 0x8CA7,
        READ_FRAMEBUFFER_EXT: 0x8CA8,
        DRAW_FRAMEBUFFER_EXT: 0x8CA9,
        READ_FRAMEBUFFER_BINDING_EXT: 0x8CAA,
        FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT: 0x8CD0,
        FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT: 0x8CD1,
        FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT: 0x8CD2,
        FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT: 0x8CD3,
        FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT: 0x8CD4,
        FRAMEBUFFER_COMPLETE_EXT: 0x8CD5,
        FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT: 0x8CD6,
        FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT: 0x8CD7,
        FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT: 0x8CD9,
        FRAMEBUFFER_INCOMPLETE_FORMATS_EXT: 0x8CDA,
        FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT: 0x8CDB,
        FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT: 0x8CDC,
        FRAMEBUFFER_UNSUPPORTED_EXT: 0x8CDD,
        MAX_COLOR_ATTACHMENTS_EXT: 0x8CDF,
        COLOR_ATTACHMENT0_EXT: 0x8CE0,
        COLOR_ATTACHMENT15_EXT: 0x8CEF,
        DEPTH_ATTACHMENT_EXT: 0x8D00,
        STENCIL_ATTACHMENT_EXT: 0x8D20,
        FRAMEBUFFER_EXT: 0x8D40,
        RENDERBUFFER_EXT: 0x8D41,
        RENDERBUFFER_WIDTH_EXT: 0x8D42,
        RENDERBUFFER_HEIGHT_EXT: 0x8D43,
        RENDERBUFFER_INTERNAL_FORMAT_EXT: 0x8D44,
        STENCIL_INDEX1_EXT: 0x8D46,
        STENCIL_INDEX4_EXT: 0x8D47,
        STENCIL_INDEX8_EXT: 0x8D48,
        STENCIL_INDEX16_EXT: 0x8D49,
        RENDERBUFFER_RED_SIZE_EXT: 0x8D50,
        RENDERBUFFER_GREEN_SIZE_EXT: 0x8D51,
        RENDERBUFFER_BLUE_SIZE_EXT: 0x8D52,
        RENDERBUFFER_ALPHA_SIZE_EXT: 0x8D53,
        RENDERBUFFER_DEPTH_SIZE_EXT: 0x8D54,
        RENDERBUFFER_STENCIL_SIZE_EXT: 0x8D55,
        TEXTURE_STENCIL_SIZE_EXT: 0x88F1,
        V2F: 0x2A20,
        V3F: 0x2A21,
        C4UB_V2F: 0x2A22,
        C4UB_V3F: 0x2A23,
        C3F_V3F: 0x2A24,
        N3F_V3F: 0x2A25,
        C4F_N3F_V3F: 0x2A26,
        T2F_V3F: 0x2A27,
        T4F_V4F: 0x2A28,
        T2F_C4UB_V3F: 0x2A29,
        T2F_C3F_V3F: 0x2A2A,
        T2F_N3F_V3F: 0x2A2B,
        T2F_C4F_N3F_V3F: 0x2A2C,
        T4F_C4F_N3F_V4F: 0x2A2D,
    };


    /*
     * Enums the guest header does not happen to #define but the host needs --
     * mostly the ones the guest passes straight through as opaque parameter
     * values (texture environment sources, sized internal formats, blend
     * factors) and never names itself.
     *
     * These are the values from the OpenGL registry. gl_protocol_consistency_test.js
     * asserts that nothing here disagrees with the guest's own table, because a
     * duplicate with a different value would be far worse than a missing one.
     */
    const GL_SUPPLEMENT = {
        ABGR_EXT: 0x8000, ACCUM_BUFFER_BIT: 0x00000200,
        ACTIVE_TEXTURE: 0x84E0, CLIENT_ACTIVE_TEXTURE: 0x84E1,
        TEXTURE0: 0x84C0, MAX_TEXTURE_UNITS: 0x84E2,
        ADD: 0x0104, ADD_SIGNED: 0x8574, SUBTRACT: 0x84E7,
        COMBINE: 0x8570, COMBINE_RGB: 0x8571, COMBINE_ALPHA: 0x8572,
        INTERPOLATE: 0x8575, CONSTANT: 0x8576, PRIMARY_COLOR: 0x8577,
        PREVIOUS: 0x8578, RGB_SCALE: 0x8573,
        SRC0_RGB: 0x8580, SRC1_RGB: 0x8581, SRC2_RGB: 0x8582,
        SRC0_ALPHA: 0x8588, SRC1_ALPHA: 0x8589, SRC2_ALPHA: 0x858A,
        OPERAND0_RGB: 0x8590, OPERAND1_RGB: 0x8591, OPERAND2_RGB: 0x8592,
        OPERAND0_ALPHA: 0x8598, OPERAND1_ALPHA: 0x8599, OPERAND2_ALPHA: 0x859A,
        SRC_COLOR: 0x0300, ONE_MINUS_SRC_COLOR: 0x0301,
        SRC_ALPHA: 0x0302, ONE_MINUS_SRC_ALPHA: 0x0303,
        DST_ALPHA: 0x0304, ONE_MINUS_DST_ALPHA: 0x0305,
        DST_COLOR: 0x0306, ONE_MINUS_DST_COLOR: 0x0307,
        SRC_ALPHA_SATURATE: 0x0308,
        MIN_EXT: 0x8007, MAX_EXT: 0x8008,
        CLEAR_LOGIC_OP: 0x1500, AND: 0x1501, AND_REVERSE: 0x1502,
        COPY: 0x1503, AND_INVERTED: 0x1504, NOOP: 0x1505,
        XOR: 0x1506, OR: 0x1507, NOR: 0x1508, EQUIV: 0x1509,
        INVERT: 0x150A, OR_REVERSE: 0x150B, COPY_INVERTED: 0x150C,
        OR_INVERTED: 0x150D, NAND: 0x150E, SET: 0x150F,
        INCR_WRAP: 0x8507, DECR_WRAP: 0x8508,
        STACK_OVERFLOW: 0x0503, STACK_UNDERFLOW: 0x0504,
        INVALID_FRAMEBUFFER_OPERATION: 0x0506,
        CLAMP: 0x2900, CLAMP_TO_BORDER: 0x812D, MIRRORED_REPEAT: 0x8370,
        BGR: 0x80E0, BGRA: 0x80E1,
        COLOR_INDEX: 0x1900, STENCIL_INDEX: 0x1901,
        COLOR_INDEX8_EXT: 0x80E5,
        INTENSITY: 0x8049, INTENSITY8: 0x804B, INTENSITY16: 0x804D,
        ALPHA4: 0x803B, ALPHA8: 0x803C, ALPHA12: 0x803D, ALPHA16: 0x803E,
        LUMINANCE4: 0x803F, LUMINANCE8: 0x8040,
        LUMINANCE12: 0x8041, LUMINANCE16: 0x8042,
        LUMINANCE4_ALPHA4: 0x8043, LUMINANCE8_ALPHA8: 0x8045,
        LUMINANCE12_ALPHA12: 0x8047, LUMINANCE16_ALPHA16: 0x8048,
        R3_G3_B2: 0x2A10, RGB4: 0x804F, RGB5: 0x8050,
        RGB10: 0x8052, RGB12: 0x8053, RGB16: 0x8054, RGB565: 0x8D62,
        RGBA2: 0x8055, RGBA4: 0x8056, RGB5_A1: 0x8057,
        RGB10_A2: 0x8059, RGBA12: 0x805A,
        HALF_FLOAT: 0x140B,
        UNSIGNED_BYTE_3_3_2: 0x8032, UNSIGNED_BYTE_2_3_3_REV: 0x8362,
        UNSIGNED_INT_10_10_10_2: 0x8036, UNSIGNED_INT_2_10_10_10_REV: 0x8368,
        RED_SCALE: 0x0D14, RED_BIAS: 0x0D15,
        GREEN_SCALE: 0x0D18, GREEN_BIAS: 0x0D19,
        BLUE_SCALE: 0x0D1A, BLUE_BIAS: 0x0D1B,
        ALPHA_SCALE: 0x0D1C, ALPHA_BIAS: 0x0D1D,
        MAP_COLOR: 0x0D10,
        MAP_STENCIL: 0x0D11, INDEX_SHIFT: 0x0D12, INDEX_OFFSET: 0x0D13,
        ZOOM_X: 0x0D16, ZOOM_Y: 0x0D17, DEPTH_SCALE: 0x0D1E, DEPTH_BIAS: 0x0D1F,
        LOGIC_OP_MODE: 0x0BF0,
        NORMALIZE: 0x0BA1, POLYGON_SMOOTH: 0x0B41, POLYGON_STIPPLE: 0x0B42,
        MULTISAMPLE: 0x809D, SAMPLE_ALPHA_TO_COVERAGE: 0x809E, SAMPLE_ALPHA_TO_ONE: 0x809F,
        AUTO_NORMAL: 0x0D80,
        SAMPLE_BUFFERS: 0x80A8, SAMPLES: 0x80A9,
        COLOR_SUM: 0x8458,
        FOG_COORD_SRC: 0x8450, FOG_COORD: 0x8451, FRAGMENT_DEPTH: 0x8452,
        FOG_COORD_ARRAY: 0x8457, SECONDARY_COLOR_ARRAY: 0x845E,
        NORMAL_MAP: 0x8511, REFLECTION_MAP: 0x8512,
        POINT_SIZE_MIN: 0x8126, POINT_SIZE_MAX: 0x8127,
        POINT_FADE_THRESHOLD_SIZE: 0x8128,
        POINT_DISTANCE_ATTENUATION: 0x8129,
        POINT_SPRITE: 0x8861, COORD_REPLACE: 0x8862,
        TEXTURE_FILTER_CONTROL: 0x8500, TEXTURE_LOD_BIAS: 0x8501,
        TEXTURE_COMPARE_MODE: 0x884C, TEXTURE_COMPARE_FUNC: 0x884D,
        COMPARE_R_TO_TEXTURE: 0x884E,
        TEXTURE_MAX_ANISOTROPY_EXT: 0x84FE, MAX_TEXTURE_LOD_BIAS: 0x84FD,
        DEPTH_CLAMP: 0x864F,
        ARRAY_BUFFER: 0x8892,
        ARRAY_BUFFER_BINDING: 0x8894,
        ELEMENT_ARRAY_BUFFER: 0x8893,
        ELEMENT_ARRAY_BUFFER_BINDING: 0x8895,
        STATIC_DRAW: 0x88E4,
        READ_FRAMEBUFFER: 0x8CA8, DRAW_FRAMEBUFFER: 0x8CA9,
        FRAMEBUFFER_BINDING: 0x8CA6, RENDERBUFFER_BINDING: 0x8CA7,
        MAX_RENDERBUFFER_SIZE: 0x84E8,
        COLOR_ATTACHMENT0: 0x8CE0, DEPTH_ATTACHMENT: 0x8D00,
        STENCIL_ATTACHMENT: 0x8D20, DEPTH_STENCIL_ATTACHMENT: 0x821A,
        MAX_COLOR_ATTACHMENTS: 0x8CDF,
        FRAMEBUFFER_COMPLETE: 0x8CD5,
        FRAMEBUFFER_INCOMPLETE_ATTACHMENT: 0x8CD6,
        FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: 0x8CD7,
        FRAMEBUFFER_UNDEFINED: 0x8219,
        MAX_ELEMENTS_VERTICES: 0x80E8, MAX_ELEMENTS_INDICES: 0x80E9,
        FLOAT_MAT2x3: 0x8B65, FLOAT_MAT2x4: 0x8B66,
        FLOAT_MAT3x2: 0x8B67, FLOAT_MAT3x4: 0x8B68,
        FLOAT_MAT4x2: 0x8B69, FLOAT_MAT4x3: 0x8B6A,
    };

    // The guest's own table wins wherever both define a name: it is what the
    // guest actually sends, and a disagreement should surface as a test
    // failure rather than as the host quietly using a different value.
    const GL_CONFLICTS = [];
    for (const key of Object.keys(GL_SUPPLEMENT)) {
        if (GL[key] === undefined) GL[key] = GL_SUPPLEMENT[key];
        else if (GL[key] !== GL_SUPPLEMENT[key])
            GL_CONFLICTS.push({ name: key, guest: GL[key],
                                registry: GL_SUPPLEMENT[key] });
    }

    const NAME_BY_OPCODE = protocol.NAME_BY_OPCODE;

    const api = { GLFN, CTRL, GL, NAME_BY_OPCODE, GL_SUPPLEMENT, GL_CONFLICTS };
    global.GLWGConstants = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// What the renderer can do, as OpenGL reports it (docs/opengl-webgpu-plan.md
// C3: the capabilities come from one place). The page's executor and the CPU
// side's shadow (gl_shadow.js) both answer from here. A capabilities object
// is { limits, features } as the WebGPU adapter reports them -- plain data:
// the renderer sends it to the device once its adapter is up, and the device
// keeps it in its snapshot, so a guest restored on another machine is told
// what it was told before.

(function(global) {
    "use strict";

    const constants = global.GLWGConstants || require("./gl_constants.js");
    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const GL = constants.GL;

    // Fixed by the executor's design, whatever the adapter
    const LIMITS = {
        MAX_TEXTURE_UNITS: 8,
        MAX_TEXTURE_COORDS: 8,
        MAX_TEXTURE_IMAGE_UNITS: 8,
        MAX_VERTEX_TEXTURE_IMAGE_UNITS: 8,
        MAX_COMBINED_TEXTURE_IMAGE_UNITS: 8,
        MAX_LIGHTS: 8,
        MAX_CLIP_PLANES: 6,
        MAX_VERTEX_ATTRIBS: 16,
        MAX_NAME_STACK_DEPTH: 64,
        MAX_EVAL_ORDER: protocol.C.V86GL_MAX_EVAL_ORDER,
        MAX_ATTRIB_STACK_DEPTH: 16,
        MAX_CLIENT_ATTRIB_STACK_DEPTH: 16,
        MAX_POINT_SIZE: 64,
        MAX_LINE_WIDTH: 64,
        MAX_DRAW_BUFFERS: 8,
        MAX_PIXEL_MAP_TABLE: protocol.C.V86GL_MAX_TYPED_QUERY_VALUES,
        MODELVIEW_STACK_DEPTH: 32,
        OTHER_STACK_DEPTH: 4,
        MAX_PROGRAM_MATRICES: 8,
        PROGRAM_MATRIX_STACK_DEPTH: 2,
        // gl_shader_translator.js's MAX_INTER_STAGE_SLOTS vec4s
        MAX_VARYING_FLOATS: 16 * 4,
    };

    const BASE_EXTENSIONS = [
        "GL_ARB_multitexture", "GL_ARB_texture_env_add",
        "GL_ARB_texture_env_combine", "GL_ARB_texture_env_dot3",
        "GL_ARB_texture_env_crossbar", "GL_ARB_texture_cube_map", "GL_ARB_texture_rectangle",
        "GL_ARB_texture_border_clamp", "GL_ARB_texture_mirrored_repeat",
        "GL_ARB_texture_compression", "GL_SGI_color_matrix",
        "GL_ARB_texture_non_power_of_two", "GL_ARB_depth_texture",
        "GL_ARB_shadow", "GL_ARB_point_parameters", "GL_ARB_point_sprite",
        "GL_ARB_transpose_matrix", "GL_ARB_window_pos",
        "GL_ARB_vertex_buffer_object", "GL_ARB_occlusion_query",
        "GL_ARB_multisample", "GL_ARB_shader_objects",
        "GL_ARB_vertex_shader", "GL_ARB_fragment_shader",
        "GL_ARB_shading_language_100", "GL_ARB_draw_buffers",
        "GL_EXT_texture3D", "GL_EXT_texture_lod_bias",
        "GL_EXT_texture_edge_clamp", "GL_EXT_blend_color",
        "GL_EXT_blend_minmax", "GL_EXT_blend_subtract",
        "GL_EXT_blend_func_separate", "GL_EXT_blend_equation_separate",
        "GL_EXT_stencil_wrap", "GL_EXT_stencil_two_side",
        "GL_EXT_secondary_color", "GL_EXT_fog_coord",
        "GL_EXT_compiled_vertex_array",
        "GL_EXT_packed_pixels", "GL_EXT_rescale_normal",
        "GL_EXT_separate_specular_color", "GL_EXT_bgra",
        "GL_EXT_framebuffer_object", "GL_EXT_framebuffer_blit",
        "GL_EXT_generate_mipmap",
        "GL_SGIS_generate_mipmap", "GL_SGIS_texture_edge_clamp",
        "GL_SGIS_texture_lod", "GL_NV_blend_square",
    ];

    /*
     * The capabilities to send: the adapter limits the answers use, and the
     * features. limits and features are the executor's (WebGPU's
     * GPUSupportedLimits, and its own feature flags).
     */
    function capabilities(limits, features) {
        limits = limits || {};
        features = features || {};
        return {
            limits: {
                maxTextureDimension2D: limits.maxTextureDimension2D || 8192,
                maxTextureDimension3D: limits.maxTextureDimension3D || 2048,
                maxColorAttachments: limits.maxColorAttachments || 8,
            },
            features: {
                bc: !!features.bc,
                float32Filterable: !!features.float32Filterable,
                depthClipControl: !!features.depthClipControl,
            },
        };
    }

    function extensions(caps) {
        if (caps.profile) return caps.profile.strings[GL.EXTENSIONS];
        const list = [...BASE_EXTENSIONS];
        // Only advertised when the adapter really has the BC formats: the
        // CPU decode path keeps the picture right, but a guest that sees
        // S3TC will hand us compressed data forever, and saying so honestly
        // lets it choose.
        if (caps.features.bc)
            list.push("GL_EXT_texture_compression_s3tc");
        if (caps.features.float32Filterable)
            list.push("GL_ARB_texture_float", "GL_ARB_half_float_pixel");
        if (caps.features.depthClipControl)
            list.push("GL_ARB_depth_clamp");
        list.push("GL_EXT_texture_filter_anisotropic");
        return list.join(" ");
    }

    // glGetString's answer, or undefined when pname is not a capability
    function string(caps, pname) {
        if (caps.profile) return caps.profile.strings[pname];
        switch (pname) {
        case GL.VENDOR: return "VMware, Inc.";
        case GL.RENDERER: return "SVGA3D; v86 WebGPU bridge";
        case GL.VERSION: return "2.1 v86gl (WebGPU)";
        case GL.SHADING_LANGUAGE_VERSION: return "1.20 v86gl";
        case GL.EXTENSIONS: return extensions(caps);
        default: return undefined;
        }
    }

    // The DLL chooses a WineD3D compatibility hint; the CPU owns the answer.
    // WineD3D 1.7.52 treats an unknown vendor as GeForce FX, disables NPOT,
    // and then selects rectangle textures. Preserve its known SVGA3D path.
    // A hint only hides capabilities; it never grants an unimplemented one.
    function profileString(caps, profile, pname) {
        if (!profile) return string(caps, pname);
        const C = protocol.C, legacy = profile === C.V86GL_CAPS_PROFILE_WINED3D_GL15 ||
            profile === C.V86GL_CAPS_PROFILE_WINED3D_ARB;
        switch (pname) {
        case GL.VENDOR: return "VMware, Inc.";
        case GL.RENDERER: return "SVGA3D; v86 WebGPU bridge";
        case GL.VERSION: return legacy ? "1.5 v86gl (WineD3D backbuffer profile)" : "2.1 v86gl (WebGPU)";
        case GL.SHADING_LANGUAGE_VERSION: return legacy ? undefined : string(caps, pname);
        case GL.EXTENSIONS:
            return extensions(caps).split(" ").filter(name => {
                if (legacy && ["GL_ARB_shader_objects", "GL_ARB_vertex_shader", "GL_ARB_fragment_shader", "GL_ARB_shading_language_100"].includes(name)) return false;
                if ((legacy || profile === C.V86GL_CAPS_PROFILE_GL21_NO_FBO) &&
                    ["GL_EXT_framebuffer_object", "GL_EXT_framebuffer_blit"].includes(name)) return false;
                if ((profile === C.V86GL_CAPS_PROFILE_WINED3D_GL15 || profile === C.V86GL_CAPS_PROFILE_GL21_FBO_FFP) &&
                    ["GL_ARB_vertex_program", "GL_ARB_fragment_program"].includes(name)) return false;
                return true;
            }).join(" ");
        default: return string(caps, pname);
        }
    }

    // glGetIntegerv's answer, or undefined when pname is not a capability
    function integer(caps, pname) {
        if (caps.profile) return caps.profile.integers[pname];
        const limits = caps.limits;
        const C = protocol.C;
        switch (pname) {
        case C.V86GL_QUERY_HOST_CAPABILITIES:
            return C.V86GL_HOST_CAP_WEBGL2 | C.V86GL_HOST_CAP_COLOR_BUFFER_FLOAT |
                (caps.features.float32Filterable ? C.V86GL_HOST_CAP_TEXTURE_FLOAT_LINEAR : 0) |
                C.V86GL_HOST_CAP_ANISOTROPY;
        case GL.MAX_TEXTURE_SIZE:
        case GL.MAX_RECTANGLE_TEXTURE_SIZE:
            return Math.min(16384, limits.maxTextureDimension2D);
        case GL.MAX_3D_TEXTURE_SIZE:
            return Math.min(2048, limits.maxTextureDimension3D);
        case GL.MAX_CUBE_MAP_TEXTURE_SIZE:
            return Math.min(16384, limits.maxTextureDimension2D);
        case GL.MAX_TEXTURE_UNITS: return LIMITS.MAX_TEXTURE_UNITS;
        case GL.MAX_PROGRAM_MATRICES_ARB: return LIMITS.MAX_PROGRAM_MATRICES;
        case GL.MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB: return LIMITS.PROGRAM_MATRIX_STACK_DEPTH;
        case GL.MAX_EVAL_ORDER: return LIMITS.MAX_EVAL_ORDER;
        case GL.MAX_ATTRIB_STACK_DEPTH: return LIMITS.MAX_ATTRIB_STACK_DEPTH;
        case GL.MAX_CLIENT_ATTRIB_STACK_DEPTH: return LIMITS.MAX_CLIENT_ATTRIB_STACK_DEPTH;
        case GL.MAX_COLOR_MATRIX_STACK_DEPTH_SGI: return LIMITS.OTHER_STACK_DEPTH;
        case GL.RGBA_MODE: return 1;
        case GL.STEREO: return 0;
        case GL.AUX_BUFFERS: case GL.INDEX_MODE: case GL.INDEX_BITS:
        case GL.ACCUM_RED_BITS: case GL.ACCUM_GREEN_BITS: case GL.ACCUM_BLUE_BITS: case GL.ACCUM_ALPHA_BITS: return 0;
        case GL.MAX_TEXTURE_COORDS: return LIMITS.MAX_TEXTURE_COORDS;
        case GL.MAX_TEXTURE_IMAGE_UNITS: return LIMITS.MAX_TEXTURE_IMAGE_UNITS;
        case GL.MAX_VERTEX_TEXTURE_IMAGE_UNITS: return LIMITS.MAX_VERTEX_TEXTURE_IMAGE_UNITS;
        case GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS: return LIMITS.MAX_COMBINED_TEXTURE_IMAGE_UNITS;
        case GL.MAX_VERTEX_ATTRIBS: return LIMITS.MAX_VERTEX_ATTRIBS;
        case GL.MAX_NAME_STACK_DEPTH: return LIMITS.MAX_NAME_STACK_DEPTH;
        case GL.MAX_VARYING_FLOATS: return LIMITS.MAX_VARYING_FLOATS;
        case GL.MAX_VERTEX_UNIFORM_COMPONENTS: return 1024;
        case GL.MAX_FRAGMENT_UNIFORM_COMPONENTS: return 1024;
        case GL.MAX_DRAW_BUFFERS:
        case GL.MAX_COLOR_ATTACHMENTS:
            return Math.min(LIMITS.MAX_DRAW_BUFFERS, limits.maxColorAttachments);
        case GL.MAX_RENDERBUFFER_SIZE:
            return Math.min(16384, limits.maxTextureDimension2D);
        case GL.MAX_LIGHTS: return LIMITS.MAX_LIGHTS;
        case GL.MAX_PIXEL_MAP_TABLE: return LIMITS.MAX_PIXEL_MAP_TABLE;
        case GL.MAX_CLIP_PLANES: return LIMITS.MAX_CLIP_PLANES;
        case GL.MAX_MODELVIEW_STACK_DEPTH: return LIMITS.MODELVIEW_STACK_DEPTH;
        case GL.MAX_PROJECTION_STACK_DEPTH: return LIMITS.OTHER_STACK_DEPTH;
        case GL.MAX_TEXTURE_STACK_DEPTH: return LIMITS.OTHER_STACK_DEPTH;
        case GL.MAX_TEXTURE_MAX_ANISOTROPY_EXT: return 16;
        case GL.MAX_TEXTURE_LOD_BIAS: return 16;
        case GL.MAX_ELEMENTS_VERTICES: return 65536;
        case GL.MAX_ELEMENTS_INDICES: return 65536;
        case GL.SAMPLE_BUFFERS: return 0;
        // GL_SAMPLES is zero when the selected framebuffer has no
        // multisample buffer; one is the raster sample count, not the GL
        // query value for this pixel format.
        case GL.SAMPLES: return 0;
        case GL.SUBPIXEL_BITS: return 8;
        case GL.RED_BITS: case GL.GREEN_BITS:
        case GL.BLUE_BITS: case GL.ALPHA_BITS: return 8;
        case GL.DEPTH_BITS: return 24;
        case GL.STENCIL_BITS: return 8;
        case GL.MAX_VIEWPORT_DIMS:
            return Math.min(16384, limits.maxTextureDimension2D);
        case GL.NUM_COMPRESSED_TEXTURE_FORMATS:
            return caps.features.bc ? 4 : 0;
        default: return undefined;
        }
    }

    function vector(caps, pname) {
        if (caps.profile?.vectors && Object.hasOwn(caps.profile.vectors, pname)) return caps.profile.vectors[pname];
        switch (pname) {
        case GL.POINT_SIZE_RANGE: case GL.ALIASED_POINT_SIZE_RANGE: return [1, LIMITS.MAX_POINT_SIZE];
        case GL.LINE_WIDTH_RANGE: case GL.ALIASED_LINE_WIDTH_RANGE: return [1, LIMITS.MAX_LINE_WIDTH];
        case GL.POINT_SIZE_GRANULARITY: case GL.LINE_WIDTH_GRANULARITY: return [1];
        case GL.COMPRESSED_TEXTURE_FORMATS:
            return [GL.COMPRESSED_RGB_S3TC_DXT1_EXT, GL.COMPRESSED_RGBA_S3TC_DXT1_EXT, GL.COMPRESSED_RGBA_S3TC_DXT3_EXT, GL.COMPRESSED_RGBA_S3TC_DXT5_EXT]
                .slice(0, integer(caps, GL.NUM_COMPRESSED_TEXTURE_FORMATS));
        default: return undefined;
        }
    }

    // Keep the virtual device's advertised values independent of later host
    // adapters and bridge revisions. Copy first: the renderer owns its input.
    function freeze(caps) {
        if (!caps) return null;
        const result = capabilities(caps.limits, caps.features);
        let integers = {}, strings = {}, vectors = {};
        if (caps.profile) {
            integers = { ...caps.profile.integers }; strings = { ...caps.profile.strings };
            // Values formerly answered by the DLL remain stable in saves
            // predating the CPU vector capability table.
            for (const [pname, value] of [[GL.MAX_ATTRIB_STACK_DEPTH, 16], [GL.MAX_CLIENT_ATTRIB_STACK_DEPTH, 16],
                [GL.MAX_COLOR_MATRIX_STACK_DEPTH_SGI, 2], [GL.MAX_PROGRAM_MATRICES_ARB, LIMITS.MAX_PROGRAM_MATRICES],
                [GL.MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB, LIMITS.PROGRAM_MATRIX_STACK_DEPTH], [GL.RGBA_MODE, 1], [GL.STEREO, 0]]) if (integers[pname] === undefined) integers[pname] = value;
            for (const pname of [GL.AUX_BUFFERS, GL.INDEX_MODE, GL.INDEX_BITS, GL.ACCUM_RED_BITS, GL.ACCUM_GREEN_BITS, GL.ACCUM_BLUE_BITS, GL.ACCUM_ALPHA_BITS])
                if (integers[pname] === undefined) integers[pname] = 0;
        } else {
            for (const pname of new Set([...Object.values(GL), protocol.C.V86GL_QUERY_HOST_CAPABILITIES])) {
                const value = integer(result, pname), text = string(result, pname);
                if (value !== undefined) integers[pname] = value;
                if (text !== undefined) strings[pname] = text;
            }
        }
        for (const pname of [GL.POINT_SIZE_RANGE, GL.ALIASED_POINT_SIZE_RANGE, GL.LINE_WIDTH_RANGE, GL.ALIASED_LINE_WIDTH_RANGE, GL.POINT_SIZE_GRANULARITY, GL.LINE_WIDTH_GRANULARITY, GL.COMPRESSED_TEXTURE_FORMATS])
            vectors[pname] = Object.freeze(Array.from(vector(caps, pname)));
        result.profile = Object.freeze({ version: 2, integers: Object.freeze(integers), strings: Object.freeze(strings), vectors: Object.freeze(vectors) });
        Object.freeze(result.limits); Object.freeze(result.features);
        return Object.freeze(result);
    }

    const api = { LIMITS, capabilities, extensions, string, profileString, integer, vector, freeze };
    global.GLCaps = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// glslang for the GL shadow (docs/opengl-webgpu-plan.md C4, the first step:
// glslang checks and reflects desktop GLSL 1.10/1.20 on the CPU side). The
// module is the GLES host's (bios/glslang.wasm, tools/glslang/build.sh); this
// is a classic script for the v86gl device and its CPU worker, with the
// desktop entry points of tools/glslang/v86_glslang.cpp. Synchronous once
// instantiated: the shadow answers glGetShaderiv and glGetProgramiv inside
// the request that asked.

(function(global) {
    "use strict";

    // glslang's EShLanguage, by GL's shader type
    const STAGE_OF_GL_TYPE = new Map([[0x8B31, 0], [0x8B30, 4]]);
    const ERRNO_NOSYS = 52;

    class Glslang {
        /*
         * module: a compiled WebAssembly.Module (the renderer compiles it
         * and sends it to the device: modules cross to workers), or the
         * bytes, compiled here synchronously (a worker or Node; browsers
         * refuse large synchronous compiles on the page's thread).
         */
        constructor(module) {
            if (!(module instanceof WebAssembly.Module)) module = new WebAssembly.Module(module);
            this.printed = "";
            this.limits = null;
            const instance = new WebAssembly.Instance(module, this.imports());
            this.exports = instance.exports;
            if (typeof this.exports.v86_glslang_desktop_link !== "function")
                throw new Error("glslang.wasm has no desktop entry points; rebuild it (tools/glslang/build.sh)");
            this.exports._initialize();
        }

        memory() {
            return new Uint8Array(this.exports.memory.buffer);
        }

        imports() {
            const view = () => new DataView(this.exports.memory.buffer);
            return {
                env: {
                    emscripten_notify_memory_growth: () => {},
                    __syscall_getcwd: () => -ERRNO_NOSYS,
                },
                wasi_snapshot_preview1: {
                    fd_write: (fd, iovs, count, written) => {
                        let total = 0;
                        for (let i = 0; i < count; i++) {
                            const pointer = view().getUint32(iovs + 8 * i, true);
                            const length = view().getUint32(iovs + 8 * i + 4, true);
                            this.printed += new TextDecoder().decode(this.memory().subarray(pointer, pointer + length));
                            total += length;
                        }
                        view().setUint32(written, total, true);
                        return 0;
                    },
                    fd_close: () => 0,
                    fd_seek: () => ERRNO_NOSYS,
                    environ_sizes_get: (count, size) => {
                        view().setUint32(count, 0, true);
                        view().setUint32(size, 0, true);
                        return 0;
                    },
                    environ_get: () => 0,
                },
            };
        }

        withBytes(bytes, call) {
            const pointer = this.exports.v86_glslang_alloc(Math.max(1, bytes.length));
            this.memory().set(bytes, pointer);
            try {
                return call(pointer);
            } finally {
                this.exports.v86_glslang_free(pointer);
            }
        }

        output() {
            const pointer = this.exports.v86_glslang_output(), size = this.exports.v86_glslang_output_size();
            return new TextDecoder().decode(this.memory().subarray(pointer, pointer + size));
        }

        // The limits gl_Max* and the checks see, by glslangValidator's names
        setLimits(limits) {
            const text = Object.entries(limits).map(([name, value]) => name + " " + value).join("\n") + "\n";
            if (text === this.limits) return;
            this.limits = text;
            this.withBytes(new TextEncoder().encode(text + "\0"), pointer => this.exports.v86_glslang_desktop_limits(pointer));
        }

        // [{stage, source}] as the module takes them
        static pack(shaders) {
            const encoded = shaders.map(s => new TextEncoder().encode(s.source));
            const bytes = new Uint8Array(encoded.reduce((n, e) => n + 8 + (e.length + 3 & ~3), 0));
            const view = new DataView(bytes.buffer);
            let at = 0;
            shaders.forEach((s, i) => {
                view.setUint32(at, s.stage, true);
                view.setUint32(at + 4, encoded[i].length, true);
                bytes.set(encoded[i], at + 8);
                at += 8 + (encoded[i].length + 3 & ~3);
            });
            return bytes;
        }

        // One shader: {ok, log}
        compile(stage, source) {
            const packed = Glslang.pack([{ stage, source }]);
            const ok = this.withBytes(packed, pointer => this.exports.v86_glslang_desktop_compile(pointer, packed.length));
            return { ok: !!ok, log: this.output() };
        }

        // A program from any number of shaders a stage: {ok, log} and, when
        // it linked, the reflection (uniforms, inputs, outputs, ...)
        link(shaders) {
            const packed = Glslang.pack(shaders);
            this.withBytes(packed, pointer => this.exports.v86_glslang_desktop_link(pointer, packed.length));
            return JSON.parse(this.output());
        }
    }

    const api = { Glslang, STAGE_OF_GL_TYPE };
    global.V86GLGlslang = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Shared column-major GL matrix operations for CPU state and rendering.
(function(global) {
    "use strict";
    function identity4(out) {
        const m = out || new Float32Array(16);
        m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 0;
        m[4] = 0; m[5] = 1; m[6] = 0; m[7] = 0;
        m[8] = 0; m[9] = 0; m[10] = 1; m[11] = 0;
        m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
        return m;
    }

    function multiply4(a, b, out) {
        const m = out || new Float32Array(16);
        for (let c = 0; c < 4; ++c) {
            const b0 = b[c * 4], b1 = b[c * 4 + 1];
            const b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
            m[c * 4] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
            m[c * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
            m[c * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
            m[c * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
        }
        return m;
    }

    function invert4(m, out) {
        const inv = out || new Float32Array(16);
        const a = m;
        inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] +
                 a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] -
                 a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] +
                 a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] -
                  a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] -
                 a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] +
                 a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] -
                 a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] +
                  a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] +
                 a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
        inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] -
                 a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
        inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] +
                  a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
        inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] -
                  a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
        inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] -
                 a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
        inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] +
                 a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
        inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] -
                  a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
        inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] +
                  a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
        let det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
        if (det === 0) return identity4(inv);
        det = 1.0 / det;
        for (let i = 0; i < 16; ++i) inv[i] *= det;
        return inv;
    }

    function transpose4(m, out) {
        const t = out || new Float32Array(16);
        for (let c = 0; c < 4; ++c)
            for (let r = 0; r < 4; ++r)
                t[r * 4 + c] = m[c * 4 + r];
        return t;
    }

    /* gl_NormalMatrix is the inverse transpose of the modelview's upper 3x3. */
    function normalMatrixOf(modelview, out) {
        const n = out || new Float32Array(9);
        const a = modelview;
        const m00 = a[0], m01 = a[4], m02 = a[8];
        const m10 = a[1], m11 = a[5], m12 = a[9];
        const m20 = a[2], m21 = a[6], m22 = a[10];
        const c00 = m11 * m22 - m12 * m21;
        const c01 = m12 * m20 - m10 * m22;
        const c02 = m10 * m21 - m11 * m20;
        let det = m00 * c00 + m01 * c01 + m02 * c02;
        if (det === 0) {
            n[0] = 1; n[1] = 0; n[2] = 0;
            n[3] = 0; n[4] = 1; n[5] = 0;
            n[6] = 0; n[7] = 0; n[8] = 1;
            return n;
        }
        det = 1 / det;
        // The inverse transpose is the cofactor matrix over the determinant:
        // element (r, c) is cofactor (r, c), stored column-major at c * 3 + r
        // (review S2-1: these were the inverse's, the cofactors transposed)
        n[0] = c00 * det;
        n[1] = (m02 * m21 - m01 * m22) * det;
        n[2] = (m01 * m12 - m02 * m11) * det;
        n[3] = c01 * det;
        n[4] = (m00 * m22 - m02 * m20) * det;
        n[5] = (m02 * m10 - m00 * m12) * det;
        n[6] = c02 * det;
        n[7] = (m01 * m20 - m00 * m21) * det;
        n[8] = (m00 * m11 - m01 * m10) * det;
        return n;
    }

    function transformPoint4(m, v, out) {
        const o = out || new Float32Array(4);
        o[0] = m[0]*v[0] + m[4]*v[1] + m[8]*v[2] + m[12]*v[3];
        o[1] = m[1]*v[0] + m[5]*v[1] + m[9]*v[2] + m[13]*v[3];
        o[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14]*v[3];
        o[3] = m[3]*v[0] + m[7]*v[1] + m[11]*v[2] + m[15]*v[3];
        return o;
    }

    const api = { identity4, multiply4, invert4, transpose4, normalMatrixOf, transformPoint4 };
    global.GLMath = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Shared packed client-array decoding for the CPU state machine and renderer.
(function(global) {
    "use strict";
    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    function parseArrayBlocks(bytes, view, offset, end, count) {
        const blocks = [];
        const R = protocol.F.CLIENT_ARRAY_BLOCK, header = protocol.RECORDS.CLIENT_ARRAY_BLOCK.size;
        let at = offset;
        for (let i = 0; i < count; ++i) {
            if (at + header > end) return null;
            const enabled = view.getUint32(at + R.enabled, true) !== 0;
            const size = view.getInt32(at + R.size, true);
            const type = view.getUint32(at + R.type, true);
            const stride = view.getInt32(at + R.stride, true);
            const dataSize = view.getUint32(at + R.data_size, true);
            at += header;
            if (at + dataSize > end) return null;
            blocks.push({
                enabled, size: enabled ? size : 0, type: enabled ? type : 0,
                stride: enabled ? stride : 0,
                data: enabled && dataSize ?
                    bytes.subarray(at, at + dataSize) : null,
            });
            at += dataSize;
        }
        return { blocks, offset: at };
    }

    function parseGenericBlocks(bytes, view, offset, end, count) {
        const blocks = [];
        const R = protocol.F.GENERIC_ATTRIB_BLOCK, header = protocol.RECORDS.GENERIC_ATTRIB_BLOCK.size;
        let at = offset;
        for (let i = 0; i < count; ++i) {
            if (at + header > end) return null;
            const index = view.getUint32(at + R.index, true);
            const normalized = view.getUint32(at + R.normalized, true) !== 0;
            const enabled = view.getUint32(at + R.enabled, true) !== 0;
            const size = view.getInt32(at + R.size, true);
            const type = view.getUint32(at + R.type, true);
            const stride = view.getInt32(at + R.stride, true);
            const dataSize = view.getUint32(at + R.data_size, true);
            at += header;
            if (at + dataSize > end) return null;
            blocks.push({
                index, normalized, enabled, size, type, stride,
                data: enabled && dataSize ?
                    bytes.subarray(at, at + dataSize) : null,
            });
            at += dataSize;
        }
        return { blocks, offset: at };
    }

    function componentBytes(type) {
        switch (type) {
        case GL.BYTE: case GL.UNSIGNED_BYTE: return 1;
        case GL.SHORT: case GL.UNSIGNED_SHORT: case GL.HALF_FLOAT: return 2;
        case GL.INT: case GL.UNSIGNED_INT: case GL.FLOAT: return 4;
        case GL.DOUBLE: return 8;
        default: return 0;
        }
    }

    function readComponent(view, at, type, normalized) {
        switch (type) {
        case GL.FLOAT: return view.getFloat32(at, true);
        case GL.DOUBLE: return view.getFloat64(at, true);
        case GL.BYTE: {
            const v = view.getInt8(at);
            return normalized ? (2 * v + 1) / 255 : v;
        }
        case GL.UNSIGNED_BYTE: {
            const v = view.getUint8(at);
            return normalized ? v / 255 : v;
        }
        case GL.SHORT: {
            const v = view.getInt16(at, true);
            return normalized ? (2 * v + 1) / 65535 : v;
        }
        case GL.UNSIGNED_SHORT: {
            const v = view.getUint16(at, true);
            return normalized ? v / 65535 : v;
        }
        case GL.INT: {
            const v = view.getInt32(at, true);
            return normalized ? (2 * v + 1) / 4294967295 : v;
        }
        case GL.UNSIGNED_INT: {
            const v = view.getUint32(at, true);
            return normalized ? v / 4294967295 : v;
        }
        default: return 0;
        }
    }
    function decode(name, bytes, view, offset, size) {
        const indexed = name.includes("ELEMENTS"), gl2 = name.endsWith("GL2"), baseName = indexed ? "DRAW_ELEMENTS" : "DRAW_ARRAYS";
        const base = protocol.RECORDS[baseName], end = offset + size;
        if (size < base.size) throw new Error("packed draw header is short");
        const r = protocol.decodeRecord(baseName, view, offset), magic = offset + base.size + 4 <= end ? view.getUint32(offset + base.size, true) : 0;
        let indexAt, count = 4, units = 0, secondary = false, fog = false, generic = 0;
        if (magic === protocol.C.CLIENT_ARRAY_MT_MAGIC) {
            const mt = "PACKED_" + baseName + (gl2 ? "_GL2_MT" : "_MT");
            if (size < protocol.RECORDS[mt].size) throw new Error("packed multitexture header is short");
            const fields = protocol.decodeRecord(mt, view, offset);
            secondary = !!(fields.tex_unit_count & protocol.C.CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT);
            fog = !!(fields.tex_unit_count & protocol.C.CLIENT_ARRAY_MT_FOG_COORD_BIT);
            units = fields.tex_unit_count & ~(protocol.C.CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT | protocol.C.CLIENT_ARRAY_MT_FOG_COORD_BIT);
            if (units > 8 || fields.generic_attrib_count > 16) throw new Error("packed array count exceeds the profile");
            generic = gl2 ? fields.generic_attrib_count : 0; indexAt = offset + protocol.RECORDS[mt].size;
            count = 3 + units + +secondary + +fog;
        } else indexAt = offset + 16;
        const dataSize = indexed ? r.index_data_size : 0;
        if (indexed && indexAt + dataSize > end) throw new Error("index data is truncated");
        const at = indexed ? indexAt + dataSize : magic === protocol.C.CLIENT_ARRAY_MT_MAGIC ? indexAt : offset + 8;
        const parsed = parseArrayBlocks(bytes, view, at, end, count);
        if (!parsed) throw new Error("client array blocks are truncated");
        const order = magic === protocol.C.CLIENT_ARRAY_MT_MAGIC ? ["vertex", "color", "normal",
            ...Array.from({ length: units }, (_, i) => "texCoord" + i), ...(secondary ? ["secondaryColor"] : []), ...(fog ? ["fogCoord"] : [])] :
            ["vertex", "color", "texCoord0", "normal"];
        const arrays = Object.fromEntries(order.flatMap((key, i) => parsed.blocks[i]?.enabled ? [[key, parsed.blocks[i]]] : []));
        if (generic) {
            const result = parseGenericBlocks(bytes, view, parsed.offset, end, generic);
            if (!result) throw new Error("generic attribute blocks are truncated");
            for (const block of result.blocks) if (block.enabled) arrays["generic" + block.index] = block;
        }
        return { mode: r.mode, count: r.count, arrays, indices: indexed ? { type: r.type, data: bytes.subarray(indexAt, indexAt + dataSize) } : null };
    }
    function attribute(block, index, components, normalized = false) {
        if (!block?.data) return null;
        const bytes = componentBytes(block.type), size = block.size, stride = block.stride || bytes * size;
        const start = index * stride;
        if (!bytes || size < 1 || size > 4 || start < 0 || start + size * bytes > block.data.length) throw new Error("array element is outside its captured data");
        const view = new DataView(block.data.buffer, block.data.byteOffset, block.data.length);
        return Array.from({ length: components }, (_, i) => i < size ? readComponent(view, start + i * bytes, block.type, block.normalized ?? normalized) : i === 3 ? 1 : 0);
    }
    const immediateAttributes = new Set(("COLOR4F NORMAL3F TEX_COORD2F TEX_COORD4F MULTI_TEX_COORD4F " +
        "SECONDARY_COLOR3F FOG_COORDF EDGE_FLAG VERTEX_ATTRIB4F INDEX_STATE").split(" "));
    function parseImmediateBlock(bytes, view, offset, size) {
        const header = protocol.RECORDS.IMMEDIATE_VERTEX_BLOCK.size, end = offset + size, runs = [];
        if (size < header || size > protocol.C.V86GL_IMMEDIATE_BLOCK_BYTES) return null;
        const r = protocol.decodeRecord("IMMEDIATE_VERTEX_BLOCK", view, offset);
        if (r.data_size !== size - header || r.run_count > r.data_size / 8) return null;
        let at = offset + header;
        for (let i = 0; i < r.run_count; ++i) {
            if (at + 8 > end) return null;
            const attributes = view.getUint32(at, true), count = view.getUint32(at + 4, true);at += 8;
            const vertices = at + attributes, next = vertices + count * 16;
            if (next > end) return null;
            let p = at;
            while (p < vertices) {
                if (p + 4 > vertices) return null;
                const fn = view.getUint16(p, true), length = view.getUint16(p + 2, true), name = protocol.NAME_BY_OPCODE[fn];p += 4;
                const expected = protocol.SIGNATURES[name] ? protocol.payloadBytes(protocol.SIGNATURES[name][1]) : protocol.RECORDS[name]?.size;
                if (!immediateAttributes.has(name) || length !== expected || p + length > vertices ||
                    name === "VERTEX_ATTRIB4F" && view.getUint32(p, true) === 0 || name === "INDEX_STATE" && view.getUint32(p, true) !== 0) return null;
                p += length;
            }
            runs.push({ attributes: bytes.subarray(at, vertices), positions: bytes.subarray(vertices, next), count });at = next;
        }
        return at === end ? runs : null;
    }
    function immediateRecord(runs) {
        const size = 8 + runs.reduce((sum, run) => sum + 8 + run.attributes.length + run.positions.length, 0), extended = size >= 0xFFFF,
            bytes = new Uint8Array(size + (extended ? 8 : 4)), view = new DataView(bytes.buffer), base = bytes.length - size;
        view.setUint16(0, protocol.GLFN.IMMEDIATE_VERTEX_BLOCK, true);view.setUint16(2, extended ? 0xFFFF : size, true);if (extended) view.setUint32(4, size, true);
        view.setUint32(base, runs.length, true);view.setUint32(base + 4, size - 8, true);let at = base + 8;
        for (const run of runs) {
            view.setUint32(at, run.attributes.length, true);view.setUint32(at + 4, run.positions.length / 16, true);at += 8;
            bytes.set(run.attributes, at);at += run.attributes.length;bytes.set(run.positions, at);at += run.positions.length;
        }
        return bytes;
    }
    const api = { parseArrayBlocks, parseGenericBlocks, componentBytes, readComponent, decode, attribute, parseImmediateBlock, immediateRecord };
    global.GLClientArrays = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Image storage decisions shared by the CPU metadata and the renderer.
(function(global) {
    "use strict";
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const PROXY_TARGETS = new Map([[GL.PROXY_TEXTURE_1D, GL.TEXTURE_1D], [GL.PROXY_TEXTURE_2D, GL.TEXTURE_2D],
        [GL.PROXY_TEXTURE_3D, GL.TEXTURE_3D], [GL.PROXY_TEXTURE_CUBE_MAP, GL.TEXTURE_CUBE_MAP], [GL.PROXY_TEXTURE_RECTANGLE, GL.TEXTURE_RECTANGLE]]);
    const BASE_INTERNAL_FORMATS = {
        [GL.ALPHA]: "ALPHA", [GL.ALPHA4]: "ALPHA", [GL.ALPHA8]: "ALPHA",
        [GL.ALPHA12]: "ALPHA", [GL.ALPHA16]: "ALPHA",
        [GL.LUMINANCE]: "LUMINANCE", [GL.LUMINANCE4]: "LUMINANCE",
        [GL.LUMINANCE8]: "LUMINANCE", [GL.LUMINANCE12]: "LUMINANCE",
        [GL.LUMINANCE16]: "LUMINANCE",
        [GL.LUMINANCE_ALPHA]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE4_ALPHA4]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE8_ALPHA8]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE12_ALPHA12]: "LUMINANCE_ALPHA",
        [GL.LUMINANCE16_ALPHA16]: "LUMINANCE_ALPHA",
        0x8044: "LUMINANCE_ALPHA", 0x8046: "LUMINANCE_ALPHA", // LUMINANCE6_ALPHA2 / LUMINANCE12_ALPHA4
        [GL.INTENSITY]: "INTENSITY", [GL.INTENSITY8]: "INTENSITY",
        [GL.INTENSITY16]: "INTENSITY",
        0x804A: "INTENSITY", 0x804C: "INTENSITY", // INTENSITY4/12
        [GL.RGB]: "RGB", [GL.RGB4]: "RGB", [GL.RGB5]: "RGB", [GL.RGB8]: "RGB",
        [GL.RGB10]: "RGB", [GL.RGB12]: "RGB", [GL.RGB16]: "RGB",
        [GL.R3_G3_B2]: "RGB", [GL.RGB565]: "RGB",
        [GL.RGBA]: "RGBA", [GL.RGBA2]: "RGBA", [GL.RGBA4]: "RGBA",
        [GL.RGB5_A1]: "RGBA", [GL.RGBA8]: "RGBA", [GL.RGB10_A2]: "RGBA",
        [GL.RGBA12]: "RGBA", [GL.RGBA16]: "RGBA",
        [GL.BGR]: "RGB", [GL.BGRA]: "RGBA",
        3: "RGB", 4: "RGBA", 1: "LUMINANCE", 2: "LUMINANCE_ALPHA",
        [GL.DEPTH_COMPONENT]: "DEPTH",
        [GL.DEPTH_COMPONENT16]: "DEPTH",
        [GL.DEPTH_COMPONENT24]: "DEPTH",
        [GL.DEPTH_COMPONENT32]: "DEPTH",
        [GL.SRGB]: "RGB", [GL.SRGB8]: "RGB",
        [GL.SRGB_ALPHA]: "RGBA", [GL.SRGB8_ALPHA8]: "RGBA",
        [GL.RGBA16F_ARB]: "RGBA", [GL.RGBA32F_ARB]: "RGBA",
        [GL.RGB16F_ARB]: "RGB", [GL.RGB32F_ARB]: "RGB",
        [GL.ALPHA16F_ARB]: "ALPHA", [GL.ALPHA32F_ARB]: "ALPHA",
        [GL.LUMINANCE16F_ARB]: "LUMINANCE", [GL.LUMINANCE32F_ARB]: "LUMINANCE",
        [GL.LUMINANCE_ALPHA16F_ARB]: "LUMINANCE_ALPHA", [GL.LUMINANCE_ALPHA32F_ARB]: "LUMINANCE_ALPHA",
        0x881D: "INTENSITY", 0x8817: "INTENSITY", // INTENSITY16F/32F_ARB
        [GL.SLUMINANCE]: "LUMINANCE", [GL.SLUMINANCE8]: "LUMINANCE",
        [GL.SLUMINANCE_ALPHA]: "LUMINANCE_ALPHA", [GL.SLUMINANCE8_ALPHA8]: "LUMINANCE_ALPHA",
        [GL.COMPRESSED_ALPHA]: "ALPHA", [GL.COMPRESSED_LUMINANCE]: "LUMINANCE",
        [GL.COMPRESSED_LUMINANCE_ALPHA]: "LUMINANCE_ALPHA", [GL.COMPRESSED_INTENSITY]: "INTENSITY",
        [GL.COMPRESSED_RGB]: "RGB", [GL.COMPRESSED_RGBA]: "RGBA",
        [GL.COMPRESSED_SRGB]: "RGB", [GL.COMPRESSED_SRGB_ALPHA]: "RGBA",
        [GL.COMPRESSED_SLUMINANCE]: "LUMINANCE", [GL.COMPRESSED_SLUMINANCE_ALPHA]: "LUMINANCE_ALPHA",
        [GL.COLOR_INDEX]: "COLOR_INDEX",
        [GL.COLOR_INDEX8_EXT]: "COLOR_INDEX",
        0x80E2: "COLOR_INDEX", 0x80E3: "COLOR_INDEX", 0x80E4: "COLOR_INDEX", // COLOR_INDEX1/2/4_EXT
        0x80E6: "COLOR_INDEX", 0x80E7: "COLOR_INDEX", // COLOR_INDEX12/16_EXT
    };

    const COMPRESSED_FORMATS = {
        [GL.COMPRESSED_RGB_S3TC_DXT1_EXT]:
            { gpu: "bc1-rgba-unorm", blockBytes: 8, base: "RGB", dxt: 1 },
        [GL.COMPRESSED_RGBA_S3TC_DXT1_EXT]:
            { gpu: "bc1-rgba-unorm", blockBytes: 8, base: "RGBA", dxt: 1 },
        [GL.COMPRESSED_RGBA_S3TC_DXT3_EXT]:
            { gpu: "bc2-rgba-unorm", blockBytes: 16, base: "RGBA", dxt: 3 },
        [GL.COMPRESSED_RGBA_S3TC_DXT5_EXT]:
            { gpu: "bc3-rgba-unorm", blockBytes: 16, base: "RGBA", dxt: 5 },
    };

    // GL 2.1 section 3.8.1 makes only the legacy component counts aliases
    // of the corresponding base formats. A base format and a sized format
    // remain distinct for mip/cube completeness (section 3.8.10).
    function textureInternalFormatKey(format) {
        return [0, GL.LUMINANCE, GL.LUMINANCE_ALPHA, GL.RGB, GL.RGBA][format] || format;
    }

    function storageFormatFor(internalFormat, features) {
        const compressed = COMPRESSED_FORMATS[internalFormat];
        if (compressed) {
            // Without the BC feature the blocks are decoded on the CPU rather
            // than the extension being withdrawn, because the guest may have
            // already been told S3TC exists. The decode is deterministic, so
            // the picture is identical; only the memory cost differs.
            return features && features.bc ?
                { gpu: compressed.gpu, compressed: true, blockBytes: compressed.blockBytes,
                  base: compressed.base, dxt: compressed.dxt } :
                { gpu: "rgba8unorm", compressed: false, decodeDXT: compressed.dxt,
                  base: compressed.base, blockBytes: compressed.blockBytes };
        }
        const base = BASE_INTERNAL_FORMATS[internalFormat];
        switch (internalFormat) {
        case GL.DEPTH_COMPONENT:
        case GL.DEPTH_COMPONENT24:
            // depth24plus has an implementation-defined texel layout and cannot
            // receive queue.writeTexture data. depth32float preserves uploaded
            // depth values and remains renderable/sampleable on core WebGPU.
            return { gpu: "depth32float", depth: true, base: "DEPTH" };
        case GL.DEPTH_COMPONENT16:
            return { gpu: "depth16unorm", depth: true, base: "DEPTH" };
        case GL.DEPTH_COMPONENT32:
            return { gpu: "depth32float", depth: true, base: "DEPTH" };
        case GL.SRGB:
        case GL.SRGB8:
        case GL.SRGB_ALPHA:
        case GL.SRGB8_ALPHA8:
        case GL.SLUMINANCE:
        case GL.SLUMINANCE8:
        case GL.SLUMINANCE_ALPHA:
        case GL.SLUMINANCE8_ALPHA8:
        case GL.COMPRESSED_SRGB:
        case GL.COMPRESSED_SRGB_ALPHA:
        case GL.COMPRESSED_SLUMINANCE:
        case GL.COMPRESSED_SLUMINANCE_ALPHA:
            return { gpu: "rgba8unorm-srgb", base: base || "RGBA" };
        case GL.RGBA16F_ARB:
        case GL.RGB16F_ARB:
        case GL.ALPHA16F_ARB:
        case GL.LUMINANCE16F_ARB:
        case GL.LUMINANCE_ALPHA16F_ARB:
        case 0x881D:
            return { gpu: "rgba16float", base: base || "RGBA", float: true };
        case GL.RGBA32F_ARB:
        case GL.RGB32F_ARB:
        case GL.ALPHA32F_ARB:
        case GL.LUMINANCE32F_ARB:
        case GL.LUMINANCE_ALPHA32F_ARB:
        case 0x8817:
            return { gpu: "rgba32float", base: base || "RGBA", float: true };
        case GL.COLOR_INDEX:
        case GL.COLOR_INDEX8_EXT:
        case 0x80E2:
        case 0x80E3:
        case 0x80E4:
        case 0x80E6:
        case 0x80E7:
            // Palettised textures keep their index on the GPU and are resolved
            // in the shader, the same way the D3D9 path handles P8.
            return { gpu: "r8uint", base: "COLOR_INDEX", palettised: true };
        default:
            return { gpu: "rgba8unorm", base: base || "RGBA",
                ...(SIZED_BITS[internalFormat] ? { bits: SIZED_BITS[internalFormat] } : {}) };
        }
    }

    /*
     * The bits a sized internal format keeps per channel (R, G, B, A), when
     * fewer than eight: the image is stored at that precision as GL does
     * (review S2-22: RGB5_A1 kept an alpha of 100 that has one bit)
     */
    const SIZED_BITS = {
        [GL.R3_G3_B2]: [3, 3, 2, 8], [GL.RGB4]: [4, 4, 4, 8], [GL.RGB5]: [5, 5, 5, 8],
        [GL.RGBA2]: [2, 2, 2, 2], [GL.RGBA4]: [4, 4, 4, 4], [GL.RGB5_A1]: [5, 5, 5, 1],
        [GL.ALPHA4]: [8, 8, 8, 4], [GL.LUMINANCE4]: [4, 4, 4, 8], [GL.LUMINANCE4_ALPHA4]: [4, 4, 4, 4],
        0x804A: [4, 4, 4, 4],
    };


    function renderbufferFormatFor(format, features) {
        const stencil = [GL.STENCIL_INDEX, GL.STENCIL_INDEX1_EXT, GL.STENCIL_INDEX4_EXT, GL.STENCIL_INDEX8_EXT, GL.STENCIL_INDEX16_EXT];
        if (stencil.includes(format)) return { gpu: "depth24plus-stencil8", base: "STENCIL", components: [0, 0, 0, 0, 0, 8] };
        if ([GL.DEPTH_STENCIL, GL.DEPTH24_STENCIL8].includes(format))
            return { gpu: "depth24plus-stencil8", base: "DEPTH_STENCIL", components: [0, 0, 0, 0, 24, 8] };
        if ([GL.DEPTH_COMPONENT, GL.DEPTH_COMPONENT16, GL.DEPTH_COMPONENT24, GL.DEPTH_COMPONENT32, GL.DEPTH_COMPONENT32F].includes(format)) {
            const bits = format === GL.DEPTH_COMPONENT16 ? 16 : 32;
            return { gpu: bits === 16 ? "depth16unorm" : "depth32float", base: "DEPTH", depth: true,
                internalFormat: bits === 16 ? GL.DEPTH_COMPONENT16 : format === GL.DEPTH_COMPONENT32F ? format : GL.DEPTH_COMPONENT32,
                components: [0, 0, 0, 0, bits, 0] };
        }
        const base = BASE_INTERNAL_FORMATS[format];
        if (!["RGB", "RGBA"].includes(base) || format < 5 || [GL.BGR, GL.BGRA, GL.RGB565].includes(format)) return null;
        const storage = storageFormatFor(format, features);
        const bits = storage.gpu === "rgba32float" ? 32 : storage.gpu === "rgba16float" ? 16 : 8;
        return { ...storage, components: [bits, bits, bits, base === "RGBA" ? bits : 0, 0, 0] };
    }

    // Effective component resolutions, including the low-bit quantization
    // performed before uploading to RGBA8 storage. Missing channels stay zero.
    function textureComponents(format, compressed, features) {
        const storage = storageFormatFor(format, features), base = storage.base;
        const dxt = compressed && COMPRESSED_FORMATS[format];
        if (dxt) return [5, 6, 5, base === "RGBA" ? dxt.dxt === 1 ? 1 : dxt.dxt === 3 ? 4 : 8 : 0, 0, 0, 0, 0, 0];
        if (storage.depth) return [0, 0, 0, 0, 0, 0, storage.gpu === "depth16unorm" ? 16 : 32, 0, 0];
        if (storage.palettised) return [0, 0, 0, 0, 0, 0, 0, 0, 8];
        const n = storage.gpu === "rgba16float" ? 16 : storage.gpu === "rgba32float" ? 32 : 8;
        const bits = storage.bits || [n, n, n, n];
        return [base === "RGB" || base === "RGBA" ? bits[0] : 0,
            base === "RGB" || base === "RGBA" ? bits[1] : 0,
            base === "RGB" || base === "RGBA" ? bits[2] : 0,
            ["RGBA", "ALPHA", "LUMINANCE_ALPHA"].includes(base) ? bits[3] : 0,
            base === "LUMINANCE" || base === "LUMINANCE_ALPHA" ? bits[0] : 0,
            base === "INTENSITY" ? bits[0] : 0, 0, 0, 0];
    }

    const UNCOMPRESSED_HINTS = new Map([
        [GL.COMPRESSED_ALPHA, GL.ALPHA], [GL.COMPRESSED_LUMINANCE, GL.LUMINANCE],
        [GL.COMPRESSED_LUMINANCE_ALPHA, GL.LUMINANCE_ALPHA], [GL.COMPRESSED_INTENSITY, GL.INTENSITY],
        [GL.COMPRESSED_RGB, GL.RGB], [GL.COMPRESSED_RGBA, GL.RGBA],
        [GL.COMPRESSED_SRGB, GL.SRGB], [GL.COMPRESSED_SRGB_ALPHA, GL.SRGB_ALPHA],
        [GL.COMPRESSED_SLUMINANCE, GL.SLUMINANCE], [GL.COMPRESSED_SLUMINANCE_ALPHA, GL.SLUMINANCE_ALPHA],
    ]);
    function uncompressedInternalFormat(format) {
        const block = COMPRESSED_FORMATS[format];
        return block ? block.base === "RGB" ? GL.RGB : GL.RGBA : UNCOMPRESSED_HINTS.get(format) || format;
    }

    const api = { BASE_INTERNAL_FORMATS, COMPRESSED_FORMATS, SIZED_BITS, PROXY_TARGETS, textureInternalFormatKey, storageFormatFor,
        renderbufferFormatFor, textureComponents, uncompressedInternalFormat };
    global.GLImageFormats = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Desktop texture parameter defaults and typed setter conversion.
(function(global) {
    "use strict";
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const targets = [GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_3D, GL.TEXTURE_CUBE_MAP, GL.TEXTURE_RECTANGLE];
    const definitions = new Map();
    const add = (p, value, kind, field, allowed) => definitions.set(p, { initial: Array.isArray(value) ? value : [value], kind, field, allowed });
    const wraps = [GL.CLAMP, GL.CLAMP_TO_EDGE, GL.CLAMP_TO_BORDER, GL.REPEAT, GL.MIRRORED_REPEAT];
    add(GL.TEXTURE_MIN_FILTER, GL.NEAREST_MIPMAP_LINEAR, "e", "minFilter", [GL.NEAREST, GL.LINEAR,
        GL.NEAREST_MIPMAP_NEAREST, GL.NEAREST_MIPMAP_LINEAR, GL.LINEAR_MIPMAP_NEAREST, GL.LINEAR_MIPMAP_LINEAR]);
    add(GL.TEXTURE_MAG_FILTER, GL.LINEAR, "e", "magFilter", [GL.NEAREST, GL.LINEAR]);
    for (const [p, field] of [[GL.TEXTURE_WRAP_S, "wrapS"], [GL.TEXTURE_WRAP_T, "wrapT"], [GL.TEXTURE_WRAP_R, "wrapR"]])
        add(p, GL.REPEAT, "e", field, wraps);
    add(GL.TEXTURE_BORDER_COLOR, [0, 0, 0, 0], "n", "borderColor");
    add(GL.TEXTURE_PRIORITY, 1, "f");
    add(GL.TEXTURE_RESIDENT, 1, "b");
    add(GL.TEXTURE_MIN_LOD, -1000, "f", "minLod"); add(GL.TEXTURE_MAX_LOD, 1000, "f", "maxLod");
    add(GL.TEXTURE_BASE_LEVEL, 0, "i", "baseLevel"); add(GL.TEXTURE_MAX_LEVEL, 1000, "i", "maxLevel");
    add(GL.TEXTURE_LOD_BIAS, 0, "f", "lodBias");
    add(GL.DEPTH_TEXTURE_MODE, GL.LUMINANCE, "e", "depthTextureMode", [GL.LUMINANCE, GL.INTENSITY, GL.ALPHA]);
    add(GL.TEXTURE_COMPARE_MODE, GL.NONE, "e", "compareMode", [GL.NONE, GL.COMPARE_R_TO_TEXTURE]);
    add(GL.TEXTURE_COMPARE_FUNC, GL.LEQUAL, "e", "compareFunc", Array.from({ length: 8 }, (_, i) => GL.NEVER + i));
    add(GL.GENERATE_MIPMAP, 0, "b");
    add(GL.TEXTURE_MAX_ANISOTROPY_EXT, 1, "f", "maxAnisotropy");
    const clamp = v => Math.max(0, Math.min(1, v));
    const initial = target => {
        const values = Object.fromEntries([...definitions].map(([p, d]) => [p, d.initial.slice()]));
        if (target === GL.TEXTURE_RECTANGLE) {
            values[GL.TEXTURE_MIN_FILTER] = [GL.LINEAR];
            for (const p of [GL.TEXTURE_WRAP_S, GL.TEXTURE_WRAP_T, GL.TEXTURE_WRAP_R]) values[p] = [GL.CLAMP_TO_EDGE];
        }
        return values;
    };
    function samplerDefaults(target) {
        const out = {};
        const values = initial(target);
        for (const [p, d] of definitions) if (d.field)
            out[d.field] = d.initial.length > 1 ? new Float32Array(values[p]) : values[p][0];
        return out;
    }
    function normalize(pname, values, integer, scalar, target) {
        const d = definitions.get(pname);
        if (!d || pname === GL.TEXTURE_RESIDENT || scalar && d.initial.length > 1) return { error: GL.INVALID_ENUM };
        if (values.length < d.initial.length) return { error: GL.INVALID_VALUE };
        let v = values[0];
        if (d.kind === "e") {
            v = Math.trunc(v); if (!d.allowed.includes(v)) return { error: GL.INVALID_ENUM };
        } else if (d.kind === "i") {
            v = Math.round(v); if (v < 0 || v > 0x7FFFFFFF) return { error: GL.INVALID_VALUE };
        } else if (d.kind === "b") v = +(!!Math.trunc(v));
        else if (pname === GL.TEXTURE_BORDER_COLOR)
            return { values: values.slice(0, 4).map(v => Math.fround(clamp(integer ? v / 0x7FFFFFFF : v))) };
        else if (pname === GL.TEXTURE_PRIORITY) v = Math.fround(clamp(integer ? v / 0x7FFFFFFF : v));
        else if (pname === GL.TEXTURE_MAX_ANISOTROPY_EXT) {
            if (v < 1) return { error: GL.INVALID_VALUE }; v = Math.fround(Math.min(16, v));
        } else v = Math.fround(v);
        if (target === GL.TEXTURE_RECTANGLE) {
            if ([GL.TEXTURE_WRAP_S, GL.TEXTURE_WRAP_T, GL.TEXTURE_WRAP_R].includes(pname) && ![GL.CLAMP, GL.CLAMP_TO_EDGE, GL.CLAMP_TO_BORDER].includes(v) ||
                    pname === GL.TEXTURE_MIN_FILTER && ![GL.NEAREST, GL.LINEAR].includes(v)) return { error: GL.INVALID_ENUM };
            if (pname === GL.TEXTURE_BASE_LEVEL && v !== 0) return { error: GL.INVALID_VALUE };
        }
        return { values: [v] };
    }
    const api = { targets, definitions, initial, samplerDefaults, normalize };
    global.GLTextureParameters = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Draw/read selection belongs to the selected framebuffer, including zero.
(function(global) {
    "use strict";
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const initial = first => ({ drawBuffers: [first], readBuffer: first });
    function owner(c, map, read) {
        const name = read ? c.readFramebuffer : c.drawFramebuffer;
        const object = read ? c.readFramebufferObject : c.drawFramebufferObject;
        return name ? object?.name === name ? object : map.get(name) : c.defaultFramebufferState;
    }
    function sync(c, map) {
        c.drawBuffers = owner(c, map, false).drawBuffers.slice();
        c.readBuffer = owner(c, map, true).readBuffer;
    }
    function draw(c, map, values) {
        owner(c, map, false).drawBuffers = values.length ? values.slice() : [GL.NONE]; sync(c, map);
    }
    function read(c, map, value) {
        owner(c, map, true).readBuffer = value; sync(c, map);
    }
    function bind(c, map, target, name, object = name ? map.get(name) : null) {
        if (target !== GL.READ_FRAMEBUFFER) { c.drawFramebuffer = name; c.drawFramebufferObject = object; }
        if (target !== GL.DRAW_FRAMEBUFFER) { c.readFramebuffer = name; c.readFramebufferObject = object; }
        sync(c, map);
    }
    const api = { initial, owner, sync, draw, read, bind };
    global.GLFramebufferState = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Wire decoding shared by the CPU state and the renderer. The three
// PixelMap records carry their original element types, including u16.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    function decode(name, view, offset) {
        const r = P.decodeRecord(name, view, offset), color = r.map >= GL.PIXEL_MAP_I_TO_R;
        const stride = name === "PIXEL_MAPUSV" ? 2 : 4, max = name === "PIXEL_MAPUSV" ? 65535 : 0xFFFFFFFF;
        const values = Array.from({ length: r.count }, (_, i) => {
            const at = offset + P.RECORDS[name].size + i * stride;
            const value = name === "PIXEL_MAPFV" ? view.getFloat32(at, true) : stride === 2 ? view.getUint16(at, true) : view.getUint32(at, true);
            return color ? Math.fround(Math.max(0, Math.min(1, name === "PIXEL_MAPFV" ? value : value / max))) : value;
        });
        return { map: r.map, values };
    }
    const api = { decode };
    global.GLPixelMaps = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The CPU side's OpenGL state (docs/opengl-webgpu-plan.md C3). It lives in
// the v86gl device (v86gl_device.js), on the thread the CPU runs on, and
// reads every GL batch in guest memory before the renderer gets it: the
// guest's synchronous queries are answered there, before the device
// completes the request. So the answers are the same whether the renderer
// is on this thread or across the CPU worker's boundary, where the renderer
// only ever sees a copy (review S1-1).
//
// CPU modules answer state, object, program and error queries. Rendering
// results (pixels, occlusion results and programmable vertex captures) use
// explicit asynchronous replies; renderer state is never a GL error source.
//
// The parts: this file walks the records and keeps the contexts and their
// share groups; the others add handlers (GLShadow.install) and the state
// they keep (GLShadow.parts), to snapshots too.

(function(global) {
    "use strict";

    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const glCaps = global.GLCaps || require("./gl_caps.js");
    const clientArrays = global.GLClientArrays || require("./gl_client_arrays.js");
    const { GLFN, CTRL, C, F, RECORDS } = protocol;

    const OK = C.V86GL_SYNC_QUERY_STATUS_OK;
    const PENDING = C.V86GL_SYNC_QUERY_STATUS_PENDING;
    const EXTENDED_RECORD_SIZE = C.V86GL_EXTENDED_RECORD_SIZE;
    const UPLOAD_NAMES = new Set(("BUFFER_DATA BUFFER_SUB_DATA SHADER_SOURCE PROGRAM_STRING_ARB UNIFORM_FV UNIFORM_IV UNIFORM_MATRIX_FV UNIFORM_MATRIX_RECT_FV " +
        "TEX_IMAGE_1D TEX_IMAGE_2D TEX_IMAGE_3D TEX_SUB_IMAGE_1D TEX_SUB_IMAGE_2D TEX_SUB_IMAGE_3D " +
        "COMPRESSED_TEX_IMAGE_1D COMPRESSED_TEX_IMAGE_2D COMPRESSED_TEX_IMAGE_3D " +
        "COMPRESSED_TEX_SUB_IMAGE_1D COMPRESSED_TEX_SUB_IMAGE_2D COMPRESSED_TEX_SUB_IMAGE_3D " +
        "DRAW_ARRAYS DRAW_ARRAYS_GL2 DRAW_ELEMENTS DRAW_ELEMENTS_GL2 BITMAP DRAW_PIXELS MAP_EVALUATOR PIXEL_MAPFV PIXEL_MAPUIV PIXEL_MAPUSV").split(" "));
    const uploadHash = bytes => { let hash = 2166136261;for (const value of bytes) hash = Math.imul(hash ^ value, 16777619);return hash >>> 0; };

    // record code -> method; the parts add theirs
    const HANDLERS = new Map();
    // CPU rules run before any state mutation or query handler. Returning a
    // GL error suppresses the original command in both halves of the stream.
    const RULES = new Map();
    // glGetIntegerv and glGetString pnames the parts answer: pname -> method
    // of the shadow, giving the value (undefined: not this side's yet)
    const INTEGERS = new Map(), STRINGS = new Map();
    // { reset(shadow), group(group), context(context), getState(shadow, state), setState(shadow, state) }
    // of each part
    const PARTS = [];

    // Attribute stacks copy live numeric state; JSON would turn NaN and
    // infinities into null and -0 into +0 before a snapshot even begins.
    function cloneState(value) {
        if (Array.isArray(value)) return value.map(cloneState);
        if (ArrayBuffer.isView(value)) return value instanceof DataView ?
            new DataView(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength)) : new value.constructor(value);
        if (value instanceof ArrayBuffer) return value.slice(0);
        if (value instanceof Map) return new Map([...value].map(([key, item]) => [cloneState(key), cloneState(item)]));
        if (value instanceof Set) return new Set([...value].map(cloneState));
        if (value && typeof value === "object") return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, cloneState(item)]));
        return value;
    }
    const NUMBER_TAG = "$v86glNumber";
    function snapshotNumbers(value, decode) {
        if (typeof value === "number" && !decode) {
            const token = Number.isNaN(value) ? "nan" : value === Infinity ? "+inf" : value === -Infinity ? "-inf" : Object.is(value, -0) ? "-0" : null;
            return token ? { [NUMBER_TAG]: token } : value;
        }
        if (Array.isArray(value)) return value.map(item => snapshotNumbers(item, decode));
        if (value && typeof value === "object") {
            if (decode && Object.keys(value).length === 1 && Object.hasOwn(value, NUMBER_TAG)) {
                switch (value[NUMBER_TAG]) {
                case "nan": return NaN;
                case "+inf": return Infinity;
                case "-inf": return -Infinity;
                case "-0": return -0;
                }
            }
            return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, snapshotNumbers(item, decode)]));
        }
        return value;
    }

    class GLShadow {
        constructor() {
            // gl_caps.js's capabilities object; none until the renderer sends it
            this.capabilities = null;
            // gl_glslang.js's Glslang; none until the renderer sends the module
            this.glslang = null;
            this.stats = { batches: 0, records: 0, answered: 0, malformed: 0, rejected: 0 };
            this.reset();
        }

        // fn -> the method's name, for a part
        static install(handlers) {
            for (const [fn, method] of Object.entries(handlers)) HANDLERS.set(+fn, method);
        }

        static rules(rules) {
            for (const [fn, rule] of Object.entries(rules)) RULES.set(+fn, rule);
        }

        // pname -> the method's name, for a part's glGetIntegerv and glGetString
        static queries({ integers, strings }) {
            for (const [pname, method] of Object.entries(integers || {})) INTEGERS.set(+pname, method);
            for (const [pname, method] of Object.entries(strings || {})) STRINGS.set(+pname, method);
        }

        static part(part) {
            PARTS.push(part);
        }

        static clone(value) { return cloneState(value); }

        // A new owner of the arena, or a reset machine: no contexts
        reset() {
            this.responseTargets = new Map();
            this.nextResponseTarget = 0;
            this.cpuBarrier = null;
            this.deferredCPU = [];
            this.deferredCPUBytes = 0;
            this.cpuResumeInsertion = null;
            this.completedCPUBatches = null;
            this.contexts = new Map();
            this.groups = new Map();
            this.nextGroup = 1;
            this.current = null;
            for (const part of PARTS) if (part.reset) part.reset(this);
        }

        setCapabilities(capabilities) {
            this.capabilities = glCaps.freeze(capabilities);
        }

        setGlslang(glslang) {
            this.glslang = glslang;
            this.glslangFailure = null;
        }

        setGlslangFailure(message) {
            this.glslang = null;
            this.glslangFailure = "ERROR: GLSL compiler is unavailable: " + message + "\n";
        }

        /*
         * One GL batch's records, in guest memory (a view the answers are
         * written into). A malformed stream stops the walk: the executor
         * reports it.
         */
        process(bytes, metadata = this.batchMetadata) {
            if (this.cpuBarrier) { this.deferCPU(bytes, metadata); return []; }
            const previous = this.batchMetadata;
            this.batchMetadata = metadata;
            try {
                const result = this.processBatch(bytes);
                if (!this.cpuBarrier && metadata?.root) {
                    if (this.completedCPUBatches) this.completedCPUBatches.push(metadata);
                    else this.onCPUComplete?.(metadata);
                }
                return result;
            }
            finally { this.batchMetadata = previous; }
        }

        blockCPU(token) {
            if (this.cpuBarrier) throw new Error("a GL CPU state dependency is already pending");
            this.cpuBarrier = { token, metadata: this.batchMetadata };
        }

        deferCPU(bytes, metadata) {
            const task = { bytes: bytes.slice(), metadata: metadata && { ...metadata, deferred: true } };
            if (this.current?.listExecution) task.execution = { context: this.current.key, ...GLShadow.clone(this.current.listExecution) };
            (this.cpuResumeInsertion || this.deferredCPU).push(task); this.deferredCPUBytes += task.bytes.length;
        }

        cancelCPU() {
            const barrier = this.cpuBarrier;
            this.cpuBarrier = null; this.deferredCPU = []; this.deferredCPUBytes = 0;
            for (const part of PARTS) if (part.cancelCPU) part.cancelCPU(this, barrier);
        }

        resumeCPU(token, commands = []) {
            if (this.cpuBarrier?.token !== token) return false;
            if (this.completedCPUBatches) { Promise.resolve().then(() => this.resumeCPU(token, commands)); return true; }
            const metadata = this.cpuBarrier.metadata;
            this.cpuBarrier = null; this.completedCPUBatches = [];
            try {
                if (commands.length) this.onCPUResume?.(new Uint8Array(), commands.map(bytes => ({ bytes })), { ...metadata, generated: true, root: false, offset: 0 });
                while (!this.cpuBarrier && this.deferredCPU.length) {
                    const task = this.deferredCPU.shift(); this.deferredCPUBytes -= task.bytes.length;
                    const context = task.execution && this.contexts.get(task.execution.context), previous = context?.listExecution;
                    if (context) context.listExecution = { depth: task.execution.depth, bytes: task.execution.bytes };
                    this.cpuResumeInsertion = [];
                    try {
                        const segments = this.process(task.bytes, task.metadata);
                        this.onCPUResume?.(task.bytes, segments, task.metadata);
                    } finally {
                        this.deferredCPU.unshift(...this.cpuResumeInsertion); this.cpuResumeInsertion = null;
                        if (context) context.listExecution = previous;
                    }
                }
                // Publish continuations before releasing the submitting IOCTL:
                // a newly accepted batch must follow their rendering work.
                for (const completed of this.completedCPUBatches) this.onCPUComplete?.(completed);
            } finally { this.completedCPUBatches = null; }
            return true;
        }

        deferResponse(view, offset, length, local = false) {
            if (this.batchMetadata?.batchId !== undefined && !local) return { ...this.batchMetadata, offset: (this.batchMetadata.offset || 0) + offset };
            const id = ++this.nextResponseTarget;
            this.responseTargets.set(id, new Uint8Array(view.buffer, view.byteOffset + offset, length));
            return { localId: id };
        }

        completeResponse(target, bytes, statusOffset) {
            if (target.localId) {
                const output = this.responseTargets.get(target.localId);
                this.responseTargets.delete(target.localId);
                if (output) { output.set(bytes.subarray(0, statusOffset)); output.set(bytes.subarray(statusOffset + 4), statusOffset + 4); output.set(bytes.subarray(statusOffset, statusOffset + 4), statusOffset); }
            } else if (this.writeResponse) this.writeResponse(target, bytes, statusOffset);
        }

        processBatch(bytes) {
            ++this.stats.batches;
            const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            const end = bytes.byteLength;
            let offset = 0;
            const segments = [];
            let accepted = 0, filtered = false;
            while (offset + 4 <= end) {
                const recordOffset = offset;
                const fn = view.getUint16(offset, true);
                let size = view.getUint16(offset + 2, true);
                offset += 4;
                if (size === EXTENDED_RECORD_SIZE) {
                    if (offset + 4 > end) { offset = recordOffset; break; }
                    size = view.getUint32(offset, true);
                    offset += 4;
                }
                if (offset + size > end) {
                    offset = recordOffset; break;
                }
                ++this.stats.records;
                let routed;
                const name = protocol.NAME_BY_OPCODE[fn], signature = protocol.SIGNATURES[name], header = RECORDS[name];
                const minimum = signature ? protocol.payloadBytes(signature[1]) : header ? header.size : 0;
                if (size < minimum || !name && !Object.values(CTRL).includes(fn) && !protocol.isRejected(fn)) {
                    ++this.stats.malformed;
                    const fields = protocol.F[name];
                    if (fields?.status !== undefined && fields.status + 4 <= size) view.setUint32(offset + fields.status, C.V86GL_SYNC_QUERY_STATUS_FAILED, true);
                    routed = { error: GL.INVALID_OPERATION };
                } else if (!this.current && fn < 0xFF00 && !name?.startsWith("QUERY_")) {
                    routed = { commands: [] };
                } else if (fn === GLFN.UPLOAD_CHUNK) {
                    routed = this.uploadChunk(bytes, view, offset, size);
                } else for (const part of PARTS) if (part.routeRecord) {
                    routed = part.routeRecord(this, fn, bytes, view, offset, size);
                    if (routed !== undefined) break;
                }
                if (routed === undefined && fn === GLFN.IMMEDIATE_VERTEX_BLOCK) routed = this.immediateBlock(bytes, view, offset, size);
                const error = routed ? routed.error || 0 : this.dispatch(fn, bytes, view, offset, size);
                if (error) {
                    if (this.current && !this.current.error) this.current.error = error;
                    view.setUint16(recordOffset, protocol.rejectedOpcode(fn >= 1 && fn <= C.V86GL_MAX_OPCODE ? fn : GLFN.QUERY_ERROR, error), true);
                    ++this.stats.rejected;
                } else if (!routed) {
                    for (const part of PARTS) if (part.forwardOpcode) {
                        const forwarded = part.forwardOpcode(this, fn, bytes, view, offset, size);
                        if (forwarded !== undefined) view.setUint16(recordOffset, forwarded, true);
                    }
                }
                const statusOffset = protocol.F[name]?.status;
                if (this.batchMetadata?.deferred && !this.batchMetadata.generated && statusOffset !== undefined && statusOffset + 4 <= size &&
                    view.getUint32(offset + statusOffset, true) !== PENDING && this.writeResponse)
                    this.completeResponse({ ...this.batchMetadata, offset: (this.batchMetadata.offset || 0) + offset }, bytes.slice(offset, offset + size), statusOffset);
                offset += size;
                if (routed || protocol.isRejected(view.getUint16(recordOffset, true))) {
                    if (accepted < recordOffset) segments.push({ offset: accepted, length: recordOffset - accepted });
                    if (routed && !error) for (const command of routed.commands || []) segments.push({ bytes: command });
                    accepted = offset; filtered = true;
                }
                if (this.cpuBarrier) {
                    this.deferCPU(bytes.subarray(offset), this.batchMetadata && { ...this.batchMetadata, offset: (this.batchMetadata.offset || 0) + offset });
                    if (accepted < offset) segments.push({ offset: accepted, length: offset - accepted });
                    return segments;
                }
            }
            if (offset !== end) ++this.stats.malformed;
            for (const part of PARTS) if (part.afterBatch) part.afterBatch(this);
            if (offset !== end) {
                if (accepted < offset) segments.push({ offset: accepted, length: offset - accepted });
                return segments;
            }
            if (!filtered) return;
            if (accepted < end) segments.push({ offset: accepted, length: end - accepted });
            return segments;
        }

        // CPU-generated records own their storage and carry no guest reply
        // addresses. Lists and evaluators use this to validate their expansion
        // through exactly the same command rules as the original stream.
        expand(bytes) {
            const segments = this.process(bytes, this.batchMetadata && { ...this.batchMetadata, generated: true, root: false, offset: 0 });
            return segments ? segments.map(segment => segment.bytes || bytes.subarray(segment.offset, segment.offset + segment.length)) : [bytes];
        }

        // Only the complete immutable original enters GL validation and list
        // compilation. Partial transport data cannot redefine live storage.
        uploadChunk(bytes, view, offset, size) {
            const c = this.current, r = protocol.decodeRecord("UPLOAD_CHUNK", view, offset), header = RECORDS.UPLOAD_CHUNK.size;
            const fail = error => { if (c) c.upload = null;return { error }; };
            if (!c || !r.token || !UPLOAD_NAMES.has(protocol.NAME_BY_OPCODE[r.opcode]) || !r.total_size || !r.data_size ||
                size !== header + r.data_size || r.offset + r.data_size > r.total_size) return fail(GL.INVALID_OPERATION);
            if (r.total_size > C.V86GL_MAX_UPLOAD_BYTES) return fail(GL.OUT_OF_MEMORY);
            const data = bytes.subarray(offset + header, offset + size), last = c.completedUpload;
            if (last?.token === r.token) return last.opcode === r.opcode && last.total_size === r.total_size &&
                last.offset === r.offset && last.data_size === r.data_size && last.hash === uploadHash(data) ? { commands: [] } : fail(GL.INVALID_OPERATION);
            let upload = c.upload;
            if (!r.offset && upload?.token !== r.token) {
                c.upload = null;
                const allocated = [...this.contexts.values()].reduce((sum, context) => sum + (context.upload?.total_size || 0), 0);
                if (allocated + r.total_size > C.V86GL_MAX_UPLOAD_BYTES) return fail(GL.OUT_OF_MEMORY);
                try {
                    const record = new Uint8Array(8 + r.total_size), v = new DataView(record.buffer);
                    v.setUint16(0, r.opcode, true);v.setUint16(2, EXTENDED_RECORD_SIZE, true);v.setUint32(4, r.total_size, true);
                    c.upload = upload = { token: r.token, opcode: r.opcode, total_size: r.total_size, received: 0, record };
                } catch (_) { return fail(GL.OUT_OF_MEMORY); }
            }
            if (!upload || upload.token !== r.token || upload.opcode !== r.opcode || upload.total_size !== r.total_size) return fail(GL.INVALID_OPERATION);
            if (r.offset < upload.received) {
                if (r.offset + r.data_size > upload.received || !data.every((v, i) => v === upload.record[8 + r.offset + i])) return fail(GL.INVALID_OPERATION);
                return { commands: [] };
            }
            if (r.offset !== upload.received) return fail(GL.INVALID_OPERATION);
            upload.record.set(data, 8 + r.offset);upload.received += data.length;
            if (upload.received < upload.total_size) return { commands: [] };
            c.upload = null;c.completedUpload = { ...r, hash: uploadHash(data) };
            return { commands: this.expand(upload.record) };
        }

        immediateBlock(bytes, view, offset, size) {
            if (!this.current?.state) return { error: GL.INVALID_OPERATION };
            const runs = clientArrays.parseImmediateBlock(bytes, view, offset, size);
            if (!runs) return { error: GL.INVALID_OPERATION };
            const c = this.current, render = (c.state.selection?.mode ?? GL.RENDER) === GL.RENDER, output = [], renderRuns = [];
            for (const run of runs) {
                if (!render) {
                    output.push(...this.expand(run.attributes));
                    for (let at = 0; at < run.positions.length; at += 16) {
                        const record = new Uint8Array(20), v = new DataView(record.buffer);v.setUint16(0, GLFN.VERTEX4F, true);v.setUint16(2, 16, true);record.set(run.positions.subarray(at, at + 16), 4);
                        output.push(...this.expand(record));
                    }
                } else {
                    const attrs = this.expand(run.attributes), attributes = new Uint8Array(attrs.reduce((n, b) => n + b.length, 0));let at = 0;
                    for (const part of attrs) {attributes.set(part, at);at += part.length;}
                    if (run.count && !c.state.begin && !c.error) c.error = GL.INVALID_OPERATION;
                    renderRuns.push({ attributes, positions: c.state.begin ? run.positions : new Uint8Array() });
                }
            }
            return { commands: render ? [clientArrays.immediateRecord(renderRuns)] : output };
        }

        dispatch(fn, bytes, view, offset, size, validateOnly = false) {
            if (protocol.isRejected(fn)) {
                if (this.current && !this.current.error) this.current.error = protocol.rejectionError(fn);
                return;
            }
            const name = protocol.NAME_BY_OPCODE[fn];
            const signature = protocol.SIGNATURES[name];
            const header = RECORDS[name];
            const minimum = signature ? protocol.payloadBytes(signature[1]) : header ? header.size : 0;
            if (size < minimum) { ++this.stats.malformed; return; }
            const rule = RULES.get(fn);
            if (rule && this.current) {
                const error = rule(this, bytes, view, offset, size);
                if (error) return error;
            }
            for (const part of PARTS) if (part.checkCommand && this.current) {
                const error = part.checkCommand(this, fn, bytes, view, offset, size);
                if (error) return error;
            }
            if (validateOnly) return 0;
            // GL errors are CPU state, including NO_ERROR. The renderer may
            // still report host failures, but cannot change this GL answer.
            if (fn === GLFN.QUERY_ERROR && this.pending("QUERY_ERROR", view, offset, size)) {
                const c = this.current, inside = c?.state?.begin;
                view.setUint32(offset + F.QUERY_ERROR.value, inside ? 0 : c?.error || 0, true);
                if (inside) { if (!c.error) c.error = GL.INVALID_OPERATION; }
                else if (c) c.error = 0;
                this.answered("QUERY_ERROR", view, offset, OK);
            }
            for (const part of PARTS) if (part.command) part.command(this, fn, bytes, view, offset, size);
            const method = HANDLERS.get(fn);
            if (!method) return;
            // (a GL command needs a context; one without is the executor's to report)
            if (fn < 0xFF00 && fn !== GLFN.QUERY_INTEGER && fn !== GLFN.QUERY_GL_STRING &&
                    fn !== GLFN.QUERY_STATE && fn !== GLFN.QUERY_PARAMETER && fn !== GLFN.QUERY_BUFFER_DATA &&
                    fn !== GLFN.QUERY_BUFFER_MAP && fn !== GLFN.QUERY_BUFFER_UNMAP && fn !== GLFN.QUERY_NAMES &&
                    fn !== GLFN.QUERY_TEXTURE_IMAGE && fn !== GLFN.QUERY_POLYGON_STIPPLE &&
                    fn !== GLFN.POLYGON_STIPPLE_BITMAP && fn !== GLFN.QUERY_LIST && fn !== GLFN.QUERY_RENDER_MODE && !this.current) return;
            return this[method](bytes, view, offset, size);
        }

        // A flat record's arguments, or null when it is short
        args(name, view, offset, size) {
            const out = [];
            return protocol.decodeArgs(protocol.SIGNATURES[name][1], view, offset, size, out) < 0 ? null : out;
        }

        // An older guest's MAKE_CURRENT stops short; the fields after read as 0
        field(name, field, view, offset, size) {
            const at = F[name][field];
            return at + 4 <= size ? view.getUint32(offset + at, true) : 0;
        }

        // Keyed as the executor keys them: by the guest's context id, or by
        // the window for a guest too old to send one. Objects belong to the
        // share group: the guest's, or the context's own.
        makeCurrent(bytes, view, offset, size) {
            const id = this.field("MAKE_CURRENT", "context_id", view, offset, size);
            const hwnd = this.field("MAKE_CURRENT", "hwnd", view, offset, size);
            const key = id ? "context:" + id : "hwnd:" + hwnd;
            let context = this.contexts.get(key);
            if (!context) {
                const shareGroup = this.field("MAKE_CURRENT", "share_group", view, offset, size);
                context = this.createContext(key, id, shareGroup ? "guest:" + shareGroup : "private:" + this.nextGroup++);
            }
            // Optional in older MAKE_CURRENT records. Freeze the first hint
            // for this context; a later switch cannot change its contract.
            if (context.capsProfile === undefined) {
                const profile = this.field("MAKE_CURRENT", "caps_profile", view, offset, size);
                context.capsProfile = profile >= 0 && profile <= C.V86GL_CAPS_PROFILE_GL21 ? profile : 0;
            }
            this.current = context;
            for (const part of PARTS) if (part.current) part.current(this, context, view, offset, size);
        }

        createContext(key, id, groupKey) {
            let group = this.groups.get(groupKey);
            if (!group) {
                group = { key: groupKey };
                for (const part of PARTS) if (part.group) part.group(group);
                this.groups.set(groupKey, group);
            }
            const context = { key, id, group, program: 0, error: 0 };
            for (const part of PARTS) if (part.context) part.context(context);
            this.contexts.set(key, context);
            return context;
        }

        releaseCurrent() {
            this.current = null;
        }

        destroyContext(bytes, view, offset, size) {
            if (size < RECORDS.DESTROY_CONTEXT.size) {
                this.reset();
                return;
            }
            const key = "context:" + view.getUint32(offset + F.DESTROY_CONTEXT.context_id, true);
            const context = this.contexts.get(key);
            if (this.current && this.current.key === key) this.current = null;
            this.contexts.delete(key);
            if (context) for (const part of PARTS) if (part.destroy) part.destroy(this, context);
            // a share group goes with its last context
            for (const [groupKey, group] of this.groups)
                if (![...this.contexts.values()].some(c => c.group === group)) this.groups.delete(groupKey);
        }

        // Whether a query record is still the guest's to have answered
        pending(name, view, offset, size) {
            return size >= RECORDS[name].size && view.getUint32(offset + F[name].status, true) === PENDING;
        }

        answered(name, view, offset, status) {
            view.setUint32(offset + F[name].status, status, true);
            ++this.stats.answered;
        }

        // A name list record (GEN_*, DELETE_*, DRAW_BUFFERS): the names, or null
        names(name, view, offset, size) {
            if (size < RECORDS[name].size) return null;
            const count = view.getUint32(offset + F[name].count, true);
            if (RECORDS[name].size + count * 4 > size) return null;
            const names = [];
            for (let i = 0; i < count; ++i) names.push(view.getUint32(offset + RECORDS[name].size + i * 4, true));
            return names;
        }

        // A capability, or a part's state (with a context)
        integer(pname) {
            const method = INTEGERS.get(pname);
            if (method && this.current) {
                const value = this[method](pname); if (value !== undefined) return value;
            }
            return glCaps.integer(this.capabilities, pname);
        }

        string(pname) {
            const c = this.current;
            if (c?.capsProfile && !c.capsStrings) {
                c.capsStrings = {};
                for (const name of [GL.VENDOR, GL.RENDERER, GL.VERSION, GL.SHADING_LANGUAGE_VERSION, GL.EXTENSIONS]) {
                    const text = glCaps.profileString(this.capabilities, c.capsProfile, name);
                    if (text !== undefined) c.capsStrings[name] = text;
                }
            }
            const value = c?.capsProfile ? c.capsStrings[pname] : glCaps.string(this.capabilities, pname);
            const method = STRINGS.get(pname);
            return value !== undefined || !method || !this.current ? value : this[method](pname);
        }

        queryInteger(bytes, view, offset, size) {
            if (!this.capabilities || !this.pending("QUERY_INTEGER", view, offset, size)) return;
            const pname = view.getUint32(offset + F.QUERY_INTEGER.pname, true);
            let value = this.integer(pname);
            if (value === undefined && this.current && this.stateValue) {
                const result = this.stateValue(pname);
                if (result) {
                    const v = result[0][0] || 0, kind = result[1];
                    value = kind === "u" ? v | 0 : kind === "n" ? (v <= -1 ? -0x80000000 : Math.trunc(Math.max(-1, Math.min(1, v)) * 0x7FFFFFFF)) :
                        kind === "t" ? Math.trunc(v) : v < 0 ? -Math.round(-v) : Math.round(v);
                }
            }
            if (value === undefined) {
                if (this.current && !this.current.error) this.current.error = GL.INVALID_ENUM;
                return this.answered("QUERY_INTEGER", view, offset, C.V86GL_SYNC_QUERY_STATUS_FAILED);
            }
            view.setUint32(offset + F.QUERY_INTEGER.value, value >>> 0, true);
            view.setUint32(offset + F.QUERY_INTEGER.status, OK, true);
            ++this.stats.answered;
        }

        // {name, status, length, data_size} then data_size bytes for the
        // text; length counts its terminator, which is written when it fits
        queryString(bytes, view, offset, size) {
            if (!this.capabilities || !this.pending("QUERY_GL_STRING", view, offset, size)) return;
            const text = this.string(view.getUint32(offset + F.QUERY_GL_STRING.name, true));
            const capacity = view.getUint32(offset + F.QUERY_GL_STRING.data_size, true);
            const header = RECORDS.QUERY_GL_STRING.size;
            if (text === undefined || header + capacity > size) {
                if (this.current && !this.current.error) this.current.error = text === undefined ? GL.INVALID_ENUM : GL.INVALID_VALUE;
                return this.answered("QUERY_GL_STRING", view, offset, C.V86GL_SYNC_QUERY_STATUS_FAILED);
            }
            const limit = Math.min(text.length + 1, capacity);
            for (let i = 0; i < limit; ++i)
                bytes[offset + header + i] = i < text.length ? text.charCodeAt(i) & 0xff : 0;
            view.setUint32(offset + F.QUERY_GL_STRING.length, text.length + 1, true);
            view.setUint32(offset + F.QUERY_GL_STRING.status, OK, true);
            ++this.stats.answered;
        }

        getState() {
            if (this.cpuBarrier || this.deferredCPU.length)
                throw new Error("GL snapshot requires completed CPU state dependencies");
            const state = {
                numberEncoding: 1,
                capabilities: this.capabilities,
                nextGroup: this.nextGroup,
                contexts: [...this.contexts.values()].map(c => ({ key: c.key, id: c.id, group: c.group.key,
                    program: c.program, error: c.error, capsProfile: c.capsProfile || 0,
                    ...(c.completedUpload ? { completedUpload: { ...c.completedUpload } } : {}),
                    ...(c.upload ? { upload: { ...c.upload, record: Array.from(c.upload.record.subarray(0, 8 + c.upload.received)) } } : {}),
                    ...(c.capsStrings ? { capsStrings: { ...c.capsStrings } } : {}) })),
                current: this.current ? this.current.key : null,
            };
            for (const part of PARTS) if (part.getState) part.getState(this, state);
            return snapshotNumbers(state, false);
        }

        // A snapshot's capabilities stand: the guest was told those
        setState(state) {
            this.reset();
            if (!state) return;
            state = state.numberEncoding === 1 ? snapshotNumbers(state, true) : cloneState(state);
            this.capabilities = glCaps.freeze(state.capabilities);
            this.nextGroup = state.nextGroup || 1;
            for (const c of state.contexts || []) {
                const context = this.createContext(c.key, c.id, c.group || "private:" + c.key);
                context.program = c.program || 0;
                context.error = c.error || 0;
                context.capsProfile = c.capsProfile || 0;
                if (c.completedUpload) context.completedUpload = { ...c.completedUpload };
                if (c.upload) {
                    const u = c.upload;
                    if (!u.token || !UPLOAD_NAMES.has(protocol.NAME_BY_OPCODE[u.opcode]) || !Number.isInteger(u.total_size) || u.total_size < 1 || u.total_size > C.V86GL_MAX_UPLOAD_BYTES ||
                        !Number.isInteger(u.received) || u.received < 1 || u.received >= u.total_size || u.record.length !== u.received + 8)
                        throw new Error("Invalid GL upload checkpoint");
                    const allocated = [...this.contexts.values()].reduce((sum, item) => sum + (item.upload?.total_size || 0), 0);
                    if (allocated + u.total_size > C.V86GL_MAX_UPLOAD_BYTES) throw new Error("Invalid GL upload checkpoint budget");
                    const record = new Uint8Array(8 + u.total_size), v = new DataView(record.buffer);
                    v.setUint16(0, u.opcode, true);v.setUint16(2, EXTENDED_RECORD_SIZE, true);v.setUint32(4, u.total_size, true);
                    record.set(u.record.slice(8), 8);context.upload = { ...u, record };
                }
                if (c.capsStrings) context.capsStrings = { ...c.capsStrings };
            }
            this.current = state.current ? this.contexts.get(state.current) || null : null;
            for (const part of PARTS) if (part.setState) part.setState(this, state);
        }
    }

    GLShadow.install({
        [CTRL.MAKE_CURRENT]: "makeCurrent",
        [CTRL.RELEASE_CURRENT]: "releaseCurrent",
        [CTRL.DESTROY_CONTEXT]: "destroyContext",
        [GLFN.QUERY_INTEGER]: "queryInteger",
        [GLFN.QUERY_GL_STRING]: "queryString",
    });

    const api = { GLShadow };
    global.V86GLShadow = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The GL shadow's shaders and programs (docs/opengl-webgpu-plan.md C4, the
// first step): GLSL 1.10/1.20 checked, linked and reflected by glslang on the
// CPU side, so glGetShaderiv, glGetProgramiv, the info logs, the active
// uniforms and attributes, the locations and glGetUniform* are answered in
// the request that asks, in both of the proxy's modes.
//
// glslang runs when an answer needs it, not at glCompileShader: the module
// may arrive after the first shaders (the renderer sends it), and a guest
// that never asks pays nothing. The device holds startup submissions until
// the compiler and capabilities arrive; a load failure is a CPU-side compile
// and link failure with a log, consistently in local and worker modes.
//
// Locations are this side's. A uniform's are the program's name times
// UNIFORM_LOCATIONS_PER_PROGRAM plus its position among the program's
// uniform elements, so they are the same after a relink and after a
// snapshot; an attribute not bound by the application takes the highest
// free slot, around the slots the conventional attributes alias
// (gl_shader_translator.js's table, NVIDIA's). The page's executor adopts
// what the guest was told: an answered location record is the mapping.

(function(global) {
    "use strict";

    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const constants = global.GLWGConstants || require("./gl_constants.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const record = (fn, parts) => (global.GLListArrays || require("./gl_list_arrays.js")).record(fn, parts);
    const { STAGE_OF_GL_TYPE } = global.V86GLGlslang || require("./gl_glslang.js");
    const { GLFN, C, F, RECORDS } = protocol;
    const GL = constants.GL;

    const OK = C.V86GL_SYNC_QUERY_STATUS_OK;
    const FAILED = C.V86GL_SYNC_QUERY_STATUS_FAILED;
    const PENDING = C.V86GL_SYNC_QUERY_STATUS_PENDING;
    const UNIFORM_LOCATIONS_PER_PROGRAM = 4096;
    const MAX_VERTEX_ATTRIBS = 16;
    const RENDER_COMMANDS = new Set(["BEGIN", "DRAW_ARRAYS", "DRAW_ELEMENTS", "DRAW_ARRAYS_GL2",
        "DRAW_ELEMENTS_GL2", "DRAW_ARRAYS_DIRECT", "DRAW_ELEMENTS_DIRECT", "DRAW_RANGE_ELEMENTS_DIRECT",
        "MULTI_DRAW_ARRAYS_DIRECT", "MULTI_DRAW_ELEMENTS_DIRECT", "DRAW_PIXELS", "COPY_PIXELS", "BITMAP", "RASTER_POS4F"]
        .map(name => GLFN[name]));
    const SAMPLER_TYPES = new Set([0x8B5D, 0x8B5E, 0x8B5F, 0x8B60, 0x8B61, 0x8B62, 0x8B63, 0x8B64]);
    const CONVENTIONAL_SLOTS = {
        gl_Vertex: 0, gl_Normal: 2, gl_Color: 3, gl_SecondaryColor: 4, gl_FogCoord: 5,
        gl_MultiTexCoord0: 8, gl_MultiTexCoord1: 9, gl_MultiTexCoord2: 10, gl_MultiTexCoord3: 11,
        gl_MultiTexCoord4: 12, gl_MultiTexCoord5: 13, gl_MultiTexCoord6: 14, gl_MultiTexCoord7: 15,
    };

    // GL type -> [base ("f", "i", "b"), columns, rows]: a vector is one
    // column, a sampler an int
    const TYPES = new Map([
        [0x1406, ["f", 1, 1]], [0x8B50, ["f", 1, 2]], [0x8B51, ["f", 1, 3]], [0x8B52, ["f", 1, 4]],
        [0x1404, ["i", 1, 1]], [0x8B53, ["i", 1, 2]], [0x8B54, ["i", 1, 3]], [0x8B55, ["i", 1, 4]],
        [0x8B56, ["b", 1, 1]], [0x8B57, ["b", 1, 2]], [0x8B58, ["b", 1, 3]], [0x8B59, ["b", 1, 4]],
        [0x8B5A, ["f", 2, 2]], [0x8B5B, ["f", 3, 3]], [0x8B5C, ["f", 4, 4]],
        [0x8B65, ["f", 2, 3]], [0x8B66, ["f", 2, 4]], [0x8B67, ["f", 3, 2]],
        [0x8B68, ["f", 3, 4]], [0x8B69, ["f", 4, 2]], [0x8B6A, ["f", 4, 3]],
    ]);
    const typeOf = type => TYPES.get(type) || ["i", 1, 1];
    const objectError = (group, name, shader) => (shader ? group.shaders : group.programs).has(name) ? 0 :
        (shader ? group.programs : group.shaders).has(name) ? GL.INVALID_OPERATION : GL.INVALID_VALUE;
    GLShadow.rules({ [GLFN.RESOLVED_PROGRAM_INTERFACE]: () => GL.INVALID_OPERATION, [GLFN.PROGRAM_LINK_FAILED]: () => GL.INVALID_OPERATION,
        [GLFN.PROGRAM_LINK_VALIDATION]: () => GL.INVALID_OPERATION });
    function rule(name, check) {
        GLShadow.rules({ [GLFN[name]]: (shadow, bytes, view, offset, size) => {
            if (shadow.current.state && shadow.current.state.begin) return GL.INVALID_OPERATION;
            return check(shadow, bytes, view, offset, size);
        } });
    }
    for (const name of ["CREATE_SHADER", "CREATE_PROGRAM"]) rule(name, (s, b, v, o, n) => {
        const a = s.args(name, v, o, n), g = s.current.group;
        if (name === "CREATE_SHADER" && !STAGE_OF_GL_TYPE.has(a[1])) return GL.INVALID_ENUM;
        if (name === "CREATE_PROGRAM" && g.nextProgramObjectId > 0xFFFFFFFF) return GL.OUT_OF_MEMORY;
        const kind = name === "CREATE_SHADER" ? C.V86GL_OBJECT_KIND_SHADER : C.V86GL_OBJECT_KIND_PROGRAM;
        if (g.glslNames?.has(a[0]) && g.glslNames.get(a[0]) !== kind) return GL.INVALID_VALUE;
        return !a[0] || g.shaders.has(a[0]) || g.programs.has(a[0]) ? GL.INVALID_VALUE : 0;
    });
    for (const [name, shader] of [["COMPILE_SHADER", true], ["DELETE_SHADER", true], ["LINK_PROGRAM", false],
            ["DELETE_PROGRAM", false], ["VALIDATE_PROGRAM", false], ["USE_PROGRAM", false]]) rule(name, (s, b, v, o, n) => {
        const id = s.args(name, v, o, n)[0];
        if (!id && ["DELETE_SHADER", "DELETE_PROGRAM", "USE_PROGRAM"].includes(name)) return 0;
        const error = objectError(s.current.group, id, shader); if (error) return error;
        if (name === "LINK_PROGRAM" && s.current.group.programs.get(id).linkGeneration >= 0xFFFFFFFF) return GL.OUT_OF_MEMORY;
        if (name === "USE_PROGRAM") {
            const linked = s.linkedProgram(s.current.group.programs.get(id));
            if (linked && !linked.ok) return GL.INVALID_OPERATION;
        }
        return 0;
    });
    for (const name of ["ATTACH_SHADER", "DETACH_SHADER"]) rule(name, (s, b, v, o, n) => {
        const a = s.args(name, v, o, n), g = s.current.group;
        const error = objectError(g, a[0], false) || objectError(g, a[1], true);
        if (error) return error;
        const attached = g.programs.get(a[0]).attached.has(a[1]);
        return attached === (name === "ATTACH_SHADER") ? GL.INVALID_OPERATION : 0;
    });
    rule("SHADER_SOURCE", (s, b, v, o, n) => {
        const r = protocol.decodeRecord("SHADER_SOURCE", v, o);
        return objectError(s.current.group, r.shader, true) || (RECORDS.SHADER_SOURCE.size + r.length > n ? GL.INVALID_VALUE : 0);
    });
    rule("BIND_ATTRIB_LOCATION", (s, b, v, o, n) => {
        const r = protocol.decodeRecord("BIND_ATTRIB_LOCATION", v, o), header = RECORDS.BIND_ATTRIB_LOCATION.size;
        const error = objectError(s.current.group, r.program, false); if (error) return error;
        if (header + r.name_length > n || r.location < 0 || r.location >= MAX_VERTEX_ATTRIBS) return GL.INVALID_VALUE;
        return readText(b, o + header, r.name_length).startsWith("gl_") ? GL.INVALID_OPERATION : 0;
    });

    // Validate the entire update before either side writes any uniform. An
    // array update stops at this uniform's end, never at the next location.
    for (const name of ["UNIFORM_FV", "UNIFORM_IV", "UNIFORM_MATRIX_FV", "UNIFORM_MATRIX_RECT_FV"])
        rule(name, (s, b, v, o, n) => {
            const r = protocol.decodeRecord(name, v, o), matrix = name.includes("MATRIX");
            const columns = matrix ? r.columns ?? r.dimension : 1, rows = matrix ? r.rows ?? r.dimension : r.components;
            if (r.count < 0 || columns < (matrix ? 2 : 1) || columns > 4 || rows < (matrix ? 2 : 1) || rows > 4 ||
                    RECORDS[name].size + r.count * columns * rows * 4 > n) return GL.INVALID_VALUE;
            const program = s.current.group.programs.get(s.current.program);
            if (!program) return GL.INVALID_OPERATION;
            if (r.location === -1) return 0;
            const linked = s.linkedProgram(program);
            if (linked === null) return 0; // the existing late-glslang path
            const entry = linked.ok && linked.byLocation.get(r.location);
            if (!entry) return GL.INVALID_OPERATION;
            const [base, c, rowsOfType] = typeOf(entry.uniform.type), integer = name === "UNIFORM_IV";
            if (columns !== c || rows !== rowsOfType || base !== "b" && integer !== (base === "i") ||
                    r.count > 1 && !entry.uniform.array) return GL.INVALID_OPERATION;
            const count = Math.min(r.count, entry.uniform.size - entry.element);
            v.setInt32(o + F[name].count, count, true);
            return 0;
        });

    // glslang's limits, from what the renderer backs (gl_caps.js)
    function glslangLimits(capabilities) {
        const limits = capabilities.limits;
        return {
            MaxLights: 8, MaxClipPlanes: 6, MaxTextureUnits: 8, MaxTextureCoords: 8,
            MaxVertexAttribs: MAX_VERTEX_ATTRIBS, MaxVertexUniformComponents: 1024, MaxVaryingFloats: 64,
            MaxVertexTextureImageUnits: caps.integer(capabilities, GL.MAX_VERTEX_TEXTURE_IMAGE_UNITS),
            MaxCombinedTextureImageUnits: caps.integer(capabilities, GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS),
            MaxTextureImageUnits: caps.integer(capabilities, GL.MAX_TEXTURE_IMAGE_UNITS),
            MaxFragmentUniformComponents: 1024, MaxDrawBuffers: Math.min(8, limits.maxColorAttachments),
        };
    }

    function readText(bytes, at, length, terminated = true) {
        let end = at;
        while (end < at + length && (!terminated || bytes[end])) ++end;
        let text = "";
        for (let i = at; i < end; ++i) text += String.fromCharCode(bytes[i]);
        return text;
    }

    // As much of `text` as fits in `capacity` bytes, terminated; the length written
    function writeText(bytes, at, capacity, text) {
        if (!capacity) return 0;
        const length = Math.min(text.length, capacity - 1);
        for (let i = 0; i < length; ++i) bytes[at + i] = text.charCodeAt(i) & 0xff;
        bytes[at + length] = 0;
        return length;
    }

    const proto = GLShadow.prototype;

    proto.createShader = function(bytes, view, offset, size) {
        const a = this.args("CREATE_SHADER", view, offset, size);
        if (!a || !STAGE_OF_GL_TYPE.has(a[1])) return;
        this.current.group.glslNames?.delete(a[0]);
        this.current.group.shaders.set(a[0], { name: a[0], type: a[1], source: "", compileSource: null,
            compiled: null, deleted: false });
    };

    proto.shaderSource = function(bytes, view, offset, size) {
        if (size < RECORDS.SHADER_SOURCE.size) return;
        const shader = this.current.group.shaders.get(view.getUint32(offset + F.SHADER_SOURCE.shader, true));
        const length = view.getUint32(offset + F.SHADER_SOURCE.length, true);
        if (!shader || RECORDS.SHADER_SOURCE.size + length > size) return;
        shader.source = readText(bytes, offset + RECORDS.SHADER_SOURCE.size, length, false);
    };

    // compiled when an answer needs it: the source as it was now
    proto.compileShader = function(bytes, view, offset, size) {
        const a = this.args("COMPILE_SHADER", view, offset, size);
        const shader = a && this.current.group.shaders.get(a[0]);
        if (!shader) return;
        shader.compileSource = shader.source;
        shader.compiled = null;
    };

    // {ok, log}, or null without glslang
    proto.compiledShader = function(shader) {
        if (shader.compileSource === null) return { ok: false, log: "" };
        if (!shader.compiled) {
            if (this.glslangFailure) return shader.compiled = { ok: false, log: this.glslangFailure };
            if (!this.prepareGlslang()) return null;
            shader.compiled = this.glslang.compile(STAGE_OF_GL_TYPE.get(shader.type), shader.compileSource);
        }
        return shader.compiled;
    };

    proto.prepareGlslang = function() {
        if (!this.glslang || !this.capabilities) return false;
        this.glslang.setLimits(glslangLimits(this.capabilities));
        return true;
    };

    // A shader flagged for deletion goes once no program has it attached
    proto.deleteShader = function(bytes, view, offset, size) {
        const a = this.args("DELETE_SHADER", view, offset, size);
        const group = this.current.group;
        const shader = a && group.shaders.get(a[0]);
        if (!shader) return;
        shader.deleted = true;
        this.collectShader(group, shader);
    };

    proto.collectShader = function(group, shader) {
        if (!shader.deleted) return;
        for (const program of group.programs.values()) if (program.attached.has(shader.name)) return;
        group.shaders.delete(shader.name);
    };

    proto.createProgram = function(bytes, view, offset, size) {
        const a = this.args("CREATE_PROGRAM", view, offset, size);
        if (!a) return;
        this.current.group.glslNames?.delete(a[0]);
        this.current.group.programs.set(a[0], { name: a[0], objectId: this.current.group.nextProgramObjectId++, linkGeneration: 0, attached: new Set(), bindings: new Map(),
            linkInput: null, linked: null, executableInput: null, resetValues: false,
            validated: false, validation: null, installed: null, deleted: false, values: new Map() });
    };

    proto.attachShader = function(bytes, view, offset, size) {
        const a = this.args("ATTACH_SHADER", view, offset, size);
        const group = this.current.group;
        const program = a && group.programs.get(a[0]);
        if (program && group.shaders.has(a[1])) program.attached.add(a[1]);
    };

    proto.detachShader = function(bytes, view, offset, size) {
        const a = this.args("DETACH_SHADER", view, offset, size);
        const group = this.current.group;
        const program = a && group.programs.get(a[0]);
        if (!program || !program.attached.delete(a[1])) return;
        const shader = group.shaders.get(a[1]);
        if (shader) this.collectShader(group, shader);
    };

    proto.bindAttribLocation = function(bytes, view, offset, size) {
        const header = RECORDS.BIND_ATTRIB_LOCATION.size;
        if (size < header) return;
        const program = this.current.group.programs.get(view.getUint32(offset + F.BIND_ATTRIB_LOCATION.program, true));
        const length = view.getUint32(offset + F.BIND_ATTRIB_LOCATION.name_length, true);
        if (!program || header + length > size) return;
        // takes effect at the next link
        program.bindings.set(readText(bytes, offset + header, length),
            view.getInt32(offset + F.BIND_ATTRIB_LOCATION.location, true));
    };

    // Freeze the compile inputs at LinkProgram, even if glslang arrives later.
    // Only a successful link replaces installed code and zeros its uniforms
    // (GL 2.1 section 2.15.2); failed links still replace the query result.
    proto.linkProgram = function(bytes, view, offset, size) {
        const a = this.args("LINK_PROGRAM", view, offset, size);
        const group = this.current.group;
        const program = a && group.programs.get(a[0]);
        if (!program) return;
        this.linkedProgram(program);
        const shaders = [...program.attached].map(name => group.shaders.get(name)).filter(Boolean);
        program.linkInput = { generation: ++program.linkGeneration, shaders: shaders.map(s => ({ type: s.type, compileSource: s.compileSource })),
            bindings: [...program.bindings] };
        program.backendLinkFailure = null;
        program.linked = null;
        program.validated = false;
        program.validation = null;
        program.resetValues = true;
        this.linkedProgram(program);
    };

    // The link's result: {ok, log, uniforms, attributes, byLocation}, or null without glslang
    proto.linkedProgram = function(program) {
        if (program.linked) return program.linked;
        if (!program.linkInput) return { ok: false, log: "", uniforms: [], attributes: [], byLocation: new Map() };
        const failed = log => ({ ok: false, log, uniforms: [], attributes: [], byLocation: new Map() });
        if (program.backendLinkFailure?.generation === program.linkInput.generation)
            return program.linked = failed(program.backendLinkFailure.log);
        if (this.glslangFailure) return program.linked = failed(this.glslangFailure);
        if (!this.prepareGlslang()) return null;
        const shaders = program.linkInput.shaders;
        if (!shaders.length) return program.linked = failed("ERROR: no shaders attached\n");
        for (const shader of shaders) {
            if (shader.compileSource === null) return program.linked = failed("ERROR: a shader is not compiled\n");
            if (!this.glslang.compile(STAGE_OF_GL_TYPE.get(shader.type), shader.compileSource).ok)
                return program.linked = failed("ERROR: a shader did not compile\n");
        }
        const result = this.glslang.link(shaders.map(s => ({ stage: STAGE_OF_GL_TYPE.get(s.type),
            source: s.compileSource })));
        if (!result.ok) return program.linked = failed(result.log);
        // glslang's desktop resource table constrains built-ins, but does not
        // reject every excess active user sampler. Count stage uses as GL 2.1
        // requires: a uniform used by both stages consumes two combined slots.
        let vertexSamplers = 0, fragmentSamplers = 0;
        for (const uniform of result.uniforms) if (SAMPLER_TYPES.has(uniform.type)) {
            if (uniform.stages & 1) vertexSamplers += Math.max(1, uniform.size);
            if (uniform.stages & 16) fragmentSamplers += Math.max(1, uniform.size);
        }
        if (vertexSamplers > caps.integer(this.capabilities, GL.MAX_VERTEX_TEXTURE_IMAGE_UNITS) ||
                fragmentSamplers > caps.integer(this.capabilities, GL.MAX_TEXTURE_IMAGE_UNITS) ||
                vertexSamplers + fragmentSamplers > caps.integer(this.capabilities, GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS))
            return program.linked = failed("ERROR: active samplers exceed the available texture image units\n");
        const linked = { ok: true, log: result.log, uniforms: [], attributes: [], byLocation: new Map() };
        // uniforms: GL's list, built-ins too ("gl_" names have no location)
        let next = (program.name % (0x80000000 / UNIFORM_LOCATIONS_PER_PROGRAM)) * UNIFORM_LOCATIONS_PER_PROGRAM;
        for (const u of result.uniforms) {
            const name = u.name.replace(/\[0\]$/, "");
            const size = Math.max(1, u.size);
            const uniform = { name, active: u.arrayDims ? name + "[0]" : name, array: !!u.arrayDims, type: u.type,
                size, location: -1 };
            if (!name.startsWith("gl_")) {
                uniform.location = next;
                for (let i = 0; i < size; ++i) linked.byLocation.set(next + i, { uniform, element: i });
                next += size;
            }
            linked.uniforms.push(uniform);
        }
        // attributes: the vertex stage's inputs
        const vertexSources = shaders.filter(s => s.type === GL.VERTEX_SHADER).map(s => s.compileSource).join("\n");
        const used = new Set();
        if (/\b(gl_Vertex|ftransform)\b/.test(vertexSources)) used.add(0);
        const inputs = result.inputs.filter(i => i.stages & 1 || !(i.stages >>> 0));
        for (const i of inputs) if (i.name in CONVENTIONAL_SLOTS) used.add(CONVENTIONAL_SLOTS[i.name]);
        const bindings = new Map(program.linkInput.bindings);
        const slots = type => typeOf(type)[1];
        for (const i of inputs) {
            const attribute = { name: i.name, type: i.type, size: Math.max(1, i.size), location: -1 };
            linked.attributes.push(attribute);
            if (i.name.startsWith("gl_")) continue;
            const bound = bindings.get(i.name);
            if (bound !== undefined) {
                attribute.location = bound;
                const n = slots(i.type) * attribute.size;
                if (bound + n > MAX_VERTEX_ATTRIBS)
                    return program.linked = failed("ERROR: vertex attribute '" + i.name + "' exceeds the available slots\n");
                for (let k = 0; k < n; ++k) used.add(bound + k);
            }
        }
        for (const attribute of linked.attributes) {
            if (attribute.name.startsWith("gl_") || attribute.location >= 0) continue;
            const n = slots(attribute.type) * attribute.size;
            let base = MAX_VERTEX_ATTRIBS - n;
            const free = b => { for (let k = 0; k < n; ++k) if (used.has(b + k)) return false; return true; };
            while (base >= 0 && !free(base)) --base;
            if (base < 0) return program.linked = failed("ERROR: out of vertex attribute slots for '" +
                attribute.name + "'\n");
            for (let k = 0; k < n; ++k) used.add(base + k);
            attribute.location = base;
        }
        if (program.resetValues) {
            program.values = new Map();
            for (const uniform of linked.uniforms) {
                const values = result.initializers?.[uniform.name];
                if (!values || uniform.location < 0) continue;
                const [, columns, rows] = typeOf(uniform.type), width = columns * rows;
                for (let i = 0; i < uniform.size; ++i) if (values.length >= (i + 1) * width)
                    program.values.set(uniform.location + i, values.slice(i * width, (i + 1) * width));
            }
        }
        program.resetValues = false;
        program.executableInput = program.linkInput;
        program.executableGeneration = program.linkInput.generation || 1;
        program.installed = linked;
        return program.linked = linked;
    };

    // The latest failed link does not replace code already installed by UseProgram.
    // Reflection is reconstructed from the successful input after a snapshot.
    proto.installedProgram = function(program) {
        this.linkedProgram(program);
        if (!program.installed && program.executableInput) {
            program.installed = this.linkedProgram({ name: program.name, linkInput: program.executableInput,
                values: new Map(), resetValues: false });
        }
        return program.installed;
    };

    proto.samplerValidation = function(program, linked) {
        const units = new Map(), limit = caps.integer(this.capabilities, GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS);
        for (const uniform of linked.uniforms) {
            if (!SAMPLER_TYPES.has(uniform.type) || uniform.location < 0) continue;
            for (let i = 0; i < uniform.size; ++i) {
                const unit = (program.values.get(uniform.location + i) || [0])[0];
                if (unit < 0 || unit >= limit)
                    return { ok: false, log: "ERROR: sampler '" + uniform.name + "' selects an unavailable texture unit\n" };
                if (units.has(unit) && units.get(unit) !== uniform.type)
                    return { ok: false, log: "ERROR: samplers of different types select the same texture unit\n" };
                units.set(unit, uniform.type);
            }
        }
        return { ok: true, log: "" };
    };

    proto.programValidation = function(program) {
        if (!program.validated) return { ok: false, log: "" };
        if (!program.validation) {
            const linked = this.linkedProgram(program);
            if (!linked) return null;
            program.validation = linked.ok ? this.samplerValidation(program, linked) :
                { ok: false, log: "ERROR: program is not linked\n" };
        }
        return program.validation;
    };

    proto.useProgram = function(bytes, view, offset, size) {
        const a = this.args("USE_PROGRAM", view, offset, size);
        if (!a) return;
        const group = this.current.group;
        const previous = group.programs.get(this.current.program);
        this.current.program = a[0];
        if (previous) this.collectProgram(group, previous);
    };

    proto.validateProgram = function(bytes, view, offset, size) {
        const a = this.args("VALIDATE_PROGRAM", view, offset, size);
        const program = a && this.current.group.programs.get(a[0]);
        if (program) {
            program.validated = true;
            program.validation = null;
            this.programValidation(program);
        }
    };

    // A program flagged for deletion goes once no context uses it
    proto.deleteProgram = function(bytes, view, offset, size) {
        const a = this.args("DELETE_PROGRAM", view, offset, size);
        const group = this.current.group;
        const program = a && group.programs.get(a[0]);
        if (!program) return;
        program.deleted = true;
        this.collectProgram(group, program);
    };

    proto.collectProgram = function(group, program) {
        if (!program.deleted) return;
        for (const context of this.contexts.values())
            if (context.group === group && context.program === program.name) return;
        group.programs.delete(program.name);
        for (const name of program.attached) {
            const shader = group.shaders.get(name);
            program.attached.delete(name);
            if (shader) this.collectShader(group, shader);
        }
    };

    // glUniform*: the current program's values, by location
    proto.uniformValues = function(view, offset, size, header, location, columns, rows, count, transpose, integer) {
        const program = this.current.group.programs.get(this.current.program);
        const linked = program && this.linkedProgram(program);
        if (!linked || !linked.ok) return;
        const start = linked.byLocation.get(location);
        if (!start) return;
        count = Math.min(count, start.uniform.size - start.element);
        for (let m = 0; m < count; ++m) {
            const entry = linked.byLocation.get(location + m);
            if (!entry || entry.uniform.location + entry.uniform.size <= location + m) return;
            const values = [];
            for (let c = 0; c < columns; ++c) for (let r = 0; r < rows; ++r) {
                const index = transpose ? r * columns + c : c * rows + r;
                const at = offset + header + (m * columns * rows + index) * 4;
                if (at + 4 > offset + size) return;
                const v = integer ? view.getInt32(at, true) : view.getFloat32(at, true);
                values.push(typeOf(entry.uniform.type)[0] === "b" ? +!!v : v);
            }
            program.values.set(location + m, values);
        }
    };

    proto.uniformVector = function(integer) {
        return function(bytes, view, offset, size) {
            const name = integer ? "UNIFORM_IV" : "UNIFORM_FV";
            if (size < RECORDS[name].size) return;
            const components = view.getInt32(offset + F[name].components, true);
            if (components < 1 || components > 4) return;
            this.uniformValues(view, offset, size, RECORDS[name].size, view.getInt32(offset + F[name].location, true),
                1, components, view.getInt32(offset + F[name].count, true), false, integer);
        };
    };
    proto.uniformFv = proto.uniformVector(false);
    proto.uniformIv = proto.uniformVector(true);

    proto.uniformMatrix = function(bytes, view, offset, size) {
        const R = F.UNIFORM_MATRIX_FV;
        if (size < RECORDS.UNIFORM_MATRIX_FV.size) return;
        const n = view.getInt32(offset + R.dimension, true);
        this.uniformValues(view, offset, size, RECORDS.UNIFORM_MATRIX_FV.size, view.getInt32(offset + R.location, true),
            n, n, view.getInt32(offset + R.count, true), view.getUint32(offset + R.transpose, true) !== 0, false);
    };

    proto.uniformMatrixRect = function(bytes, view, offset, size) {
        const R = F.UNIFORM_MATRIX_RECT_FV;
        if (size < RECORDS.UNIFORM_MATRIX_RECT_FV.size) return;
        this.uniformValues(view, offset, size, RECORDS.UNIFORM_MATRIX_RECT_FV.size,
            view.getInt32(offset + R.location, true), view.getInt32(offset + R.columns, true),
            view.getInt32(offset + R.rows, true), view.getInt32(offset + R.count, true),
            view.getUint32(offset + R.transpose, true) !== 0, false);
    };

    /* ---- the answers ---- */

    const maxLength = list => list.reduce((n, e) => Math.max(n, e.length + 1), 0);

    proto.failedGLSLQuery = function(name, view, offset, error) {
        this.answered(name, view, offset, FAILED);
        return error;
    };

    proto.queryObjectIv = function(bytes, view, offset, size) {
        const R = F.QUERY_OBJECT_IV;
        if (!this.pending("QUERY_OBJECT_IV", view, offset, size)) return;
        const kind = view.getUint32(offset + R.object_kind, true);
        const name = view.getUint32(offset + R.name, true);
        const pname = view.getUint32(offset + R.pname, true);
        const group = this.current.group;
        let value = null;
        if (kind === C.V86GL_OBJECT_KIND_SHADER) {
            const shader = group.shaders.get(name);
            if (!shader) return this.failedGLSLQuery("QUERY_OBJECT_IV", view, offset, objectError(group, name, true));
            if (pname === GL.COMPILE_STATUS || pname === GL.INFO_LOG_LENGTH) {
                const compiled = this.compiledShader(shader);
                if (!compiled) return;
                value = pname === GL.COMPILE_STATUS ? +compiled.ok : compiled.log ? compiled.log.length + 1 : 0;
            } else if (pname === GL.SHADER_TYPE) value = shader.type;
            else if (pname === GL.DELETE_STATUS) value = +shader.deleted;
            else if (pname === GL.SHADER_SOURCE_LENGTH) value = shader.source ? shader.source.length + 1 : 0;
        } else if (kind === C.V86GL_OBJECT_KIND_PROGRAM) {
            const program = group.programs.get(name);
            if (!program) return this.failedGLSLQuery("QUERY_OBJECT_IV", view, offset, objectError(group, name, false));
            if (pname === GL.DELETE_STATUS) value = +program.deleted;
            else if (pname === GL.ATTACHED_SHADERS) value = program.attached.size;
            else if (pname === GL.VALIDATE_STATUS) {
                const validation = this.programValidation(program);
                if (!validation) return;
                value = +validation.ok;
            } else {
                const linked = this.linkedProgram(program);
                if (!linked) return;
                switch (pname) {
                case GL.LINK_STATUS: value = +linked.ok; break;
                case GL.INFO_LOG_LENGTH: {
                    const result = program.validated ? this.programValidation(program) : linked;
                    if (!result) return;
                    value = result.log ? result.log.length + 1 : 0; break;
                }
                case GL.ACTIVE_UNIFORMS: value = linked.uniforms.length; break;
                case GL.ACTIVE_ATTRIBUTES: value = linked.attributes.length; break;
                case GL.ACTIVE_UNIFORM_MAX_LENGTH: value = maxLength(linked.uniforms.map(u => u.active)); break;
                case GL.ACTIVE_ATTRIBUTE_MAX_LENGTH: value = maxLength(linked.attributes.map(a => a.name)); break;
                }
            }
        } else {
            // queries' results are the renderer's
            return;
        }
        if (value === null) return this.failedGLSLQuery("QUERY_OBJECT_IV", view, offset, GL.INVALID_ENUM);
        view.setInt32(offset + R.value, value, true);
        this.answered("QUERY_OBJECT_IV", view, offset, OK);
    };

    // {object_kind, name, buf_size, status, length, data_size} then the text
    proto.queryObjectLog = function(bytes, view, offset, size) {
        const R = F.QUERY_OBJECT_LOG, header = RECORDS.QUERY_OBJECT_LOG.size;
        if (!this.pending("QUERY_OBJECT_LOG", view, offset, size)) return;
        const kind = view.getUint32(offset + R.object_kind, true);
        const name = view.getUint32(offset + R.name, true);
        const capacity = view.getUint32(offset + R.buf_size, true);
        const group = this.current.group;
        let result;
        if (kind === C.V86GL_TEXT_SHADER_SOURCE) {
            const shader = group.shaders.get(name);
            if (!shader) return this.failedGLSLQuery("QUERY_OBJECT_LOG", view, offset, objectError(group, name, true));
            result = {log: shader.source};
        } else if (kind === C.V86GL_OBJECT_KIND_SHADER) {
            const shader = group.shaders.get(name);
            if (!shader) return this.failedGLSLQuery("QUERY_OBJECT_LOG", view, offset, objectError(group, name, true));
            result = shader ? this.compiledShader(shader) : undefined;
        } else if (kind === C.V86GL_OBJECT_KIND_PROGRAM) {
            const program = group.programs.get(name);
            if (!program) return this.failedGLSLQuery("QUERY_OBJECT_LOG", view, offset, objectError(group, name, false));
            result = program.validated ? this.programValidation(program) : this.linkedProgram(program);
        } else {
            return;
        }
        if (result === null) return;
        if (!result || header + capacity > size) return this.answered("QUERY_OBJECT_LOG", view, offset, FAILED);
        view.setUint32(offset + R.length, writeText(bytes, offset + header, capacity, result.log), true);
        this.answered("QUERY_OBJECT_LOG", view, offset, OK);
    };

    // {active_kind, program, index, buf_size, status, length, size, type, data_size, reserved} then the name
    proto.queryActive = function(bytes, view, offset, size) {
        const R = F.QUERY_ACTIVE, header = RECORDS.QUERY_ACTIVE.size;
        if (!this.pending("QUERY_ACTIVE", view, offset, size)) return;
        const program = this.current.group.programs.get(view.getUint32(offset + R.program, true));
        if (!program) return this.failedGLSLQuery("QUERY_ACTIVE", view, offset,
            objectError(this.current.group, view.getUint32(offset + R.program, true), false));
        const linked = program ? this.linkedProgram(program) : undefined;
        if (linked === null) return;
        const kind = view.getUint32(offset + R.active_kind, true);
        const index = view.getUint32(offset + R.index, true);
        const capacity = view.getUint32(offset + R.buf_size, true);
        const list = !linked ? [] : kind === C.V86GL_ACTIVE_KIND_ATTRIB ? linked.attributes : linked.uniforms;
        const entry = list[index];
        if (!entry) return this.failedGLSLQuery("QUERY_ACTIVE", view, offset, GL.INVALID_VALUE);
        if (header + capacity > size) return this.answered("QUERY_ACTIVE", view, offset, FAILED);
        view.setUint32(offset + R.length, writeText(bytes, offset + header, capacity, entry.active || entry.name), true);
        view.setInt32(offset + R.size, entry.size, true);
        view.setUint32(offset + R.type, entry.type, true);
        this.answered("QUERY_ACTIVE", view, offset, OK);
    };

    // {location_kind, program, guest_location, status, result, value_type, value_count, name_length} then the name
    proto.queryLocation = function(bytes, view, offset, size) {
        const R = F.QUERY_LOCATION, header = RECORDS.QUERY_LOCATION.size;
        if (!this.pending("QUERY_LOCATION", view, offset, size)) return;
        const program = this.current.group.programs.get(view.getUint32(offset + R.program, true));
        if (!program) return this.failedGLSLQuery("QUERY_LOCATION", view, offset,
            objectError(this.current.group, view.getUint32(offset + R.program, true), false));
        const linked = program ? this.linkedProgram(program) : undefined;
        if (linked === null) return;
        const length = view.getUint32(offset + R.name_length, true);
        if (!linked || !linked.ok) return this.failedGLSLQuery("QUERY_LOCATION", view, offset, GL.INVALID_OPERATION);
        if (header + length > size) return this.answered("QUERY_LOCATION", view, offset, FAILED);
        const name = readText(bytes, offset + header, length);
        let location = -1, type = 0, count = 0;
        if (view.getUint32(offset + R.location_kind, true) === C.V86GL_LOCATION_KIND_ATTRIB) {
            const attribute = linked.attributes.find(a => a.name === name);
            if (attribute && attribute.location >= 0) {
                location = attribute.location;
                type = attribute.type;
                count = typeOf(type)[1] * typeOf(type)[2];
            }
        } else {
            // name, name[0] and name[i]: location + i
            const match = /^(.*)\[(\d+)\]$/.exec(name);
            const base = match ? match[1] : name, element = match ? +match[2] : 0;
            const uniform = linked.uniforms.find(u => u.name === base);
            if (uniform && uniform.location >= 0 && element < uniform.size && (!match || uniform.array)) {
                location = uniform.location + element;
                type = uniform.type;
                count = typeOf(type)[1] * typeOf(type)[2];
            }
        }
        view.setInt32(offset + R.result, location, true);
        view.setUint32(offset + R.value_type, type, true);
        view.setInt32(offset + R.value_count, count, true);
        this.answered("QUERY_LOCATION", view, offset, OK);
    };

    // {program, location, value_kind, status, value_count, data_size, reserved0, reserved1} then the values:
    // value_kind 1 floats, 2 ints
    proto.queryUniform = function(bytes, view, offset, size) {
        const R = F.QUERY_UNIFORM, header = RECORDS.QUERY_UNIFORM.size;
        if (!this.pending("QUERY_UNIFORM", view, offset, size)) return;
        const program = this.current.group.programs.get(view.getUint32(offset + R.program, true));
        if (!program) return this.failedGLSLQuery("QUERY_UNIFORM", view, offset,
            objectError(this.current.group, view.getUint32(offset + R.program, true), false));
        const linked = program ? this.linkedProgram(program) : undefined;
        if (linked === null) return;
        const location = view.getInt32(offset + R.location, true);
        const entry = linked && linked.ok ? linked.byLocation.get(location) : null;
        if (!entry) return this.failedGLSLQuery("QUERY_UNIFORM", view, offset, GL.INVALID_OPERATION);
        const [base, columns, rows] = typeOf(entry.uniform.type);
        const count = columns * rows;
        if (header + count * 4 > size) return this.answered("QUERY_UNIFORM", view, offset, FAILED);
        const values = program.values.get(location) || new Array(count).fill(0);
        const integer = view.getUint32(offset + R.value_kind, true) === 2;
        for (let i = 0; i < count; ++i) {
            let v = values[i] ?? 0;
            if (base === "b") v = v ? 1 : 0;
            if (integer) view.setInt32(offset + header + i * 4, base === "f" ? Math.round(v) : v, true);
            else view.setFloat32(offset + header + i * 4, v, true);
        }
        view.setInt32(offset + R.value_count, count, true);
        this.answered("QUERY_UNIFORM", view, offset, OK);
    };

    GLShadow.install({
        [GLFN.CREATE_SHADER]: "createShader",
        [GLFN.SHADER_SOURCE]: "shaderSource",
        [GLFN.COMPILE_SHADER]: "compileShader",
        [GLFN.DELETE_SHADER]: "deleteShader",
        [GLFN.CREATE_PROGRAM]: "createProgram",
        [GLFN.ATTACH_SHADER]: "attachShader",
        [GLFN.DETACH_SHADER]: "detachShader",
        [GLFN.BIND_ATTRIB_LOCATION]: "bindAttribLocation",
        [GLFN.LINK_PROGRAM]: "linkProgram",
        [GLFN.USE_PROGRAM]: "useProgram",
        [GLFN.VALIDATE_PROGRAM]: "validateProgram",
        [GLFN.DELETE_PROGRAM]: "deleteProgram",
        [GLFN.UNIFORM_FV]: "uniformFv",
        [GLFN.UNIFORM_IV]: "uniformIv",
        [GLFN.UNIFORM_MATRIX_FV]: "uniformMatrix",
        [GLFN.UNIFORM_MATRIX_RECT_FV]: "uniformMatrixRect",
        [GLFN.QUERY_OBJECT_IV]: "queryObjectIv",
        [GLFN.QUERY_OBJECT_LOG]: "queryObjectLog",
        [GLFN.QUERY_ACTIVE]: "queryActive",
        [GLFN.QUERY_LOCATION]: "queryLocation",
        [GLFN.QUERY_UNIFORM]: "queryUniform",
    });

    function validationRecord(job, operation, data = new Uint8Array()) {
        return record(GLFN.PROGRAM_LINK_VALIDATION, [protocol.encodeRecord("PROGRAM_LINK_VALIDATION", {
            operation, job: job.id, program: job.program.name, object_id: job.program.objectId,
            link_generation: job.generation, data_size: data.length,
        }), data]);
    }
    function restoreExecutable(program, previous) {
        for (const name of ["installed", "values", "executableInput", "executableGeneration"]) program[name] = previous[name];
        program.resetValues = false;
    }
    proto.completeProgramLink = function(message) {
        const job = this.programLinkJobs?.get(message.job);
        if (!job || this.cpuBarrier?.token !== job.token || message.program !== job.program.name || message.object_id !== job.program.objectId ||
                message.link_generation !== job.generation || typeof message.ok !== "boolean") return false;
        this.programLinkJobs.delete(job.id);
        const program = job.program, current = job.group.programs.get(program.name) === program && program.linkGeneration === job.generation;
        const ok = current && message.ok;
        if (ok) {
            program.linked = job.linked;
            for (const name of ["installed", "values", "executableInput", "executableGeneration"]) program[name] = job.candidate[name];
            program.resetValues = false;
        } else if (current) {
            const log = "ERROR: GPU program link: " + String(message.log || "validation failed").slice(0, 65536);
            program.backendLinkFailure = { generation: job.generation, log };
            program.linked = { ok: false, log, uniforms: [], attributes: [], byLocation: new Map() };
        }
        const commands = [validationRecord(job, ok ? 1 : 2)];
        if (!ok && current) commands.push(record(GLFN.PROGRAM_LINK_FAILED, [protocol.encodeArgs("PROGRAM_LINK_FAILED", [program.name])]));
        this.resumeCPU(job.token, commands);
        return true;
    };

    // The objects, in snapshots: what was compiled and linked, and the
    // uniforms' values (glslang runs again when an answer needs it)
    GLShadow.part({
        routeRecord(shadow, fn, bytes, view, offset, size) {
            if (fn !== GLFN.LINK_PROGRAM || !shadow.current) return;
            if (shadow.requireGPUProgramValidation && shadow.nextProgramLinkJob >= 0xFFFFFFFF) return { error: GL.OUT_OF_MEMORY };
            const original = shadow.current.group.programs.get(view.getUint32(offset, true));
            if (original) shadow.linkedProgram(original);
            const previous = original && { installed: original.installed, values: original.values,
                executableInput: original.executableInput, executableGeneration: original.executableGeneration };
            const error = shadow.dispatch(fn, bytes, view, offset, size); if (error) return { error };
            const program = shadow.current.group.programs.get(view.getUint32(offset, true)), linked = program.linked;
            if (!linked) return { commands: [record(fn, [bytes.subarray(offset, offset + size)])] };
            if (!linked.ok) {
                view.setUint16(offset - 4, GLFN.PROGRAM_LINK_FAILED, true);
                return { commands: [record(GLFN.PROGRAM_LINK_FAILED, [bytes.subarray(offset, offset + size)])] };
            }
            const entry = (row, kind) => ({ name: row.name, location_kind: kind, location: row.location, value_type: row.type, value_count: row.size });
            const entries = [...linked.attributes.filter(a => a.location >= 0).map(a => entry(a, C.V86GL_LOCATION_KIND_ATTRIB)),
                ...linked.uniforms.filter(u => u.location >= 0).map(u => entry(u, C.V86GL_LOCATION_KIND_UNIFORM))];
            const resolved = protocol.encodeProgramInterface({ program: program.name, object_id: program.objectId, link_generation: program.executableGeneration }, entries,
                [...program.values].map(([location, values]) => ({location, values})));
            if (shadow.requireGPUProgramValidation) {
                const id = ++shadow.nextProgramLinkJob, job = { id, token: {kind: "program-link", id}, program, group: shadow.current.group,
                    generation: program.linkGeneration, linked, candidate: { installed: program.installed, values: program.values,
                        executableInput: program.executableInput, executableGeneration: program.executableGeneration } };
                restoreExecutable(program, previous);shadow.programLinkJobs.set(id, job);shadow.blockCPU(job.token);
                return { commands: [validationRecord(job, 0, resolved)] };
            }
            return { commands: [record(fn, [bytes.subarray(offset, offset + size)]), record(GLFN.RESOLVED_PROGRAM_INTERFACE,
                [resolved])] };
        },
        forwardOpcode(shadow, fn, bytes, view, offset, size) {
            if (fn !== GLFN.LINK_PROGRAM || !shadow.current || size < 4) return;
            const program = shadow.current.group.programs.get(view.getUint32(offset, true));
            if (program && program.linked && !program.linked.ok) return GLFN.PROGRAM_LINK_FAILED;
        },
        checkCommand(shadow, fn) {
            if (!RENDER_COMMANDS.has(fn) || !shadow.capabilities) return 0;
            const program = shadow.current.group.programs.get(shadow.current.program);
            if (!program) return 0;
            const linked = shadow.installedProgram(program);
            return linked && linked.ok && !shadow.samplerValidation(program, linked).ok ? GL.INVALID_OPERATION : 0;
        },
        group(group) {
            group.nextProgramObjectId = 1;
            group.shaders = new Map();
            group.programs = new Map();
        },
        reset(shadow) {shadow.programLinkJobs = new Map();shadow.nextProgramLinkJob ||= 0;},
        cancelCPU(shadow) {
            for (const job of shadow.programLinkJobs.values()) {
                const log = "ERROR: GPU program link validation cancelled";
                job.program.backendLinkFailure = {generation: job.generation, log};
                job.program.linked = {ok: false, log, uniforms: [], attributes: [], byLocation: new Map()};
            }
            shadow.programLinkJobs.clear();
        },
        destroy(shadow, context) {
            for (const program of context.group.programs.values()) shadow.collectProgram(context.group, program);
        },
        getState(shadow, state) {
            state.glsl = [...shadow.groups.values()].map(group => ({
                key: group.key,
                nextProgramObjectId: group.nextProgramObjectId,
                shaders: [...group.shaders.values()].map(s => [s.name, s.type, s.source, s.compileSource, s.deleted]),
                programs: [...group.programs.values()].map(p => [p.name, [...p.attached], [...p.bindings], p.linkInput,
                    p.validated, p.deleted, [...p.values], p.executableInput, p.resetValues, p.validation, p.objectId, p.linkGeneration, p.executableGeneration, p.backendLinkFailure || null]),
            }));
        },
        setState(shadow, state) {
            for (const saved of state.glsl || []) {
                const group = shadow.groups.get(saved.key);
                if (!group) continue;
                group.nextProgramObjectId = saved.nextProgramObjectId || 1;
                for (const [name, type, source, compileSource, deleted] of saved.shaders)
                    group.shaders.set(name, { name, type, source, compileSource, compiled: null, deleted });
                for (const [name, attached, bindings, linkInput, validated, deleted, values, executableInput, resetValues, validation, objectId, linkGeneration, executableGeneration, backendLinkFailure] of saved.programs)
                    group.programs.set(name, { name, attached: new Set(attached), bindings: new Map(bindings), linkInput,
                        objectId: objectId || group.nextProgramObjectId++, linkGeneration: linkGeneration || linkInput?.generation || 0, executableGeneration: executableGeneration || executableInput?.generation || 0,
                        linked: null, executableInput: executableInput || null, resetValues: !!resetValues,
                        validated, validation: validation || null, installed: null, deleted, values: new Map(values), backendLinkFailure: backendLinkFailure || null });
            }
        },
    });

    const api = { UNIFORM_LOCATIONS_PER_PROGRAM, CONVENTIONAL_SLOTS };
    global.V86GLShadowGLSL = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The GL shadow's textures, renderbuffers and framebuffer objects
// (docs/opengl-webgpu-plan.md C3): their names, what each texture level and
// renderbuffer holds (size and internal format), what each framebuffer
// object has attached, and the bindings -- so glCheckFramebufferStatus is
// answered on the CPU side, by EXT_framebuffer_object's rules.
//
// Textures and renderbuffers belong to the share group. Framebuffer names
// currently follow the executor's context-local namespace; distinguishing
// shared EXT FBO creation from core creation remains part of C3/C6.
// Draw/read buffer selection belongs to each framebuffer, including zero.

(function(global) {
    "use strict";

    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const constants = global.GLWGConstants || require("./gl_constants.js");
    const glCaps = global.GLCaps || require("./gl_caps.js");
    const buffers = global.GLFramebufferState || require("./gl_framebuffer_state.js");
    const formats = global.GLImageFormats || require("./gl_image_formats.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const { GLFN, C, F, RECORDS } = protocol;
    const GL = constants.GL;

    const OK = C.V86GL_SYNC_QUERY_STATUS_OK;
    const PENDING = C.V86GL_SYNC_QUERY_STATUS_PENDING;
    const MAX_TEXTURE_UNITS = glCaps.LIMITS.MAX_TEXTURE_UNITS;
    const MAX_COLOR_ATTACHMENTS = glCaps.LIMITS.MAX_DRAW_BUFFERS;
    const AUTO_MIPMAP_UPDATES = new Set([GLFN.TEX_SUB_IMAGE_1D, GLFN.TEX_SUB_IMAGE_2D, GLFN.TEX_SUB_IMAGE_3D,
        GLFN.COMPRESSED_TEX_SUB_IMAGE_1D, GLFN.COMPRESSED_TEX_SUB_IMAGE_2D, GLFN.COMPRESSED_TEX_SUB_IMAGE_3D,
        GLFN.COPY_TEX_SUB_IMAGE_1D, GLFN.COPY_TEX_SUB_IMAGE_2D, GLFN.COPY_TEX_SUB_IMAGE_3D]);

    const DEPTH_FORMATS = new Set([GL.DEPTH_COMPONENT, GL.DEPTH_COMPONENT16, GL.DEPTH_COMPONENT24,
        GL.DEPTH_COMPONENT32, GL.DEPTH_COMPONENT32F, GL.DEPTH_STENCIL, GL.DEPTH24_STENCIL8, 0x8CAD]);
    const STENCIL_FORMATS = new Set([GL.STENCIL_INDEX, GL.STENCIL_INDEX1_EXT, GL.STENCIL_INDEX4_EXT,
        GL.STENCIL_INDEX8_EXT, GL.STENCIL_INDEX16_EXT, GL.DEPTH_STENCIL, GL.DEPTH24_STENCIL8, 0x8CAD]);

    // A texture image target: the binding it is made through, and its layer
    // (a cube map's face)
    function imageTarget(target) {
        const face = target - GL.TEXTURE_CUBE_MAP_POSITIVE_X;
        if (face >= 0 && face < 6) return { binding: GL.TEXTURE_CUBE_MAP, layer: face };
        return { binding: target, layer: 0 };
    }

    const proto = GLShadow.prototype;

    proto.textureObject = function(name) {
        const group = this.current.group;
        let texture = group.textures.get(name);
        if (!texture) {
            texture = { name, id: group.nextImageId++, target: 0, levels: new Map(), deleted: false };
            group.textures.set(name, texture);
            group.textureImages.set(texture.id, texture);
        }
        return texture;
    };

    proto.defaultTexture = function(target) {
        const c = this.current, group = c.group;
        let id = c.defaultTextures.get(target);
        if (!id) {
            const texture = { name: 0, id: group.nextImageId++, target, levels: new Map(), deleted: false };
            id = texture.id; group.textureImages.set(id, texture); c.defaultTextures.set(target, id);
        }
        return group.textureImages.get(id);
    };

    proto.genTextures = function(bytes, view, offset, size) {
        for (const name of this.names("GEN_TEXTURES", view, offset, size) || []) if (name) this.textureObject(name);
    };

    // A deleted texture is unbound from every unit of this context and
    // detached from the framebuffer objects bound here
    proto.deleteTextures = function(bytes, view, offset, size) {
        const c = this.current;
        for (const name of this.names("DELETE_TEXTURES", view, offset, size) || []) {
            const texture = c.group.textures.get(name);
            if (!name || !texture) continue;
            c.group.textures.delete(name); texture.deleted = true;
            c.units.forEach((unit, i) => {
                for (const [target] of unit) if (c.unitImages[i].get(target) === texture.id) {
                    unit.set(target, 0); c.unitImages[i].delete(target);
                }
            });
            this.detach("texture", texture.id);
        }
    };

    proto.bindTexture = function(bytes, view, offset, size) {
        const a = this.args("BIND_TEXTURE", view, offset, size);
        if (!a) return;
        const c = this.current;
        c.units[c.activeTexture].set(a[0], a[1]);
        c.unitImages[c.activeTexture].set(a[0], a[1] ? this.textureObject(a[1]).id : this.defaultTexture(a[0]).id);
        if (a[1]) {
            const texture = this.textureObject(a[1]);
            if (!texture.target) texture.target = a[0];
        }
    };

    proto.activeTexture = function(bytes, view, offset, size) {
        const a = this.args("ACTIVE_TEXTURE", view, offset, size);
        const unit = a ? a[0] - GL.TEXTURE0 : -1;
        if (unit >= 0 && unit < MAX_TEXTURE_UNITS) this.current.activeTexture = unit;
    };

    // The texture bound for an image target on the active unit, and the layer
    proto.boundImage = function(target) {
        const { binding, layer } = imageTarget(target);
        const c = this.current;
        const id = c.unitImages[c.activeTexture].get(binding);
        return { texture: id ? c.group.textureImages.get(id) : this.defaultTexture(binding), layer };
    };

    proto.defineLevel = function(target, level, width, height, depth, internalFormat, metadata) {
        if (metadata && !metadata.compressed) internalFormat = formats.uncompressedInternalFormat(internalFormat);
        if (formats.PROXY_TARGETS.has(target)) {
            let levels = this.current.proxyImages.get(target);
            if (!levels) this.current.proxyImages.set(target, levels = new Map());
            const binding = formats.PROXY_TARGETS.get(target), border = metadata?.border || 0;
            const maximum = this.capabilities ? glCaps.integer(this.capabilities, binding === GL.TEXTURE_3D ? GL.MAX_3D_TEXTURE_SIZE :
                binding === GL.TEXTURE_CUBE_MAP ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 8192;
            const limit = Math.max(1, Math.floor(maximum / 2 ** level)) + 2 * border;
            if (width > limit || binding !== GL.TEXTURE_1D && height > limit || binding === GL.TEXTURE_3D && depth > limit) {
                // GL 2.1 3.8.11: an unsupported probe clears its image state
                // without a GL error, rather than retaining the last success.
                levels.set(level, { width: 0, height: 0, depth: 0, border: 0, internalFormat: 0, compressed: false, unsupported: true });
                return;
            }
            if (width && height && depth) levels.set(level, { width, height, depth, internalFormat, ...metadata });
            else levels.delete(level);
            return;
        }
        const image = this.boundImage(target);
        if (!image || level < 0) return;
        let layer = image.texture.levels.get(image.layer);
        if (!layer) image.texture.levels.set(image.layer, layer = new Map());
        if (width > 0 && height > 0 && depth > 0) layer.set(level, { width, height, depth, internalFormat, ...metadata });
        else layer.delete(level);
    };

    proto.texImage = function(dimensions) {
        const name = "TEX_IMAGE_" + dimensions + "D";
        return function(bytes, view, offset, size) {
            if (size < RECORDS[name].size) return;
            const r = protocol.decodeRecord(name, view, offset);
            this.defineLevel(r.target, r.level, r.width, dimensions >= 2 ? r.height : 1, dimensions >= 3 ? r.depth : 1,
                r.internalformat >>> 0, { border: r.border, compressed: false });
            this.autoMipmap(r.target, r.level);
        };
    };
    proto.texImage1D = proto.texImage(1);
    proto.texImage2D = proto.texImage(2);
    proto.texImage3D = proto.texImage(3);

    proto.compressedTexImage = function(dimensions) {
        const name = "COMPRESSED_TEX_IMAGE_" + dimensions + "D";
        return function(bytes, view, offset, size) {
            if (size < RECORDS[name].size) return;
            const r = protocol.decodeRecord(name, view, offset);
            this.defineLevel(r.target, r.level, r.width, dimensions >= 2 ? r.height : 1, dimensions >= 3 ? r.depth : 1,
                r.internalformat, { border: r.border, compressed: true, compressedSize: r.image_size });
            this.autoMipmap(r.target, r.level);
        };
    };
    proto.compressedTexImage1D = proto.compressedTexImage(1);
    proto.compressedTexImage2D = proto.compressedTexImage(2);
    proto.compressedTexImage3D = proto.compressedTexImage(3);

    // glCopyTexImage1D (target, level, internalformat, x, y, width, border)
    proto.copyTexImage1D = function(bytes, view, offset, size) {
        const a = this.args("COPY_TEX_IMAGE_1D", view, offset, size);
        if (a) {
            this.defineLevel(a[0], a[1], a[5], 1, 1, a[2], { border: a[6], compressed: false });
            this.autoMipmap(a[0], a[1]);
        }
    };

    // glCopyTexImage2D (target, level, internalformat, x, y, width, height, border)
    proto.copyTexImage2D = function(bytes, view, offset, size) {
        const a = this.args("COPY_TEX_IMAGE_2D", view, offset, size);
        if (a) {
            this.defineLevel(a[0], a[1], a[5], a[6], 1, a[2], { border: a[7], compressed: false });
            this.autoMipmap(a[0], a[1]);
        }
    };

    // Manual generation stops at q=min(p,MAX_LEVEL); automatic generation
    // replaces the full pyramid through p, including border texels (GL 2.1
    // 3.8.8 and EXT_framebuffer_object's Manual Mipmap Generation).
    proto.generateMipmap = function(bytes, view, offset, size) {
        const a = this.args("GENERATE_MIPMAP", view, offset, size);
        const texture = a && this.boundImage(a[0]).texture;
        if (!texture) return;
        this.generateMipmapForImage(texture);
    };

    proto.autoMipmap = function(target, level) {
        if (formats.PROXY_TARGETS.has(target)) return;
        const image = this.boundImage(target).texture;
        if (target !== GL.TEXTURE_RECTANGLE && level === (image.parameters?.[GL.TEXTURE_BASE_LEVEL]?.[0] || 0) && image.parameters?.[GL.GENERATE_MIPMAP]?.[0])
            this.generateMipmapForImage(image, target);
    };

    proto.generateMipmapForImage = function(texture, changedTarget) {
        const first = texture.parameters?.[GL.TEXTURE_BASE_LEVEL]?.[0] || 0;
        const maximum = changedTarget === undefined ? texture.parameters?.[GL.TEXTURE_MAX_LEVEL]?.[0] ?? 1000 : Infinity;
        const dimensions = texture.target === GL.TEXTURE_1D ? 1 : texture.target === GL.TEXTURE_3D ? 3 : 2;
        for (const [face, layer] of texture.levels) {
            if (changedTarget !== undefined && face !== imageTarget(changedTarget).layer) continue;
            const base = layer.get(first);
            if (!base) continue;
            const border = base.border || 0;
            let { width, height, depth } = base;
            width -= 2 * border;
            if (dimensions >= 2) height -= 2 * border;
            if (dimensions >= 3) depth -= 2 * border;
            if (width < 1 || height < 1 || depth < 1) continue;
            for (let level = first + 1; level <= maximum && (width > 1 || height > 1 || depth > 1); ++level) {
                width = Math.max(1, Math.floor(width / 2));
                height = Math.max(1, Math.floor(height / 2));
                depth = Math.max(1, Math.floor(depth / 2));
                layer.set(level, { ...base, width: width + 2 * border,
                    height: height + (dimensions >= 2 ? 2 * border : 0), depth: depth + (dimensions >= 3 ? 2 * border : 0),
                    ...(base.compressed ? { compressedSize: Math.ceil(width / 4) * Math.ceil(height / 4) * depth *
                        (formats.COMPRESSED_FORMATS[base.internalFormat]?.blockBytes || 0) } : {}) });
            }
        }
    };

    proto.genRenderbuffers = function(bytes, view, offset, size) {
        for (const name of this.names("GEN_RENDERBUFFERS", view, offset, size) || [])
            if (name) this.renderbufferObject(name);
    };

    proto.renderbufferObject = function(name) {
        const group = this.current.group;
        let rb = group.renderbuffers.get(name);
        if (!rb) {
            rb = { name, id: group.nextImageId++, width: 0, height: 0, internalFormat: GL.RGBA,
                componentBits: [0, 0, 0, 0, 0, 0], deleted: false, created: false };
            group.renderbuffers.set(name, rb); group.renderbufferImages.set(rb.id, rb);
        }
        return rb;
    };

    proto.deleteRenderbuffers = function(bytes, view, offset, size) {
        const c = this.current;
        for (const name of this.names("DELETE_RENDERBUFFERS", view, offset, size) || []) {
            const rb = c.group.renderbuffers.get(name);
            if (!name || !rb) continue;
            c.group.renderbuffers.delete(name); rb.deleted = true;
            if (c.renderbufferImage === rb.id) c.renderbuffer = c.renderbufferImage = 0;
            this.detach("renderbuffer", rb.id);
        }
    };

    proto.bindRenderbuffer = function(bytes, view, offset, size) {
        const a = this.args("BIND_RENDERBUFFER", view, offset, size);
        if (!a) return;
        this.current.renderbufferImage = a[1] ? this.renderbufferObject(a[1]).id : 0;
        if (a[1]) this.renderbufferObject(a[1]).created = true;
        this.current.renderbuffer = a[1];
    };

    // glRenderbufferStorage (target, internalformat, width, height)
    proto.renderbufferStorage = function(bytes, view, offset, size) {
        const a = this.args("RENDERBUFFER_STORAGE", view, offset, size);
        const rb = a && this.current.group.renderbufferImages.get(this.current.renderbufferImage);
        if (!rb) return;
        const storage = a[2] && a[3] ? formats.renderbufferFormatFor(a[1], this.capabilities?.features) : null;
        rb.internalFormat = storage ? storage.internalFormat || a[1] : GL.RGBA;
        rb.componentBits = storage ? storage.components : [0, 0, 0, 0, 0, 0];
        rb.width = storage ? a[2] : 0;
        rb.height = storage ? a[3] : 0;
    };

    proto.findFramebuffer = function(name) {
        const c = this.current;
        return c.privateFramebuffers.get(name) || c.group.sharedFramebuffers.get(name) || c.group.framebufferReservations.get(name);
    };

    proto.framebufferObject = function(name) {
        const c = this.current, g = c.group;
        let fbo = this.findFramebuffer(name);
        if (!fbo) {
            fbo = { name, id: g.nextFramebufferId++, created: false, shared: false, deleted: false,
                ...buffers.initial(GL.COLOR_ATTACHMENT0), color: new Array(MAX_COLOR_ATTACHMENTS).fill(null), depth: null, stencil: null };
            g.framebufferObjects.set(fbo.id, fbo); g.framebufferReservations.set(name, fbo);
        }
        c.framebuffers.set(name, fbo);
        return fbo;
    };

    proto.genFramebuffers = function(bytes, view, offset, size) {
        for (const name of this.names("GEN_FRAMEBUFFERS", view, offset, size) || []) if (name) this.framebufferObject(name);
    };

    proto.deleteFramebuffers = function(bytes, view, offset, size) {
        const c = this.current;
        for (const name of this.names("DELETE_FRAMEBUFFERS", view, offset, size) || []) {
            const object = name && this.findFramebuffer(name); if (!object) continue;
            if (c.privateFramebuffers.get(name) === object) c.privateFramebuffers.delete(name);
            if (c.group.sharedFramebuffers.get(name) === object) c.group.sharedFramebuffers.delete(name);
            if (c.group.framebufferReservations.get(name) === object) c.group.framebufferReservations.delete(name);
            object.deleted = true;
            for (const other of this.contexts.values()) if (other.group === c.group && other.framebuffers.get(name) === object) other.framebuffers.delete(name);
            if (c.drawFramebufferObject === object) { c.drawFramebuffer = 0; c.drawFramebufferObject = null; }
            if (c.readFramebufferObject === object) { c.readFramebuffer = 0; c.readFramebufferObject = null; }
        }
        buffers.sync(c, c.framebuffers);
    };

    proto.bindFramebuffer = function(bytes, view, offset, size, shared = false) {
        const a = this.args("BIND_FRAMEBUFFER", view, offset, size);
        if (!a) return;
        const c = this.current;
        const object = a[1] ? this.framebufferObject(a[1]) : null;
        if (object && !object.created) {
            object.created = true; object.shared = shared;
            c.group.framebufferReservations.delete(a[1]);
            if (shared) {
                c.group.sharedFramebuffers.set(a[1], object);
                for (const other of this.contexts.values()) if (other.group === c.group && !other.privateFramebuffers.has(a[1])) other.framebuffers.set(a[1], object);
            } else {
                c.privateFramebuffers.set(a[1], object);
                for (const other of this.contexts.values()) if (other !== c && other.group === c.group && other.framebuffers.get(a[1]) === object) other.framebuffers.delete(a[1]);
            }
        }
        buffers.bind(c, c.framebuffers, a[0], a[1], object);
    };
    proto.bindFramebufferEXT = function(bytes, view, offset, size) { this.bindFramebuffer(bytes, view, offset, size, true); };

    proto.boundFramebuffer = function(target) {
        const c = this.current;
        const name = target === GL.READ_FRAMEBUFFER ? c.readFramebuffer : c.drawFramebuffer;
        return name ? buffers.owner(c, c.framebuffers, target === GL.READ_FRAMEBUFFER) || null : null;
    };

    proto.attach = function(target, attachment, record) {
        const fbo = this.boundFramebuffer(target);
        if (!fbo) return;
        if (attachment === GL.DEPTH_ATTACHMENT) fbo.depth = record;
        else if (attachment === GL.STENCIL_ATTACHMENT) fbo.stencil = record;
        else if (attachment === GL.DEPTH_STENCIL_ATTACHMENT) fbo.depth = fbo.stencil = record;
        else {
            const index = attachment - GL.COLOR_ATTACHMENT0;
            if (index >= 0 && index < MAX_COLOR_ATTACHMENTS) fbo.color[index] = record;
        }
    };

    // An object deleted while attached to a framebuffer object bound here
    proto.detach = function(kind, id) {
        const c = this.current;
        for (const fbo of new Set([this.boundFramebuffer(GL.DRAW_FRAMEBUFFER), this.boundFramebuffer(GL.READ_FRAMEBUFFER)])) {
            if (!fbo) continue;
            const stale = record => record && record.kind === kind && record.id === id;
            fbo.color = fbo.color.map(record => stale(record) ? null : record);
            if (stale(fbo.depth)) fbo.depth = null;
            if (stale(fbo.stencil)) fbo.stencil = null;
        }
    };

    // glFramebufferTexture* (target, attachment, textarget, texture, level, zoffset)
    proto.framebufferTexture = function(bytes, view, offset, size) {
        const a = this.args("FRAMEBUFFER_TEXTURE", view, offset, size);
        if (!a) return;
        const face = a[2] - GL.TEXTURE_CUBE_MAP_POSITIVE_X;
        this.attach(a[0], a[1], a[3] ? { kind: "texture", name: a[3], level: a[4],
            id: this.current.group.textures.get(a[3])?.id,
            layer: face >= 0 && face < 6 ? face : a[5] } : null);
    };

    // glFramebufferRenderbuffer (target, attachment, renderbuffertarget, renderbuffer)
    proto.framebufferRenderbuffer = function(bytes, view, offset, size) {
        const a = this.args("FRAMEBUFFER_RENDERBUFFER", view, offset, size);
        if (a) this.attach(a[0], a[1], a[3] ? { kind: "renderbuffer", name: a[3], level: 0, layer: 0,
            id: this.current.group.renderbuffers.get(a[3])?.id } : null);
    };

    proto.drawBuffer = function(bytes, view, offset, size) {
        const a = this.args("DRAW_BUFFER", view, offset, size);
        if (a) buffers.draw(this.current, this.current.framebuffers, [a[0]]);
    };

    proto.drawBuffers = function(bytes, view, offset, size) {
        const list = this.names("DRAW_BUFFERS", view, offset, size);
        if (list) buffers.draw(this.current, this.current.framebuffers, list);
    };

    proto.readBuffer = function(bytes, view, offset, size) {
        const a = this.args("READ_BUFFER", view, offset, size);
        if (a) buffers.read(this.current, this.current.framebuffers, a[0]);
    };

    // What an attachment holds: {width, height, internalFormat}, or null
    proto.attachedImage = function(record) {
        const group = this.current.group;
        if (record.kind === "renderbuffer") {
            const rb = group.renderbufferImages.get(record.id);
            return rb && rb.width && rb.height ? rb : null;
        }
        const texture = group.textureImages.get(record.id);
        const layer = texture && texture.levels.get(texture.target === GL.TEXTURE_3D ? 0 : record.layer);
        const level = layer && layer.get(record.level);
        if (!level || !level.width || !level.height) return null;
        // a 3D texture's attached layer must be one it has
        return texture.target === GL.TEXTURE_3D && (record.layer < 0 || record.layer >= level.depth) ? null : level;
    };

    // EXT_framebuffer_object's completeness. Two depth and stencil images
    // that are not the same one are UNSUPPORTED: WebGPU has one
    // depth-stencil attachment.
    proto.framebufferStatus = function(target) {
        const c = this.current;
        const name = target === GL.READ_FRAMEBUFFER ? c.readFramebuffer : c.drawFramebuffer;
        if (!name) return GL.FRAMEBUFFER_COMPLETE;
        const fbo = this.boundFramebuffer(target);
        if (!fbo) return GL.FRAMEBUFFER_UNDEFINED;
        let width = 0, height = 0, any = false, dimensions = true;
        const colorFormats = new Set();
        const complete = (record, accept) => {
            if (!record) return true;
            const image = this.attachedImage(record);
            if (!image || !accept(image.internalFormat >>> 0, image, record)) return false;
            if (!any) {
                width = image.width;
                height = image.height;
                any = true;
            } else if (image.width !== width || image.height !== height) {
                dimensions = false;
            }
            return true;
        };
        for (const record of fbo.color)
            if (!complete(record, (format, image, record) => {
                if (formats.COMPRESSED_FORMATS[format] || !["RGB", "RGBA"].includes(formats.BASE_INTERNAL_FORMATS[format])) return false;
                const storage = formats.storageFormatFor(format, this.capabilities?.features);
                const bits = record.kind === "renderbuffer" ? image.componentBits : formats.textureComponents(format, false, this.capabilities?.features);
                colorFormats.add(JSON.stringify([storage.gpu, storage.base, bits.slice(0, 4)]));
                return true;
            }))
                return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (!complete(fbo.depth, format => DEPTH_FORMATS.has(format)))
            return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (!complete(fbo.stencil, format => STENCIL_FORMATS.has(format)))
            return GL.FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (!any) return GL.FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
        if (!dimensions) return GL.FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT;
        // This profile exposes EXT_framebuffer_object, whose colour images
        // must share an internal format. ARB FBO's relaxed rule is not exposed.
        if (colorFormats.size > 1) return GL.FRAMEBUFFER_INCOMPLETE_FORMATS_EXT;
        const same = (x, y) => x.kind === y.kind && x.id === y.id && x.level === y.level && x.layer === y.layer;
        if (fbo.depth && fbo.stencil && !same(fbo.depth, fbo.stencil)) return GL.FRAMEBUFFER_UNSUPPORTED_EXT;
        // a buffer that is not an attachment (BACK on an FBO) means the first
        const index = buffer => buffer >= GL.COLOR_ATTACHMENT0 ? buffer - GL.COLOR_ATTACHMENT0 : 0;
        if (target !== GL.READ_FRAMEBUFFER)
            for (const buffer of c.drawBuffers)
                if (buffer !== GL.NONE && !fbo.color[index(buffer)]) return GL.FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT;
        if (target !== GL.DRAW_FRAMEBUFFER && c.readBuffer !== GL.NONE && !fbo.color[index(c.readBuffer)])
            return GL.FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT;
        return GL.FRAMEBUFFER_COMPLETE;
    };

    // {target, status, result, reserved}
    proto.checkFramebufferStatus = function(bytes, view, offset, size) {
        const R = F.CHECK_FRAMEBUFFER_STATUS;
        if (size < RECORDS.CHECK_FRAMEBUFFER_STATUS.size || view.getUint32(offset + R.status, true) !== PENDING) return;
        const target = view.getUint32(offset + R.target, true);
        const error = this.current.state?.begin ? GL.INVALID_OPERATION :
            [GL.FRAMEBUFFER_EXT, GL.READ_FRAMEBUFFER, GL.DRAW_FRAMEBUFFER].includes(target) ? 0 : GL.INVALID_ENUM;
        if (error) { this.answered("CHECK_FRAMEBUFFER_STATUS", view, offset, C.V86GL_SYNC_QUERY_STATUS_FAILED); return error; }
        view.setUint32(offset + R.result, this.framebufferStatus(target), true);
        view.setUint32(offset + R.status, OK, true);
        ++this.stats.answered;
    };

    GLShadow.install({
        [GLFN.GEN_TEXTURES]: "genTextures",
        [GLFN.DELETE_TEXTURES]: "deleteTextures",
        [GLFN.BIND_TEXTURE]: "bindTexture",
        [GLFN.ACTIVE_TEXTURE]: "activeTexture",
        [GLFN.TEX_IMAGE_1D]: "texImage1D",
        [GLFN.TEX_IMAGE_2D]: "texImage2D",
        [GLFN.TEX_IMAGE_3D]: "texImage3D",
        [GLFN.COMPRESSED_TEX_IMAGE_1D]: "compressedTexImage1D",
        [GLFN.COMPRESSED_TEX_IMAGE_2D]: "compressedTexImage2D",
        [GLFN.COMPRESSED_TEX_IMAGE_3D]: "compressedTexImage3D",
        [GLFN.COPY_TEX_IMAGE_1D]: "copyTexImage1D",
        [GLFN.COPY_TEX_IMAGE_2D]: "copyTexImage2D",
        [GLFN.GENERATE_MIPMAP]: "generateMipmap",
        [GLFN.GEN_RENDERBUFFERS]: "genRenderbuffers",
        [GLFN.DELETE_RENDERBUFFERS]: "deleteRenderbuffers",
        [GLFN.BIND_RENDERBUFFER]: "bindRenderbuffer",
        [GLFN.RENDERBUFFER_STORAGE]: "renderbufferStorage",
        [GLFN.GEN_FRAMEBUFFERS]: "genFramebuffers",
        [GLFN.DELETE_FRAMEBUFFERS]: "deleteFramebuffers",
        [GLFN.BIND_FRAMEBUFFER]: "bindFramebuffer",
        [GLFN.BIND_FRAMEBUFFER_EXT]: "bindFramebufferEXT",
        [GLFN.FRAMEBUFFER_TEXTURE]: "framebufferTexture",
        [GLFN.FRAMEBUFFER_TEXTURE_1D]: "framebufferTexture",
        [GLFN.FRAMEBUFFER_TEXTURE_2D]: "framebufferTexture",
        [GLFN.FRAMEBUFFER_TEXTURE_3D]: "framebufferTexture",
        [GLFN.FRAMEBUFFER_RENDERBUFFER]: "framebufferRenderbuffer",
        [GLFN.DRAW_BUFFER]: "drawBuffer",
        [GLFN.DRAW_BUFFERS]: "drawBuffers",
        [GLFN.READ_BUFFER]: "readBuffer",
        [GLFN.CHECK_FRAMEBUFFER_STATUS]: "checkFramebufferStatus",
    });

    const levelsOut = texture => [...texture.levels].map(([layer, levels]) => [layer, [...levels]]);
    const levelsIn = saved => new Map(saved.map(([layer, levels]) => [layer, new Map(levels)]));

    function collectImages(shadow) {
        for (const group of shadow.groups.values()) {
            const live = new Set();
            const held = new Set([...group.sharedFramebuffers.values(), ...group.framebufferReservations.values()]);
            const units = list => { for (const unit of list) for (const id of unit.values()) live.add(id); };
            for (const c of shadow.contexts.values()) if (c.group === group) {
                units(c.unitImages); for (const id of c.defaultTextures.values()) live.add(id);
                live.add(c.renderbufferImage);
                if (c.textureRead) live.add(c.textureRead.imageId);
                for (const fbo of c.privateFramebuffers.values()) held.add(fbo);
                if (c.drawFramebufferObject) held.add(c.drawFramebufferObject);
                if (c.readFramebufferObject) held.add(c.readFramebufferObject);
                for (const a of c.state?.attrib || []) {
                    const images = a.parts?.images;
                    if (images) units(images.unitImages.map(u => new Map(u)));
                }
            }
            for (const [id, fbo] of group.framebufferObjects) {
                if (!held.has(fbo)) { group.framebufferObjects.delete(id); continue; }
                for (const r of [...fbo.color, fbo.depth, fbo.stencil]) if (r) live.add(r.id);
            }
            for (const pool of [group.textureImages, group.renderbufferImages])
                for (const [id, image] of pool) if ((image.deleted || !image.name) && !live.has(id)) pool.delete(id);
        }
    }

    GLShadow.part({
        afterBatch: collectImages,
        command(shadow, fn, bytes, view, offset) {
            if (AUTO_MIPMAP_UPDATES.has(fn) && shadow.current)
                shadow.autoMipmap(view.getUint32(offset, true), view.getInt32(offset + 4, true));
        },
        current(shadow, c, view, offset, size) {
            if (c.buffersInitialized) { buffers.sync(c, c.framebuffers); return; }
            const first = shadow.field("MAKE_CURRENT", "format", view, offset, size) & C.V86GL_FORMAT_SINGLE_BUFFER ? GL.FRONT : GL.BACK;
            c.defaultFramebufferState = buffers.initial(first); buffers.sync(c, c.framebuffers); c.buffersInitialized = true;
        },
        group(group) {
            group.textures = new Map();
            group.renderbuffers = new Map();
            group.textureImages = new Map();
            group.renderbufferImages = new Map();
            group.nextImageId = 1;
            group.framebufferObjects = new Map(); group.sharedFramebuffers = new Map(); group.framebufferReservations = new Map(); group.nextFramebufferId = 1;
        },
        context(context) {
            context.activeTexture = 0;
            context.units = Array.from({ length: MAX_TEXTURE_UNITS }, () => new Map());
            context.unitImages = Array.from({ length: MAX_TEXTURE_UNITS }, () => new Map());
            context.defaultTextures = new Map();
            context.proxyImages = new Map();
            context.framebuffers = new Map();
            context.privateFramebuffers = new Map();
            context.drawFramebufferObject = context.readFramebufferObject = null;
            context.drawFramebuffer = 0;
            context.readFramebuffer = 0;
            context.renderbuffer = 0;
            context.renderbufferImage = 0;
            context.drawBuffers = [GL.BACK];
            context.readBuffer = GL.BACK;
            context.defaultFramebufferState = buffers.initial(GL.BACK);
        },
        getState(shadow, state) {
            state.objects = {
                version: 3,
                groups: [...shadow.groups.values()].map(group => ({ key: group.key,
                    nextImageId: group.nextImageId,
                    textures: [...group.textureImages.values()].map(t => [t.name, t.target, levelsOut(t), t.id, t.deleted, t.parameters]),
                    renderbuffers: [...group.renderbufferImages.values()], nextFramebufferId: group.nextFramebufferId,
                    framebuffers: [...group.framebufferObjects.values()], sharedFramebuffers: [...group.sharedFramebuffers].map(([n, f]) => [n, f.id]),
                    framebufferReservations: [...group.framebufferReservations].map(([n, f]) => [n, f.id]) })),
                contexts: [...shadow.contexts.values()].map(c => ({ key: c.key, activeTexture: c.activeTexture,
                    units: c.units.map(unit => [...unit]), framebuffers: [...c.framebuffers].map(([n, f]) => [n, f.id]),
                    privateFramebuffers: [...c.privateFramebuffers].map(([n, f]) => [n, f.id]),
                    drawFramebufferId: c.drawFramebufferObject?.id || 0, readFramebufferId: c.readFramebufferObject?.id || 0,
                    unitImages: c.unitImages.map(unit => [...unit]), defaultTextures: [...c.defaultTextures],
                    proxyImages: [...c.proxyImages].map(([target, levels]) => [target, [...levels]]),
                    renderbufferImage: c.renderbufferImage,
                    defaultFramebufferState: c.defaultFramebufferState,
                    drawFramebuffer: c.drawFramebuffer, readFramebuffer: c.readFramebuffer, renderbuffer: c.renderbuffer,
                    drawBuffers: c.drawBuffers, readBuffer: c.readBuffer })),
            };
        },
        setState(shadow, state) {
            const saved = state.objects;
            if (!saved) return;
            for (const g of saved.groups) {
                const group = shadow.groups.get(g.key);
                if (!group) continue;
                group.nextImageId = g.nextImageId || 1;
                group.nextFramebufferId = g.nextFramebufferId || 1;
                for (const fbo of g.framebuffers || []) group.framebufferObjects.set(fbo.id, fbo);
                for (const [n, id] of g.sharedFramebuffers || []) group.sharedFramebuffers.set(n, group.framebufferObjects.get(id));
                for (const [n, id] of g.framebufferReservations || []) group.framebufferReservations.set(n, group.framebufferObjects.get(id));
                for (const [name, target, levels, savedId, deleted, parameters] of g.textures) {
                    const t = { name, target, levels: levelsIn(levels), id: savedId || group.nextImageId++, deleted: !!deleted, parameters };
                    group.textureImages.set(t.id, t); if (name && !deleted) group.textures.set(name, t);
                }
                for (const savedRb of g.renderbuffers) {
                    const rb = { ...savedRb, id: savedRb.id || group.nextImageId++ };
                    if (!rb.componentBits) {
                        const storage = rb.width && rb.height ? formats.renderbufferFormatFor(rb.internalFormat, shadow.capabilities?.features) : null;
                        rb.internalFormat = storage ? storage.internalFormat || rb.internalFormat : GL.RGBA;
                        rb.componentBits = storage ? storage.components : [0, 0, 0, 0, 0, 0];
                        if (!storage) rb.width = rb.height = 0;
                    }
                    group.renderbufferImages.set(rb.id, rb); if (!rb.deleted) group.renderbuffers.set(rb.name, rb);
                }
            }
            for (const c of saved.contexts) {
                const context = shadow.contexts.get(c.key);
                if (!context) continue;
                Object.assign(context, { activeTexture: c.activeTexture, units: c.units.map(unit => new Map(unit)),
                    unitImages: c.unitImages ? c.unitImages.map(unit => new Map(unit)) : c.units.map(unit =>
                        new Map(unit.map(([target, name]) => [target, context.group.textures.get(name)?.id]))),
                    defaultTextures: new Map(c.defaultTextures || []),
                    proxyImages: new Map((c.proxyImages || []).map(([target, levels]) => [target, new Map(levels)])),
                    renderbufferImage: c.renderbufferImage || context.group.renderbuffers.get(c.renderbuffer)?.id || 0,
                    framebuffers: new Map(saved.version >= 3 ? c.framebuffers.map(([n, id]) => [n, context.group.framebufferObjects.get(id)]) : c.framebuffers.map(fbo => [fbo.name, fbo])), drawFramebuffer: c.drawFramebuffer,
                    readFramebuffer: c.readFramebuffer, renderbuffer: c.renderbuffer, drawBuffers: c.drawBuffers,
                    readBuffer: c.readBuffer });
                context.defaultFramebufferState = c.defaultFramebufferState || buffers.initial(GL.BACK);
                if (saved.version >= 3) {
                    context.privateFramebuffers = new Map(c.privateFramebuffers.map(([n, id]) => [n, context.group.framebufferObjects.get(id)]));
                    context.drawFramebufferObject = context.group.framebufferObjects.get(c.drawFramebufferId) || null;
                    context.readFramebufferObject = context.group.framebufferObjects.get(c.readFramebufferId) || null;
                } else {
                    for (const fbo of context.framebuffers.values()) {
                        fbo.id = context.group.nextFramebufferId++; fbo.shared = false; fbo.deleted = false;
                        context.group.framebufferObjects.set(fbo.id, fbo);
                        (fbo.created === false ? context.group.framebufferReservations : context.privateFramebuffers).set(fbo.name, fbo);
                    }
                    context.drawFramebufferObject = context.framebuffers.get(c.drawFramebuffer) || null;
                    context.readFramebufferObject = context.framebuffers.get(c.readFramebuffer) || null;
                }
                for (const fbo of context.framebuffers.values()) {
                    if (!fbo.drawBuffers) fbo.drawBuffers = fbo.name === c.drawFramebuffer ? c.drawBuffers.slice() : [GL.COLOR_ATTACHMENT0];
                    if (fbo.readBuffer === undefined) fbo.readBuffer = fbo.name === c.readFramebuffer ? c.readBuffer : GL.COLOR_ATTACHMENT0;
                }
                if (!c.defaultFramebufferState) {
                    if (!c.drawFramebuffer) context.defaultFramebufferState.drawBuffers = c.drawBuffers.slice();
                    if (!c.readFramebuffer) context.defaultFramebufferState.readBuffer = c.readBuffer;
                }
                context.buffersInitialized = true;
                buffers.sync(context, context.framebuffers);
                if (!saved.version) for (const fbo of context.framebuffers.values())
                    for (const r of [...fbo.color, fbo.depth, fbo.stencil]) if (r)
                        r.id = (r.kind === "texture" ? context.group.textures : context.group.renderbuffers).get(r.name)?.id;
            }
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Desktop GL's typed state queries (plan C3). Unlike QUERY_INTEGER, which
// predates the CPU shadow and has only one int, QUERY_STATE carries vectors
// and doubles. Migrate a whole state family here before the guest stops
// answering it locally. Other families explicitly return UNSUPPORTED.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const { GLFN, F, RECORDS, C } = P;
    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
    const clone = GLShadow.clone;
    const identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    function multiply(a, b) {
        return Array.from({ length: 16 }, (_, i) => {
            const row = i % 4, col = i - row;
            return a[row] * b[col] + a[row + 4] * b[col + 1] +
                a[row + 8] * b[col + 2] + a[row + 12] * b[col + 3];
        });
    }

    // pname -> [initial values, GL state type, glPushAttrib group]. Colors
    // and depth's normalized conversion are distinct from ordinary floats.
    const DEFAULTS = new Map();
    const QUERIES = new Map(), PARAMETERS = new Map(), ATTRIB_PARTS = new Map();
    const define = (names, values, kind, group) => {
        for (const name of names.split(" ")) {
            if (GL[name] === undefined) throw new Error("unknown GL state " + name);
            DEFAULTS.set(GL[name], [values, kind || "i", group || 0]);
        }
    };
    define("VIEWPORT", [0, 0, 0, 0], "i", 0x800);
    define("SCISSOR_BOX", [0, 0, 0, 0], "i", 0x80000);
    define("DEPTH_RANGE", [0, 1], "n", 0x800);
    define("COLOR_CLEAR_VALUE ACCUM_CLEAR_VALUE", [0, 0, 0, 0], "n", 0x4000);
    define("CURRENT_INDEX", [1], "f", 1);
    define("INDEX_CLEAR_VALUE", [0], "f", 0x4000);
    define("INDEX_WRITEMASK", [0xFFFFFFFF], "u", 0x4000);
    DEFAULTS.get(GL.ACCUM_CLEAR_VALUE)[2] = 0x200;
    define("DEPTH_CLEAR_VALUE", [1], "n", 0x100);
    define("STENCIL_CLEAR_VALUE", [0], "i", 0x400);
    define("DEPTH_FUNC", [GL.LESS], "i", 0x100);
    define("DEPTH_WRITEMASK", [1], "b", 0x100);
    define("COLOR_WRITEMASK", [1, 1, 1, 1], "b", 0x4000);
    define("CULL_FACE_MODE", [GL.BACK], "i", 0x8);
    define("FRONT_FACE", [GL.CCW], "i", 0x8);
    define("SHADE_MODEL", [GL.SMOOTH], "i", 0x40);
    define("LINE_WIDTH POINT_SIZE", [1], "f", 0x4);
    DEFAULTS.get(GL.POINT_SIZE)[2] = 0x2;
    define("POLYGON_MODE", [GL.FILL, GL.FILL], "i", 0x8);
    define("POLYGON_OFFSET_FACTOR POLYGON_OFFSET_UNITS", [0], "f", 0x8);
    define("LINE_STIPPLE_REPEAT", [1], "i", 0x4);
    define("LINE_STIPPLE_PATTERN", [65535], "i", 0x4);
    define("BLEND_SRC_RGB BLEND_SRC_ALPHA", [GL.ONE], "i", 0x4000);
    define("BLEND_DST_RGB BLEND_DST_ALPHA", [GL.ZERO], "i", 0x4000);
    define("BLEND_EQUATION_RGB BLEND_EQUATION_ALPHA", [GL.FUNC_ADD], "i", 0x4000);
    // GL_BLEND_COLOR (not present in older generated constant tables).
    DEFAULTS.set(0x8005, [[0, 0, 0, 0], "n", 0x4000]);
    define("ALPHA_TEST_FUNC", [GL.ALWAYS], "i", 0x4000);
    define("ALPHA_TEST_REF", [0], "n", 0x4000);
    define("SAMPLE_COVERAGE_VALUE", [1], "f", 0x20000000);
    define("SAMPLE_COVERAGE_INVERT", [0], "b", 0x20000000);
    define("PACK_ALIGNMENT UNPACK_ALIGNMENT", [4]);
    const STORE_BOOLEAN = new Set([GL.PACK_SWAP_BYTES, GL.PACK_LSB_FIRST, GL.UNPACK_SWAP_BYTES, GL.UNPACK_LSB_FIRST]);
    const STORE = new Set([GL.PACK_ALIGNMENT, GL.UNPACK_ALIGNMENT, ...STORE_BOOLEAN,
        GL.PACK_ROW_LENGTH, GL.PACK_SKIP_ROWS, GL.PACK_SKIP_PIXELS, GL.PACK_SKIP_IMAGES, GL.PACK_IMAGE_HEIGHT,
        GL.UNPACK_ROW_LENGTH, GL.UNPACK_SKIP_ROWS, GL.UNPACK_SKIP_PIXELS, GL.UNPACK_SKIP_IMAGES, GL.UNPACK_IMAGE_HEIGHT]);
    for (const pname of STORE) if (!DEFAULTS.has(pname)) DEFAULTS.set(pname, [[0], STORE_BOOLEAN.has(pname) ? "b" : "i", 0]);
    const COMPARE = value => value >= GL.NEVER && value <= GL.ALWAYS;
    const FACTORS = new Set([0, 1, GL.SRC_COLOR, GL.ONE_MINUS_SRC_COLOR, GL.DST_COLOR, GL.ONE_MINUS_DST_COLOR,
        GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA, GL.DST_ALPHA, GL.ONE_MINUS_DST_ALPHA,
        GL.CONSTANT_COLOR, GL.ONE_MINUS_CONSTANT_COLOR, GL.CONSTANT_ALPHA, GL.ONE_MINUS_CONSTANT_ALPHA]);
    const EQUATIONS = new Set([GL.FUNC_ADD, GL.FUNC_SUBTRACT, GL.FUNC_REVERSE_SUBTRACT, GL.MIN, GL.MAX]);
    const SETTERS = new Map();
    const WITHIN_BEGIN = new Set();
    const on = (name, fn, withinBegin) => { SETTERS.set(GLFN[name], fn); if (withinBegin) WITHIN_BEGIN.add(GLFN[name]); };
    const set = (c, pname, values) => { c.state.values[pname] = values; };
    function rule(name, check, withinBegin) {
        GLShadow.rules({ [GLFN[name]]: (shadow, bytes, view, offset, size) => {
            const c = shadow.current;
            if (c.state.begin && !withinBegin) return GL.INVALID_OPERATION;
            const a = P.SIGNATURES[name] ? shadow.args(name, view, offset, size) : [];
            return check ? check(c, a, shadow) : 0;
        } });
    }
    const scalar = (name, pname, valid, convert, error = GL.INVALID_ENUM) => {
        rule(name, (c, a) => valid && !valid(a[0]) ? error : 0);
        on(name, (c, a) => set(c, pname, [convert ? convert(a[0]) : a[0]]));
    };
    on("VIEWPORT", (c, a, shadow, view, offset) => {
        const limit = shadow.capabilities && caps.integer(shadow.capabilities, GL.MAX_VIEWPORT_DIMS);
        if (limit) {
            a[2] = Math.min(a[2], limit); a[3] = Math.min(a[3], limit);
            // The page receives the same accepted, clamped rectangle.
            view.setInt32(offset + 8, a[2], true); view.setInt32(offset + 12, a[3], true);
        }
        set(c, GL.VIEWPORT, a);
    });
    on("SCISSOR", (c, a) => { if (a[2] >= 0 && a[3] >= 0) set(c, GL.SCISSOR_BOX, a); });
    on("DEPTH_RANGE", (c, a) => set(c, GL.DEPTH_RANGE, a.map(v => clamp(v, 0, 1))));
    on("CLEAR_COLOR", (c, a) => set(c, GL.COLOR_CLEAR_VALUE, a.map(v => clamp(v, 0, 1))));
    on("CLEAR_ACCUM", (c, a) => set(c, GL.ACCUM_CLEAR_VALUE, a.map(v => clamp(v, -1, 1))));
    scalar("CLEAR_DEPTH", GL.DEPTH_CLEAR_VALUE, null, v => clamp(v, 0, 1));
    scalar("CLEAR_STENCIL", GL.STENCIL_CLEAR_VALUE);
    scalar("DEPTH_FUNC", GL.DEPTH_FUNC, COMPARE);
    scalar("DEPTH_MASK", GL.DEPTH_WRITEMASK, null, v => +!!v);
    on("COLOR_MASK", (c, a) => set(c, GL.COLOR_WRITEMASK, a.map(v => +!!v)));
    scalar("CULL_FACE", GL.CULL_FACE_MODE, v => [GL.FRONT, GL.BACK, GL.FRONT_AND_BACK].includes(v));
    scalar("FRONT_FACE", GL.FRONT_FACE, v => v === GL.CW || v === GL.CCW);
    scalar("SHADE_MODEL", GL.SHADE_MODEL, v => v === GL.FLAT || v === GL.SMOOTH);
    scalar("LINE_WIDTH", GL.LINE_WIDTH, v => v > 0, null, GL.INVALID_VALUE);
    scalar("POINT_SIZE", GL.POINT_SIZE, v => v > 0, null, GL.INVALID_VALUE);
    on("POLYGON_MODE", (c, a) => {
        if (![GL.FRONT, GL.BACK, GL.FRONT_AND_BACK].includes(a[0]) || ![GL.POINT, GL.LINE, GL.FILL].includes(a[1])) return;
        const modes = c.state.values[GL.POLYGON_MODE].slice();
        if (a[0] !== GL.BACK) modes[0] = a[1];
        if (a[0] !== GL.FRONT) modes[1] = a[1];
        set(c, GL.POLYGON_MODE, modes);
    });
    on("POLYGON_OFFSET", (c, a) => { set(c, GL.POLYGON_OFFSET_FACTOR, [a[0]]); set(c, GL.POLYGON_OFFSET_UNITS, [a[1]]); });
    on("LINE_STIPPLE", (c, a) => { set(c, GL.LINE_STIPPLE_REPEAT, [clamp(a[0], 1, 256)]); set(c, GL.LINE_STIPPLE_PATTERN, [a[1] & 65535]); });
    function blend(c, a) {
        if (!a.every((v, i) => FACTORS.has(v) || i % 2 === 0 && v === GL.SRC_ALPHA_SATURATE)) return;
        [GL.BLEND_SRC_RGB, GL.BLEND_DST_RGB, GL.BLEND_SRC_ALPHA, GL.BLEND_DST_ALPHA].forEach((p, i) => set(c, p, [a[i]]));
    }
    on("BLEND_FUNC", (c, a) => {
        if ((!FACTORS.has(a[0]) && a[0] !== GL.SRC_ALPHA_SATURATE) || !FACTORS.has(a[1])) return;
        [GL.BLEND_SRC_RGB, GL.BLEND_DST_RGB, GL.BLEND_SRC_ALPHA, GL.BLEND_DST_ALPHA].forEach((p, i) => set(c, p, [a[i % 2]]));
    });
    on("BLEND_FUNC_SEPARATE", blend);
    on("BLEND_EQUATION", (c, a) => { if (EQUATIONS.has(a[0])) for (const p of [GL.BLEND_EQUATION_RGB, GL.BLEND_EQUATION_ALPHA]) set(c, p, a); });
    on("BLEND_EQUATION_SEPARATE", (c, a) => {
        if (a.every(v => EQUATIONS.has(v))) { set(c, GL.BLEND_EQUATION_RGB, [a[0]]); set(c, GL.BLEND_EQUATION_ALPHA, [a[1]]); }
    });
    on("BLEND_COLOR", (c, a) => set(c, 0x8005, a.map(v => clamp(v, 0, 1))));
    on("ALPHA_FUNC", (c, a) => { if (COMPARE(a[0])) { set(c, GL.ALPHA_TEST_FUNC, [a[0]]); set(c, GL.ALPHA_TEST_REF, [clamp(a[1], 0, 1)]); } });
    on("SAMPLE_COVERAGE", (c, a) => { set(c, GL.SAMPLE_COVERAGE_VALUE, [clamp(a[0], 0, 1)]); set(c, GL.SAMPLE_COVERAGE_INVERT, [+!!a[1]]); });
    on("PIXEL_STOREI", (c, [p, value]) => {
        if (!STORE.has(p)) return;
        if (p === GL.PACK_ALIGNMENT || p === GL.UNPACK_ALIGNMENT) { if (![1, 2, 4, 8].includes(value)) return; }
        else if (STORE_BOOLEAN.has(p)) value = +!!value;
        else if (value < 0) return;
        set(c, p, [value]);
    });
    function stack(c) { return c.state.mode === GL.TEXTURE ? c.state.texture[c.activeTexture || 0] : c.state.stacks[c.state.mode]; }
    const programMatrix = mode => mode >= GL.MATRIX0_ARB && mode < GL.MATRIX0_ARB + caps.LIMITS.MAX_PROGRAM_MATRICES;
    const matrixLimit = (c, shadow) => c.state.mode === GL.MODELVIEW ? caps.LIMITS.MODELVIEW_STACK_DEPTH :
        programMatrix(c.state.mode) ? (shadow.capabilities && caps.integer(shadow.capabilities, GL.MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB)) ?? caps.LIMITS.PROGRAM_MATRIX_STACK_DEPTH :
        c.state.mode === GL.COLOR && shadow.capabilities ? caps.integer(shadow.capabilities, GL.MAX_COLOR_MATRIX_STACK_DEPTH_SGI) ?? caps.LIMITS.OTHER_STACK_DEPTH : caps.LIMITS.OTHER_STACK_DEPTH;
    on("MATRIX_MODE", (c, [mode]) => { c.state.mode = mode; });
    on("LOAD_IDENTITY", c => { const s = stack(c); s[s.length - 1] = identity(); });
    on("PUSH_MATRIX", (c, a, shadow) => { const s = stack(c); if (s.length < matrixLimit(c, shadow)) s.push(s[s.length - 1].slice()); });
    on("POP_MATRIX", c => { const s = stack(c); if (s.length > 1) s.pop(); });
    const mult = (c, m) => { const s = stack(c); s[s.length - 1] = multiply(s[s.length - 1], m).map(Math.fround); };
    on("TRANSLATEF", (c, [x, y, z]) => { const m = identity(); m[12] = x; m[13] = y; m[14] = z; mult(c, m); });
    on("SCALEF", (c, [x, y, z]) => { const m = identity(); m[0] = x; m[5] = y; m[10] = z; mult(c, m); });
    on("ROTATEF", (c, [angle, x, y, z]) => {
        const len = Math.hypot(x, y, z); if (!len) return;
        x /= len; y /= len; z /= len;
        const cs = Math.cos(angle * Math.PI / 180), sn = Math.sin(angle * Math.PI / 180), t = 1 - cs;
        mult(c, [t*x*x+cs, t*x*y+sn*z, t*x*z-sn*y, 0, t*x*y-sn*z, t*y*y+cs, t*y*z+sn*x, 0,
            t*x*z+sn*y, t*y*z-sn*x, t*z*z+cs, 0, 0, 0, 0, 1]);
    });
    on("ORTHO", (c, [l, r, b, t, n, f]) => {
        if (l === r || b === t || n === f) return;
        mult(c, [2/(r-l), 0, 0, 0, 0, 2/(t-b), 0, 0, 0, 0, -2/(f-n), 0, -(r+l)/(r-l), -(t+b)/(t-b), -(f+n)/(f-n), 1]);
    });
    on("FRUSTUM", (c, [l, r, b, t, n, f]) => {
        if (l === r || b === t || n <= 0 || f <= 0 || n === f) return;
        mult(c, [2*n/(r-l), 0, 0, 0, 0, 2*n/(t-b), 0, 0, (r+l)/(r-l), (t+b)/(t-b), -(f+n)/(f-n), -1, 0, 0, -2*f*n/(f-n), 0]);
    });

    // State families share the same validation path before forwarding.
    for (const name of ["VIEWPORT", "SCISSOR"])
        rule(name, (c, a) => a[2] < 0 || a[3] < 0 ? GL.INVALID_VALUE : 0);
    for (const name of ["DEPTH_RANGE", "CLEAR_COLOR", "CLEAR_ACCUM", "COLOR_MASK", "POLYGON_OFFSET", "LINE_STIPPLE",
            "BLEND_COLOR", "SAMPLE_COVERAGE", "LOAD_IDENTITY", "LOAD_MATRIXF", "MULT_MATRIXF", "TRANSLATEF", "SCALEF", "ROTATEF", "FLUSH", "FINISH"])
        rule(name);
    rule("POLYGON_MODE", (c, a) => [GL.FRONT, GL.BACK, GL.FRONT_AND_BACK].includes(a[0]) &&
        [GL.POINT, GL.LINE, GL.FILL].includes(a[1]) ? 0 : GL.INVALID_ENUM);
    rule("BLEND_FUNC", (c, a) => (FACTORS.has(a[0]) || a[0] === GL.SRC_ALPHA_SATURATE) && FACTORS.has(a[1]) ? 0 : GL.INVALID_ENUM);
    rule("BLEND_FUNC_SEPARATE", (c, a) => a.every((v, i) => FACTORS.has(v) || i % 2 === 0 && v === GL.SRC_ALPHA_SATURATE) ? 0 : GL.INVALID_ENUM);
    for (const name of ["BLEND_EQUATION", "BLEND_EQUATION_SEPARATE"])
        rule(name, (c, a) => a.every(v => EQUATIONS.has(v)) ? 0 : GL.INVALID_ENUM);
    rule("ALPHA_FUNC", (c, a) => COMPARE(a[0]) ? 0 : GL.INVALID_ENUM);
    rule("PIXEL_STOREI", (c, [p, v]) => {
        if (!STORE.has(p)) return GL.INVALID_ENUM;
        if (p === GL.PACK_ALIGNMENT || p === GL.UNPACK_ALIGNMENT) return [1, 2, 4, 8].includes(v) ? 0 : GL.INVALID_VALUE;
        return !STORE_BOOLEAN.has(p) && v < 0 ? GL.INVALID_VALUE : 0;
    });
    rule("MATRIX_MODE", (c, [mode]) => [GL.MODELVIEW, GL.PROJECTION, GL.TEXTURE, GL.COLOR].includes(mode) || programMatrix(mode) ? 0 : GL.INVALID_ENUM);
    rule("PUSH_MATRIX", (c, a, shadow) => stack(c).length >= matrixLimit(c, shadow) ? GL.STACK_OVERFLOW : 0);
    rule("POP_MATRIX", c => stack(c).length <= 1 ? GL.STACK_UNDERFLOW : 0);
    rule("ORTHO", (c, [l, r, b, t, n, f]) => l === r || b === t || n === f ? GL.INVALID_VALUE : 0);
    rule("FRUSTUM", (c, [l, r, b, t, n, f]) => l === r || b === t || n === f || n <= 0 || f <= 0 ? GL.INVALID_VALUE : 0);
    for (const [push, pop, property] of [["PUSH_ATTRIB", "POP_ATTRIB", "attrib"], ["PUSH_CLIENT_ATTRIB", "POP_CLIENT_ATTRIB", "clientAttrib"]]) {
        rule(push, c => c.state[property].length >= (property === "attrib" ? caps.LIMITS.MAX_ATTRIB_STACK_DEPTH : caps.LIMITS.MAX_CLIENT_ATTRIB_STACK_DEPTH) ? GL.STACK_OVERFLOW : 0);
        rule(pop, c => c.state[property].length === 0 ? GL.STACK_UNDERFLOW : 0);
    }
    for (const name of ["ACTIVE_TEXTURE", "CLIENT_ACTIVE_TEXTURE"])
        rule(name, (c, [unit]) => unit >= GL.TEXTURE0 && unit < GL.TEXTURE0 + caps.LIMITS.MAX_TEXTURE_UNITS ? 0 : GL.INVALID_ENUM);
    rule("BEGIN", (c, [mode]) => c.state.begin ? GL.INVALID_OPERATION : mode <= GL.POLYGON ? 0 : GL.INVALID_ENUM, true);
    rule("END", c => c.state.begin ? 0 : GL.INVALID_OPERATION, true);
    for (const name of ["VERTEX3F", "VERTEX4F"]) rule(name, c => c.state.begin ? 0 : GL.INVALID_OPERATION, true);

    const proto = GLShadow.prototype;
    GLShadow.rules({ [GLFN.INDEX_STATE]: (shadow, bytes, view, offset) => {
        const r = P.decodeRecord("INDEX_STATE", view, offset);
        return r.operation > 2 ? GL.INVALID_ENUM : r.operation && shadow.current.state.begin ? GL.INVALID_OPERATION : 0;
    } });
    proto.indexState = function(bytes, view, offset) {
        const r = P.decodeRecord("INDEX_STATE", view, offset);
        set(this.current, [GL.CURRENT_INDEX, GL.INDEX_CLEAR_VALUE, GL.INDEX_WRITEMASK][r.operation], [r.operation === 2 ? r.mask : Math.fround(r.value)]);
    };
    GLShadow.install({ [GLFN.INDEX_STATE]: "indexState" });
    proto.stateValue = function(pname) {
        const c = this.current;
        if (!c) return null;
        const state = c.state;
        const query = QUERIES.get(pname);
        if (query) return query(this);
        if (DEFAULTS.has(pname)) return [state.values[pname], DEFAULTS.get(pname)[1]];
        if (pname === GL.BLEND_SRC) return [state.values[GL.BLEND_SRC_RGB], "i"];
        if (pname === GL.BLEND_DST) return [state.values[GL.BLEND_DST_RGB], "i"];
        const matrix = { [GL.MODELVIEW_MATRIX]: GL.MODELVIEW, [GL.PROJECTION_MATRIX]: GL.PROJECTION,
            [GL.TEXTURE_MATRIX]: GL.TEXTURE, [GL.COLOR_MATRIX_SGI]: GL.COLOR }[pname];
        if (matrix) { const s = matrix === GL.TEXTURE ? state.texture[c.activeTexture || 0] : state.stacks[matrix]; return [s[s.length - 1], "f"]; }
        const transposed = { [GL.TRANSPOSE_MODELVIEW_MATRIX]: GL.MODELVIEW, [GL.TRANSPOSE_PROJECTION_MATRIX]: GL.PROJECTION,
            [GL.TRANSPOSE_TEXTURE_MATRIX]: GL.TEXTURE, [GL.TRANSPOSE_COLOR_MATRIX]: GL.COLOR }[pname];
        if (transposed) {
            const s = transposed === GL.TEXTURE ? state.texture[c.activeTexture || 0] : state.stacks[transposed], m = s[s.length - 1];
            return [Array.from({ length: 16 }, (_, i) => m[(i % 4) * 4 + (i >> 2)]), "f"];
        }
        const depth = { [GL.MODELVIEW_STACK_DEPTH]: GL.MODELVIEW, [GL.PROJECTION_STACK_DEPTH]: GL.PROJECTION,
            [GL.TEXTURE_STACK_DEPTH]: GL.TEXTURE, [GL.COLOR_MATRIX_STACK_DEPTH_SGI]: GL.COLOR }[pname];
        if (depth) return [[(depth === GL.TEXTURE ? state.texture[c.activeTexture || 0] : state.stacks[depth]).length], "i"];
        if (pname === GL.CURRENT_MATRIX_ARB || pname === GL.TRANSPOSE_CURRENT_MATRIX_ARB) {
            const s = stack(c), m = s[s.length - 1];
            return [pname === GL.CURRENT_MATRIX_ARB ? m : Array.from({ length: 16 }, (_, i) => m[(i % 4) * 4 + (i >> 2)]), "f"];
        }
        if (pname === GL.CURRENT_MATRIX_STACK_DEPTH_ARB) return [[stack(c).length], "i"];
        if (pname === GL.MATRIX_MODE) return [[state.mode], "i"];
        if (pname === GL.CURRENT_PROGRAM) return [[c.program], "i"];
        if (pname === GL.ATTRIB_STACK_DEPTH || pname === GL.CLIENT_ATTRIB_STACK_DEPTH) return [[(pname === GL.ATTRIB_STACK_DEPTH ? state.attrib : state.clientAttrib).length], "i"];
        if (pname === GL.PROGRAM_ERROR_POSITION_ARB) return [[this.programErrorPosition?.() ?? -1], "i"];
        const vector = this.capabilities && caps.vector(this.capabilities, pname);
        if (vector) return [vector, pname === GL.COMPRESSED_TEXTURE_FORMATS ? "i" : "f"];
        if (pname === GL.ACTIVE_TEXTURE) return [[GL.TEXTURE0 + (c.activeTexture || 0)], "i"];
        if (pname === GL.CLIENT_ACTIVE_TEXTURE) return [[GL.TEXTURE0 + state.clientTexture], "i"];
        // Format queries belong to the selected drawable, not the adapter.
        if (pname === GL.DOUBLEBUFFER) return [[+(!c.drawFramebuffer && !(state.format & C.V86GL_FORMAT_SINGLE_BUFFER))], "b"];
        if (pname === GL.SAMPLES || pname === GL.SAMPLE_BUFFERS) {
            if (c.drawFramebuffer) return this.framebufferStateValue?.(pname) || null;
            const samples = (state.format & C.V86GL_FORMAT_SAMPLES_MASK) >>> C.V86GL_FORMAT_SAMPLES_SHIFT;
            return [[pname === GL.SAMPLES ? (samples > 1 ? samples : 0) : +(samples > 1)], "i"];
        }
        if (pname === GL.MAX_VIEWPORT_DIMS && this.capabilities) {
            const max = caps.integer(this.capabilities, pname); return [[max, max], "i"];
        }
        // Do not use the adapter's unconditional bit-depth answers for FBOs.
        if (c.drawFramebuffer && pname >= GL.RED_BITS && pname <= GL.STENCIL_BITS) return this.framebufferStateValue?.(pname) || null;
        const value = this.capabilities && caps.integer(this.capabilities, pname);
        return value !== undefined && value !== null ? [[value], "i"] : null;
    };

    proto.queryState = function(bytes, view, offset, size) {
        return this.queryTyped("QUERY_STATE", view, offset, size);
    };
    proto.queryParameter = function(bytes, view, offset, size) {
        return this.queryTyped("QUERY_PARAMETER", view, offset, size);
    };
    proto.queryTyped = function(name, view, offset, size) {
        if (!this.pending(name, view, offset, size)) return;
        const r = P.decodeRecord(name, view, offset);
        const header = RECORDS[name].size;
        const fail = error => {
            if (this.current && !this.current.error) this.current.error = error;
            view.setUint32(offset + F[name].error, error, true);
            this.answered(name, view, offset, C.V86GL_SYNC_QUERY_STATUS_FAILED);
        };
        const parameter = name === "QUERY_PARAMETER";
        if (header + r.data_size > size || r.value_kind < C.V86GL_STATE_INTEGER ||
                r.value_kind > (parameter ? C.V86GL_STATE_UNSIGNED_INT : C.V86GL_STATE_IS_ENABLED) ||
                parameter && r.value_kind === C.V86GL_STATE_IS_ENABLED) return fail(GL.INVALID_VALUE);
        if (!this.current || this.current.state.begin) return fail(GL.INVALID_OPERATION);
        const isEnabled = r.value_kind === C.V86GL_STATE_IS_ENABLED;
        const resolver = parameter && PARAMETERS.get(r.query_kind);
        const result = parameter ? resolver && resolver(this, r.target, r.pname, r.index) :
            isEnabled ? this.enableStateValue && this.enableStateValue(r.pname) : this.stateValue(r.pname);
        if (typeof result === "number") return fail(result);
        if (isEnabled && !result) return fail(GL.INVALID_ENUM);
        if (!result) return fail(GL.INVALID_ENUM);
        const [values, kind] = result;
        if (values.length * 8 > r.data_size) return fail(GL.INVALID_VALUE);
        for (let i = 0; i < values.length; ++i) {
            let v = values[i];
            if (r.value_kind === C.V86GL_STATE_BOOLEAN || isEnabled) v = +!!v;
            else if (r.value_kind === C.V86GL_STATE_UNSIGNED_SHORT || r.value_kind === C.V86GL_STATE_UNSIGNED_INT) {
                const max = r.value_kind === C.V86GL_STATE_UNSIGNED_SHORT ? 65535 : 0xFFFFFFFF;
                v = kind === "n" ? (max === 65535 ? Math.round(clamp(v, 0, 1) * max) : Math.trunc(clamp(v, 0, 1) * max)) : Math.trunc(v) >>> 0;
                if (max === 65535) v &= max;
            }
            else if (r.value_kind === C.V86GL_STATE_INTEGER) {
                // Color/depth conversion scales then truncates (the Apple
                // GL 2.1 oracle), unlike an ordinary float's nearest integer.
                // Preserve the specified negative endpoint even though the
                // Apple driver returns -INT_MAX there (glGet's refpage).
                v = kind === "u" ? v | 0 : kind === "n" ? (v <= -1 ? -0x80000000 : Math.trunc(clamp(v, -1, 1) * 0x7FFFFFFF)) :
                    kind === "t" ? Math.trunc(v) : v < 0 ? -Math.round(-v) : Math.round(v);
                v = clamp(v, -0x80000000, 0x7FFFFFFF);
            } else if (r.value_kind === C.V86GL_STATE_FLOAT) v = Math.fround(v);
            view.setFloat64(offset + header + i * 8, v, true);
        }
        view.setUint32(offset + F[name].count, values.length, true);
        this.answered(name, view, offset, C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [GLFN.QUERY_STATE]: "queryState", [GLFN.QUERY_PARAMETER]: "queryParameter" });
    GLShadow.part({
        context(c) {
            c.state = { values: Object.fromEntries([...DEFAULTS].map(([p, [v]]) => [p, v.slice()])),
                mode: GL.MODELVIEW, stacks: { [GL.MODELVIEW]: [identity()], [GL.PROJECTION]: [identity()], [GL.COLOR]: [identity()] },
                texture: Array.from({ length: caps.LIMITS.MAX_TEXTURE_UNITS }, () => [identity()]),
                attrib: [], clientAttrib: [], clientTexture: 0, format: 0, initialized: false, begin: false };
            for (let i = 0; i < caps.LIMITS.MAX_PROGRAM_MATRICES; ++i) c.state.stacks[GL.MATRIX0_ARB + i] = [identity()];
        },
        current(shadow, c, view, offset, size) {
            if (!c.state.initialized) {
                const width = shadow.field("MAKE_CURRENT", "width", view, offset, size), height = shadow.field("MAKE_CURRENT", "height", view, offset, size);
                set(c, GL.VIEWPORT, [0, 0, width, height]); set(c, GL.SCISSOR_BOX, [0, 0, width, height]);
                c.state.format = shadow.field("MAKE_CURRENT", "format", view, offset, size);
                c.state.initialized = true;
            }
        },
        command(shadow, fn, bytes, view, offset, size) {
            const c = shadow.current; if (!c) return;
            const state = c.state;
            if (fn === GLFN.BEGIN) { if (size >= 4 && view.getUint32(offset, true) <= GL.POLYGON) state.begin = true; return; }
            if (fn === GLFN.END) { state.begin = false; return; }
            if (state.begin && !WITHIN_BEGIN.has(fn)) return;
            if (fn === GLFN.LOAD_MATRIXF || fn === GLFN.MULT_MATRIXF) {
                if (size < 64) return;
                const m = Array.from({ length: 16 }, (_, i) => view.getFloat32(offset + i * 4, true)), s = stack(c);
                s[s.length - 1] = fn === GLFN.LOAD_MATRIXF ? m : multiply(s[s.length - 1], m).map(Math.fround);
                return;
            }
            if (fn === GLFN.PUSH_ATTRIB || fn === GLFN.PUSH_CLIENT_ATTRIB) {
                if (size < 4) return;
                const mask = view.getUint32(offset, true), client = fn === GLFN.PUSH_CLIENT_ATTRIB;
                const list = client ? state.clientAttrib : state.attrib;
                if (list.length >= 16) return;
                const values = {};
                for (const [p, [, , group]] of DEFAULTS)
                    if (client ? STORE.has(p) && (mask & 1) : group && (mask & group)) values[p] = state.values[p].slice();
                list.push({ values, mode: !client && (mask & 0x1000) ? state.mode : null,
                    clientTexture: client && (mask & 2) ? state.clientTexture : null,
                    activeTexture: !client && (mask & 0x40000) ? c.activeTexture : null,
                    parts: Object.fromEntries([...ATTRIB_PARTS].map(([name, part]) => [name, part.capture(c, mask, client)])) });
                return;
            }
            if (fn === GLFN.POP_ATTRIB || fn === GLFN.POP_CLIENT_ATTRIB) {
                const saved = (fn === GLFN.POP_ATTRIB ? state.attrib : state.clientAttrib).pop();
                if (!saved) return;
                Object.assign(state.values, saved.values);
                if (saved.mode !== null) state.mode = saved.mode;
                if (saved.clientTexture !== null) state.clientTexture = saved.clientTexture;
                if (saved.activeTexture !== null && saved.activeTexture !== undefined) c.activeTexture = saved.activeTexture;
                for (const [name, part] of ATTRIB_PARTS) if (saved.parts && saved.parts[name]) part.restore(c, saved.parts[name]);
                return;
            }
            if (fn === GLFN.CLIENT_ACTIVE_TEXTURE) {
                if (size >= 4) { const unit = view.getUint32(offset, true) - GL.TEXTURE0; if (unit >= 0 && unit < caps.LIMITS.MAX_TEXTURE_COORDS) state.clientTexture = unit; }
                return;
            }
            const setter = SETTERS.get(fn);
            if (setter) { const a = shadow.args(P.NAME_BY_OPCODE[fn], view, offset, size); if (a) setter(c, a, shadow, view, offset); }
        },
        getState(shadow, state) { state.typed = [...shadow.contexts.values()].map(c => [c.key, clone(c.state)]); },
        setState(shadow, state) {
            for (const [key, saved] of state.typed || []) {
                const c = shadow.contexts.get(key); if (!c) continue;
                c.state = clone(saved);
                // Early snapshots stored EdgeFlag under MAX_LIST_NESTING's
                // enum. Migrate the current value and saved attribute values
                // before filling defaults for the corrected pname.
                for (const values of [c.state.values, ...(c.state.attrib || []).map(a => a.values)]) if (values && values[0x0B31] && !values[GL.CURRENT_EDGE_FLAG]) {
                    values[GL.CURRENT_EDGE_FLAG] = values[0x0B31]; delete values[0x0B31];
                }
                for (const [p, [values]] of DEFAULTS) if (!c.state.values[p]) c.state.values[p] = values.slice();
                // The first typed-state snapshot used the query pname as a
                // color stack key. Normalize that internal snapshot spelling.
                if (!c.state.stacks[GL.COLOR]) c.state.stacks[GL.COLOR] = c.state.stacks[GL.COLOR_MATRIX_SGI] || [identity()];
                delete c.state.stacks[GL.COLOR_MATRIX_SGI];
                for (let i = 0; i < caps.LIMITS.MAX_PROGRAM_MATRICES; ++i) if (!c.state.stacks[GL.MATRIX0_ARB + i]) c.state.stacks[GL.MATRIX0_ARB + i] = [identity()];
                if (c.state.mode === GL.COLOR_MATRIX_SGI) c.state.mode = GL.COLOR;
            }
        },
    });
    // State-family modules extend this registry, rather than adding another
    // copy of typed conversion, command walking or snapshot serialization.
    PARAMETERS.set(C.V86GL_PARAMETER_ATTACHED_SHADERS, (shadow, program, maximum, first) => {
        const group = shadow.current.group, object = group.programs?.get(program);
        if (!object) return group.shaders?.has(program) ? GL.INVALID_OPERATION : GL.INVALID_VALUE;
        if (maximum > 0x7FFFFFFF) return GL.INVALID_VALUE;
        return [[...object.attached].slice(first, first + Math.min(maximum, C.V86GL_MAX_TYPED_QUERY_VALUES)), "i"];
    });
    const api = { define, on, set, rule,
        query(pnames, fn) { for (const pname of pnames) QUERIES.set(pname, fn); },
        parameter(kind, fn) { PARAMETERS.set(kind, fn); },
        attributes(name, part) { ATTRIB_PARTS.set(name, part); } };
    global.V86GLShadowState = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// GL 2.1 enable bits, stencil state and hints on the CPU (plan C3/C6).
// Includes per-unit texture/texgen enables and per-client-unit array enables.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const { GLFN } = P;
    const clone = GLShadow.clone;
    // cap -> [default, server attribute group]; ENABLE_BIT saves all caps.
    const ENABLES = new Map();
    function enables(names, group, initial = 0) {
        for (const name of names.split(" ")) {
            if (GL[name] === undefined) throw new Error("unknown GL enable " + name);
            ENABLES.set(GL[name], [initial, group]);
        }
    }
    enables("ALPHA_TEST BLEND COLOR_LOGIC_OP INDEX_LOGIC_OP", 0x4000);
    enables("DITHER", 0x4000, 1);
    enables("DEPTH_TEST", 0x100); enables("STENCIL_TEST STENCIL_TEST_TWO_SIDE_EXT", 0x400);
    enables("CULL_FACE POLYGON_SMOOTH POLYGON_OFFSET_FILL POLYGON_OFFSET_LINE POLYGON_OFFSET_POINT", 0x8);
    enables("POLYGON_STIPPLE", 0x10); enables("SCISSOR_TEST", 0x80000);
    enables("LINE_SMOOTH LINE_STIPPLE", 0x4); enables("POINT_SMOOTH POINT_SPRITE", 0x2);
    enables("FOG", 0x80); enables("LIGHTING COLOR_MATERIAL COLOR_SUM", 0x40);
    enables("NORMALIZE RESCALE_NORMAL", 0x1000);
    enables("MULTISAMPLE", 0x20000000, 1);
    enables("SAMPLE_ALPHA_TO_COVERAGE SAMPLE_ALPHA_TO_ONE SAMPLE_COVERAGE", 0x20000000);
    enables("VERTEX_PROGRAM_ARB FRAGMENT_PROGRAM_ARB VERTEX_PROGRAM_POINT_SIZE VERTEX_PROGRAM_TWO_SIDE", 0);
    enables("AUTO_NORMAL", 0x10000); enables("DEPTH_CLAMP", 0x1000);
    for (let i = 0; i < caps.LIMITS.MAX_LIGHTS; ++i) ENABLES.set(GL.LIGHT0 + i, [0, 0x40]);
    for (let i = 0; i < caps.LIMITS.MAX_CLIP_PLANES; ++i) ENABLES.set(GL.CLIP_PLANE0 + i, [0, 0x1000]);
    for (let i = 0; i < 9; ++i) { ENABLES.set(GL.MAP1_COLOR_4 + i, [0, 0x10000]); ENABLES.set(GL.MAP2_COLOR_4 + i, [0, 0x10000]); }
    const UNIT_ENABLES = [GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_3D, GL.TEXTURE_CUBE_MAP, GL.TEXTURE_RECTANGLE,
        GL.TEXTURE_GEN_S, GL.TEXTURE_GEN_T, GL.TEXTURE_GEN_R, GL.TEXTURE_GEN_Q];
    const CLIENT_ENABLES = [GL.VERTEX_ARRAY, GL.NORMAL_ARRAY, GL.COLOR_ARRAY, GL.INDEX_ARRAY,
        GL.EDGE_FLAG_ARRAY, GL.SECONDARY_COLOR_ARRAY_EXT, GL.FOG_COORDINATE_ARRAY_EXT];
    const unitCap = cap => UNIT_ENABLES.includes(cap);
    const serverCap = (shadow, cap) => (ENABLES.has(cap) || unitCap(cap)) &&
        (cap !== GL.DEPTH_CLAMP || !!(shadow.capabilities && shadow.capabilities.features.depthClipControl));
    const clientCap = cap => CLIENT_ENABLES.includes(cap) || cap === GL.TEXTURE_COORD_ARRAY;
    const defaults = list => Object.fromEntries(list.map(cap => [cap, 0]));
    const newRaster = () => ({ enabled: Object.fromEntries([...ENABLES].map(([cap, [value]]) => [cap, value])),
        units: Array.from({ length: caps.LIMITS.MAX_TEXTURE_UNITS }, () => defaults(UNIT_ENABLES)),
        client: defaults(CLIENT_ENABLES), texcoord: Array(caps.LIMITS.MAX_TEXTURE_UNITS).fill(0) });
    GLShadow.prototype.enableStateValue = function(cap) {
        const c = this.current, r = c.state.raster;
        if (serverCap(this, cap)) return [[unitCap(cap) ? r.units[c.activeTexture || 0][cap] : r.enabled[cap]], "b"];
        if (clientCap(cap)) return [[cap === GL.TEXTURE_COORD_ARRAY ? r.texcoord[c.state.clientTexture] : r.client[cap]], "b"];
        return null;
    };
    for (const cap of [...ENABLES.keys(), ...UNIT_ENABLES, ...CLIENT_ENABLES, GL.TEXTURE_COORD_ARRAY])
        state.query([cap], shadow => shadow.enableStateValue(cap));
    for (const [name, value] of [["ENABLE", 1], ["DISABLE", 0]]) {
        state.rule(name, (c, [cap], shadow) => serverCap(shadow, cap) ? 0 : GL.INVALID_ENUM);
        state.on(name, (c, [cap]) => { const r = c.state.raster; if (unitCap(cap)) r.units[c.activeTexture || 0][cap] = value; else r.enabled[cap] = value; });
    }
    for (const [name, value] of [["ENABLE_CLIENT_STATE", 1], ["DISABLE_CLIENT_STATE", 0]]) {
        state.rule(name, (c, [cap]) => clientCap(cap) ? 0 : GL.INVALID_ENUM);
        state.on(name, (c, [cap]) => { const r = c.state.raster; if (cap === GL.TEXTURE_COORD_ARRAY) r.texcoord[c.state.clientTexture] = value; else r.client[cap] = value; });
    }
    state.attributes("raster", {
        capture(c, mask, client) {
            const r = c.state.raster;
            if (client) return mask & 2 ? { client: clone(r.client), texcoord: r.texcoord.slice() } : {};
            const enabled = {};
            for (const [cap, [, group]] of ENABLES) if (mask & (0x2000 | group)) enabled[cap] = r.enabled[cap];
            return { enabled, ...(mask & (0x2000 | 0x40000) ? { units: clone(r.units) } : {}) };
        },
        restore(c, saved) {
            const r = c.state.raster;
            if (saved.enabled) Object.assign(r.enabled, saved.enabled);
            if (saved.units) r.units = saved.units;
            if (saved.client) r.client = saved.client;
            if (saved.texcoord) r.texcoord = saved.texcoord;
        },
    });
    const STENCIL_FIELDS = [
        ["STENCIL_FUNC STENCIL_BACK_FUNC", GL.ALWAYS, "i"], ["STENCIL_REF STENCIL_BACK_REF", 0, "i"],
        ["STENCIL_VALUE_MASK STENCIL_BACK_VALUE_MASK STENCIL_WRITEMASK STENCIL_BACK_WRITEMASK", 0xFFFFFFFF, "u"],
        ["STENCIL_FAIL STENCIL_BACK_FAIL STENCIL_PASS_DEPTH_FAIL STENCIL_BACK_PASS_DEPTH_FAIL STENCIL_PASS_DEPTH_PASS STENCIL_BACK_PASS_DEPTH_PASS", GL.KEEP, "i"],
    ];
    for (const [names, value, kind] of STENCIL_FIELDS) state.define(names, [value], kind, 0x400);
    const FACES = [GL.FRONT, GL.BACK, GL.FRONT_AND_BACK];
    const COMPARE = func => func >= GL.NEVER && func <= GL.ALWAYS;
    const OPS = new Set([GL.KEEP, GL.ZERO, GL.REPLACE, GL.INCR, GL.DECR, GL.INVERT, GL.INCR_WRAP, GL.DECR_WRAP]);
    const faceState = (c, face, front, back, value) => {
        if (face !== GL.BACK) state.set(c, front, [value]);
        if (face !== GL.FRONT) state.set(c, back, [value]);
    };
    function stencilFunc(c, [face, func, ref, mask]) {
        faceState(c, face, GL.STENCIL_FUNC, GL.STENCIL_BACK_FUNC, func);
        faceState(c, face, GL.STENCIL_REF, GL.STENCIL_BACK_REF, ref);
        faceState(c, face, GL.STENCIL_VALUE_MASK, GL.STENCIL_BACK_VALUE_MASK, mask);
    }
    state.rule("STENCIL_FUNC", (c, [func]) => COMPARE(func) ? 0 : GL.INVALID_ENUM);
    state.on("STENCIL_FUNC", (c, a) => stencilFunc(c, [GL.FRONT_AND_BACK, ...a]));
    state.rule("STENCIL_FUNC_SEPARATE", (c, [face, func]) => FACES.includes(face) && COMPARE(func) ? 0 : GL.INVALID_ENUM);
    state.on("STENCIL_FUNC_SEPARATE", stencilFunc);
    for (const [name, separate] of [["STENCIL_MASK", false], ["STENCIL_MASK_SEPARATE", true]]) {
        state.rule(name, (c, a) => !separate || FACES.includes(a[0]) ? 0 : GL.INVALID_ENUM);
        state.on(name, (c, a) => faceState(c, separate ? a[0] : GL.FRONT_AND_BACK, GL.STENCIL_WRITEMASK, GL.STENCIL_BACK_WRITEMASK, a[separate ? 1 : 0]));
    }
    for (const [name, separate] of [["STENCIL_OP", false], ["STENCIL_OP_SEPARATE", true]]) {
        state.rule(name, (c, a) => (!separate || FACES.includes(a[0])) && a.slice(separate ? 1 : 0).every(op => OPS.has(op)) ? 0 : GL.INVALID_ENUM);
        state.on(name, (c, a) => {
            const face = separate ? a[0] : GL.FRONT_AND_BACK, values = a.slice(separate ? 1 : 0);
            for (const [i, front, back] of [[0, GL.STENCIL_FAIL, GL.STENCIL_BACK_FAIL], [1, GL.STENCIL_PASS_DEPTH_FAIL, GL.STENCIL_BACK_PASS_DEPTH_FAIL], [2, GL.STENCIL_PASS_DEPTH_PASS, GL.STENCIL_BACK_PASS_DEPTH_PASS]])
                faceState(c, face, front, back, values[i]);
        });
    }
    // Keep the unclamped ref: switching between a framebuffer with/without
    // stencil changes the queried clamped value, not the specified state.
    const STENCIL_BITS = new Map([[GL.STENCIL_INDEX1_EXT, 1], [GL.STENCIL_INDEX4_EXT, 4], [GL.STENCIL_INDEX16_EXT, 16],
        [GL.STENCIL_INDEX8_EXT, 8], [GL.STENCIL_INDEX, 8], [GL.DEPTH_STENCIL, 8], [GL.DEPTH24_STENCIL8, 8], [0x8CAD, 8]]);
    function stencilBits(shadow) {
        const c = shadow.current;
        if (!c.drawFramebuffer) return 8;
        const fbo = c.framebuffers.get(c.drawFramebuffer), image = fbo && fbo.stencil && shadow.attachedImage(fbo.stencil);
        if (!image) return 0;
        if (image.componentBits) return image.componentBits[5];
        return STENCIL_BITS.get(image.internalFormat) || 0;
    }
    for (const pname of [GL.STENCIL_REF, GL.STENCIL_BACK_REF]) state.query([pname], shadow =>
        [[Math.max(0, Math.min(2 ** stencilBits(shadow) - 1, shadow.current.state.values[pname][0]))], "i"]);
    const HINTS = [GL.PERSPECTIVE_CORRECTION_HINT, GL.POINT_SMOOTH_HINT, GL.LINE_SMOOTH_HINT, GL.POLYGON_SMOOTH_HINT,
        GL.FOG_HINT, GL.GENERATE_MIPMAP_HINT, GL.TEXTURE_COMPRESSION_HINT, GL.FRAGMENT_SHADER_DERIVATIVE_HINT];
    state.define("PERSPECTIVE_CORRECTION_HINT POINT_SMOOTH_HINT LINE_SMOOTH_HINT POLYGON_SMOOTH_HINT FOG_HINT GENERATE_MIPMAP_HINT TEXTURE_COMPRESSION_HINT FRAGMENT_SHADER_DERIVATIVE_HINT", [GL.DONT_CARE], "i", 0x8000);
    state.rule("HINT", (c, [target, mode]) => HINTS.includes(target) && [GL.DONT_CARE, GL.FASTEST, GL.NICEST].includes(mode) ? 0 : GL.INVALID_ENUM);
    state.on("HINT", (c, [target, mode]) => state.set(c, target, [mode]));
    GLShadow.part({
        context(c) { c.state.raster = newRaster(); },
        setState(shadow) { for (const c of shadow.contexts.values()) if (!c.state.raster) c.state.raster = newRaster(); },
    });
    const api = { stencilBits };
    global.V86GLShadowRaster = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Fixed-function parameter state on the CPU. Queries use the same typed
// reply path as glGet*v; matrix-dependent values are captured at set time.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const math = global.GLMath || require("./gl_math.js");
    const { C } = P;
    const clone = GLShadow.clone;
    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
    const COLORS = new Set([GL.AMBIENT, GL.DIFFUSE, GL.SPECULAR, GL.EMISSION, GL.AMBIENT_AND_DIFFUSE]);
    const LIGHT_COUNTS = { [GL.AMBIENT]: 4, [GL.DIFFUSE]: 4, [GL.SPECULAR]: 4, [GL.POSITION]: 4,
        [GL.SPOT_DIRECTION]: 3, [GL.SPOT_EXPONENT]: 1, [GL.SPOT_CUTOFF]: 1,
        [GL.CONSTANT_ATTENUATION]: 1, [GL.LINEAR_ATTENUATION]: 1, [GL.QUADRATIC_ATTENUATION]: 1 };
    const MATERIAL_COUNTS = { [GL.AMBIENT]: 4, [GL.DIFFUSE]: 4, [GL.SPECULAR]: 4, [GL.EMISSION]: 4,
        [GL.AMBIENT_AND_DIFFUSE]: 4, [GL.SHININESS]: 1, [GL.COLOR_INDEXES]: 3 };
    const MODEL = { [GL.LIGHT_MODEL_AMBIENT]: [4, "n"], [GL.LIGHT_MODEL_TWO_SIDE]: [1, "b"],
        [GL.LIGHT_MODEL_LOCAL_VIEWER]: [1, "b"], [GL.LIGHT_MODEL_COLOR_CONTROL]: [1, "i"] };
    const unit = c => c.state.fixed.units[c.activeTexture || 0];
    const top = c => c.state.stacks[GL.MODELVIEW].at(-1);
    const transform = (m, v) => Array.from(math.transformPoint4(m, v, new Float64Array(4)));
    const plane = (c, v) => transform(math.transpose4(math.invert4(top(c), new Float64Array(16)), new Float64Array(16)), v);
    // Integer color setters use the signed normalized mapping, while
    // positions, planes, shininess and enumerants are ordinary numbers.
    const normalized = v => Math.max(-1, v / 0x7FFFFFFF);
    function parameters(a, vector, start, color, integer) {
        const values = vector ? a.slice(start + 1, start + 1 + a[start]) : [a[start]];
        return integer && color ? values.map(v => Math.fround(normalized(v))) : values;
    }
    function material() {
        return { [GL.AMBIENT]: [0.2, 0.2, 0.2, 1], [GL.DIFFUSE]: [0.8, 0.8, 0.8, 1],
            [GL.SPECULAR]: [0, 0, 0, 1], [GL.EMISSION]: [0, 0, 0, 1], [GL.SHININESS]: [0], [GL.COLOR_INDEXES]: [0, 1, 1] };
    }
    function environment() {
        const e = { [GL.TEXTURE_ENV_MODE]: [GL.MODULATE], [GL.TEXTURE_ENV_COLOR]: [0, 0, 0, 0],
            [GL.COMBINE_RGB]: [GL.MODULATE], [GL.COMBINE_ALPHA]: [GL.MODULATE],
            [GL.RGB_SCALE]: [1], [GL.ALPHA_SCALE]: [1], [GL.TEXTURE_LOD_BIAS]: [0], [GL.COORD_REPLACE]: [0] };
        for (let i = 0; i < 3; ++i) {
            e[GL.SRC0_RGB + i] = e[GL.SRC0_ALPHA + i] = [[GL.TEXTURE, GL.PREVIOUS, GL.CONSTANT][i]];
            e[GL.OPERAND0_RGB + i] = [i === 2 ? GL.SRC_ALPHA : GL.SRC_COLOR];
            e[GL.OPERAND0_ALPHA + i] = [GL.SRC_ALPHA];
        }
        return e;
    }
    function initial() {
        return { lights: Array.from({ length: caps.LIMITS.MAX_LIGHTS }, (_, i) => ({
            [GL.AMBIENT]: [0, 0, 0, 1], [GL.DIFFUSE]: i ? [0, 0, 0, 1] : [1, 1, 1, 1],
            [GL.SPECULAR]: i ? [0, 0, 0, 1] : [1, 1, 1, 1], [GL.POSITION]: [0, 0, 1, 0],
            [GL.SPOT_DIRECTION]: [0, 0, -1], [GL.SPOT_EXPONENT]: [0], [GL.SPOT_CUTOFF]: [180],
            [GL.CONSTANT_ATTENUATION]: [1], [GL.LINEAR_ATTENUATION]: [0], [GL.QUADRATIC_ATTENUATION]: [0] })),
        materials: [material(), material()], clip: Array.from({ length: caps.LIMITS.MAX_CLIP_PLANES }, () => [0, 0, 0, 0]),
        units: Array.from({ length: caps.LIMITS.MAX_TEXTURE_UNITS }, () => ({ env: environment(),
            gen: Array.from({ length: 4 }, (_, i) => {
                const p = [+(i === 0), +(i === 1), 0, 0];
                return { [GL.TEXTURE_GEN_MODE]: [GL.EYE_LINEAR], [GL.OBJECT_PLANE]: p.slice(), [GL.EYE_PLANE]: p.slice() };
            }), coord: [0, 0, 0, 1] })) };
    }
    state.define("CURRENT_COLOR", [1, 1, 1, 1], "n", 1);
    state.define("CURRENT_NORMAL", [0, 0, 1], "f", 1);
    state.define("CURRENT_SECONDARY_COLOR_EXT", [0, 0, 0, 1], "n", 1);
    state.define("CURRENT_FOG_COORDINATE_EXT", [0], "f", 1);
    state.define("LIGHT_MODEL_AMBIENT", [0.2, 0.2, 0.2, 1], "n", 0x40);
    state.define("LIGHT_MODEL_TWO_SIDE LIGHT_MODEL_LOCAL_VIEWER", [0], "b", 0x40);
    state.define("LIGHT_MODEL_COLOR_CONTROL", [GL.SINGLE_COLOR], "i", 0x40);
    state.define("COLOR_MATERIAL_FACE", [GL.FRONT_AND_BACK], "i", 0x40);
    state.define("COLOR_MATERIAL_PARAMETER", [GL.AMBIENT_AND_DIFFUSE], "i", 0x40);
    state.query([GL.CURRENT_TEXTURE_COORDS], shadow => [unit(shadow.current).coord, "f"]);
    function latch(c) {
        if (!c.state.raster || !c.state.raster.enabled[GL.COLOR_MATERIAL]) return;
        const face = c.state.values[GL.COLOR_MATERIAL_FACE][0], mode = c.state.values[GL.COLOR_MATERIAL_PARAMETER][0];
        c.state.fixed.materials.forEach((m, i) => {
            if (face === (i ? GL.FRONT : GL.BACK)) return;
            for (const p of mode === GL.AMBIENT_AND_DIFFUSE ? [GL.AMBIENT, GL.DIFFUSE] : [mode])
                m[p] = c.state.values[GL.CURRENT_COLOR].slice();
        });
    }
    for (const [name, pname] of [["COLOR4F", GL.CURRENT_COLOR], ["NORMAL3F", GL.CURRENT_NORMAL],
            ["SECONDARY_COLOR3F", GL.CURRENT_SECONDARY_COLOR_EXT], ["FOG_COORDF", GL.CURRENT_FOG_COORDINATE_EXT]]) {
        state.rule(name, null, true);
        state.on(name, (c, a) => { state.set(c, pname, name === "SECONDARY_COLOR3F" ? [...a, 1] : a); if (name === "COLOR4F") latch(c); }, true);
    }
    state.rule("TEX_COORD2F", null, true);
    state.on("TEX_COORD2F", (c, a) => { c.state.fixed.units[0].coord = [...a, 0, 1]; }, true);
    state.rule("TEX_COORD4F", null, true);
    state.on("TEX_COORD4F", (c, a) => { c.state.fixed.units[0].coord = a.slice(); }, true);
    state.rule("MULTI_TEX_COORD4F", (c, a) => a[0] >= GL.TEXTURE0 && a[0] < GL.TEXTURE0 + caps.LIMITS.MAX_TEXTURE_UNITS ? 0 : GL.INVALID_ENUM, true);
    state.on("MULTI_TEX_COORD4F", (c, a) => { c.state.fixed.units[a[0] - GL.TEXTURE0].coord = a.slice(1); }, true);
    state.rule("COLOR_MATERIAL", (c, a) => [GL.FRONT, GL.BACK, GL.FRONT_AND_BACK].includes(a[0]) && COLORS.has(a[1]) ? 0 : GL.INVALID_ENUM);
    state.on("COLOR_MATERIAL", (c, a) => { state.set(c, GL.COLOR_MATERIAL_FACE, [a[0]]); state.set(c, GL.COLOR_MATERIAL_PARAMETER, [a[1]]); latch(c); });

    for (const family of ["LIGHT", "MATERIAL", "LIGHT_MODEL"]) for (const suffix of ["F", "I", "FV", "IV"]) {
        const name = family + suffix, vector = suffix.endsWith("V"), integer = suffix.startsWith("I"), model = family === "LIGHT_MODEL";
        const read = a => { const p = a[model ? 0 : 1]; return parameters(a, vector, model ? 1 : 2, COLORS.has(p) || p === GL.LIGHT_MODEL_AMBIENT, integer); };
        state.rule(name, (c, a) => {
            const p = a[model ? 0 : 1], count = model ? MODEL[p] && MODEL[p][0] : (family === "LIGHT" ? LIGHT_COUNTS : MATERIAL_COUNTS)[p];
            if (!count || (!vector && count !== 1)) return GL.INVALID_ENUM;
            if (!model && (family === "LIGHT" ? a[0] < GL.LIGHT0 || a[0] >= GL.LIGHT0 + caps.LIMITS.MAX_LIGHTS :
                    ![GL.FRONT, GL.BACK, GL.FRONT_AND_BACK].includes(a[0]))) return GL.INVALID_ENUM;
            const v = read(a); if (v.length < count) return GL.INVALID_VALUE;
            if ((p === GL.SPOT_EXPONENT || p === GL.SHININESS) && !(v[0] >= 0 && v[0] <= 128) ||
                    p === GL.SPOT_CUTOFF && !(v[0] >= 0 && v[0] <= 90 || v[0] === 180) ||
                    [GL.CONSTANT_ATTENUATION, GL.LINEAR_ATTENUATION, GL.QUADRATIC_ATTENUATION].includes(p) && !(v[0] >= 0)) return GL.INVALID_VALUE;
            return p === GL.LIGHT_MODEL_COLOR_CONTROL && ![GL.SINGLE_COLOR, GL.SEPARATE_SPECULAR_COLOR].includes(v[0]) ? GL.INVALID_ENUM : 0;
        }, family === "MATERIAL");
        state.on(name, (c, a) => {
            const p = a[model ? 0 : 1]; let v = read(a);
            if (model) { state.set(c, p, MODEL[p][1] === "b" ? [+!!v[0]] : v); return; }
            if (family === "LIGHT") {
                if (p === GL.POSITION || p === GL.SPOT_DIRECTION) v = transform(top(c), p === GL.POSITION ? v : [...v.slice(0, 3), 0]).map(Math.fround);
                c.state.fixed.lights[a[0] - GL.LIGHT0][p] = v.slice(0, LIGHT_COUNTS[p]);
            } else c.state.fixed.materials.forEach((m, i) => {
                if (a[0] === (i ? GL.FRONT : GL.BACK)) return;
                for (const q of p === GL.AMBIENT_AND_DIFFUSE ? [GL.AMBIENT, GL.DIFFUSE] : [p]) m[q] = v.slice(0, MATERIAL_COUNTS[p]);
            });
        }, family === "MATERIAL");
    }
    const validLight = target => target >= GL.LIGHT0 && target < GL.LIGHT0 + caps.LIMITS.MAX_LIGHTS;
    state.parameter(C.V86GL_PARAMETER_LIGHT, (shadow, target, pname) => validLight(target) && LIGHT_COUNTS[pname] ?
        [shadow.current.state.fixed.lights[target - GL.LIGHT0][pname], COLORS.has(pname) ? "n" : "f"] : GL.INVALID_ENUM);
    state.parameter(C.V86GL_PARAMETER_MATERIAL, (shadow, target, pname) => [GL.FRONT, GL.BACK].includes(target) &&
        MATERIAL_COUNTS[pname] && pname !== GL.AMBIENT_AND_DIFFUSE ?
        [shadow.current.state.fixed.materials[+(target === GL.BACK)][pname], COLORS.has(pname) ? "n" : "f"] : GL.INVALID_ENUM);
    state.rule("CLIP_PLANE", (c, a) => a[0] >= GL.CLIP_PLANE0 && a[0] < GL.CLIP_PLANE0 + caps.LIMITS.MAX_CLIP_PLANES ? 0 : GL.INVALID_ENUM);
    state.on("CLIP_PLANE", (c, a) => { c.state.fixed.clip[a[0] - GL.CLIP_PLANE0] = plane(c, a.slice(1)); });
    state.parameter(C.V86GL_PARAMETER_CLIP_PLANE, (shadow, target) => target >= GL.CLIP_PLANE0 && target < GL.CLIP_PLANE0 + caps.LIMITS.MAX_CLIP_PLANES ?
        [shadow.current.state.fixed.clip[target - GL.CLIP_PLANE0], "f"] : GL.INVALID_ENUM);

    function envKind(target, p) {
        if (target === GL.POINT_SPRITE) return p === GL.COORD_REPLACE ? "b" : null;
        if (target === GL.TEXTURE_FILTER_CONTROL) return p === GL.TEXTURE_LOD_BIAS ? "t" : null;
        if (target !== GL.TEXTURE_ENV || p === GL.TEXTURE_LOD_BIAS || p === GL.COORD_REPLACE) return null;
        return p === GL.TEXTURE_ENV_COLOR ? "n" : Object.hasOwn(environment(), p) ? "i" : null;
    }
    function envError(target, p, v) {
        if (!envKind(target, p)) return GL.INVALID_ENUM;
        const x = v[0];
        if (p === GL.COORD_REPLACE) return [0, 1].includes(x) ? 0 : GL.INVALID_ENUM;
        if (p === GL.TEXTURE_ENV_COLOR) return v.length >= 4 ? 0 : GL.INVALID_VALUE;
        if (p === GL.RGB_SCALE || p === GL.ALPHA_SCALE) return [1, 2, 4].includes(x) ? 0 : GL.INVALID_VALUE;
        if (p === GL.TEXTURE_ENV_MODE) return [GL.MODULATE, GL.DECAL, GL.BLEND, GL.REPLACE, GL.ADD, GL.COMBINE].includes(x) ? 0 : GL.INVALID_ENUM;
        if (p === GL.COMBINE_RGB || p === GL.COMBINE_ALPHA) return [GL.REPLACE, GL.MODULATE, GL.ADD, GL.ADD_SIGNED, GL.INTERPOLATE, GL.SUBTRACT,
            ...(p === GL.COMBINE_RGB ? [GL.DOT3_RGB, GL.DOT3_RGBA] : [])].includes(x) ? 0 : GL.INVALID_ENUM;
        if (p >= GL.SRC0_RGB && p <= GL.SRC2_RGB || p >= GL.SRC0_ALPHA && p <= GL.SRC2_ALPHA)
            return [GL.TEXTURE, GL.CONSTANT, GL.PRIMARY_COLOR, GL.PREVIOUS].includes(x) || x >= GL.TEXTURE0 && x < GL.TEXTURE0 + caps.LIMITS.MAX_TEXTURE_UNITS ? 0 : GL.INVALID_ENUM;
        if (p >= GL.OPERAND0_RGB && p <= GL.OPERAND2_RGB) return [GL.SRC_COLOR, GL.ONE_MINUS_SRC_COLOR, GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA].includes(x) ? 0 : GL.INVALID_ENUM;
        if (p >= GL.OPERAND0_ALPHA && p <= GL.OPERAND2_ALPHA) return [GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA].includes(x) ? 0 : GL.INVALID_ENUM;
        return 0;
    }
    for (const family of ["TEX_ENV", "TEX_GEN"]) for (const suffix of ["I", "F", "IV", "FV"]) {
        const name = family + suffix, vector = suffix.endsWith("V"), env = family === "TEX_ENV";
        const read = a => parameters(a, vector, 2, env && a[1] === GL.TEXTURE_ENV_COLOR, suffix.startsWith("I"));
        state.rule(name, (c, a) => {
            const v = read(a), p = a[1];
            if (env) return !vector && p === GL.TEXTURE_ENV_COLOR ? GL.INVALID_ENUM : envError(a[0], p, v);
            if (a[0] < GL.S || a[0] > GL.Q || ![GL.TEXTURE_GEN_MODE, GL.OBJECT_PLANE, GL.EYE_PLANE].includes(p) ||
                    !vector && p !== GL.TEXTURE_GEN_MODE) return GL.INVALID_ENUM;
            if (p !== GL.TEXTURE_GEN_MODE) return v.length >= 4 ? 0 : GL.INVALID_VALUE;
            if (![GL.EYE_LINEAR, GL.OBJECT_LINEAR, GL.SPHERE_MAP, GL.NORMAL_MAP, GL.REFLECTION_MAP].includes(v[0]) ||
                    v[0] === GL.SPHERE_MAP && a[0] > GL.T || [GL.NORMAL_MAP, GL.REFLECTION_MAP].includes(v[0]) && a[0] === GL.Q) return GL.INVALID_ENUM;
            return 0;
        });
        state.on(name, (c, a) => {
            const v = read(a), p = a[1];
            if (env) unit(c).env[p] = p === GL.TEXTURE_ENV_COLOR ? v.map(x => clamp(x, 0, 1)) : p === GL.COORD_REPLACE ? [+!!v[0]] : [v[0]];
            else unit(c).gen[a[0] - GL.S][p] = p === GL.EYE_PLANE ? plane(c, v).map(Math.fround) : p === GL.TEXTURE_GEN_MODE ? [v[0]] : v;
        });
    }
    state.parameter(C.V86GL_PARAMETER_TEX_ENV, (shadow, target, pname) => {
        const kind = envKind(target, pname); return kind ? [unit(shadow.current).env[pname], kind] : GL.INVALID_ENUM;
    });
    state.parameter(C.V86GL_PARAMETER_TEX_GEN, (shadow, target, pname) => target >= GL.S && target <= GL.Q &&
        [GL.TEXTURE_GEN_MODE, GL.OBJECT_PLANE, GL.EYE_PLANE].includes(pname) ?
        [unit(shadow.current).gen[target - GL.S][pname], pname === GL.TEXTURE_GEN_MODE ? "i" : "t"] : GL.INVALID_ENUM);
    state.attributes("fixed", {
        capture(c, mask, client) {
            if (client) return null;
            const f = c.state.fixed, saved = {};
            if (mask & 0x40) { saved.lights = f.lights; saved.materials = f.materials; }
            if (mask & 0x1000) saved.clip = f.clip;
            if (mask & 0x40000) saved.units = f.units.map(u => ({ env: u.env, gen: u.gen }));
            if (mask & 1) saved.coords = f.units.map(u => u.coord);
            return clone(saved);
        },
        restore(c, saved) {
            const f = c.state.fixed;
            for (const key of ["lights", "materials", "clip"]) if (saved[key]) f[key] = clone(saved[key]);
            if (saved.units) saved.units.forEach((u, i) => { f.units[i].env = clone(u.env); f.units[i].gen = clone(u.gen); });
            if (saved.coords) saved.coords.forEach((v, i) => { f.units[i].coord = v.slice(); });
        },
    });
    GLShadow.part({
        context(c) { c.state.fixed = initial(); },
        command(shadow, fn, bytes, view, offset) {
            if (fn === P.GLFN.ENABLE && shadow.current && view.getUint32(offset, true) === GL.COLOR_MATERIAL) latch(shadow.current);
        },
        setState(shadow) { for (const c of shadow.contexts.values()) if (!c.state.fixed) c.state.fixed = initial(); },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Queryable fixed-function scalar/vector families: fog, point parameters,
// pixel transfer/zoom, logic operation and the current edge flag (plan C3).
(function(global) {
    "use strict";
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const clamp = v => Math.max(0, Math.min(1, v));
    state.define("FOG_MODE", [GL.EXP], "i", 0x80);
    state.define("FOG_DENSITY FOG_END", [1], "f", 0x80);
    state.define("FOG_START FOG_INDEX", [0], "f", 0x80);
    state.define("FOG_COORD_SRC", [GL.FRAGMENT_DEPTH], "i", 0x80);
    state.define("FOG_COLOR", [0, 0, 0, 0], "n", 0x80);
    const FOG = new Set([GL.FOG_MODE, GL.FOG_DENSITY, GL.FOG_START, GL.FOG_END, GL.FOG_INDEX, GL.FOG_COORD_SRC, GL.FOG_COLOR]);
    for (const name of ["FOGF", "FOGI", "FOGFV"]) {
        state.rule(name, (c, a) => {
            const p = a[0], vector = name === "FOGFV", v = a[vector ? 2 : 1];
            if (!FOG.has(p) || !vector && p === GL.FOG_COLOR) return GL.INVALID_ENUM;
            if (vector && a[1] < (p === GL.FOG_COLOR ? 4 : 1)) return GL.INVALID_VALUE;
            if (p === GL.FOG_MODE && ![GL.LINEAR, GL.EXP, GL.EXP2].includes(v) ||
                    p === GL.FOG_COORD_SRC && ![GL.FOG_COORD, GL.FRAGMENT_DEPTH].includes(v)) return GL.INVALID_ENUM;
            return p === GL.FOG_DENSITY && v < 0 ? GL.INVALID_VALUE : 0;
        });
        state.on(name, (c, a) => state.set(c, a[0], a[0] === GL.FOG_COLOR ? a.slice(2, 6).map(clamp) : [Math.fround(a[name === "FOGFV" ? 2 : 1]) ]));
    }
    state.define("POINT_SIZE_MIN", [0], "f", 2);
    state.define("POINT_SIZE_MAX", [64], "f", 2);
    state.define("POINT_FADE_THRESHOLD_SIZE", [1], "f", 2);
    state.define("POINT_DISTANCE_ATTENUATION", [1, 0, 0], "f", 2);
    state.define("POINT_SPRITE_COORD_ORIGIN", [GL.UPPER_LEFT], "i", 2);
    const POINT = new Set([GL.POINT_SIZE_MIN, GL.POINT_SIZE_MAX, GL.POINT_FADE_THRESHOLD_SIZE,
        GL.POINT_DISTANCE_ATTENUATION, GL.POINT_SPRITE_COORD_ORIGIN]);
    for (const name of ["POINT_PARAMETERF", "POINT_PARAMETERI", "POINT_PARAMETERFV", "POINT_PARAMETERIV"]) {
        state.rule(name, (c, a) => {
            const p = a[0];
            if (!POINT.has(p) || p === GL.POINT_DISTANCE_ATTENUATION && !name.endsWith("V")) return GL.INVALID_ENUM;
            if (p === GL.POINT_SPRITE_COORD_ORIGIN) return [GL.LOWER_LEFT, GL.UPPER_LEFT].includes(a[1]) ? 0 : GL.INVALID_ENUM;
            return p !== GL.POINT_DISTANCE_ATTENUATION && a[1] < 0 ? GL.INVALID_VALUE : 0;
        });
        state.on(name, (c, a) => state.set(c, a[0], a.slice(1, a[0] === GL.POINT_DISTANCE_ATTENUATION ? 4 : 2).map(Math.fround)));
    }
    const TRANSFER = new Map();
    function transfer(names, initial, kind) {
        state.define(names, [initial], kind, 0x20);
        for (const name of names.split(" ")) TRANSFER.set(GL[name], kind);
    }
    transfer("RED_SCALE GREEN_SCALE BLUE_SCALE ALPHA_SCALE DEPTH_SCALE POST_COLOR_MATRIX_RED_SCALE_SGI " +
        "POST_COLOR_MATRIX_GREEN_SCALE_SGI POST_COLOR_MATRIX_BLUE_SCALE_SGI POST_COLOR_MATRIX_ALPHA_SCALE_SGI", 1, "f");
    transfer("RED_BIAS GREEN_BIAS BLUE_BIAS ALPHA_BIAS DEPTH_BIAS POST_COLOR_MATRIX_RED_BIAS_SGI " +
        "POST_COLOR_MATRIX_GREEN_BIAS_SGI POST_COLOR_MATRIX_BLUE_BIAS_SGI POST_COLOR_MATRIX_ALPHA_BIAS_SGI", 0, "f");
    transfer("MAP_COLOR MAP_STENCIL", 0, "b");
    transfer("INDEX_SHIFT INDEX_OFFSET", 0, "i");
    for (const name of ["PIXEL_TRANSFERF", "PIXEL_TRANSFERI"]) {
        state.rule(name, (c, a) => TRANSFER.has(a[0]) ? 0 : GL.INVALID_ENUM);
        state.on(name, (c, [p, v]) => state.set(c, p, [TRANSFER.get(p) === "b" ? +!!v : TRANSFER.get(p) === "i" ? Math.round(v) : Math.fround(v)]));
    }
    state.define("ZOOM_X ZOOM_Y", [1], "f", 0x20);
    state.rule("PIXEL_ZOOM");
    state.on("PIXEL_ZOOM", (c, a) => { state.set(c, GL.ZOOM_X, [a[0]]); state.set(c, GL.ZOOM_Y, [a[1]]); });
    state.define("LOGIC_OP_MODE", [GL.COPY], "i", 0x4000);
    state.rule("LOGIC_OP", (c, a) => a[0] >= GL.CLEAR_LOGIC_OP && a[0] <= GL.SET ? 0 : GL.INVALID_ENUM);
    state.on("LOGIC_OP", (c, a) => state.set(c, GL.LOGIC_OP_MODE, a));
    state.define("CURRENT_EDGE_FLAG", [1], "b", 1);
    state.rule("EDGE_FLAG", null, true);
    state.on("EDGE_FLAG", (c, a) => state.set(c, GL.CURRENT_EDGE_FLAG, [+!!a[0]]), true);
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Current raster state, including fixed-function lighting/texgen at set
// time. WindowPos follows ARB_window_pos, whose bypass rules differ from
// RasterPos (the gl2.1 WindowPos refpage incorrectly describes lighting).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const math = global.GLMath || require("./gl_math.js");
    if (typeof module !== "undefined" && module.exports) {
        require("./gl_shadow_objects.js"); require("./gl_shadow_raster.js"); require("./gl_shadow_fixed.js"); require("./gl_shadow_misc.js");
    }
    const clone = GLShadow.clone;
    const dot = (a, b) => a.reduce((sum, v, i) => sum + v * b[i], 0);
    const unit = a => { const n = Math.hypot(...a); return n ? a.map(v => v / n) : a.map(() => 0); };
    const clamp = v => Math.max(0, Math.min(1, v));
    const float = values => values.map(Math.fround);
    const transform = (m, p) => Array.from(math.transformPoint4(m, p, new Float64Array(4)));
    const top = (c, mode) => c.state.stacks[mode].at(-1);
    const enabled = (c, cap) => !!c.state.raster.enabled[cap];
    const value = (c, p) => c.state.values[p];
    state.define("CURRENT_RASTER_POSITION", [0, 0, 0, 1], "f", 1);
    state.define("CURRENT_RASTER_POSITION_VALID", [1], "b", 1);
    state.define("CURRENT_RASTER_DISTANCE", [0], "f", 1);
    state.define("CURRENT_RASTER_COLOR", [1, 1, 1, 1], "n", 1);
    state.define("CURRENT_RASTER_SECONDARY_COLOR", [0, 0, 0, 1], "n", 1);
    state.define("CURRENT_RASTER_INDEX", [1], "f", 1); // the drawable is RGBA
    state.query([GL.CURRENT_RASTER_TEXTURE_COORDS], s => [s.current.state.rasterCoords[s.current.activeTexture || 0], "f"]);
    function normal(c) {
        const m = math.normalMatrixOf(top(c, GL.MODELVIEW), new Float64Array(9)), n = value(c, GL.CURRENT_NORMAL);
        let out = [0, 1, 2].map(i => m[i] * n[0] + m[3 + i] * n[1] + m[6 + i] * n[2]);
        if (enabled(c, GL.NORMALIZE)) out = unit(out);
        else if (enabled(c, GL.RESCALE_NORMAL)) {
            const len = Math.hypot(m[6], m[7], m[8]); if (len) out = out.map(v => v / len);
        }
        return out;
    }
    function lighting(c, eye, n, side = 0) {
        const m = c.state.fixed.materials[side], model = value(c, GL.LIGHT_MODEL_AMBIENT);
        const color = [0, 1, 2].map(i => m[GL.EMISSION][i] + m[GL.AMBIENT][i] * model[i]);
        const specular = [0, 0, 0];
        const position = eye.slice(0, 3).map(v => eye[3] ? v / eye[3] : v);
        const viewer = value(c, GL.LIGHT_MODEL_LOCAL_VIEWER)[0] ? unit(position.map(v => -v)) : [0, 0, 1];
        c.state.fixed.lights.forEach((light, index) => {
            if (!enabled(c, GL.LIGHT0 + index)) return;
            const p = light[GL.POSITION], delta = p.slice(0, 3).map((v, i) => p[3] ? v / p[3] - position[i] : v);
            const distance = Math.hypot(...delta), l = unit(delta);
            let attenuation = p[3] ? 1 / (light[GL.CONSTANT_ATTENUATION][0] + light[GL.LINEAR_ATTENUATION][0] * distance +
                light[GL.QUADRATIC_ATTENUATION][0] * distance * distance) : 1;
            if (light[GL.SPOT_CUTOFF][0] !== 180) {
                const cosine = dot(l.map(v => -v), unit(light[GL.SPOT_DIRECTION]));
                attenuation *= cosine < Math.cos(light[GL.SPOT_CUTOFF][0] * Math.PI / 180) ? 0 : Math.pow(Math.max(0, cosine), light[GL.SPOT_EXPONENT][0]);
            }
            const diffuse = Math.max(0, dot(n, l)), half = unit(l.map((v, i) => v + viewer[i]));
            const spec = diffuse > 0 ? Math.pow(Math.max(0, dot(n, half)), m[GL.SHININESS][0]) : 0;
            for (let i = 0; i < 3; ++i) {
                color[i] += attenuation * (m[GL.AMBIENT][i] * light[GL.AMBIENT][i] + diffuse * m[GL.DIFFUSE][i] * light[GL.DIFFUSE][i]);
                specular[i] += attenuation * spec * m[GL.SPECULAR][i] * light[GL.SPECULAR][i];
            }
        });
        const separate = value(c, GL.LIGHT_MODEL_COLOR_CONTROL)[0] === GL.SEPARATE_SPECULAR_COLOR;
        return [float([...color.map((v, i) => clamp(v + (separate ? 0 : specular[i]))), clamp(m[GL.DIFFUSE][3])]),
            float([...(separate ? specular.map(clamp) : [0, 0, 0]), 1])];
    }
    function coordinates(c, object, eye, n) {
        const eyeUnit = unit(eye.slice(0, 3)), normalUnit = unit(n);
        const reflect = eyeUnit.map((v, i) => v - 2 * normalUnit[i] * dot(normalUnit, eyeUnit));
        const divisor = 2 * Math.hypot(reflect[0], reflect[1], reflect[2] + 1);
        return c.state.fixed.units.map((u, index) => {
            const coord = u.coord.slice();
            for (let i = 0; i < 4; ++i) {
                if (!c.state.raster.units[index][GL.TEXTURE_GEN_S + i]) continue;
                const gen = u.gen[i], mode = gen[GL.TEXTURE_GEN_MODE][0];
                if (mode === GL.OBJECT_LINEAR) coord[i] = dot(object, gen[GL.OBJECT_PLANE]);
                else if (mode === GL.EYE_LINEAR) coord[i] = dot(eye, gen[GL.EYE_PLANE]);
                else if (mode === GL.NORMAL_MAP) coord[i] = normalUnit[i];
                else if (mode === GL.REFLECTION_MAP) coord[i] = reflect[i];
                else if (mode === GL.SPHERE_MAP) coord[i] = (divisor ? reflect[i] / divisor : 0) + 0.5;
            }
            return float(transform(c.state.texture[index].at(-1), coord));
        });
    }
    function set(c, p, values) { state.set(c, p, values); }
    state.rule("RASTER_POS4F", (c, a, shadow) => c.state.selection && c.state.selection.mode !== GL.RENDER ||
        shadow.framebufferStatus(GL.DRAW_FRAMEBUFFER) === GL.FRAMEBUFFER_COMPLETE ? 0 : GL.INVALID_FRAMEBUFFER_OPERATION);
    state.on("RASTER_POS4F", (c, object) => {
        const eye = transform(top(c, GL.MODELVIEW), object), clip = transform(top(c, GL.PROJECTION), eye), w = clip[3];
        let valid = w > 0 && Math.abs(clip[0]) <= w && Math.abs(clip[1]) <= w &&
            (enabled(c, GL.DEPTH_CLAMP) || Math.abs(clip[2]) <= w);
        for (let i = 0; valid && i < c.state.fixed.clip.length; ++i)
            if (enabled(c, GL.CLIP_PLANE0 + i) && dot(c.state.fixed.clip[i], eye) < 0) valid = false;
        set(c, GL.CURRENT_RASTER_POSITION_VALID, [+valid]);
        if (!valid) return; // associated values are undefined; keep stable bits
        const viewport = value(c, GL.VIEWPORT), depth = value(c, GL.DEPTH_RANGE);
        set(c, GL.CURRENT_RASTER_POSITION, float([viewport[0] + (clip[0] / w + 1) * viewport[2] / 2,
            viewport[1] + (clip[1] / w + 1) * viewport[3] / 2, depth[0] + (clip[2] / w + 1) * (depth[1] - depth[0]) / 2, w]));
        const n = normal(c), colors = enabled(c, GL.LIGHTING) ? lighting(c, eye, n) :
            [value(c, GL.CURRENT_COLOR).map(clamp), value(c, GL.CURRENT_SECONDARY_COLOR_EXT).map(clamp)];
        set(c, GL.CURRENT_RASTER_COLOR, colors[0]); set(c, GL.CURRENT_RASTER_SECONDARY_COLOR, colors[1]);
        // RasterPos keeps eye distance and the explicit raster fog coordinate
        // separately. WindowPos has the ARB extension's combined-distance rule.
        set(c, GL.CURRENT_RASTER_DISTANCE, [Math.fround(Math.abs(eye[2]))]);
        c.state.rasterFogCoord = value(c, GL.CURRENT_FOG_COORDINATE_EXT)[0];
        c.state.rasterCoords = coordinates(c, object, eye, n);
    });
    state.rule("WINDOW_POS3F");
    state.on("WINDOW_POS3F", (c, a) => {
        const depth = value(c, GL.DEPTH_RANGE);
        set(c, GL.CURRENT_RASTER_POSITION, float([a[0], a[1], depth[0] + clamp(a[2]) * (depth[1] - depth[0]), 1]));
        set(c, GL.CURRENT_RASTER_POSITION_VALID, [1]);
        set(c, GL.CURRENT_RASTER_COLOR, value(c, GL.CURRENT_COLOR).map(clamp));
        set(c, GL.CURRENT_RASTER_SECONDARY_COLOR, value(c, GL.CURRENT_SECONDARY_COLOR_EXT).map(clamp));
        set(c, GL.CURRENT_RASTER_DISTANCE, value(c, GL.FOG_COORD_SRC)[0] === GL.FOG_COORD ? value(c, GL.CURRENT_FOG_COORDINATE_EXT).slice() : [0]);
        c.state.rasterFogCoord = value(c, GL.CURRENT_RASTER_DISTANCE)[0];
        c.state.rasterCoords = c.state.fixed.units.map(u => u.coord.slice());
    });
    GLShadow.rules({ [P.GLFN.BITMAP]: (s, bytes, view, offset, size) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        if ((!s.current.state.selection || s.current.state.selection.mode === GL.RENDER) &&
            s.framebufferStatus(GL.DRAW_FRAMEBUFFER) !== GL.FRAMEBUFFER_COMPLETE) return GL.INVALID_FRAMEBUFFER_OPERATION;
        const r = P.decodeRecord("BITMAP", view, offset);
        return r.width < 0 || r.height < 0 || P.RECORDS.BITMAP.size + r.data_size > size ? GL.INVALID_VALUE : 0;
    } });
    const coords = () => Array.from({ length: 8 }, () => [0, 0, 0, 1]);
    state.attributes("rasterPosition", {
        capture(c, mask, client) { return !client && mask & 1 ? { coords: clone(c.state.rasterCoords), fog: c.state.rasterFogCoord } : null; },
        restore(c, saved) { c.state.rasterCoords = clone(saved.coords); c.state.rasterFogCoord = saved.fog; },
    });
    GLShadow.part({
        context(c) { c.state.rasterCoords = coords(); c.state.rasterFogCoord = 0; },
        command(s, fn, bytes, view, offset) {
            if (fn !== P.GLFN.BITMAP || !s.current || !value(s.current, GL.CURRENT_RASTER_POSITION_VALID)[0]) return;
            const r = P.decodeRecord("BITMAP", view, offset), p = value(s.current, GL.CURRENT_RASTER_POSITION).slice();
            p[0] = Math.fround(p[0] + r.xmove); p[1] = Math.fround(p[1] + r.ymove);
            set(s.current, GL.CURRENT_RASTER_POSITION, p);
        },
        setState(s) { for (const c of s.contexts.values()) {
            if (!c.state.rasterCoords) c.state.rasterCoords = coords();
            if (c.state.rasterFogCoord === undefined) c.state.rasterFogCoord = 0;
        } },
    });
    // Selection/feedback uses the same fixed vertex operations as RasterPos.
    const api = { transform, normal, lighting, coordinates };
    global.GLFixedVertex = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const maps = global.GLPixelMaps || require("./gl_pixel_maps.js");
    const initial = () => Array.from({ length: 10 }, () => [0]);
    const indexOf = map => map - GL.PIXEL_MAP_I_TO_I;
    for (let i = 0; i < 10; ++i)
        state.query([GL.PIXEL_MAP_I_TO_I_SIZE + i], s => [[s.current.state.pixelMaps[i].length], "i"]);
    for (const name of ["PIXEL_MAPFV", "PIXEL_MAPUIV", "PIXEL_MAPUSV"]) {
        const stride = name === "PIXEL_MAPUSV" ? 2 : 4;
        GLShadow.rules({ [P.GLFN[name]]: (s, bytes, view, offset, size) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const r = P.decodeRecord(name, view, offset), index = indexOf(r.map);
            if (index < 0 || index >= 10) return GL.INVALID_ENUM;
            return r.count < 1 || r.count > caps.LIMITS.MAX_PIXEL_MAP_TABLE || index < 6 && (r.count & r.count - 1) ||
                P.RECORDS[name].size + r.count * stride > size ? GL.INVALID_VALUE : 0;
        } });
    }
    state.parameter(P.C.V86GL_PARAMETER_PIXEL_MAP, (s, target) => {
        const index = indexOf(target);
        return index >= 0 && index < 10 ? [s.current.state.pixelMaps[index], index < 2 ? "u" : "n"] : GL.INVALID_ENUM;
    });
    state.attributes("pixelMaps", {
        capture(c, mask, client) { return !client && mask & 0x20 ? c.state.pixelMaps.map(v => v.slice()) : null; },
        restore(c, saved) { c.state.pixelMaps = saved.map(v => v.slice()); },
    });
    GLShadow.part({
        context(c) { c.state.pixelMaps = initial(); },
        command(s, fn, bytes, view, offset) {
            if (![P.GLFN.PIXEL_MAPFV, P.GLFN.PIXEL_MAPUIV, P.GLFN.PIXEL_MAPUSV].includes(fn)) return;
            const r = maps.decode(P.NAME_BY_OPCODE[fn], view, offset);
            s.current.state.pixelMaps[indexOf(r.map)] = r.values;
        },
        setState(s) { for (const c of s.contexts.values()) if (!c.state.pixelMaps) c.state.pixelMaps = initial(); },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const initial = () => Array(128).fill(255);
    function layout(c, pack) {
        const value = name => c.state.values[GL[(pack ? "PACK_" : "UNPACK_") + name]]?.[0] || 0;
        const alignment = value("ALIGNMENT") || 4, pixels = value("SKIP_PIXELS");
        const row_stride = Math.ceil(Math.ceil((value("ROW_LENGTH") || 32) / 8) / alignment) * alignment;
        const start = value("SKIP_ROWS") * row_stride + Math.floor(pixels / 8), bit_offset = pixels % 8;
        const row_bytes = Math.ceil((bit_offset + 32) / 8);
        return { row_stride, start, row_bytes, bit_offset, lsb_first: value("LSB_FIRST"), span: start + 31 * row_stride + row_bytes };
    }
    const bitMask = (bit, lsb) => lsb ? 1 << (bit % 8) : 0x80 >> (bit % 8);
    function canonical(source, l, pointer = 0, compact = false) {
        const out = Array(128).fill(0);
        for (let row = 0; row < 32; ++row) for (let x = 0; x < 32; ++x) {
            const bit = l.bit_offset + x, at = pointer + (compact ? row * l.row_bytes : l.start + row * l.row_stride) + Math.floor(bit / 8);
            if (source[at] & bitMask(bit, l.lsb_first)) out[row * 4 + Math.floor(x / 8)] |= 0x80 >> (x % 8);
        }
        return out;
    }
    function answer(s, name, bytes, view, offset, size) {
        if (!s.pending(name, view, offset, size)) return;
        const fields = P.F[name], header = P.RECORDS[name].size, r = P.decodeRecord(name, view, offset), c = s.current;
        const fail = error => {
            if (name === "QUERY_POLYGON_STIPPLE" && r.mode === 2 && s.captureListFailure?.(P.GLFN.POLYGON_STIPPLE, error)) {
                view.setUint32(offset + fields.pbo, P.C.V86GL_STIPPLE_CAPTURE_FAILURE_BIT, true);
                view.setUint32(offset + fields.error, 0, true);
                return s.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
            }
            if (c && !c.error) c.error = error;
            view.setUint32(offset + fields.error, error, true);
            s.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
        };
        if (!c || c.state.begin) return fail(GL.INVALID_OPERATION);
        const query = name === "QUERY_POLYGON_STIPPLE", l = layout(c, query && r.mode === 0);
        if (!Number.isSafeInteger(l.span) || l.span > 0xFFFFFFFF) return fail(GL.OUT_OF_MEMORY);
        const buffer = s.boundBuffer?.(query && r.mode === 0 ? GL.PIXEL_PACK_BUFFER : GL.PIXEL_UNPACK_BUFFER);
        if (query) {
            if (r.mode > 2 || r.data_size !== 320 || size < header + 320) return fail(GL.INVALID_VALUE);
            if (buffer && (buffer.mapped || r.pointer + l.span > buffer.size)) return fail(GL.INVALID_OPERATION);
            for (const [key, value] of Object.entries(l)) view.setUint32(offset + fields[key], value, true);
            view.setUint32(offset + fields.pbo, buffer ? P.C.V86GL_STIPPLE_SOURCE_PBO_BIT : 0, true);
            const output = bytes.subarray(offset + header, offset + header + 320); output.fill(0);
            if (r.mode === 0) {
                const count = 32 * l.row_bytes;
                for (let row = 0; row < 32; ++row) for (let x = 0; x < 32; ++x) {
                    const bit = l.bit_offset + x, at = row * l.row_bytes + Math.floor(bit / 8), mask = bitMask(bit, l.lsb_first);
                    output[count + at] |= mask;
                    if (c.state.polygonStipple[row * 4 + Math.floor(x / 8)] & 0x80 >> (x % 8)) output[at] |= mask;
                }
            } else if (buffer) {
                const pattern = canonical(buffer.data, l, r.pointer);
                if (r.mode === 1) c.state.polygonStipple = pattern;
                output.set(pattern);
            }
        } else {
            if (buffer) return fail(GL.INVALID_OPERATION);
            if (r.data_size !== l.row_bytes * 32 || size < header + Math.max(128, r.data_size)) return fail(GL.INVALID_VALUE);
            c.state.polygonStipple = canonical(bytes.subarray(offset + header, offset + header + r.data_size), l, 0, true);
            bytes.set(c.state.polygonStipple, offset + header);
        }
        view.setUint32(offset + fields.error, 0, true);
        s.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
    }
    GLShadow.prototype.queryPolygonStipple = function(...args) { answer(this, "QUERY_POLYGON_STIPPLE", ...args); };
    GLShadow.prototype.polygonStippleBitmap = function(...args) { answer(this, "POLYGON_STIPPLE_BITMAP", ...args); };
    GLShadow.install({ [P.GLFN.QUERY_POLYGON_STIPPLE]: "queryPolygonStipple", [P.GLFN.POLYGON_STIPPLE_BITMAP]: "polygonStippleBitmap" });
    GLShadow.rules({ [P.GLFN.POLYGON_STIPPLE]: s => s.current.state.begin ? GL.INVALID_OPERATION : 0 });
    state.attributes("polygonStipple", {
        capture(c, mask, client) { return !client && mask & 0x10 ? c.state.polygonStipple.slice() : null; },
        restore(c, saved) { c.state.polygonStipple = saved.slice(); },
    });
    GLShadow.part({
        context(c) { c.state.polygonStipple = initial(); },
        command(s, fn, bytes, view, offset) {
            if (fn === P.GLFN.POLYGON_STIPPLE) s.current.state.polygonStipple = Array.from(bytes.subarray(offset, offset + 128));
        },
        setState(s) { for (const c of s.contexts.values()) if (!c.state.polygonStipple) c.state.polygonStipple = initial(); },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Public GL names are bindings; retained image identities survive name reuse.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const buffers = global.GLFramebufferState || require("./gl_framebuffer_state.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const formats = global.GLImageFormats || require("./gl_image_formats.js");
    const targets = new Set([GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_3D, GL.TEXTURE_CUBE_MAP, GL.TEXTURE_RECTANGLE]);
    GLShadow.prototype.framebufferStateValue = function(pname) {
        const fbo = this.boundFramebuffer(GL.DRAW_FRAMEBUFFER); if (!fbo) return null;
        // The exposed renderbuffer/texture storage is single-sampled. Window
        // multisampling is selected separately through its pixel format.
        if (pname === GL.SAMPLES || pname === GL.SAMPLE_BUFFERS) return [[0], "i"];
        const index = pname - GL.RED_BITS;
        if (index < 0 || index > 5) return null;
        // These describe the attached colour buffers, even with DrawBuffer
        // NONE; EXT FBO requires matching formats across all colour images.
        const record = index < 4 ? fbo.color.find(Boolean) : index === 4 ? fbo.depth : fbo.stencil;
        const image = record && this.attachedImage(record);
        if (!image) return [[0], "i"];
        const bits = record.kind === "renderbuffer" ? image.componentBits :
            formats.textureComponents(image.internalFormat, !!image.compressed, this.capabilities?.features);
        return [[bits?.[record.kind === "renderbuffer" || index < 4 ? index : index + 2] || 0], "i"];
    };
    GLShadow.prototype.framebufferInteger = function(pname) {
        const value = this.stateValue(pname); return value ? value[0][0] : undefined;
    };
    GLShadow.queries({ integers: Object.fromEntries([GL.RED_BITS, GL.GREEN_BITS, GL.BLUE_BITS, GL.ALPHA_BITS,
        GL.DEPTH_BITS, GL.STENCIL_BITS, GL.SAMPLE_BUFFERS, GL.SAMPLES].map(pname => [pname, "framebufferInteger"])) });
    for (const [pname, target] of [[GL.TEXTURE_BINDING_1D, GL.TEXTURE_1D], [GL.TEXTURE_BINDING_2D, GL.TEXTURE_2D],
            [GL.TEXTURE_BINDING_3D, GL.TEXTURE_3D], [GL.TEXTURE_BINDING_CUBE_MAP, GL.TEXTURE_CUBE_MAP], [GL.TEXTURE_BINDING_RECTANGLE, GL.TEXTURE_RECTANGLE]])
        state.query([pname], s => [[s.current.units[s.current.activeTexture].get(target) || 0], "u"]);
    for (const [pname, field] of [[GL.RENDERBUFFER_BINDING, "renderbuffer"], [GL.FRAMEBUFFER_BINDING, "drawFramebuffer"],
            [GL.READ_FRAMEBUFFER_BINDING_EXT, "readFramebuffer"]])
        state.query([pname], s => [[s.current[field]], "u"]);
    state.rule("BIND_TEXTURE", (c, [target, name]) => {
        if (!targets.has(target)) return GL.INVALID_ENUM;
        const t = c.group.textures.get(name);
        return t?.target && t.target !== target ? GL.INVALID_OPERATION : 0;
    });
    state.rule("BIND_RENDERBUFFER", (c, [target]) => target === GL.RENDERBUFFER_EXT ? 0 : GL.INVALID_ENUM);
    state.rule("RENDERBUFFER_STORAGE", (c, [target, format, width, height], s) => {
        if (target !== GL.RENDERBUFFER_EXT || !formats.renderbufferFormatFor(format, s.capabilities?.features)) return GL.INVALID_ENUM;
        if (!c.renderbufferImage) return GL.INVALID_OPERATION;
        const max = s.capabilities ? caps.integer(s.capabilities, GL.MAX_RENDERBUFFER_SIZE) : 16384;
        return width < 0 || height < 0 || width > max || height > max ? GL.INVALID_VALUE : 0;
    });
    for (const name of ["BIND_FRAMEBUFFER", "BIND_FRAMEBUFFER_EXT"]) state.rule(name, (c, [target]) =>
        [GL.FRAMEBUFFER_EXT, GL.READ_FRAMEBUFFER, GL.DRAW_FRAMEBUFFER].includes(target) ? 0 : GL.INVALID_ENUM);
    const limit = (s, pname) => s.capabilities ? caps.integer(s.capabilities, pname) : caps.LIMITS.MAX_DRAW_BUFFERS;
    function attachmentError(c, target, attachment, s) {
        if (![GL.FRAMEBUFFER_EXT, GL.READ_FRAMEBUFFER, GL.DRAW_FRAMEBUFFER].includes(target)) return GL.INVALID_ENUM;
        if (!(target === GL.READ_FRAMEBUFFER ? c.readFramebuffer : c.drawFramebuffer)) return GL.INVALID_OPERATION;
        return [GL.DEPTH_ATTACHMENT, GL.STENCIL_ATTACHMENT, GL.DEPTH_STENCIL_ATTACHMENT].includes(attachment) ||
            attachment >= GL.COLOR_ATTACHMENT0 && attachment < GL.COLOR_ATTACHMENT0 + limit(s, GL.MAX_COLOR_ATTACHMENTS) ? 0 : GL.INVALID_ENUM;
    }
    for (const [name, dimension] of [["FRAMEBUFFER_TEXTURE", 0], ["FRAMEBUFFER_TEXTURE_1D", 1],
        ["FRAMEBUFFER_TEXTURE_2D", 2], ["FRAMEBUFFER_TEXTURE_3D", 3]])
        state.rule(name, (c, [target, attachment, textarget, texture, level, slice], s) => {
            const error = attachmentError(c, target, attachment, s); if (error || !texture) return error;
            const cube = textarget >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && textarget <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z;
            const dimensionality = textarget === GL.TEXTURE_1D ? 1 : textarget === GL.TEXTURE_3D ? 3 : textarget === GL.TEXTURE_2D || textarget === GL.TEXTURE_RECTANGLE || cube ? 2 : 0;
            if (!dimensionality || dimension && dimensionality !== dimension) return GL.INVALID_ENUM;
            const object = c.group.textures.get(texture);
            if (!object?.target || object.target !== (cube ? GL.TEXTURE_CUBE_MAP : textarget)) return GL.INVALID_OPERATION;
            const max = s.capabilities ? caps.integer(s.capabilities, dimensionality === 3 ? GL.MAX_3D_TEXTURE_SIZE :
                cube ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : dimensionality === 3 ? 2048 : 16384;
            if (level < 0 || level > Math.floor(Math.log2(max))) return GL.INVALID_VALUE;
            // EXT_framebuffer_object only rejects slices above the maximum.
            // An absent (including negative) slice makes the FBO incomplete.
            return dimensionality === 3 && slice >= max ? GL.INVALID_VALUE : 0;
        });
    state.rule("FRAMEBUFFER_RENDERBUFFER", (c, [target, attachment, rbTarget, name], s) => {
        const error = attachmentError(c, target, attachment, s); if (error || !name) return error;
        if (rbTarget !== GL.RENDERBUFFER_EXT) return GL.INVALID_ENUM;
        return c.group.renderbuffers.get(name)?.created ? 0 : GL.INVALID_OPERATION;
    });
    state.query([0x0C01], s => [[s.current.drawBuffers[0] || GL.NONE], "e"]);
    state.query([0x0C02], s => [[s.current.readBuffer], "e"]);
    for (let i = 0; i < 16; ++i) state.query([GL.DRAW_BUFFER0 + i], s =>
        i < limit(s, GL.MAX_DRAW_BUFFERS) ? [[s.current.drawBuffers[i] || GL.NONE], "e"] : GL.INVALID_ENUM);
    function selection(c, value, read, many, s) {
        if (value === GL.NONE) return 0;
        const window = value >= 0x400 && value <= 0x40C && (!read || value !== 0x408);
        const color = value >= GL.COLOR_ATTACHMENT0 && value < GL.COLOR_ATTACHMENT0 + 32;
        if (!window && !color) return GL.INVALID_ENUM;
        if (many && value >= GL.FRONT && value <= GL.FRONT_AND_BACK) return GL.INVALID_OPERATION;
        if (read ? c.readFramebuffer : c.drawFramebuffer)
            return color && value < GL.COLOR_ATTACHMENT0 + limit(s, GL.MAX_COLOR_ATTACHMENTS) ? 0 : GL.INVALID_OPERATION;
        if (color || [0x401, 0x403, 0x407].includes(value) || value >= 0x409) return GL.INVALID_OPERATION;
        if (c.state.format & P.C.V86GL_FORMAT_SINGLE_BUFFER && [GL.BACK, 0x402].includes(value)) return GL.INVALID_OPERATION;
        return 0;
    }
    state.rule("DRAW_BUFFER", (c, [value], s) => selection(c, value, false, false, s));
    state.rule("READ_BUFFER", (c, [value], s) => selection(c, value, true, false, s));
    GLShadow.rules({ [P.GLFN.DRAW_BUFFERS]: (s, bytes, v, o, size) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const count = v.getInt32(o, true);
        if (count < 0 || count > limit(s, GL.MAX_DRAW_BUFFERS) || 4 + count * 4 > size) return GL.INVALID_VALUE;
        const seen = new Set();
        for (let i = 0; i < count; ++i) {
            const value = v.getUint32(o + 4 + i * 4, true), error = selection(s.current, value, false, true, s);
            if (error) return error;
            if (value !== GL.NONE && seen.has(value)) return GL.INVALID_OPERATION;
            seen.add(value);
        }
        return 0;
    } });
    for (const name of ["GEN_TEXTURES", "DELETE_TEXTURES", "GEN_RENDERBUFFERS", "DELETE_RENDERBUFFERS", "GEN_FRAMEBUFFERS", "DELETE_FRAMEBUFFERS"])
        GLShadow.rules({ [P.GLFN[name]]: (s, bytes, v, o, size) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const count = v.getInt32(o, true);
            return count < 0 || P.RECORDS[name].size + count * 4 > size ? GL.INVALID_VALUE : 0;
        } });
    state.parameter(P.C.V86GL_PARAMETER_IS_OBJECT, (s, kind, name) => {
        const c = s.current, g = c.group;
        let result;
        switch (kind) {
        case P.C.V86GL_OBJECT_KIND_SHADER: result = g.shaders?.has(name); break;
        case P.C.V86GL_OBJECT_KIND_PROGRAM: result = g.programs?.has(name); break;
        case P.C.V86GL_OBJECT_KIND_TEXTURE: result = !!g.textures.get(name)?.target; break;
        case P.C.V86GL_OBJECT_KIND_RENDERBUFFER: result = g.renderbuffers.get(name)?.created; break;
        case P.C.V86GL_OBJECT_KIND_FRAMEBUFFER: result = s.findFramebuffer(name)?.created; break;
        case P.C.V86GL_OBJECT_KIND_BUFFER: result = g.buffers?.get(name)?.created; break;
        case P.C.V86GL_OBJECT_KIND_QUERY: result = c.queryObjects?.get(name)?.created; break;
        case P.C.V86GL_OBJECT_KIND_ARB_PROGRAM: result = g.arbPrograms?.get(name)?.target; break;
        default: return GL.INVALID_ENUM;
        }
        return [[+(!!name && !!result)], "b"];
    });
    state.parameter(P.C.V86GL_PARAMETER_FRAMEBUFFER_ATTACHMENT, (s, target, pname, attachment) => {
        if (![GL.FRAMEBUFFER_EXT, GL.READ_FRAMEBUFFER, GL.DRAW_FRAMEBUFFER].includes(target)) return GL.INVALID_ENUM;
        const fbo = s.boundFramebuffer(target); if (!fbo) return GL.INVALID_OPERATION;
        let record;
        if (attachment === GL.DEPTH_ATTACHMENT) record = fbo.depth;
        else if (attachment === GL.STENCIL_ATTACHMENT) record = fbo.stencil;
        else if (attachment >= GL.COLOR_ATTACHMENT0 && attachment < GL.COLOR_ATTACHMENT0 + limit(s, GL.MAX_COLOR_ATTACHMENTS))
            record = fbo.color[attachment - GL.COLOR_ATTACHMENT0];
        else return GL.INVALID_ENUM;
        if (pname === GL.FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT)
            return [[record ? record.kind === "texture" ? GL.TEXTURE : GL.RENDERBUFFER_EXT : GL.NONE], "e"];
        if (pname === GL.FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT) return [[record?.name || 0], "u"];
        if (!record || record.kind !== "texture") return GL.INVALID_ENUM;
        const texture = s.current.group.textureImages.get(record.id);
        switch (pname) {
        case GL.FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT: return [[record.level], "i"];
        case GL.FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT:
            return [[texture?.target === GL.TEXTURE_CUBE_MAP ? GL.TEXTURE_CUBE_MAP_POSITIVE_X + record.layer : 0], "e"];
        case GL.FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT:
            return [[texture?.target === GL.TEXTURE_3D ? record.layer : 0], "i"];
        default: return GL.INVALID_ENUM;
        }
    });
    state.parameter(P.C.V86GL_PARAMETER_RENDERBUFFER, (s, target, pname) => {
        if (target !== GL.RENDERBUFFER_EXT) return GL.INVALID_ENUM;
        const rb = s.current.group.renderbufferImages.get(s.current.renderbufferImage);
        if (!rb) return GL.INVALID_OPERATION;
        if (pname === GL.RENDERBUFFER_WIDTH_EXT) return [[rb.width], "i"];
        if (pname === GL.RENDERBUFFER_HEIGHT_EXT) return [[rb.height], "i"];
        if (pname === GL.RENDERBUFFER_INTERNAL_FORMAT_EXT) return [[rb.internalFormat], "e"];
        if (pname === 0x8CAB) return [[0], "i"]; // RENDERBUFFER_SAMPLES
        const index = [GL.RENDERBUFFER_RED_SIZE_EXT, GL.RENDERBUFFER_GREEN_SIZE_EXT, GL.RENDERBUFFER_BLUE_SIZE_EXT,
            GL.RENDERBUFFER_ALPHA_SIZE_EXT, GL.RENDERBUFFER_DEPTH_SIZE_EXT, GL.RENDERBUFFER_STENCIL_SIZE_EXT].indexOf(pname);
        if (index < 0) return GL.INVALID_ENUM;
        const bits = rb.componentBits || (rb.width && rb.height ? formats.renderbufferFormatFor(rb.internalFormat)?.components : null);
        return [[bits?.[index] || 0], "i"];
    });
    state.attributes("images", {
        capture(c, mask, client) {
            return !client && mask & 0x40000 ? { activeTexture: c.activeTexture,
                units: c.units.map(u => [...u]), unitImages: c.unitImages.map(u => [...u]) } : null;
        },
        restore(c, saved) {
            c.activeTexture = saved.activeTexture;
            c.units = saved.units.map(u => new Map(u)); c.unitImages = saved.unitImages.map(u => new Map(u));
        },
    });
    state.attributes("framebufferSelection", {
        capture(c, mask, client) {
            return client ? null : { ...(mask & 0x4000 ? { draw: c.drawBuffers.slice() } : {}),
                ...(mask & 0x20 ? { read: c.readBuffer } : {}) };
        },
        restore(c, saved) {
            if (saved.draw) buffers.draw(c, c.framebuffers, saved.draw);
            if (saved.read !== undefined) buffers.read(c, c.framebuffers, saved.read);
        },
    });
    const draws = new Set(["CLEAR", "BEGIN", "DRAW_ARRAYS", "DRAW_ELEMENTS", "DRAW_ARRAYS_GL2", "DRAW_ELEMENTS_GL2",
        "DRAW_ARRAYS_DIRECT", "DRAW_ELEMENTS_DIRECT", "DRAW_RANGE_ELEMENTS_DIRECT", "MULTI_DRAW_ARRAYS_DIRECT",
        "MULTI_DRAW_ELEMENTS_DIRECT", "DRAW_PIXELS", "COPY_PIXELS", "BITMAP", "BLIT_FRAMEBUFFER"].map(name => P.GLFN[name]));
    const reads = new Set(["READ_PIXELS", "COPY_PIXELS", "BLIT_FRAMEBUFFER"].map(name => P.GLFN[name]));
    GLShadow.part({ checkCommand(s, fn, bytes, view, offset) {
        if (s.current.state.selection && s.current.state.selection.mode !== GL.RENDER && fn !== P.GLFN.READ_PIXELS) return 0;
        const badDraw = draws.has(fn) && s.framebufferStatus(GL.DRAW_FRAMEBUFFER) !== GL.FRAMEBUFFER_COMPLETE;
        const badRead = reads.has(fn) && s.framebufferStatus(GL.READ_FRAMEBUFFER) !== GL.FRAMEBUFFER_COMPLETE;
        if (!badDraw && !badRead) return 0;
        if (fn === P.GLFN.READ_PIXELS) s.answered("READ_PIXELS", view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
        return GL.INVALID_FRAMEBUFFER_OPERATION;
    } });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const parameters = global.GLTextureParameters || require("./gl_texture_parameters.js");
    const formats = global.GLImageFormats || require("./gl_image_formats.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    const IMAGE_TARGETS = [null, new Set([GL.TEXTURE_1D, GL.PROXY_TEXTURE_1D]),
        new Set([GL.TEXTURE_2D, GL.PROXY_TEXTURE_2D, GL.TEXTURE_RECTANGLE, GL.PROXY_TEXTURE_RECTANGLE, GL.PROXY_TEXTURE_CUBE_MAP,
            ...Array.from({ length: 6 }, (_, i) => GL.TEXTURE_CUBE_MAP_POSITIVE_X + i)]),
        new Set([GL.TEXTURE_3D, GL.PROXY_TEXTURE_3D])];
    const PIXEL_FORMATS = new Set([GL.COLOR_INDEX, GL.RED, GL.GREEN, GL.BLUE, GL.ALPHA, GL.RGB, GL.RGBA,
        GL.LUMINANCE, GL.LUMINANCE_ALPHA, GL.BGR, GL.BGRA, GL.DEPTH_COMPONENT]);
    const PIXEL_TYPES = new Set([GL.BYTE, GL.UNSIGNED_BYTE, GL.SHORT, GL.UNSIGNED_SHORT, GL.INT, GL.UNSIGNED_INT,
        GL.FLOAT, GL.HALF_FLOAT_ARB, 0x1A00]); // BITMAP
    const COMPONENTS = new Map([[GL.COLOR_INDEX, 1], [GL.RED, 1], [GL.GREEN, 1], [GL.BLUE, 1], [GL.ALPHA, 1],
        [GL.LUMINANCE, 1], [GL.LUMINANCE_ALPHA, 2], [GL.RGB, 3], [GL.BGR, 3], [GL.RGBA, 4], [GL.BGRA, 4], [GL.DEPTH_COMPONENT, 1]]);
    const SCALAR_BYTES = new Map([[GL.BYTE, 1], [GL.UNSIGNED_BYTE, 1], [GL.SHORT, 2], [GL.UNSIGNED_SHORT, 2],
        [GL.INT, 4], [GL.UNSIGNED_INT, 4], [GL.FLOAT, 4], [GL.HALF_FLOAT_ARB, 2]]);
    const RGB_PACKED = new Set([GL.UNSIGNED_BYTE_3_3_2, GL.UNSIGNED_BYTE_2_3_3_REV, GL.UNSIGNED_SHORT_5_6_5, GL.UNSIGNED_SHORT_5_6_5_REV]);
    const RGBA_PACKED = new Set([GL.UNSIGNED_SHORT_4_4_4_4, GL.UNSIGNED_SHORT_5_5_5_1, GL.UNSIGNED_SHORT_4_4_4_4_REV,
        GL.UNSIGNED_SHORT_1_5_5_5_REV, GL.UNSIGNED_INT_8_8_8_8, GL.UNSIGNED_INT_8_8_8_8_REV,
        GL.UNSIGNED_INT_10_10_10_2, GL.UNSIGNED_INT_2_10_10_10_REV]);
    const setters = new Set(["TEX_PARAMETERI", "TEX_PARAMETERF", "TEX_PARAMETERIV", "TEX_PARAMETERFV"]);
    const ensure = image => image.parameters || (image.parameters = parameters.initial(image.target));
    const copy = values => Object.fromEntries(Object.entries(values).map(([p, v]) => [p, v.slice()]));
    function pixelError(format, type, rectangle = false) {
        if (!(PIXEL_FORMATS.has(format) || rectangle && format === GL.STENCIL_INDEX) || !PIXEL_TYPES.has(type) && !RGB_PACKED.has(type) && !RGBA_PACKED.has(type) ||
            type === 0x1A00 && format !== GL.COLOR_INDEX && !(rectangle && format === GL.STENCIL_INDEX)) return GL.INVALID_ENUM;
        return RGB_PACKED.has(type) && format !== GL.RGB || RGBA_PACKED.has(type) && ![GL.RGBA, GL.BGRA].includes(format) ? GL.INVALID_OPERATION : 0;
    }
    function uploadSpan(c, r, dimensions, pack = false) {
        if (!r.width || dimensions >= 2 && !r.height || dimensions >= 3 && !r.depth) return 0;
        const value = pname => c.state.values[pack ? PACK_PNAMES.get(pname) : pname]?.[0] || 0;
        const height = dimensions >= 2 ? r.height : 1, depth = dimensions >= 3 ? r.depth : 1;
        let bits = r.type === 0x1A00 ? 1 : SCALAR_BYTES.get(r.type) * COMPONENTS.get(r.format) * 8;
        if (RGB_PACKED.has(r.type)) bits = [GL.UNSIGNED_BYTE_3_3_2, GL.UNSIGNED_BYTE_2_3_3_REV].includes(r.type) ? 8 : 16;
        if (RGBA_PACKED.has(r.type)) bits = [GL.UNSIGNED_INT_8_8_8_8, GL.UNSIGNED_INT_8_8_8_8_REV,
            GL.UNSIGNED_INT_10_10_10_2, GL.UNSIGNED_INT_2_10_10_10_REV].includes(r.type) ? 32 : 16;
        const alignment = value(GL.UNPACK_ALIGNMENT) || 4;
        const stride = Math.ceil(Math.ceil((value(GL.UNPACK_ROW_LENGTH) || r.width) * bits / 8) / alignment) * alignment;
        const imageStride = stride * (value(GL.UNPACK_IMAGE_HEIGHT) || height);
        const skipBits = value(GL.UNPACK_SKIP_PIXELS) * bits;
        return (dimensions >= 3 ? value(GL.UNPACK_SKIP_IMAGES) * imageStride : 0) + value(GL.UNPACK_SKIP_ROWS) * stride +
            Math.floor(skipBits / 8) + (depth - 1) * imageStride + (height - 1) * stride + Math.ceil((skipBits % 8 + r.width * bits) / 8);
    }
    const PACK_PNAMES = new Map(["ALIGNMENT", "ROW_LENGTH", "IMAGE_HEIGHT", "SKIP_IMAGES", "SKIP_ROWS", "SKIP_PIXELS"]
        .map(name => [GL["UNPACK_" + name], GL["PACK_" + name]]));
    GLShadow.prototype.queryTextureImage = function(bytes, view, offset, size) {
        const name = "QUERY_TEXTURE_IMAGE", fields = P.F[name], header = P.RECORDS[name].size;
        if (!this.pending(name, view, offset, size)) return;
        const r = P.decodeRecord(name, view, offset), c = this.current;
        const fail = error => {
            if (c && !c.error) c.error = error;
            view.setUint32(offset + fields.error, error, true);
            this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
        };
        if (!c || c.state.begin) return fail(GL.INVALID_OPERATION);
        if (r.mode === 3) {
            if (c.textureRead?.ticket === r.ticket) c.textureRead = null;
            return this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
        }
        if (r.mode === 1 || r.mode === 2) {
            const saved = c.textureRead;
            if (!saved || saved.ticket !== r.ticket || saved.context_id !== r.context_id ||
                !r.data_size || r.data_size > 262144 || r.data_size > size - header || r.offset + r.data_size > saved.span)
                return fail(GL.INVALID_OPERATION);
            for (const key of ["target", "level", "format", "type", "compressed", "span", "width", "height", "depth"])
                view.setUint32(offset + fields[key], saved[key], true);
            if (r.mode === 2) c.textureRead = null;
            return; // The executor answers only the resource bytes.
        }
        if (r.mode !== 0 || r.data_size) return fail(GL.INVALID_VALUE);
        const cube = r.target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && r.target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z;
        if (![GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_3D, GL.TEXTURE_RECTANGLE].includes(r.target) && !cube) return fail(GL.INVALID_ENUM);
        const maximum = this.capabilities ? caps.integer(this.capabilities, r.target === GL.TEXTURE_3D ? GL.MAX_3D_TEXTURE_SIZE :
            cube ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 8192;
        if (r.level < 0 || r.level > Math.floor(Math.log2(maximum)) || r.target === GL.TEXTURE_RECTANGLE && r.level !== 0) return fail(GL.INVALID_VALUE);
        const image = this.boundImage(r.target), info = image.texture.levels.get(image.layer)?.get(r.level);
        const base = info ? formats.storageFormatFor(info.internalFormat).base : "RGBA";
        if (r.compressed) {
            if (!info || !(info.compressed ?? !!formats.COMPRESSED_FORMATS[info.internalFormat])) return fail(GL.INVALID_OPERATION);
        } else {
            const error = pixelError(r.format, r.type); if (error) return fail(error);
            if ((base === "DEPTH") !== (r.format === GL.DEPTH_COMPONENT) ||
                (base === "COLOR_INDEX") !== (r.format === GL.COLOR_INDEX)) return fail(GL.INVALID_OPERATION);
        }
        const dimensions = r.target === GL.TEXTURE_3D ? 3 : r.target === GL.TEXTURE_1D ? 1 : 2;
        const values = { ...r, imageId: image.texture.id, layer: image.layer,
            width: info?.width || 0, height: info?.height || 0, depth: info?.depth || 0 };
        const block = info && formats.COMPRESSED_FORMATS[info.internalFormat];
        values.span = r.compressed ? info.compressedSize ?? Math.ceil(info.width / 4) * Math.ceil(info.height / 4) * info.depth * block.blockBytes :
            uploadSpan(c, values, dimensions, true);
        const pbo = this.boundBuffer?.(GL.PIXEL_PACK_BUFFER);
        const elementBytes = r.compressed || r.type === 0x1A00 ? 1 : SCALAR_BYTES.get(r.type) ||
            (RGB_PACKED.has(r.type) ? [GL.UNSIGNED_BYTE_3_3_2, GL.UNSIGNED_BYTE_2_3_3_REV].includes(r.type) ? 1 : 2 :
                [GL.UNSIGNED_INT_8_8_8_8, GL.UNSIGNED_INT_8_8_8_8_REV, GL.UNSIGNED_INT_10_10_10_2, GL.UNSIGNED_INT_2_10_10_10_REV].includes(r.type) ? 4 : 2);
        if (!Number.isSafeInteger(values.span) || values.span > 0xFFFFFFFF) return fail(GL.OUT_OF_MEMORY);
        if (pbo && (pbo.mapped || r.pointer % elementBytes || r.pointer + values.span > pbo.size)) return fail(GL.INVALID_OPERATION);
        values.ticket = values.span ? c.nextTextureRead = (c.nextTextureRead || 0) % 0xFFFFFFFF + 1 : 0;
        values.context_id = c.id;
        c.textureRead = values.span ? values : null;
        for (const key of ["span", "ticket", "width", "height", "depth", "context_id"])
            view.setUint32(offset + fields[key], values[key], true);
        this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [P.GLFN.QUERY_TEXTURE_IMAGE]: "queryTextureImage" });
    GLShadow.part({
        getState(s, saved) { saved.textureReads = [...s.contexts.values()].map(c => [c.key, c.nextTextureRead || 0, c.textureRead || null]); },
        setState(s, saved) { for (const [key, next, read] of saved.textureReads || []) {
            const c = s.contexts.get(key); if (c) { c.nextTextureRead = next; c.textureRead = read; }
        } },
    });
    function blockRegionError(info, r, compressed) {
        const block = formats.COMPRESSED_FORMATS[info.internalFormat];
        if (!(info.compressed ?? !!block) || !block) return 0;
        if (r.xoffset % 4 || r.yoffset % 4) return GL.INVALID_OPERATION;
        if ((compressed || r.xoffset || r.yoffset) &&
            (r.width % 4 && r.xoffset + r.width !== info.width || r.height % 4 && r.yoffset + r.height !== info.height))
            return GL.INVALID_OPERATION;
        return 0;
    }
    function copySourceError(s, internalFormat) {
        if (s.framebufferStatus(GL.READ_FRAMEBUFFER) !== GL.FRAMEBUFFER_COMPLETE) return GL.INVALID_FRAMEBUFFER_OPERATION;
        const base = formats.storageFormatFor(internalFormat).base;
        if (base === "DEPTH") return s.current.readFramebuffer && !s.boundFramebuffer(GL.READ_FRAMEBUFFER).depth ? GL.INVALID_OPERATION : 0;
        return base === "COLOR_INDEX" || s.current.readBuffer === GL.NONE ? GL.INVALID_OPERATION : 0;
    }
    GLShadow.rules({ [P.GLFN.GENERATE_MIPMAP]: (s, b, v, o, size) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const [target] = s.args("GENERATE_MIPMAP", v, o, size);
        if (![GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_3D, GL.TEXTURE_CUBE_MAP].includes(target)) return GL.INVALID_ENUM;
        if (target === GL.TEXTURE_CUBE_MAP) {
            const image = s.boundImage(target).texture, level = ensure(image)[GL.TEXTURE_BASE_LEVEL][0];
            const base = image.levels.get(0)?.get(level);
            if (!base || base.width <= 0 || base.height !== base.width) return GL.INVALID_OPERATION;
            for (let face = 1; face < 6; ++face) {
                const info = image.levels.get(face)?.get(level);
                if (!info || info.width !== base.width || info.height !== base.height ||
                    info.internalFormat !== base.internalFormat || (info.border || 0) !== (base.border || 0)) return GL.INVALID_OPERATION;
            }
        }
        return 0;
    } });
    for (const dimensions of [1, 2, 3]) for (const compressed of [false, true]) {
        const name = (compressed ? "COMPRESSED_TEX_IMAGE_" : "TEX_IMAGE_") + dimensions + "D";
        GLShadow.rules({ [P.GLFN[name]]: (s, b, v, o, size) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const r = P.decodeRecord(name, v, o), proxy = formats.PROXY_TARGETS.get(r.target);
            if (!IMAGE_TARGETS[dimensions].has(r.target)) return GL.INVALID_ENUM;
            const target = proxy || (dimensions === 2 && r.target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && r.target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z ? GL.TEXTURE_CUBE_MAP : r.target);
            const maximum = s.capabilities ? caps.integer(s.capabilities, target === GL.TEXTURE_3D ? GL.MAX_3D_TEXTURE_SIZE :
                target === GL.TEXTURE_CUBE_MAP ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 8192;
            if (target === GL.TEXTURE_RECTANGLE && (r.level !== 0 || r.border !== 0)) return GL.INVALID_VALUE;
            if (target === GL.TEXTURE_RECTANGLE && compressed) return GL.INVALID_ENUM;
            if (target === GL.TEXTURE_RECTANGLE && (r.format === GL.COLOR_INDEX || formats.BASE_INTERNAL_FORMATS[r.internalformat] === "COLOR_INDEX" || !compressed && !!formats.COMPRESSED_FORMATS[r.internalformat])) return GL.INVALID_ENUM;
            if (r.level < 0 || r.level > Math.floor(Math.log2(maximum)) || r.border < 0 || r.border > 1) return GL.INVALID_VALUE;
            const sizes = [r.width, ...(dimensions >= 2 ? [r.height] : []), ...(dimensions >= 3 ? [r.depth] : [])];
            const maximumSize = Math.max(1, Math.floor(maximum / 2 ** r.level)) + 2 * r.border;
            if (sizes.some(n => n < 2 * r.border || !proxy && n > maximumSize) || target === GL.TEXTURE_CUBE_MAP && r.width !== r.height)
                return GL.INVALID_VALUE;
            const block = formats.COMPRESSED_FORMATS[r.internalformat];
            if (compressed) {
                if (!block || dimensions !== 2) return GL.INVALID_ENUM;
                if (r.border) return GL.INVALID_OPERATION;
                if (r.image_size !== Math.ceil(r.width / 4) * Math.ceil(r.height / 4) * block.blockBytes) return GL.INVALID_VALUE;
            } else {
                if ((!formats.BASE_INTERNAL_FORMATS[r.internalformat] && !block) || [GL.BGR, GL.BGRA, GL.RGB565].includes(r.internalformat))
                    return GL.INVALID_VALUE;
                const error = pixelError(r.format, r.type); if (error) return error;
                const base = formats.storageFormatFor(r.internalformat).base;
                if ((base === "DEPTH") !== (r.format === GL.DEPTH_COMPONENT) ||
                    base === "COLOR_INDEX" && r.format !== GL.COLOR_INDEX || base === "DEPTH" && ![GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_RECTANGLE].includes(target))
                    return GL.INVALID_OPERATION;
            }
            const bytes = compressed ? r.image_size : r.data_size;
            if (!proxy && P.RECORDS[name].size + bytes > size) return GL.INVALID_VALUE;
            if (!proxy && !compressed && bytes && bytes < uploadSpan(s.current, r, dimensions)) return GL.INVALID_OPERATION;
            return 0;
        } });
    }
    for (const dimensions of [1, 2, 3]) for (const compressed of [false, true]) {
        const name = (compressed ? "COMPRESSED_TEX_SUB_IMAGE_" : "TEX_SUB_IMAGE_") + dimensions + "D";
        GLShadow.rules({ [P.GLFN[name]]: (s, b, v, o, size) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const r = P.decodeRecord(name, v, o);
            if (!IMAGE_TARGETS[dimensions].has(r.target) || formats.PROXY_TARGETS.has(r.target)) return GL.INVALID_ENUM;
            if (r.target === GL.TEXTURE_RECTANGLE && compressed) return GL.INVALID_ENUM;
            const cube = dimensions === 2 && r.target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && r.target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z;
            const maximum = s.capabilities ? caps.integer(s.capabilities, dimensions === 3 ? GL.MAX_3D_TEXTURE_SIZE :
                cube ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 8192;
            if (r.level < 0 || r.level > Math.floor(Math.log2(maximum)) || r.target === GL.TEXTURE_RECTANGLE && r.level !== 0) return GL.INVALID_VALUE;
            const image = s.boundImage(r.target), info = image.texture.levels.get(image.layer)?.get(r.level);
            if (!info) return GL.INVALID_OPERATION;
            const lengths = [r.width, ...(dimensions >= 2 ? [r.height] : []), ...(dimensions >= 3 ? [r.depth] : [])];
            const offsets = [r.xoffset, r.yoffset, r.zoffset], bounds = [info.width, info.height, info.depth], border = info.border || 0;
            if (lengths.some((n, i) => n < 0 || offsets[i] < -border || offsets[i] + n > bounds[i] - border)) return GL.INVALID_VALUE;
            const block = formats.COMPRESSED_FORMATS[info.internalFormat], isCompressed = info.compressed ?? !!block;
            if (compressed) {
                if (!formats.COMPRESSED_FORMATS[r.format] || dimensions !== 2) return GL.INVALID_ENUM;
                if (!isCompressed || r.format !== info.internalFormat) return GL.INVALID_OPERATION;
                if (r.image_size !== Math.ceil(r.width / 4) * Math.ceil(r.height / 4) * block.blockBytes) return GL.INVALID_VALUE;
            } else {
                const error = pixelError(r.format, r.type); if (error) return error;
                const base = formats.storageFormatFor(info.internalFormat).base;
                if ((base === "DEPTH") !== (r.format === GL.DEPTH_COMPONENT) || base === "COLOR_INDEX" && r.format !== GL.COLOR_INDEX)
                    return GL.INVALID_OPERATION;
            }
            const regionError = blockRegionError(info, r, compressed); if (regionError) return regionError;
            const bytes = compressed ? r.image_size : r.data_size;
            if (P.RECORDS[name].size + bytes > size) return GL.INVALID_VALUE;
            if (!compressed && bytes && bytes < uploadSpan(s.current, r, dimensions)) return GL.INVALID_OPERATION;
            return 0;
        } });
    }
    for (const dimensions of [1, 2]) state.rule("COPY_TEX_IMAGE_" + dimensions + "D", (c, a, s) => {
        const [target, level, internalFormat, , , width] = a, height = dimensions === 2 ? a[6] : 1, border = a[dimensions === 2 ? 7 : 6];
        if (!IMAGE_TARGETS[dimensions].has(target) || formats.PROXY_TARGETS.has(target)) return GL.INVALID_ENUM;
        const cube = dimensions === 2 && target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z;
        const maximum = s.capabilities ? caps.integer(s.capabilities, cube ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 8192;
        if (target === GL.TEXTURE_RECTANGLE && (level !== 0 || border !== 0)) return GL.INVALID_VALUE;
        if (target === GL.TEXTURE_RECTANGLE && formats.COMPRESSED_FORMATS[internalFormat]) return GL.INVALID_ENUM;
        if (level < 0 || level > Math.floor(Math.log2(maximum)) || border < 0 || border > 1) return GL.INVALID_VALUE;
        const max = Math.max(1, Math.floor(maximum / 2 ** level)) + 2 * border;
        if (width < 2 * border || width > max || dimensions === 2 && (height < 2 * border || height > max) || cube && width !== height)
            return GL.INVALID_VALUE;
        if (internalFormat <= 4 || (!formats.BASE_INTERNAL_FORMATS[internalFormat] && !formats.COMPRESSED_FORMATS[internalFormat]) ||
            [GL.BGR, GL.BGRA, GL.RGB565].includes(internalFormat) || formats.BASE_INTERNAL_FORMATS[internalFormat] === "COLOR_INDEX")
            return GL.INVALID_ENUM;
        if (cube && formats.storageFormatFor(internalFormat).depth) return GL.INVALID_OPERATION;
        return copySourceError(s, internalFormat);
    });
    for (const dimensions of [1, 2, 3]) state.rule("COPY_TEX_SUB_IMAGE_" + dimensions + "D", (c, a, s) => {
        const [target, level, xoffset] = a;
        if (!IMAGE_TARGETS[dimensions].has(target) || formats.PROXY_TARGETS.has(target)) return GL.INVALID_ENUM;
        const cube = dimensions === 2 && target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z;
        const maximum = s.capabilities ? caps.integer(s.capabilities, dimensions === 3 ? GL.MAX_3D_TEXTURE_SIZE :
            cube ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 8192;
        if (level < 0 || level > Math.floor(Math.log2(maximum)) || target === GL.TEXTURE_RECTANGLE && level !== 0) return GL.INVALID_VALUE;
        const image = s.boundImage(target), info = image.texture.levels.get(image.layer)?.get(level);
        if (!info) return GL.INVALID_OPERATION;
        const width = a[dimensions + 4], height = dimensions >= 2 ? a[dimensions + 5] : 1;
        const offsets = [xoffset, dimensions >= 2 ? a[3] : 0, dimensions === 3 ? a[4] : 0];
        const counts = [width, height, 1], bounds = [info.width, info.height, info.depth], border = info.border || 0;
        if (counts.slice(0, dimensions).some((n, i) => n < 0 || offsets[i] < -border || offsets[i] + n > bounds[i] - border))
            return GL.INVALID_VALUE;
        const regionError = blockRegionError(info, { xoffset, yoffset: offsets[1], width, height }, false);
        return regionError || copySourceError(s, info.internalFormat);
    });
    function command(name, a) {
        const scalar = name === "TEX_PARAMETERI" || name === "TEX_PARAMETERF";
        return parameters.normalize(a[1], scalar ? [a[2]] : a.slice(3, 3 + a[2]), name.endsWith("I") || name.endsWith("IV"), scalar, a[0]);
    }
    for (const name of setters) state.rule(name, (c, a) => {
        if (!parameters.targets.includes(a[0])) return GL.INVALID_ENUM;
        const d = parameters.definitions.get(a[1]);
        if (d && name.endsWith("V") && a[2] !== d.initial.length) return GL.INVALID_VALUE;
        return command(name, a).error || 0;
    });
    GLShadow.rules({ [P.GLFN.PRIORITIZE_TEXTURES]: (s, b, v, o, n) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const count = v.getInt32(o, true); return count < 0 || 4 + count * 8 > n ? GL.INVALID_VALUE : 0;
    } });
    GLShadow.prototype.prioritizeTextures = function(bytes, view, offset) {
        for (let i = 0; i < view.getInt32(offset, true); ++i) {
            const image = this.current.group.textures.get(view.getUint32(offset + 4 + i * 8, true));
            if (image?.target) ensure(image)[GL.TEXTURE_PRIORITY] = [Math.fround(Math.max(0, Math.min(1, view.getFloat32(offset + 8 + i * 8, true))))];
        }
    };
    GLShadow.install({ [P.GLFN.PRIORITIZE_TEXTURES]: "prioritizeTextures" });
    state.parameter(P.C.V86GL_PARAMETER_TEXTURE_RESIDENCY, (s, texture, checkName) => {
        if (checkName && (!texture || !s.current.group.textures.get(texture)?.target)) return GL.INVALID_VALUE;
        // There is no residency eviction policy: every live named texture is
        // resident. The CPU still validates names and Begin/End ordering.
        return [[1], "b"];
    });
    state.parameter(P.C.V86GL_PARAMETER_TEXTURE, (s, target, pname) => {
        if (!parameters.targets.includes(target)) return GL.INVALID_ENUM;
        const d = parameters.definitions.get(pname); if (!d) return GL.INVALID_ENUM;
        return [ensure(s.boundImage(target).texture)[pname], d.kind];
    });
    state.parameter(P.C.V86GL_PARAMETER_TEXTURE_LEVEL, (s, target, pname, level) => {
        const proxy = formats.PROXY_TARGETS.get(target);
        const cube = target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL.TEXTURE_CUBE_MAP_NEGATIVE_Z;
        if (![GL.TEXTURE_1D, GL.TEXTURE_2D, GL.TEXTURE_3D, GL.TEXTURE_RECTANGLE].includes(target) && !cube && !proxy) return GL.INVALID_ENUM;
        const maximum = s.capabilities ? caps.integer(s.capabilities, (proxy || target) === GL.TEXTURE_3D ? GL.MAX_3D_TEXTURE_SIZE :
            cube || proxy === GL.TEXTURE_CUBE_MAP ? GL.MAX_CUBE_MAP_TEXTURE_SIZE : GL.MAX_TEXTURE_SIZE) : 16384;
        if (level > Math.floor(Math.log2(maximum)) || (target === GL.TEXTURE_RECTANGLE || proxy === GL.TEXTURE_RECTANGLE) && level !== 0) return GL.INVALID_VALUE;
        const image = !proxy && s.boundImage(target), info = proxy ? s.current.proxyImages.get(target)?.get(level) : image.texture.levels.get(image.layer)?.get(level);
        const compressed = !!(info && (info.compressed ?? !!formats.COMPRESSED_FORMATS[info.internalFormat]));
        switch (pname) {
        case GL.TEXTURE_WIDTH: return [[info?.width || 0], "i"];
        case GL.TEXTURE_HEIGHT: return [[info?.height || 0], "i"];
        case GL.TEXTURE_DEPTH: return [[info?.depth || 0], "i"];
        case GL.TEXTURE_INTERNAL_FORMAT:
            return [[!info ? 1 : !compressed && formats.COMPRESSED_FORMATS[info.internalFormat] ?
                formats.COMPRESSED_FORMATS[info.internalFormat].base === "RGB" ? GL.RGB8 : GL.RGBA8 : info.internalFormat], "e"];
        case GL.TEXTURE_BORDER: return [[info?.border || 0], "i"];
        case GL.TEXTURE_COMPRESSED: return [[+compressed], "b"];
        case GL.TEXTURE_COMPRESSED_IMAGE_SIZE: {
            if (!compressed || proxy) return GL.INVALID_OPERATION;
            const block = formats.COMPRESSED_FORMATS[info.internalFormat]?.blockBytes || 0;
            return [[info.compressedSize ?? Math.ceil(info.width / 4) * Math.ceil(info.height / 4) * info.depth * block], "i"];
        }
        default: {
            if (pname >= 0x8C10 && pname <= 0x8C16) {
                const channel = pname - 0x8C10;
                const bits = info && !info.unsupported && formats.textureComponents(info.internalFormat, compressed, s.capabilities?.features)[channel];
                const storage = info && formats.storageFormatFor(info.internalFormat, s.capabilities?.features);
                return [[!bits ? GL.NONE : storage.float || storage.gpu === "depth32float" ? GL.FLOAT : 0x8C17], "e"];
            }
            const index = [GL.TEXTURE_RED_SIZE, GL.TEXTURE_GREEN_SIZE, GL.TEXTURE_BLUE_SIZE, GL.TEXTURE_ALPHA_SIZE,
                GL.TEXTURE_LUMINANCE_SIZE, GL.TEXTURE_INTENSITY_SIZE, GL.TEXTURE_DEPTH_SIZE, GL.TEXTURE_STENCIL_SIZE_EXT, 0x80ED].indexOf(pname);
            if (index < 0) return GL.INVALID_ENUM;
            return [[info && !info.unsupported ? formats.textureComponents(info.internalFormat, compressed, s.capabilities?.features)[index] : 0], "i"];
        }
        }
    });
    state.attributes("textureParameters", {
        capture(c, mask, client) {
            if (client || !(mask & 0x40000)) return null;
            const saved = new Map();
            for (const unit of c.unitImages) for (const target of parameters.targets) {
                const id = unit.get(target) || c.defaultTextures.get(target) || 0, key = id || "default:" + target;
                const image = c.group.textureImages.get(id);
                saved.set(key, { id, target, values: image ? ensure(image) : parameters.initial(target) });
            }
            return [...saved.values()].map(item => ({ ...item, values: copy(item.values) }));
        },
        restore(c, saved) {
            for (const item of saved) {
                const image = c.group.textureImages.get(item.id || c.defaultTextures.get(item.target));
                if (image) image.parameters = copy(item.values);
            }
        },
    });
    GLShadow.part({
        command(s, fn, bytes, view, offset, size) {
            const name = P.NAME_BY_OPCODE[fn]; if (!setters.has(name)) return;
            const a = s.args(name, view, offset, size), r = command(name, a);
            if (!r.error) ensure(s.boundImage(a[0]).texture)[a[1]] = r.values;
        },
    });
    const api = { pixelError, uploadSpan };
    global.V86GLShadowTextures = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Buffer names, retained identities, storage and CPU queries (plan C3/C6).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const targets = new Set([GL.ARRAY_BUFFER, GL.ELEMENT_ARRAY_BUFFER, GL.PIXEL_PACK_BUFFER, GL.PIXEL_UNPACK_BUFFER]);
    const usages = new Set([0x88E0, 0x88E1, 0x88E2, 0x88E4, 0x88E5, 0x88E6, 0x88E8, 0x88E9, 0x88EA]);
    const proto = GLShadow.prototype;
    proto.bufferObject = function(name) {
        const group = this.current.group;
        let buffer = group.buffers.get(name);
        if (!buffer) {
            buffer = { name, id: group.nextBufferId++, created: false, deleted: false, size: 0, usage: GL.STATIC_DRAW_ARB, data: null,
                access: GL.READ_WRITE_ARB, mapped: false, pointer: 0 };
            group.buffers.set(name, buffer); group.bufferObjects.set(buffer.id, buffer);
        }
        return buffer;
    };
    proto.boundBuffer = function(target) {
        return this.current.group.bufferObjects.get(this.current.bufferBindings.get(target));
    };
    for (const name of ["GEN_BUFFERS", "DELETE_BUFFERS"]) {
        GLShadow.rules({ [P.GLFN[name]]: (s, b, v, o, size) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const count = v.getInt32(o, true);
            return count < 0 || count * 4 + 4 > size ? GL.INVALID_VALUE : 0;
        } });
        const method = "shadow" + name;
        proto[method] = function(bytes, v, o) {
            const s = this, c = s.current;
            const count = v.getUint32(o, true);
            for (let i = 0; i < count; ++i) {
                const name = v.getUint32(o + 4 + i * 4, true);
                if (!name) continue;
                if (method === "shadowGEN_BUFFERS") s.bufferObject(name);
                else {
                    const buffer = c.group.buffers.get(name); if (!buffer) continue;
                    c.group.buffers.delete(name); buffer.deleted = true;
                    for (const [target, id] of c.bufferBindings) if (id === buffer.id) c.bufferBindings.delete(target);
                }
            }
        };
        GLShadow.install({ [P.GLFN[name]]: method });
    }
    state.rule("BIND_BUFFER", (c, [target]) => targets.has(target) ? 0 : GL.INVALID_ENUM);
    state.on("BIND_BUFFER", (c, [target, name], s) => {
        if (!name) c.bufferBindings.delete(target);
        else { const buffer = s.bufferObject(name); buffer.created = true; c.bufferBindings.set(target, buffer.id); }
    });
    for (const name of ["BUFFER_DATA", "BUFFER_SUB_DATA"]) {
        GLShadow.rules({ [P.GLFN[name]]: (s, b, v, o, size) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const r = P.decodeRecord(name, v, o);
            if (!targets.has(r.target)) return GL.INVALID_ENUM;
            if (r.size < 0 || name === "BUFFER_SUB_DATA" && r.offset < 0) return GL.INVALID_VALUE;
            if (name === "BUFFER_DATA" && !usages.has(r.usage)) return GL.INVALID_ENUM;
            const buffer = s.boundBuffer(r.target); if (!buffer) return GL.INVALID_OPERATION;
            if (name === "BUFFER_SUB_DATA" && buffer.mapped) return GL.INVALID_OPERATION;
            if (r.data_size > size - P.RECORDS[name].size || r.data_size && r.data_size < r.size ||
                name === "BUFFER_SUB_DATA" && (r.offset + r.size > buffer.size || r.data_size < r.size)) return GL.INVALID_VALUE;
            if (name === "BUFFER_DATA") {
                try { s.pendingBufferData = new Uint8Array(r.size); }
                catch (_) { return GL.OUT_OF_MEMORY; }
            }
            return 0;
        } });
        const method = "shadow" + name;
        proto[method] = function(bytes, v, o) {
            const s = this;
            const r = P.decodeRecord(name, v, o), buffer = s.boundBuffer(r.target);
            const data = new Uint8Array(v.buffer, v.byteOffset + o + P.RECORDS[name].size, Math.min(r.data_size, r.size));
            if (name === "BUFFER_DATA") {
                buffer.data = s.pendingBufferData; delete s.pendingBufferData;
                buffer.size = r.size; buffer.usage = r.usage;
                buffer.access = GL.READ_WRITE_ARB; buffer.mapped = false; buffer.pointer = 0;
            }
            if (data.length) buffer.data.set(data, name === "BUFFER_DATA" ? 0 : r.offset);
        };
        GLShadow.install({ [P.GLFN[name]]: method });
    }
    for (const [pname, target] of [[GL.ARRAY_BUFFER_BINDING, GL.ARRAY_BUFFER], [GL.ELEMENT_ARRAY_BUFFER_BINDING, GL.ELEMENT_ARRAY_BUFFER],
            [GL.PIXEL_PACK_BUFFER_BINDING, GL.PIXEL_PACK_BUFFER], [GL.PIXEL_UNPACK_BUFFER_BINDING, GL.PIXEL_UNPACK_BUFFER]])
        state.query([pname], s => [[s.boundBuffer(target)?.name || 0], "u"]);
    state.parameter(P.C.V86GL_PARAMETER_BUFFER, (s, target, pname) => {
        if (!targets.has(target)) return GL.INVALID_ENUM;
        const buffer = s.boundBuffer(target); if (!buffer) return GL.INVALID_OPERATION;
        if (pname === GL.BUFFER_SIZE_ARB) return [[buffer.size], "i"];
        if (pname === GL.BUFFER_USAGE_ARB) return [[buffer.usage], "e"];
        if (pname === GL.BUFFER_ACCESS_ARB) return [[buffer.access], "e"];
        if (pname === GL.BUFFER_MAPPED_ARB) return [[+buffer.mapped], "b"];
        return GL.INVALID_ENUM;
    });
    state.parameter(P.C.V86GL_PARAMETER_BUFFER_POINTER, (s, target, pname) => {
        if (!targets.has(target) || pname !== GL.BUFFER_MAP_POINTER_ARB) return GL.INVALID_ENUM;
        const buffer = s.boundBuffer(target); if (!buffer) return GL.INVALID_OPERATION;
        return [[buffer.pointer], "u"];
    });
    for (const name of ["QUERY_BUFFER_DATA", "QUERY_BUFFER_MAP", "QUERY_BUFFER_UNMAP"]) {
        const method = "shadow" + name;
        proto[method] = function(bytes, view, offset, size) {
            if (!this.pending(name, view, offset, size)) return;
            const r = P.decodeRecord(name, view, offset), header = P.RECORDS[name].size;
            const fail = error => {
                if (this.current && !this.current.error) this.current.error = error;
                view.setUint32(offset + P.F[name].error, error, true);
                this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
            };
            const arrayEncoding = name === "QUERY_BUFFER_DATA" && !targets.has(r.target);
            if (!this.current || this.current.state.begin && !arrayEncoding) return fail(GL.INVALID_OPERATION);
            let buffer;
            if (targets.has(r.target)) {
                buffer = name === "QUERY_BUFFER_DATA" && r.buffer ? this.current.group.buffers.get(r.buffer) : this.boundBuffer(r.target);
            } else if (name === "QUERY_BUFFER_DATA") {
                // Read the captured object, including a deleted VBO retained
                // by another context or a client attribute stack. Names may
                // already identify a new object by the time it is encoded.
                const arrays = this.current.state.arrays, keys = new Map([[GL.VERTEX_ARRAY,"vertex"],[GL.NORMAL_ARRAY,"normal"],
                    [GL.COLOR_ARRAY,"color"],[GL.INDEX_ARRAY,"index"],[GL.EDGE_FLAG_ARRAY,"edgeFlag"],
                    [GL.SECONDARY_COLOR_ARRAY_EXT,"secondaryColor"],[GL.FOG_COORDINATE_ARRAY_EXT,"fogCoord"]]);
                const array = r.target === GL.TEXTURE_COORD_ARRAY ? arrays?.texCoord?.[r.buffer] :
                    r.target === GL.VERTEX_ATTRIB_ARRAY_POINTER ? arrays?.generic?.[r.buffer] : arrays?.[keys.get(r.target)];
                if (!array) return fail(GL.INVALID_ENUM);
                buffer = this.current.group.bufferObjects.get(array.bufferId);
            } else return fail(GL.INVALID_ENUM);
            if (!buffer || !buffer.created) return fail(GL.INVALID_OPERATION);
            if (name === "QUERY_BUFFER_DATA") {
                if (r.offset < 0 || r.size < 0 || r.range_size < r.size || r.offset + r.range_size > buffer.size ||
                    r.size > r.data_size || r.data_size > size - header) return fail(GL.INVALID_VALUE);
                if (buffer.mapped) return fail(GL.INVALID_OPERATION);
                if (r.size) bytes.set(buffer.data.subarray(r.offset, r.offset + r.size), offset + header);
            } else if (name === "QUERY_BUFFER_MAP") {
                if (![GL.READ_ONLY_ARB, GL.WRITE_ONLY_ARB, GL.READ_WRITE_ARB].includes(r.access)) return fail(GL.INVALID_ENUM);
                if (buffer.mapped) return fail(GL.INVALID_OPERATION);
                if (!r.pointer) return fail(GL.OUT_OF_MEMORY);
                buffer.mapped = true; buffer.access = r.access; buffer.pointer = r.pointer;
            } else {
                if (!buffer.mapped) return fail(GL.INVALID_OPERATION);
                buffer.mapped = false; buffer.pointer = 0;
                view.setUint32(offset + P.F[name].result, 1, true);
            }
            this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
        };
        GLShadow.install({ [P.GLFN[name]]: method });
    }
    state.attributes("buffers", {
        capture(c, mask, client) {
            if (!client) return null;
            return [...targets].filter(target => target === GL.PIXEL_PACK_BUFFER || target === GL.PIXEL_UNPACK_BUFFER ? mask & 1 : mask & 2)
                .map(target => [target, c.bufferBindings.get(target) || 0]);
        },
        restore(c, saved) {
            for (const [target, id] of saved) { if (id) c.bufferBindings.set(target, id); else c.bufferBindings.delete(target); }
        },
    });
    GLShadow.part({
        afterBatch(s) {
            const arrayIds = (formats, live) => {
                for (const value of Object.values(formats || {})) for (const a of Array.isArray(value) ? value : [value])
                    if (a.bufferId) live.add(a.bufferId);
            };
            for (const group of s.groups.values()) {
                const live = new Set([...group.buffers.values()].map(b => b.id));
                for (const c of s.contexts.values()) if (c.group === group) {
                    for (const id of c.bufferBindings.values()) live.add(id);
                    arrayIds(c.state.arrays, live);
                    for (const saved of c.state.clientAttrib) {
                        for (const [, id] of saved.parts?.buffers || []) if (id) live.add(id);
                        arrayIds(saved.parts?.arrays?.formats, live);
                    }
                }
                for (const id of group.bufferObjects.keys()) if (!live.has(id)) group.bufferObjects.delete(id);
            }
        },
        group(g) { g.buffers = new Map(); g.bufferObjects = new Map(); g.nextBufferId = 1; },
        context(c) { c.bufferBindings = new Map(); },
        getState(s, saved) {
            saved.buffers = { groups: [...s.groups.values()].map(g => ({ key: g.key, nextBufferId: g.nextBufferId,
                objects: [...g.bufferObjects.values()].map(b => ({ ...b, data: b.data && Array.from(b.data) })) })),
            contexts: [...s.contexts.values()].map(c => [c.key, [...c.bufferBindings]]) };
        },
        setState(s, saved) {
            for (const item of saved.buffers?.groups || []) {
                const g = s.groups.get(item.key); if (!g) continue; g.nextBufferId = item.nextBufferId;
                for (const data of item.objects) {
                    const buffer = { access: GL.READ_WRITE_ARB, mapped: false, pointer: 0, ...data, data: data.data && Uint8Array.from(data.data) };
                    g.bufferObjects.set(buffer.id, buffer); if (!buffer.deleted) g.buffers.set(buffer.name, buffer);
                }
            }
            for (const [key, bindings] of saved.buffers?.contexts || []) { const c = s.contexts.get(key); if (c) c.bufferBindings = new Map(bindings); }
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Client array formats/pointers are CPU state even when vertices themselves
// are copied into a later packed draw by the guest encoder (plan C3/C6).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    if (typeof module !== "undefined") { require("./gl_shadow_raster.js"); require("./gl_shadow_buffers.js"); }
    const clone = GLShadow.clone;
    const FLOATS = [GL.FLOAT, GL.DOUBLE], VERTEX_TYPES = [GL.SHORT, GL.INT, ...FLOATS];
    const NORMAL_TYPES = [GL.BYTE, ...VERTEX_TYPES], COLOR_TYPES = [GL.UNSIGNED_BYTE, GL.UNSIGNED_SHORT, GL.UNSIGNED_INT, ...NORMAL_TYPES];
    // key, command, size, types, enable, size/type/stride/pointer/buffer pnames
    const definitions = [
        ["vertex", "VERTEX_POINTER_VBO", 4, VERTEX_TYPES, GL.VERTEX_ARRAY, [0x807A, 0x807B, 0x807C, 0x808E, 0x8896]],
        ["normal", "NORMAL_POINTER_VBO", 3, NORMAL_TYPES, GL.NORMAL_ARRAY, [0, 0x807E, 0x807F, 0x808F, 0x8897]],
        ["color", "COLOR_POINTER_VBO", 4, COLOR_TYPES, GL.COLOR_ARRAY, [0x8081, 0x8082, 0x8083, 0x8090, 0x8898]],
        ["index", "INDEX_POINTER_VBO", 1, [GL.UNSIGNED_BYTE, ...VERTEX_TYPES], GL.INDEX_ARRAY, [0, 0x8085, 0x8086, 0x8091, 0x8899]],
        ["texCoord", "TEX_COORD_POINTER_VBO", 4, VERTEX_TYPES, GL.TEXTURE_COORD_ARRAY, [0x8088, 0x8089, 0x808A, 0x8092, 0x889A]],
        ["edgeFlag", "EDGE_FLAG_POINTER_VBO", 1, [GL.UNSIGNED_BYTE], GL.EDGE_FLAG_ARRAY, [0, 0, 0x808C, 0x8093, 0x889B]],
        ["secondaryColor", "SECONDARY_COLOR_POINTER_VBO", 3, COLOR_TYPES, GL.SECONDARY_COLOR_ARRAY_EXT, [0x845A, 0x845B, 0x845C, 0x845D, 0x889C]],
        ["fogCoord", "FOG_COORD_POINTER_VBO", 1, FLOATS, GL.FOG_COORDINATE_ARRAY_EXT, [0, 0x8454, 0x8455, 0x8456, 0x889D]],
    ];
    const initial = (size, type = GL.FLOAT) => ({ size, type, stride: 0, pointer: 0, bufferId: 0, normalized: false });
    function arrays() {
        const result = Object.fromEntries(definitions.map(([key, , size]) => [key, key === "texCoord" ?
            Array.from({ length: caps.LIMITS.MAX_TEXTURE_COORDS }, () => initial(size)) : initial(size, key === "edgeFlag" ? GL.UNSIGNED_BYTE : GL.FLOAT)]));
        result.generic = Array.from({ length: caps.LIMITS.MAX_VERTEX_ATTRIBS }, () => ({ ...initial(4), enabled: false, current: [0, 0, 0, 1] }));
        result.lock = { first: 0, count: 0 };
        return result;
    }
    const get = (c, key) => key === "texCoord" ? c.state.arrays.texCoord[c.state.clientTexture] : c.state.arrays[key];
    for (const [pname, field] of [[GL.ARRAY_ELEMENT_LOCK_FIRST_EXT, "first"], [GL.ARRAY_ELEMENT_LOCK_COUNT_EXT, "count"]])
        state.query([pname], s => [[s.current.state.arrays.lock?.[field] || 0], "i"]);
    GLShadow.prototype.queryArrayLock = function(bytes, view, offset, size) {
        const name = "QUERY_ARRAY_LOCK";if (!this.pending(name, view, offset, size)) return;
        const c = this.current, r = P.decodeRecord(name, view, offset), lock = c?.state.arrays.lock || { first: 0, count: 0 };
        const error = !c || c.state.begin ? GL.INVALID_OPERATION : r.operation > 2 ? GL.INVALID_ENUM :
            !r.operation && (r.first < 0 || r.count <= 0) ? GL.INVALID_VALUE :
                r.operation < 2 && (!!lock.count === !r.operation) ? GL.INVALID_OPERATION : 0;
        if (error) {
            if (c && !c.error) c.error = error;view.setUint32(offset + P.F[name].error, error, true);
            return this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
        }
        if (!r.operation) c.state.arrays.lock = { first: r.first, count: r.count };
        else if (r.operation === 1) c.state.arrays.lock = { first: 0, count: 0 };
        const current = c.state.arrays.lock || lock;
        view.setInt32(offset + P.F[name].first, current.first, true);view.setInt32(offset + P.F[name].count, current.count, true);
        view.setUint32(offset + P.F[name].error, 0, true);this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [P.GLFN.QUERY_ARRAY_LOCK]: "queryArrayLock" });
    const nameOf = (c, array) => c.group.bufferObjects.get(array.bufferId)?.name || 0;
    const pointers = new Map(definitions.map(d => [P.GLFN[d[1]], d]));
    for (const [key, command, , types, , pnames] of definitions) {
        GLShadow.rules({ [P.GLFN[command]]: (s, b, v, o) => {
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            const r = P.decodeRecord(command, v, o);
            const validSize = key === "vertex" ? r.size >= 2 && r.size <= 4 : key === "color" ? r.size === 3 || r.size === 4 :
                key === "texCoord" ? r.size >= 1 && r.size <= 4 : r.size === (key === "normal" || key === "secondaryColor" ? 3 : 1);
            if (!validSize || r.stride < 0) return GL.INVALID_VALUE;
            return types.includes(r.type) ? 0 : GL.INVALID_ENUM;
        } });
        for (const [i, field, kind] of [[0, "size", "i"], [1, "type", "e"], [2, "stride", "i"], [4, "bufferId", "u"]])
            if (pnames[i]) state.query([pnames[i]], s => { const array = get(s.current, key); return [[field === "bufferId" ? nameOf(s.current, array) : array[field]], kind]; });
    }
    GLShadow.rules({ [P.GLFN.VERTEX_ATTRIB_POINTER_VBO]: (s, b, v, o) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const r = P.decodeRecord("VERTEX_ATTRIB_POINTER_VBO", v, o);
        if (r.index >= caps.LIMITS.MAX_VERTEX_ATTRIBS || r.size < 1 || r.size > 4 || r.stride < 0) return GL.INVALID_VALUE;
        return COLOR_TYPES.includes(r.type) ? 0 : GL.INVALID_ENUM;
    } });
    for (const [command, enabled] of [["ENABLE_VERTEX_ATTRIB_ARRAY", true], ["DISABLE_VERTEX_ATTRIB_ARRAY", false]]) {
        state.rule(command, (c, [index]) => index < caps.LIMITS.MAX_VERTEX_ATTRIBS ? 0 : GL.INVALID_VALUE);
        state.on(command, (c, [index]) => { c.state.arrays.generic[index].enabled = enabled; });
    }
    state.rule("VERTEX_ATTRIB4F", (c, [index]) => index < caps.LIMITS.MAX_VERTEX_ATTRIBS ? 0 : GL.INVALID_VALUE, true);
    state.on("VERTEX_ATTRIB4F", (c, [index, ...value]) => { c.state.arrays.generic[index].current = value.map(Math.fround); }, true);
    state.parameter(P.C.V86GL_PARAMETER_VERTEX_ATTRIB, (s, index, pname) => {
        if (index >= caps.LIMITS.MAX_VERTEX_ATTRIBS) return GL.INVALID_VALUE;
        const array = s.current.state.arrays.generic[index];
        if (pname === GL.CURRENT_VERTEX_ATTRIB) return index === 0 ? GL.INVALID_OPERATION : [array.current, "t"];
        if (pname === GL.VERTEX_ATTRIB_ARRAY_ENABLED) return [[+array.enabled], "b"];
        if (pname === GL.VERTEX_ATTRIB_ARRAY_SIZE) return [[array.size], "i"];
        if (pname === GL.VERTEX_ATTRIB_ARRAY_STRIDE) return [[array.stride], "i"];
        if (pname === GL.VERTEX_ATTRIB_ARRAY_TYPE) return [[array.type], "e"];
        if (pname === GL.VERTEX_ATTRIB_ARRAY_NORMALIZED) return [[+array.normalized], "b"];
        if (pname === GL.VERTEX_ATTRIB_ARRAY_BUFFER_BINDING) return [[nameOf(s.current, array)], "u"];
        return GL.INVALID_ENUM;
    });
    GLShadow.prototype.arrayPointerParameter = function(target, pname, index) {
        const s = this;
        if (target === GL.VERTEX_ATTRIB_ARRAY_POINTER) {
            if (index >= caps.LIMITS.MAX_VERTEX_ATTRIBS) return GL.INVALID_VALUE;
            return pname === GL.VERTEX_ATTRIB_ARRAY_POINTER ? [[s.current.state.arrays.generic[index].pointer], "u"] : GL.INVALID_ENUM;
        }
        const definition = definitions.find(d => d[5][3] === pname);
        if (definition) return [[get(s.current, definition[0]).pointer], "u"];
        if ([0x0DF0, 0x0DF3].includes(pname)) return null; // selection/feedback buffers migrate with those modes
        return GL.INVALID_ENUM;
    };
    state.parameter(P.C.V86GL_PARAMETER_ARRAY_POINTER, (s, ...args) => s.arrayPointerParameter(...args));
    state.attributes("arrays", {
        capture(c, mask, client) {
            if (client) return mask & 2 ? { formats: clone(c.state.arrays) } : null;
            return mask & 1 ? { current: c.state.arrays.generic.map(a => a.current.slice()) } : null;
        },
        restore(c, saved) {
            if (saved.formats) {
                const current = c.state.arrays.generic.map(a => a.current);
                c.state.arrays = saved.formats;
                c.state.arrays.generic.forEach((a, i) => { a.current = current[i]; });
            }
            if (saved.current) saved.current.forEach((value, i) => { c.state.arrays.generic[i].current = value; });
        },
    });
    const draws = new Set([P.GLFN.DRAW_ARRAYS, P.GLFN.DRAW_ELEMENTS,
        P.GLFN.DRAW_ARRAYS_GL2, P.GLFN.DRAW_ELEMENTS_GL2, P.GLFN.DRAW_ARRAYS_DIRECT, P.GLFN.DRAW_ELEMENTS_DIRECT,
        P.GLFN.DRAW_RANGE_ELEMENTS_DIRECT, P.GLFN.MULTI_DRAW_ARRAYS_DIRECT, P.GLFN.MULTI_DRAW_ELEMENTS_DIRECT]);
    function enabledArrays(c) {
        const a = c.state.arrays, result = a.generic.filter(array => array.enabled);
        for (const [key, , , , enable] of definitions) {
            if (key === "texCoord") a.texCoord.forEach((array, i) => { if (c.state.raster.texcoord[i]) result.push(array); });
            else if (c.state.raster.client[enable]) result.push(a[key]);
        }
        return result;
    }
    GLShadow.prototype.arrayInputError = function(fn) {
        const c = this.current, mapped = array => c.group.bufferObjects.get(array.bufferId)?.mapped;
        if (enabledArrays(c).some(mapped)) return GL.INVALID_OPERATION;
        if (P.NAME_BY_OPCODE[fn].includes("ELEMENTS") && this.boundBuffer(GL.ELEMENT_ARRAY_BUFFER)?.mapped) return GL.INVALID_OPERATION;
        return 0;
    };
    GLShadow.part({
        context(c) { c.state.arrays = arrays(); },
        setState(s) { for (const c of s.contexts.values()) if (!c.state.arrays) c.state.arrays = arrays(); },
        checkCommand(s, fn) {
            if (!draws.has(fn)) return 0;
            // List draws own captured client bytes. Unrelated VBO mapping at
            // call time does not participate in those frozen inputs.
            if (s.current.listExecution && [P.GLFN.DRAW_ARRAYS, P.GLFN.DRAW_ELEMENTS, P.GLFN.DRAW_ARRAYS_GL2, P.GLFN.DRAW_ELEMENTS_GL2].includes(fn)) return 0;
            return s.arrayInputError(fn);
        },
        command(s, fn, bytes, view, offset) {
            const c = s.current; if (!c) return;
            const definition = pointers.get(fn), generic = fn === P.GLFN.VERTEX_ATTRIB_POINTER_VBO;
            if (definition || generic) {
                const r = P.decodeRecord(P.NAME_BY_OPCODE[fn], view, offset), array = generic ? c.state.arrays.generic[r.index] : get(c, definition[0]);
                Object.assign(array, { size: r.size, type: r.type, stride: r.stride, pointer: r.offset,
                    bufferId: c.bufferBindings.get(GL.ARRAY_BUFFER) || 0, normalized: generic && !!r.normalized });
            } else if (fn === P.GLFN.DELETE_BUFFERS) {
                const ids = new Set(); for (let i = 0; i < view.getUint32(offset, true); ++i) {
                    const b = c.group.buffers.get(view.getUint32(offset + 4 + i * 4, true)); if (b) ids.add(b.id);
                }
                for (const value of Object.values(c.state.arrays)) for (const array of Array.isArray(value) ? value : [value])
                    if (ids.has(array.bufferId)) array.bufferId = 0;
            }
        },
    });
    const api = { enabledArrays };
    global.V86GLShadowArrays = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Synchronous CPU name allocation shares the same namespaces as binds and
// deletion, so glGen* cannot collide with application-chosen names (C3).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const { C } = P;
    GLShadow.prototype.queryNames = function(bytes, view, offset, size) {
        const name = "QUERY_NAMES", header = P.RECORDS[name].size;
        if (!this.pending(name, view, offset, size)) return;
        const r = P.decodeRecord(name, view, offset);
        const fail = error => {
            if (this.current && !this.current.error) this.current.error = error;
            view.setUint32(offset + P.F[name].error, error, true);
            this.answered(name, view, offset, C.V86GL_SYNC_QUERY_STATUS_FAILED);
        };
        if (!this.current || this.current.state.begin) return fail(GL.INVALID_OPERATION);
        if (r.count < 0 || r.count * 4 > r.data_size || r.data_size > size - header) return fail(GL.INVALID_VALUE);
        const c = this.current, group = c.group;
        let names, allocate, pool, owner = group, cursorKind = r.object_kind;
        switch (r.object_kind) {
        case C.V86GL_OBJECT_KIND_SHADER:
        case C.V86GL_OBJECT_KIND_PROGRAM:
            cursorKind = C.V86GL_OBJECT_KIND_SHADER;
            names = { size: group.shaders.size + group.programs.size + group.glslNames.size,
                has: n => group.shaders.has(n) || group.programs.has(n) || group.glslNames.has(n),
                delete: n => group.glslNames.delete(n) };
            allocate = n => { group.glslNames.set(n, r.object_kind); return { name: n }; }; break;
        case C.V86GL_OBJECT_KIND_ARB_PROGRAM: names = group.arbPrograms; allocate = n => this.arbNamedProgram(n); pool = group.arbObjects; break;
        case C.V86GL_OBJECT_KIND_QUERY:
            if (c.activeQueryName) return fail(GL.INVALID_OPERATION);
            names = c.queryObjects; allocate = n => this.queryObject(n); owner = c; break;
        case C.V86GL_OBJECT_KIND_TEXTURE: names = group.textures; allocate = n => this.textureObject(n); pool = group.textureImages; break;
        case C.V86GL_OBJECT_KIND_RENDERBUFFER: names = group.renderbuffers; allocate = n => this.renderbufferObject(n); pool = group.renderbufferImages; break;
        case C.V86GL_OBJECT_KIND_BUFFER: names = group.buffers; allocate = n => this.bufferObject(n); pool = group.bufferObjects; break;
        case C.V86GL_OBJECT_KIND_FRAMEBUFFER:
            names = { size: group.framebufferObjects.size,
                has: n => group.sharedFramebuffers.has(n) || group.framebufferReservations.has(n) ||
                    [...this.contexts.values()].some(other => other.group === group && other.privateFramebuffers.has(n)),
                delete: n => { group.framebufferReservations.delete(n); c.framebuffers.delete(n); } };
            allocate = n => this.framebufferObject(n); pool = group.framebufferObjects; break;
        default: return fail(GL.INVALID_ENUM);
        }
        if (r.count > 0xFFFFFFFF - names.size) return fail(GL.OUT_OF_MEMORY);
        let next = owner.nameCursors.get(cursorKind) || 1;
        const chosen = [], used = new Set();
        for (let i = 0; i < r.count; ++i) {
            while (names.has(next) || used.has(next)) next = (next + 1) >>> 0 || 1;
            chosen.push(next); used.add(next); next = (next + 1) >>> 0 || 1;
        }
        const created = [], nextImageId = group.nextImageId, nextBufferId = group.nextBufferId, nextArbProgramId = group.nextArbProgramId,
            nextFramebufferId = group.nextFramebufferId;
        try { for (const n of chosen) created.push(allocate(n)); }
        catch (_) {
            for (const object of created) { names.delete(object.name); if (pool) pool.delete(object.id); }
            group.nextImageId = nextImageId; group.nextBufferId = nextBufferId;
            group.nextArbProgramId = nextArbProgramId;
            group.nextFramebufferId = nextFramebufferId;
            return fail(GL.OUT_OF_MEMORY);
        }
        for (let i = 0; i < chosen.length; ++i) view.setUint32(offset + header + i * 4, chosen[i], true);
        if (r.count) owner.nameCursors.set(cursorKind, next);
        this.answered(name, view, offset, C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [P.GLFN.QUERY_NAMES]: "queryNames" });
    GLShadow.part({
        group(g) { g.nameCursors = new Map(); g.glslNames = new Map(); },
        context(c) { c.nameCursors = new Map(); },
        getState(s, saved) {
            saved.names = { groups: [...s.groups.values()].map(g => [g.key, [...g.nameCursors], [...g.glslNames]]),
                contexts: [...s.contexts.values()].map(c => [c.key, [...c.nameCursors]]) };
        },
        setState(s, saved) {
            for (const [key, values, glsl] of saved.names?.groups || []) {
                const g = s.groups.get(key); if (g) { g.nameCursors = new Map(values); g.glslNames = new Map(glsl || []); }
            }
            for (const [key, values] of saved.names?.contexts || []) { const c = s.contexts.get(key); if (c) c.nameCursors = new Map(values); }
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Occlusion query names and logical state are private to each context. Only
// completed GPU counts/availability need the renderer (GL 2.1 4.1.7, C3/C7).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const targets = new Set([GL.SAMPLES_PASSED, GL.ANY_SAMPLES_PASSED, GL.ANY_SAMPLES_PASSED_CONSERVATIVE]);
    GLShadow.prototype.queryObject = function(name) {
        const objects = this.current.queryObjects;
        let object = objects.get(name);
        if (!object) objects.set(name, object = { name, target: 0, created: false, active: false });
        return object;
    };
    for (const name of ["GEN_QUERIES", "DELETE_QUERIES"]) {
        GLShadow.rules({ [P.GLFN[name]]: (s, b, v, o, size) => {
            if (s.current.state.begin || s.current.activeQueryName) return GL.INVALID_OPERATION;
            const count = v.getInt32(o, true); return count < 0 || 4 + count * 4 > size ? GL.INVALID_VALUE : 0;
        } });
        const method = "shadow" + name;
        GLShadow.prototype[method] = function(bytes, view, offset, size) {
            for (const id of this.names(name, view, offset, size) || []) if (id) {
                if (name === "GEN_QUERIES") this.queryObject(id); else this.current.queryObjects.delete(id);
            }
        };
        GLShadow.install({ [P.GLFN[name]]: method });
    }
    state.rule("BEGIN_QUERY", (c, [target, id]) => {
        if (!targets.has(target)) return GL.INVALID_ENUM;
        const query = c.queryObjects.get(id);
        return !id || c.activeQueryName || query?.active || query?.target && query.target !== target ? GL.INVALID_OPERATION : 0;
    });
    state.on("BEGIN_QUERY", (c, [target, id], s) => {
        Object.assign(s.queryObject(id), { target, created: true, active: true }); c.activeQueryName = id;
    });
    state.rule("END_QUERY", (c, [target]) => !targets.has(target) ? GL.INVALID_ENUM :
        !c.activeQueryName || c.queryObjects.get(c.activeQueryName).target !== target ? GL.INVALID_OPERATION : 0);
    state.on("END_QUERY", c => { c.queryObjects.get(c.activeQueryName).active = false; c.activeQueryName = 0; });
    state.parameter(P.C.V86GL_PARAMETER_QUERY_TARGET, (s, target, pname) => {
        if (!targets.has(target)) return GL.INVALID_ENUM;
        if (pname === GL.QUERY_COUNTER_BITS) return [[target === GL.SAMPLES_PASSED ? 31 : 1], "i"];
        if (pname === GL.CURRENT_QUERY) return [[s.current.queryObjects.get(s.current.activeQueryName)?.target === target ? s.current.activeQueryName : 0], "u"];
        return GL.INVALID_ENUM;
    });
    GLShadow.part({
        context(c) { c.queryObjects = new Map(); c.activeQueryName = 0; },
        checkCommand(s, fn, bytes, view, offset, size) {
            const single = fn === P.GLFN.QUERY_OBJECT_IV, batch = fn === P.GLFN.QUERY_OBJECT_BATCH;
            if (!single && !batch) return 0;
            const name = single ? "QUERY_OBJECT_IV" : "QUERY_OBJECT_BATCH", r = P.decodeRecord(name, view, offset);
            if (r.object_kind !== P.C.V86GL_OBJECT_KIND_QUERY) return 0;
            if (!s.pending(name, view, offset, size)) return 0;
            const fail = error => { if (!s.current.error) s.current.error = error;
                if (single) view.setUint32(offset + P.F.QUERY_OBJECT_IV.reserved, error, true);
                s.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED); return 0; };
            if (s.current.state.begin) return fail(GL.INVALID_OPERATION);
            const valid = id => !!s.current.queryObjects.get(id)?.created && !s.current.queryObjects.get(id).active;
            if (single) {
                if (!valid(r.name)) return fail(GL.INVALID_OPERATION);
                if (![GL.QUERY_RESULT, GL.QUERY_RESULT_AVAILABLE].includes(r.pname)) return fail(GL.INVALID_ENUM);
            } else {
                if (P.RECORDS[name].size + r.count * P.RECORDS.QUERY_OBJECT_BATCH_ENTRY.size > size) return fail(GL.INVALID_VALUE);
                for (let i = 0; i < r.count; ++i) if (!valid(view.getUint32(offset + P.RECORDS[name].size + i * P.RECORDS.QUERY_OBJECT_BATCH_ENTRY.size, true)))
                    return fail(GL.INVALID_OPERATION);
            }
            return 0;
        },
        getState(s, saved) { saved.queryObjects = [...s.contexts.values()].map(c => [c.key, [...c.queryObjects], c.activeQueryName]); },
        setState(s, saved) { for (const [key, queries, active] of saved.queryObjects || []) {
            const c = s.contexts.get(key); if (c) { c.queryObjects = new Map(queries); c.activeQueryName = active; }
        } },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Freeze VBO inputs when a draw is compiled, using the packed draw ABI. The
// list then owns only the referenced elements, independent of buffer names,
// later uploads, deletion and the caller's current client-array formats.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const scalarBytes = new Map([[GL.BYTE, 1], [GL.UNSIGNED_BYTE, 1], [GL.SHORT, 2], [GL.UNSIGNED_SHORT, 2],
        [GL.INT, 4], [GL.UNSIGNED_INT, 4], [GL.FLOAT, 4], [GL.DOUBLE, 8]]);
    const direct = new Set(["DRAW_ARRAYS_DIRECT", "DRAW_ELEMENTS_DIRECT", "DRAW_RANGE_ELEMENTS_DIRECT", "MULTI_DRAW_ARRAYS_DIRECT", "MULTI_DRAW_ELEMENTS_DIRECT"]);
    const packedNames = new Set(["DRAW_ARRAYS", "DRAW_ELEMENTS", "DRAW_ARRAYS_GL2", "DRAW_ELEMENTS_GL2"]);
    const indexSizes = new Map([[GL.UNSIGNED_BYTE, 1], [GL.UNSIGNED_SHORT, 2], [GL.UNSIGNED_INT, 4]]);
    const LIMIT = 64 * 1024 * 1024;
    function record(opcode, chunks) {
        const size = chunks.reduce((sum, chunk) => sum + chunk.length, 0), extended = size >= 0xFFFF;
        const out = new Uint8Array(size + (extended ? 8 : 4)), view = new DataView(out.buffer);
        view.setUint16(0, opcode, true); view.setUint16(2, extended ? 0xFFFF : size, true); if (extended) view.setUint32(4, size, true);
        let at = out.length - size; for (const chunk of chunks) { out.set(chunk, at); at += chunk.length; } return out;
    }
    function reject(fn, error) { return [record(P.rejectedOpcode(fn, error), [])]; }
    function packed(shadow, fn, mode, count, first, indexType, indexOffset) {
        const c = shadow.current, a = c.state.arrays;
        if (mode > GL.POLYGON) return reject(fn, GL.INVALID_ENUM);
        if (count < 0 || first < 0) return reject(fn, GL.INVALID_VALUE);
        const indexed = indexType !== null, indexBytes = indexed ? indexSizes.get(indexType) : 0;
        if (indexed && !indexBytes) return reject(fn, GL.INVALID_ENUM);
        if (!count) return [];
        if (count > LIMIT / 4) return reject(fn, GL.OUT_OF_MEMORY);
        const indices = indexed ? new Uint32Array(count) : null, elements = [], seen = new Map();
        if (indexed) {
            const buffer = shadow.boundBuffer(GL.ELEMENT_ARRAY_BUFFER);
            if (!buffer || buffer.mapped || indexOffset + count * indexBytes > buffer.size) return reject(fn, GL.INVALID_OPERATION);
            const view = new DataView(buffer.data.buffer, buffer.data.byteOffset, buffer.data.length);
            for (let i = 0; i < count; ++i) {
                const at = indexOffset + i * indexBytes, index = indexBytes === 1 ? view.getUint8(at) : indexBytes === 2 ? view.getUint16(at, true) : view.getUint32(at, true);
                if (!seen.has(index)) { seen.set(index, elements.length); elements.push(index); }
                indices[i] = seen.get(index);
            }
        } else for (let i = 0; i < count; ++i) elements.push(first + i);
        const arrays = [[a.vertex, c.state.raster.client[GL.VERTEX_ARRAY]], [a.color, c.state.raster.client[GL.COLOR_ARRAY]],
            [a.normal, c.state.raster.client[GL.NORMAL_ARRAY]], ...a.texCoord.map((array, i) => [array, c.state.raster.texcoord[i]]),
            [a.secondaryColor, c.state.raster.client[GL.SECONDARY_COLOR_ARRAY_EXT]], [a.fogCoord, c.state.raster.client[GL.FOG_COORDINATE_ARRAY_EXT]]];
        const generic = a.generic.map((array, index) => [array, array.enabled, index]).filter(([, enabled]) => enabled);
        const blocks = []; let total = indexed ? indices.byteLength : 0;
        for (const [array, enabled, index] of [...arrays, ...generic]) {
            const elementSize = array.size * (scalarBytes.get(array.type) || 0), dataSize = enabled ? elements.length * elementSize : 0;
            total += dataSize + (index === undefined ? 20 : 28);
            if (total > LIMIT) return reject(fn, GL.OUT_OF_MEMORY);
            const data = new Uint8Array(dataSize);
            if (enabled) {
                const buffer = c.group.bufferObjects.get(array.bufferId), stride = array.stride || elementSize;
                if (!buffer || buffer.mapped || !elementSize) return reject(fn, GL.INVALID_OPERATION);
                for (let i = 0; i < elements.length; ++i) {
                    const at = array.pointer + elements[i] * stride;
                    if (!Number.isSafeInteger(at) || at + elementSize > buffer.size) return reject(fn, GL.INVALID_OPERATION);
                    data.set(buffer.data.subarray(at, at + elementSize), i * elementSize);
                }
            }
            blocks.push(P.encodeRecord(index === undefined ? "CLIENT_ARRAY_BLOCK" : "GENERIC_ATTRIB_BLOCK",
                { index, normalized: +!!array.normalized, enabled: +!!enabled, size: array.size, type: array.type, stride: elementSize, data_size: dataSize }), data);
        }
        const gl2 = !!generic.length, name = indexed ? "DRAW_ELEMENTS" : "DRAW_ARRAYS", opcode = P.GLFN[name + (gl2 ? "_GL2" : "")];
        const header = P.encodeRecord("PACKED_" + name + (gl2 ? "_GL2_MT" : "_MT"), { mode, count, type: GL.UNSIGNED_INT,
            index_data_size: indexed ? indices.byteLength : 0, magic: P.C.CLIENT_ARRAY_MT_MAGIC,
            tex_unit_count: (a.texCoord.length | P.C.CLIENT_ARRAY_MT_SECONDARY_COLOR_BIT | P.C.CLIENT_ARRAY_MT_FOG_COORD_BIT) >>> 0,
            client_active_texture: c.state.clientTexture, generic_attrib_count: generic.length });
        return [record(opcode, [header, ...(indexed ? [new Uint8Array(indices.buffer)] : []), ...blocks])];
    }
    function capture(shadow, fn, bytes, view, offset, size) {
        const name = P.NAME_BY_OPCODE[fn];
        if (!direct.has(name) && !packedNames.has(name)) return null;
        const error = shadow.arrayInputError?.(fn); if (error) return reject(fn, error);
        if (!direct.has(name)) return null;
        const r = name === "DRAW_ARRAYS_DIRECT" ? { mode: view.getUint32(offset, true), first: view.getInt32(offset + 4, true), count: view.getInt32(offset + 8, true) } :
            P.decodeRecord(name, view, offset);
        if (name === "DRAW_RANGE_ELEMENTS_DIRECT" && r.end < r.start) return reject(fn, GL.INVALID_VALUE);
        if (!name.startsWith("MULTI_")) return packed(shadow, fn, r.mode, r.count, r.first || 0,
            name.includes("ELEMENTS") ? r.type : null, r.offset || 0);
        const header = P.RECORDS[name].size;
        if (r.primcount < 0 || r.primcount * 8 > size - header) return reject(fn, GL.INVALID_VALUE);
        if (r.mode > GL.POLYGON || name.includes("ELEMENTS") && !indexSizes.has(r.type)) return reject(fn, GL.INVALID_ENUM);
        const out = [];
        for (let i = 0; i < r.primcount; ++i) {
            const at = offset + header + i * 8, indexed = name === "MULTI_DRAW_ELEMENTS_DIRECT";
            out.push(...packed(shadow, fn, r.mode, view.getInt32(at + (indexed ? 0 : 4), true),
                indexed ? 0 : view.getInt32(at, true), indexed ? r.type : null, indexed ? view.getUint32(at + 4, true) : 0));
        }
        return out;
    }
    const api = { capture, record };
    global.GLListArrays = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// GL 2.1 section 5.4: compilation stores commands, nested calls keep their
// names, and client operations/queries execute while a list is being built.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const arrays = global.GLListArrays || require("./gl_list_arrays.js");
    const INDEX = 0x0B33, MODE = 0x0B30, NESTING = 0x0B31;
    const MAX_DEPTH = 64, MAX_EXPANSION = 64 * 1024 * 1024;
    const unpackFields = ["ALIGNMENT", "SWAP_BYTES", "LSB_FIRST", "ROW_LENGTH", "IMAGE_HEIGHT", "SKIP_PIXELS", "SKIP_ROWS", "SKIP_IMAGES"]
        .map(name => GL["UNPACK_" + name]);
    const pixelInputs = new Set(["BITMAP", "DRAW_PIXELS", "POLYGON_STIPPLE_BITMAP", "TEX_IMAGE_1D", "TEX_IMAGE_2D", "TEX_IMAGE_3D",
        "TEX_SUB_IMAGE_1D", "TEX_SUB_IMAGE_2D", "TEX_SUB_IMAGE_3D"]);
    const unpack = c => unpackFields.map(pname => c.state.values[pname][0]);
    function expandEntry(s, entry) {
        if (Array.isArray(entry)) return s.expand(Uint8Array.from(entry));
        const saved = unpack(s.current), commands = [];
        const apply = values => {
            for (let i = 0; i < unpackFields.length; ++i) if (s.current.state.values[unpackFields[i]][0] !== values[i]) commands.push(...s.expand(arrays.record(P.GLFN.PIXEL_STOREI,
                [P.encodeArgs("PIXEL_STOREI", [unpackFields[i], values[i]])])));
        };
        apply(entry.unpack);
        try { commands.push(...s.expand(Uint8Array.from(entry.data))); } finally { apply(saved); }
        return commands;
    }
    const immediateNames = new Set(("PIXEL_STOREI PIXEL_STOREF READ_PIXELS FINISH FLUSH CLIENT_ACTIVE_TEXTURE " +
        "ENABLE_CLIENT_STATE DISABLE_CLIENT_STATE ENABLE_VERTEX_ATTRIB_ARRAY DISABLE_VERTEX_ATTRIB_ARRAY PUSH_CLIENT_ATTRIB POP_CLIENT_ATTRIB " +
        "GEN_TEXTURES DELETE_TEXTURES GEN_BUFFERS DELETE_BUFFERS BIND_BUFFER BUFFER_DATA BUFFER_SUB_DATA " +
        "GEN_QUERIES DELETE_QUERIES GEN_PROGRAMS_ARB DELETE_PROGRAMS_ARB " +
        "CREATE_SHADER CREATE_PROGRAM DELETE_SHADER DELETE_PROGRAM ATTACH_SHADER DETACH_SHADER BIND_ATTRIB_LOCATION COMPILE_SHADER SHADER_SOURCE LINK_PROGRAM VALIDATE_PROGRAM " +
        "MAP_UNIFORM_LOCATION MAP_ATTRIB_LOCATION INVALIDATE_PROGRAM_LOCATIONS CHECK_FRAMEBUFFER_STATUS GENERATE_MIPMAP").split(" "));
    function immediate(fn, bytes, view, offset) {
        const name = P.NAME_BY_OPCODE[fn] || "";
        if (fn >= 0xFF00 || name.startsWith("QUERY_") || immediateNames.has(name) || name.endsWith("POINTER_VBO") ||
            name.includes("FRAMEBUFFER") || name.includes("RENDERBUFFER")) return true;
        if (["TEX_IMAGE_1D", "TEX_IMAGE_2D", "TEX_IMAGE_3D", "COMPRESSED_TEX_IMAGE_1D", "COMPRESSED_TEX_IMAGE_2D", "COMPRESSED_TEX_IMAGE_3D"].includes(name))
            return [GL.PROXY_TEXTURE_1D, GL.PROXY_TEXTURE_2D, GL.PROXY_TEXTURE_3D, GL.PROXY_TEXTURE_CUBE_MAP].includes(view.getUint32(offset, true));
        return false;
    }
    const exists = (g, name) => !!name && (g.displayLists.has(name) || g.listRanges.some(([start, end]) => name >= start && name < end));
    const error = (c, value) => { if (c && !c.error) c.error = value; };
    function allocate(g, range) {
        if (!range) return 0;
        const intervals = [...g.listRanges, ...[...g.displayLists.keys()].map(name => [name, name + 1])].sort((a, b) => a[0] - b[0]);
        let first = 1;
        for (const [start, end] of intervals) {
            if (first + range <= start) break;
            if (first < end) first = end;
        }
        if (first + range > 0x100000000) return 0;
        g.listRanges.push([first, first + range]); return first;
    }
    function remove(g, name, range) {
        const end = Math.min(0x100000000, name + range), intervals = [];
        for (const [start, last] of g.listRanges) {
            if (last <= name || start >= end) intervals.push([start, last]);
            else { if (start < name) intervals.push([start, name]); if (last > end) intervals.push([end, last]); }
        }
        g.listRanges = intervals;
        for (const id of g.displayLists.keys()) if (id >= name && id < end) g.displayLists.delete(id);
    }
    GLShadow.prototype.queryList = function(bytes, view, offset, size) {
        const name = "QUERY_LIST", fields = P.F[name]; if (!this.pending(name, view, offset, size)) return;
        const r = P.decodeRecord(name, view, offset), c = this.current;
        const failure = !c || c.state.begin ? GL.INVALID_OPERATION : r.operation > 2 ? GL.INVALID_ENUM :
            r.operation !== 1 && r.range < 0 ? GL.INVALID_VALUE : 0;
        if (failure) {
            error(c, failure); view.setUint32(offset + fields.error, failure, true);
            return this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
        }
        const result = r.operation === 0 ? allocate(c.group, r.range) : r.operation === 1 ? +exists(c.group, r.name) : 0;
        if (r.operation === 2 && r.range) remove(c.group, r.name, r.range);
        view.setUint32(offset + fields.result, result, true); view.setUint32(offset + fields.error, 0, true);
        this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [P.GLFN.QUERY_LIST]: "queryList" });
    GLShadow.prototype.captureListFailure = function(fn, value) {
        const c = this.current;
        if (!c?.listCompile || c.listExecution) return false;
        c.listCompile.entries.push(Array.from(arrays.record(P.rejectedOpcode(fn, value), [])));
        if (c.listCompile.mode === GL.COMPILE_AND_EXECUTE) error(c, value);
        return true;
    };
    function execute(s, name) {
        const c = s.current;
        if (!name) { error(c, GL.INVALID_VALUE); return []; }
        const entries = c.group.displayLists.get(name); if (!entries) return [];
        const outer = !c.listExecution;
        if (outer) c.listExecution = { depth: 0, bytes: 0 };
        const execution = c.listExecution;
        if (execution.depth >= MAX_DEPTH) return [];
        ++execution.depth; const commands = [];
        try {
            for (const entry of entries) {
                execution.bytes += Array.isArray(entry) ? entry.length : entry.data.length;
                if (execution.bytes > MAX_EXPANSION) { error(c, GL.OUT_OF_MEMORY); break; }
                commands.push(...expandEntry(s, entry));
            }
        } finally { --execution.depth; if (outer) c.listExecution = null; }
        return commands;
    }
    state.query([INDEX], s => [[s.current.listCompile?.name || 0], "i"]);
    state.query([MODE], s => [[s.current.listCompile?.mode || 0], "e"]);
    state.query([NESTING], () => [[MAX_DEPTH], "i"]);
    state.define("LIST_BASE", [0], "u", 0x20000);
    GLShadow.part({
        group(g) { g.displayLists = new Map(); g.listRanges = []; },
        routeRecord(s, fn, bytes, view, offset, size) {
            const c = s.current; if (!c) return;
            const control = fn === P.GLFN.LIST_COMMAND, calls = fn === P.GLFN.LIST_CALLS;
            const r = control ? P.decodeRecord("LIST_COMMAND", view, offset) : null;
            if (control && (r.operation === 1 || r.operation === 2)) {
                if (c.state.begin || c.listCompile?.begin) return { error: GL.INVALID_OPERATION };
                if (r.operation === 1) {
                    if (c.listCompile) return { error: GL.INVALID_OPERATION };
                    if (!r.name) return { error: GL.INVALID_VALUE };
                    if (![GL.COMPILE, GL.COMPILE_AND_EXECUTE].includes(r.value)) return { error: GL.INVALID_ENUM };
                    c.listCompile = { name: r.name, mode: r.value, entries: [], begin: false };
                } else {
                    if (!c.listCompile) return { error: GL.INVALID_OPERATION };
                    c.group.displayLists.set(c.listCompile.name, c.listCompile.entries); c.listCompile = null;
                }
                return { commands: [] };
            }
            if (c.listCompile && !c.listExecution && !immediate(fn, bytes, view, offset)) {
                const records = arrays.capture(s, fn, bytes, view, offset, size) ||
                    [arrays.record(fn, [bytes.subarray(offset, offset + size)])];
                const entries = records.map(copy => pixelInputs.has(P.NAME_BY_OPCODE[fn]) ? { data: Array.from(copy), unpack: unpack(c) } : Array.from(copy));
                c.listCompile.entries.push(...entries);
                if (fn === P.GLFN.BEGIN) c.listCompile.begin = true;
                if (fn === P.GLFN.END) c.listCompile.begin = false;
                if (c.listCompile.mode === GL.COMPILE) return { commands: [] };
                const execution = c.listExecution; c.listExecution = { depth: 0, bytes: 0 };
                try { return { commands: entries.flatMap(entry => expandEntry(s, entry)) }; } finally { c.listExecution = execution; }
            }
            if (control) {
                if (r.operation === 3) return { commands: execute(s, r.name) };
                if (r.operation === 4) {
                    if (c.state.begin) return { error: GL.INVALID_OPERATION };
                    state.set(c, GL.LIST_BASE, [r.name]); return { commands: [] };
                }
                return { error: GL.INVALID_ENUM };
            }
            if (calls) {
                const r = P.decodeRecord("LIST_CALLS", view, offset), header = P.RECORDS.LIST_CALLS.size;
                if (r.data_size !== r.count * 4 || r.data_size > size - header) return { error: GL.INVALID_VALUE };
                const commands = [];
                for (let i = 0; i < r.count; ++i) commands.push(...execute(s,
                    (view.getUint32(offset + header + i * 4, true) + c.state.values[GL.LIST_BASE][0]) >>> 0));
                return { commands };
            }
        },
        getState(s, saved) {
            saved.lists = [...s.groups.values()].map(g => [g.key, [...g.displayLists], g.listRanges]);
            saved.listCompilers = [...s.contexts.values()].map(c => [c.key, c.listCompile || null]);
        },
        setState(s, saved) {
            for (const [key, entries, ranges] of saved.lists || []) { const g = s.groups.get(key); if (g) { g.displayLists = new Map(entries); g.listRanges = ranges; } }
            for (const [key, compiler] of saved.listCompilers || []) { const c = s.contexts.get(key); if (c) c.listCompile = compiler; }
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// EXT_stencil_two_side with GL 2.0 has three state sets: shared front,
// core back and EXT back. EXT's active face selects ordinary setters and
// queries even while the extension is disabled (EXT spec issues 16/17).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js"), { GL } = global.GLWGConstants || require("./gl_constants.js"),
        { GLShadow } = global.V86GLShadow || require("./gl_shadow.js"), state = global.V86GLShadowState || require("./gl_shadow_state.js"),
        { stencilBits } = global.V86GLShadowRaster || require("./gl_shadow_raster.js"), arrays = global.GLListArrays || require("./gl_list_arrays.js");
    // This part must follow the display-list router: compile-only commands
    // must be stored before they can change any of the three state sets.
    if (typeof require === "function") require("./gl_shadow_lists.js");
    const fields = [GL.STENCIL_FUNC, GL.STENCIL_REF, GL.STENCIL_VALUE_MASK, GL.STENCIL_WRITEMASK,
        GL.STENCIL_FAIL, GL.STENCIL_PASS_DEPTH_FAIL, GL.STENCIL_PASS_DEPTH_PASS];
    const backs = [GL.STENCIL_BACK_FUNC, GL.STENCIL_BACK_REF, GL.STENCIL_BACK_VALUE_MASK, GL.STENCIL_BACK_WRITEMASK,
        GL.STENCIL_BACK_FAIL, GL.STENCIL_BACK_PASS_DEPTH_FAIL, GL.STENCIL_BACK_PASS_DEPTH_PASS];
    const initial = () => Object.fromEntries(fields.map((p, i) => [p, [GL.ALWAYS, 0, 0xFFFFFFFF, 0xFFFFFFFF, GL.KEEP, GL.KEEP, GL.KEEP][i]]));
    const activeBack = c => c.state.values[GL.ACTIVE_STENCIL_FACE_EXT][0] === GL.BACK;
    const enabled = c => !!c.state.raster.enabled[GL.STENCIL_TEST_TWO_SIDE_EXT];
    state.define("ACTIVE_STENCIL_FACE_EXT", [GL.FRONT], "i", 0x400);
    state.rule("ACTIVE_STENCIL_FACE", (c, [face]) => [GL.FRONT, GL.BACK].includes(face) ? 0 : GL.INVALID_ENUM);
    state.on("ACTIVE_STENCIL_FACE", (c, [face]) => state.set(c, GL.ACTIVE_STENCIL_FACE_EXT, [face]));
    function ordinary(c, indices, values) {
        for (let j = 0; j < indices.length; ++j) {
            const i = indices[j], value = values[j];
            if (activeBack(c)) c.state.stencilExtBack[fields[i]] = value;
            else { state.set(c, fields[i], [value]); state.set(c, backs[i], [value]); }
        }
    }
    state.on("STENCIL_FUNC", (c, a) => ordinary(c, [0, 1, 2], a));
    state.on("STENCIL_MASK", (c, a) => ordinary(c, [3], a));
    state.on("STENCIL_OP", (c, a) => ordinary(c, [4, 5, 6], a));
    for (const [i, pname] of fields.entries()) state.query([pname], shadow => {
        const c = shadow.current;
        let value = activeBack(c) ? c.state.stencilExtBack[pname] : c.state.values[pname][0];
        if (pname === GL.STENCIL_REF) value = Math.max(0, Math.min(2 ** stencilBits(shadow) - 1, value));
        return [[value], i === 2 || i === 3 ? "u" : "i"];
    });
    state.attributes("stencilExtBack", {
        capture(c, mask, client) { return !client && mask & 0x400 ? { values: { ...c.state.stencilExtBack } } : {}; },
        restore(c, saved) { if (saved.values) c.state.stencilExtBack = saved.values; },
    });
    const families = new Map([
        [P.GLFN.STENCIL_FUNC, [0, 1, 2]], [P.GLFN.STENCIL_FUNC_SEPARATE, [0, 1, 2]],
        [P.GLFN.STENCIL_MASK, [3]], [P.GLFN.STENCIL_MASK_SEPARATE, [3]],
        [P.GLFN.STENCIL_OP, [4, 5, 6]], [P.GLFN.STENCIL_OP_SEPARATE, [4, 5, 6]],
    ]);
    function resolved(c, indices) {
        const commands = [], ext = enabled(c);
        for (const face of [GL.FRONT, GL.BACK]) {
            const values = fields.map((p, i) => face === GL.BACK && ext ? c.state.stencilExtBack[p] : c.state.values[face === GL.BACK ? backs[i] : p][0]);
            const name = indices[0] === 0 ? "STENCIL_FUNC_SEPARATE" : indices[0] === 3 ? "STENCIL_MASK_SEPARATE" : "STENCIL_OP_SEPARATE";
            commands.push(arrays.record(P.GLFN[name], [P.encodeArgs(name, [face, ...indices.map(i => values[i])])]));
        }
        return commands;
    }
    GLShadow.part({
        context(c) { c.state.stencilExtBack = initial(); },
        setState(s) {
            for (const c of s.contexts.values()) if (!c.state.stencilExtBack) {
                c.state.stencilExtBack = enabled(c) ? Object.fromEntries(fields.map((p, i) => [p, c.state.values[backs[i]][0]])) : initial();
            }
        },
        routeRecord(s, fn, bytes, view, offset, size) {
            const c = s.current; if (!c) return;
            const indices = families.get(fn), face = fn === P.GLFN.ACTIVE_STENCIL_FACE,
                toggle = (fn === P.GLFN.ENABLE || fn === P.GLFN.DISABLE) && view.getUint32(offset, true) === GL.STENCIL_TEST_TWO_SIDE_EXT,
                pop = fn === P.GLFN.POP_ATTRIB;
            if (!indices && !face && !toggle && !pop) return;
            const failure = s.dispatch(fn, bytes, view, offset, size); if (failure) return { error: failure };
            if (face) return { commands: [] };
            if (indices) return { commands: resolved(c, indices) };
            return { commands: [arrays.record(fn, [bytes.subarray(offset, offset + size)]),
                ...[[0, 1, 2], [3], [4, 5, 6]].flatMap(group => resolved(c, group))] };
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// Final render-command validation lives beside the CPU state, so errors
// never depend on the page processing a later batch (plan C3/C6/C7).
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js"), { GL } = global.GLWGConstants || require("./gl_constants.js"),
        { GLShadow } = global.V86GLShadow || require("./gl_shadow.js"), state = global.V86GLShadowState || require("./gl_shadow_state.js"),
        pixels = global.V86GLShadowTextures || require("./gl_shadow_textures.js"), client = global.GLClientArrays || require("./gl_client_arrays.js"),
        { enabledArrays } = global.V86GLShadowArrays || require("./gl_shadow_arrays.js");
    if (typeof require === "function") { require("./gl_shadow_objects.js"); require("./gl_shadow_arrays.js"); require("./gl_shadow_lists.js"); }
    const primitive = mode => mode <= GL.POLYGON, indexBytes = type => type === GL.UNSIGNED_BYTE ? 1 : type === GL.UNSIGNED_SHORT ? 2 : type === GL.UNSIGNED_INT ? 4 : 0;
    const bits = GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT | GL.STENCIL_BUFFER_BIT;
    function attachment(s, target, kind) {
        const c = s.current, name = target === GL.READ_FRAMEBUFFER ? c.readFramebuffer : c.drawFramebuffer;
        if (!name) return { internalFormat: GL.DEPTH24_STENCIL8, componentBits: [0, 0, 0, 0, 24, 8] };
        const fbo = s.boundFramebuffer(target);
        return fbo?.[kind] ? s.attachedImage(fbo[kind]) : null;
    }
    function attachmentFormat(image, kind) {
        const format = image.internalFormat;
        if (kind === "stencil") return String(image.componentBits?.[5] || 8);
        const bits = image.componentBits?.[4] || (format === GL.DEPTH_COMPONENT16 ? 16 : format === GL.DEPTH24_STENCIL8 ? 24 : 32);
        return bits + ":" + (format === GL.DEPTH_COMPONENT32F || format === 0x8CAD ? "float" : "fixed");
    }
    function sourceError(s, format) {
        if (format === GL.DEPTH_COMPONENT || format === GL.STENCIL_INDEX)
            return attachment(s, GL.READ_FRAMEBUFFER, format === GL.DEPTH_COMPONENT ? "depth" : "stencil") ? 0 : GL.INVALID_OPERATION;
        return s.current.readBuffer === GL.NONE ? GL.INVALID_OPERATION : 0;
    }
    state.rule("CLEAR", (c, [mask]) => mask & ~(bits | GL.ACCUM_BUFFER_BIT) ? GL.INVALID_VALUE : 0);
    state.rule("COPY_PIXELS", (c, [x, y, width, height, type], s) => width < 0 || height < 0 ? GL.INVALID_VALUE :
        ![GL.COLOR, GL.DEPTH, GL.STENCIL].includes(type) ? GL.INVALID_ENUM :
            sourceError(s, type === GL.DEPTH ? GL.DEPTH_COMPONENT : type === GL.STENCIL ? GL.STENCIL_INDEX : GL.RGBA) ||
            (type === GL.STENCIL && !attachment(s, GL.DRAW_FRAMEBUFFER, "stencil") ? GL.INVALID_OPERATION : 0));
    state.rule("ACCUM", (c, [op], s) => op < 0x100 || op > 0x104 ? GL.INVALID_ENUM :
        c.drawFramebuffer !== c.readFramebuffer || !s.stateValue(GL.ACCUM_RED_BITS)?.[0][0] ? GL.INVALID_OPERATION : 0);
    state.rule("BLIT_FRAMEBUFFER", (c, a, s) => {
        if (a[8] & ~bits) return GL.INVALID_VALUE;
        if (![GL.NEAREST, GL.LINEAR].includes(a[9])) return GL.INVALID_ENUM;
        if (a[8] & (GL.DEPTH_BUFFER_BIT | GL.STENCIL_BUFFER_BIT) && a[9] !== GL.NEAREST) return GL.INVALID_OPERATION;
        for (const [bit, kind] of [[GL.DEPTH_BUFFER_BIT, "depth"], [GL.STENCIL_BUFFER_BIT, "stencil"]]) if (a[8] & bit) {
            const read = attachment(s, GL.READ_FRAMEBUFFER, kind), draw = attachment(s, GL.DRAW_FRAMEBUFFER, kind);
            // EXT_framebuffer_blit ignores an absent buffer. Two present
            // buffers must have exactly matching GL storage formats.
            if (read && draw && attachmentFormat(read, kind) !== attachmentFormat(draw, kind)) return GL.INVALID_OPERATION;
        }
        return 0;
    });
    const packedNames = ["DRAW_ARRAYS", "DRAW_ELEMENTS", "DRAW_ARRAYS_GL2", "DRAW_ELEMENTS_GL2"],
        directNames = ["DRAW_ARRAYS_DIRECT", "DRAW_ELEMENTS_DIRECT", "DRAW_RANGE_ELEMENTS_DIRECT"],
        multiNames = ["MULTI_DRAW_ARRAYS_DIRECT", "MULTI_DRAW_ELEMENTS_DIRECT"];
    function indexError(c, type, offset, count) {
        const bytes = indexBytes(type); if (!bytes) return GL.INVALID_ENUM;
        const buffer = c.group.bufferObjects.get(c.bufferBindings.get(GL.ELEMENT_ARRAY_BUFFER));
        if (!count) return buffer?.mapped ? GL.INVALID_OPERATION : 0;
        return !buffer || buffer.mapped || offset + count * bytes > (buffer.data?.length || 0) ? GL.INVALID_OPERATION : 0;
    }
    function direct(c, name, r) {
        if (!primitive(r.mode)) return GL.INVALID_ENUM;
        if (r.count < 0 || name === "DRAW_RANGE_ELEMENTS_DIRECT" && r.end < r.start) return GL.INVALID_VALUE;
        const indexed = name.includes("ELEMENTS"), error = indexed && indexError(c, r.type, r.offset, r.count);
        if (error || !r.count) return error || 0;
        let first = r.first, last = first + r.count - 1;
        if (indexed) {
            const data = c.group.bufferObjects.get(c.bufferBindings.get(GL.ELEMENT_ARRAY_BUFFER)).data,
                indices = new DataView(data.buffer, data.byteOffset + r.offset, data.length - r.offset), step = indexBytes(r.type);
            first = 0xFFFFFFFF; last = 0;
            for (let i = 0; i < r.count; ++i) {
                const value = step === 1 ? indices.getUint8(i) : step === 2 ? indices.getUint16(i * 2, true) : indices.getUint32(i * 4, true);
                first = Math.min(first, value); last = Math.max(last, value);
            }
        }
        for (const array of enabledArrays(c)) {
            const buffer = c.group.bufferObjects.get(array.bufferId), bytes = client.componentBytes(array.type),
                span = array.pointer + last * (array.stride || array.size * bytes) + array.size * bytes;
            if (!buffer || first < 0 || !Number.isSafeInteger(span) || span > buffer.size) return GL.INVALID_OPERATION;
        }
        return 0;
    }
    for (const name of directNames) GLShadow.rules({ [P.GLFN[name]]: (s, bytes, view, offset, size) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const r = name === "DRAW_ARRAYS_DIRECT" ? Object.fromEntries(["mode", "first", "count"].map((key, i) => [key, s.args(name, view, offset, size)[i]])) : P.decodeRecord(name, view, offset);
        return direct(s.current, name, r);
    } });
    for (const name of multiNames) GLShadow.rules({ [P.GLFN[name]]: (s, bytes, view, offset, size) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const r = P.decodeRecord(name, view, offset), indexed = name.includes("ELEMENTS"), header = P.RECORDS[name].size,
            entry = indexed ? "MULTI_DRAW_ELEMENTS_ENTRY" : "MULTI_DRAW_ARRAYS_ENTRY", stride = P.RECORDS[entry].size;
        if (!primitive(r.mode) || indexed && !indexBytes(r.type)) return GL.INVALID_ENUM;
        if (r.primcount < 0 || header + r.primcount * stride > size) return GL.INVALID_VALUE;
        for (let i = 0; i < r.primcount; ++i) {
            const value = P.decodeRecord(entry, view, offset + header + i * stride);
            if (!value.count) continue;
            const error = direct(s.current, indexed ? "DRAW_ELEMENTS_DIRECT" : "DRAW_ARRAYS_DIRECT", { ...r, ...value });
            if (error) return error;
        }
        return 0;
    } });
    function packed(name, bytes, view, offset, size) {
        const header = P.decodeRecord(name.includes("ELEMENTS") ? "DRAW_ELEMENTS" : "DRAW_ARRAYS", view, offset);
        if (!primitive(header.mode)) return GL.INVALID_ENUM;
        if (header.count < 0) return GL.INVALID_VALUE;
        if (name.includes("ELEMENTS") && !indexBytes(header.type)) return GL.INVALID_ENUM;
        if (!header.count) return 0;
        let packet;
        try { packet = client.decode(name, bytes, view, offset, size); } catch (error) { return GL.INVALID_OPERATION; }
        let max = packet.count - 1;
        if (packet.indices) {
            const step = indexBytes(packet.indices.type), data = packet.indices.data;
            if (packet.count * step > data.length) return GL.INVALID_OPERATION;
            const indices = new DataView(data.buffer, data.byteOffset, data.length); max = 0;
            for (let i = 0; i < packet.count; ++i) max = Math.max(max, step === 1 ? indices.getUint8(i) : step === 2 ? indices.getUint16(i * 2, true) : indices.getUint32(i * 4, true));
        }
        const validTypes = [GL.BYTE, GL.UNSIGNED_BYTE, GL.SHORT, GL.UNSIGNED_SHORT, GL.INT, GL.UNSIGNED_INT, GL.FLOAT, GL.DOUBLE];
        for (const [key, array] of Object.entries(packet.arrays)) {
            if (key.startsWith("generic") && +key.slice(7) >= 16 || array.size < 1 || array.size > 4 || array.stride < 0) return GL.INVALID_VALUE;
            if (!validTypes.includes(array.type)) return GL.INVALID_ENUM;
            const bytes = client.componentBytes(array.type), span = max * (array.stride || bytes * array.size) + bytes * array.size;
            if (!array.data || span > array.data.length) return GL.INVALID_OPERATION;
        }
        return 0;
    }
    for (const name of packedNames) GLShadow.rules({ [P.GLFN[name]]: (s, bytes, view, offset, size) =>
        s.current.state.begin ? GL.INVALID_OPERATION : packed(name, bytes, view, offset, size) });
    for (const name of ["READ_PIXELS", "DRAW_PIXELS"]) GLShadow.rules({ [P.GLFN[name]]: (s, bytes, view, offset, size) => {
        const r = P.decodeRecord(name, view, offset), read = name === "READ_PIXELS", c = s.current;
        const fail = error => { if (read) view.setUint32(offset + P.F[name].status, P.C.V86GL_SYNC_QUERY_STATUS_FAILED, true); return error; };
        if (c.state.begin) return fail(GL.INVALID_OPERATION);
        if (r.width < 0 || r.height < 0) return fail(GL.INVALID_VALUE);
        const error = pixels.pixelError(r.format, r.type, true); if (error) return fail(error);
        if (read && r.format === GL.COLOR_INDEX) return fail(GL.INVALID_OPERATION);
        if (read) { const error = sourceError(s, r.format); if (error) return fail(error); }
        if (!read && r.format === GL.STENCIL_INDEX && !attachment(s, GL.DRAW_FRAMEBUFFER, "stencil")) return fail(GL.INVALID_OPERATION);
        const span = pixels.uploadSpan(c, { ...r, format: r.format === GL.STENCIL_INDEX ? GL.COLOR_INDEX : r.format }, 2, read);
        if (!Number.isSafeInteger(span) || span > r.data_size || P.RECORDS[name].size + r.data_size > size) return fail(GL.INVALID_OPERATION);
        return 0;
    } });
    GLShadow.part({
        routeRecord(s, fn, bytes, view, offset, size) {
            const name = P.NAME_BY_OPCODE[fn];
            if (name?.startsWith("QUERY_") && name !== "QUERY_ERROR") {
                const encodingRead = name === "QUERY_BUFFER_DATA" && [GL.VERTEX_ARRAY,GL.NORMAL_ARRAY,GL.COLOR_ARRAY,GL.INDEX_ARRAY,
                    GL.EDGE_FLAG_ARRAY,GL.SECONDARY_COLOR_ARRAY_EXT,GL.FOG_COORDINATE_ARRAY_EXT,GL.TEXTURE_COORD_ARRAY,GL.VERTEX_ATTRIB_ARRAY_POINTER]
                    .includes(view.getUint32(offset + P.F.QUERY_BUFFER_DATA.target,true));
                let error = s.current?.state.begin && !encodingRead || !s.current && !["QUERY_INTEGER", "QUERY_GL_STRING"].includes(name) ? GL.INVALID_OPERATION : 0;
                if (["QUERY_OBJECT_IV", "QUERY_OBJECT_LOG", "QUERY_OBJECT_BATCH"].includes(name)) {
                    const kind = view.getUint32(offset + P.F[name].object_kind, true), kinds = name === "QUERY_OBJECT_BATCH" ? [P.C.V86GL_OBJECT_KIND_QUERY] :
                        name === "QUERY_OBJECT_LOG" ? [P.C.V86GL_OBJECT_KIND_SHADER, P.C.V86GL_OBJECT_KIND_PROGRAM, P.C.V86GL_TEXT_SHADER_SOURCE] : [P.C.V86GL_OBJECT_KIND_SHADER, P.C.V86GL_OBJECT_KIND_PROGRAM, P.C.V86GL_OBJECT_KIND_QUERY];
                    if (!kinds.includes(kind)) error ||= GL.INVALID_ENUM;
                }
                const selector = { QUERY_ACTIVE: "active_kind", QUERY_LOCATION: "location_kind", QUERY_UNIFORM: "value_kind" }[name];
                if (selector && ![1, 2].includes(view.getUint32(offset + P.F[name][selector], true))) error ||= GL.INVALID_ENUM;
                if (P.F[name].data_size !== undefined) {
                    const capacity = view.getUint32(offset + P.F[name].data_size, true);
                    if (P.RECORDS[name].size + capacity > size || P.F[name].buf_size !== undefined && view.getUint32(offset + P.F[name].buf_size, true) > capacity) error ||= GL.INVALID_VALUE;
                }
                if (error) {
                    if (P.F[name].error !== undefined) view.setUint32(offset + P.F[name].error, error, true);
                    s.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED); return { error };
                }
            }
            if (!s.current) return;
            if (name === "BLIT_FRAMEBUFFER" && !s.args(name, view, offset, size)[8]) {
                const error = s.dispatch(fn, bytes, view, offset, size); return error ? { error } : { commands: [] };
            }
            if (name !== "READ_PIXELS" && name !== "DRAW_PIXELS" && !packedNames.includes(name)) return;
            const r = P.decodeRecord(packedNames.includes(name) ? name.includes("ELEMENTS") ? "DRAW_ELEMENTS" : "DRAW_ARRAYS" : name, view, offset);
            if (packedNames.includes(name) ? r.count : r.width && r.height) return;
            const error = s.dispatch(fn, bytes, view, offset, size); if (error) return { error };
            if (name === "READ_PIXELS") s.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
            return { commands: [] };
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// CPU geometry for GL_SELECT/GL_FEEDBACK. Clipping happens in homogeneous
// coordinates, before division and viewport/depth-range conversion.
(function(global) {
    "use strict";
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const fixed = global.GLFixedVertex || require("./gl_shadow_rasterpos.js");
    const enabled = (c, cap) => !!c.state.raster.enabled[cap];
    const value = (c, pname) => c.state.values[pname];
    const dot = (a, b) => a.reduce((sum, n, i) => sum + n * b[i], 0);
    const clamp = n => Math.max(0, Math.min(1, n));
    function vertex(c, position, attributes) {
        let context = c;
        if (attributes) {
            const values = { ...c.state.values };
            for (const [pname, field] of [[GL.CURRENT_COLOR, "color"], [GL.CURRENT_NORMAL, "normal"]])
                if (attributes[field]) values[pname] = attributes[field];
            const units = c.state.fixed.units.map((u, i) => attributes["texCoord" + i] ? { ...u, coord: attributes["texCoord" + i] } : u);
            let materials = c.state.fixed.materials;
            if (attributes.color && enabled(c, GL.COLOR_MATERIAL)) {
                const face = value(c, GL.COLOR_MATERIAL_FACE)[0], parameter = value(c, GL.COLOR_MATERIAL_PARAMETER)[0];
                materials = materials.map((material, side) => face === GL.FRONT && side === 1 || face === GL.BACK && side === 0 ? material :
                    { ...material, ...Object.fromEntries((parameter === GL.AMBIENT_AND_DIFFUSE ? [GL.AMBIENT, GL.DIFFUSE] : [parameter]).map(p => [p, attributes.color.slice()])) });
            }
            context = { ...c, state: { ...c.state, values, fixed: { ...c.state.fixed, units, materials } } };
        }
        const eye = fixed.transform(context.state.stacks[GL.MODELVIEW].at(-1), position), clip = fixed.transform(context.state.stacks[GL.PROJECTION].at(-1), eye);
        const n = fixed.normal(context), lit = enabled(context, GL.LIGHTING);
        const color = lit ? fixed.lighting(context, eye, n)[0] : value(context, GL.CURRENT_COLOR).map(clamp);
        const backColor = lit && value(context, GL.LIGHT_MODEL_TWO_SIDE)[0] ? fixed.lighting(context, eye, n.map(v => -v), 1)[0] : color.slice();
        return { eye, clip, color, backColor, tex: fixed.coordinates(context, position, eye, n)[0], edge: attributes?.edgeFlag ?? !!value(context, GL.EDGE_FLAG)[0] };
    }
    function distances(c, v) {
        const p = v.clip, out = [p[3] + p[0], p[3] - p[0], p[3] + p[1], p[3] - p[1], p[3] + p[2], p[3] - p[2]];
        for (let i = 0; i < c.state.fixed.clip.length; ++i)
            if (enabled(c, GL.CLIP_PLANE0 + i)) out.push(dot(c.state.fixed.clip[i], v.eye));
        return out;
    }
    function mix(a, b, t) {
        return { ...Object.fromEntries(["eye", "clip", "color", "backColor", "tex"].map(key => [key, a[key].map((n, i) => n + (b[key][i] - n) * t)])), edge: a.edge };
    }
    function clip(c, vertices, kind) {
        if (kind === "point") return distances(c, vertices[0]).every(n => n >= 0) && vertices[0].clip[3] > 0 ? vertices : [];
        if (kind === "line") {
            const da = distances(c, vertices[0]), db = distances(c, vertices[1]); let lo = 0, hi = 1;
            for (let i = 0; i < da.length; ++i) {
                if (da[i] < 0 && db[i] < 0) return [];
                if (da[i] < 0) lo = Math.max(lo, da[i] / (da[i] - db[i]));
                if (db[i] < 0) hi = Math.min(hi, da[i] / (da[i] - db[i]));
                if (lo > hi) return [];
            }
            return [mix(vertices[0], vertices[1], lo), mix(vertices[0], vertices[1], hi)].filter(v => v.clip[3] > 0);
        }
        let polygon = vertices;
        for (let plane = 0, count = distances(c, vertices[0]).length; plane < count && polygon.length; ++plane) {
            const next = []; let previous = polygon.at(-1), before = distances(c, previous)[plane];
            for (const current of polygon) {
                const after = distances(c, current)[plane];
                if ((before < 0) !== (after < 0)) {
                    const crossing = mix(previous, current, before / (before - after));
                    if (before >= 0) crossing.edge = true;
                    next.push(crossing);
                }
                if (after >= 0) next.push(current);
                previous = current; before = after;
            }
            polygon = next;
        }
        return polygon.filter(v => v.clip[3] > 0);
    }
    function window(c, v) {
        const p = v.clip, viewport = value(c, GL.VIEWPORT), depth = value(c, GL.DEPTH_RANGE);
        return [viewport[0] + (p[0] / p[3] + 1) * viewport[2] / 2, viewport[1] + (p[1] / p[3] + 1) * viewport[3] / 2,
            depth[0] + (p[2] / p[3] + 1) * (depth[1] - depth[0]) / 2, p[3]].map(Math.fround);
    }
    function front(c, vertices) {
        let area = 0;
        for (let i = 0; i < vertices.length; ++i) {
            const a = vertices[i].clip, b = vertices[(i + 1) % vertices.length].clip;
            area += a[0] / a[3] * b[1] / b[3] - b[0] / b[3] * a[1] / a[3];
        }
        return value(c, GL.FRONT_FACE)[0] === GL.CCW ? area >= 0 : area <= 0;
    }
    function primitives(c, mode, input, emit) {
        function primitive(kind, vertices, reset = false) {
            if (value(c, GL.SHADE_MODEL)[0] === GL.FLAT && vertices.length > 1) {
                const provoking = mode === GL.POLYGON ? vertices[0] : vertices.at(-1);
                vertices = vertices.map(v => ({ ...v, color: provoking.color, backColor: provoking.backColor }));
            }
            const clipped = clip(c, vertices, kind);
            if (clipped.length < (kind === "polygon" ? 3 : kind === "line" ? 2 : 1)) return;
            const facing = kind !== "polygon" || front(c, clipped);
            if (kind === "polygon" && enabled(c, GL.CULL_FACE)) {
                const face = value(c, GL.CULL_FACE_MODE)[0];
                if (face === GL.FRONT_AND_BACK || (face === GL.FRONT) === facing) return;
            }
            emit({ kind, vertices: clipped, front: facing, reset });
        }
        const v = input, n = v.length;
        if (mode === GL.POINTS) for (const p of v) primitive("point", [p]);
        else if (mode === GL.LINES) for (let i = 0; i + 1 < n; i += 2) primitive("line", v.slice(i, i + 2), true);
        else if (mode === GL.LINE_STRIP || mode === GL.LINE_LOOP) {
            for (let i = 1; i < n; ++i) primitive("line", [v[i - 1], v[i]], i === 1);
            if (mode === GL.LINE_LOOP && n >= 2) primitive("line", [v[n - 1], v[0]], false);
        } else if (mode === GL.TRIANGLES) for (let i = 0; i + 2 < n; i += 3) primitive("polygon", v.slice(i, i + 3));
        else if (mode === GL.TRIANGLE_STRIP) for (let i = 2; i < n; ++i) primitive("polygon", i & 1 ? [v[i - 1], v[i - 2], v[i]] : v.slice(i - 2, i + 1));
        else if (mode === GL.TRIANGLE_FAN) for (let i = 2; i < n; ++i) primitive("polygon", [v[0], v[i - 1], v[i]]);
        else if (mode === GL.QUADS) for (let i = 0; i + 3 < n; i += 4) primitive("polygon", v.slice(i, i + 4));
        else if (mode === GL.QUAD_STRIP) for (let i = 2; i + 1 < n; i += 2) primitive("polygon", [v[i - 2], v[i - 1], v[i + 1], v[i]]);
        else if (mode === GL.POLYGON && n >= 3) primitive("polygon", v);
    }
    const api = { vertex, clip, window, primitives };
    global.GLSelectGeometry = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js");
    const { GL } = global.GLWGConstants || require("./gl_constants.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const state = global.V86GLShadowState || require("./gl_shadow_state.js");
    const geometry = global.GLSelectGeometry || require("./gl_select_geometry.js");
    const arrays = global.GLListArrays || require("./gl_list_arrays.js");
    const clientArrays = global.GLClientArrays || require("./gl_client_arrays.js");
    const caps = global.GLCaps || require("./gl_caps.js");
    if (typeof module !== "undefined" && module.exports) require("./gl_shadow_arrays.js");
    const CHUNK = 262144;
    const MAX_CAPTURE_VERTICES = 65536, MAX_PENDING_VERTICES = 262144;
    const maxNames = s => s.capabilities ? caps.integer(s.capabilities, GL.MAX_NAME_STACK_DEPTH) ?? caps.LIMITS.MAX_NAME_STACK_DEPTH : caps.LIMITS.MAX_NAME_STACK_DEPTH;
    const feedbackTypes = new Set([GL["2D"], GL["3D"], GL["3D_COLOR"], GL["3D_COLOR_TEXTURE"], GL["4D_COLOR_TEXTURE"]]);
    const initial = () => ({ mode: GL.RENDER, selectSize: 0, selectPointer: 0, feedbackSize: 0, feedbackPointer: 0, feedbackType: GL["2D"],
        names: [], hitNames: [], data: [], overflow: false, hits: 0, hit: null, primitive: null, pending: null, nextTicket: 0,
        events: [], retired: [], sequence: 0 });
    const modeOf = c => c.state.selection?.mode ?? GL.RENDER;
    function write(q, values, direct = false) {
        if (!direct && q.events?.length) { q.events.push({ kind: "words", values }); return; }
        const capacity = q.mode === GL.SELECT ? q.selectSize : q.feedbackSize;
        for (const value of values) if (q.data.length < capacity) q.data.push(value); else q.overflow = true;
    }
    function flushHit(q) {
        if (!q.hit) return;
        const names = q.hitNames || q.names;
        write(q, [names.length, Math.round(q.hit[0] * 0xFFFFFFFF), Math.round(q.hit[1] * 0xFFFFFFFF), ...names], true);
        ++q.hits; q.hit = null;
    }
    function hit(q, depths, direct = false) {
        if (!direct && q.events?.length) { q.events.push({ kind: "hit", depths }); return; }
        for (const z of depths) if (Number.isFinite(z)) {
            const value = Math.max(0, Math.min(1, z));
            if (!q.hit) q.hit = [value, value];
            else { q.hit[0] = Math.min(q.hit[0], value); q.hit[1] = Math.max(q.hit[1], value); }
        }
    }
    function feedbackVertex(q, position, color, tex) {
        const out = position.slice(0, q.feedbackType === GL["2D"] ? 2 : q.feedbackType === GL["4D_COLOR_TEXTURE"] ? 4 : 3);
        if ([GL["3D_COLOR"], GL["3D_COLOR_TEXTURE"], GL["4D_COLOR_TEXTURE"]].includes(q.feedbackType)) out.push(...color);
        if ([GL["3D_COLOR_TEXTURE"], GL["4D_COLOR_TEXTURE"]].includes(q.feedbackType)) out.push(...tex);
        return out.map(Math.fround);
    }
    function emit(c, primitive, q = c.state.selection, direct = false) {
        const vertices = primitive.vertices, points = vertices.map(v => geometry.window(c, v)), append = values => write(q, values, direct);
        if (q.mode === GL.SELECT) return hit(q, points.map(p => p[2]), direct);
        const vertex = i => feedbackVertex(q, points[i], primitive.front ? vertices[i].color : vertices[i].backColor, vertices[i].tex);
        if (primitive.kind === "point") return append([GL.POINT_TOKEN, ...vertex(0)]);
        if (primitive.kind === "line") return append([primitive.reset ? GL.LINE_RESET_TOKEN : GL.LINE_TOKEN, ...vertex(0), ...vertex(1)]);
        const polygonMode = c.state.values[GL.POLYGON_MODE][primitive.front ? 0 : 1];
        // Rotating a polygon preserves its winding. Start with its provoking
        // end vertex, matching the legacy native feedback packet ordering.
        const order = [vertices.length - 1, ...Array.from({ length: vertices.length - 1 }, (_, i) => i)];
        if (polygonMode === GL.POINT) for (const i of order) { if (vertices[i].edge) append([GL.POINT_TOKEN, ...vertex(i)]); }
        else if (polygonMode === GL.LINE) {
            let first = true;
            for (let i = 0; i < order.length; ++i) if (vertices[order[i]].edge) {
                append([first ? GL.LINE_RESET_TOKEN : GL.LINE_TOKEN, ...vertex(order[i]), ...vertex(order[(i + 1) % order.length])]); first = false;
            }
        }
        else append([GL.POLYGON_TOKEN, vertices.length, ...order.flatMap(vertex)]);
    }
    function finish(s, c, q, out) {
        if (out.mode === GL.SELECT) flushHit(out);
        const r = out.close.fields, ticket = out.data.length ? q.nextTicket = q.nextTicket % 0xFFFFFFFF + 1 : 0;
        if (out.close.sequence === q.sequence) q.pending = ticket ? { ticket, data: out.data, mode: out.mode } : null;
        Object.assign(r, { status: out.failure ? P.C.V86GL_SYNC_QUERY_STATUS_FAILED : P.C.V86GL_SYNC_QUERY_STATUS_OK, error: out.failure || 0,
            result: out.overflow ? -1 : out.mode === GL.SELECT ? out.hits : out.mode === GL.FEEDBACK ? out.data.length : 0,
            ticket, total: out.data.length * 4, size: c.id, pointer: out.mode === GL.SELECT ? out.selectPointer : out.mode === GL.FEEDBACK ? out.feedbackPointer : 0, type: out.mode });
        if (out.failure && !c.error) c.error = out.failure;
        s.completeResponse(out.close.target, P.encodeRecord("QUERY_RENDER_MODE", r), P.F.QUERY_RENDER_MODE.status); ++s.stats.answered;
    }
    function drain(s, c, q, out) {
        while (out.events.length) {
            const event = out.events[0]; if (event.kind === "capture" && !event.ready) break;
            out.events.shift();
            if (event.kind === "words") write(out, event.values, true);
            else if (event.kind === "hit") hit(out, event.depths, true);
            else if (event.kind === "names") { flushHit(out); out.hitNames = event.names; }
            else if (event.kind === "capture") {
                if (event.error) out.failure = event.error;
                else geometry.primitives(event.context, event.mode, event.vertices, primitive => emit(event.context, primitive, out, true));
            }
        }
        if (!out.events.length && out.close) {
            finish(s, c, q, out); q.retired = q.retired.filter(item => item !== out);
        }
    }
    function program(s) {
        const c = s.current, p = c.group.programs?.get(c.program);
        if (p) {
            const linked = s.installedProgram?.(p);
            if (p.executableInput?.shaders.some(shader => shader.type === GL.VERTEX_SHADER))
                return { attributes: linked?.attributes || [], sources: p.executableInput.shaders.map(shader => shader.compileSource).join("\n"), glsl: true };
            // An installed fragment-only GLSL program leaves the vertex stage fixed.
            return null;
        }
        if (c.state.raster.enabled[GL.VERTEX_PROGRAM_ARB]) {
            const p = s.arbProgram?.(GL.VERTEX_PROGRAM_ARB);
            if (p?.compiled?.ok) return { attributes: p.compiled.reflection.attributes, invariant: p.compiled.reflection.positionInvariant };
        }
        return null;
    }
    const conventional = { gl_Vertex: 0, gl_Normal: 2, gl_Color: 3, gl_SecondaryColor: 4, gl_FogCoord: 5,
        ...Object.fromEntries(Array.from({ length: 8 }, (_, i) => ["gl_MultiTexCoord" + i, 8 + i])) };
    function input(c, p, position, attributes = {}) {
        const values = c.state.values, slots = c.state.arrays.generic.map((a, i) => (attributes["generic" + i] || a.current).slice());
        const compat = { gl_Vertex: position, gl_Normal: attributes.normal || values[GL.CURRENT_NORMAL], gl_Color: attributes.color || values[GL.CURRENT_COLOR],
            gl_SecondaryColor: attributes.secondaryColor || values[GL.CURRENT_SECONDARY_COLOR_EXT], gl_FogCoord: attributes.fogCoord || values[GL.CURRENT_FOG_COORDINATE_EXT] };
        for (let i = 0; i < 8; ++i) compat["gl_MultiTexCoord" + i] = attributes["texCoord" + i] || c.state.fixed.units[i].coord;
        const used = new Set(p.attributes.map(a => a.name || a.field));
        if (/\b(?:gl_Vertex|ftransform)\b/.test(p.sources || "") || p.invariant) used.add("gl_Vertex");
        for (const name of used) if (conventional[name] !== undefined) {
            const slot = conventional[name], value = compat[name] || [0, 0, 0, 1];
            slots[slot] = attributes["generic" + slot] || Array.from({ length: 4 }, (_, i) => value[i] ?? (i === 3 ? 1 : 0));
        }
        // Conventional vertex submission also supplies generic attribute zero.
        slots[0] = position.slice();
        return slots.flat().map(Math.fround);
    }
    function captureContext(c, p) {
        const context = { state: { values: GLShadow.clone(c.state.values), raster: { enabled: GLShadow.clone(c.state.raster.enabled) }, fixed: { clip: GLShadow.clone(c.state.fixed.clip) } } };
        if (!p.glsl && !p.invariant) for (let i = 0; i < 6; ++i) context.state.raster.enabled[GL.CLIP_PLANE0 + i] = false;
        return context;
    }
    function rasterCommand(c) {
        return arrays.record(P.GLFN.RESOLVED_RASTER_ALL, [P.encodeRecord("RESOLVED_RASTER_ALL", {
            valid: c.state.values[GL.CURRENT_RASTER_POSITION_VALID][0], position: c.state.values[GL.CURRENT_RASTER_POSITION],
            color: c.state.values[GL.CURRENT_RASTER_COLOR], secondary: c.state.values[GL.CURRENT_RASTER_SECONDARY_COLOR],
            distance: c.state.values[GL.CURRENT_RASTER_DISTANCE][0], fog: c.state.rasterFogCoord, coords: c.state.rasterCoords.flat(),
        })]);
    }
    function captureRaster(s, bytes, view, offset, size, p) {
        const error = s.dispatch(P.GLFN.RASTER_POS4F, bytes, view, offset, size, true); if (error) return { error };
        if (s.nextSelectionJob >= 0xFFFFFFFF) return { error: GL.OUT_OF_MEMORY };
        const c = s.current, job = ++s.nextSelectionJob, attributes = input(c, p, s.args("RASTER_POS4F", view, offset, size));
        const event = { kind: "raster", context: captureContext(c, p), total: 1 };
        s.selectionJobs.set(job, { c, event }); s.blockCPU(job);
        return { commands: [arrays.record(P.GLFN.CAPTURE_RASTER_VERTEX, [P.encodeRecord("CAPTURE_RASTER_VERTEX", { job }), new Uint8Array(new Float32Array(attributes).buffer)])] };
    }
    function capture(s, mode, vertices, p) {
        const c = s.current, q = c.state.selection;
        if (!vertices.length) return [];
        let pending = vertices.length;
        for (const { event } of s.selectionJobs.values()) pending += event.total;
        if (vertices.length > MAX_CAPTURE_VERTICES || pending > MAX_PENDING_VERTICES || s.nextSelectionJob >= 0xFFFFFFFF) return null;
        const job = ++s.nextSelectionJob;
        const context = captureContext(c, p);
        const event = { kind: "capture", job, mode, context, total: vertices.length, received: 0, chunks: [], vertices: [], edges: vertices.map(v => v.edge), ready: false,
            twoSide: !!c.state.raster.enabled[GL.VERTEX_PROGRAM_TWO_SIDE] };
        q.events.push(event); s.selectionJobs.set(job, { c, q, out: q, event });
        const commands = [];
        for (let first = 0; first < vertices.length; first += 256) {
            const count = Math.min(256, vertices.length - first), data = new Float32Array(count * 64);
            for (let i = 0; i < count; ++i) data.set(vertices[first + i].input, i * 64);
            commands.push(arrays.record(P.GLFN.CAPTURE_VERTICES, [P.encodeRecord("CAPTURE_VERTICES", { job, first, count, total: vertices.length }), new Uint8Array(data.buffer)]));
        }
        return commands;
    }
    GLShadow.prototype.completeVertexCapture = function(message) {
        const saved = this.selectionJobs.get(message.job); if (!saved) return;
        const { c, q, out, event } = saved, first = message.first, values = message.values, count = values?.length / 20;
        if (event.kind === "raster") {
            if (this.cpuBarrier?.token !== message.job) { this.selectionJobs.delete(message.job); return; }
            if (!message.error && (!(values instanceof Float32Array) || values.length !== 56 || first !== 0)) return;
            this.selectionJobs.delete(message.job);
            const part = at => Array.from(values.subarray(at, at + 4));
            const vertex = !message.error && { clip: part(0), color: part(4), backColor: part(8), tex: part(12), eye: part(16) },
                valid = vertex && geometry.clip(event.context, [vertex], "point").length > 0;
            state.set(c, GL.CURRENT_RASTER_POSITION_VALID, [+!!valid]);
            if (message.error && !c.error) c.error = GL.INVALID_OPERATION;
            if (valid) {
                const position = geometry.window(event.context, vertex);
                state.set(c, GL.CURRENT_RASTER_POSITION, position); state.set(c, GL.CURRENT_RASTER_COLOR, vertex.color);
                state.set(c, GL.CURRENT_RASTER_SECONDARY_COLOR, part(20)); state.set(c, GL.CURRENT_RASTER_DISTANCE, [values[24]]);
                c.state.rasterFogCoord = values[24]; c.state.rasterCoords = [vertex.tex, ...Array.from({ length: 7 }, (_, i) => part(28 + i * 4))];
                if (c.state.selection.mode === GL.SELECT) hit(c.state.selection, [position[2]]);
            }
            this.resumeCPU(message.job, [rasterCommand(c)]); return;
        }
        if (!message.error && (!(values instanceof Float32Array) || !Number.isInteger(first) || first < 0 || first >= event.total || first % 256 ||
            !Number.isInteger(count) || count <= 0 || count !== Math.min(256, event.total - first) || event.chunks.includes(first))) return;
        if (message.error) { event.error = GL.INVALID_OPERATION; event.ready = true; }
        else {
            for (let i = 0; i < count; ++i) {
                const at = i * 20, part = offset => Array.from(values.subarray(at + offset, at + offset + 4));
                event.vertices[first + i] = { clip: part(0), color: part(4), backColor: part(event.twoSide ? 8 : 4), tex: part(12), eye: part(16), edge: event.edges[first + i] };
            }
            event.chunks.push(first); event.received += count; event.ready = event.received === event.total;
        }
        if (event.ready) { this.selectionJobs.delete(message.job); drain(this, c, q, out); }
    };
    GLShadow.prototype.evaluatedVertex = function(position, attributes) {
        const c = this.current, q = c.state.selection;
        if (!c.state.begin) return [];
        if (q.mode !== GL.RENDER) {
            const primitive = q.primitive; if (!primitive) return [];
            if (primitive.vertices.length >= (primitive.program ? MAX_CAPTURE_VERTICES : 1048576)) { if (!c.error) c.error = GL.OUT_OF_MEMORY; return []; }
            primitive.vertices.push(primitive.program ? { input: input(c, primitive.program, position, attributes), edge: !!c.state.values[GL.EDGE_FLAG][0] } : geometry.vertex(c, position, attributes));
            return [];
        }
        return [arrays.record(P.GLFN.RESOLVED_EVAL_VERTEX, [P.encodeRecord("RESOLVED_EVAL_VERTEX", { position, color: attributes.color, normal: [...attributes.normal, 0], texcoord: attributes.texCoord0 })])];
    };
    const drawNames = new Set(["DRAW_ARRAYS", "DRAW_ELEMENTS", "DRAW_ARRAYS_GL2", "DRAW_ELEMENTS_GL2", "DRAW_ARRAYS_DIRECT", "DRAW_ELEMENTS_DIRECT",
        "DRAW_RANGE_ELEMENTS_DIRECT", "MULTI_DRAW_ARRAYS_DIRECT", "MULTI_DRAW_ELEMENTS_DIRECT"]);
    function draw(s, name, bytes, view, offset, size) {
        const c = s.current, packet = clientArrays.decode(name, bytes, view, offset, size), a = packet.arrays, p = program(s);
        if (packet.mode > GL.POLYGON) return GL.INVALID_ENUM;
        if (packet.count < 0) return GL.INVALID_VALUE;
        if (packet.count > (p ? MAX_CAPTURE_VERTICES : 1048576)) return GL.OUT_OF_MEMORY;
        const indexed = packet.indices, indexBytes = indexed && new Map([[GL.UNSIGNED_BYTE, 1], [GL.UNSIGNED_SHORT, 2], [GL.UNSIGNED_INT, 4]]).get(indexed.type);
        if (indexed && !indexBytes) return GL.INVALID_ENUM;
        if (indexed && indexed.data.length < packet.count * indexBytes) return GL.INVALID_OPERATION;
        const indexView = indexed && new DataView(indexed.data.buffer, indexed.data.byteOffset, indexed.data.length), vertices = [];
        for (let i = 0; i < packet.count; ++i) {
            const index = !indexed ? i : indexBytes === 1 ? indexView.getUint8(i) : indexBytes === 2 ? indexView.getUint16(i * 2, true) : indexView.getUint32(i * 4, true);
            const position = clientArrays.attribute(a.vertex || a.generic0, index, 4); if (!position && !p) return GL.INVALID_OPERATION;
            const attributes = { color: clientArrays.attribute(a.color || a.generic3, index, 4, true), normal: clientArrays.attribute(a.normal || a.generic2, index, 3, true) };
            attributes.secondaryColor = clientArrays.attribute(a.secondaryColor, index, 4, true);
            attributes.fogCoord = clientArrays.attribute(a.fogCoord, index, 1);
            for (let slot = 0; slot < 16; ++slot) attributes["generic" + slot] = clientArrays.attribute(a["generic" + slot], index, 4);
            for (let unit = 0; unit < 8; ++unit) attributes["texCoord" + unit] = clientArrays.attribute(a["texCoord" + unit] || a["generic" + (8 + unit)], index, 4);
            vertices.push(p ? { input: input(c, p, position || c.state.arrays.generic[0].current, attributes), edge: !!c.state.values[GL.EDGE_FLAG][0] } : geometry.vertex(c, position, attributes));
        }
        if (p) return capture(s, packet.mode, vertices, p) || GL.OUT_OF_MEMORY;
        geometry.primitives(c, packet.mode, vertices, primitive => emit(c, primitive)); return 0;
    }
    GLShadow.prototype.queryRenderMode = function(bytes, view, offset, size) {
        const name = "QUERY_RENDER_MODE", f = P.F[name], header = P.RECORDS[name].size;
        if (!this.pending(name, view, offset, size)) return;
        const r = P.decodeRecord(name, view, offset), c = this.current, q = c?.state.selection;
        const fail = value => {
            if (c && !c.error) c.error = value;
            view.setUint32(offset + f.error, value, true); this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED);
        };
        if (!c || c.state.begin) return fail(GL.INVALID_OPERATION);
        if (r.operation > 3) return fail(GL.INVALID_ENUM);
        if (r.operation < 2) {
            if (r.size < 0) return fail(GL.INVALID_VALUE);
            if (q.mode === (r.operation === 0 ? GL.SELECT : GL.FEEDBACK)) return fail(GL.INVALID_OPERATION);
            if (r.operation === 1 && !feedbackTypes.has(r.type)) return fail(GL.INVALID_ENUM);
            if (r.operation === 0) { q.selectSize = r.size; q.selectPointer = r.pointer; }
            else { q.feedbackSize = r.size; q.feedbackPointer = r.pointer; q.feedbackType = r.type; }
        } else if (r.operation === 2) {
            if (![GL.RENDER, GL.SELECT, GL.FEEDBACK].includes(r.type)) return fail(GL.INVALID_ENUM);
            if (r.type === GL.SELECT && !q.selectSize || r.type === GL.FEEDBACK && !q.feedbackSize) return fail(GL.INVALID_OPERATION);
            const out = Object.fromEntries(["mode", "selectSize", "selectPointer", "feedbackSize", "feedbackPointer", "feedbackType", "data", "overflow", "hits", "hit", "hitNames", "events", "failure"].map(key => [key, q[key]]));
            out.close = { sequence: ++q.sequence, target: this.deferResponse(view, offset, header, !q.events.length), fields: r };
            for (const job of this.selectionJobs.values()) if (job.out === q) job.out = out;
            q.pending = null; q.mode = r.type; q.data = []; q.events = []; q.overflow = false; q.hits = 0; q.hit = null; q.failure = 0;
            if (r.type === GL.SELECT) q.names = [];
            q.hitNames = q.names.slice();
            q.retired.push(out); drain(this, c, q, out); return;
        } else {
            const saved = q.pending;
            if (!saved || r.ticket !== saved.ticket || (r.size >>> 0) !== c.id || r.offset % 4 || r.data_size % 4 || !r.data_size ||
                r.data_size > CHUNK || r.data_size > size - header || r.offset + r.data_size > saved.data.length * 4) return fail(GL.INVALID_OPERATION);
            for (let i = 0; i < r.data_size; i += 4) {
                const value = saved.data[(r.offset + i) / 4];
                if (saved.mode === GL.SELECT) view.setUint32(offset + header + i, value, true);
                else view.setFloat32(offset + header + i, value, true);
            }
            if (r.offset + r.data_size === saved.data.length * 4) q.pending = null;
        }
        view.setUint32(offset + f.error, 0, true); this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [P.GLFN.QUERY_RENDER_MODE]: "queryRenderMode" });
    GLShadow.rules({ [P.GLFN.NAME_COMMAND]: (s, b, v, o) => {
        if (s.current.state.begin) return GL.INVALID_OPERATION;
        const r = P.decodeRecord("NAME_COMMAND", v, o), q = s.current.state.selection;
        if (r.operation > 4) return GL.INVALID_ENUM;
        if (q.mode !== GL.SELECT || r.operation === 4) return 0;
        if (r.operation === 1 && !q.names.length) return GL.INVALID_OPERATION;
        if (r.operation === 2 && q.names.length >= maxNames(s)) return GL.STACK_OVERFLOW;
        return r.operation === 3 && !q.names.length ? GL.STACK_UNDERFLOW : 0;
    }, [P.GLFN.RESOLVED_RASTER_STATE]: () => GL.INVALID_OPERATION, [P.GLFN.CAPTURE_VERTICES]: () => GL.INVALID_OPERATION,
    [P.GLFN.CAPTURE_RASTER_VERTEX]: () => GL.INVALID_OPERATION, [P.GLFN.RESOLVED_RASTER_ALL]: () => GL.INVALID_OPERATION });
    state.query([GL.RENDER_MODE], s => [[modeOf(s.current)], "e"]);
    state.query([GL.MAX_NAME_STACK_DEPTH], s => [[maxNames(s)], "i"]);
    state.query([0x0D70], s => [[s.current.state.selection.names.length], "i"]);
    for (const [pname, key, kind] of [[0x0DF4, "selectSize", "i"], [0x0DF1, "feedbackSize", "i"], [0x0DF2, "feedbackType", "e"]])
        state.query([pname], s => [[s.current.state.selection[key]], kind]);
    // ARRAY_POINTER's selection/feedback entries were deliberately left open
    // until their buffer metadata moved together with RenderMode.
    state.parameter(P.C.V86GL_PARAMETER_ARRAY_POINTER, (s, target, pname, index) => {
        if (pname === 0x0DF3 || pname === 0x0DF0) return [[s.current.state.selection[pname === 0x0DF3 ? "selectPointer" : "feedbackPointer"]], "u"];
        return s.arrayPointerParameter?.(target, pname, index) ?? GL.INVALID_ENUM;
    });
    GLShadow.part({
        reset(s) { s.nextSelectionJob = 0; s.selectionJobs = new Map(); },
        cancelCPU(s, barrier) { if (barrier) s.selectionJobs.delete(barrier.token); },
        destroy(s, c) {
            for (const [id, job] of s.selectionJobs) if (job.c === c) s.selectionJobs.delete(id);
            for (const out of c.state.selection.retired) if (out.close?.target.localId) s.responseTargets.delete(out.close.target.localId);
        },
        context(c) { c.state.selection = initial(); },
        routeRecord(s, fn, bytes, view, offset, size) {
            const c = s.current; if (!c) return;
            if (fn === P.GLFN.NAME_COMMAND) return { error: s.dispatch(fn, bytes, view, offset, size) || 0, commands: [] };
            if (fn === P.GLFN.RASTER_POS4F || fn === P.GLFN.WINDOW_POS3F) {
                const p = fn === P.GLFN.RASTER_POS4F && program(s);
                if (p) return captureRaster(s, bytes, view, offset, size, p);
                const failure = s.dispatch(fn, bytes, view, offset, size); if (failure) return { error: failure };
                return { commands: [rasterCommand(c)] };
            }
            const q = c.state.selection; if (q.mode === GL.RENDER) return;
            const name = P.NAME_BY_OPCODE[fn], vertex = ["VERTEX2F", "VERTEX3F", "VERTEX4F"].includes(name),
                attribZero = name === "VERTEX_ATTRIB4F" && view.getUint32(offset, true) === 0 && c.state.begin;
            if (fn === P.GLFN.BEGIN || fn === P.GLFN.END || vertex || attribZero) {
                const failure = s.dispatch(fn, bytes, view, offset, size); if (failure) return { error: failure };
                if (fn === P.GLFN.BEGIN) q.primitive = { mode: view.getUint32(offset, true), vertices: [], program: program(s) };
                else if (fn === P.GLFN.END) {
                    const primitive = q.primitive; q.primitive = null;
                    if (primitive?.program) {
                        const commands = capture(s, primitive.mode, primitive.vertices, primitive.program);
                        return commands ? { commands } : { error: GL.OUT_OF_MEMORY };
                    }
                    if (primitive) geometry.primitives(c, primitive.mode, primitive.vertices, p => emit(c, p));
                } else if (q.primitive) {
                    if (q.primitive.vertices.length >= (q.primitive.program ? MAX_CAPTURE_VERTICES : 1048576)) return { error: GL.OUT_OF_MEMORY };
                    const position = s.args(name, view, offset, size).slice(attribZero ? 1 : 0);
                    while (position.length < 4) position.push(position.length === 3 ? 1 : 0);
                    q.primitive.vertices.push(q.primitive.program ? { input: input(c, q.primitive.program, position), edge: !!c.state.values[GL.EDGE_FLAG][0] } : geometry.vertex(c, position));
                }
                return { commands: attribZero ? [arrays.record(fn, [bytes.subarray(offset, offset + size)])] : [] };
            }
            if (drawNames.has(name)) {
                const failure = s.dispatch(fn, bytes, view, offset, size); if (failure) return { error: failure };
                try {
                    if (name.endsWith("DIRECT")) return { commands: (arrays.capture(s, fn, bytes, view, offset, size) || []).flatMap(record => s.expand(record)) };
                    const result = draw(s, name, bytes, view, offset, size);
                    return Array.isArray(result) ? { commands: result } : { error: result, commands: [] };
                } catch (error) { return { error: GL.INVALID_OPERATION }; }
            }
            if (["BITMAP", "DRAW_PIXELS", "COPY_PIXELS", "CLEAR", "ACCUM", "BLIT_FRAMEBUFFER"].includes(name)) {
                const position = c.state.values[GL.CURRENT_RASTER_POSITION]?.slice(), valid = c.state.values[GL.CURRENT_RASTER_POSITION_VALID]?.[0];
                const failure = s.dispatch(fn, bytes, view, offset, size); if (failure) return { error: failure };
                if (q.mode === GL.FEEDBACK && valid && ["BITMAP", "DRAW_PIXELS", "COPY_PIXELS"].includes(name))
                    write(q, [GL[name === "BITMAP" ? "BITMAP_TOKEN" : name === "DRAW_PIXELS" ? "DRAW_PIXEL_TOKEN" : "COPY_PIXEL_TOKEN"],
                        ...feedbackVertex(q, position, c.state.values[GL.CURRENT_RASTER_COLOR], c.state.rasterCoords[0])]);
                if (name === "BITMAP") {
                    const r = P.decodeRecord(name, view, offset);
                    return { commands: [arrays.record(fn, [P.encodeRecord("BITMAP", { xmove: r.xmove, ymove: r.ymove })])] };
                }
                return { commands: [] };
            }
        },
        command(s, fn, bytes, view, offset) {
            const c = s.current; if (!c) return;
            const q = c.state.selection;
            if (fn === P.GLFN.NAME_COMMAND) {
                const r = P.decodeRecord("NAME_COMMAND", view, offset);
                if (r.operation === 4) { if (q.mode === GL.FEEDBACK) write(q, [GL.PASS_THROUGH_TOKEN, r.value]); return; }
                if (q.mode !== GL.SELECT) return;
                if (r.operation === 0) q.names = [];
                if (r.operation === 1) q.names[q.names.length - 1] = r.name;
                if (r.operation === 2) q.names.push(r.name);
                if (r.operation === 3) q.names.pop();
                if (q.events.length) q.events.push({ kind: "names", names: q.names.slice() });
                else { flushHit(q); q.hitNames = q.names.slice(); }
            } else if (q.mode === GL.SELECT && [P.GLFN.RASTER_POS4F, P.GLFN.WINDOW_POS3F].includes(fn) && c.state.values[GL.CURRENT_RASTER_POSITION_VALID][0])
                hit(q, [c.state.values[GL.CURRENT_RASTER_POSITION][2]]);
        },
        getState(s, saved) { saved.nextSelectionJob = s.nextSelectionJob; },
        setState(s, saved) {
            s.nextSelectionJob = saved.nextSelectionJob || 0;
            for (const c of s.contexts.values()) {
                if (!c.state.selection) c.state.selection = initial();
                const q = c.state.selection;
                q.events ||= []; q.retired ||= []; q.hitNames ||= q.names.slice(); q.sequence ||= 0;
                for (const out of [q, ...q.retired]) for (const event of out.events) if (event.kind === "capture" && !event.ready)
                    s.selectionJobs.set(event.job, { c, q, out, event });
            }
        },
    });
    const api = { emit, write, modeOf, feedbackVertex, vertexProgram: program };
    global.GLSelection = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// GL 2.1 section 5.1. Coefficients and grid state belong to the CPU context;
// the page receives evaluated vertices without changes to current attributes.
(function(global) {
    "use strict";
    const P = global.GLProtocol || require("./gl_protocol.js"), { GL } = global.GLWGConstants || require("./gl_constants.js"),
        { GLShadow } = global.V86GLShadow || require("./gl_shadow.js"), state = global.V86GLShadowState || require("./gl_shadow_state.js"),
        caps = global.GLCaps || require("./gl_caps.js"), arrays = global.GLListArrays || require("./gl_list_arrays.js"), selection = global.GLSelection || require("./gl_shadow_select.js");
    const dimensions = target => target >= GL.MAP1_COLOR_4 && target <= GL.MAP1_VERTEX_4 ? 1 : target >= GL.MAP2_COLOR_4 && target <= GL.MAP2_VERTEX_4 ? 2 : 0;
    const index = target => target - (dimensions(target) === 2 ? GL.MAP2_COLOR_4 : GL.MAP1_COLOR_4);
    const components = target => [4, 1, 3, 1, 2, 3, 4, 3, 4][index(target)] || 0;
    const enabled = (c, target) => !!c.state.raster.enabled[target];
    const limit = s => (s.capabilities && caps.integer(s.capabilities, GL.MAX_EVAL_ORDER)) ?? caps.LIMITS.MAX_EVAL_ORDER;
    function initial() {
        const maps = {};
        for (const base of [GL.MAP1_COLOR_4, GL.MAP2_COLOR_4]) for (let i = 0; i < 9; ++i) {
            const target = base + i, k = components(target), defaults = i === 0 ? [1, 1, 1, 1] : i === 1 ? [1] : i === 2 ? [0, 0, 1] : [0, 0, 0, 1];
            maps[target] = { uorder: 1, vorder: 1, domain: [0, 1, 0, 1], points: defaults.slice(0, k) };
        }
        return maps;
    }
    // De Casteljau evaluates interpolation and extrapolation without the
    // large alternating binomial terms of a direct Bernstein sum.
    function curve(points, order, k, t) {
        const values = points.slice();
        for (let remaining = order - 1; remaining > 0; --remaining)
            for (let i = 0; i < remaining; ++i) for (let c = 0; c < k; ++c)
                values[i * k + c] = (1 - t) * values[i * k + c] + t * values[(i + 1) * k + c];
        return values.slice(0, k);
    }
    function evaluate(map, k, u, v) {
        const tu = (u - map.domain[0]) / (map.domain[1] - map.domain[0]), tv = (v - map.domain[2]) / (map.domain[3] - map.domain[2]), rows = [];
        for (let i = 0; i < map.uorder; ++i) rows.push(...curve(map.points.slice(i * map.vorder * k, (i + 1) * map.vorder * k), map.vorder, k, tv));
        return curve(rows, map.uorder, k, tu);
    }
    function derivative(map, k, u, v, axis) {
        const order = axis === 0 ? map.uorder : map.vorder;
        if (order < 2) return Array(k).fill(0);
        const result = [], nu = map.uorder - +(axis === 0), nv = map.vorder - +(axis === 1);
        for (let i = 0; i < nu; ++i) for (let j = 0; j < nv; ++j) for (let c = 0; c < k; ++c) {
            const at = (i * map.vorder + j) * k + c, next = at + (axis === 0 ? map.vorder * k : k);
            result.push((order - 1) * (map.points[next] - map.points[at]));
        }
        return evaluate({ ...map, uorder: nu, vorder: nv, points: result }, k, u, v);
    }
    function grid(index, count, first, last) { return index === 0 ? first : index === count ? last : first + index * (last - first) / count; }
    const pad = values => Array.from({ length: 4 }, (_, i) => values[i] ?? (i === 3 ? 1 : 0));
    function coordinate(s, dim, u, v) {
        const c = s.current, base = dim === 1 ? GL.MAP1_COLOR_4 : GL.MAP2_COLOR_4, maps = c.state.evaluators;
        const highest = indices => indices.find(i => enabled(c, base + i)), vertex = highest([8, 7]);
        if (vertex === undefined || !c.state.begin) return [];
        const position = pad(evaluate(maps[base + vertex], components(base + vertex), u, v)), attributes = {
            color: c.state.values[GL.CURRENT_COLOR].slice(), normal: c.state.values[GL.CURRENT_NORMAL].slice(), texCoord0: c.state.fixed.units[0].coord.slice(),
        };
        if (enabled(c, base)) attributes.color = evaluate(maps[base], 4, u, v);
        const tex = highest([6, 5, 4, 3]);
        if (tex !== undefined) attributes.texCoord0 = pad(evaluate(maps[base + tex], components(base + tex), u, v));
        if (dim === 2 && enabled(c, GL.AUTO_NORMAL)) {
            const map = maps[base + vertex], k = components(base + vertex), du = derivative(map, k, u, v, 0), dv = derivative(map, k, u, v, 1);
            const tangent = d => d.slice(0, 3).map((n, i) => k === 4 ? (n * position[3] - d[3] * position[i]) / (position[3] * position[3]) : n), a = tangent(du), b = tangent(dv);
            let n = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
            if (!selection.vertexProgram(s)) { const length = Math.hypot(...n); if (length) n = n.map(v => v / length); }
            attributes.normal = n;
        } else if (enabled(c, base + 2)) attributes.normal = evaluate(maps[base + 2], 3, u, v);
        return s.evaluatedVertex(position, attributes);
    }
    state.define("MAP1_GRID_DOMAIN", [0, 1], "f", 0x10000);
    state.define("MAP2_GRID_DOMAIN", [0, 1, 0, 1], "f", 0x10000);
    state.define("MAP1_GRID_SEGMENTS", [1], "i", 0x10000);
    state.define("MAP2_GRID_SEGMENTS", [1, 1], "i", 0x10000);
    state.query([GL.MAX_EVAL_ORDER], s => [[limit(s)], "i"]);
    GLShadow.rules({
        [P.GLFN.MAP_EVALUATOR]: (s, bytes, view, offset, size) => {
            const r = P.decodeRecord("MAP_EVALUATOR", view, offset), dim = dimensions(r.target), k = components(r.target);
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            if (!dim || dim !== r.dimensions) return GL.INVALID_ENUM;
            if (s.current.activeTexture) return GL.INVALID_OPERATION;
            if (r.uorder < 1 || r.uorder > limit(s) || dim === 2 && (r.vorder < 1 || r.vorder > limit(s)) || r.ustride < k || dim === 2 && r.vstride < k ||
                r.domain[0] === r.domain[1] || dim === 2 && r.domain[2] === r.domain[3]) return GL.INVALID_VALUE;
            const count = r.uorder * (dim === 2 ? r.vorder : 1) * k;
            return r.data_size !== count * 8 || r.data_size > size - P.RECORDS.MAP_EVALUATOR.size ? GL.INVALID_VALUE : 0;
        },
        [P.GLFN.EVALUATOR_GRID]: (s, bytes, view, offset) => {
            const r = P.decodeRecord("EVALUATOR_GRID", view, offset);
            return s.current.state.begin ? GL.INVALID_OPERATION : ![1, 2].includes(r.dimensions) ? GL.INVALID_ENUM : r.un < 1 || r.dimensions === 2 && r.vn < 1 ? GL.INVALID_VALUE : 0;
        },
        [P.GLFN.EVAL_COORD]: (s, bytes, view, offset) => {
            const r = P.decodeRecord("EVAL_COORD", view, offset);
            return ![1, 2].includes(r.dimensions) || r.operation > 1 ? GL.INVALID_ENUM : r.operation && r.values.some((n, i) => i < r.dimensions && (!Number.isInteger(n) || n < -2147483648 || n > 2147483647)) ? GL.INVALID_VALUE : 0;
        },
        [P.GLFN.EVAL_MESH]: (s, bytes, view, offset) => {
            const r = P.decodeRecord("EVAL_MESH", view, offset);
            if (s.current.state.begin) return GL.INVALID_OPERATION;
            if (![1, 2].includes(r.dimensions) || ![GL.POINT, GL.LINE, ...(r.dimensions === 2 ? [GL.FILL] : [])].includes(r.mode)) return GL.INVALID_ENUM;
            const base = r.dimensions === 1 ? GL.MAP1_COLOR_4 : GL.MAP2_COLOR_4;
            if (!enabled(s.current, base + 7) && !enabled(s.current, base + 8)) return 0;
            const nu = Math.max(0, r.indices[1] - r.indices[0] + 1), nv = r.dimensions === 2 ? Math.max(0, r.indices[3] - r.indices[2] + 1) : 1;
            return nu * nv * (r.dimensions === 2 && r.mode !== GL.POINT ? 2 : 1) > 65536 ? GL.OUT_OF_MEMORY : 0;
        },
        [P.GLFN.RESOLVED_EVAL_VERTEX]: () => GL.INVALID_OPERATION,
    });
    GLShadow.prototype.queryEvaluator = function(bytes, view, offset, size) {
        const name = "QUERY_EVALUATOR", f = P.F[name]; if (!this.pending(name, view, offset, size)) return;
        const r = P.decodeRecord(name, view, offset), c = this.current, dim = dimensions(r.target);
        const fail = error => { if (c && !c.error) c.error = error; view.setUint32(offset + f.error, error, true); this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_FAILED); };
        if (!c || c.state.begin) return fail(GL.INVALID_OPERATION);
        if (!dim || ![GL.COEFF, GL.ORDER, GL.DOMAIN].includes(r.pname)) return fail(GL.INVALID_ENUM);
        if (![P.C.V86GL_STATE_INTEGER, P.C.V86GL_STATE_FLOAT, P.C.V86GL_STATE_DOUBLE].includes(r.value_kind)) return fail(GL.INVALID_ENUM);
        const map = c.state.evaluators[r.target], data = r.pname === GL.COEFF ? map.points : r.pname === GL.ORDER ? [map.uorder, ...(dim === 2 ? [map.vorder] : [])] : map.domain.slice(0, dim * 2), header = P.RECORDS[name].size;
        if (r.data_size < data.length * 8 || r.data_size > 2048 || r.data_size % 8 || header + r.data_size > size) return fail(GL.INVALID_VALUE);
        for (let i = 0; i < data.length; ++i) {
            let value = data[i];
            if (r.value_kind === P.C.V86GL_STATE_FLOAT) value = Math.fround(value);
            else if (r.value_kind === P.C.V86GL_STATE_INTEGER) value = Math.max(-2147483648, Math.min(2147483647, value < 0 ? -Math.round(-value) : Math.round(value)));
            view.setFloat64(offset + header + i * 8, value, true);
        }
        view.setUint32(offset + f.count, data.length, true); view.setUint32(offset + f.error, 0, true); this.answered(name, view, offset, P.C.V86GL_SYNC_QUERY_STATUS_OK);
    };
    GLShadow.install({ [P.GLFN.QUERY_EVALUATOR]: "queryEvaluator" });
    function evalPoint(s, dim, u, v) {
        const values = s.current.state.values, domain = values[dim === 1 ? GL.MAP1_GRID_DOMAIN : GL.MAP2_GRID_DOMAIN], counts = values[dim === 1 ? GL.MAP1_GRID_SEGMENTS : GL.MAP2_GRID_SEGMENTS];
        return coordinate(s, dim, grid(u, counts[0], domain[0], domain[1]), dim === 1 ? 0 : grid(v, counts[1], domain[2], domain[3]));
    }
    function mesh(s, r) {
        const commands = [], [u1, u2, v1, v2] = r.indices;
        if (u2 < u1 || r.dimensions === 2 && v2 < v1) return commands;
        const base = r.dimensions === 1 ? GL.MAP1_COLOR_4 : GL.MAP2_COLOR_4;
        if (!enabled(s.current, base + 7) && !enabled(s.current, base + 8)) return commands;
        const primitive = (mode, emit) => {
            commands.push(...s.expand(arrays.record(P.GLFN.BEGIN, [P.encodeArgs("BEGIN", [mode])])));
            if (!s.current.state.begin) return;
            emit(); commands.push(...s.expand(arrays.record(P.GLFN.END, [])));
        };
        const point = (u, v) => commands.push(...evalPoint(s, r.dimensions, u, v));
        if (r.dimensions === 1) primitive(r.mode === GL.POINT ? GL.POINTS : GL.LINE_STRIP, () => { for (let u = u1; u <= u2; ++u) point(u, 0); });
        else if (r.mode === GL.POINT) primitive(GL.POINTS, () => { for (let v = v1; v <= v2; ++v) for (let u = u1; u <= u2; ++u) point(u, v); });
        else if (r.mode === GL.LINE) {
            for (let v = v1; v <= v2; ++v) primitive(GL.LINE_STRIP, () => { for (let u = u1; u <= u2; ++u) point(u, v); });
            for (let u = u1; u <= u2; ++u) primitive(GL.LINE_STRIP, () => { for (let v = v1; v <= v2; ++v) point(u, v); });
        } else for (let v = v1; v < v2; ++v) primitive(GL.QUAD_STRIP, () => { for (let u = u1; u <= u2; ++u) { point(u, v); point(u, v + 1); } });
        return commands;
    }
    GLShadow.part({
        context(c) { c.state.evaluators = initial(); },
        setState(s) { for (const c of s.contexts.values()) if (!c.state.evaluators) c.state.evaluators = initial(); },
        routeRecord(s, fn, bytes, view, offset, size) {
            if (!s.current || ![P.GLFN.MAP_EVALUATOR, P.GLFN.EVALUATOR_GRID, P.GLFN.EVAL_COORD, P.GLFN.EVAL_MESH].includes(fn)) return;
            const failure = s.dispatch(fn, bytes, view, offset, size); if (failure) return { error: failure };
            const r = P.decodeRecord(P.NAME_BY_OPCODE[fn], view, offset), c = s.current;
            if (fn === P.GLFN.MAP_EVALUATOR) {
                const at = offset + P.RECORDS.MAP_EVALUATOR.size;
                c.state.evaluators[r.target] = { uorder: r.uorder, vorder: r.dimensions === 2 ? r.vorder : 1, domain: r.dimensions === 2 ? r.domain : [...r.domain.slice(0, 2), 0, 1],
                    points: Array.from({ length: r.data_size / 8 }, (_, i) => view.getFloat64(at + i * 8, true)) };
            } else if (fn === P.GLFN.EVALUATOR_GRID) {
                state.set(c, r.dimensions === 1 ? GL.MAP1_GRID_DOMAIN : GL.MAP2_GRID_DOMAIN, r.domain.slice(0, r.dimensions * 2));
                state.set(c, r.dimensions === 1 ? GL.MAP1_GRID_SEGMENTS : GL.MAP2_GRID_SEGMENTS, r.dimensions === 1 ? [r.un] : [r.un, r.vn]);
            } else if (fn === P.GLFN.EVAL_COORD) return { commands: r.operation ? evalPoint(s, r.dimensions, ...r.values) : coordinate(s, r.dimensions, ...r.values) };
            else return { commands: mesh(s, r) };
            return { commands: [] };
        },
    });
    const api = { dimensions, components, curve, evaluate, derivative, grid };
    global.GLEvaluators = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The canonical OpenGL "state" uniform block.
//
// Three things need to agree byte for byte about where gl_ModelViewMatrix
// lives: the fixed-function WGSL generator, the GLSL compatibility built-ins
// in gl_shader_translator.js, and the executor code that fills the buffer.
// Historically that kind of agreement is maintained by three copies of a
// struct and a bug report; here there is one table and everyone derives from
// it.
//
// Fields are allocated per program, not globally: a shader that only touches
// gl_ModelViewProjectionMatrix gets a 16-float block, not the ~4 KB the full
// table would occupy. The unit of trimming is the *field*, and for the array
// state (lights, texture matrices, clip planes) the whole array -- because
// GLSL lets a loop index gl_LightSource[i] with a non-constant i, and an array
// that only sometimes exists cannot be indexed dynamically.
//
// Matrix layout: GL and WGSL are both column-major, so a mat4 is a straight
// 16-float copy. A WGSL mat3x3 in the uniform address space pads each column
// to 16 bytes, hence 12 floats for gl_NormalMatrix.
//
// See docs/glbridge.md (CPU ownership and snapshot resources).

(function(global) {
    "use strict";

    const MAX_LIGHTS = 8;
    const MAX_PROGRAM_MATRICES = 8;
    const MAX_TEXTURE_UNITS = 8;
    const MAX_TEXTURE_COORDS = 8;
    const MAX_CLIP_PLANES = 6;

    /* ---------- helpers used by the field writers ---------- */

    function writeVec4(out, offset, v) {
        out[offset] = v[0]; out[offset + 1] = v[1];
        out[offset + 2] = v[2]; out[offset + 3] = v[3];
        return offset + 4;
    }

    function writeMat4(out, offset, m) {
        for (let i = 0; i < 16; ++i) out[offset + i] = m[i];
        return offset + 16;
    }

    /* A WGSL mat3x3<f32> is three vec4-aligned columns. The source is a
     * 9-float column-major GL 3x3. */
    function writeMat3(out, offset, m) {
        out[offset] = m[0]; out[offset + 1] = m[1]; out[offset + 2] = m[2];
        out[offset + 3] = 0;
        out[offset + 4] = m[3]; out[offset + 5] = m[4]; out[offset + 6] = m[5];
        out[offset + 7] = 0;
        out[offset + 8] = m[6]; out[offset + 9] = m[7]; out[offset + 10] = m[8];
        out[offset + 11] = 0;
        return offset + 12;
    }

    function writeMaterial(out, offset, m) {
        offset = writeVec4(out, offset, m.emission);
        offset = writeVec4(out, offset, m.ambient);
        offset = writeVec4(out, offset, m.diffuse);
        offset = writeVec4(out, offset, m.specular);
        out[offset] = m.shininess;
        out[offset + 1] = 0; out[offset + 2] = 0; out[offset + 3] = 0;
        return offset + 4;
    }

    /* gl_LightSourceParameters, laid out as 8 vec4 so that a WGSL struct with
     * the same field order needs no padding surprises. */
    function writeLight(out, offset, l) {
        offset = writeVec4(out, offset, l.ambient);
        offset = writeVec4(out, offset, l.diffuse);
        offset = writeVec4(out, offset, l.specular);
        offset = writeVec4(out, offset, l.position);
        offset = writeVec4(out, offset, l.halfVector);
        out[offset] = l.spotDirection[0];
        out[offset + 1] = l.spotDirection[1];
        out[offset + 2] = l.spotDirection[2];
        out[offset + 3] = l.spotExponent;
        offset += 4;
        out[offset] = l.spotCutoff;
        out[offset + 1] = l.spotCosCutoff;
        out[offset + 2] = l.constantAttenuation;
        out[offset + 3] = l.linearAttenuation;
        offset += 4;
        out[offset] = l.quadraticAttenuation;
        out[offset + 1] = 0; out[offset + 2] = 0; out[offset + 3] = 0;
        return offset + 4;
    }

    function writeLightProduct(out, offset, p) {
        offset = writeVec4(out, offset, p.ambient);
        offset = writeVec4(out, offset, p.diffuse);
        offset = writeVec4(out, offset, p.specular);
        return offset;
    }

    /* ---------- WGSL struct declarations shared by the fields ---------- */

    const STRUCT_DECLS = {
        GLMaterial:
            "struct GLMaterial {\n" +
            "    emission : vec4<f32>,\n" +
            "    ambient : vec4<f32>,\n" +
            "    diffuse : vec4<f32>,\n" +
            "    specular : vec4<f32>,\n" +
            "    shininess : f32,\n" +
            "    _pad0 : f32,\n" +
            "    _pad1 : f32,\n" +
            "    _pad2 : f32,\n" +
            "}",
        GLLight:
            "struct GLLight {\n" +
            "    ambient : vec4<f32>,\n" +
            "    diffuse : vec4<f32>,\n" +
            "    specular : vec4<f32>,\n" +
            "    position : vec4<f32>,\n" +
            "    halfVector : vec4<f32>,\n" +
            "    spotDirection : vec3<f32>,\n" +
            "    spotExponent : f32,\n" +
            "    spotCutoff : f32,\n" +
            "    spotCosCutoff : f32,\n" +
            "    constantAttenuation : f32,\n" +
            "    linearAttenuation : f32,\n" +
            "    quadraticAttenuation : f32,\n" +
            "    _pad0 : f32,\n" +
            "    _pad1 : f32,\n" +
            "    _pad2 : f32,\n" +
            "}",
        GLLightProduct:
            "struct GLLightProduct {\n" +
            "    ambient : vec4<f32>,\n" +
            "    diffuse : vec4<f32>,\n" +
            "    specular : vec4<f32>,\n" +
            "}",
    };

    /*
     * The field table.
     *
     * id       stable key used by signatures and by the shader cache
     * wgsl     field name inside the generated GLState struct
     * type     WGSL type text
     * floats   size in f32 units (already includes WGSL padding)
     * needs    struct declarations this field's type depends on
     * write    fills `out` starting at `offset`, returns the next offset
     */
    const FIELDS = [
        /* --- matrices --- */
        { id: "modelview", wgsl: "modelview", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.modelview) },
        { id: "projection", wgsl: "projection", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.projection) },
        { id: "mvp", wgsl: "mvp", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.mvp) },
        { id: "normalMatrix", wgsl: "normalMatrix", type: "mat3x3<f32>", floats: 12,
          write: (s, o, off) => writeMat3(o, off, s.matrices.normal) },
        { id: "modelviewInverse", wgsl: "modelviewInverse", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.modelviewInverse) },
        { id: "modelviewTranspose", wgsl: "modelviewTranspose", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.modelviewTranspose) },
        { id: "modelviewInverseTranspose", wgsl: "modelviewInverseTranspose", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.modelviewInverseTranspose) },
        { id: "projectionInverse", wgsl: "projectionInverse", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.projectionInverse) },
        { id: "projectionTranspose", wgsl: "projectionTranspose", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.projectionTranspose) },
        { id: "projectionInverseTranspose", wgsl: "projectionInverseTranspose", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.projectionInverseTranspose) },
        { id: "mvpInverse", wgsl: "mvpInverse", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.mvpInverse) },
        { id: "mvpTranspose", wgsl: "mvpTranspose", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.mvpTranspose) },
        { id: "mvpInverseTranspose", wgsl: "mvpInverseTranspose", type: "mat4x4<f32>", floats: 16,
          write: (s, o, off) => writeMat4(o, off, s.matrices.mvpInverseTranspose) },

        ...["", "Inverse", "Transpose", "InverseTranspose"].map(suffix => ({
            id: "programMatrix" + suffix, wgsl: "programMatrix" + suffix,
            type: "array<mat4x4<f32>, " + MAX_PROGRAM_MATRICES + ">", floats: 16 * MAX_PROGRAM_MATRICES,
            write: (s, o, off) => {
                for (let i = 0; i < MAX_PROGRAM_MATRICES; ++i) off = writeMat4(o, off, s.matrices["program" + suffix][i]);
                return off;
            },
        })),

        /* Texture matrices are one array, not eight fields: GLSL allows
         * gl_TextureMatrix[i] with a runtime i. */
        { id: "textureMatrix", wgsl: "textureMatrix",
          type: "array<mat4x4<f32>, " + MAX_TEXTURE_COORDS + ">",
          floats: 16 * MAX_TEXTURE_COORDS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_COORDS; ++i)
                  off = writeMat4(o, off, s.matrices.texture[i]);
              return off;
          } },
        { id: "textureMatrixInverse", wgsl: "textureMatrixInverse",
          type: "array<mat4x4<f32>, " + MAX_TEXTURE_COORDS + ">",
          floats: 16 * MAX_TEXTURE_COORDS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_COORDS; ++i)
                  off = writeMat4(o, off, s.matrices.textureInverse[i]);
              return off;
          } },
        { id: "textureMatrixTranspose", wgsl: "textureMatrixTranspose",
          type: "array<mat4x4<f32>, " + MAX_TEXTURE_COORDS + ">",
          floats: 16 * MAX_TEXTURE_COORDS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_COORDS; ++i)
                  off = writeMat4(o, off, s.matrices.textureTranspose[i]);
              return off;
          } },
        { id: "textureMatrixInverseTranspose", wgsl: "textureMatrixInverseTranspose",
          type: "array<mat4x4<f32>, " + MAX_TEXTURE_COORDS + ">",
          floats: 16 * MAX_TEXTURE_COORDS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_COORDS; ++i)
                  off = writeMat4(o, off, s.matrices.textureInverseTranspose[i]);
              return off;
          } },

        /* --- lighting --- */
        { id: "lights", wgsl: "lights",
          type: "array<GLLight, " + MAX_LIGHTS + ">", needs: ["GLLight"],
          floats: 32 * MAX_LIGHTS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_LIGHTS; ++i)
                  off = writeLight(o, off, s.lights[i]);
              return off;
          } },
        { id: "frontMaterial", wgsl: "frontMaterial", type: "GLMaterial",
          needs: ["GLMaterial"], floats: 20,
          write: (s, o, off) => writeMaterial(o, off, s.material.front) },
        { id: "backMaterial", wgsl: "backMaterial", type: "GLMaterial",
          needs: ["GLMaterial"], floats: 20,
          write: (s, o, off) => writeMaterial(o, off, s.material.back) },
        { id: "lightModelAmbient", wgsl: "lightModelAmbient", type: "vec4<f32>",
          floats: 4,
          write: (s, o, off) => writeVec4(o, off, s.lightModel.ambient) },
        { id: "frontSceneColor", wgsl: "frontSceneColor", type: "vec4<f32>",
          floats: 4,
          write: (s, o, off) => writeVec4(o, off, s.derived.frontSceneColor) },
        { id: "backSceneColor", wgsl: "backSceneColor", type: "vec4<f32>",
          floats: 4,
          write: (s, o, off) => writeVec4(o, off, s.derived.backSceneColor) },
        { id: "frontLightProduct", wgsl: "frontLightProduct",
          type: "array<GLLightProduct, " + MAX_LIGHTS + ">",
          needs: ["GLLightProduct"], floats: 12 * MAX_LIGHTS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_LIGHTS; ++i)
                  off = writeLightProduct(o, off, s.derived.frontLightProduct[i]);
              return off;
          } },
        { id: "backLightProduct", wgsl: "backLightProduct",
          type: "array<GLLightProduct, " + MAX_LIGHTS + ">",
          needs: ["GLLightProduct"], floats: 12 * MAX_LIGHTS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_LIGHTS; ++i)
                  off = writeLightProduct(o, off, s.derived.backLightProduct[i]);
              return off;
          } },

        /* --- fog --- */
        { id: "fogColor", wgsl: "fogColor", type: "vec4<f32>", floats: 4,
          write: (s, o, off) => writeVec4(o, off, s.fog.color) },
        { id: "fogParams", wgsl: "fogParams", type: "vec4<f32>", floats: 4,
          /* density, start, end, scale = 1/(end-start); GLSL exposes these as
           * gl_Fog.density/.start/.end/.scale. */
          write: (s, o, off) => {
              const span = s.fog.end - s.fog.start;
              return writeVec4(o, off, [s.fog.density, s.fog.start, s.fog.end,
                  span !== 0 ? 1 / span : 0]);
          } },

        /* --- texture environment / coordinate generation --- */
        { id: "texEnvColor", wgsl: "texEnvColor",
          type: "array<vec4<f32>, " + MAX_TEXTURE_UNITS + ">",
          floats: 4 * MAX_TEXTURE_UNITS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_UNITS; ++i)
                  off = writeVec4(o, off, s.texEnvColor[i]);
              return off;
          } },
        /* Each unit's LOD bias: the unit's TEXTURE_FILTER_CONTROL bias plus
         * its texture's TEXTURE_LOD_BIAS, four units a vec4. */
        { id: "textureLodBias", wgsl: "textureLodBias",
          type: "array<vec4<f32>, " + MAX_TEXTURE_UNITS / 4 + ">",
          floats: MAX_TEXTURE_UNITS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_UNITS; i += 4)
                  off = writeVec4(o, off, s.textureLodBias.subarray(i, i + 4));
              return off;
          } },
        /* Each unit's texture border colour, in its base format's channels
         * (GL 2.1 table 3.15: a LUMINANCE border is (R, R, R, 1)). */
        { id: "textureBorderColor", wgsl: "textureBorderColor",
          type: "array<vec4<f32>, " + MAX_TEXTURE_UNITS + ">",
          floats: 4 * MAX_TEXTURE_UNITS,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_TEXTURE_UNITS; ++i)
                  off = writeVec4(o, off, s.textureBorderColor[i]);
              return off;
          } },
        // Four vec4 rows per typed sampler binding, rather than per enabled
        // texture unit: GLSL/ARB instructions select their target explicitly.
        { id: "samplerState", wgsl: "samplerState", type: "array<vec4<f32>, 64>", floats: 256,
          write: (s, o, off) => {
              if (s.samplerState) o.set(s.samplerState, off);
              else o.fill(0, off, off + 256);
              return off + 256;
          } },
        /* Object/eye linear planes: 4 coordinates (S,T,R,Q) x 8 units, each a
         * pair of vec4 (object plane, eye plane). Flat array so the fixed
         * function generator can index it with a constant. */
        { id: "texGenPlanes", wgsl: "texGenPlanes",
          type: "array<vec4<f32>, " + (MAX_TEXTURE_UNITS * 8) + ">",
          floats: 4 * MAX_TEXTURE_UNITS * 8,
          write: (s, o, off) => {
              for (let u = 0; u < MAX_TEXTURE_UNITS; ++u) {
                  const gen = s.texGen[u];
                  for (let c = 0; c < 4; ++c) off = writeVec4(o, off, gen[c].objectPlane);
                  for (let c = 0; c < 4; ++c) off = writeVec4(o, off, gen[c].eyePlane);
              }
              return off;
          } },

        /* --- clipping, depth, point size, alpha test --- */
        { id: "clipPlanes", wgsl: "clipPlanes",
          type: "array<vec4<f32>, " + MAX_CLIP_PLANES + ">",
          floats: 4 * MAX_CLIP_PLANES,
          write: (s, o, off) => {
              for (let i = 0; i < MAX_CLIP_PLANES; ++i)
                  off = writeVec4(o, off, s.clipPlanes[i]);
              return off;
          } },
        { id: "depthRange", wgsl: "depthRange", type: "vec4<f32>", floats: 4,
          /* near, far, diff, unused -- matches gl_DepthRangeParameters. */
          write: (s, o, off) => writeVec4(o, off, [s.depthRange.near,
              s.depthRange.far, s.depthRange.far - s.depthRange.near, 0]) },
        { id: "pointParams", wgsl: "pointParams", type: "vec4<f32>", floats: 4,
          /* size, sizeMin, sizeMax, fadeThresholdSize */
          write: (s, o, off) => writeVec4(o, off, [s.point.size, s.point.sizeMin,
              s.point.sizeMax, s.point.fadeThreshold]) },
        { id: "pointAttenuation", wgsl: "pointAttenuation", type: "vec4<f32>", floats: 4,
          write: (s, o, off) => writeVec4(o, off, [s.point.attenuation[0],
              s.point.attenuation[1], s.point.attenuation[2], 0]) },
        { id: "alphaRef", wgsl: "alphaRef", type: "vec4<f32>", floats: 4,
          /* ref, unused... a vec4 because a lone f32 in a uniform struct still
           * costs 16 bytes of alignment; naming the padding avoids surprises. */
          write: (s, o, off) => writeVec4(o, off, [s.alphaRef, 0, 0, 0]) },

        /* --- viewport, needed by point/line expansion and stipple --- */
        { id: "viewportParams", wgsl: "viewportParams", type: "vec4<f32>", floats: 4,
          /* x, y, width, height in framebuffer pixels */
          write: (s, o, off) => writeVec4(o, off, [s.viewport.x, s.viewport.y,
              s.viewport.width, s.viewport.height]) },
        { id: "rasterParams", wgsl: "rasterParams", type: "vec4<f32>", floats: 4,
          /* lineWidth, lineStipplePattern (as float bits), lineStippleFactor,
           * polygon-stipple enable */
          write: (s, o, off) => writeVec4(o, off, [s.lineWidth,
              s.lineStipple.pattern, s.lineStipple.factor,
              s.polygonStippleEnabled ? 1 : 0]) },
        { id: "polygonStipple", wgsl: "polygonStipple",
          type: "array<vec4<f32>, 32>", floats: 128,
          /* One byte per float keeps all 1024 pattern bits exact while staying
           * inside the existing f32-only uniform staging layout. */
          write: (s, o, off) => {
              for (let i = 0; i < 128; ++i) o[off++] = s.polygonStipple[i];
              return off;
          } },
    ];

    const FIELD_BY_ID = new Map();
    for (const field of FIELDS) FIELD_BY_ID.set(field.id, field);

    /* Fields whose GLSL name maps directly onto one table entry. The compat
     * built-ins the translator resolves through structured access (lights,
     * materials, fog members) are handled in the translator itself. */
    const GLSL_TO_FIELD = {
        gl_ModelViewMatrix: "modelview",
        gl_ProjectionMatrix: "projection",
        gl_ModelViewProjectionMatrix: "mvp",
        gl_NormalMatrix: "normalMatrix",
        gl_ModelViewMatrixInverse: "modelviewInverse",
        gl_ModelViewMatrixTranspose: "modelviewTranspose",
        gl_ModelViewMatrixInverseTranspose: "modelviewInverseTranspose",
        gl_ProjectionMatrixInverse: "projectionInverse",
        gl_ProjectionMatrixTranspose: "projectionTranspose",
        gl_ProjectionMatrixInverseTranspose: "projectionInverseTranspose",
        gl_ModelViewProjectionMatrixInverse: "mvpInverse",
        gl_ModelViewProjectionMatrixTranspose: "mvpTranspose",
        gl_ModelViewProjectionMatrixInverseTranspose: "mvpInverseTranspose",
        gl_TextureMatrix: "textureMatrix",
        gl_TextureMatrixInverse: "textureMatrixInverse",
        gl_TextureMatrixTranspose: "textureMatrixTranspose",
        gl_TextureMatrixInverseTranspose: "textureMatrixInverseTranspose",
        gl_LightSource: "lights",
        gl_FrontMaterial: "frontMaterial",
        gl_BackMaterial: "backMaterial",
        gl_LightModel: "lightModelAmbient",
        gl_FrontLightModelProduct: "frontSceneColor",
        gl_BackLightModelProduct: "backSceneColor",
        gl_FrontLightProduct: "frontLightProduct",
        gl_BackLightProduct: "backLightProduct",
        gl_Fog: "fogColor",             // .color; the scalars come from fogParams
        gl_TextureEnvColor: "texEnvColor",
        gl_ObjectPlaneS: "texGenPlanes",
        gl_EyePlaneS: "texGenPlanes",
        gl_ClipPlane: "clipPlanes",
        gl_DepthRange: "depthRange",
        gl_Point: "pointParams",
    };

    /*
     * Builds the WGSL declaration for a chosen subset.
     *
     * The subset is ordered by the table, not by the order the caller
     * discovered the fields, so the same set always produces the same struct
     * -- which is what lets the shader cache key on the field list.
     */
    function buildLayout(fieldIds) {
        const wanted = new Set(fieldIds);
        const chosen = FIELDS.filter(f => wanted.has(f.id));
        const missing = [...wanted].filter(id => !FIELD_BY_ID.has(id));
        if (missing.length)
            throw new Error("unknown GL state field(s): " + missing.join(", "));

        const structs = [];
        const seenStructs = new Set();
        for (const field of chosen) {
            for (const name of field.needs || []) {
                if (seenStructs.has(name)) continue;
                seenStructs.add(name);
                structs.push(STRUCT_DECLS[name]);
            }
        }

        const lines = [];
        let floats = 0;
        const offsets = new Map();
        lines.push("struct GLState {");
        for (const field of chosen) {
            offsets.set(field.id, floats);
            lines.push("    " + field.wgsl + " : " + field.type + ",");
            floats += field.floats;
        }
        /* An empty uniform struct is invalid WGSL, and a program that touches
         * no GL state still needs a binding slot so the pipeline layout does
         * not change shape per shader. */
        if (!chosen.length) lines.push("    _unused : vec4<f32>,");
        lines.push("}");

        return {
            fields: chosen.map(f => f.id),
            floats: chosen.length ? floats : 4,
            offsets,
            structDecls: structs,
            structText: (structs.length ? structs.join("\n\n") + "\n\n" : "") +
                lines.join("\n"),
        };
    }

    /* Fills `out` (a Float32Array view of the uniform staging buffer) with the
     * layout's fields, in table order, starting at `offset`. Returns the
     * number of floats written, which must equal layout.floats. */
    function writeLayout(layout, state, out, offset) {
        const start = offset;
        for (const id of layout.fields) {
            offset = FIELD_BY_ID.get(id).write(state, out, offset);
        }
        if (!layout.fields.length) {
            out[offset] = 0; out[offset + 1] = 0;
            out[offset + 2] = 0; out[offset + 3] = 0;
            offset += 4;
        }
        return offset - start;
    }

    // Border filtering is performed at each selected mip, with that mip's
    // texel footprint. Using one base-level blend factor around an ordinary
    // trilinear sample gives the wrong border band during minification.
    function textureSamplingWGSL(name, dimension, shadow, stage) {
        const volume = dimension === "3D", cube = dimension === "Cube", n = volume || cube ? 3 : 2,
            vector = "vec" + n, coordinate = vector + "<f32>", integer = vector + "<i32>",
            type = shadow ? "texture_depth_2d" : "texture_" + (cube ? "cube" : volume ? "3d" : "2d") + "<f32>",
            result = shadow ? "f32" : "vec4<f32>", sampler = shadow ? "sampler_comparison" : "sampler";
        const finish = code => {
            if (shadow) return code;
            return code.replace("fn " + name + "(", "fn " + name + "_raw(") + `
fn ${name}(t:${type},s:${sampler},uv:${coordinate},index:u32,level:f32,bias:f32,glExplicit:bool)->${result}{
 var color=${name}_raw(t,s,uv,index,level,bias,glExplicit);
 if(glState.samplerState[index*4u+3u].z>0.5){color.a=1.0;}return color;
}`;
        };
        if (cube) return finish(`fn ${name}(t:${type},s:${sampler},uv:${coordinate},index:u32,level:f32,bias:f32,glExplicit:bool)->${result}{
 let state=glState.samplerState[index*4u+2u];
 ${stage === "vertex" ? "return textureSampleLevel(t,s,uv,level+clamp(state.w,-16.0,16.0));" : `if(glExplicit){return textureSampleLevel(t,s,uv,level+clamp(state.w,-16.0,16.0));}
 return textureSampleBias(t,s,uv,clamp(state.w+bias,-16.0,16.0));`}
}`);
        const load = shadow ? `let depth=textureLoad(t,p,level);return ${name}_compare(glReference,depth,u32(glState.samplerState[index*4u+3u].x));` :
            "return textureLoad(t,p,level);";
        const border = shadow ? `${name}_compare(glReference,glState.samplerState[index*4u].x,u32(glState.samplerState[index*4u+3u].x))` :
            "glState.samplerState[index*4u]";
        const taps = volume ? `for(var z=0;z<2;z++){for(var y=0;y<2;y++){for(var x=0;x<2;x++){
 let bit=vec3<i32>(x,y,z);let w=select(vec3<f32>(1.0)-f,f,bit==vec3<i32>(1));
 sum+=${name}_load(t,at+bit,level,index,glReference,true)*w.x*w.y*w.z;}}}` :
            `for(var y=0;y<2;y++){for(var x=0;x<2;x++){
 let bit=vec2<i32>(x,y);let w=select(vec2<f32>(1.0)-f,f,bit==vec2<i32>(1));
 sum+=${name}_load(t,at+bit,level,index,glReference,true)*w.x*w.y;}}`;
        return finish(`${shadow ? `fn ${name}_compare(glReference:f32,depth:f32,func:u32)->f32 {
 let reference=clamp(glReference,0.0,1.0);
 switch(func){case 512u:{return 0.0;}case 513u:{return select(0.0,1.0,reference<depth);}
 case 514u:{return select(0.0,1.0,reference==depth);}case 515u:{return select(0.0,1.0,reference<=depth);}
 case 516u:{return select(0.0,1.0,reference>depth);}case 517u:{return select(0.0,1.0,reference!=depth);}
 case 518u:{return select(0.0,1.0,reference>=depth);}default:{return 1.0;}}
}
fn ${name}_color(value:f32,index:u32)->vec4<f32>{
 let mode=u32(glState.samplerState[index*4u+3u].y);
 if(mode==6406u){return vec4<f32>(0.0,0.0,0.0,value);}
 if(mode==32841u){return vec4<f32>(value);}return vec4<f32>(vec3<f32>(value),1.0);
}` : ""}
fn ${name}_load(t:${type},at:${integer},level:i32,index:u32,glReference:f32,linear:bool)->${result}{
 let size=${integer}(textureDimensions(t,level));let wraps=glState.samplerState[index*4u+1u];var p=at;
 for(var axis=0;axis<${n};axis++){
 let mode=u32(wraps[axis]);let length=size[axis];
 if(mode==10497u){p[axis]=((p[axis]%length)+length)%length;}
 else if(mode==33648u){let period=length*2;let q=((p[axis]%period)+period)%period;p[axis]=min(q,period-1-q);}
 else if(mode==33069u||(mode==10496u&&linear)){
 if(p[axis]<0||p[axis]>=length){return ${border};}
 }else{p[axis]=clamp(p[axis],0,length-1);}}
 ${load}
}
fn ${name}_level(t:${type},uv:${coordinate},level:i32,index:u32,glReference:f32,linear:bool)->${result}{
 let wraps=glState.samplerState[index*4u+1u];var coord=uv;
 for(var axis=0;axis<${n};axis++){if(u32(wraps[axis])==10496u){coord[axis]=clamp(coord[axis],0.0,1.0);}}
 let p=coord*${coordinate}(textureDimensions(t,level));
 if(!linear){return ${name}_load(t,${integer}(floor(p)),level,index,glReference,false);}
 let at=${integer}(floor(p-${coordinate}(0.5)));let f=fract(p-${coordinate}(0.5));var sum=${result}(0.0);
 ${taps}
 return sum;
}
fn ${name}(t:${type},s:${sampler},uv:${coordinate},index:u32,level:f32,bias:f32,glExplicit:bool${shadow ? ",glReference:f32" : ""})->${result}{
 let wraps=glState.samplerState[index*4u+1u];let state=glState.samplerState[index*4u+2u];
 let minimum=u32(wraps.w);let maximum=u32(state.x);
 ${!shadow ? `let border=any(vec${n}<u32>(wraps.${volume ? "xyz" : "xy"})==vec${n}<u32>(33069u))||any(vec${n}<u32>(wraps.${volume ? "xyz" : "xy"})==vec${n}<u32>(10496u));
 if(!border){${stage === "vertex" ? "return textureSampleLevel(t,s,uv,level+clamp(state.w,-16.0,16.0));" : `if(glExplicit){return textureSampleLevel(t,s,uv,level+clamp(state.w,-16.0,16.0));}return textureSampleBias(t,s,uv,clamp(state.w+bias,-16.0,16.0));`}}` : ""}
 var lod=level+clamp(state.w,-16.0,16.0);
 ${stage === "fragment" ? `if(!glExplicit){let size=${coordinate}(textureDimensions(t,0));
 let dx=dpdx(uv*size);let dy=dpdy(uv*size);lod=log2(max(max(length(dx),length(dy)),1e-8))+clamp(state.w+bias,-16.0,16.0);}` : ""}
 lod=clamp(lod,state.y,state.z);let last=f32(textureNumLevels(t)-1u);
 let threshold=select(0.0,0.5,maximum==9729u&&(minimum==9984u||minimum==9986u));
 if(lod<=threshold){return ${name}_level(t,uv,0,index,${shadow ? "glReference" : "0.0"},maximum==9729u);}
 let linear=minimum==9729u||minimum==9985u||minimum==9987u;
 if(minimum==9728u||minimum==9729u){return ${name}_level(t,uv,0,index,${shadow ? "glReference" : "0.0"},linear);}
 lod=clamp(lod,0.0,last);
 if(minimum==9984u||minimum==9985u){return ${name}_level(t,uv,i32(ceil(lod-0.5)),index,${shadow ? "glReference" : "0.0"},linear);}
 let first=i32(floor(lod));return mix(${name}_level(t,uv,first,index,${shadow ? "glReference" : "0.0"},linear),
 ${name}_level(t,uv,min(first+1,i32(last)),index,${shadow ? "glReference" : "0.0"},linear),fract(lod));
}`);
    }

    // Expansion uses a builtin vertex index, leaving all sixteen application
    // attribute locations available. Desktop aliased points round both size
    // and window centre before the square is expanded (GL 2.1 section 3.3).
    function pointRasterWGSL(desktop, depthClamp, rounded = true) {
        return `
fn gl_point_corner(index:u32)->vec2<f32>{
 let corners=array<vec2<f32>,6>(vec2<f32>(-1.0,-1.0),vec2<f32>(1.0,-1.0),vec2<f32>(-1.0,1.0),
 vec2<f32>(-1.0,1.0),vec2<f32>(1.0,-1.0),vec2<f32>(1.0,1.0));return corners[index%6u];
}
fn gl_point_position(clip:vec4<f32>,size:f32,corner:vec2<f32>,viewport:vec4<f32>)->vec4<f32>{
 ${desktop ? `if(clip.w<=0.0||any(abs(clip.xy)>vec2<f32>(clip.w))${depthClamp ? "" : "||abs(clip.z)>clip.w"}){return vec4<f32>(0.0,0.0,0.0,-1.0);}
 ${rounded ? `let rounded=max(1.0,floor(min(size,64.0)+0.5));let window=viewport.xy+(clip.xy/clip.w*0.5+0.5)*viewport.zw;
 let centre=select(floor(window)+0.5,floor(window+0.5),u32(rounded)%2u==0u);
 var result=clip;result.x=((centre.x-viewport.x)/viewport.z*2.0-1.0)*clip.w;
 result.y=((centre.y-viewport.y)/viewport.w*2.0-1.0)*clip.w;` : "let rounded=min(size,64.0);var result=clip;"}` : "let rounded=size;var result=clip;"}
 result.x+=corner.x*rounded*clip.w/viewport.z;result.y+=corner.y*rounded*clip.w/viewport.w;return result;
}
`;
    }

    const api = {
        FIELDS, FIELD_BY_ID, GLSL_TO_FIELD, STRUCT_DECLS,
        MAX_PROGRAM_MATRICES, MAX_LIGHTS, MAX_TEXTURE_UNITS, MAX_TEXTURE_COORDS, MAX_CLIP_PLANES,
        buildLayout, writeLayout, textureSamplingWGSL, pointRasterWGSL,
    };
    global.GLStateLayout = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// GLSL 1.10/1.20 -> WGSL.
//
// This is a real front end -- preprocessor, lexer, parser, type checker, code
// generator -- and not a source rewriter, for a reason specific to the games
// this bridge has to run. Cube 2's shaders are assembled at runtime by
// CubeScript: tests/cube2_glsl_coverage.md records that 25 of its shader pairs
// are literal and the rest are generated by nested substitution, so the set of
// source texts is not enumerable ahead of time. Anything pattern-based passes
// the corpus and then fails unpredictably on the variants a real map produces.
//
// Structure:
//   preprocess()  #define/#if/#extension/#version, macro expansion
//   lex()         GLSL token stream
//   parse()       AST
//   analyze()     scopes, overload resolution, implicit conversions, usage
//   generate()    WGSL text + reflection, at link time (both stages together)
//
// Compilation is per shader (glCompileShader) but code generation is per
// program (glLinkProgram), because the varying interface -- which varyings
// exist, in which slots, packed how -- is only decidable once both stages are
// known. That split matches GL's own semantics exactly.
//
// See docs/opengl-webgpu-plan.md E1/E3.

(function(global) {
    "use strict";

    const stateLayout = (typeof require === "function" && typeof module !== "undefined") ?
        require("./gl_state_layout.js") : global.GLStateLayout;

    const TRANSLATOR_REVISION = 7;

    // A capture invocation runs the actual vertex program, with the same
    // uniforms and textures as a draw. Keep GL clip coordinates for the CPU
    // selection/feedback clipper; viewport conversion belongs to that clipper.
    const VERTEX_CAPTURE = Object.freeze({ attributes: 16, inputBytes: 256, outputBytes: 80, rasterOutputBytes: 224, workgroupSize: 64 });
    function vertexCaptureBindings(raster = false) {
        return [
            "struct GLCaptureInput { attributes : array<vec4<f32>, 16>, }",
            "struct GLCaptureOutput { clip : vec4<f32>, color : vec4<f32>, backColor : vec4<f32>, tex : vec4<f32>, eye : vec4<f32>, " +
                (raster ? "secondary : vec4<f32>, fog : vec4<f32>, texExtra : array<vec4<f32>, 7>, " : "") + "}",
            "@group(3) @binding(0) var<storage, read> glCaptureInput : array<GLCaptureInput>;",
            "@group(3) @binding(1) var<storage, read_write> glCaptureOutput : array<GLCaptureOutput>;",
        ].join("\n");
    }

    /* ================================================================== */
    /* Types                                                              */
    /* ================================================================== */

    // GLSL and WGSL agree that matNxM is N columns by M rows, so matrix types
    // map across without a transpose anywhere.
    const T = {};
    T.void = { kind: "void", name: "void" };
    T.bool = { kind: "scalar", base: "bool", name: "bool" };
    T.int = { kind: "scalar", base: "int", name: "int" };
    T.float = { kind: "scalar", base: "float", name: "float" };

    const VEC_NAMES = { bool: "bvec", int: "ivec", float: "vec" };
    const vectorCache = new Map();
    function vec(base, size) {
        const key = base + size;
        let t = vectorCache.get(key);
        if (!t) {
            t = { kind: "vector", base, size, name: VEC_NAMES[base] + size };
            vectorCache.set(key, t);
        }
        return t;
    }

    const matrixCache = new Map();
    function mat(cols, rows) {
        const key = cols + "x" + rows;
        let t = matrixCache.get(key);
        if (!t) {
            t = { kind: "matrix", cols, rows, base: "float",
                  name: cols === rows ? "mat" + cols : "mat" + cols + "x" + rows };
            matrixCache.set(key, t);
        }
        return t;
    }

    const samplerCache = new Map();
    function sampler(dim, shadow) {
        const key = dim + (shadow ? "S" : "");
        let t = samplerCache.get(key);
        if (!t) {
            t = { kind: "sampler", dim, shadow: !!shadow,
                  name: "sampler" + dim + (shadow ? "Shadow" : "") };
            samplerCache.set(key, t);
        }
        return t;
    }

    function arrayOf(elem, size) {
        return { kind: "array", elem, size,
                 name: elem.name + "[" + (size >= 0 ? size : "") + "]" };
    }

    function structType(name, fields) {
        return { kind: "struct", name, fields };
    }

    const BASIC_TYPES = {
        void: T.void, bool: T.bool, int: T.int, float: T.float,
        vec2: vec("float", 2), vec3: vec("float", 3), vec4: vec("float", 4),
        bvec2: vec("bool", 2), bvec3: vec("bool", 3), bvec4: vec("bool", 4),
        ivec2: vec("int", 2), ivec3: vec("int", 3), ivec4: vec("int", 4),
        mat2: mat(2, 2), mat3: mat(3, 3), mat4: mat(4, 4),
        mat2x2: mat(2, 2), mat2x3: mat(2, 3), mat2x4: mat(2, 4),
        mat3x2: mat(3, 2), mat3x3: mat(3, 3), mat3x4: mat(3, 4),
        mat4x2: mat(4, 2), mat4x3: mat(4, 3), mat4x4: mat(4, 4),
        sampler1D: sampler("1D"), sampler2D: sampler("2D"),
        sampler3D: sampler("3D"), samplerCube: sampler("Cube"),
        sampler1DShadow: sampler("1D", true), sampler2DShadow: sampler("2D", true),
        sampler2DRect: sampler("2DRect"), sampler2DRectShadow: sampler("2DRect", true),
        // GL_OES_EGL_image_external (GLSL ES): a 2D sampler of an EGL image,
        // which the executor holds as an ordinary 2D texture
        samplerExternalOES: { kind: "sampler", dim: "2D", shadow: false, name: "samplerExternalOES", external: true },
    };

    function sameType(a, b) {
        if (a === b) return true;
        if (!a || !b || a.kind !== b.kind) return false;
        switch (a.kind) {
        case "void": return true;
        case "scalar": return a.base === b.base;
        case "vector": return a.base === b.base && a.size === b.size;
        case "matrix": return a.cols === b.cols && a.rows === b.rows;
        case "sampler": return a.dim === b.dim && a.shadow === b.shadow;
        case "array": return a.size === b.size && sameType(a.elem, b.elem);
        case "struct": return a.name === b.name;
        default: return false;
        }
    }

    function componentCount(t) {
        switch (t.kind) {
        case "scalar": return 1;
        case "vector": return t.size;
        case "matrix": return t.cols * t.rows;
        default: return 0;
        }
    }

    function baseOf(t) {
        if (t.kind === "scalar" || t.kind === "vector") return t.base;
        if (t.kind === "matrix") return "float";
        return null;
    }

    /* GLSL 1.20 has exactly one implicit conversion: int -> float, applied
     * componentwise. Nothing converts to int, and bool never converts. WGSL has
     * no implicit conversions at all, so every one of these has to be made
     * explicit during analysis -- which is why it is tracked as a type relation
     * here rather than left to code generation to guess at. */
    function convertsTo(from, to) {
        if (sameType(from, to)) return true;
        if (!from || !to) return false;
        if (from.kind === "scalar" && to.kind === "scalar")
            return from.base === "int" && to.base === "float";
        if (from.kind === "vector" && to.kind === "vector")
            return from.size === to.size && from.base === "int" && to.base === "float";
        return false;
    }

    function typeToWGSL(t) {
        switch (t.kind) {
        case "void": return "void";
        case "scalar":
            return t.base === "float" ? "f32" : (t.base === "int" ? "i32" : "bool");
        case "vector":
            return "vec" + t.size + "<" +
                (t.base === "float" ? "f32" : (t.base === "int" ? "i32" : "bool")) + ">";
        case "matrix":
            return "mat" + t.cols + "x" + t.rows + "<f32>";
        case "array":
            return "array<" + typeToWGSL(t.elem) + ", " + t.size + ">";
        case "struct":
            return safeName(t.name);
        default:
            throw new Error("no WGSL type for " + t.kind);
        }
    }

    /* GLSL's == of two values of type t (already named, as each is read more
     * than once) as one WGSL bool */
    function equalityWGSL(a, b, t) {
        switch (t.kind) {
        case "vector":
            return "all(" + a + " == " + b + ")";
        case "matrix": {
            const columns = [];
            for (let c = 0; c < t.cols; ++c)
                columns.push("all(" + a + "[" + c + "] == " + b + "[" + c + "])");
            return "(" + columns.join(" && ") + ")";
        }
        case "array": {
            const elements = [];
            for (let i = 0; i < t.size; ++i)
                elements.push(equalityWGSL(a + "[" + i + "]", b + "[" + i + "]", t.elem));
            return "(" + elements.join(" && ") + ")";
        }
        case "struct":
            return "(" + t.fields.map(f => equalityWGSL(a + "." + safeName(f.name),
                b + "." + safeName(f.name), f.type)).join(" && ") + ")";
        default:
            return "(" + a + " == " + b + ")";
        }
    }

    function zeroValueWGSL(t) {
        switch (t.kind) {
        case "scalar":
            return t.base === "float" ? "0.0" : (t.base === "int" ? "0" : "false");
        case "vector":
        case "matrix":
        case "array":
        case "struct":
            return typeToWGSL(t) + "()";
        default:
            return typeToWGSL(t) + "()";
        }
    }

    /* ================================================================== */
    /* Diagnostics                                                        */
    /* ================================================================== */

    class GLSLError extends Error {
        constructor(message, line) {
            super(message);
            this.glslLine = line || 0;
        }
    }

    function fail(line, message) {
        throw new GLSLError(message, line);
    }

    /* ================================================================== */
    /* Preprocessor                                                       */
    /* ================================================================== */

    /*
     * Comment removal and line splicing, producing an array of physical lines.
     *
     * Line numbers have to survive both transformations because every later
     * error message is reported against them, so a splice or a multi-line
     * comment emits the blank lines it swallowed rather than shifting
     * everything after it.
     */
    function stripCommentsAndSplice(source) {
        const lines = [];
        let cur = "";
        let pending = 0;
        let i = 0;
        const n = source.length;
        const endLine = () => {
            lines.push(cur);
            cur = "";
            for (let k = 0; k < pending; ++k) lines.push("");
            pending = 0;
        };
        while (i < n) {
            const c = source[i];
            if (c === "\\" && source[i + 1] === "\n") { i += 2; ++pending; continue; }
            if (c === "\\" && source[i + 1] === "\r" && source[i + 2] === "\n") {
                i += 3; ++pending; continue;
            }
            if (c === "\r") { ++i; continue; }
            if (c === "/" && source[i + 1] === "/") {
                while (i < n && source[i] !== "\n") ++i;
                continue;
            }
            if (c === "/" && source[i + 1] === "*") {
                i += 2;
                while (i < n && !(source[i] === "*" && source[i + 1] === "/")) {
                    if (source[i] === "\n") ++pending;
                    ++i;
                }
                if (i >= n) fail(0, "unterminated block comment");
                i += 2;
                cur += " ";
                continue;
            }
            if (c === "\n") { endLine(); ++i; continue; }
            cur += c;
            ++i;
        }
        endLine();
        return lines;
    }

    // Preprocessor tokens are coarser than GLSL tokens: identifiers, numbers,
    // whitespace, and single punctuation characters. Macro expansion only ever
    // needs to recognise identifiers.
    const PP_TOKEN = /([A-Za-z_][A-Za-z0-9_]*)|(\d+\.\d*(?:[eE][+-]?\d+)?[fF]?|\.\d+(?:[eE][+-]?\d+)?[fF]?|\d+[eE][+-]?\d+[fF]?|0[xX][0-9a-fA-F]+|\d+)|(\s+)|([\s\S])/g;

    function ppTokenize(text) {
        const tokens = [];
        let m;
        PP_TOKEN.lastIndex = 0;
        while ((m = PP_TOKEN.exec(text)) !== null) {
            if (m[1] !== undefined) tokens.push({ type: "id", value: m[1] });
            else if (m[2] !== undefined) tokens.push({ type: "num", value: m[2] });
            else if (m[3] !== undefined) tokens.push({ type: "ws", value: " " });
            else tokens.push({ type: "punct", value: m[4] });
        }
        return tokens;
    }

    function ppJoin(tokens) {
        let out = "";
        for (const t of tokens) out += t.value;
        return out;
    }

    const SUPPORTED_EXTENSIONS = new Set([
        "GL_ARB_texture_rectangle",
        "GL_ARB_draw_buffers",
        "GL_ARB_shader_texture_lod",
        "GL_ARB_texture_cube_map",
        "GL_ARB_shading_language_120",
        "GL_OES_standard_derivatives",
        "GL_EXT_gpu_shader4",
    ]);

    /* GLSL ES 1.00's (options.es: OpenGL ES 2.0's shaders, src/gles/glsl_es.js) */
    const ES_EXTENSIONS = new Set([
        "GL_OES_standard_derivatives",
        "GL_OES_EGL_image_external",
    ]);

    class Preprocessor {
        constructor(stage, options) {
            this.stage = stage;
            this.options = options || {};
            this.macros = new Map();
            this.version = 110;
            this.extensions = new Map();
            this.warnings = [];
            this.defineBuiltins();
        }

        defineBuiltins() {
            const objectMacro = (name, value) =>
                this.macros.set(name, { params: null, body: ppTokenize(value) });
            if (this.options.es) {
                // GLSL ES 1.00, 3.4: GL_ES, and the fragment stage's highp
                this.version = 100;
                objectMacro("__VERSION__", "100");
                objectMacro("GL_ES", "1");
                if (this.stage === "fragment") objectMacro("GL_FRAGMENT_PRECISION_HIGH", "1");
                for (const ext of ES_EXTENSIONS) objectMacro(ext, "1");
                for (const [name, value] of Object.entries(this.options.defines || {}))
                    objectMacro(name, String(value));
                return;
            }
            objectMacro("__VERSION__", "110");
            objectMacro("GL_ARB_texture_rectangle", "1");
            for (const [name, value] of Object.entries(this.options.defines || {}))
                objectMacro(name, String(value));
            // __LINE__ and __FILE__ are substituted dynamically, not stored.
        }

        run(source) {
            const lines = stripCommentsAndSplice(source);
            this.out = [];
            this.conditionals = [];
            this.line = 1;
            this.sawNonDirective = false;

            for (let i = 0; i < lines.length; ++i) {
                this.line = i + 1;
                const raw = lines[i];
                const trimmed = raw.replace(/^[ \t]+/, "");
                if (trimmed.startsWith("#")) {
                    this.directive(trimmed.slice(1).replace(/^[ \t]+/, ""));
                    this.out.push("");
                    continue;
                }
                if (!this.active()) { this.out.push(""); continue; }
                if (raw.trim().length) this.sawNonDirective = true;
                this.out.push(this.expandLine(raw));
            }
            if (this.conditionals.length) fail(this.line, "unterminated #if");
            return {
                text: this.out.join("\n"),
                version: this.version,
                extensions: this.extensions,
                warnings: this.warnings,
            };
        }

        active() {
            for (const c of this.conditionals) if (!c.active) return false;
            return true;
        }

        parentActive() {
            for (let i = 0; i < this.conditionals.length - 1; ++i)
                if (!this.conditionals[i].active) return false;
            return true;
        }

        pushConditional(value) {
            const active = this.active() && value;
            this.conditionals.push({ active, everActive: active, sawElse: false });
        }

        directive(body) {
            const m = /^([A-Za-z_][A-Za-z0-9_]*)?/.exec(body);
            const name = m[1] || "";
            const rest = body.slice(name.length);

            // Conditional directives are processed even inside an inactive
            // region -- otherwise nesting could not be tracked.
            switch (name) {
            case "":
                return;
            case "ifdef":
            case "ifndef": {
                if (!this.active()) { this.pushConditional(false); return; }
                const id = this.expectIdentifier(rest);
                const defined = this.macros.has(id);
                this.pushConditional(name === "ifdef" ? defined : !defined);
                return;
            }
            case "if": {
                if (!this.active()) { this.pushConditional(false); return; }
                this.pushConditional(this.evalCondition(rest) !== 0);
                return;
            }
            case "elif": {
                const top = this.conditionals[this.conditionals.length - 1];
                if (!top) fail(this.line, "#elif without #if");
                if (top.sawElse) fail(this.line, "#elif after #else");
                if (top.everActive || !this.parentActive()) {
                    top.active = false;
                } else {
                    top.active = this.evalCondition(rest) !== 0;
                    if (top.active) top.everActive = true;
                }
                return;
            }
            case "else": {
                const top = this.conditionals[this.conditionals.length - 1];
                if (!top) fail(this.line, "#else without #if");
                if (top.sawElse) fail(this.line, "duplicate #else");
                top.sawElse = true;
                top.active = !top.everActive && this.parentActive();
                if (top.active) top.everActive = true;
                return;
            }
            case "endif": {
                if (!this.conditionals.length) fail(this.line, "#endif without #if");
                this.conditionals.pop();
                return;
            }
            }

            if (!this.active()) return;

            switch (name) {
            case "define":
                return this.defineDirective(rest);
            case "undef":
                this.macros.delete(this.expectIdentifier(rest));
                return;
            case "version": {
                if (this.sawNonDirective)
                    fail(this.line, "#version must precede any other statement");
                const v = /\s*(\d+)/.exec(rest);
                if (!v) fail(this.line, "malformed #version");
                this.version = parseInt(v[1], 10);
                if (this.options.es && this.version !== 100)
                    fail(this.line, "#version " + this.version + " is not supported: GLSL ES 1.00 is #version 100");
                if (this.version > 120) {
                    // Refusing outright turns a shader we might still translate
                    // into a hard failure; the analyzer rejects what it really
                    // cannot represent, with a message naming the construct.
                    this.warnings.push("#version " + this.version +
                        " requested; translating as GLSL 1.20");
                }
                this.macros.set("__VERSION__",
                    { params: null, body: ppTokenize(String(this.version)) });
                return;
            }
            case "extension": {
                const e = /\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_]+)/.exec(rest);
                if (!e) fail(this.line, "malformed #extension");
                const [, ext, behavior] = e;
                const supported = this.options.es ? ES_EXTENSIONS : SUPPORTED_EXTENSIONS;
                if (behavior === "require" && ext !== "all" &&
                        !supported.has(ext))
                    fail(this.line, "extension not supported: " + ext);
                if (behavior === "enable" && ext !== "all" &&
                        !supported.has(ext))
                    this.warnings.push("extension not supported: " + ext);
                this.extensions.set(ext, behavior);
                return;
            }
            case "line": {
                const expanded = ppJoin(this.expand(ppTokenize(rest)));
                const l = /\s*(\d+)/.exec(expanded);
                if (l) this.line = parseInt(l[1], 10) - 1;
                return;
            }
            case "error":
                fail(this.line, "#error" + rest);
                return;
            case "pragma":
                return;   // optimize/debug/STDGL invariant: no effect here
            default:
                fail(this.line, "unknown directive #" + name);
            }
        }

        expectIdentifier(rest) {
            const m = /^\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(rest);
            if (!m) fail(this.line, "expected an identifier");
            return m[1];
        }

        defineDirective(rest) {
            const m = /^\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(rest);
            if (!m) fail(this.line, "malformed #define");
            const name = m[1];
            let tail = rest.slice(m.index + m[0].length);
            let params = null;
            if (tail.startsWith("(")) {
                const close = tail.indexOf(")");
                if (close < 0) fail(this.line, "malformed macro parameter list");
                const inside = tail.slice(1, close).trim();
                params = inside.length ? inside.split(",").map(s => s.trim()) : [];
                for (const p of params)
                    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(p))
                        fail(this.line, "malformed macro parameter: " + p);
                tail = tail.slice(close + 1);
            }
            this.macros.set(name, { params, body: ppTokenize(tail.trim()) });
        }

        expandLine(text) {
            return ppJoin(this.expand(ppTokenize(text)));
        }

        /*
         * Macro expansion with the usual no-self-reference rule: a macro is
         * hidden while its own body is being expanded, so `#define A A + 1`
         * terminates instead of recursing forever.
         */
        expand(tokens, hideSet) {
            const hidden = hideSet || new Set();
            const out = [];
            for (let i = 0; i < tokens.length; ++i) {
                const t = tokens[i];
                if (t.type !== "id") { out.push(t); continue; }
                if (t.value === "__LINE__") {
                    out.push({ type: "num", value: String(this.line) });
                    continue;
                }
                if (t.value === "__FILE__") {
                    out.push({ type: "num", value: "0" });
                    continue;
                }
                const macro = this.macros.get(t.value);
                if (!macro || hidden.has(t.value)) { out.push(t); continue; }

                if (macro.params === null) {
                    const nested = new Set(hidden);
                    nested.add(t.value);
                    out.push(...this.expand(macro.body, nested));
                    continue;
                }

                // A function-like macro only expands when the next non-space
                // token is '('. Otherwise the identifier stands for itself.
                let j = i + 1;
                while (j < tokens.length && tokens[j].type === "ws") ++j;
                if (j >= tokens.length || tokens[j].value !== "(") {
                    out.push(t);
                    continue;
                }
                let depth = 0;
                const args = [];
                let current = [];
                let k = j;
                for (; k < tokens.length; ++k) {
                    const tok = tokens[k];
                    if (tok.value === "(") {
                        ++depth;
                        if (depth === 1) continue;
                    } else if (tok.value === ")") {
                        --depth;
                        if (depth === 0) { args.push(current); break; }
                    } else if (tok.value === "," && depth === 1) {
                        args.push(current);
                        current = [];
                        continue;
                    }
                    current.push(tok);
                }
                if (depth !== 0) fail(this.line, "unterminated macro invocation");
                if (args.length === 1 && !macro.params.length &&
                        args[0].every(a => a.type === "ws"))
                    args.length = 0;
                if (args.length !== macro.params.length)
                    fail(this.line, "macro " + t.value + " expects " +
                        macro.params.length + " argument(s), got " + args.length);

                const substituted = [];
                const nested = new Set(hidden);
                nested.add(t.value);
                for (const bt of macro.body) {
                    if (bt.type === "id") {
                        const idx = macro.params.indexOf(bt.value);
                        if (idx >= 0) {
                            // Arguments are expanded in the caller's hide set,
                            // not the macro's, so a parameter naming the macro
                            // itself still expands once.
                            substituted.push({ type: "punct", value: "(" });
                            substituted.push(...this.expand(args[idx], hidden));
                            substituted.push({ type: "punct", value: ")" });
                            continue;
                        }
                    }
                    substituted.push(bt);
                }
                out.push(...this.expand(substituted, nested));
                i = k;
            }
            return out;
        }

        /*
         * #if expression evaluation. The operand grammar is C's, restricted to
         * integers: no floats, no sizeof, no casts. `defined X` and
         * `defined(X)` are resolved before macro expansion, per the spec.
         */
        evalCondition(text) {
            let tokens = ppTokenize(text).filter(t => t.type !== "ws");
            const resolved = [];
            for (let i = 0; i < tokens.length; ++i) {
                if (tokens[i].type === "id" && tokens[i].value === "defined") {
                    let j = i + 1;
                    let paren = false;
                    if (tokens[j] && tokens[j].value === "(") { paren = true; ++j; }
                    if (!tokens[j] || tokens[j].type !== "id")
                        fail(this.line, "malformed defined()");
                    const has = this.macros.has(tokens[j].value);
                    resolved.push({ type: "num", value: has ? "1" : "0" });
                    ++j;
                    if (paren) {
                        if (!tokens[j] || tokens[j].value !== ")")
                            fail(this.line, "malformed defined()");
                        ++j;
                    }
                    i = j - 1;
                    continue;
                }
                resolved.push(tokens[i]);
            }
            tokens = this.expand(resolved).filter(t => t.type !== "ws");
            // Any identifier surviving expansion evaluates to 0, per the C
            // rules the GLSL preprocessor inherits.
            const items = tokens.map(t =>
                t.type === "id" ? { type: "num", value: "0" } : t);
            return new PPExprParser(items, this.line).parse();
        }
    }

    const PP_TWO_CHAR = new Set(["&&", "||", "==", "!=", "<=", ">=", "<<", ">>"]);
    const PP_PRECEDENCE = {
        "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5,
        "==": 6, "!=": 6,
        "<": 7, ">": 7, "<=": 7, ">=": 7,
        "<<": 8, ">>": 8,
        "+": 9, "-": 9,
        "*": 10, "/": 10, "%": 10,
    };

    function applyPPBinary(op, a, b, line) {
        switch (op) {
        case "||": return (a || b) ? 1 : 0;
        case "&&": return (a && b) ? 1 : 0;
        case "|": return a | b;
        case "^": return a ^ b;
        case "&": return a & b;
        case "==": return a === b ? 1 : 0;
        case "!=": return a !== b ? 1 : 0;
        case "<": return a < b ? 1 : 0;
        case ">": return a > b ? 1 : 0;
        case "<=": return a <= b ? 1 : 0;
        case ">=": return a >= b ? 1 : 0;
        case "<<": return a << b;
        case ">>": return a >> b;
        case "+": return (a + b) | 0;
        case "-": return (a - b) | 0;
        case "*": return Math.imul(a, b);
        case "/":
            if (b === 0) fail(line, "division by zero in #if");
            return (a / b) | 0;
        case "%":
            if (b === 0) fail(line, "division by zero in #if");
            return (a % b) | 0;
        default: fail(line, "unsupported operator '" + op + "' in #if");
        }
    }

    /* A precedence-climbing evaluator for #if. Integer only. */
    class PPExprParser {
        constructor(tokens, line) {
            this.tokens = tokens;
            this.pos = 0;
            this.line = line;
        }
        peek() { return this.tokens[this.pos]; }
        next() { return this.tokens[this.pos++]; }
        expect(v) {
            const t = this.next();
            if (!t || t.value !== v) fail(this.line, "expected '" + v + "' in #if");
            return t;
        }
        parse() {
            if (!this.tokens.length) fail(this.line, "empty #if expression");
            const value = this.ternary();
            if (this.pos !== this.tokens.length)
                fail(this.line, "trailing tokens in #if expression");
            return value;
        }
        ternary() {
            const cond = this.binary(1);
            if (this.peek() && this.peek().value === "?") {
                this.next();
                const a = this.ternary();
                this.expect(":");
                const b = this.ternary();
                return cond ? a : b;
            }
            return cond;
        }
        binary(minPrec) {
            let left = this.unary();
            for (;;) {
                const t = this.peek();
                if (!t) break;
                let op = t.value;
                const t2 = this.tokens[this.pos + 1];
                if (t2 && PP_TWO_CHAR.has(op + t2.value)) op = op + t2.value;
                const prec = PP_PRECEDENCE[op];
                if (prec === undefined || prec < minPrec) break;
                this.pos += op.length > 1 ? 2 : 1;
                const right = this.binary(prec + 1);
                left = applyPPBinary(op, left, right, this.line);
            }
            return left;
        }
        unary() {
            const t = this.peek();
            if (!t) fail(this.line, "unexpected end of #if expression");
            if (t.value === "(") {
                this.next();
                const value = this.ternary();
                this.expect(")");
                return value;
            }
            if (t.value === "!") { this.next(); return this.unary() ? 0 : 1; }
            if (t.value === "-") { this.next(); return -this.unary(); }
            if (t.value === "+") { this.next(); return this.unary(); }
            if (t.value === "~") { this.next(); return ~this.unary(); }
            if (t.type === "num") {
                this.next();
                const text = t.value.replace(/[uUlL]+$/, "");
                if (/^0[xX]/.test(text)) return parseInt(text, 16) | 0;
                if (/^0[0-7]+$/.test(text)) return parseInt(text, 8) | 0;
                if (/[.eEfF]/.test(text))
                    fail(this.line, "floating point is not allowed in #if");
                return parseInt(text, 10) | 0;
            }
            fail(this.line, "unexpected token '" + t.value + "' in #if");
        }
    }

    /* ================================================================== */
    /* Lexer                                                              */
    /* ================================================================== */

    const KEYWORDS = new Set([
        "attribute", "const", "uniform", "varying", "invariant", "centroid",
        "break", "continue", "do", "for", "while", "if", "else",
        "in", "out", "inout", "true", "false", "discard", "return", "struct",
        "void", "bool", "int", "float",
        "vec2", "vec3", "vec4", "bvec2", "bvec3", "bvec4",
        "ivec2", "ivec3", "ivec4",
        "mat2", "mat3", "mat4",
        "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4",
        "mat4x2", "mat4x3", "mat4x4",
        "sampler1D", "sampler2D", "sampler3D", "samplerCube",
        "sampler1DShadow", "sampler2DShadow",
        "sampler2DRect", "sampler2DRectShadow", "samplerExternalOES",
        "lowp", "mediump", "highp", "precision",
    ]);

    /* Words the spec reserves. Using one is an error rather than a silently
     * accepted identifier, because a shader that says `switch` means it and the
     * result of ignoring that is wrong pixels rather than a message. */
    const RESERVED = new Set([
        "asm", "class", "union", "enum", "typedef", "template", "this", "packed",
        "goto", "switch", "default", "inline", "noinline", "volatile", "public",
        "static", "extern", "external", "interface", "long", "short", "double",
        "half", "fixed", "unsigned", "input", "output",
        "hvec2", "hvec3", "hvec4", "dvec2", "dvec3", "dvec4",
        "fvec2", "fvec3", "fvec4", "sampler3DRect",
        "sizeof", "cast", "namespace", "using", "flat", "noperspective",
    ]);

    // Longest first: the lexer takes the first match, so "<<=" must precede
    // "<<" which must precede "<".
    const OPERATORS = [
        "<<=", ">>=",
        "++", "--", "<=", ">=", "==", "!=", "&&", "||", "^^",
        "*=", "/=", "+=", "-=", "%=", "&=", "^=", "|=", "<<", ">>",
        "(", ")", "[", "]", "{", "}", ".", ",", ":", ";", "?",
        "+", "-", "*", "/", "%", "<", ">", "&", "^", "|", "~", "!", "=",
    ];

    function lex(text) {
        const tokens = [];
        let i = 0;
        let line = 1;
        const n = text.length;
        while (i < n) {
            const c = text[i];
            if (c === "\n") { ++line; ++i; continue; }
            if (c === " " || c === "\t" || c === "\r" || c === "\f" || c === "\v") {
                ++i;
                continue;
            }
            if (/[A-Za-z_]/.test(c)) {
                let j = i + 1;
                while (j < n && /[A-Za-z0-9_]/.test(text[j])) ++j;
                const word = text.slice(i, j);
                if (RESERVED.has(word))
                    fail(line, "'" + word + "' is a reserved word in GLSL 1.20");
                tokens.push({
                    type: KEYWORDS.has(word) ? "keyword" : "ident",
                    value: word, line,
                });
                i = j;
                continue;
            }
            if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(text[i + 1] || ""))) {
                const m = /^(?:0[xX][0-9a-fA-F]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fF]?|\d+)/.exec(text.slice(i));
                if (!m) fail(line, "malformed number");
                const raw = m[0];
                const isFloat = /[.eEfF]/.test(raw) && !/^0[xX]/.test(raw);
                let value;
                if (isFloat) {
                    value = parseFloat(raw.replace(/[fF]$/, ""));
                } else if (/^0[xX]/.test(raw)) {
                    value = parseInt(raw, 16) | 0;
                } else if (/^0[0-7]+$/.test(raw)) {
                    value = parseInt(raw, 8) | 0;
                } else {
                    value = parseInt(raw, 10) | 0;
                }
                tokens.push({ type: isFloat ? "float" : "int", value, raw, line });
                i += raw.length;
                continue;
            }
            let matched = null;
            for (const op of OPERATORS) {
                if (text.startsWith(op, i)) { matched = op; break; }
            }
            if (!matched) fail(line, "unexpected character '" + c + "'");
            tokens.push({ type: "op", value: matched, line });
            i += matched.length;
        }
        tokens.push({ type: "eof", value: "", line });
        return tokens;
    }

    /* ================================================================== */
    /* Parser                                                             */
    /* ================================================================== */

    const ASSIGN_OPS = new Set(["=", "+=", "-=", "*=", "/=", "%=",
        "<<=", ">>=", "&=", "^=", "|="]);

    // Binary precedence, lowest binding first. Bit operators are GLSL 1.30
    // and up, accepted here because EXT_gpu_shader4 shaders in the wild use
    // them and rejecting costs more than accepting.
    const BINARY_PRECEDENCE = {
        "||": 1, "^^": 2, "&&": 3,
        "|": 4, "^": 5, "&": 6,
        "==": 7, "!=": 7,
        "<": 8, ">": 8, "<=": 8, ">=": 8,
        "<<": 9, ">>": 9,
        "+": 10, "-": 10,
        "*": 11, "/": 11, "%": 11,
    };

    class Parser {
        constructor(tokens, stage) {
            this.tokens = tokens;
            this.pos = 0;
            this.stage = stage;
            this.structs = new Map();   // name -> type, so the statement parser
                                        // can tell a declaration from an
                                        // expression that starts with an ident
            this.constants = Object.create(null);
        }

        peek(offset) { return this.tokens[this.pos + (offset || 0)]; }
        get line() { return this.peek().line; }
        next() { return this.tokens[this.pos++]; }
        at(value) {
            const t = this.peek();
            return (t.type === "op" || t.type === "keyword") && t.value === value;
        }
        accept(value) {
            if (this.at(value)) { ++this.pos; return true; }
            return false;
        }
        expect(value) {
            if (!this.at(value))
                fail(this.line, "expected '" + value + "' but found '" +
                    String(this.peek().value) + "'");
            return this.next();
        }
        expectIdent() {
            const t = this.peek();
            if (t.type !== "ident")
                fail(t.line, "expected an identifier but found '" +
                    String(t.value) + "'");
            ++this.pos;
            return t.value;
        }

        parseProgram() {
            const decls = [];
            while (this.peek().type !== "eof") {
                const decl = this.parseExternalDeclaration();
                if (decl) decls.push(decl);
            }
            return { kind: "Program", decls };
        }

        /* `precision mediump float;` is GLSL ES syntax that desktop shaders
         * nonetheless carry when they are shared between the two. It has no
         * meaning at 1.20, so it parses and disappears. */
        parsePrecisionStatement() {
            this.expect("precision");
            const t = this.next();
            if (!["lowp", "mediump", "highp"].includes(t.value))
                fail(t.line, "expected a precision qualifier");
            this.parseTypeSpecifier();
            this.expect(";");
            return null;
        }

        parseExternalDeclaration() {
            if (this.at("precision")) return this.parsePrecisionStatement();
            if (this.accept(";")) return null;

            const line = this.line;
            const qualifiers = this.parseQualifiers();
            if (qualifiers.invariant && !qualifiers.storage && this.peek().type === "ident") {
                const names = [this.expectIdent()];
                while (this.accept(",")) names.push(this.expectIdent());
                this.expect(";");
                return { kind: "InvariantDecl", names, line };
            }

            // struct S { ... } [name];
            if (this.at("struct")) {
                const struct = this.parseStructSpecifier();
                if (this.at(";")) { this.next(); return { kind: "StructDecl", struct, line }; }
                const declarators = this.parseDeclaratorList(struct.type);
                this.expect(";");
                return { kind: "VarDecl", qualifiers, type: struct.type,
                         declarators, struct, line };
            }

            const type = this.parseTypeSpecifier();

            // A function definition or prototype.
            if (this.peek().type === "ident" && this.peek(1) &&
                    this.peek(1).type === "op" && this.peek(1).value === "(") {
                const name = this.expectIdent();
                const params = this.parseParameterList();
                if (this.accept(";"))
                    return { kind: "FunctionProto", name, returnType: type,
                             params, line };
                const body = this.parseBlock();
                return { kind: "FunctionDef", name, returnType: type, params,
                         body, line };
            }

            if (this.at(";")) { this.next(); return null; }  // bare type spec
            const declarators = this.parseDeclaratorList(type);
            if (qualifiers.storage === "const") for (const d of declarators) if (d.init) {
                const value = foldConstantInt(d.init, this.constants);
                if (value !== null) this.constants[d.name] = value;
            }
            this.expect(";");
            return { kind: "VarDecl", qualifiers, type, declarators, line };
        }

        parseQualifiers() {
            const q = { storage: null, invariant: false, centroid: false,
                        direction: null };
            for (;;) {
                if (this.accept("invariant")) { q.invariant = true; continue; }
                if (this.accept("centroid")) { q.centroid = true; continue; }
                if (this.at("lowp") || this.at("mediump") || this.at("highp")) {
                    this.next();
                    continue;
                }
                if (this.at("const") || this.at("attribute") || this.at("uniform") ||
                        this.at("varying")) {
                    if (q.storage)
                        fail(this.line, "conflicting storage qualifiers");
                    q.storage = this.next().value;
                    continue;
                }
                if (this.at("in") || this.at("out") || this.at("inout")) {
                    q.direction = this.next().value;
                    continue;
                }
                break;
            }
            return q;
        }

        parseStructSpecifier() {
            this.expect("struct");
            let name = null;
            if (this.peek().type === "ident") name = this.expectIdent();
            this.expect("{");
            const fields = [];
            while (!this.at("}")) {
                this.parseQualifiers();
                const fieldType = this.parseTypeSpecifier();
                do {
                    const fname = this.expectIdent();
                    let ftype = fieldType;
                    if (this.accept("[")) {
                        const size = this.parseConstantIndex();
                        this.expect("]");
                        ftype = arrayOf(fieldType, size);
                    }
                    fields.push({ name: fname, type: ftype });
                } while (this.accept(","));
                this.expect(";");
            }
            this.expect("}");
            if (!fields.length) fail(this.line, "struct has no members");
            const structName = name || ("_anon" + this.structs.size);
            const type = structType(structName, fields);
            this.structs.set(structName, type);
            return { name: structName, type, fields };
        }

        parseTypeSpecifier() {
            const t = this.peek();
            if (t.type === "keyword" && BASIC_TYPES[t.value]) {
                ++this.pos;
                return BASIC_TYPES[t.value];
            }
            if (t.type === "keyword" && t.value === "struct")
                return this.parseStructSpecifier().type;
            if (t.type === "ident" && this.structs.has(t.value)) {
                ++this.pos;
                return this.structs.get(t.value);
            }
            fail(t.line, "expected a type but found '" + String(t.value) + "'");
        }

        parseDeclaratorList(baseType) {
            const declarators = [];
            do {
                const line = this.line;
                const name = this.expectIdent();
                let type = baseType;
                if (this.accept("[")) {
                    // An unsized array declaration is legal only when an
                    // initializer or a later redeclaration supplies the size;
                    // GLSL 1.20 also allows `varying vec4 v[]` sized by use,
                    // which the analyzer resolves.
                    let size = -1;
                    if (!this.at("]")) size = this.parseConstantIndex();
                    this.expect("]");
                    type = arrayOf(baseType, size);
                }
                let init = null;
                if (this.accept("=")) init = this.parseAssignment();
                declarators.push({ name, type, init, line });
            } while (this.accept(","));
            return declarators;
        }

        /* Array sizes must be integral constant expressions. Full constant
         * folding happens in the analyzer, but the parser needs enough of it
         * to build the type -- so it evaluates the literal-and-operator subset
         * here and defers anything else. */
        parseConstantIndex() {
            const expr = this.parseAssignment();
            const value = foldConstantInt(expr, this.constants);
            if (value === null)
                fail(this.line, "array size must be an integral constant expression");
            if (value <= 0) fail(this.line, "array size must be positive");
            return value;
        }

        parseParameterList() {
            this.expect("(");
            const params = [];
            if (this.accept(")")) return params;
            if (this.at("void") && this.peek(1) && this.peek(1).value === ")") {
                this.next();
                this.expect(")");
                return params;
            }
            do {
                const line = this.line;
                const q = this.parseQualifiers();
                const baseType = this.parseTypeSpecifier();
                let name = null;
                let type = baseType;
                if (this.peek().type === "ident") name = this.expectIdent();
                if (this.accept("[")) {
                    const size = this.parseConstantIndex();
                    this.expect("]");
                    type = arrayOf(baseType, size);
                }
                params.push({
                    name, type, line,
                    direction: q.direction || "in",
                    isConst: q.storage === "const",
                });
            } while (this.accept(","));
            this.expect(")");
            return params;
        }

        parseBlock() {
            const line = this.line;
            this.expect("{");
            const previous = this.constants;this.constants = Object.create(previous);
            const stmts = [];
            while (!this.at("}")) {
                if (this.peek().type === "eof") fail(line, "unterminated block");
                const s = this.parseStatement();
                if (s) stmts.push(s);
            }
            this.expect("}");
            this.constants = previous;
            return { kind: "Block", stmts, line };
        }

        startsDeclaration() {
            const t = this.peek();
            if (t.type === "keyword") {
                if (BASIC_TYPES[t.value]) return true;
                if (["struct", "const", "attribute", "uniform", "varying",
                     "invariant", "centroid", "lowp", "mediump", "highp"].includes(t.value))
                    return true;
                return false;
            }
            // A declaration whose type is a user struct: `S s;` versus the
            // expression statement `S;` (which is meaningless) or `s.x = 1;`.
            if (t.type === "ident" && this.structs.has(t.value)) {
                const nt = this.peek(1);
                return !!nt && nt.type === "ident";
            }
            return false;
        }

        parseStatement() {
            const t = this.peek();
            if (t.type === "op") {
                if (t.value === "{") return this.parseBlock();
                if (t.value === ";") { this.next(); return null; }
            }
            if (t.type === "keyword") {
                switch (t.value) {
                case "if": return this.parseIf();
                case "for": return this.parseFor();
                case "while": return this.parseWhile();
                case "do": return this.parseDoWhile();
                case "return": {
                    const line = this.line;
                    this.next();
                    let expr = null;
                    if (!this.at(";")) expr = this.parseExpression();
                    this.expect(";");
                    return { kind: "Return", expr, line };
                }
                case "break": {
                    const line = this.line;
                    this.next();
                    this.expect(";");
                    return { kind: "Break", line };
                }
                case "continue": {
                    const line = this.line;
                    this.next();
                    this.expect(";");
                    return { kind: "Continue", line };
                }
                case "discard": {
                    const line = this.line;
                    if (this.stage !== "fragment")
                        fail(line, "discard is only allowed in a fragment shader");
                    this.next();
                    this.expect(";");
                    return { kind: "Discard", line };
                }
                case "precision":
                    return this.parsePrecisionStatement();
                }
            }
            if (this.startsDeclaration()) return this.parseDeclarationStatement();
            const line = this.line;
            const expr = this.parseExpression();
            this.expect(";");
            return { kind: "ExprStmt", expr, line };
        }

        parseDeclarationStatement() {
            const line = this.line;
            const qualifiers = this.parseQualifiers();
            if (this.at("struct")) {
                const struct = this.parseStructSpecifier();
                if (this.accept(";")) return { kind: "StructDecl", struct, line };
                const declarators = this.parseDeclaratorList(struct.type);
                this.expect(";");
                return { kind: "VarDecl", qualifiers, type: struct.type,
                         declarators, struct, line };
            }
            const type = this.parseTypeSpecifier();
            const declarators = this.parseDeclaratorList(type);
            if (qualifiers.storage === "const") for (const d of declarators) if (d.init) {
                const value = foldConstantInt(d.init, this.constants);
                if (value !== null) this.constants[d.name] = value;
            }
            this.expect(";");
            return { kind: "VarDecl", qualifiers, type, declarators, line };
        }

        parseIf() {
            const line = this.line;
            this.expect("if");
            this.expect("(");
            const cond = this.parseExpression();
            this.expect(")");
            const then = this.parseStatement();
            let otherwise = null;
            if (this.accept("else")) otherwise = this.parseStatement();
            return { kind: "If", cond, then, otherwise, line };
        }

        parseWhile() {
            const line = this.line;
            this.expect("while");
            this.expect("(");
            const cond = this.parseExpression();
            this.expect(")");
            const body = this.parseStatement();
            return { kind: "While", cond, body, line };
        }

        parseDoWhile() {
            const line = this.line;
            this.expect("do");
            const body = this.parseStatement();
            this.expect("while");
            this.expect("(");
            const cond = this.parseExpression();
            this.expect(")");
            this.expect(";");
            return { kind: "DoWhile", body, cond, line };
        }

        parseFor() {
            const line = this.line;
            this.expect("for");
            this.expect("(");
            let init = null;
            if (this.at(";")) this.next();
            else if (this.startsDeclaration()) init = this.parseDeclarationStatement();
            else {
                init = { kind: "ExprStmt", expr: this.parseExpression(),
                         line: this.line };
                this.expect(";");
            }
            let cond = null;
            if (!this.at(";")) cond = this.parseExpression();
            this.expect(";");
            let step = null;
            if (!this.at(")")) step = this.parseExpression();
            this.expect(")");
            const body = this.parseStatement();
            return { kind: "For", init, cond, step, body, line };
        }

        /* ---- expressions ---- */

        parseExpression() {
            let expr = this.parseAssignment();
            while (this.at(",")) {
                const line = this.line;
                this.next();
                const right = this.parseAssignment();
                expr = { kind: "Sequence", left: expr, right, line };
            }
            return expr;
        }

        parseAssignment() {
            const start = this.pos;
            const left = this.parseConditional();
            const t = this.peek();
            if (t.type === "op" && ASSIGN_OPS.has(t.value)) {
                const line = t.line;
                this.next();
                const value = this.parseAssignment();
                return { kind: "Assign", op: t.value, target: left, value, line };
            }
            void start;
            return left;
        }

        parseConditional() {
            const cond = this.parseBinary(1);
            if (this.at("?")) {
                const line = this.line;
                this.next();
                const then = this.parseExpression();
                this.expect(":");
                const otherwise = this.parseAssignment();
                return { kind: "Conditional", cond, then, otherwise, line };
            }
            return cond;
        }

        parseBinary(minPrec) {
            let left = this.parseUnary();
            for (;;) {
                const t = this.peek();
                if (t.type !== "op") break;
                const prec = BINARY_PRECEDENCE[t.value];
                if (prec === undefined || prec < minPrec) break;
                const line = t.line;
                this.next();
                const right = this.parseBinary(prec + 1);
                left = { kind: "Binary", op: t.value, left, right, line };
            }
            return left;
        }

        parseUnary() {
            const t = this.peek();
            if (t.type === "op" &&
                    ["++", "--", "+", "-", "!", "~"].includes(t.value)) {
                const line = t.line;
                this.next();
                const operand = this.parseUnary();
                return { kind: "Unary", op: t.value, operand, prefix: true, line };
            }
            return this.parsePostfix();
        }

        parsePostfix() {
            let expr = this.parsePrimary();
            for (;;) {
                const t = this.peek();
                if (t.type !== "op") break;
                if (t.value === "[") {
                    const line = t.line;
                    this.next();
                    const index = this.parseExpression();
                    this.expect("]");
                    expr = { kind: "Index", object: expr, index, line };
                    continue;
                }
                if (t.value === ".") {
                    const line = t.line;
                    this.next();
                    const name = this.expectIdent();
                    if (name === "length" && this.accept("(")) {
                        this.expect(")");expr = { kind: "ArrayLength", object: expr, line };continue;
                    }
                    expr = { kind: "Field", object: expr, name, line };
                    continue;
                }
                if (t.value === "++" || t.value === "--") {
                    const line = t.line;
                    this.next();
                    expr = { kind: "Unary", op: t.value, operand: expr,
                             prefix: false, line };
                    continue;
                }
                break;
            }
            return expr;
        }

        parsePrimary() {
            const t = this.peek();
            if (t.type === "int") { this.next(); return { kind: "IntLit", value: t.value, line: t.line }; }
            if (t.type === "float") { this.next(); return { kind: "FloatLit", value: t.value, line: t.line }; }
            if (t.type === "keyword" && (t.value === "true" || t.value === "false")) {
                this.next();
                return { kind: "BoolLit", value: t.value === "true", line: t.line };
            }
            if (t.type === "op" && t.value === "(") {
                this.next();
                const expr = this.parseExpression();
                this.expect(")");
                return expr;
            }
            // Constructors read as calls whose callee names a type.
            if (t.type === "keyword" && BASIC_TYPES[t.value]) {
                this.next();
                let constructType = BASIC_TYPES[t.value];
                if (this.accept("[")) {
                    const size = this.at("]") ? -1 : this.parseConstantIndex();
                    this.expect("]");constructType = arrayOf(constructType, size);
                }
                const args = this.parseArguments();
                if (constructType.kind === "array" && constructType.size < 0) constructType = arrayOf(constructType.elem, args.length);
                return { kind: "Call", callee: t.value, args,
                         constructType, line: t.line };
            }
            if (t.type === "ident") {
                this.next();
                if (this.at("(")) {
                    const args = this.parseArguments();
                    const structT = this.structs.get(t.value);
                    return { kind: "Call", callee: t.value, args,
                             constructType: structT || null, line: t.line };
                }
                return { kind: "Ident", name: t.value, line: t.line };
            }
            fail(t.line, "unexpected token '" + String(t.value) + "'");
        }

        parseArguments() {
            this.expect("(");
            const args = [];
            if (this.accept(")")) return args;
            if (this.at("void") && this.peek(1) && this.peek(1).value === ")") {
                this.next();
                this.expect(")");
                return args;
            }
            do {
                args.push(this.parseAssignment());
            } while (this.accept(","));
            this.expect(")");
            return args;
        }
    }

    /* Enough constant folding to size an array at parse time. Anything more
     * involved (a `const int` declared earlier) is resolved by the analyzer,
     * which re-checks every array size against the full constant environment. */
    function foldConstantInt(expr, env) {
        switch (expr.kind) {
        case "IntLit": return expr.value;
        case "Ident": {
            if (env && expr.name in env) {
                const v = env[expr.name];
                return typeof v === "number" ? (v | 0) : null;
            }
            return null;
        }
        case "Unary": {
            const v = foldConstantInt(expr.operand, env);
            if (v === null) return null;
            if (expr.op === "-") return -v;
            if (expr.op === "+") return v;
            if (expr.op === "~") return ~v;
            return null;
        }
        case "Binary": {
            const a = foldConstantInt(expr.left, env);
            const b = foldConstantInt(expr.right, env);
            if (a === null || b === null) return null;
            switch (expr.op) {
            case "+": return (a + b) | 0;
            case "-": return (a - b) | 0;
            case "*": return Math.imul(a, b);
            case "/": return b === 0 ? null : (a / b) | 0;
            case "%": return b === 0 ? null : (a % b) | 0;
            case "<<": return a << b;
            case ">>": return a >> b;
            case "&": return a & b;
            case "|": return a | b;
            case "^": return a ^ b;
            default: return null;
            }
        }
        default:
            return null;
        }
    }

    /* ================================================================== */
    /* Built-in functions                                                 */
    /* ================================================================== */

    /*
     * Overloads are stored expanded: every genType entry becomes four concrete
     * signatures at module load. Resolution is then the same linear scan for
     * built-ins and user functions, which matters because GLSL's rule -- exact
     * match first, then a *unique* implicitly-convertible match -- has to be
     * applied identically to both, or a call like min(1, 2.0) resolves
     * differently depending on who declared it.
     */
    const F = BASIC_TYPES;
    const GEN_F = [F.float, F.vec2, F.vec3, F.vec4];
    const GEN_I = [F.int, F.ivec2, F.ivec3, F.ivec4];
    const MATRICES = [F.mat2, F.mat3, F.mat4, F.mat2x3, F.mat2x4,
        F.mat3x2, F.mat3x4, F.mat4x2, F.mat4x3];

    const builtinFunctions = new Map();   // name -> [overload]

    function addOverload(name, params, ret, emit, flags) {
        let list = builtinFunctions.get(name);
        if (!list) { list = []; builtinFunctions.set(name, list); }
        list.push({ name, params, ret, emit, ...(flags || {}) });
    }

    /* Straight rename: same argument order, same semantics. */
    function simple(wgslName) {
        return args => wgslName + "(" + args.join(", ") + ")";
    }

    function unaryGen(name, wgslName) {
        for (const t of GEN_F) addOverload(name, [t], t, simple(wgslName || name));
    }
    function binaryGen(name, wgslName) {
        for (const t of GEN_F) addOverload(name, [t, t], t, simple(wgslName || name));
    }
    function ternaryGen(name, wgslName) {
        for (const t of GEN_F)
            addOverload(name, [t, t, t], t, simple(wgslName || name));
    }

    unaryGen("radians"); unaryGen("degrees");
    unaryGen("sin"); unaryGen("cos"); unaryGen("tan");
    unaryGen("asin"); unaryGen("acos");
    unaryGen("atan");
    binaryGen("atan", "atan2");
    unaryGen("exp"); unaryGen("log"); unaryGen("exp2"); unaryGen("log2");
    unaryGen("sqrt");
    unaryGen("inversesqrt", "inverseSqrt");
    unaryGen("abs"); unaryGen("sign"); unaryGen("floor"); unaryGen("ceil");
    unaryGen("fract");
    binaryGen("pow");
    binaryGen("min"); binaryGen("max");
    ternaryGen("clamp");
    unaryGen("dFdx", "dpdx"); unaryGen("dFdy", "dpdy"); unaryGen("fwidth");

    /* GLSL's mod() is floor-based; WGSL's % is trunc-based, so they differ for
     * negative operands -- which is exactly where texture-coordinate wrapping
     * code lives. Emitting the definition rather than '%' is not pedantry. */
    const emitMod = args =>
        "(" + args[0] + " - " + args[1] + " * floor(" + args[0] + " / " + args[1] + "))";
    binaryGen("mod", null);
    builtinFunctions.get("mod").forEach(o => { o.emit = emitMod; });

    /* The (genType, float) forms: the scalar is broadcast. */
    for (let i = 1; i < GEN_F.length; ++i) {
        const t = GEN_F[i];
        const splat = s => typeToWGSL(t) + "(" + s + ")";
        addOverload("mod", [t, F.float], t,
            args => emitMod([args[0], splat(args[1])]));
        addOverload("min", [t, F.float], t,
            args => "min(" + args[0] + ", " + splat(args[1]) + ")");
        addOverload("max", [t, F.float], t,
            args => "max(" + args[0] + ", " + splat(args[1]) + ")");
        addOverload("clamp", [t, F.float, F.float], t,
            args => "clamp(" + args[0] + ", " + splat(args[1]) + ", " +
                splat(args[2]) + ")");
        addOverload("mix", [t, t, F.float], t,
            args => "mix(" + args[0] + ", " + args[1] + ", " + splat(args[2]) + ")");
        addOverload("step", [F.float, t], t,
            args => "step(" + splat(args[0]) + ", " + args[1] + ")");
        addOverload("smoothstep", [F.float, F.float, t], t,
            args => "smoothstep(" + splat(args[0]) + ", " + splat(args[1]) +
                ", " + args[2] + ")");
    }
    ternaryGen("mix");
    binaryGen("step");
    ternaryGen("smoothstep");

    /* Integer min/max/abs/sign/clamp come from EXT_gpu_shader4 and GLSL 1.30,
     * but shaders that declare the extension use them and they cost nothing. */
    for (const t of GEN_I) {
        addOverload("abs", [t], t, simple("abs"));
        addOverload("sign", [t], t, simple("sign"));
        addOverload("min", [t, t], t, simple("min"));
        addOverload("max", [t, t], t, simple("max"));
        addOverload("clamp", [t, t, t], t, simple("clamp"));
    }

    /* Geometric */
    for (const t of GEN_F) {
        addOverload("length", [t], F.float, simple("length"));
        addOverload("distance", [t, t], F.float, simple("distance"));
        addOverload("dot", [t, t], F.float, simple("dot"));
        addOverload("normalize", [t], t, simple("normalize"));
        addOverload("faceforward", [t, t, t], t, simple("faceForward"));
        addOverload("reflect", [t, t], t, simple("reflect"));
        addOverload("refract", [t, t, F.float], t, simple("refract"));
    }
    addOverload("cross", [F.vec3, F.vec3], F.vec3, simple("cross"));

    /* Matrix */
    for (const m of MATRICES) {
        addOverload("matrixCompMult", [m, m], m, args => {
            // WGSL has no componentwise matrix multiply; expanding it column
            // by column is the whole implementation.
            const cols = [];
            for (let c = 0; c < m.cols; ++c)
                cols.push("(" + args[0] + "[" + c + "] * " + args[1] + "[" + c + "])");
            return typeToWGSL(m) + "(" + cols.join(", ") + ")";
        });
        addOverload("transpose", [m], mat(m.rows, m.cols), simple("transpose"));
    }
    for (let c = 2; c <= 4; ++c) {
        for (let r = 2; r <= 4; ++r) {
            addOverload("outerProduct", [vec("float", r), vec("float", c)],
                mat(c, r), args => {
                    const cols = [];
                    for (let i = 0; i < c; ++i)
                        cols.push("(" + args[0] + " * " + args[1] + "[" + i + "])");
                    return "mat" + c + "x" + r + "<f32>(" + cols.join(", ") + ")";
                });
        }
    }

    /* Vector relational. WGSL spells these as operators on vectors, which
     * already produce vecN<bool>. */
    function relational(name, op) {
        for (let n = 2; n <= 4; ++n) {
            for (const base of ["float", "int"]) {
                const t = vec(base, n);
                addOverload(name, [t, t], vec("bool", n),
                    args => "(" + args[0] + " " + op + " " + args[1] + ")");
            }
        }
    }
    relational("lessThan", "<");
    relational("lessThanEqual", "<=");
    relational("greaterThan", ">");
    relational("greaterThanEqual", ">=");
    relational("equal", "==");
    relational("notEqual", "!=");
    for (let n = 2; n <= 4; ++n) {
        const bt = vec("bool", n);
        addOverload("equal", [bt, bt], bt, args => "(" + args[0] + " == " + args[1] + ")");
        addOverload("notEqual", [bt, bt], bt, args => "(" + args[0] + " != " + args[1] + ")");
        addOverload("any", [bt], F.bool, simple("any"));
        addOverload("all", [bt], F.bool, simple("all"));
        addOverload("not", [bt], bt, args => "(!" + args[0] + ")");
    }

    /*
     * noise() returns 0. The GLSL spec permits an implementation to return
     * zero and every desktop driver does, so a shader that genuinely depends
     * on it is already broken on real hardware. Deviation D-11.
     */
    for (const t of GEN_F) {
        addOverload("noise1", [t], F.float, () => "0.0", { noise: true });
        addOverload("noise2", [t], F.vec2, () => "vec2<f32>(0.0)", { noise: true });
        addOverload("noise3", [t], F.vec3, () => "vec3<f32>(0.0)", { noise: true });
        addOverload("noise4", [t], F.vec4, () => "vec4<f32>(0.0)", { noise: true });
    }

    /* ---- texture lookups ---------------------------------------------- */

    /*
     * A GLSL sampler is one value; WGSL needs a texture handle and a sampler
     * handle. Argument 0 of every lookup therefore expands to two WGSL
     * arguments, which ctx.samplerPair() produces. Shadow samplers expand to a
     * texture_depth_* plus a sampler_comparison.
     *
     * Coordinate handling per sampler dimension:
     *   1D      implemented as a height-1 2D texture, so u -> vec2(u, 0.5)
     *   2DRect  unnormalised coordinates -> divided by textureDimensions()
     *   Proj    the projective divide happens here, not in the sampler
     */
    function projSwizzle(size) {
        return ["x", "xy", "xyz"][size - 1];
    }

    function textureLookup(spec) {
        const { name, samplerType, coordType, bias, lod, proj } = spec;
        const params = [samplerType, coordType];
        if (bias || lod) params.push(F.float);
        addOverload(name, params, F.vec4, (args, node, ctx) => {
            const pair = ctx.samplerPair(node.args[0]);
            // A cube lookup takes three coordinates like a 3D one; getting
            // this wrong truncates the direction vector to .xy, which is
            // exactly what every cubemap shader in the Cube 2 corpus caught.
            const dims = (samplerType.dim === "3D" || samplerType.dim === "Cube") ?
                3 : (samplerType.dim === "1D" ? 1 : 2);
            const wComponent = coordType.size === 4 ? "w" : "z";
            let coord = args[1];
            if (proj) {
                coord = "(" + args[1] + "." + projSwizzle(dims) + " / " +
                    args[1] + "." + wComponent + ")";
            } else if (samplerType.shadow || coordType.size > dims) {
                // shadow2D takes vec3 for a 2D lookup: the extra component is
                // the depth reference, not a coordinate.
                coord = args[1] + "." + projSwizzle(dims);
            }
            if (samplerType.dim === "1D")
                coord = "vec2<f32>(" + coord + ", 0.5)";
            else if (samplerType.dim === "2DRect")
                coord = "(" + coord + " / vec2<f32>(textureDimensions(" +
                    pair.texture + ")))";

            if (samplerType.shadow) {
                const refExpr = proj ?
                    "(" + args[1] + ".z / " + args[1] + "." + wComponent + ")" :
                    args[1] + ".z";
                if (ctx.desktopSample) return "vec4<f32>(" + ctx.desktopSample(pair, coord, lod ? args[2] : "0.0", bias ? args[2] : "0.0", refExpr, !!lod) + ")";
                const call = lod ?
                    "textureSampleCompareLevel(" + pair.texture + ", " +
                        pair.sampler + ", " + coord + ", " + refExpr + ")" :
                    ctx.compareSample(pair, coord, refExpr);
                return "vec4<f32>(" + call + ")";
            }
            if (lod && ctx.desktopSample) return ctx.desktopSample(pair, coord, args[2], "0.0", null, true);
            if (lod)
                return "textureSampleLevel(" + pair.texture + ", " +
                    pair.sampler + ", " + coord + ", " + args[2] + ")";
            return ctx.sample(pair, coord, bias ? args[2] : null);
        }, { texture: true, lod: !!lod, proj: !!proj });
    }

    (function registerTextureLookups() {
        const combos = [
            { prefix: "texture1D", st: F.sampler1D, coord: F.float, projCoords: [F.vec2, F.vec4] },
            { prefix: "texture2D", st: F.sampler2D, coord: F.vec2, projCoords: [F.vec3, F.vec4] },
            { prefix: "texture3D", st: F.sampler3D, coord: F.vec3, projCoords: [F.vec4] },
            { prefix: "textureCube", st: F.samplerCube, coord: F.vec3, projCoords: [] },
            { prefix: "texture2DRect", st: F.sampler2DRect, coord: F.vec2, projCoords: [F.vec3, F.vec4] },
        ];
        for (const c of combos) {
            textureLookup({ name: c.prefix, samplerType: c.st, coordType: c.coord });
            textureLookup({ name: c.prefix, samplerType: c.st, coordType: c.coord, bias: true });
            textureLookup({ name: c.prefix + "Lod", samplerType: c.st, coordType: c.coord, lod: true });
            for (const pc of c.projCoords) {
                textureLookup({ name: c.prefix + "Proj", samplerType: c.st, coordType: pc, proj: true });
                textureLookup({ name: c.prefix + "Proj", samplerType: c.st, coordType: pc, proj: true, bias: true });
                textureLookup({ name: c.prefix + "ProjLod", samplerType: c.st, coordType: pc, proj: true, lod: true });
            }
        }
        textureLookup({ name: "shadow1D", samplerType: F.sampler1DShadow, coordType: F.vec3 });
        textureLookup({ name: "shadow1D", samplerType: F.sampler1DShadow, coordType: F.vec3, bias: true });
        textureLookup({ name: "shadow1DLod", samplerType: F.sampler1DShadow, coordType: F.vec3, lod: true });
        textureLookup({ name: "shadow1DProj", samplerType: F.sampler1DShadow, coordType: F.vec4, proj: true });
        textureLookup({ name: "shadow1DProj", samplerType: F.sampler1DShadow, coordType: F.vec4, proj: true, bias: true });
        textureLookup({ name: "shadow2D", samplerType: F.sampler2DShadow, coordType: F.vec3 });
        textureLookup({ name: "shadow2D", samplerType: F.sampler2DShadow, coordType: F.vec3, bias: true });
        textureLookup({ name: "shadow2DLod", samplerType: F.sampler2DShadow, coordType: F.vec3, lod: true });
        textureLookup({ name: "shadow2DProj", samplerType: F.sampler2DShadow, coordType: F.vec4, proj: true });
        textureLookup({ name: "shadow2DProj", samplerType: F.sampler2DShadow, coordType: F.vec4, proj: true, bias: true });
        textureLookup({ name: "shadow2DProjLod", samplerType: F.sampler2DShadow, coordType: F.vec4, proj: true, lod: true });
        textureLookup({ name: "shadow2DRect", samplerType: F.sampler2DRectShadow, coordType: F.vec3 });
        textureLookup({ name: "shadow2DRectProj", samplerType: F.sampler2DRectShadow, coordType: F.vec4, proj: true });
    })();

    /* ftransform() must be bit-identical to the fixed-function transform, or a
     * depth pre-pass drawn with the fixed pipeline and a lighting pass drawn
     * with a shader will z-fight. Both go through the same expression. */
    addOverload("ftransform", [], F.vec4,
        (args, node, ctx) => ctx.ftransform(),
        { vertexOnly: true, usesState: ["mvp"] });

    /* ================================================================== */
    /* Built-in variables and compatibility state                          */
    /* ================================================================== */

    const GL_LIGHT_SOURCE = structType("gl_LightSourceParameters", [
        { name: "ambient", type: F.vec4 },
        { name: "diffuse", type: F.vec4 },
        { name: "specular", type: F.vec4 },
        { name: "position", type: F.vec4 },
        { name: "halfVector", type: F.vec4 },
        { name: "spotDirection", type: F.vec3 },
        { name: "spotExponent", type: F.float },
        { name: "spotCutoff", type: F.float },
        { name: "spotCosCutoff", type: F.float },
        { name: "constantAttenuation", type: F.float },
        { name: "linearAttenuation", type: F.float },
        { name: "quadraticAttenuation", type: F.float },
    ]);

    const GL_MATERIAL = structType("gl_MaterialParameters", [
        { name: "emission", type: F.vec4 },
        { name: "ambient", type: F.vec4 },
        { name: "diffuse", type: F.vec4 },
        { name: "specular", type: F.vec4 },
        { name: "shininess", type: F.float },
    ]);

    const GL_LIGHT_PRODUCTS = structType("gl_LightProducts", [
        { name: "ambient", type: F.vec4 },
        { name: "diffuse", type: F.vec4 },
        { name: "specular", type: F.vec4 },
    ]);

    const GL_LIGHT_MODEL = structType("gl_LightModelParameters", [
        { name: "ambient", type: F.vec4 },
    ]);

    const GL_LIGHT_MODEL_PRODUCTS = structType("gl_LightModelProducts", [
        { name: "sceneColor", type: F.vec4 },
    ]);

    const GL_FOG = structType("gl_FogParameters", [
        { name: "color", type: F.vec4 },
        { name: "density", type: F.float },
        { name: "start", type: F.float },
        { name: "end", type: F.float },
        { name: "scale", type: F.float },
    ]);

    const GL_DEPTH_RANGE = structType("gl_DepthRangeParameters", [
        { name: "near", type: F.float },
        { name: "far", type: F.float },
        { name: "diff", type: F.float },
    ]);

    const GL_POINT = structType("gl_PointParameters", [
        { name: "size", type: F.float },
        { name: "sizeMin", type: F.float },
        { name: "sizeMax", type: F.float },
        { name: "fadeThresholdSize", type: F.float },
        { name: "distanceConstantAttenuation", type: F.float },
        { name: "distanceLinearAttenuation", type: F.float },
        { name: "distanceQuadraticAttenuation", type: F.float },
    ]);

    const MAX_TEXTURE_COORDS = stateLayout.MAX_TEXTURE_COORDS;
    const MAX_LIGHTS = stateLayout.MAX_LIGHTS;
    const MAX_CLIP_PLANES = stateLayout.MAX_CLIP_PLANES;
    const MAX_DRAW_BUFFERS = 8;

    /*
     * Vertex attribute slots for the compatibility attributes.
     *
     * This is NVIDIA's classic aliasing layout, and it is not a free choice:
     * shaders and engines of this era assume it (gl_Vertex is attribute 0,
     * gl_MultiTexCoord0 is 8), and so does every tool that ever debugged them.
     * Generic attributes are assigned around it; see assignAttributeLocations.
     */
    // the vertex stage's colour outputs, which GL clamps
    const COLOR_OUTPUTS = new Set(["gl_FrontColor", "gl_BackColor", "gl_FrontSecondaryColor", "gl_BackSecondaryColor"]);

    const COMPAT_ATTRIBUTE_LOCATIONS = {
        gl_Vertex: 0,
        gl_Normal: 2,
        gl_Color: 3,
        gl_SecondaryColor: 4,
        gl_FogCoord: 5,
        gl_MultiTexCoord0: 8, gl_MultiTexCoord1: 9, gl_MultiTexCoord2: 10,
        gl_MultiTexCoord3: 11, gl_MultiTexCoord4: 12, gl_MultiTexCoord5: 13,
        gl_MultiTexCoord6: 14, gl_MultiTexCoord7: 15,
    };

    const COMPAT_ATTRIBUTE_TYPES = {
        gl_Vertex: F.vec4, gl_Normal: F.vec3, gl_Color: F.vec4,
        gl_SecondaryColor: F.vec4, gl_FogCoord: F.float,
        gl_MultiTexCoord0: F.vec4, gl_MultiTexCoord1: F.vec4,
        gl_MultiTexCoord2: F.vec4, gl_MultiTexCoord3: F.vec4,
        gl_MultiTexCoord4: F.vec4, gl_MultiTexCoord5: F.vec4,
        gl_MultiTexCoord6: F.vec4, gl_MultiTexCoord7: F.vec4,
    };

    /* Vertex outputs that are really varyings, and their fragment counterparts.
     * gl_TexCoord is an array in both stages: dynamic indexing of it is common
     * enough (a loop over texture units) that it cannot be flattened away. */
    const VERTEX_VARYING_OUTPUTS = {
        gl_FrontColor: F.vec4,
        gl_BackColor: F.vec4,
        gl_FrontSecondaryColor: F.vec4,
        gl_BackSecondaryColor: F.vec4,
        gl_TexCoord: arrayOf(F.vec4, MAX_TEXTURE_COORDS),
        gl_FogFragCoord: F.float,
    };

    const FRAGMENT_VARYING_INPUTS = {
        gl_Color: F.vec4,
        gl_SecondaryColor: F.vec4,
        gl_TexCoord: arrayOf(F.vec4, MAX_TEXTURE_COORDS),
        gl_FogFragCoord: F.float,
    };

    /* Which vertex output feeds which fragment input. Two-sided lighting makes
     * this a choice at the fragment end rather than a wire: gl_Color reads
     * gl_FrontColor or gl_BackColor depending on gl_FrontFacing. */
    const VARYING_LINKAGE = {
        gl_Color: { front: "gl_FrontColor", back: "gl_BackColor" },
        gl_SecondaryColor: { front: "gl_FrontSecondaryColor", back: "gl_BackSecondaryColor" },
        gl_TexCoord: { front: "gl_TexCoord", back: null },
        gl_FogFragCoord: { front: "gl_FogFragCoord", back: null },
    };

    /* GLSL name -> how to reach it inside the shared GLState block. `expr` is
     * used when the GLSL type maps onto one WGSL member with the same shape;
     * `members` handles the cases where GL packs several scalars into a vec4. */
    const STATE_UNIFORMS = {
        gl_ModelViewMatrix: { field: "modelview", type: F.mat4, expr: "glState.modelview" },
        gl_ProjectionMatrix: { field: "projection", type: F.mat4, expr: "glState.projection" },
        gl_ModelViewProjectionMatrix: { field: "mvp", type: F.mat4, expr: "glState.mvp" },
        gl_NormalMatrix: { field: "normalMatrix", type: F.mat3, expr: "glState.normalMatrix" },
        gl_ModelViewMatrixInverse: { field: "modelviewInverse", type: F.mat4, expr: "glState.modelviewInverse" },
        gl_ModelViewMatrixTranspose: { field: "modelviewTranspose", type: F.mat4, expr: "glState.modelviewTranspose" },
        gl_ModelViewMatrixInverseTranspose: { field: "modelviewInverseTranspose", type: F.mat4, expr: "glState.modelviewInverseTranspose" },
        gl_ProjectionMatrixInverse: { field: "projectionInverse", type: F.mat4, expr: "glState.projectionInverse" },
        gl_ProjectionMatrixTranspose: { field: "projectionTranspose", type: F.mat4, expr: "glState.projectionTranspose" },
        gl_ProjectionMatrixInverseTranspose: { field: "projectionInverseTranspose", type: F.mat4, expr: "glState.projectionInverseTranspose" },
        gl_ModelViewProjectionMatrixInverse: { field: "mvpInverse", type: F.mat4, expr: "glState.mvpInverse" },
        gl_ModelViewProjectionMatrixTranspose: { field: "mvpTranspose", type: F.mat4, expr: "glState.mvpTranspose" },
        gl_ModelViewProjectionMatrixInverseTranspose: { field: "mvpInverseTranspose", type: F.mat4, expr: "glState.mvpInverseTranspose" },
        gl_TextureMatrix: { field: "textureMatrix", type: arrayOf(F.mat4, MAX_TEXTURE_COORDS), expr: "glState.textureMatrix" },
        gl_TextureMatrixInverse: { field: "textureMatrixInverse", type: arrayOf(F.mat4, MAX_TEXTURE_COORDS), expr: "glState.textureMatrixInverse" },
        gl_TextureMatrixTranspose: { field: "textureMatrixTranspose", type: arrayOf(F.mat4, MAX_TEXTURE_COORDS), expr: "glState.textureMatrixTranspose" },
        gl_TextureMatrixInverseTranspose: { field: "textureMatrixInverseTranspose", type: arrayOf(F.mat4, MAX_TEXTURE_COORDS), expr: "glState.textureMatrixInverseTranspose" },
        gl_LightSource: { field: "lights", type: arrayOf(GL_LIGHT_SOURCE, MAX_LIGHTS), expr: "glState.lights" },
        gl_FrontMaterial: { field: "frontMaterial", type: GL_MATERIAL, expr: "glState.frontMaterial" },
        gl_BackMaterial: { field: "backMaterial", type: GL_MATERIAL, expr: "glState.backMaterial" },
        gl_FrontLightProduct: { field: "frontLightProduct", type: arrayOf(GL_LIGHT_PRODUCTS, MAX_LIGHTS), expr: "glState.frontLightProduct" },
        gl_BackLightProduct: { field: "backLightProduct", type: arrayOf(GL_LIGHT_PRODUCTS, MAX_LIGHTS), expr: "glState.backLightProduct" },
        gl_TextureEnvColor: { field: "texEnvColor", type: arrayOf(F.vec4, MAX_TEXTURE_COORDS), expr: "glState.texEnvColor" },
        gl_ClipPlane: { field: "clipPlanes", type: arrayOf(F.vec4, MAX_CLIP_PLANES), expr: "glState.clipPlanes" },

        gl_LightModel: { field: "lightModelAmbient", type: GL_LIGHT_MODEL, expr: null,
            members: { ambient: "glState.lightModelAmbient" } },
        gl_FrontLightModelProduct: { field: "frontSceneColor", type: GL_LIGHT_MODEL_PRODUCTS, expr: null,
            members: { sceneColor: "glState.frontSceneColor" } },
        gl_BackLightModelProduct: { field: "backSceneColor", type: GL_LIGHT_MODEL_PRODUCTS, expr: null,
            members: { sceneColor: "glState.backSceneColor" } },
        gl_Fog: { field: ["fogColor", "fogParams"], type: GL_FOG, expr: null,
            members: {
                color: "glState.fogColor",
                density: "glState.fogParams.x",
                start: "glState.fogParams.y",
                end: "glState.fogParams.z",
                scale: "glState.fogParams.w",
            } },
        gl_DepthRange: { field: "depthRange", type: GL_DEPTH_RANGE, expr: null,
            members: {
                near: "glState.depthRange.x",
                far: "glState.depthRange.y",
                diff: "glState.depthRange.z",
            } },
        gl_Point: { field: ["pointParams", "pointAttenuation"], type: GL_POINT, expr: null,
            members: {
                size: "glState.pointParams.x",
                sizeMin: "glState.pointParams.y",
                sizeMax: "glState.pointParams.z",
                fadeThresholdSize: "glState.pointParams.w",
                distanceConstantAttenuation: "glState.pointAttenuation.x",
                distanceLinearAttenuation: "glState.pointAttenuation.y",
                distanceQuadraticAttenuation: "glState.pointAttenuation.z",
            } },
    };

    /* The eye/object plane arrays are indexed per texture unit and stored
     * interleaved: unit u occupies 8 vec4 -- object planes S,T,R,Q then eye
     * planes S,T,R,Q -- so gl_EyePlaneT[u] is texGenPlanes[u*8 + 4 + 1]. */
    ["S", "T", "R", "Q"].forEach((coord, i) => {
        STATE_UNIFORMS["gl_ObjectPlane" + coord] = {
            field: "texGenPlanes", type: arrayOf(F.vec4, MAX_TEXTURE_COORDS),
            expr: null, planeStride: 8, planeOffset: i,
        };
        STATE_UNIFORMS["gl_EyePlane" + coord] = {
            field: "texGenPlanes", type: arrayOf(F.vec4, MAX_TEXTURE_COORDS),
            expr: null, planeStride: 8, planeOffset: 4 + i,
        };
    });

    const BUILTIN_CONSTANTS = {
        gl_MaxLights: { type: F.int, value: MAX_LIGHTS },
        gl_MaxClipPlanes: { type: F.int, value: MAX_CLIP_PLANES },
        gl_MaxTextureUnits: { type: F.int, value: 8 },
        gl_MaxTextureCoords: { type: F.int, value: MAX_TEXTURE_COORDS },
        gl_MaxVertexAttribs: { type: F.int, value: 16 },
        gl_MaxVertexUniformComponents: { type: F.int, value: 1024 },
        gl_MaxVaryingFloats: { type: F.int, value: 64 },
        gl_MaxVertexTextureImageUnits: { type: F.int, value: 16 },
        gl_MaxCombinedTextureImageUnits: { type: F.int, value: 24 },
        gl_MaxTextureImageUnits: { type: F.int, value: 16 },
        gl_MaxFragmentUniformComponents: { type: F.int, value: 1024 },
        gl_MaxDrawBuffers: { type: F.int, value: MAX_DRAW_BUFFERS },
    };

    /* Stage built-ins that are neither varyings nor state: they map onto a
     * WGSL entry-point builtin or an entry-point output. */
    const VERTEX_BUILTIN_OUTPUTS = {
        gl_Position: { type: F.vec4, required: true },
        gl_PointSize: { type: F.float },
        gl_ClipVertex: { type: F.vec4 },
    };

    /*
     * gl_PointCoord is deliberately not a WGSL builtin here. WebGPU has none:
     * its point-list topology draws one pixel, so the executor expands every
     * point into two triangles (plan 10.3) and the generated vertex wrapper
     * writes the corner's coordinate into an ordinary varying. A program that
     * reads gl_PointCoord therefore links in a point-sprite variant, and reads
     * (0.5, 0.5) -- the centre, which is what a 1-pixel point would sample --
     * when drawn as anything else.
     */
    const FRAGMENT_BUILTIN_INPUTS = {
        gl_FragCoord: { type: F.vec4, wgsl: "position" },
        gl_FrontFacing: { type: F.bool, wgsl: "front_facing" },
        gl_PointCoord: { type: F.vec2, wgsl: null, pointSpriteVarying: true },
    };

    const FRAGMENT_BUILTIN_OUTPUTS = {
        gl_FragColor: { type: F.vec4 },
        gl_FragData: { type: arrayOf(F.vec4, MAX_DRAW_BUFFERS) },
        gl_FragDepth: { type: F.float },
    };

    /* GLSL ES 1.00 (7.4): its constants are OpenGL ES 2.0's limits, which the
     * GLES host reports (src/gles/gles_caps.js); one draw buffer; no
     * gl_FragDepth, no compatibility state but gl_DepthRange, no fixed-function
     * attributes or varyings */
    const ES_BUILTIN_CONSTANTS = {
        gl_MaxVertexAttribs: { type: F.int, value: 16 },
        gl_MaxVertexUniformVectors: { type: F.int, value: 256 },
        gl_MaxVaryingVectors: { type: F.int, value: 15 },
        gl_MaxVertexTextureImageUnits: { type: F.int, value: 16 },
        gl_MaxCombinedTextureImageUnits: { type: F.int, value: 32 },
        gl_MaxTextureImageUnits: { type: F.int, value: 16 },
        gl_MaxFragmentUniformVectors: { type: F.int, value: 224 },
        gl_MaxDrawBuffers: { type: F.int, value: 1 },
    };
    const ES_FRAGMENT_BUILTIN_OUTPUTS = {
        gl_FragColor: { type: F.vec4 },
        gl_FragData: { type: arrayOf(F.vec4, 1) },
    };

    /* ================================================================== */
    /* Semantic analysis                                                  */
    /* ================================================================== */

    const SWIZZLE_SETS = [
        { chars: "xyzw" }, { chars: "rgba" }, { chars: "stpq" },
    ];

    function swizzleIndices(name) {
        for (const set of SWIZZLE_SETS) {
            let ok = true;
            const indices = [];
            for (const ch of name) {
                const idx = set.chars.indexOf(ch);
                if (idx < 0) { ok = false; break; }
                indices.push(idx);
            }
            if (ok) return indices;
        }
        return null;
    }

    /* Mangling exists because WGSL has no overloading. The type names are the
     * GLSL ones, so a mangled name is still readable in a shader dump -- which
     * matters when the only debugging tool is the generated source. */
    function mangle(name, paramTypes) {
        if (!paramTypes.length) return "glsl_" + name;
        return "glsl_" + name + "__" +
            paramTypes.map(t => t.name.replace(/[\[\]]/g, "_")).join("_");
    }

    class Scope {
        constructor(parent) {
            this.parent = parent;
            this.names = new Map();
        }
        lookup(name) {
            for (let s = this; s; s = s.parent) {
                const found = s.names.get(name);
                if (found) return found;
            }
            return null;
        }
        declare(name, symbol, line) {
            if (this.names.has(name))
                fail(line, "'" + name + "' is already declared in this scope");
            this.names.set(name, symbol);
            return symbol;
        }
    }

    class Analyzer {
        constructor(stage, ast, options) {
            this.stage = stage;
            this.ast = ast;
            this.options = options || {};
            this.globalScope = new Scope(null);
            this.scope = this.globalScope;
            this.functions = new Map();     // name -> [{params, returnType, def}]
            this.usage = {
                attributes: new Map(),      // name -> {type, isCompat}
                varyings: new Map(),        // name -> {type, isCompat, written, read}
                uniforms: [],               // declaration order
                uniformByName: new Map(),
                samplers: [],
                stateFields: new Set(),
                builtinInputs: new Set(),
                builtinOutputs: new Set(),
                texCoordMax: -1,
                texCoordDynamic: false,
                fragDataMax: -1,
                fragDataDynamic: false,
                usesDiscard: false,
                usesFragColor: false,
                usesFragData: false,
                writesFragDepth: false,
                writesPointSize: false,
                writesClipVertex: false,
                usesPointCoord: false,
                nonUniformSamples: 0,
                uniformSamples: 0,
                noiseCalls: 0,
            };
            this.constEnv = Object.create(null);
            this.controlFlowUniform = [true];
            this.currentFunction = null;
            this.loopDepth = 0;
        }

        get inUniformControlFlow() {
            return this.controlFlowUniform[this.controlFlowUniform.length - 1];
        }

        run() {
            this.declareBuiltins();
            // Two passes over the top level: function signatures first, so a
            // call may precede its definition the way GLSL allows after a
            // prototype, and so mutual recursion is diagnosed rather than
            // silently resolving to nothing.
            for (const decl of this.ast.decls) {
                if (decl.kind === "FunctionProto" || decl.kind === "FunctionDef")
                    this.declareFunction(decl);
            }
            for (const decl of this.ast.decls) {
                switch (decl.kind) {
                case "StructDecl": break;      // the parser already interned it
                case "InvariantDecl":
                    for (const name of decl.names) {
                        const symbol = this.globalScope.lookup(name);
                        if (!symbol) fail(decl.line, "unknown invariant '" + name + "'");
                        symbol.invariant = true;
                    }
                    break;
                case "VarDecl": this.globalVarDecl(decl); break;
                case "FunctionProto": break;
                case "FunctionDef": this.functionBody(decl); break;
                }
            }
            const main = this.functions.get("main");
            if ((!main || !main.some(o => o.def && !o.params.length)) && !this.options.library)
                fail(0, "no main() function");
            return this.usage;
        }

        declareBuiltins() {
            const g = this.globalScope;
            if (this.options.es) {
                for (const [name, info] of Object.entries(ES_BUILTIN_CONSTANTS))
                    g.names.set(name, { kind: "builtinConst", name, type: info.type,
                                        value: info.value });
                g.names.set("gl_DepthRange", { kind: "stateUniform", name: "gl_DepthRange",
                                               type: STATE_UNIFORMS.gl_DepthRange.type,
                                               info: STATE_UNIFORMS.gl_DepthRange });
                const outputs = this.stage === "vertex" ? VERTEX_BUILTIN_OUTPUTS : ES_FRAGMENT_BUILTIN_OUTPUTS;
                for (const [name, info] of Object.entries(outputs)) {
                    if (name === "gl_ClipVertex") continue;
                    g.names.set(name, { kind: "builtinOut", name, type: info.type, info });
                }
                if (this.stage === "fragment") {
                    for (const [name, info] of Object.entries(FRAGMENT_BUILTIN_INPUTS))
                        g.names.set(name, { kind: "builtinIn", name, type: info.type,
                                            info });
                }
                return;
            }
            for (const [name, info] of Object.entries(BUILTIN_CONSTANTS))
                g.names.set(name, { kind: "builtinConst", name, type: info.type,
                                    value: info.value });
            for (const [name, info] of Object.entries(STATE_UNIFORMS))
                g.names.set(name, { kind: "stateUniform", name, type: info.type,
                                    info });
            if (this.stage === "vertex") {
                for (const [name, type] of Object.entries(COMPAT_ATTRIBUTE_TYPES))
                    g.names.set(name, { kind: "compatAttribute", name, type,
                                        location: COMPAT_ATTRIBUTE_LOCATIONS[name] });
                for (const [name, type] of Object.entries(VERTEX_VARYING_OUTPUTS))
                    g.names.set(name, { kind: "compatVaryingOut", name, type });
                for (const [name, info] of Object.entries(VERTEX_BUILTIN_OUTPUTS))
                    g.names.set(name, { kind: "builtinOut", name, type: info.type,
                                        info });
            } else {
                for (const [name, type] of Object.entries(FRAGMENT_VARYING_INPUTS))
                    g.names.set(name, { kind: "compatVaryingIn", name, type });
                for (const [name, info] of Object.entries(FRAGMENT_BUILTIN_INPUTS))
                    g.names.set(name, { kind: "builtinIn", name, type: info.type,
                                        info });
                for (const [name, info] of Object.entries(FRAGMENT_BUILTIN_OUTPUTS))
                    g.names.set(name, { kind: "builtinOut", name, type: info.type,
                                        info });
            }
        }

        declareFunction(decl) {
            const paramTypes = decl.params.map(p => p.type);
            let list = this.functions.get(decl.name);
            if (!list) { list = []; this.functions.set(decl.name, list); }
            const existing = list.find(o =>
                o.params.length === paramTypes.length &&
                o.params.every((t, i) => sameType(t, paramTypes[i])));
            if (existing) {
                if (decl.kind === "FunctionDef") {
                    if (existing.def)
                        fail(decl.line, "'" + decl.name + "' is defined twice");
                    existing.def = decl;
                }
                if (!sameType(existing.returnType, decl.returnType))
                    fail(decl.line, "'" + decl.name +
                        "' redeclared with a different return type");
                decl.overload = existing;
                return existing;
            }
            if (builtinFunctions.has(decl.name) && decl.name !== "main") {
                // GLSL allows a user function to overload a built-in name; only
                // an identical signature is an error. Keeping both in separate
                // tables makes resolution prefer the user's, which matches the
                // spec's "user-defined functions hide built-ins" rule.
            }
            const overload = {
                name: decl.name,
                params: paramTypes,
                paramInfo: decl.params,
                returnType: decl.returnType,
                def: decl.kind === "FunctionDef" ? decl : null,
                mangled: decl.name === "main" ? "glsl_main" :
                    mangle(decl.name, paramTypes),
                hasOutputs: decl.params.some(p => p.direction !== "in"),
            };
            list.push(overload);
            decl.overload = overload;
            return overload;
        }

        globalVarDecl(decl) {
            const storage = decl.qualifiers.storage;
            for (const d of decl.declarators) {
                let type = d.type;
                if (type.kind === "array" && type.size < 0) {
                    // `varying vec4 v[];` is sized by its largest use. Nothing
                    // in the corpus does this, so it is refused with a message
                    // rather than guessed at.
                    fail(d.line, "unsized array '" + d.name +
                        "' is not supported; give it an explicit size");
                }
                const existing = this.globalScope.names.get(d.name);
                if (existing && ["compatVaryingIn", "compatVaryingOut"].includes(existing.kind) && storage === "varying") {
                    if (type.kind !== "array" || existing.type.kind !== "array" || !sameType(type.elem, existing.type.elem) || type.size > existing.type.size)
                        fail(d.line, "incompatible built-in varying redeclaration");
                    existing.type = type;d.symbol = existing;continue;
                }
                if (existing && existing.kind !== "builtinConst")
                    fail(d.line, "'" + d.name + "' is already declared");

                let symbol;
                if (storage === "attribute") {
                    if (this.stage !== "vertex")
                        fail(d.line, "attribute variables are vertex-shader only");
                    if (type.kind === "array" || type.kind === "struct")
                        fail(d.line, "an attribute may not be an array or a struct");
                    symbol = { kind: "attribute", name: d.name, type };
                    this.usage.attributes.set(d.name, { type, isCompat: false });
                } else if (storage === "varying") {
                    symbol = { kind: "varying", name: d.name, type,
                               centroid: decl.qualifiers.centroid,
                               invariant: decl.qualifiers.invariant };
                    this.usage.varyings.set(d.name, {
                        type, isCompat: false, written: false, read: false,
                    });
                } else if (storage === "uniform") {
                    symbol = { kind: "uniform", name: d.name, type };
                    this.registerUniform(d.name, type, d.line);
                } else if (storage === "const") {
                    if (!d.init) fail(d.line, "a const variable needs an initializer");
                    symbol = { kind: "const", name: d.name, type };
                } else {
                    symbol = { kind: "global", name: d.name, type };
                }
                this.globalScope.names.set(d.name, symbol);
                if (d.init) {
                    this.expr(d.init);
                    this.coerce(d.init, type, d.line);
                    if (storage === "const") {
                        const folded = foldConstantInt(d.init, this.constEnv);
                        if (folded !== null) this.constEnv[d.name] = folded;
                    }
                }
                d.symbol = symbol;
            }
        }

        registerUniform(name, type, line) {
            // Samplers are bindings, not buffer members; everything else joins
            // the user uniform block.
            const leafSamplers = [];
            collectSamplerLeaves(name, type, leafSamplers);
            if (leafSamplers.length) {
                // (an array of samplers is one leaf of arraySize elements)
                const elements = leafSamplers.reduce((n, leaf) => n + Math.max(1, leaf.arraySize), 0);
                for (const leaf of leafSamplers) {
                    this.usage.samplers.push({
                        name: leaf.path, type: leaf.type,
                        arraySize: leaf.arraySize || 0,
                        binding: -1,       // assigned at link time
                    });
                }
                this.usage.uniformByName.set(name, { name, type, sampler: true });
                if (elements === countLeaves(type)) return;
            }
            const entry = { name, type };
            this.usage.uniforms.push(entry);
            this.usage.uniformByName.set(name, entry);
        }

        functionBody(decl) {
            const overload = decl.overload || this.declareFunction(decl);
            this.currentFunction = overload;
            this.scope = new Scope(this.globalScope);
            for (const p of decl.params) {
                if (!p.name) continue;
                this.scope.declare(p.name, {
                    kind: "param", name: p.name, type: p.type,
                    direction: p.direction, isConst: p.isConst,
                }, p.line);
            }
            this.block(decl.body, false);
            this.scope = this.globalScope;
            this.currentFunction = null;
        }

        block(node, newScope = true) {
            const saved = this.scope;
            if (newScope) this.scope = new Scope(this.scope);
            for (const stmt of node.stmts) this.statement(stmt);
            this.scope = saved;
        }

        statement(node) {
            switch (node.kind) {
            case "Block": return this.block(node);
            case "VarDecl": return this.localVarDecl(node);
            case "StructDecl": return;
            case "ExprStmt": return void this.expr(node.expr);
            case "If": {
                this.expr(node.cond);
                this.requireBool(node.cond, "if");
                const uniform = this.inUniformControlFlow && !node.cond.nonUniform;
                this.controlFlowUniform.push(uniform);
                this.statement(node.then);
                if (node.otherwise) this.statement(node.otherwise);
                this.controlFlowUniform.pop();
                return;
            }
            case "While": {
                this.expr(node.cond);
                this.requireBool(node.cond, "while");
                this.enterLoop(node.cond.nonUniform, () => this.statement(node.body));
                return;
            }
            case "DoWhile": {
                this.enterLoop(true, () => this.statement(node.body));
                this.expr(node.cond);
                this.requireBool(node.cond, "do-while");
                return;
            }
            case "For": {
                const saved = this.scope;
                this.scope = new Scope(this.scope);
                if (node.init) this.statement(node.init);
                if (node.cond) {
                    this.expr(node.cond);
                    this.requireBool(node.cond, "for");
                }
                this.enterLoop(node.cond ? node.cond.nonUniform : true, () => {
                    if (node.step) this.expr(node.step);
                    this.statement(node.body);
                });
                this.scope = saved;
                return;
            }
            case "Return": {
                const expected = this.currentFunction ?
                    this.currentFunction.returnType : T.void;
                if (node.expr) {
                    this.expr(node.expr);
                    if (expected.kind === "void")
                        fail(node.line, "returning a value from a void function");
                    this.coerce(node.expr, expected, node.line);
                } else if (expected.kind !== "void") {
                    fail(node.line, "a non-void function must return a value");
                }
                return;
            }
            case "Break":
            case "Continue":
                if (!this.loopDepth)
                    fail(node.line, "'" + node.kind.toLowerCase() +
                        "' outside of a loop");
                return;
            case "Discard":
                this.usage.usesDiscard = true;
                return;
            default:
                fail(node.line || 0, "unsupported statement " + node.kind);
            }
        }

        enterLoop(condNonUniform, body) {
            const uniform = this.inUniformControlFlow && !condNonUniform;
            this.controlFlowUniform.push(uniform);
            ++this.loopDepth;
            body();
            --this.loopDepth;
            this.controlFlowUniform.pop();
        }

        localVarDecl(node) {
            for (const d of node.declarators) {
                if (d.type.kind === "array" && d.type.size < 0)
                    fail(d.line, "unsized local array '" + d.name + "'");
                if (d.type.kind === "sampler")
                    fail(d.line, "a sampler may only be a uniform or a parameter");
                const symbol = {
                    kind: node.qualifiers.storage === "const" ? "const" : "local",
                    name: d.name, type: d.type,
                    nonUniform: false,
                };
                if (d.init) {
                    this.expr(d.init);
                    this.coerce(d.init, d.type, d.line);
                    symbol.nonUniform = !!d.init.nonUniform;
                    if (node.qualifiers.storage === "const") {
                        const folded = foldConstantInt(d.init, this.constEnv);
                        if (folded !== null) this.constEnv[d.name] = folded;
                    }
                } else if (node.qualifiers.storage === "const") {
                    fail(d.line, "a const variable needs an initializer");
                }
                this.scope.declare(d.name, symbol, d.line);
                d.symbol = symbol;
            }
        }

        requireBool(node, where) {
            if (!sameType(node.type, T.bool))
                fail(node.line, where + " condition must be a bool, not " +
                    node.type.name);
        }

        /* Records the implicit conversion the analyzer decided on, so code
         * generation never has to re-derive it. */
        coerce(node, target, line) {
            if (sameType(node.type, target)) return;
            if (convertsTo(node.type, target)) {
                node.convertTo = target;
                return;
            }
            fail(line || node.line,
                "cannot convert " + node.type.name + " to " + target.name);
        }

        /* ---- expressions ---- */

        expr(node) {
            const type = this.exprInner(node);
            node.type = type;
            return type;
        }

        exprInner(node) {
            switch (node.kind) {
            case "IntLit": node.nonUniform = false; return T.int;
            case "FloatLit": node.nonUniform = false; return T.float;
            case "BoolLit": node.nonUniform = false; return T.bool;
            case "Ident": return this.identifier(node);
            case "Sequence":
                this.expr(node.left);
                this.expr(node.right);
                node.nonUniform = node.right.nonUniform;
                return node.right.type;
            case "Assign": return this.assignment(node);
            case "Conditional": return this.conditional(node);
            case "Binary": return this.binary(node);
            case "Unary": return this.unary(node);
            case "Index": return this.index(node);
            case "Field": return this.field(node);
            case "Call": return this.call(node);
            case "ArrayLength":
                this.expr(node.object);
                if (node.object.type.kind !== "array") fail(node.line, ".length() requires an array");
                node.value = node.object.type.size;node.nonUniform = false;return T.int;
            default:
                fail(node.line, "unsupported expression " + node.kind);
            }
        }

        identifier(node) {
            const symbol = this.scope.lookup(node.name);
            if (!symbol)
                fail(node.line, "undeclared identifier '" + node.name + "'");
            node.symbol = symbol;
            node.nonUniform = this.symbolIsNonUniform(symbol);
            this.noteSymbolUse(symbol, node);
            return symbol.type;
        }

        symbolIsNonUniform(symbol) {
            switch (symbol.kind) {
            case "attribute":
            case "compatAttribute":
            case "varying":
            case "compatVaryingIn":
            case "compatVaryingOut":
            case "builtinIn":
                return true;
            case "local":
            case "param":
                return !!symbol.nonUniform;
            default:
                return false;
            }
        }

        noteSymbolUse(symbol, node) {
            switch (symbol.kind) {
            case "compatAttribute":
                this.usage.attributes.set(symbol.name,
                    { type: symbol.type, isCompat: true,
                      location: symbol.location });
                break;
            case "attribute":
                this.usage.attributes.set(symbol.name,
                    { type: symbol.type, isCompat: false });
                break;
            case "varying": {
                const entry = this.usage.varyings.get(symbol.name);
                if (entry) {
                    if (this.stage === "vertex") entry.written = true;
                    else entry.read = true;
                }
                break;
            }
            case "compatVaryingIn":
            case "compatVaryingOut": {
                if (!this.usage.varyings.has(symbol.name))
                    this.usage.varyings.set(symbol.name, {
                        type: symbol.type, isCompat: true,
                        written: false, read: false,
                    });
                const entry = this.usage.varyings.get(symbol.name);
                if (this.stage === "vertex") entry.written = true;
                else entry.read = true;
                break;
            }
            case "stateUniform": {
                const fields = Array.isArray(symbol.info.field) ?
                    symbol.info.field : [symbol.info.field];
                for (const f of fields) this.usage.stateFields.add(f);
                break;
            }
            case "builtinIn":
                this.usage.builtinInputs.add(symbol.name);
                if (symbol.name === "gl_PointCoord")
                    this.usage.usesPointCoord = true;
                break;
            case "builtinOut":
                this.usage.builtinOutputs.add(symbol.name);
                if (symbol.name === "gl_FragColor") this.usage.usesFragColor = true;
                if (symbol.name === "gl_FragData") this.usage.usesFragData = true;
                if (symbol.name === "gl_FragDepth") this.usage.writesFragDepth = true;
                if (symbol.name === "gl_PointSize") this.usage.writesPointSize = true;
                if (symbol.name === "gl_ClipVertex") this.usage.writesClipVertex = true;
                break;
            }
            void node;
        }

        assignment(node) {
            this.expr(node.target);
            this.expr(node.value);
            this.requireLValue(node.target);
            if (node.op === "=") {
                this.coerce(node.value, node.target.type, node.line);
            } else {
                // Compound assignment: the arithmetic must be legal and the
                // result must fit back into the target without conversion.
                const op = node.op.slice(0, -1);
                const resultType = this.binaryResultType(op, node.target.type,
                    node.value.type, node);
                if (!sameType(resultType, node.target.type))
                    fail(node.line, "cannot assign " + resultType.name +
                        " to " + node.target.type.name);
            }
            this.propagateNonUniform(node.target, node.value.nonUniform);
            node.nonUniform = node.target.nonUniform || node.value.nonUniform;
            return node.target.type;
        }

        /* A local that is ever assigned a non-uniform value stays non-uniform
         * for the rest of the shader. Processing in source order makes this a
         * single pass; it can only over-approximate, which is the safe way for
         * a decision that picks textureSample versus textureSampleGrad. */
        propagateNonUniform(target, nonUniform) {
            if (!nonUniform) return;
            let node = target;
            while (node && (node.kind === "Field" || node.kind === "Index"))
                node = node.object;
            if (node && node.kind === "Ident" && node.symbol &&
                    (node.symbol.kind === "local" || node.symbol.kind === "param" ||
                     node.symbol.kind === "global"))
                node.symbol.nonUniform = true;
        }

        requireLValue(node) {
            let root = node;
            while (root.kind === "Field" || root.kind === "Index") {
                if (root.kind === "Field" && root.swizzle) {
                    const seen = new Set();
                    for (const i of root.swizzle) {
                        if (seen.has(i))
                            fail(root.line, "a swizzle used as an l-value may " +
                                "not repeat a component");
                        seen.add(i);
                    }
                }
                root = root.object;
            }
            if (root.kind !== "Ident")
                fail(node.line, "expression is not assignable");
            const symbol = root.symbol;
            switch (symbol.kind) {
            case "const":
            case "builtinConst":
                fail(node.line, "cannot assign to the constant '" + symbol.name + "'");
                break;
            case "uniform":
            case "stateUniform":
                fail(node.line, "cannot assign to the uniform '" + symbol.name + "'");
                break;
            case "attribute":
            case "compatAttribute":
                fail(node.line, "cannot assign to the attribute '" + symbol.name + "'");
                break;
            case "builtinIn":
                fail(node.line, "cannot assign to '" + symbol.name + "'");
                break;
            case "varying":
            case "compatVaryingIn":
                if (this.stage === "fragment")
                    fail(node.line, "cannot assign to the varying '" +
                        symbol.name + "' in a fragment shader");
                break;
            case "param":
                // (an `in` parameter is the function's own copy: only a
                // `const in` one is read-only)
                if (symbol.isConst)
                    fail(node.line, "cannot assign to the const parameter '" +
                        symbol.name + "'");
                break;
            }
        }

        conditional(node) {
            this.expr(node.cond);
            this.requireBool(node.cond, "?:");
            this.expr(node.then);
            this.expr(node.otherwise);
            let type;
            if (sameType(node.then.type, node.otherwise.type)) {
                type = node.then.type;
            } else if (convertsTo(node.then.type, node.otherwise.type)) {
                node.then.convertTo = node.otherwise.type;
                type = node.otherwise.type;
            } else if (convertsTo(node.otherwise.type, node.then.type)) {
                node.otherwise.convertTo = node.then.type;
                type = node.then.type;
            } else {
                fail(node.line, "the two arms of ?: have incompatible types (" +
                    node.then.type.name + " and " + node.otherwise.type.name + ")");
            }
            node.nonUniform = node.cond.nonUniform || node.then.nonUniform ||
                node.otherwise.nonUniform;
            return type;
        }

        binary(node) {
            this.expr(node.left);
            this.expr(node.right);
            node.nonUniform = node.left.nonUniform || node.right.nonUniform;
            return this.binaryResultType(node.op, node.left.type, node.right.type,
                node);
        }

        binaryResultType(op, left, right, node) {
            const line = node.line;
            switch (op) {
            case "&&": case "||": case "^^":
                if (!sameType(left, T.bool) || !sameType(right, T.bool))
                    fail(line, "'" + op + "' requires bool operands");
                return T.bool;
            case "==": case "!=":
                if (!sameType(left, right)) {
                    if (convertsTo(left, right)) node.left.convertTo = right;
                    else if (convertsTo(right, left)) node.right.convertTo = left;
                    else fail(line, "cannot compare " + left.name + " with " +
                        right.name);
                }
                if (left.kind === "sampler")
                    fail(line, "samplers cannot be compared");
                return T.bool;
            case "<": case ">": case "<=": case ">=":
                if (left.kind !== "scalar" || right.kind !== "scalar" ||
                        left.base === "bool" || right.base === "bool")
                    fail(line, "'" + op + "' requires scalar numeric operands");
                if (!sameType(left, right)) {
                    if (convertsTo(left, right)) node.left.convertTo = right;
                    else if (convertsTo(right, left)) node.right.convertTo = left;
                    else fail(line, "cannot compare " + left.name + " with " +
                        right.name);
                }
                return T.bool;
            case "<<": case ">>": case "&": case "|": case "^":
                if (this.options.es)
                    fail(line, "'" + op + "' is reserved in GLSL ES 1.00");
                if (baseOf(left) !== "int" || baseOf(right) !== "int")
                    fail(line, "'" + op + "' requires integer operands");
                return componentCount(left) >= componentCount(right) ? left : right;
            case "%":
                if (this.options.es)
                    fail(line, "'%' is reserved in GLSL ES 1.00");
                if (baseOf(left) !== "int" || baseOf(right) !== "int")
                    fail(line, "'%' requires integer operands in GLSL 1.20");
                return componentCount(left) >= componentCount(right) ? left : right;
            case "+": case "-": case "*": case "/":
                return this.arithmeticType(op, left, right, node);
            default:
                fail(line, "unsupported operator '" + op + "'");
            }
        }

        arithmeticType(op, left, right, node) {
            const line = node.line;
            const lb = baseOf(left), rb = baseOf(right);
            if (lb === "bool" || rb === "bool" || !lb || !rb)
                fail(line, "'" + op + "' cannot be applied to " + left.name +
                    " and " + right.name);

            // Mixed int/float: the int side converts. Matrices are float-only,
            // so an int operand next to a matrix converts too.
            let L = left, R = right;
            if (lb !== rb) {
                if (lb === "int") { node.left.convertTo = toFloatType(left); L = node.left.convertTo; }
                else { node.right.convertTo = toFloatType(right); R = node.right.convertTo; }
            } else if (lb === "int" && (left.kind === "matrix" || right.kind === "matrix")) {
                fail(line, "matrices are float-only");
            }

            if (op === "*") {
                if (L.kind === "matrix" && R.kind === "matrix") {
                    if (L.cols !== R.rows)
                        fail(line, "matrix dimensions do not agree: " +
                            L.name + " * " + R.name);
                    return mat(R.cols, L.rows);
                }
                if (L.kind === "matrix" && R.kind === "vector") {
                    if (L.cols !== R.size)
                        fail(line, L.name + " * " + R.name + " does not agree");
                    return vec("float", L.rows);
                }
                if (L.kind === "vector" && R.kind === "matrix") {
                    if (R.rows !== L.size)
                        fail(line, L.name + " * " + R.name + " does not agree");
                    return vec("float", R.cols);
                }
            }
            if (L.kind === "matrix" || R.kind === "matrix") {
                if (L.kind === "scalar") return R;
                if (R.kind === "scalar") return L;
                if (L.kind === "matrix" && R.kind === "matrix") {
                    if (L.cols !== R.cols || L.rows !== R.rows)
                        fail(line, "matrix dimensions do not agree");
                    return L;
                }
                fail(line, "'" + op + "' cannot mix " + L.name + " and " + R.name);
            }
            if (L.kind === "scalar" && R.kind === "scalar") {
                return L;
            }
            if (L.kind === "vector" && R.kind === "vector") {
                if (L.size !== R.size)
                    fail(line, "vector sizes do not agree: " + L.name + " " +
                        op + " " + R.name);
                return L;
            }
            // scalar-vector broadcast
            return L.kind === "vector" ? L : R;
        }

        unary(node) {
            this.expr(node.operand);
            node.nonUniform = node.operand.nonUniform;
            const t = node.operand.type;
            switch (node.op) {
            case "!":
                if (!sameType(t, T.bool))
                    fail(node.line, "'!' requires a bool operand");
                return T.bool;
            case "~":
                if (this.options.es)
                    fail(node.line, "'~' is reserved in GLSL ES 1.00");
                if (baseOf(t) !== "int")
                    fail(node.line, "'~' requires an integer operand");
                return t;
            case "-": case "+":
                if (!baseOf(t) || baseOf(t) === "bool")
                    fail(node.line, "'" + node.op + "' requires a numeric operand");
                return t;
            case "++": case "--":
                this.requireLValue(node.operand);
                if (!baseOf(t) || baseOf(t) === "bool")
                    fail(node.line, "'" + node.op + "' requires a numeric operand");
                return t;
            default:
                fail(node.line, "unsupported unary operator '" + node.op + "'");
            }
        }

        index(node) {
            this.expr(node.object);
            this.expr(node.index);
            if (baseOf(node.index.type) !== "int" ||
                    node.index.type.kind !== "scalar")
                fail(node.line, "an array index must be an integer");
            const constIndex = foldConstantInt(node.index, this.constEnv);
            node.constIndex = constIndex;
            node.nonUniform = node.object.nonUniform || node.index.nonUniform;

            const t = node.object.type;
            if (t.kind === "array") {
                if (constIndex !== null && (constIndex < 0 || constIndex >= t.size))
                    fail(node.line, "array index " + constIndex + " is out of range");
                this.noteArrayIndex(node, constIndex);
                return t.elem;
            }
            if (t.kind === "vector") {
                if (constIndex !== null && (constIndex < 0 || constIndex >= t.size))
                    fail(node.line, "vector index " + constIndex + " is out of range");
                return t.base === "float" ? T.float :
                    (t.base === "int" ? T.int : T.bool);
            }
            if (t.kind === "matrix") {
                if (constIndex !== null && (constIndex < 0 || constIndex >= t.cols))
                    fail(node.line, "matrix column " + constIndex + " is out of range");
                return vec("float", t.rows);
            }
            fail(node.line, t.name + " cannot be indexed");
        }

        /* gl_TexCoord and gl_FragData decide how many varying slots and colour
         * attachments the program needs, so their index range is tracked here
         * rather than rediscovered at code generation. */
        noteArrayIndex(node, constIndex) {
            let base = node.object;
            if (base.kind !== "Ident" || !base.symbol) return;
            const name = base.symbol.name;
            if (name === "gl_TexCoord") {
                if (constIndex === null) this.usage.texCoordDynamic = true;
                else this.usage.texCoordMax = Math.max(this.usage.texCoordMax, constIndex);
            } else if (name === "gl_FragData") {
                if (constIndex === null) this.usage.fragDataDynamic = true;
                else this.usage.fragDataMax = Math.max(this.usage.fragDataMax, constIndex);
            } else if (base.symbol.kind === "uniform" &&
                    base.symbol.type.kind === "array" &&
                    base.symbol.type.elem.kind === "sampler") {
                if (constIndex === null)
                    fail(node.line, "a sampler array must be indexed by a " +
                        "constant expression (WGSL has no binding arrays)");
            }
        }

        field(node) {
            this.expr(node.object);
            node.nonUniform = node.object.nonUniform;
            const t = node.object.type;
            if (t.kind === "struct") {
                const f = t.fields.find(x => x.name === node.name);
                if (!f)
                    fail(node.line, t.name + " has no member '" + node.name + "'");
                node.structField = f;
                return f.type;
            }
            if (t.kind === "vector" || t.kind === "scalar") {
                const indices = swizzleIndices(node.name);
                if (!indices)
                    fail(node.line, "'" + node.name + "' is not a valid swizzle");
                const size = t.kind === "scalar" ? 1 : t.size;
                for (const i of indices)
                    if (i >= size)
                        fail(node.line, "swizzle component out of range for " +
                            t.name);
                if (indices.length > 4)
                    fail(node.line, "a swizzle may select at most 4 components");
                node.swizzle = indices;
                const base = baseOf(t);
                return indices.length === 1 ?
                    (base === "float" ? T.float : (base === "int" ? T.int : T.bool)) :
                    vec(base, indices.length);
            }
            if (t.kind === "array" && node.name === "length")
                fail(node.line, ".length() is GLSL 1.20+ on arrays only in " +
                    "later versions; use the declared size");
            fail(node.line, t.name + " has no member '" + node.name + "'");
        }

        call(node) {
            for (const arg of node.args) this.expr(arg);
            node.nonUniform = node.args.some(a => a.nonUniform);

            if (node.constructType) return this.constructorCall(node);

            // A user function hides a built-in of the same name, per the spec.
            const userList = this.functions.get(node.callee);
            if (userList) {
                const resolved = this.resolveOverload(userList, node);
                if (resolved) {
                    node.resolved = resolved;
                    node.userCall = true;
                    if (resolved.hasOutputs) this.checkOutputArgs(node, resolved);
                    return resolved.returnType;
                }
            }
            const builtinList = builtinFunctions.get(node.callee);
            if (builtinList) {
                const resolved = this.resolveOverload(builtinList, node);
                if (resolved) {
                    node.resolved = resolved;
                    node.builtinCall = true;
                    this.noteBuiltinCall(resolved, node);
                    return resolved.ret;
                }
                fail(node.line, "no overload of '" + node.callee +
                    "' matches (" + node.args.map(a => a.type.name).join(", ") + ")");
            }
            if (userList)
                fail(node.line, "no overload of '" + node.callee +
                    "' matches (" + node.args.map(a => a.type.name).join(", ") + ")");
            fail(node.line, "call to undeclared function '" + node.callee + "'");
        }

        noteBuiltinCall(overload, node) {
            if (overload.texture) {
                /* What WGSL constrains is uniform *control flow*, not a uniform
                 * coordinate: a varying texcoord is the normal case and does
                 * not force explicit gradients. Conflating the two costs every
                 * ordinary sample its mip selection. */
                const uniform = this.inUniformControlFlow;
                node.uniformSample = uniform;
                if (uniform) ++this.usage.uniformSamples;
                else ++this.usage.nonUniformSamples;
            }
            if (overload.name === "ftransform") {
                // ftransform() is gl_ModelViewProjectionMatrix * gl_Vertex, so
                // it uses the attribute whether or not the shader names it.
                this.usage.attributes.set("gl_Vertex", {
                    type: COMPAT_ATTRIBUTE_TYPES.gl_Vertex, isCompat: true,
                    location: COMPAT_ATTRIBUTE_LOCATIONS.gl_Vertex,
                });
            }
            if (overload.noise) ++this.usage.noiseCalls;
            if (overload.vertexOnly && this.stage !== "vertex")
                fail(node.line, "'" + overload.name +
                    "' is only available in a vertex shader");
            for (const field of overload.usesState || [])
                this.usage.stateFields.add(field);
        }

        checkOutputArgs(node, overload) {
            overload.paramInfo.forEach((p, i) => {
                if (p.direction === "in") return;
                this.requireLValue(node.args[i]);
                this.propagateNonUniform(node.args[i], true);
            });
        }

        /*
         * GLSL overload resolution: an exact match wins outright; otherwise
         * there must be exactly one candidate all of whose arguments convert
         * implicitly. Ambiguity is an error rather than a first-match win,
         * because picking silently is how min(1, 2.0) ends up integer.
         */
        resolveOverload(list, node) {
            const argTypes = node.args.map(a => a.type);
            const arity = list.filter(o =>
                (o.params ? o.params.length : 0) === argTypes.length);
            const exact = arity.find(o =>
                o.params.every((p, i) => sameType(p, argTypes[i])));
            if (exact) return exact;
            const convertible = arity.filter(o =>
                o.params.every((p, i) => convertsTo(argTypes[i], p)));
            if (convertible.length === 1) {
                convertible[0].params.forEach((p, i) => {
                    if (!sameType(argTypes[i], p)) node.args[i].convertTo = p;
                });
                return convertible[0];
            }
            if (convertible.length > 1)
                fail(node.line, "ambiguous call to '" + node.callee + "'");
            return null;
        }

        constructorCall(node) {
            const target = node.constructType;
            const args = node.args;
            if (!args.length)
                fail(node.line, "a constructor needs at least one argument");

            if (target.kind === "struct") {
                if (args.length !== target.fields.length)
                    fail(node.line, target.name + " takes " +
                        target.fields.length + " arguments");
                target.fields.forEach((f, i) => this.coerce(args[i], f.type, node.line));
                return target;
            }
            if (target.kind === "array") {
                if (args.length !== target.size) fail(node.line, "array constructor size mismatch");
                for (const arg of args) this.coerce(arg, target.elem, node.line);
                return target;
            }
            if (target.kind === "scalar") {
                if (args.length !== 1)
                    fail(node.line, target.name + "() takes one argument");
                const from = args[0].type;
                if (from.kind !== "scalar" && from.kind !== "vector")
                    fail(node.line, "cannot construct " + target.name +
                        " from " + from.name);
                return target;
            }
            if (target.kind === "vector") {
                if (args.length === 1 && args[0].type.kind === "scalar")
                    return target;           // splat
                let available = 0;
                for (const a of args) {
                    if (a.type.kind === "matrix") {
                        available += componentCount(a.type);
                    } else if (a.type.kind === "scalar" || a.type.kind === "vector") {
                        available += componentCount(a.type);
                    } else {
                        fail(node.line, "cannot use " + a.type.name +
                            " in a vector constructor");
                    }
                }
                if (available < target.size)
                    fail(node.line, target.name + "() needs " + target.size +
                        " components but was given " + available);
                return target;
            }
            if (target.kind === "matrix") {
                if (args.length === 1) {
                    const from = args[0].type;
                    if (from.kind === "scalar") return target;      // diagonal
                    if (from.kind === "matrix") return target;      // resize
                    fail(node.line, "cannot construct " + target.name +
                        " from " + from.name);
                }
                let available = 0;
                let allColumns = args.length === target.cols;
                for (const a of args) {
                    if (a.type.kind === "vector") {
                        if (a.type.size !== target.rows) allColumns = false;
                    } else if (a.type.kind === "scalar") {
                        allColumns = false;
                    } else {
                        fail(node.line, "cannot use " + a.type.name +
                            " in a matrix constructor");
                    }
                    available += componentCount(a.type);
                }
                node.matrixFromColumns = allColumns;
                if (!allColumns && available < target.cols * target.rows)
                    fail(node.line, target.name + "() needs " +
                        (target.cols * target.rows) + " components");
                return target;
            }
            fail(node.line, "cannot construct " + target.name);
        }
    }

    function toFloatType(t) {
        if (t.kind === "scalar") return T.float;
        if (t.kind === "vector") return vec("float", t.size);
        return t;
    }

    function countLeaves(type) {
        if (type.kind === "array") return type.size * countLeaves(type.elem);
        if (type.kind === "struct")
            return type.fields.reduce((n, f) => n + countLeaves(f.type), 0);
        return 1;
    }

    function collectSamplerLeaves(path, type, out) {
        if (type.kind === "sampler") {
            out.push({ path, type, arraySize: 0 });
            return;
        }
        if (type.kind === "array") {
            if (type.elem.kind === "sampler") {
                out.push({ path, type: type.elem, arraySize: type.size });
                return;
            }
            for (let i = 0; i < type.size; ++i)
                collectSamplerLeaves(path + "[" + i + "]", type.elem, out);
            return;
        }
        if (type.kind === "struct") {
            for (const f of type.fields)
                collectSamplerLeaves(path + "." + f.name, f.type, out);
        }
    }

    /* ================================================================== */
    /* Uniform block layout                                               */
    /* ================================================================== */

    /*
     * Every member of the user uniform block is placed at a 16-byte boundary
     * and given an explicit @align/@size. WGSL's own layout rules would work
     * out for most of these, but "most" is the problem: a mismatch between the
     * offsets this file computes and the offsets the WGSL compiler chooses is
     * invisible until a uniform reads as garbage, and the difference shows up
     * only for the awkward types (a lone float, a vec3, a mat3). Pinning both
     * sides to the same rule removes the class of bug.
     *
     * Two consequences worth knowing:
     *   - a float uniform costs 16 bytes; uniform space is 64 KB, so this has
     *     never mattered
     *   - an array of float/vec2/vec3 is declared array<vec4<f32>, N>, because
     *     the uniform address space requires a 16-byte array stride. Reads go
     *     through a generated accessor so the swizzle stays out of the
     *     expression generator.
     */

    function roundUp(value, alignment) {
        return Math.ceil(value / alignment) * alignment;
    }

    function matrixColumnStrideBytes(type) {
        return type.rows === 2 ? 8 : 16;
    }

    function naturalSizeBytes(type) {
        switch (type.kind) {
        case "scalar": return 4;
        case "vector": return type.size * 4;
        case "matrix": return type.cols * matrixColumnStrideBytes(type);
        default: throw new Error("naturalSizeBytes: " + type.kind);
        }
    }

    /* The WGSL type a uniform member is declared with, plus how a shader reads
     * a value of the GLSL type back out of it. */
    function uniformStorageFor(type) {
        if (type.kind === "scalar") {
            if (type.base === "bool")
                return { wgsl: "i32", read: e => "(" + e + " != 0)" };
            return { wgsl: typeToWGSL(type), read: e => e };
        }
        if (type.kind === "vector") {
            if (type.base === "bool") {
                const it = "vec" + type.size + "<i32>";
                return { wgsl: it,
                         read: e => "(" + e + " != " + it + "(0))" };
            }
            return { wgsl: typeToWGSL(type), read: e => e };
        }
        if (type.kind === "matrix")
            return { wgsl: typeToWGSL(type), read: e => e };
        throw new Error("uniformStorageFor: " + type.kind);
    }

    /* Element storage for an array member: scalars and short vectors are
     * widened to a vec4 so the stride is 16. */
    function uniformArrayElementFor(type) {
        if (type.kind === "scalar" || type.kind === "vector") {
            const base = type.base === "bool" ? "i32" :
                (type.base === "int" ? "i32" : "f32");
            const count = type.kind === "scalar" ? 1 : type.size;
            const wgsl = "vec4<" + base + ">";
            const swizzle = ["x", "xy", "xyz", "xyzw"][count - 1];
            const read = e => {
                const v = e + "." + swizzle;
                if (type.base !== "bool") return v;
                const zero = count === 1 ? "0" : "vec" + count + "<i32>(0)";
                return "(" + v + " != " + zero + ")";
            };
            return { wgsl, strideBytes: 16, read, componentsUsed: count };
        }
        if (type.kind === "matrix") {
            const size = roundUp(naturalSizeBytes(type), 16);
            return { wgsl: typeToWGSL(type), strideBytes: size, read: e => e,
                     componentsUsed: type.cols * type.rows };
        }
        return null;
    }

    class UniformBlockLayout {
        constructor() {
            this.members = [];        // WGSL struct members, in order
            this.leaves = new Map();  // GL uniform path -> leaf descriptor
            this.structs = [];        // generated WGSL struct declarations
            this.structNames = new Map();
            this.accessors = [];      // generated WGSL accessor functions
            this.cursor = 0;
        }

        get sizeBytes() { return roundUp(this.cursor, 16); }

        /* Declares one GLSL uniform. Returns a descriptor code generation uses
         * to read it: `read` for a plain value, `readIndexed` for an array. */
        add(name, type) {
            const offset = roundUp(this.cursor, 16);
            const wgslName = "u_" + safeName(name);
            const descriptor = this.place(name, wgslName, type, offset);
            this.cursor = offset + descriptor.sizeBytes;
            this.members.push({
                wgslName, wgslType: descriptor.wgslType,
                align: 16, size: descriptor.sizeBytes,
            });
            return descriptor;
        }

        place(glslPath, wgslPath, type, offset) {
            if (type.kind === "sampler")
                throw new Error("samplers do not live in the uniform block");

            if (type.kind === "array") {
                const elem = type.elem;
                if (elem.kind === "struct") {
                    const structName = this.declareStruct(elem);
                    const elemSize = this.structSizeBytes(elem);
                    for (let i = 0; i < type.size; ++i) {
                        this.placeStructLeaves(
                            glslPath + "[" + i + "]",
                            "glUniforms." + wgslPath + "[" + i + "]",
                            elem, offset + i * elemSize);
                    }
                    return {
                        wgslType: "array<" + structName + ", " + type.size + ">",
                        sizeBytes: elemSize * type.size,
                        kind: "structArray",
                    };
                }
                const storage = uniformArrayElementFor(elem);
                if (!storage)
                    throw new Error("unsupported uniform array element " + elem.name);
                const accessor = "u_at_" + wgslPath;
                this.accessors.push(
                    "fn " + accessor + "(i : i32) -> " + typeToWGSL(elem) + " {\n" +
                    "    return " + storage.read("glUniforms." + wgslPath + "[i]") + ";\n" +
                    "}");
                const leaf = {
                    glslPath, type: elem, offsetBytes: offset,
                    arraySize: type.size, arrayStrideBytes: storage.strideBytes,
                    componentsUsed: storage.componentsUsed,
                    matrixColumnStrideBytes: elem.kind === "matrix" ?
                        matrixColumnStrideBytes(elem) : 0,
                    readIndexed: idx => accessor + "(" + idx + ")",
                };
                this.leaves.set(glslPath, leaf);
                return {
                    wgslType: "array<" + storage.wgsl + ", " + type.size + ">",
                    sizeBytes: storage.strideBytes * type.size,
                    kind: "array", leaf,
                };
            }

            if (type.kind === "struct") {
                const structName = this.declareStruct(type);
                this.placeStructLeaves(glslPath, "glUniforms." + wgslPath, type, offset);
                return { wgslType: structName,
                         sizeBytes: this.structSizeBytes(type), kind: "struct" };
            }

            const storage = uniformStorageFor(type);
            const leaf = {
                glslPath, type, offsetBytes: offset, arraySize: 0,
                arrayStrideBytes: 0,
                componentsUsed: componentCount(type),
                matrixColumnStrideBytes: type.kind === "matrix" ?
                    matrixColumnStrideBytes(type) : 0,
                read: storage.read("glUniforms." + wgslPath),
            };
            this.leaves.set(glslPath, leaf);
            return {
                wgslType: storage.wgsl,
                sizeBytes: roundUp(naturalSizeBytes(type), 16),
                kind: "scalar", leaf,
            };
        }

        placeStructLeaves(glslPath, wgslExpr, type, offset) {
            let cursor = offset;
            for (const field of type.fields) {
                if (field.type.kind === "sampler" || field.type.kind === "array" && field.type.elem.kind === "sampler") continue;
                const fieldOffset = roundUp(cursor, 16);
                const path = glslPath + "." + field.name;
                const expr = wgslExpr + "." + safeName(field.name);
                if (field.type.kind === "struct") {
                    this.placeStructLeaves(path, expr, field.type, fieldOffset);
                    cursor = fieldOffset + this.structSizeBytes(field.type);
                    continue;
                }
                if (field.type.kind === "array" && field.type.elem.kind === "struct") {
                    const elemSize = this.structSizeBytes(field.type.elem);
                    for (let i = 0; i < field.type.size; ++i)
                        this.placeStructLeaves(path + "[" + i + "]", expr + "[" + i + "]",
                            field.type.elem, fieldOffset + i * elemSize);
                    cursor = fieldOffset + elemSize * field.type.size;
                    continue;
                }
                if (field.type.kind === "array") {
                    const storage = uniformArrayElementFor(field.type.elem);
                    if (!storage)
                        throw new Error("unsupported uniform array element " +
                            field.type.elem.name);
                    this.leaves.set(path, {
                        glslPath: path, type: field.type.elem,
                        offsetBytes: fieldOffset,
                        arraySize: field.type.size,
                        arrayStrideBytes: storage.strideBytes,
                        componentsUsed: storage.componentsUsed,
                        matrixColumnStrideBytes: field.type.elem.kind === "matrix" ?
                            matrixColumnStrideBytes(field.type.elem) : 0,
                        readIndexed: idx => storage.read(expr + "[" + idx + "]"),
                    });
                    cursor = fieldOffset + storage.strideBytes * field.type.size;
                    continue;
                }
                const storage = uniformStorageFor(field.type);
                this.leaves.set(path, {
                    glslPath: path, type: field.type, offsetBytes: fieldOffset,
                    arraySize: 0, arrayStrideBytes: 0,
                    componentsUsed: componentCount(field.type),
                    matrixColumnStrideBytes: field.type.kind === "matrix" ?
                        matrixColumnStrideBytes(field.type) : 0,
                    read: storage.read(expr),
                });
                cursor = fieldOffset + roundUp(naturalSizeBytes(field.type), 16);
            }
        }

        structSizeBytes(type) {
            let cursor = 0;
            for (const field of type.fields) {
                if (field.type.kind === "sampler" || field.type.kind === "array" && field.type.elem.kind === "sampler") continue;
                cursor = roundUp(cursor, 16);
                if (field.type.kind === "struct")
                    cursor += this.structSizeBytes(field.type);
                else if (field.type.kind === "array" && field.type.elem.kind === "struct")
                    cursor += this.structSizeBytes(field.type.elem) * field.type.size;
                else if (field.type.kind === "array") {
                    const storage = uniformArrayElementFor(field.type.elem);
                    cursor += storage.strideBytes * field.type.size;
                } else {
                    cursor += roundUp(naturalSizeBytes(field.type), 16);
                }
            }
            return roundUp(cursor, 16);
        }

        // Uniform storage has padding, widened arrays and integer booleans.
        // Explicit value conversion preserves ordinary GLSL struct/array types.
        declareStruct(type) {
            if (this.structNames.has(type.name)) return this.structNames.get(type.name);
            const wgslName = "vgl_storage_" + safeName(type.name);
            this.structNames.set(type.name, wgslName);
            const lines = ["struct " + wgslName + " {"];
            for (const field of type.fields) {
                if (field.type.kind === "sampler" || field.type.kind === "array" && field.type.elem.kind === "sampler") continue;
                let wgslType;
                let size;
                if (field.type.kind === "struct") {
                    wgslType = this.declareStruct(field.type);
                    size = this.structSizeBytes(field.type);
                } else if (field.type.kind === "array" && field.type.elem.kind === "struct") {
                    wgslType = "array<" + this.declareStruct(field.type.elem) + ", " +
                        field.type.size + ">";
                    size = this.structSizeBytes(field.type.elem) * field.type.size;
                } else if (field.type.kind === "array") {
                    const storage = uniformArrayElementFor(field.type.elem);
                    wgslType = "array<" + storage.wgsl + ", " + field.type.size + ">";
                    size = storage.strideBytes * field.type.size;
                } else {
                    wgslType = uniformStorageFor(field.type).wgsl;
                    size = roundUp(naturalSizeBytes(field.type), 16);
                }
                lines.push("    @align(16) @size(" + size + ") " +
                    safeName(field.name) + " : " + wgslType + ",");
            }
            lines.push("}");
            this.structs.push(lines.join("\n"));
            return wgslName;
        }

        structText() {
            const lines = ["struct GLUniforms {"];
            if (!this.members.length) {
                lines.push("    _unused : vec4<f32>,");
            } else {
                for (const m of this.members)
                    lines.push("    @align(16) @size(" + m.size + ") " +
                        m.wgslName + " : " + m.wgslType + ",");
            }
            lines.push("}");
            return (this.structs.length ? this.structs.join("\n\n") + "\n\n" : "") +
                lines.join("\n");
        }
    }

    /* ================================================================== */
    /* Link-time interface assignment                                     */
    /* ================================================================== */

    const MAX_INTER_STAGE_SLOTS = 16;
    const MAX_VERTEX_ATTRIBUTES = 16;

    /*
     * Attribute locations.
     *
     * The compatibility attributes sit at their historical slots (gl_Vertex 0,
     * gl_MultiTexCoord0 8, ...) because engines and tools of this era assume
     * them. glBindAttribLocation wins over everything. Generic attributes with
     * neither a binding nor a fixed slot are packed downward from 15, which
     * keeps them clear of gl_Vertex/gl_Normal/gl_Color at the bottom -- the
     * aliasing that made this layout famous is only safe when the two never
     * both appear, and here they can.
     */
    /* Desktop GL's generic attributes fill from the top, below the
     * compatibility ones' fixed slots; GLSL ES has none of those, so with
     * `ascending` (options.es) they take the lowest free locations in
     * declaration order, as ES drivers commonly do. */
    function assignAttributeLocations(usage, bindings, ascending) {
        const used = new Set();
        const result = new Map();
        for (const [name, info] of usage.attributes) {
            if (info.isCompat) {
                const loc = COMPAT_ATTRIBUTE_LOCATIONS[name];
                result.set(name, loc);
                used.add(loc);
            }
        }
        for (const [name, info] of usage.attributes) {
            if (info.isCompat) continue;
            const bound = bindings && bindings.get(name);
            if (bound === undefined || bound === null) continue;
            if (bound < 0 || bound >= MAX_VERTEX_ATTRIBUTES)
                throw new GLSLError("glBindAttribLocation(" + name + ", " + bound +
                    ") is outside the 0.." + (MAX_VERTEX_ATTRIBUTES - 1) +
                    " range", 0);
            if (used.has(bound))
                throw new GLSLError("attribute location " + bound +
                    " is claimed twice (" + name + ")", 0);
            result.set(name, bound);
            used.add(bound);
        }
        if (ascending) {
            for (const [name, info] of usage.attributes) {
                if (info.isCompat || result.has(name)) continue;
                const slots = attributeSlotCount(info.type);
                let base = 0;
                while (base + slots <= MAX_VERTEX_ATTRIBUTES && !slotsFree(used, base, slots)) ++base;
                if (base + slots > MAX_VERTEX_ATTRIBUTES)
                    throw new GLSLError("out of vertex attribute slots for '" +
                        name + "'", 0);
                for (let i = 0; i < slots; ++i) used.add(base + i);
                result.set(name, base);
            }
            return result;
        }
        let next = MAX_VERTEX_ATTRIBUTES - 1;
        for (const [name, info] of usage.attributes) {
            if (info.isCompat || result.has(name)) continue;
            const slots = attributeSlotCount(info.type);
            while (next >= 0 && !slotsFree(used, next - slots + 1, slots)) --next;
            if (next - slots + 1 < 0)
                throw new GLSLError("out of vertex attribute slots for '" +
                    name + "'", 0);
            const base = next - slots + 1;
            for (let i = 0; i < slots; ++i) used.add(base + i);
            result.set(name, base);
            next = base - 1;
        }
        return result;
    }

    function slotsFree(used, base, count) {
        if (base < 0) return false;
        for (let i = 0; i < count; ++i) if (used.has(base + i)) return false;
        return true;
    }

    /* A mat3 attribute occupies three consecutive locations, as it does in
     * desktop GL. */
    function attributeSlotCount(type) {
        return type.kind === "matrix" ? type.cols : 1;
    }

    /*
     * Varying assignment.
     *
     * Only varyings written by the vertex stage *and* read by the fragment
     * stage get a slot: dropping the rest is what keeps gl_TexCoord[8] from
     * eating half the budget on a shader that touches gl_TexCoord[0] alone.
     * Small varyings are then packed into shared vec4 slots, greedily and in
     * declaration order, so the packing is stable across compiles and the
     * shader cache can key on it.
     */
    function assignVaryings(vsUsage, fsUsage, options) {
        const entries = [];

        const texCoordCount = varyingArrayExtent(vsUsage, fsUsage, "gl_TexCoord");
        const addCompat = (name, type) => {
            const inVS = vsUsage.varyings.has(name);
            const inFS = fsUsage.varyings.has(name);
            if (!inVS || !inFS) return;
            entries.push({ name, type, compat: true, components: componentCount(type) });
        };

        // gl_Color in the fragment stage is fed by gl_FrontColor (and
        // gl_BackColor when two-sided lighting is on), so the linkage is by
        // role, not by name.
        for (const [fsName, link] of Object.entries(VARYING_LINKAGE)) {
            if (fsName === "gl_TexCoord") continue;
            if (!fsUsage.varyings.has(fsName)) continue;
            const type = FRAGMENT_VARYING_INPUTS[fsName];
            const frontWritten = vsUsage.varyings.has(link.front);
            const backWritten = link.back && vsUsage.varyings.has(link.back);
            if (!frontWritten && !backWritten && !options?.keepCompatInputs) continue;
            entries.push({
                name: fsName, type, compat: true, components: componentCount(type),
                vsFront: frontWritten ? link.front : null,
                vsBack: backWritten ? link.back : null,
                flat: !!(options && options.flatShading) &&
                    (fsName === "gl_Color" || fsName === "gl_SecondaryColor"),
            });
            if (backWritten && options && options.twoSided) {
                entries.push({
                    name: fsName + "__back", type, compat: true,
                    components: componentCount(type),
                    vsFront: link.back, vsBack: null, isBackHalf: true,
                    flat: !!(options && options.flatShading),
                });
            }
        }
        void addCompat;

        for (let i = 0; i < texCoordCount; ++i) {
            entries.push({
                name: "gl_TexCoord", index: i, type: F.vec4, compat: true,
                components: 4,
            });
        }

        for (const [name, info] of vsUsage.varyings) {
            if (info.isCompat) continue;
            const fsInfo = fsUsage.varyings.get(name);
            if (!fsInfo) continue;
            if (!sameType(info.type, fsInfo.type))
                throw new GLSLError("varying '" + name + "' is declared " +
                    info.type.name + " in the vertex shader and " +
                    fsInfo.type.name + " in the fragment shader", 0);
            const type = info.type;
            if (type.kind === "array") {
                for (let i = 0; i < type.size; ++i)
                    entries.push({ name, index: i, type: type.elem,
                                   components: componentCount(type.elem) });
            } else if (type.kind === "matrix") {
                for (let c = 0; c < type.cols; ++c)
                    entries.push({ name, column: c, type: vec("float", type.rows),
                                   components: type.rows });
            } else {
                entries.push({ name, type, components: componentCount(type) });
            }
        }

        // Greedy packing. Four-component entries take a whole slot; the rest
        // fill the first slot with room, which is why the vec4s are placed
        // first -- otherwise a stream of floats fragments every slot.
        const slots = [];
        const place = entry => {
            if (entry.components === 4) {
                slots.push({ used: 4, entries: [entry] });
                entry.slot = slots.length - 1;
                entry.offset = 0;
                return;
            }
            for (let i = 0; i < slots.length; ++i) {
                if (slots[i].used + entry.components <= 4) {
                    entry.slot = i;
                    entry.offset = slots[i].used;
                    slots[i].used += entry.components;
                    slots[i].entries.push(entry);
                    return;
                }
            }
            slots.push({ used: entry.components, entries: [entry] });
            entry.slot = slots.length - 1;
            entry.offset = 0;
        };
        entries.filter(e => e.components === 4).forEach(place);
        entries.filter(e => e.components !== 4).forEach(place);

        if (slots.length > MAX_INTER_STAGE_SLOTS)
            throw new GLSLError("the program needs " + slots.length +
                " varying slots but only " + MAX_INTER_STAGE_SLOTS +
                " are available (deviation D-12)", 0);

        return { entries, slotCount: slots.length };
    }

    /* How many gl_TexCoord elements the pair actually needs. A dynamic index
     * on either side forces the full array, because nothing else can answer
     * gl_TexCoord[i] for a runtime i. */
    function varyingArrayExtent(vsUsage, fsUsage, name) {
        if (!vsUsage.varyings.has(name) || !fsUsage.varyings.has(name)) return 0;
        if (vsUsage.texCoordDynamic || fsUsage.texCoordDynamic)
            return MAX_TEXTURE_COORDS;
        const max = Math.max(vsUsage.texCoordMax, fsUsage.texCoordMax);
        return max < 0 ? 0 : max + 1;
    }

    function varyingKey(entry) {
        if (entry.index !== undefined) return entry.name + "[" + entry.index + "]";
        if (entry.column !== undefined) return entry.name + "$" + entry.column;
        return entry.name;
    }

    const SWIZZLE_CHARS = "xyzw";
    function slotSwizzle(offset, components) {
        return SWIZZLE_CHARS.slice(offset, offset + components);
    }

    /* ================================================================== */
    /* Code generation                                                    */
    /* ================================================================== */

    // WGSL keyword/builtin namespace (Naga 30; W3C WGSL keyword summary).
    // Escape the escape prefix too: two different GLSL names never collapse.
    const WGSL_RESERVED = new Set([
        "alias", "break", "case", "const", "const_assert", "continue", "continuing", "default",
        "diagnostic", "discard", "else", "enable", "false", "fn", "for", "if",
        "let", "loop", "override", "requires", "return", "struct", "switch", "true",
        "var", "while", "NULL", "Self", "abstract", "active", "alignas", "alignof",
        "as", "asm", "asm_fragment", "async", "attribute", "auto", "await", "become",
        "cast", "catch", "class", "co_await", "co_return", "co_yield", "coherent", "column_major",
        "common", "compile", "compile_fragment", "concept", "const_cast", "consteval", "constexpr", "constinit",
        "crate", "debugger", "decltype", "delete", "demote", "demote_to_helper", "do", "dynamic_cast",
        "enum", "explicit", "export", "extends", "extern", "external", "fallthrough", "filter",
        "final", "finally", "friend", "from", "fxgroup", "get", "goto", "groupshared",
        "highp", "impl", "implements", "import", "inline", "instanceof", "interface", "layout",
        "lowp", "macro", "macro_rules", "match", "mediump", "meta", "mod", "module",
        "move", "mut", "mutable", "namespace", "new", "nil", "noexcept", "noinline",
        "nointerpolation", "non_coherent", "noncoherent", "noperspective", "null", "nullptr", "of", "operator",
        "package", "packoffset", "partition", "pass", "patch", "pixelfragment", "precise", "precision",
        "premerge", "priv", "protected", "pub", "public", "readonly", "ref", "regardless",
        "register", "reinterpret_cast", "require", "resource", "restrict", "self", "set", "shared",
        "sizeof", "smooth", "snorm", "static", "static_assert", "static_cast", "std", "subroutine",
        "super", "target", "template", "this", "thread_local", "throw", "trait", "try",
        "type", "typedef", "typeid", "typename", "typeof", "union", "unless", "unorm",
        "unsafe", "unsized", "use", "using", "varying", "virtual", "volatile", "wgsl",
        "where", "with", "writeonly", "yield", "bool", "i32", "u32", "f32",
        "f16", "array", "atomic", "vec2", "vec3", "vec4", "mat2x2", "mat2x3",
        "mat2x4", "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4", "ptr",
        "sampler", "sampler_comparison", "texture_1d", "texture_2d", "texture_2d_array", "texture_3d", "texture_cube", "texture_cube_array",
        "texture_multisampled_2d", "texture_depth_multisampled_2d", "texture_external", "texture_storage_1d", "texture_storage_2d", "texture_storage_2d_array", "texture_storage_3d", "texture_depth_2d",
        "texture_depth_2d_array", "texture_depth_cube", "texture_depth_cube_array", "read", "write", "read_write", "function", "private",
        "workgroup", "uniform", "storage", "rgba8unorm", "rgba8snorm", "rgba8uint", "rgba8sint", "rgba16unorm",
        "rgba16snorm", "rgba16uint", "rgba16sint", "rgba16float", "rg8unorm", "rg8snorm", "rg8uint", "rg8sint",
        "rg16unorm", "rg16snorm", "rg16uint", "rg16sint", "rg16float", "r32uint", "r32sint", "r32float",
        "rg32uint", "rg32sint", "rg32float", "rgba32uint", "rgba32sint", "rgba32float", "bgra8unorm", "r8unorm",
        "r8snorm", "r8uint", "r8sint", "r16unorm", "r16snorm", "r16uint", "r16sint", "r16float",
        "rgb10a2unorm", "rgb10a2uint", "rg11b10ufloat", "bitcast", "all", "any", "select", "arrayLength",
        "abs", "acos", "acosh", "asin", "asinh", "atan", "atanh", "atan2",
        "ceil", "clamp", "cos", "cosh", "countLeadingZeros", "countOneBits", "countTrailingZeros", "cross",
        "degrees", "determinant", "distance", "dot", "dot4U8Packed", "dot4I8Packed", "exp", "exp2",
        "extractBits", "faceForward", "firstLeadingBit", "firstTrailingBit", "floor", "fma", "fract", "frexp",
        "insertBits", "inverseSqrt", "ldexp", "length", "log", "log2", "max", "min",
        "mix", "modf", "normalize", "pow", "quantizeToF16", "radians", "reflect", "refract",
        "reverseBits", "round", "saturate", "sign", "sin", "sinh", "smoothstep", "sqrt",
        "step", "tan", "tanh", "transpose", "trunc", "dpdx", "dpdxCoarse", "dpdxFine",
        "dpdy", "dpdyCoarse", "dpdyFine", "fwidth", "fwidthCoarse", "fwidthFine", "textureDimensions", "textureGather",
        "textureGatherCompare", "textureLoad", "textureNumLayers", "textureNumLevels", "textureNumSamples", "textureSample", "textureSampleBias", "textureSampleCompare",
        "textureSampleCompareLevel", "textureSampleGrad", "textureSampleLevel", "textureSampleBaseClampToEdge", "textureStore", "atomicLoad", "atomicStore", "atomicAdd",
        "atomicSub", "atomicMax", "atomicMin", "atomicAnd", "atomicOr", "atomicXor", "atomicExchange", "atomicCompareExchangeWeak",
        "pack4x8snorm", "pack4x8unorm", "pack4xI8", "pack4xU8", "pack4xI8Clamp", "pack4xU8Clamp", "pack2x16snorm", "pack2x16unorm",
        "pack2x16float", "unpack4x8snorm", "unpack4x8unorm", "unpack4xI8", "unpack4xU8", "unpack2x16snorm", "unpack2x16unorm", "unpack2x16float",
        "storageBarrier", "textureBarrier", "workgroupBarrier", "workgroupUniformLoad", "subgroupAdd", "subgroupExclusiveAdd", "subgroupInclusiveAdd", "subgroupAll",
        "subgroupAnd", "subgroupAny", "subgroupBallot", "subgroupBroadcast", "subgroupBroadcastFirst", "subgroupElect", "subgroupMax", "subgroupMin",
        "subgroupMul", "subgroupExclusiveMul", "subgroupInclusiveMul", "subgroupOr", "subgroupShuffle", "subgroupShuffleDown", "subgroupShuffleUp", "subgroupShuffleXor",
        "subgroupXor", "quadBroadcast", "quadSwapDiagonal", "quadSwapX", "quadSwapY", "i64", "u64", "f64",
        "push_constant", "r64uint", "glState", "glUniforms", "fsin", "vsin", "out",
    ]);

    function safeName(name) {
        if (!WGSL_RESERVED.has(name) && !["GLState", "GLUniforms", "VSIn", "VSOut", "FSIn", "FSOut"].includes(name) &&
                !/^(?:__|vgl_|glsl_|_t[0-9]|[uts]_|u_at_|gl_sampler_index_)/.test(name) && /^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) return name;
        return "vgl_" + Array.from(name, c => c.charCodeAt(0).toString(16)).join("_");
    }

    function floatLiteral(value) {
        if (!isFinite(Math.fround(value)))
            return value > 0 ? "3.4028235e38" : (value < 0 ? "-3.4028235e38" : "0.0");
        if (Number.isInteger(value) && Math.abs(value) < 1e15)
            return value.toFixed(1);
        let text = String(value);
        if (!/[.eE]/.test(text)) text += ".0";
        return text;
    }

    class Generator {
        constructor(stage, ast, usage, iface, options) {
            this.stage = stage;
            this.ast = ast;
            this.usage = usage;
            this.iface = iface;
            this.options = options || {};
            this.lines = [];
            this.indent = 0;
            this.tempSerial = 0;
            this.stats = { nonUniformSamples: 0, uniformSamples: 0,
                           gradSamples: 0, levelSamples: 0 };
            this.controlFlowUniform = [true];
            // Every varying entry, keyed the way the AST reaches it.
            this.varyingBySlot = new Map();
            for (const entry of iface.varyings.entries)
                this.varyingBySlot.set(varyingKey(entry), entry);
        }

        get inUniformControlFlow() {
            return this.controlFlowUniform[this.controlFlowUniform.length - 1];
        }

        emit(text) {
            this.lines.push("    ".repeat(this.indent) + text);
        }
        emitAll(list) { for (const l of list) this.emit(l); }
        temp() { return "_t" + (this.tempSerial++); }

        /* ---- module scope ---- */

        generate() {
            this.emit("// generated by gl_shader_translator.js r" +
                TRANSLATOR_REVISION + " (" + this.stage + ")");
            // GL leaves derivatives in non-uniform control flow undefined and
            // drivers compute them anyway; WGSL refuses the shader (Tint
            // checks it, naga does not), as VX's and GX's shaders say too
            this.emit("diagnostic(off, derivative_uniformity);");
            this.emit("");
            this.emitBindings();
            if (this.options.desktopTextureState) {
                const types = new Map(), add = type => {
                    const dim = type.dim === "Cube" ? "Cube" : type.dim === "3D" ? "3D" : "2D", name = "gl_sample_" + dim + (type.shadow ? "_shadow" : "");
                    types.set(name, { dim, shadow: type.shadow });
                };
                for (const sampler of this.iface.samplers) add(sampler.type);
                for (const decl of this.ast.decls) for (const param of decl.params || []) if (param.type.kind === "sampler") add(param.type);
                for (const [name, type] of types) this.emit(stateLayout.textureSamplingWGSL(name, type.dim, type.shadow, this.stage));
            }
            this.emitUserStructs();
            this.emitIOStructs();
            this.emitPrivates();
            this.emitFunctions();
            this.emitEntryPoint();
            return this.lines.join("\n") + "\n";
        }

        emitBindings() {
            const layout = this.iface.stateLayout;
            if (layout.structText) {
                this.emitAll(layout.structText.split("\n"));
                this.emit("@group(1) @binding(0) var<uniform> glState : GLState;");
                this.emit("");
            }
            this.emitAll(this.iface.uniformLayout.structText().split("\n"));
            this.emit("@group(1) @binding(1) var<uniform> glUniforms : GLUniforms;");
            this.emit("");
            for (const s of this.iface.samplers) {
                const count = s.arraySize || 1;
                for (let i = 0; i < count; ++i) {
                    const suffix = s.arraySize ? "_" + i : "";
                    this.emit("@group(2) @binding(" + (s.binding + i * 2) + ") var t_" +
                        s.wgslName + suffix + " : " + wgslTextureType(s.type) + ";");
                    this.emit("@group(2) @binding(" + (s.binding + i * 2 + 1) + ") var s_" +
                        s.wgslName + suffix + " : " +
                        (s.type.shadow ? "sampler_comparison" : "sampler") + ";");
                }
            }
            if (this.iface.samplers.length) this.emit("");
            if (this.stage === "fragment" && this.options.logicOp &&
                    this.options.logicOp !== 0x1503) {
                for (let i = 0; i < this.iface.colorTargets; ++i)
                    this.emit("@group(3) @binding(" + i +
                        ") var glLogicTarget" + i + " : texture_2d<f32>;");
                this.emit("fn glApplyLogic(src : vec4<f32>, dst : vec4<f32>) " +
                    "-> vec4<f32> {");
                ++this.indent;
                this.emit("let s = vec4<u32>(round(clamp(src, vec4<f32>(0.0), " +
                    "vec4<f32>(1.0)) * 255.0));");
                this.emit("let d = vec4<u32>(round(clamp(dst, vec4<f32>(0.0), " +
                    "vec4<f32>(1.0)) * 255.0));");
                this.emit("let r = (" + logicOpExpression(this.options.logicOp) +
                    ") & vec4<u32>(255u);");
                this.emit("return vec4<f32>(r) / 255.0;");
                --this.indent;
                this.emit("}");
                this.emit("");
            }
            for (const accessor of this.iface.uniformLayout.accessors) {
                this.emitAll(accessor.split("\n"));
            }
            if (this.iface.uniformLayout.accessors.length) this.emit("");
        }

        emitUserStructs() {
            const seen = new Set();
            // Value structs are distinct from the padded, integer-backed storage twins.
            const emitStruct = type => {
                if (seen.has(type.name)) return;
                seen.add(type.name);
                for (const f of type.fields)
                    if (f.type.kind === "struct") emitStruct(f.type);
                    else if (f.type.kind === "array" && f.type.elem.kind === "struct")
                        emitStruct(f.type.elem);
                this.emit("struct " + safeName(type.name) + " {");
                ++this.indent;
                for (const f of type.fields) if (f.type.kind !== "sampler" && !(f.type.kind === "array" && f.type.elem.kind === "sampler"))
                    this.emit(safeName(f.name) + " : " + typeToWGSL(f.type) + ",");
                --this.indent;
                this.emit("}");
                this.emit("");
            };
            for (const decl of this.ast.decls) {
                if (decl.kind === "StructDecl") emitStruct(decl.struct.type);
                else if (decl.kind === "VarDecl" && decl.struct)
                    emitStruct(decl.struct.type);
            }
            // Structs introduced only as local declarations or return types.
            const walkType = t => {
                if (!t) return;
                if (t.kind === "struct") emitStruct(t);
                else if (t.kind === "array") walkType(t.elem);
            };
            for (const decl of this.ast.decls) {
                if (decl.kind === "FunctionDef" || decl.kind === "FunctionProto") {
                    walkType(decl.returnType);
                    for (const p of decl.params) walkType(p.type);
                }
            }
        }

        emitIOStructs() {
            if (this.stage === "vertex" && this.options.vertexCapture) {
                this.emitAll(vertexCaptureBindings(this.options.vertexCaptureRaster).split("\n"));
                return;
            }
            const varyings = this.iface.varyings;
            const slotDecls = [];
            const slotCount = new Map();
            for (const entry of varyings.entries) {
                const prev = slotCount.get(entry.slot) || 0;
                slotCount.set(entry.slot, Math.max(prev, entry.offset + entry.components));
            }
            for (const [slot, used] of [...slotCount.entries()].sort((a, b) => a[0] - b[0])) {
                const flat = varyings.entries.some(e => e.slot === slot && e.flat);
                slotDecls.push({ slot, used, flat });
            }
            this.slotDecls = slotDecls;

            if (this.stage === "vertex") {
                this.emit("struct VSIn {");
                ++this.indent;
                for (const [name, loc] of this.iface.attributes) {
                    const info = this.usage.attributes.get(name);
                    if (!info) continue;
                    if (info.type.kind === "matrix") {
                        for (let c = 0; c < info.type.cols; ++c)
                            this.emit("@location(" + (loc + c) + ") " +
                                attribFieldName(name) + "_" + c + " : " +
                                typeToWGSL(vec("float", info.type.rows)) + ",");
                    } else {
                        this.emit("@location(" + loc + ") " + attribFieldName(name) +
                            " : " + typeToWGSL(info.type) + ",");
                    }
                }
                if (this.options.pointSprite || !this.iface.attributes.size)
                    this.emit("@builtin(vertex_index) glVertexIndex : u32,");
                --this.indent;
                this.emit("}");
                this.emit("");
                this.emit("struct VSOut {");
                ++this.indent;
                // GL engines share the position calculation between depth-only
                // and shaded programs. Preserve it across backend optimization
                // so LEQUAL/EQUAL multipass rendering does not develop holes.
                this.emit("@invariant @builtin(position) position : vec4<f32>,");
                for (const d of slotDecls)
                    this.emit((d.flat ? "@interpolate(flat) " : "") +
                        "@location(" + d.slot + ") v" + d.slot + " : vec4<f32>,");
                if (this.iface.clipPlaneCount)
                    this.emit("@location(" + this.iface.clipVaryingSlot +
                        ") glClipDist : vec4<f32>,");
                if (this.iface.clipPlaneCount > 4) this.emit("@location(" + this.iface.clipExtraSlot + ") glClipExtra:vec2<f32>,");
                if (this.options.pointSprite)
                    this.emit("@location(" + this.iface.pointCoordSlot +
                        ") glPointCoord : vec2<f32>,");
                --this.indent;
                this.emit("}");
                this.emit("");
            } else {
                this.emit("struct FSIn {");
                ++this.indent;
                this.emit("@builtin(position) position : vec4<f32>,");
                if (this.usage.builtinInputs.has("gl_FrontFacing") ||
                        this.iface.twoSided)
                    this.emit("@builtin(front_facing) frontFacing : bool,");
                for (const d of slotDecls)
                    this.emit((d.flat ? "@interpolate(flat) " : "") +
                        "@location(" + d.slot + ") v" + d.slot + " : vec4<f32>,");
                if (this.iface.clipPlaneCount)
                    this.emit("@location(" + this.iface.clipVaryingSlot +
                        ") glClipDist : vec4<f32>,");
                if (this.iface.clipPlaneCount > 4) this.emit("@location(" + this.iface.clipExtraSlot + ") glClipExtra:vec2<f32>,");
                if (this.usage.usesPointCoord)
                    this.emit("@location(" + this.iface.pointCoordSlot +
                        ") glPointCoord : vec2<f32>,");
                --this.indent;
                this.emit("}");
                this.emit("");
                this.emit("struct FSOut {");
                ++this.indent;
                const targets = this.iface.colorTargets;
                for (let i = 0; i < targets; ++i)
                    this.emit("@location(" + i + ") color" + i + " : vec4<f32>,");
                if (this.usage.writesFragDepth)
                    this.emit("@builtin(frag_depth) depth : f32,");
                --this.indent;
                this.emit("}");
                this.emit("");
            }
        }

        emitPrivates() {
            const declared = new Set();
            const declare = (name, type, init) => {
                if (declared.has(name)) return;
                declared.add(name);
                this.emit("var<private> " + name + " : " + typeToWGSL(type) +
                    (init ? " = " + init : "") + ";");
            };

            if (this.stage === "vertex") {
                for (const name of this.usage.attributes.keys()) {
                    const info = this.usage.attributes.get(name);
                    declare(safeName(name), info.type);
                }
                declare("gl_Position", F.vec4);
                for (const name of this.usage.builtinOutputs)
                    declare(name, VERTEX_BUILTIN_OUTPUTS[name].type);
                for (const [name, info] of this.usage.varyings)
                    declare(safeName(name), info.type);
            } else {
                for (const name of this.usage.builtinInputs)
                    declare(name, FRAGMENT_BUILTIN_INPUTS[name].type);
                for (const name of this.usage.builtinOutputs)
                    declare(name, FRAGMENT_BUILTIN_OUTPUTS[name].type);
                for (const [name, info] of this.usage.varyings)
                    declare(safeName(name), info.type);
            }

            // User globals that are neither uniform nor interface variables.
            for (const decl of this.ast.decls) {
                if (decl.kind !== "VarDecl") continue;
                const storage = decl.qualifiers.storage;
                if (storage === "uniform" || storage === "attribute" ||
                        storage === "varying") continue;
                for (const d of decl.declarators) {
                    if (storage === "const" && d.init &&
                            isConstantFoldable(d.init)) {
                        const pre = [];
                        this.emit("const " + safeName(d.name) + " : " +
                            typeToWGSL(d.type) + " = " +
                            this.genExpr(d.init, pre, d.type) + ";");
                        declared.add(safeName(d.name));
                        continue;
                    }
                    // A global initializer that is not a constant expression
                    // has to run somewhere; GLSL says at declaration time, so
                    // the entry point runs it before main().
                    declare(safeName(d.name), d.type);
                    if (d.init) {
                        this.deferredGlobalInits = this.deferredGlobalInits || [];
                        this.deferredGlobalInits.push(d);
                    }
                }
            }
            this.emit("");
        }

        emitFunctions() {
            for (const decl of this.ast.decls) {
                if (decl.kind !== "FunctionDef") continue;
                this.genFunction(decl);
            }
        }

        genFunction(decl) {
            const overload = decl.overload;
            const params = [];
            for (const p of decl.params) {
                if (!p.name) continue;
                if (p.type.kind === "sampler") {
                    params.push("t_" + safeName(p.name) + " : " +
                        wgslTextureType(p.type));
                    params.push("s_" + safeName(p.name) + " : " +
                        (p.type.shadow ? "sampler_comparison" : "sampler"));
                    if (this.options.desktopTextureState) params.push("gl_sampler_index_" + safeName(p.name) + " : u32");
                    continue;
                }
                if (p.direction === "in") {
                    params.push(safeName(p.name) + " : " + typeToWGSL(p.type));
                } else {
                    params.push(safeName(p.name) + " : ptr<function, " +
                        typeToWGSL(p.type) + ">");
                }
            }
            const ret = overload.returnType.kind === "void" ? "" :
                " -> " + typeToWGSL(overload.returnType);
            this.emit("fn " + overload.mangled + "(" + params.join(", ") + ")" +
                ret + " {");
            ++this.indent;
            this.currentMangled = overload.mangled;
            // An `in` parameter is writable in GLSL but is a `let` in WGSL, so
            // any parameter that is assigned needs a local copy. Copying all of
            // them would be wasteful for the common read-only case, so only the
            // assigned ones get one -- which the analyzer already knows.
            for (const p of decl.params) {
                if (!p.name || p.direction !== "in" || p.type.kind === "sampler")
                    continue;
                if (!parameterIsAssigned(decl, p.name)) continue;
                this.emit("var " + safeName(p.name) + "_mut : " +
                    typeToWGSL(p.type) + " = " + safeName(p.name) + ";");
                this.mutableParams = this.mutableParams || new Set();
                this.mutableParams.add(overload.mangled + ":" + p.name);
            }
            this.genBlockBody(decl.body);
            this.currentMangled = null;
            if (overload.returnType.kind !== "void")
                this.emit("return " + zeroValueWGSL(overload.returnType) + ";");
            --this.indent;
            this.emit("}");
            this.emit("");
        }

        genBlockBody(block) {
            for (const stmt of block.stmts) this.genStatement(stmt);
        }

        /* ---- statements ---- */

        genStatement(node) {
            switch (node.kind) {
            case "Block":
                this.emit("{");
                ++this.indent;
                this.genBlockBody(node);
                --this.indent;
                this.emit("}");
                return;
            case "VarDecl": return this.genLocalDecl(node);
            case "StructDecl": return;
            case "ExprStmt": {
                const pre = [];
                const text = this.genExpr(node.expr, pre, null, true);
                this.emitAll(pre);
                if (text) this.emit(text + ";");
                return;
            }
            case "If": return this.genIf(node);
            case "While": return this.genWhile(node);
            case "DoWhile": return this.genDoWhile(node);
            case "For": return this.genFor(node);
            case "Return": {
                const pre = [];
                if (node.expr) {
                    const text = this.genExpr(node.expr, pre,
                        node.expr.convertTo || null);
                    this.emitAll(pre);
                    this.emit("return " + text + ";");
                } else {
                    this.emit("return;");
                }
                return;
            }
            case "Break": this.emit("break;"); return;
            case "Continue": this.emit("continue;"); return;
            case "Discard": this.emit("discard;"); return;
            default:
                throw new GLSLError("cannot generate " + node.kind, node.line);
            }
        }

        genLocalDecl(node) {
            for (const d of node.declarators) {
                const pre = [];
                let init = null;
                if (d.init) init = this.genExpr(d.init, pre, d.type);
                this.emitAll(pre);
                this.emit("var " + safeName(d.name) + " : " + typeToWGSL(d.type) +
                    " = " + (init !== null ? init : zeroValueWGSL(d.type)) + ";");
            }
        }

        genIf(node) {
            const pre = [];
            const cond = this.genExpr(node.cond, pre);
            this.emitAll(pre);
            this.emit("if (" + cond + ") {");
            ++this.indent;
            this.controlFlowUniform.push(this.inUniformControlFlow &&
                !node.cond.nonUniform);
            this.genStatementInline(node.then);
            this.controlFlowUniform.pop();
            --this.indent;
            if (node.otherwise) {
                this.emit("} else {");
                ++this.indent;
                this.controlFlowUniform.push(this.inUniformControlFlow &&
                    !node.cond.nonUniform);
                this.genStatementInline(node.otherwise);
                this.controlFlowUniform.pop();
                --this.indent;
            }
            this.emit("}");
        }

        /* An if/else arm that is a Block should not add a second pair of
         * braces; anything else is emitted as-is. */
        genStatementInline(node) {
            if (node.kind === "Block") this.genBlockBody(node);
            else this.genStatement(node);
        }

        genWhile(node) {
            const pre = [];
            const cond = this.genExpr(node.cond, pre);
            this.controlFlowUniform.push(this.inUniformControlFlow &&
                !node.cond.nonUniform);
            if (!pre.length) {
                this.emit("while (" + cond + ") {");
                ++this.indent;
                this.genStatementInline(node.body);
                --this.indent;
                this.emit("}");
            } else {
                // A condition with side effects has to be re-evaluated at the
                // top of every iteration, which only the loop form allows.
                this.emit("loop {");
                ++this.indent;
                this.emitAll(pre);
                this.emit("if (!(" + cond + ")) { break; }");
                this.genStatementInline(node.body);
                --this.indent;
                this.emit("}");
            }
            this.controlFlowUniform.pop();
        }

        genDoWhile(node) {
            this.controlFlowUniform.push(false);
            this.emit("loop {");
            ++this.indent;
            this.genStatementInline(node.body);
            const pre = [];
            const cond = this.genExpr(node.cond, pre);
            // The test belongs in `continuing` so that `continue` inside the
            // body reaches it, which is what a GLSL do-while does.
            this.emit("continuing {");
            ++this.indent;
            this.emitAll(pre);
            this.emit("break if !(" + cond + ");");
            --this.indent;
            this.emit("}");
            --this.indent;
            this.emit("}");
            this.controlFlowUniform.pop();
        }

        genFor(node) {
            this.emit("{");
            ++this.indent;
            if (node.init) this.genStatement(node.init);
            const condPre = [];
            const cond = node.cond ? this.genExpr(node.cond, condPre) : null;
            const stepPre = [];
            const step = node.step ?
                this.genExpr(node.step, stepPre, null, true) : null;
            this.controlFlowUniform.push(this.inUniformControlFlow &&
                !(node.cond && node.cond.nonUniform));
            this.emit("loop {");
            ++this.indent;
            if (cond !== null) {
                this.emitAll(condPre);
                this.emit("if (!(" + cond + ")) { break; }");
            }
            this.genStatementInline(node.body);
            if (step !== null) {
                this.emit("continuing {");
                ++this.indent;
                this.emitAll(stepPre);
                this.emit(step + ";");
                --this.indent;
                this.emit("}");
            }
            --this.indent;
            this.emit("}");
            this.controlFlowUniform.pop();
            --this.indent;
            this.emit("}");
        }

        /* ---- expressions ---- */

        /*
         * `pre` collects statements that must run before the returned
         * expression is evaluated. That mechanism carries every construct GLSL
         * has and WGSL does not: assignment as a value, ++/--, out parameters,
         * and swizzle assignment (WGSL allows `v.x = e` but not `v.xy = e`).
         */
        genExpr(node, pre, targetType, asStatement) {
            let text = this.rootsAtUniform(node) && node.type.kind !== "sampler" ?
                this.uniformRead(node, pre) : this.genExprInner(node, pre, asStatement);
            const conv = targetType && !sameType(node.type, targetType) ?
                targetType : node.convertTo;
            if (conv && !sameType(node.type, conv))
                text = typeToWGSL(conv) + "(" + text + ")";
            return text;
        }

        uniformRead(node, pre) {
            const access = n => {
                if (n.kind === "Ident") return "glUniforms.u_" + safeName(n.symbol.name);
                if (n.kind === "Field") return access(n.object) + "." + safeName(n.name);
                return access(n.object) + "[" + this.genExpr(n.index, pre) + "]";
            };
            const convert = (type, expr, element) => {
                if (type.kind === "struct") return typeToWGSL(type) + "(" + type.fields.map(f =>
                    convert(f.type, expr + "." + safeName(f.name), false)).join(", ") + ")";
                if (type.kind === "array") return typeToWGSL(type) + "(" + Array.from({length: type.size}, (_, i) =>
                    convert(type.elem, expr + "[" + i + "]", true)).join(", ") + ")";
                return (element ? uniformArrayElementFor(type) : uniformStorageFor(type)).read(expr);
            };
            // A GLSL swizzle acts on the converted value, not widened storage.
            if (node.kind === "Field" && node.swizzle !== undefined) return this.genField(node, pre);
            return convert(node.type, access(node), node.kind === "Index" && node.object.type.kind === "array");
        }

        genExprInner(node, pre, asStatement) {
            switch (node.kind) {
            case "IntLit": return String(node.value);
            case "FloatLit": return floatLiteral(node.value);
            case "BoolLit": return node.value ? "true" : "false";
            case "Ident": return this.genIdent(node);
            case "Field": return this.genField(node, pre);
            case "Index": return this.genIndex(node, pre);
            case "Binary": return this.genBinary(node, pre);
            case "Unary": return this.genUnary(node, pre, asStatement);
            case "Assign": return this.genAssign(node, pre, asStatement);
            case "Conditional": return this.genConditional(node, pre);
            case "Call": return this.genCall(node, pre);
            case "ArrayLength": this.genExpr(node.object, pre);return String(node.value);
            case "Sequence": {
                const text = this.genExpr(node.left, pre, null, true);
                if (text) pre.push("    ".repeat(0) + text + ";");
                return this.genExpr(node.right, pre, null, asStatement);
            }
            default:
                throw new GLSLError("cannot generate " + node.kind, node.line);
            }
        }

        genIdent(node) {
            const symbol = node.symbol;
            switch (symbol.kind) {
            case "builtinConst": return String(symbol.value);
            case "stateUniform": {
                if (symbol.info.expr) return symbol.info.expr;
                throw new GLSLError("'" + symbol.name +
                    "' must be used with a member or an index", node.line);
            }
            case "uniform": {
                const base = "glUniforms.u_" + symbol.name;
                if (symbol.type.kind === "scalar" || symbol.type.kind === "vector")
                    return uniformStorageFor(symbol.type).read(base);
                return base;
            }
            case "param": {
                if (symbol.direction !== "in")
                    return "(*" + safeName(symbol.name) + ")";
                if (this.mutableParams &&
                        this.mutableParams.has(this.currentMangled + ":" + symbol.name))
                    return safeName(symbol.name) + "_mut";
                return safeName(symbol.name);
            }
            default:
                return safeName(symbol.name);
            }
        }

        genField(node, pre) {
            // gl_Fog.density and friends: GL packs several scalars into one
            // vec4, so the member map answers rather than a '.' append.
            if (node.object.kind === "Ident" && node.object.symbol &&
                    node.object.symbol.kind === "stateUniform") {
                const members = node.object.symbol.info.members;
                if (members && members[node.name]) return members[node.name];
            }
            const objText = this.genExpr(node.object, pre);
            if (node.swizzle !== undefined) {
                const chars = node.swizzle.map(i => SWIZZLE_CHARS[i]).join("");
                if (node.object.type.kind === "scalar") {
                    // `f.x` on a scalar is legal GLSL; in WGSL the scalar has
                    // no components, so the value stands for itself.
                    return node.swizzle.length === 1 ? objText :
                        typeToWGSL(node.type) + "(" + objText + ")";
                }
                return "(" + objText + ")." + chars;
            }
            const expr = "(" + objText + ")." + safeName(node.name);
            if (this.rootsAtUniform(node.object) &&
                    (node.type.kind === "scalar" || node.type.kind === "vector"))
                return uniformStorageFor(node.type).read(expr);
            return expr;
        }

        genIndex(node, pre) {
            const symbol = node.object.kind === "Ident" ? node.object.symbol : null;

            // gl_EyePlaneT[u] lives at texGenPlanes[u * 8 + 5]: one flat array
            // holds all four object planes then all four eye planes per unit.
            if (symbol && symbol.kind === "stateUniform" &&
                    symbol.info.planeStride !== undefined) {
                const idx = this.genExpr(node.index, pre);
                return "glState.texGenPlanes[(" + idx + ") * " +
                    symbol.info.planeStride + " + " + symbol.info.planeOffset + "]";
            }

            // A sampler array element resolves to its own binding, so the index
            // must be constant; the analyzer already refused a dynamic one.
            if (node.type.kind === "sampler")
                return "";              // handled by samplerPair()

            const objText = this.genExpr(node.object, pre);
            const idx = this.genExpr(node.index, pre);

            if (symbol && symbol.kind === "uniform" &&
                    symbol.type.kind === "array" &&
                    (symbol.type.elem.kind === "scalar" ||
                     symbol.type.elem.kind === "vector")) {
                // Widened to vec4 for the 16-byte uniform array stride; the
                // generated accessor puts the swizzle in one place.
                const leaf = this.iface.uniformLayout.leaves.get(symbol.name);
                if (leaf && leaf.readIndexed) return leaf.readIndexed(idx);
            }

            const expr = objText + "[" + idx + "]";
            if (this.rootsAtUniform(node.object) &&
                    node.object.type.kind === "array" &&
                    (node.type.kind === "scalar" || node.type.kind === "vector"))
                return uniformArrayElementFor(node.type).read(expr);
            return expr;
        }

        rootsAtUniform(node) {
            let n = node;
            while (n && (n.kind === "Field" || n.kind === "Index")) n = n.object;
            return !!(n && n.kind === "Ident" && n.symbol &&
                n.symbol.kind === "uniform");
        }

        genBinary(node, pre) {
            // Short-circuit operators must not evaluate the right side when
            // the left decides the result, so a right side that needs
            // statements becomes an if.
            if (node.op === "&&" || node.op === "||") {
                const rightPre = [];
                const left = this.genExpr(node.left, pre);
                const right = this.genExpr(node.right, rightPre);
                if (!rightPre.length)
                    return "(" + left + " " + node.op + " " + right + ")";
                const tmp = this.temp();
                pre.push("var " + tmp + " : bool = " + left + ";");
                pre.push("if (" + (node.op === "&&" ? tmp : "!" + tmp) + ") {");
                for (const l of rightPre) pre.push("    " + l);
                pre.push("    " + tmp + " = " + right + ";");
                pre.push("}");
                return tmp;
            }
            const left = this.genExpr(node.left, pre, node.left.convertTo);
            let right = this.genExpr(node.right, pre, node.right.convertTo);
            if ((node.op === "/" || node.op === "%") && isConstantFoldable(node.right)) {
                const tmp = this.temp();pre.push("var " + tmp + " : " + typeToWGSL(node.right.convertTo || node.right.type) + " = " + right + ";");right = tmp;
            }
            if (node.op === "^^")
                return "((" + left + ") != (" + right + "))";
            return this.binaryText(node.op, left, node.left.convertTo || node.left.type,
                right, node.right.convertTo || node.right.type, pre);
        }

        /*
         * A binary operator where GLSL and WGSL differ:
         *  - == and != of a vector, matrix, struct or array give one bool in
         *    GLSL; WGSL compares vectors componentwise and the rest not at all
         *  - &, | and ^ take a scalar beside a vector in GLSL, not in WGSL
         *  - a shift's amount is unsigned in WGSL (a vector's, a vector)
         */
        binaryText(op, left, lt, right, rt, pre) {
            if ((op === "==" || op === "!=") && lt.kind !== "scalar") {
                let a = left, b = right;
                if (lt.kind !== "vector") {
                    // the operands are read more than once
                    a = this.temp();
                    b = this.temp();
                    pre.push("let " + a + " = " + left + ";");
                    pre.push("let " + b + " = " + right + ";");
                }
                const equal = equalityWGSL(a, b, lt);
                return op === "==" ? equal : "(!" + equal + ")";
            }
            if (op === "<<" || op === ">>") {
                const amount = lt.kind !== "vector" ? "u32(" + right + ")" :
                    "vec" + lt.size + "<u32>(" + (rt.kind === "vector" ? right : "u32(" + right + ")") + ")";
                return "(" + left + " " + op + " " + amount + ")";
            }
            if ((op === "&" || op === "|" || op === "^") && lt.kind !== rt.kind) {
                if (lt.kind === "scalar") left = typeToWGSL(rt) + "(" + left + ")";
                else right = typeToWGSL(lt) + "(" + right + ")";
            }
            // WGSL has a matrix beside a scalar for * only, and matrices
            // divide by nothing: the rest is done column by column
            const matrix = lt.kind === "matrix" ? lt : rt.kind === "matrix" ? rt : null;
            if (matrix && (op === "+" || op === "-" || op === "/") &&
                    (lt.kind === "scalar" || rt.kind === "scalar" || op === "/")) {
                const a = this.temp(), b = this.temp();
                pre.push("let " + a + " = " + left + ";");
                pre.push("let " + b + " = " + right + ";");
                const columns = [];
                for (let c = 0; c < matrix.cols; ++c) {
                    columns.push("(" + (lt.kind === "matrix" ? a + "[" + c + "]" : a) + " " + op + " " +
                        (rt.kind === "matrix" ? b + "[" + c + "]" : b) + ")");
                }
                return typeToWGSL(matrix) + "(" + columns.join(", ") + ")";
            }
            return "(" + left + " " + op + " " + right + ")";
        }

        genUnary(node, pre, asStatement) {
            if (node.op === "++" || node.op === "--") {
                const target = this.lvalue(node.operand, pre);
                const one = baseOf(node.type) === "int" ? "1" : "1.0";
                const delta = node.op === "++" ? " + " : " - ";
                const isVector = node.type.kind !== "scalar";
                const rhs = node.type.kind === "matrix" ?
                    this.binaryText(delta.trim(), target.read, node.type, one, T.float, pre) :
                    isVector ?
                    target.read + delta + typeToWGSL(node.type) + "(" + one + ")" :
                    target.read + delta + one;
                if (node.prefix) {
                    this.assignTo(target, rhs, pre);
                    return asStatement ? "" : target.read;
                }
                const tmp = this.temp();
                pre.push("let " + tmp + " = " + target.read + ";");
                this.assignTo(target, rhs, pre);
                return asStatement ? "" : tmp;
            }
            const operand = this.genExpr(node.operand, pre);
            if (node.op === "+") return "(" + operand + ")";
            // (WGSL negates no matrix)
            if (node.op === "-" && node.type.kind === "matrix") return "(" + operand + " * -1.0)";
            return "(" + node.op + "(" + operand + "))";
        }

        /*
         * Resolves an assignment target into something that can be written.
         * `components` is set for a multi-component swizzle, which WGSL cannot
         * assign to in one statement.
         */
        lvalue(node, pre) {
            if (node.kind === "Field" && node.swizzle &&
                    node.swizzle.length > 1) {
                const base = this.lvalue(node.object, pre);
                return {
                    base, components: node.swizzle,
                    read: "(" + base.read + ")." +
                        node.swizzle.map(i => SWIZZLE_CHARS[i]).join(""),
                    type: node.type,
                };
            }
            if (node.kind === "Field" && node.swizzle) {
                const base = this.lvalue(node.object, pre);
                return { write: base.write + "." + SWIZZLE_CHARS[node.swizzle[0]],
                         read: base.read + "." + SWIZZLE_CHARS[node.swizzle[0]],
                         type: node.type };
            }
            if (node.kind === "Field") {
                const base = this.lvalue(node.object, pre);
                return { write: base.write + "." + safeName(node.name),
                         read: base.read + "." + safeName(node.name),
                         type: node.type };
            }
            if (node.kind === "Index") {
                const base = this.lvalue(node.object, pre);
                const idx = this.genExpr(node.index, pre);
                return { write: base.write + "[" + idx + "]",
                         read: base.read + "[" + idx + "]",
                         type: node.type };
            }
            if (node.kind === "Ident") {
                const text = this.genIdent(node);
                return { write: text, read: text, type: node.type };
            }
            throw new GLSLError("expression is not assignable", node.line);
        }

        assignTo(target, valueText, pre) {
            if (target.components) {
                const tmp = this.temp();
                pre.push("let " + tmp + " : " + typeToWGSL(target.type) +
                    " = " + valueText + ";");
                target.components.forEach((component, i) => {
                    const source = target.components.length === 1 ? tmp :
                        tmp + "." + SWIZZLE_CHARS[i];
                    pre.push(target.base.write + "." + SWIZZLE_CHARS[component] +
                        " = " + source + ";");
                });
                return;
            }
            pre.push(target.write + " = " + valueText + ";");
        }

        genAssign(node, pre, asStatement) {
            const target = this.lvalue(node.target, pre);
            let valueText;
            if (node.op === "=") {
                valueText = this.genExpr(node.value, pre,
                    node.value.convertTo || node.target.type);
            } else {
                const op = node.op.slice(0, -1);
                const rhs = this.genExpr(node.value, pre, node.value.convertTo);
                valueText = this.binaryText(op, target.read, node.target.type, rhs,
                    node.value.convertTo || node.value.type, pre);
            }
            if (!target.components && asStatement)
                return target.write + " = " + valueText;
            this.assignTo(target, valueText, pre);
            return asStatement ? "" : target.read;
        }

        genConditional(node, pre) {
            const thenPre = [];
            const elsePre = [];
            const cond = this.genExpr(node.cond, pre);
            const thenText = this.genExpr(node.then, thenPre, node.then.convertTo);
            const elseText = this.genExpr(node.otherwise, elsePre,
                node.otherwise.convertTo);
            if (!thenPre.length && !elsePre.length)
                return "select(" + elseText + ", " + thenText + ", " + cond + ")";
            // Only one arm may run, so anything with side effects becomes a
            // real branch rather than select()'s unconditional evaluation.
            const tmp = this.temp();
            pre.push("var " + tmp + " : " + typeToWGSL(node.type) + ";");
            pre.push("if (" + cond + ") {");
            for (const l of thenPre) pre.push("    " + l);
            pre.push("    " + tmp + " = " + thenText + ";");
            pre.push("} else {");
            for (const l of elsePre) pre.push("    " + l);
            pre.push("    " + tmp + " = " + elseText + ";");
            pre.push("}");
            return tmp;
        }

        genCall(node, pre) {
            if (node.constructType) return this.genConstructor(node, pre);
            if (node.builtinCall) {
                const args = node.args.map((a, i) =>
                    a.type.kind === "sampler" ? "" :
                    this.genExpr(a, pre, a.convertTo));
                // Defer arithmetic whose GL result may be undefined to runtime.
                if (!node.resolved.texture) node.args.forEach((arg, i) => {
                    if (isConstantFoldable(arg)) {
                        const tmp = this.temp();pre.push("var " + tmp + " : " + typeToWGSL(arg.convertTo || arg.type) + " = " + args[i] + ";");args[i] = tmp;
                    }
                });
                return node.resolved.emit(args, node, this.emitContext(node, pre));
            }
            const overload = node.resolved;
            const argTexts = [];
            const writebacks = [];
            overload.paramInfo.forEach((p, i) => {
                const arg = node.args[i];
                if (p.type.kind === "sampler") {
                    const pair = this.samplerPair(arg);
                    argTexts.push(pair.texture, pair.sampler);
                    if (this.options.desktopTextureState) argTexts.push(pair.index);
                    return;
                }
                if (p.direction === "in") {
                    argTexts.push(this.genExpr(arg, pre, arg.convertTo || p.type));
                    return;
                }
                // out/inout become pointers to a function-scope temporary; a
                // pointer to a private or to a struct member is not something
                // core WGSL allows, so the copy is not optional.
                const tmp = this.temp();
                const target = this.lvalue(arg, pre);
                if (p.direction === "inout")
                    pre.push("var " + tmp + " : " + typeToWGSL(p.type) +
                        " = " + target.read + ";");
                else
                    pre.push("var " + tmp + " : " + typeToWGSL(p.type) + ";");
                argTexts.push("&" + tmp);
                writebacks.push({ target, tmp });
            });
            const call = overload.mangled + "(" + argTexts.join(", ") + ")";
            if (!writebacks.length) return call;
            if (overload.returnType.kind === "void") {
                pre.push(call + ";");
                for (const w of writebacks) this.assignTo(w.target, w.tmp, pre);
                return "";
            }
            const result = this.temp();
            pre.push("let " + result + " = " + call + ";");
            for (const w of writebacks) this.assignTo(w.target, w.tmp, pre);
            return result;
        }

        genConstructor(node, pre) {
            const target = node.constructType;
            const args = node.args.map(a => ({
                text: this.genExpr(a, pre), type: a.type,
            }));
            /* vec4(v.rgb * n, a) reads three components out of one expression.
             * Emitting that expression once per component is correct but
             * quadratic in nesting depth, and it makes a shader dump unreadable
             * exactly when someone is reading it to find a bug. */
            for (const a of args) {
                if (componentCount(a.type) <= 1) continue;
                if (/^[A-Za-z_][A-Za-z0-9_.]*$/.test(a.text)) continue;
                const tmp = this.temp();
                pre.push("let " + tmp + " = " + a.text + ";");
                a.text = tmp;
            }

            if (target.kind === "struct")
                return safeName(target.name) + "(" +
                    node.args.map((a, i) =>
                        this.genExpr(a, pre, target.fields[i].type)).join(", ") + ")";

            if (target.kind === "array") return typeToWGSL(target) + "(" + node.args.map(a => this.genExpr(a, pre, target.elem)).join(", ") + ")";

            if (target.kind === "scalar")
                return typeToWGSL(target) + "(" + scalarize(args[0]) + ")";

            if (target.kind === "vector") {
                if (args.length === 1 && args[0].type.kind === "scalar")
                    return typeToWGSL(target) + "(" +
                        convertScalar(args[0], target.base) + ")";
                const components = [];
                for (const a of args) {
                    if (components.length >= target.size) break;
                    for (const c of componentsOf(a, target.base)) {
                        if (components.length >= target.size) break;
                        components.push(c);
                    }
                }
                return typeToWGSL(target) + "(" + components.join(", ") + ")";
            }

            if (target.kind === "matrix") {
                if (args.length === 1 && args[0].type.kind === "scalar") {
                    const cols = [];
                    for (let c = 0; c < target.cols; ++c) {
                        const parts = [];
                        for (let r = 0; r < target.rows; ++r)
                            parts.push(c === r ? convertScalar(args[0], "float") : "0.0");
                        cols.push("vec" + target.rows + "<f32>(" + parts.join(", ") + ")");
                    }
                    return typeToWGSL(target) + "(" + cols.join(", ") + ")";
                }
                if (args.length === 1 && args[0].type.kind === "matrix") {
                    // Resizing keeps the overlapping submatrix and fills the
                    // rest from the identity, which is what GLSL specifies.
                    const from = args[0].type;
                    const tmp = this.temp();
                    pre.push("let " + tmp + " = " + args[0].text + ";");
                    const cols = [];
                    for (let c = 0; c < target.cols; ++c) {
                        const parts = [];
                        for (let r = 0; r < target.rows; ++r) {
                            if (c < from.cols && r < from.rows)
                                parts.push(tmp + "[" + c + "][" + r + "]");
                            else parts.push(c === r ? "1.0" : "0.0");
                        }
                        cols.push("vec" + target.rows + "<f32>(" + parts.join(", ") + ")");
                    }
                    return typeToWGSL(target) + "(" + cols.join(", ") + ")";
                }
                if (node.matrixFromColumns)
                    return typeToWGSL(target) + "(" +
                        args.map(a => a.text).join(", ") + ")";
                const flat = [];
                for (const a of args)
                    for (const c of componentsOf(a, "float")) flat.push(c);
                const cols = [];
                for (let c = 0; c < target.cols; ++c) {
                    const parts = [];
                    for (let r = 0; r < target.rows; ++r)
                        parts.push(flat[c * target.rows + r] || "0.0");
                    cols.push("vec" + target.rows + "<f32>(" + parts.join(", ") + ")");
                }
                return typeToWGSL(target) + "(" + cols.join(", ") + ")";
            }
            throw new GLSLError("cannot construct " + target.name, node.line);
        }

        /* ---- texture sampling context handed to the built-in emitters ---- */

        emitContext(node, pre) {
            const self = this;
            return {
                samplerPair: argNode => self.samplerPair(argNode),
                desktopSample: self.options.desktopTextureState ? (pair, coord, level, bias, ref, explicit) => self.desktopSample(pair, coord, level, bias, ref, explicit) : null,
                sample: (pair, coord, bias) => self.emitSample(pair, coord, bias, node),
                compareSample: (pair, coord, ref) =>
                    self.emitCompareSample(pair, coord, ref, node),
                ftransform: () => "(glState.mvp * gl_Vertex)",
                pre,
            };
        }

        samplerPair(argNode) {
            const path = node => {
                if (node.kind === "Ident" && node.symbol?.kind === "uniform") return node.symbol.name;
                if (node.kind === "Field") {const parent = path(node.object);return parent && parent + "." + node.name;}
                if (node.kind === "Index" && node.constIndex !== null && node.constIndex !== undefined) {const parent = path(node.object);return parent && parent + "[" + node.constIndex + "]";}
                return null;
            };
            const uniformPath = path(argNode), resource = this.iface.samplers.find(s => s.name === uniformPath);
            if (resource) return { texture: "t_" + resource.wgslName, sampler: "s_" + resource.wgslName,
                index: resource.binding / 2 + "u", dim: argNode.type.dim, shadow: argNode.type.shadow };
            if (argNode.kind === "Index") {
                const base = argNode.object;
                if (base.kind !== "Ident")
                    throw new GLSLError("unsupported sampler expression",
                        argNode.line);
                const index = argNode.constIndex;
                if (index === null || index === undefined)
                    throw new GLSLError("a sampler array needs a constant index",
                        argNode.line);
                const name = safeName(base.symbol.name);
                const binding = this.iface.samplers.find(s => s.name === base.symbol.name)?.binding || 0;
                return { texture: "t_" + name + "_" + index, sampler: "s_" + name + "_" + index,
                    index: (binding / 2 + index) + "u", dim: argNode.type.dim, shadow: argNode.type.shadow };
            }
            if (argNode.kind !== "Ident")
                throw new GLSLError("unsupported sampler expression", argNode.line);
            const name = safeName(argNode.symbol.name);
            const binding = this.iface.samplers.find(s => s.name === argNode.symbol.name)?.binding || 0;
            return { texture: "t_" + name, sampler: "s_" + name,
                index: argNode.symbol.kind === "param" ? "gl_sampler_index_" + name : binding / 2 + "u",
                dim: argNode.type.dim, shadow: argNode.type.shadow };
        }

        desktopSample(pair, coord, level, bias, ref, explicit) {
            if (this.stage === "vertex" || explicit) ++this.stats.levelSamples;
            else if (this.inUniformControlFlow) ++this.stats.uniformSamples;
            else ++this.stats.nonUniformSamples;
            const dim = pair.dim === "Cube" ? "Cube" : pair.dim === "3D" ? "3D" : "2D",
                name = "gl_sample_" + dim + (pair.shadow ? "_shadow" : "");
            const sample = name + "(" + pair.texture + ", " + pair.sampler + ", " + coord + ", " + pair.index + ", " + level + ", " + bias + ", " + explicit + (ref ? ", " + ref : "") + ")";
            return pair.shadow ? name + "_color(" + sample + ", " + pair.index + ")" : sample;
        }

        /*
         * Which WGSL sampling function to use is not a style choice.
         *
         *  - the vertex stage has no implicit derivatives at all, so every
         *    lookup there is an explicit-level one
         *  - in non-uniform control flow textureSample's derivatives are
         *    undefined, as in GL: with derivative_uniformity off (generate())
         *    the lookup is the same as in uniform flow ("implicit", the
         *    default); "grad" passes the coordinate's own derivatives (plan
         *    section 4.10, strategy 2), "level0" samples level 0
         *
         * Both cases are counted, because an approximation nobody measures is
         * an approximation nobody can trust.
         */
        emitSample(pair, coord, bias, node) {
            if (this.options.desktopTextureState) return this.desktopSample(pair, coord, "0.0", bias || "0.0", null, false);
            if (this.stage === "vertex") {
                ++this.stats.levelSamples;
                return "textureSampleLevel(" + pair.texture + ", " + pair.sampler +
                    ", " + coord + ", 0.0)";
            }
            const uniform = this.inUniformControlFlow;
            if (uniform) ++this.stats.uniformSamples;
            else ++this.stats.nonUniformSamples;
            const strategy = this.options.nonUniformSampleStrategy || "implicit";
            if (uniform || strategy === "implicit") {
                return bias ?
                    "textureSampleBias(" + pair.texture + ", " + pair.sampler +
                        ", " + coord + ", " + bias + ")" :
                    "textureSample(" + pair.texture + ", " + pair.sampler +
                        ", " + coord + ")";
            }
            if (strategy === "level0") {
                ++this.stats.levelSamples;
                return "textureSampleLevel(" + pair.texture + ", " + pair.sampler +
                    ", " + coord + ", 0.0)";
            }
            ++this.stats.gradSamples;
            const c = "(" + coord + ")";
            return "textureSampleGrad(" + pair.texture + ", " + pair.sampler +
                ", " + c + ", dpdx" + c + ", dpdy" + c + ")";
        }

        emitCompareSample(pair, coord, ref, node) {
            const uniform = this.stage === "fragment" && this.inUniformControlFlow;
            void node;
            if (uniform) ++this.stats.uniformSamples;
            else ++this.stats.nonUniformSamples;
            // (implicit derivatives exist in the fragment stage only; see emitSample)
            if (this.stage === "fragment" &&
                    (uniform || (this.options.nonUniformSampleStrategy || "implicit") !== "level0")) {
                return "textureSampleCompare(" + pair.texture + ", " +
                    pair.sampler + ", " + coord + ", " + ref + ")";
            }
            return "textureSampleCompareLevel(" + pair.texture + ", " +
                pair.sampler + ", " + coord + ", " + ref + ")";
        }

        /* ---- entry points ---- */

        emitEntryPoint() {
            if (this.stage === "vertex" && this.options.vertexCapture) this.emitVertexCaptureEntry();
            else if (this.stage === "vertex") this.emitVertexEntry();
            else this.emitFragmentEntry();
        }

        emitVertexCaptureEntry() {
            this.emit("@compute @workgroup_size(" + VERTEX_CAPTURE.workgroupSize + ")");
            this.emit("fn capture_main(@builtin(global_invocation_id) id : vec3<u32>) {");
            ++this.indent;
            this.emit("if (id.x >= arrayLength(&glCaptureOutput)) { return; }");
            for (const [name, location] of this.iface.attributes) {
                const type = this.usage.attributes.get(name).type;
                const column = (slot, count) => "glCaptureInput[id.x].attributes[" + slot + "]." + SWIZZLE_CHARS.slice(0, count);
                const expr = type.kind === "matrix" ? typeToWGSL(type) + "(" +
                    Array.from({ length: type.cols }, (_, i) => column(location + i, type.rows)).join(", ") + ")" :
                    column(location, componentCount(type));
                this.emit(safeName(name) + " = " + (baseOf(type) === "float" ? expr : typeToWGSL(type) + "(" + expr + ")") + ";");
            }
            this.emitGlobalInits();
            this.emit("glsl_main();");
            const output = (field, expression) => this.emit("glCaptureOutput[id.x]." + field + " = " + expression + ";");
            output("clip", "gl_Position");
            for (const [field, name] of [["color", "gl_FrontColor"], ["backColor", "gl_BackColor"]])
                output(field, this.usage.varyings.has(name) ? "saturate(" + name + ")" : "vec4<f32>(1.0)");
            output("tex", this.usage.varyings.has("gl_TexCoord") ? "gl_TexCoord[0]" : "vec4<f32>(0.0)");
            output("eye", this.usage.builtinOutputs.has("gl_ClipVertex") ? "gl_ClipVertex" : "vec4<f32>(0.0)");
            if (this.options.vertexCaptureRaster) {
                output("secondary", this.usage.varyings.has("gl_FrontSecondaryColor") ? "saturate(gl_FrontSecondaryColor)" : "vec4<f32>(0.0)");
                output("fog", this.usage.varyings.has("gl_FogFragCoord") ? "vec4<f32>(gl_FogFragCoord, 0.0, 0.0, 0.0)" : "vec4<f32>(0.0)");
                for (let i = 1; i < 8; ++i) output("texExtra[" + (i - 1) + "]", this.usage.varyings.has("gl_TexCoord") ? "gl_TexCoord[" + i + "]" : "vec4<f32>(0.0)");
            }
            --this.indent;
            this.emit("}");
        }

        emitGlobalInits() {
            for (const d of this.deferredGlobalInits || []) {
                const pre = [];
                const text = this.genExpr(d.init, pre, d.type);
                this.emitAll(pre);
                this.emit(safeName(d.name) + " = " + text + ";");
            }
        }

        emitVertexEntry() {
            if (this.options.pointSprite) this.emit(stateLayout.pointRasterWGSL(this.options.desktopTextureState, this.options.depthClamp, this.options.pointRasterRounded));
            this.emit("@vertex");
            this.emit("fn vs_main(vsin : VSIn) -> VSOut {");
            ++this.indent;
            for (const [name, loc] of this.iface.attributes) {
                const info = this.usage.attributes.get(name);
                if (!info) continue;
                void loc;
                if (info.type.kind === "matrix") {
                    const cols = [];
                    for (let c = 0; c < info.type.cols; ++c)
                        cols.push("vsin." + attribFieldName(name) + "_" + c);
                    this.emit(safeName(name) + " = " + typeToWGSL(info.type) +
                        "(" + cols.join(", ") + ");");
                } else {
                    this.emit(safeName(name) + " = vsin." +
                        attribFieldName(name) + ";");
                }
            }
            this.emitGlobalInits();
            this.emit("glsl_main();");
            this.emit("var out : VSOut;");
            this.emit("var clip = gl_Position;");

            if (this.options.pointSprite) {
                // Points are drawn as two triangles (plan 10.3), so the corner
                // offset is applied in clip space -- before the Y flip, so the
                // sign convention is still GL's.
                const size = this.options.programPointSize &&
                    this.usage.builtinOutputs.has("gl_PointSize") ?
                    "gl_PointSize" : "glState.pointParams.x";
                this.emit("let glPtSize = clamp(" + size +
                    (this.options.desktopTextureState ? ", 1.0, 64.0);" : ", glState.pointParams.y, glState.pointParams.z);"));
                this.emit("let glCorner=gl_point_corner(vsin.glVertexIndex);");
                this.emit("clip=gl_point_position(clip,glPtSize,glCorner,glState.viewportParams);");
                // GL_POINT_SPRITE_COORD_ORIGIN defaults to GL_UPPER_LEFT.
                this.emit("out.glPointCoord = vec2<f32>(" +
                    "glCorner.x * 0.5 + 0.5, " +
                    (this.options.pointCoordLowerLeft ?
                        "glCorner.y * 0.5 + 0.5);" :
                        "0.5 - glCorner.y * 0.5);"));
            }

            // GL clip space is z in [-w, w]; WebGPU wants [0, w]. The Y flip
            // makes framebuffer row 0 GL's bottom row, which is what lets
            // viewport, scissor, readback and render-to-texture all skip a
            // conversion (plan 4.3).
            this.emit("clip.z = (clip.z + clip.w) * 0.5;");
            this.emit("clip.y = -clip.y;");
            this.emit("out.position = clip;");

            for (const entry of this.iface.varyings.entries) {
                const source = this.vertexVaryingSource(entry);
                if (!source) continue;
                for (let i = 0; i < entry.components; ++i) {
                    const dst = "out.v" + entry.slot + "." +
                        SWIZZLE_CHARS[entry.offset + i];
                    const src = entry.components === 1 && !source.vector ?
                        source.expr :
                        source.expr + "." + SWIZZLE_CHARS[i];
                    this.emit(dst + " = " + src + ";");
                }
            }

            if (this.iface.clipPlaneCount) {
                const cv = this.usage.builtinOutputs.has("gl_ClipVertex") ? "gl_ClipVertex" : null,
                    mask = this.iface.clipPlaneMask, distance = i => cv && (mask & 1 << i) ? "dot(" + cv + ", glState.clipPlanes[" + i + "])" : "1.0";
                this.emit("out.glClipDist = vec4<f32>(" + Array.from({ length: 4 }, (_, i) => distance(i)).join(", ") + ");");
                if (this.iface.clipPlaneCount > 4) this.emit("out.glClipExtra=vec2<f32>(" + [4, 5].map(distance).join(",") + ");");
            }

            this.emit("return out;");
            --this.indent;
            this.emit("}");
        }

        /* Where a varying's value comes from inside the vertex shader. */
        vertexVaryingSource(entry) {
            if (entry.compat) {
                if (entry.name === "gl_TexCoord")
                    return { expr: "gl_TexCoord[" + entry.index + "]", vector: true };
                const source = entry.vsFront || entry.vsBack;
                if (!source) return null;
                if (!this.usage.varyings.has(source)) return null;
                // the colours a vertex shader writes are clamped to [0, 1]
                // before they are interpolated (GL 2.1 section 2.14.6; review
                // S2-17)
                return { expr: COLOR_OUTPUTS.has(source) ? "saturate(" + safeName(source) + ")" : safeName(source),
                         vector: componentCount(entry.type) > 1 };
            }
            if (!this.usage.varyings.has(entry.name)) return null;
            if (entry.index !== undefined)
                return { expr: safeName(entry.name) + "[" + entry.index + "]",
                         vector: entry.components > 1 };
            if (entry.column !== undefined)
                return { expr: safeName(entry.name) + "[" + entry.column + "]",
                         vector: entry.components > 1 };
            return { expr: safeName(entry.name), vector: entry.components > 1 };
        }

        emitFragmentEntry() {
            this.emit("@fragment");
            this.emit("fn fs_main(fsin : FSIn) -> FSOut {");
            ++this.indent;

            if (this.iface.clipPlaneCount) {
                // The vertex stage writes 1.0 into the unused lanes, so one
                // test covers however many planes are on. Deviation D-05: this
                // discards fragments rather than clipping geometry.
                this.emit("if (any(fsin.glClipDist < vec4<f32>(0.0))) { discard; }");
                if (this.iface.clipPlaneCount > 4) this.emit("if(any(fsin.glClipExtra<vec2<f32>(0.0))){discard;}");
            }
            if (this.options.polygonStipple) {
                this.emit("let stippleX = u32(floor(fsin.position.x)) & 31u;");
                this.emit("let stippleY = u32(floor(fsin.position.y)) & 31u;");
                this.emit("let stippleByteIndex = stippleY * 4u + (stippleX >> 3u);");
                this.emit("let stippleWord = glState.polygonStipple[stippleByteIndex >> 2u];");
                this.emit("let stippleLane = stippleByteIndex & 3u;");
                this.emit("var stippleByte = stippleWord.x;");
                this.emit("if (stippleLane == 1u) { stippleByte = stippleWord.y; }");
                this.emit("if (stippleLane == 2u) { stippleByte = stippleWord.z; }");
                this.emit("if (stippleLane == 3u) { stippleByte = stippleWord.w; }");
                this.emit("let stippleBit = 0x80u >> (stippleX & 7u);");
                this.emit("if ((u32(stippleByte) & stippleBit) == 0u) { discard; }");
            }

            if (this.usage.builtinInputs.has("gl_FragCoord"))
                this.emit("gl_FragCoord = fsin.position;");
            if (this.usage.builtinInputs.has("gl_FrontFacing"))
                this.emit("gl_FrontFacing = " + (this.options.backFacing ? "false" : "fsin.frontFacing") + ";");
            if (this.usage.usesPointCoord)
                this.emit("gl_PointCoord = " + (this.options.pointSprite ?
                    "fsin.glPointCoord" : "vec2<f32>(0.5, 0.5)") + ";");

            for (const entry of this.iface.varyings.entries) {
                if (entry.isBackHalf) continue;
                const target = this.fragmentVaryingTarget(entry);
                if (!target) continue;
                const read = this.slotRead(entry);
                if (entry.compat && entry.vsBack && this.iface.twoSided) {
                    const back = this.iface.varyings.entries.find(e =>
                        e.isBackHalf && e.name === entry.name + "__back");
                    if (back) {
                        this.emit(target + " = select(" + this.slotRead(back) +
                            ", " + read + ", " + (this.options.backFacing ? "false" : "fsin.frontFacing") + ");");
                        continue;
                    }
                }
                this.emit(target + " = " + read + ";");
            }

            this.emitGlobalInits();
            this.emit("glsl_main();");

            if (this.options.alphaTest && this.options.alphaTest !== "always") {
                const colour = this.usage.usesFragColor ? "gl_FragColor" :
                    (this.usage.usesFragData ? "gl_FragData[0]" : null);
                if (this.options.alphaTest === "never") {
                    this.emit("discard;");
                } else if (colour) {
                    const op = ALPHA_TEST_OPERATORS[this.options.alphaTest];
                    this.emit("if (!(" + colour + ".a " + op +
                        " glState.alphaRef.x)) { discard; }");
                }
            }

            this.emit("var out : FSOut;");
            const targets = this.iface.colorTargets;
            for (let i = 0; i < targets; ++i) {
                let source;
                if (this.usage.usesFragData)
                    source = "gl_FragData[" + i + "]";
                else if (this.usage.usesFragColor)
                    source = "gl_FragColor";
                else
                    source = "vec4<f32>(0.0)";
                if (this.options.logicOp && this.options.logicOp !== 0x1503)
                    source = "glApplyLogic(" + source +
                        ", textureLoad(glLogicTarget" + i +
                        ", vec2<i32>(floor(fsin.position.xy)), 0))";
                this.emit("out.color" + i + " = " + source + ";");
            }
            if (this.usage.writesFragDepth)
                this.emit("out.depth = gl_FragDepth;");
            this.emit("return out;");
            --this.indent;
            this.emit("}");
        }

        fragmentVaryingTarget(entry) {
            if (entry.compat) {
                if (entry.name === "gl_TexCoord")
                    return "gl_TexCoord[" + entry.index + "]";
                if (!this.usage.varyings.has(entry.name)) return null;
                return safeName(entry.name);
            }
            if (!this.usage.varyings.has(entry.name)) return null;
            if (entry.index !== undefined)
                return safeName(entry.name) + "[" + entry.index + "]";
            if (entry.column !== undefined)
                return safeName(entry.name) + "[" + entry.column + "]";
            return safeName(entry.name);
        }

        slotRead(entry) {
            if (entry.offset === 0 && entry.components === 4)
                return "fsin.v" + entry.slot;
            return "fsin.v" + entry.slot + "." +
                slotSwizzle(entry.offset, entry.components);
        }
    }

    const ALPHA_TEST_OPERATORS = {
        less: "<", equal: "==", lequal: "<=",
        greater: ">", notequal: "!=", gequal: ">=",
    };

    function logicOpExpression(op) {
        switch (op >>> 0) {
        case 0x1500: return "vec4<u32>(0u)";
        case 0x1501: return "s & d";
        case 0x1502: return "s & ~d";
        case 0x1503: return "s";
        case 0x1504: return "~s & d";
        case 0x1505: return "d";
        case 0x1506: return "s ^ d";
        case 0x1507: return "s | d";
        case 0x1508: return "~(s | d)";
        case 0x1509: return "~(s ^ d)";
        case 0x150A: return "~d";
        case 0x150B: return "s | ~d";
        case 0x150C: return "~s";
        case 0x150D: return "~s | d";
        case 0x150E: return "~(s & d)";
        case 0x150F: return "vec4<u32>(255u)";
        default: return "s";
        }
    }

    /* ---- small code-generation helpers ---- */

    function wgslTextureType(type) {
        if (type.shadow) return "texture_depth_2d";
        switch (type.dim) {
        case "3D": return "texture_3d<f32>";
        case "Cube": return "texture_cube<f32>";
        default: return "texture_2d<f32>";   // 1D and 2DRect ride on 2D
        }
    }

    function attribFieldName(name) {
        return safeName(name);
    }

    function scalarize(arg) {
        if (arg.type.kind === "vector") return "(" + arg.text + ").x";
        return arg.text;
    }

    function convertScalar(arg, base) {
        const from = baseOf(arg.type);
        const text = scalarize(arg);
        if (from === base) return text;
        const ctor = base === "float" ? "f32" : (base === "int" ? "i32" : "bool");
        return ctor + "(" + text + ")";
    }

    /* Every component of a constructor argument, in GLSL's order, converted to
     * the constructed type's base. Matrices contribute column by column. */
    function componentsOf(arg, base) {
        const from = baseOf(arg.type);
        const ctor = base === "float" ? "f32" : (base === "int" ? "i32" : "bool");
        const wrap = text => from === base ? text : ctor + "(" + text + ")";
        if (arg.type.kind === "scalar") return [wrap(arg.text)];
        if (arg.type.kind === "vector") {
            const out = [];
            for (let i = 0; i < arg.type.size; ++i)
                out.push(wrap("(" + arg.text + ")." + SWIZZLE_CHARS[i]));
            return out;
        }
        if (arg.type.kind === "matrix") {
            const out = [];
            for (let c = 0; c < arg.type.cols; ++c)
                for (let r = 0; r < arg.type.rows; ++r)
                    out.push(wrap("(" + arg.text + ")[" + c + "][" + r + "]"));
            return out;
        }
        return [wrap(arg.text)];
    }

    function isConstantFoldable(expr) {
        switch (expr.kind) {
        case "IntLit": case "FloatLit": case "BoolLit": return true;
        case "Unary": return expr.op !== "++" && expr.op !== "--" &&
            isConstantFoldable(expr.operand);
        case "Binary":
            return expr.op !== "/" && expr.op !== "%" && isConstantFoldable(expr.left) && isConstantFoldable(expr.right);
        case "Call":
            return !!expr.constructType && expr.args.every(isConstantFoldable);
        case "Ident":
            return !!(expr.symbol && (expr.symbol.kind === "builtinConst" ||
                expr.symbol.kind === "const"));
        default: return false;
        }
    }

    /* WGSL function parameters are immutable; GLSL's are not. Only the ones a
     * function actually writes need a local copy. */
    function parameterIsAssigned(decl, name) {
        let found = false;
        const visitExpr = node => {
            if (!node || found || typeof node !== "object") return;
            if (node.kind === "Assign" || (node.kind === "Unary" &&
                    (node.op === "++" || node.op === "--"))) {
                let root = node.target || node.operand;
                while (root && (root.kind === "Field" || root.kind === "Index"))
                    root = root.object;
                if (root && root.kind === "Ident" && root.name === name)
                    found = true;
            }
            for (const key of Object.keys(node)) {
                if (key === "symbol" || key === "type" || key === "convertTo" ||
                        key === "resolved" || key === "constructType" ||
                        key === "structField") continue;
                const value = node[key];
                if (Array.isArray(value)) value.forEach(visitExpr);
                else if (value && typeof value === "object") visitExpr(value);
            }
        };
        visitExpr(decl.body);
        return found;
    }

    function hashString(text) {
        let h = 0x811c9dc5;
        for (let i = 0; i < text.length; ++i) {
            h ^= text.charCodeAt(i);
            h = Math.imul(h, 0x01000193) >>> 0;
        }
        return h >>> 0;
    }

    /* ================================================================== */
    /* Public API                                                         */
    /* ================================================================== */

    /*
     * glCompileShader: preprocess, parse and check one stage.
     *
     * Compilation success is decided here and nowhere else. WGSL is not
     * consulted: if this function produces an AST then the shader is valid
     * GLSL, and a later WGSL error is a bug in this translator rather than in
     * the application's shader (plan section 4.8). That distinction is what
     * keeps a mistake of ours from surfacing to the guest as its own
     * GL_INVALID_OPERATION.
     */
    function compileShader(source, stage, options) {
        const opts = options || {};
        const result = {
            ok: false, stage, log: "", warnings: [],
            sourceHash: hashString(stage + " " + source),
        };
        try {
            const pp = new Preprocessor(stage, opts);
            const pre = pp.run(source);
            result.preprocessedSource = pre.text;
            result.version = pre.version;
            result.extensions = pre.extensions;
            result.warnings = pre.warnings.slice();
            const tokens = lex(pre.text);
            const ast = new Parser(tokens, stage).parseProgram();
            const analyzer = new Analyzer(stage, ast, opts);
            const usage = analyzer.run();
            result.ast = ast;
            result.usage = usage;
            result.functions = analyzer.functions;
            result.ok = true;
            result.log = result.warnings.map(w => "WARNING: " + w).join("\n");
        } catch (error) {
            if (!(error instanceof GLSLError)) throw error;
            result.log = "ERROR: 0:" + (error.glslLine || 0) + ": " + error.message;
        }
        return result;
    }

    // Preprocess compilation units independently, then merge their shared
    // globals (GLSL 1.20 section 4.2). The CPU's glslang link has checked
    // matching initializer values; retain one declaration and initializer.
    function linkShaderUnits(units, stage, options) {
        const opts = options || {};
        if (units.length === 1) return units[0];
        const result = { ok: false, stage, log: "", warnings: units.flatMap(u => u.warnings || []),
            version: Math.max(110, ...units.map(u => u.version || 110)),
            unitSources: units.map(u => ({ stage, preprocessedSource: u.preprocessedSource, version: u.version, extensions: u.extensions,
                sourceHash: u.sourceHash, warnings: u.warnings, log: u.log })),
            sourceHash: hashString(stage + " " + JSON.stringify(units.map(u => u.preprocessedSource))) };
        try {
            if (!units.length) fail(0, "the " + stage + " stage has no shaders");
            const parsed = units.map(unit => {
                if (!unit.ok || unit.stage !== stage || typeof unit.preprocessedSource !== "string")
                    fail(0, "the " + stage + " shader unit is not compiled");
                const ast = new Parser(lex(unit.preprocessedSource), stage).parseProgram();
                new Analyzer(stage, ast, { ...opts, library: true }).run();
                return ast;
            });
            const walk = (root, visit) => {
                const seen = new Set();
                const next = node => {
                    if (!node || typeof node !== "object" || seen.has(node)) return;
                    seen.add(node); visit(node);
                    for (const [key, value] of Object.entries(node)) if (key !== "symbol" && key !== "overload") next(value);
                };
                next(root);
            };
            const allNames = new Set(), typeNames = new Set();
            for (const ast of parsed) walk(ast, node => {
                if (node.kind === "VarDecl") for (const d of node.declarators) allNames.add(d.name);
                if (node.kind === "struct") typeNames.add(node.name);
            });
            const usedNames = new Set([...allNames, ...typeNames]);
            const unique = (name, used) => { while (used.has(name)) name += "_"; used.add(name); return name; };
            const typeKey = type => type.kind === "struct" ? [type.name, type.fields.map(f => [f.name, typeKey(f.type)])] :
                type.kind === "array" ? [type.kind, type.size, typeKey(type.elem)] : type.name;
            const types = new Map(), layoutsByName = new Map();
            parsed.forEach((ast, unit) => {
                const localTypes = new Map();
                walk(ast, type => { if (type.kind === "struct") localTypes.set(type, JSON.stringify(typeKey(type))); });
                for (const [type, key] of localTypes) {
                    let name = types.get(key);
                    if (!name) {
                        name = layoutsByName.has(type.name) ? unique(type.name + "__vgl_unit" + unit, usedNames) : type.name;
                        layoutsByName.set(type.name, key); types.set(key, name);
                    }
                    type.name = name;
                }
            });
            const globals = new Map(), decls = [];
            for (const ast of parsed) for (const decl of ast.decls) {
                if (decl.kind === "VarDecl") {
                    decl.declarators = decl.declarators.filter(d => {
                        const previous = globals.get(d.name);
                        if (!previous) { globals.set(d.name, { d, decl }); return true; }
                        if (previous.decl.qualifiers.storage !== decl.qualifiers.storage || !sameType(previous.d.type, d.type))
                            fail(d.line, "global variable '" + d.name + "' has conflicting shader-unit declarations");
                        if (!previous.d.init && d.init) previous.d.init = d.init;
                        previous.decl.qualifiers.centroid ||= decl.qualifiers.centroid;
                        previous.decl.qualifiers.invariant ||= decl.qualifiers.invariant;
                        return false;
                    });
                    if (!decl.declarators.length) continue;
                }
                decls.push(decl);
            }
            const ast = { kind: "Program", decls }, analyzer = new Analyzer(stage, ast, { ...opts, library: false });
            result.usage = analyzer.run(); result.ast = ast; result.functions = analyzer.functions; result.ok = true;
        } catch (error) {
            if (!(error instanceof GLSLError)) throw error;
            result.log = "ERROR: 0:" + (error.glslLine || 0) + ": " + error.message;
        }
        return result;
    }

    const POINT_CORNER_LOCATION = 7;

    /*
     * glLinkProgram: decide the interface both stages share, then generate.
     *
     * `variant` carries the pipeline state that GL still applies around a
     * programmable pipeline -- alpha test, user clip planes, point sprites,
     * two-sided colour selection, flat shading. Each one changes the generated
     * WGSL, so a program links once per distinct variant and the executor keys
     * its shader cache on the pair.
     */
    function linkProgram(vs, fs, options) {
        const opts = options || {};
        const variant = opts.variant || {};
        const log = [];
        try {
            if (!vs || !vs.ok) throw new GLSLError("the vertex shader is not compiled", 0);
            if (!fs || !fs.ok) throw new GLSLError("the fragment shader is not compiled", 0);
            if (vs.stage !== "vertex" || fs.stage !== "fragment")
                throw new GLSLError("a program needs one vertex and one fragment shader", 0);
            for (const shader of [vs, fs]) if (!shader.functions?.get("main")?.some(f => f.def && !f.params.length))
                throw new GLSLError("the " + shader.stage + " stage has no main() function", 0);

            /* ---- uniforms: one namespace across both stages ---- */
            const uniformLayout = new UniformBlockLayout();
            const seenUniforms = new Map();
            const uniformOrder = [];
            for (const stage of [vs, fs]) {
                for (const entry of stage.usage.uniforms) {
                    const existing = seenUniforms.get(entry.name);
                    if (existing) {
                        if (!sameType(existing.type, entry.type))
                            throw new GLSLError("uniform '" + entry.name +
                                "' is declared with different types in the two " +
                                "shaders (" + existing.type.name + " and " +
                                entry.type.name + ")", 0);
                        continue;
                    }
                    seenUniforms.set(entry.name, entry);
                    uniformOrder.push(entry);
                }
            }
            for (const entry of uniformOrder) uniformLayout.add(entry.name, entry.type);

            /* ---- samplers: shared bindings, two per element ---- */
            const samplers = [];
            const samplerByName = new Map();
            let bindingCursor = 0;
            for (const stage of [vs, fs]) {
                for (const s of stage.usage.samplers) {
                    const existing = samplerByName.get(s.name);
                    if (existing) {
                        if (!sameType(existing.type, s.type))
                            throw new GLSLError("sampler '" + s.name +
                                "' is declared with different types in the two " +
                                "shaders", 0);
                        continue;
                    }
                    const entry = {
                        name: s.name,
                        wgslName: safeName(s.name),
                        type: s.type, arraySize: s.arraySize,
                        binding: bindingCursor,
                    };
                    bindingCursor += 2 * Math.max(1, s.arraySize);
                    samplers.push(entry);
                    samplerByName.set(s.name, entry);
                }
            }

            /* ---- GL state fields: the union, plus what the variant needs ---- */
            const stateFields = new Set([...vs.usage.stateFields,
                ...fs.usage.stateFields, ...(opts.forceStateFields || [])]);
            if (opts.desktopTextureState && (samplers.length || [vs, fs].some(stage => stage.ast.decls.some(decl =>
                (decl.params || []).some(param => param.type.kind === "sampler"))))) stateFields.add("samplerState");
            if (variant.alphaTest && variant.alphaTest !== "always" &&
                    variant.alphaTest !== "never")
                stateFields.add("alphaRef");
            if (variant.clipPlaneCount) stateFields.add("clipPlanes");
            if (variant.pointSprite) {
                stateFields.add("pointParams");
                stateFields.add("viewportParams");
            }
            if (variant.polygonStipple) stateFields.add("polygonStipple");
            const stateLayoutInfo = stateLayout.buildLayout([...stateFields]);

            /* ---- attributes ---- */
            const bindings = normalizeBindings(opts.bindAttribLocations);
            const attributes = assignAttributeLocations(vs.usage, bindings, !!opts.es);
            /* ---- varyings ---- */
            const varyings = assignVaryings(vs.usage, fs.usage, {
                twoSided: !!variant.twoSided,
                flatShading: !!variant.flatShading,
                keepCompatInputs: !!opts.keepCompatInputs,
            });
            let nextSlot = varyings.slotCount;
            const clipPlaneCount = variant.clipPlaneCount || 0;
            const clipVaryingSlot = clipPlaneCount ? nextSlot++ : -1, clipExtraSlot = clipPlaneCount > 4 ? nextSlot++ : -1,
                clipPlaneMask = variant.clipPlaneMask === undefined ? (1 << clipPlaneCount) - 1 : variant.clipPlaneMask;
            const usesPointCoord = fs.usage.usesPointCoord || !!variant.pointSprite;
            const pointCoordSlot = usesPointCoord ? nextSlot++ : -1;
            if (nextSlot > MAX_INTER_STAGE_SLOTS)
                throw new GLSLError("the program needs " + nextSlot +
                    " varying slots but only " + MAX_INTER_STAGE_SLOTS +
                    " are available (deviation D-12)", 0);

            if (clipPlaneCount && !vs.usage.builtinOutputs.has("gl_ClipVertex"))
                log.push("WARNING: user clip planes are enabled but the vertex " +
                    "shader does not write gl_ClipVertex; no clipping is applied");


            /* ---- colour targets ---- */
            if (fs.usage.usesFragColor && fs.usage.usesFragData)
                throw new GLSLError("a fragment shader may write gl_FragColor " +
                    "or gl_FragData, not both", 0);
            // gl_FragColor goes to every draw buffer (GL 2.1 section 3.11.2)
            let colorTargets = Math.max(1, variant.colorTargets || 1);
            if (fs.usage.usesFragData) {
                colorTargets = fs.usage.fragDataDynamic ? MAX_DRAW_BUFFERS :
                    Math.max(1, fs.usage.fragDataMax + 1);
            }
            if (opts.maxColorTargets)
                colorTargets = Math.min(colorTargets, opts.maxColorTargets);

            const iface = {
                attributes, varyings, uniformLayout,
                stateLayout: stateLayoutInfo, samplers,
                clipPlaneCount, clipPlaneMask, clipVaryingSlot, clipExtraSlot, pointCoordSlot,
                colorTargets, twoSided: !!variant.twoSided,
            };
            const genOptions = {
                vertexCapture: !!opts.vertexCapture,
                desktopTextureState: !!opts.desktopTextureState,
                vertexCaptureRaster: !!opts.vertexCaptureRaster,
                pointSprite: !!variant.pointSprite,
                programPointSize: !!variant.programPointSize,
                backFacing: !!variant.backFacing,
                pointCoordLowerLeft: !!variant.pointCoordLowerLeft,
                polygonStipple: !!variant.polygonStipple,
                logicOp: variant.logicOp ? (variant.logicOp >>> 0) : 0,
                depthClamp: !!variant.depthClamp,
                pointRasterRounded: variant.pointRasterRounded !== false,
                alphaTest: variant.alphaTest || null,
                nonUniformSampleStrategy: opts.nonUniformSampleStrategy || "implicit",
            };

            const vsGen = new Generator("vertex", vs.ast, vs.usage, iface, genOptions);
            const wgslVertex = vsGen.generate();
            const fsGen = new Generator("fragment", fs.ast, fs.usage, iface, genOptions);
            const wgslFragment = fsGen.generate();

            const reflection = buildReflection(vs, fs, iface, {
                clipPlaneCount, colorTargets, usesPointCoord, variant,
            });

            return {
                ok: true,
                log: log.join("\n"),
                wgslVertex, wgslFragment, reflection,
                varyingLayout: { entries: varyings.entries.map(({ name, index, components, slot, offset, flat }) =>
                    ({ name, index, components, slot, offset, flat })),
                clip: clipVaryingSlot, clipExtra: clipExtraSlot, pointCoord: pointCoordSlot },
                stats: {
                    vertex: vsGen.stats, fragment: fsGen.stats,
                    nonUniformSamples: vsGen.stats.nonUniformSamples +
                        fsGen.stats.nonUniformSamples,
                    noiseCalls: vs.usage.noiseCalls + fs.usage.noiseCalls,
                },
                cacheKey: linkCacheKey(vs, fs, opts),
            };
        } catch (error) {
            if (!(error instanceof GLSLError)) throw error;
            log.push("ERROR: " + error.message);
            return { ok: false, log: log.join("\n") };
        }
    }

    function normalizeBindings(bindings) {
        if (!bindings) return new Map();
        if (bindings instanceof Map) return bindings;
        return new Map(Object.entries(bindings));
    }

    function linkCacheKey(vs, fs, opts) {
        const variant = opts.variant || {};
        const bindings = normalizeBindings(opts.bindAttribLocations);
        const bindingText = [...bindings.entries()]
            .sort((a, b) => (a[0] < b[0] ? -1 : 1))
            .map(entry => entry[0] + "=" + entry[1]).join(",");
        return [
            "r" + TRANSLATOR_REVISION,
            vs.sourceHash.toString(16), fs.sourceHash.toString(16),
            bindingText,
            "s" + (opts.forceStateFields || []).slice().sort().join(","),
            opts.keepCompatInputs ? "compat-inputs" : "-",
            opts.vertexCapture ? "capture" : "render",
            opts.desktopTextureState ? "desktop-sampling" : "-",
            opts.vertexCaptureRaster ? "raster" : "vertices",
            "a" + (variant.alphaTest || "-"),
            "c" + (variant.clipPlaneCount || 0), "cm" + (variant.clipPlaneMask ?? (1 << (variant.clipPlaneCount || 0)) - 1),
            variant.pointSprite ? "p" : "-",
            variant.programPointSize ? "z" : "-",
            variant.pointCoordLowerLeft ? "l" : "u",
            variant.polygonStipple ? "s" : "-",
            "o" + (variant.logicOp || 0).toString(16),
            variant.twoSided ? "2" : "-",
            variant.flatShading ? "f" : "-",
            variant.backFacing ? "back" : "-",
            !!variant.depthClamp, variant.pointRasterRounded !== false,
            "t" + (variant.colorTargets || 1),
            opts.nonUniformSampleStrategy || "implicit",
        ].join("|");
    }

    /*
     * The reflection is the contract with gl_executor.js: it says where every
     * glUniform* write lands, which texture unit each sampler reads, and which
     * vertex attribute each location expects. Nothing else in the executor is
     * allowed to guess at the generated WGSL's layout.
     */
    function buildReflection(vs, fs, iface, extra) {
        const attributes = [];
        for (const [name, location] of iface.attributes) {
            const info = vs.usage.attributes.get(name);
            attributes.push({
                name, location, glslType: info.type.name,
                components: componentCount(info.type),
                slots: attributeSlotCount(info.type),
                baseType: baseOf(info.type),
                isCompat: !!info.isCompat,
            });
        }
        attributes.sort((a, b) => a.location - b.location);

        const uniforms = [];
        let locationCursor = 0;
        for (const entry of iface.uniformLayout.leaves) {
            const path = entry[0];
            const leaf = entry[1];
            const count = leaf.arraySize || 1;
            uniforms.push({
                name: path,
                location: locationCursor,
                glslType: leaf.type.name,
                baseType: baseOf(leaf.type),
                kind: leaf.type.kind,
                components: componentCount(leaf.type),
                arraySize: leaf.arraySize,
                offsetBytes: leaf.offsetBytes,
                arrayStrideBytes: leaf.arrayStrideBytes,
                matrixColumnStrideBytes: leaf.matrixColumnStrideBytes,
                columns: leaf.type.kind === "matrix" ? leaf.type.cols : 0,
                rows: leaf.type.kind === "matrix" ? leaf.type.rows : 0,
            });
            locationCursor += count;
        }

        const samplers = [];
        for (const s of iface.samplers) {
            samplers.push({
                name: s.name,
                location: locationCursor,
                binding: s.binding,
                dim: s.type.dim,
                shadow: s.type.shadow,
                external: !!s.type.external,
                arraySize: s.arraySize,
                bindingCount: 2 * Math.max(1, s.arraySize),
            });
            locationCursor += Math.max(1, s.arraySize);
        }

        return {
            attributes, uniforms, samplers,
            uniformBlockBytes: iface.uniformLayout.sizeBytes,
            stateFields: iface.stateLayout.fields.slice(),
            stateFloats: iface.stateLayout.floats,
            varyingSlots: iface.varyings.slotCount,
            clipPlaneCount: extra.clipPlaneCount,
            clipVaryingSlot: iface.clipVaryingSlot,
            pointCoordSlot: iface.pointCoordSlot,
            colorTargets: extra.colorTargets,
            usesDiscard: fs.usage.usesDiscard,
            usesPointCoord: extra.usesPointCoord,
            writesFragDepth: fs.usage.writesFragDepth,
            writesPointSize: vs.usage.writesPointSize,
            writesClipVertex: vs.usage.builtinOutputs.has("gl_ClipVertex"),
            usesFragData: fs.usage.usesFragData,
            bindGroupLayout: {
                state: { group: 1, binding: 0 },
                uniforms: { group: 1, binding: 1 },
                textures: { group: 2 },
            },
        };
    }

    const api = {
        TRANSLATOR_REVISION,
        VERTEX_CAPTURE, vertexCaptureBindings,
        linkVertexCapture(vs, options) {
            const fs = compileShader("void main() { gl_FragColor = vec4(1.0); }", "fragment");
            const result = linkProgram(vs, fs, { ...options, variant: {}, vertexCapture: true });
            if (result.ok) result.wgslCompute = result.wgslVertex;
            return result;
        },
        compileShader, linkShaderUnits, linkProgram,
        // Exported for the test suite and for gl_fixed_function.js, which
        // shares the state block and the attribute slot table.
        Preprocessor, lex, Parser, Analyzer, Generator,
        UniformBlockLayout, GLSLError,
        BASIC_TYPES, T, vec, mat, arrayOf, structType, typeToWGSL, sameType,
        COMPAT_ATTRIBUTE_LOCATIONS, COMPAT_ATTRIBUTE_TYPES,
        STATE_UNIFORMS, BUILTIN_CONSTANTS, ES_BUILTIN_CONSTANTS,
        MAX_INTER_STAGE_SLOTS, MAX_VERTEX_ATTRIBUTES, POINT_CORNER_LOCATION,
        assignAttributeLocations, assignVaryings, hashString, safeName, logicOpExpression,
    };
    global.GLShaderTranslator = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// GL_ARB_vertex_program and GL_ARB_fragment_program assembly -> WGSL.
//
// ARB assembly is a register machine: a handful of typed register files, four
// component swizzles, write masks, source modifiers, and a flat instruction
// list with no control flow. That is structurally what D3D9's shader model 1.x
// is, and d3d9_shader_pipeline.js already solved the interesting half of it --
// how to keep swizzles, masks and saturation straight while emitting readable
// WGSL. What does not transfer is the front end (text, not bytecode) and the
// instruction semantics, so those are written out here.
//
// The state bindings (state.matrix.mvp, state.light[n].position, ...) resolve
// into the *same* GLState block the fixed pipeline and the GLSL compatibility
// built-ins use, so an ARB program and a fixed-function draw see one set of
// matrices rather than two that can drift.
//
// See docs/opengl-webgpu-plan.md E2.

(function(global) {
    "use strict";

    const nodeRequire = (typeof require === "function" &&
        typeof module !== "undefined") ? require : null;
    const stateLayout = nodeRequire ? nodeRequire("./gl_state_layout.js") :
        global.GLStateLayout;
    const translator = nodeRequire ? nodeRequire("./gl_shader_translator.js") :
        global.GLShaderTranslator;

    const ARB_REVISION = 6;

    /*
     * Vertex programs require at least 96 parameters. Fragment programs
     * retain 28 for WineD3D's program.env[27] (openglproxy/README.md).
     * Storage has one shape for both stages; validation and advertised
     * limits use the target's logical range.
     */
    const MAX_PROGRAM_PARAMETERS = 96;
    const MAX_FRAGMENT_PROGRAM_PARAMETERS = 28;
    const MAX_PROGRAM_BINDINGS = 4096;
    const MAX_ADDRESS_REGISTERS = 32;
    const parameterLimit = target => target === "vertex" ? MAX_PROGRAM_PARAMETERS : MAX_FRAGMENT_PROGRAM_PARAMETERS;
    /* Group 1 binding 0 is the shared GL state block; each stage's ARB
     * parameter block gets a binding of its own. */
    const PARAMETER_BINDING = { vertex: 1, fragment: 2 };
    const MAX_TEMPORARIES = 32;
    const MAX_TEXTURE_UNITS = stateLayout.MAX_TEXTURE_UNITS;
    const ATTR = translator.COMPAT_ATTRIBUTE_LOCATIONS;

    class ARBError extends Error {
        constructor(message, line, offset) {
            super(message);
            this.arbLine = line || 0;
            // GL_PROGRAM_ERROR_POSITION_ARB is a character offset into the
            // program string, not a line: the extension expects the caller to
            // be able to point at the exact token it rejected.
            this.arbOffset = offset === undefined ? -1 : offset;
        }
    }

    /* ================================================================== */
    /* Lexer                                                              */
    /* ================================================================== */

    const TOKEN = /(!!ARB[a-z]+1\.0)|([A-Za-z_$][A-Za-z0-9_$]*)|(-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)|(\s+)|(#[^\n]*)|([\s\S])/g;

    function lex(source) {
        const tokens = [];
        let line = 1;
        let match;
        TOKEN.lastIndex = 0;
        while ((match = TOKEN.exec(source)) !== null) {
            if (match[4] !== undefined || match[5] !== undefined) {
                for (const ch of match[0]) if (ch === "\n") ++line;
                continue;
            }
            const at = match.index;
            if (match[1] !== undefined)
                tokens.push({ type: "header", value: match[1], line, at });
            else if (match[2] !== undefined)
                tokens.push({ type: "ident", value: match[2], line, at });
            else if (match[3] !== undefined)
                tokens.push({ type: "number", value: parseFloat(match[3]),
                             line, at });
            else
                tokens.push({ type: "punct", value: match[6], line, at });
        }
        tokens.push({ type: "eof", value: "", line, at: source.length });
        return tokens;
    }

    /* ================================================================== */
    /* Instruction table                                                  */
    /* ================================================================== */

    /*
     * Each entry: how many sources it takes and how to emit it. `emit` receives
     * the already-swizzled source expressions and returns a vec4 expression;
     * the caller applies the write mask and saturation.
     */
    const INSTRUCTIONS = {
        ABS: { sources: 1, emit: a => "abs(" + a[0] + ")" },
        ADD: { sources: 2, emit: a => "(" + a[0] + " + " + a[1] + ")" },
        CMP: { sources: 3, fragmentOnly: true,
               emit: a => "select(" + a[2] + ", " + a[1] + ", " + a[0] +
                   " < vec4<f32>(0.0))" },
        COS: { sources: 1, scalar: true,
               emit: a => "vec4<f32>(cos(" + a[0] + ".x))" },
        DP3: { sources: 2,
               emit: a => "vec4<f32>(dot(" + a[0] + ".xyz, " + a[1] + ".xyz))" },
        DP4: { sources: 2,
               emit: a => "vec4<f32>(dot(" + a[0] + ", " + a[1] + "))" },
        DPH: { sources: 2,
               emit: a => "vec4<f32>(dot(" + a[0] + ".xyz, " + a[1] + ".xyz) + " +
                   a[1] + ".w)" },
        DST: { sources: 2,
               // The distance-vector helper: (1, d, d^2, 1/d) assembled from
               // two operands that each hold half of it.
               emit: a => "vec4<f32>(1.0, " + a[0] + ".y * " + a[1] + ".y, " +
                   a[0] + ".z, " + a[1] + ".w)" },
        EX2: { sources: 1, scalar: true,
               emit: a => "vec4<f32>(exp2(" + a[0] + ".x))" },
        EXP: { sources: 1, scalar: true,
               emit: a => "vec4<f32>(exp2(floor(" + a[0] + ".x)), fract(" +
                   a[0] + ".x), exp2(" + a[0] + ".x), 1.0)" },
        FLR: { sources: 1, emit: a => "floor(" + a[0] + ")" },
        FRC: { sources: 1, emit: a => "fract(" + a[0] + ")" },
        LG2: { sources: 1, scalar: true,
               emit: a => "vec4<f32>(log2(max(" + a[0] + ".x, 1e-30)))" },
        LIT: { sources: 1, emit: a => "arbLit(" + a[0] + ")" },
        LOG: { sources: 1, scalar: true,
               emit: a => "arbLog(" + a[0] + ".x)" },
        LRP: { sources: 3, fragmentOnly: true,
               emit: a => "mix(" + a[2] + ", " + a[1] + ", " + a[0] + ")" },
        MAD: { sources: 3,
               emit: a => "(" + a[0] + " * " + a[1] + " + " + a[2] + ")" },
        MAX: { sources: 2, emit: a => "max(" + a[0] + ", " + a[1] + ")" },
        MIN: { sources: 2, emit: a => "min(" + a[0] + ", " + a[1] + ")" },
        MOV: { sources: 1, emit: a => a[0] },
        MUL: { sources: 2, emit: a => "(" + a[0] + " * " + a[1] + ")" },
        POW: { sources: 2, scalar: true,
               emit: a => "vec4<f32>(pow(max(" + a[0] + ".x, 0.0), " +
                   a[1] + ".x))" },
        RCP: { sources: 1, scalar: true,
               emit: a => "vec4<f32>(1.0 / " + a[0] + ".x)" },
        RSQ: { sources: 1, scalar: true,
               emit: a => "vec4<f32>(inverseSqrt(max(abs(" + a[0] +
                   ".x), 1e-30)))" },
        SCS: { sources: 1, scalar: true, fragmentOnly: true,
               emit: a => "vec4<f32>(cos(" + a[0] + ".x), sin(" + a[0] +
                   ".x), 0.0, 0.0)" },
        SGE: { sources: 2,
               emit: a => "select(vec4<f32>(0.0), vec4<f32>(1.0), " +
                   a[0] + " >= " + a[1] + ")" },
        SIN: { sources: 1, scalar: true, fragmentOnly: true,
               emit: a => "vec4<f32>(sin(" + a[0] + ".x))" },
        SLT: { sources: 2,
               emit: a => "select(vec4<f32>(0.0), vec4<f32>(1.0), " +
                   a[0] + " < " + a[1] + ")" },
        SUB: { sources: 2, emit: a => "(" + a[0] + " - " + a[1] + ")" },
        XPD: { sources: 2,
               emit: a => "vec4<f32>(cross(" + a[0] + ".xyz, " + a[1] +
                   ".xyz), 1.0)" },
        SWZ: { sources: 1, extendedSwizzle: true, emit: a => a[0] },
        // Control and texture instructions are handled by the generator.
        ARL: { sources: 1, vertexOnly: true, address: true },
        KIL: { sources: 1, fragmentOnly: true, kill: true },
        TEX: { sources: 1, texture: "sample", fragmentOnly: true },
        TXB: { sources: 1, texture: "bias", fragmentOnly: true },
        TXP: { sources: 1, texture: "proj", fragmentOnly: true },
    };

    /* WGSL helpers the instruction set needs. Only emitted when used. */
    const HELPERS = {
        arbLit:
            "fn arbLit(v : vec4<f32>) -> vec4<f32> {\n" +
            "    // ARB's lighting helper: (1, max(x,0), x>0 ? pow(max(y,0), w) : 0, 1)\n" +
            "    let diffuse = max(v.x, 0.0);\n" +
            // (an exponent of 0 gives 1, as the extensions' power does; WGSL's
            // pow(0, 0) is NaN)
            "    let exponent = clamp(v.w, -128.0, 128.0);\n" +
            "    let power = select(pow(max(v.y, 0.0), exponent), 1.0, exponent == 0.0);\n" +
            "    let specular = select(0.0, power, v.x > 0.0);\n" +
            "    return vec4<f32>(1.0, diffuse, specular, 1.0);\n" +
            "}",
        arbLog:
            "fn arbLog(x : f32) -> vec4<f32> {\n" +
            "    let a = max(abs(x), 1e-30);\n" +
            "    let e = floor(log2(a));\n" +
            "    return vec4<f32>(e, a / exp2(e), log2(a), 1.0);\n" +
            "}",
    };

    /* ================================================================== */
    /* Register bindings                                                  */
    /* ================================================================== */

    /*
     * state.* bindings resolve into the shared GLState block. Each entry names
     * the field gl_state_layout.js must include and the WGSL expression that
     * reads it, so an ARB program and a fixed-function draw literally read the
     * same bytes.
     */
    const STATE_BINDINGS = {
        "matrix.modelview": { field: "modelview", matrix: "glState.modelview" },
        "matrix.projection": { field: "projection", matrix: "glState.projection" },
        "matrix.mvp": { field: "mvp", matrix: "glState.mvp" },
        "matrix.modelview.inverse": { field: "modelviewInverse",
            matrix: "glState.modelviewInverse" },
        "matrix.modelview.transpose": { field: "modelviewTranspose",
            matrix: "glState.modelviewTranspose" },
        "matrix.modelview.invtrans": { field: "modelviewInverseTranspose",
            matrix: "glState.modelviewInverseTranspose" },
        "matrix.projection.inverse": { field: "projectionInverse",
            matrix: "glState.projectionInverse" },
        "matrix.projection.transpose": { field: "projectionTranspose",
            matrix: "glState.projectionTranspose" },
        "matrix.projection.invtrans": { field: "projectionInverseTranspose",
            matrix: "glState.projectionInverseTranspose" },
        "matrix.mvp.inverse": { field: "mvpInverse", matrix: "glState.mvpInverse" },
        "matrix.mvp.transpose": { field: "mvpTranspose",
            matrix: "glState.mvpTranspose" },
        "matrix.mvp.invtrans": { field: "mvpInverseTranspose",
            matrix: "glState.mvpInverseTranspose" },
        "material.ambient": { field: "frontMaterial",
            vector: "glState.frontMaterial.ambient" },
        "material.diffuse": { field: "frontMaterial",
            vector: "glState.frontMaterial.diffuse" },
        "material.specular": { field: "frontMaterial",
            vector: "glState.frontMaterial.specular" },
        "material.emission": { field: "frontMaterial",
            vector: "glState.frontMaterial.emission" },
        "material.shininess": { field: "frontMaterial",
            vector: "vec4<f32>(glState.frontMaterial.shininess, 0.0, 0.0, 1.0)" },
        "material.front.ambient": { field: "frontMaterial",
            vector: "glState.frontMaterial.ambient" },
        "material.front.diffuse": { field: "frontMaterial",
            vector: "glState.frontMaterial.diffuse" },
        "material.front.specular": { field: "frontMaterial",
            vector: "glState.frontMaterial.specular" },
        "material.back.ambient": { field: "backMaterial",
            vector: "glState.backMaterial.ambient" },
        "material.back.diffuse": { field: "backMaterial",
            vector: "glState.backMaterial.diffuse" },
        "material.back.specular": { field: "backMaterial",
            vector: "glState.backMaterial.specular" },
        "material.back.emission": { field: "backMaterial",
            vector: "glState.backMaterial.emission" },
        "material.back.shininess": { field: "backMaterial",
            vector: "vec4<f32>(glState.backMaterial.shininess, 0.0, 0.0, 1.0)" },
        "lightmodel.ambient": { field: "lightModelAmbient",
            vector: "glState.lightModelAmbient" },
        "lightmodel.scenecolor": { field: "frontSceneColor",
            vector: "glState.frontSceneColor" },
        "lightmodel.front.scenecolor": { field: "frontSceneColor",
            vector: "glState.frontSceneColor" },
        "lightmodel.back.scenecolor": { field: "backSceneColor",
            vector: "glState.backSceneColor" },
        "fog.color": { field: "fogColor", vector: "glState.fogColor" },
        "fog.params": { field: "fogParams", vector: "glState.fogParams" },
        "depth.range": { field: "depthRange", vector: "glState.depthRange" },
        "point.size": { field: "pointParams", vector: "glState.pointParams" },
        "point.attenuation": { field: "pointAttenuation",
            vector: "glState.pointAttenuation" },
    };

    const INDEXED_STATE = [
        { pattern: /^light\[(\d+)\]\.(ambient|diffuse|specular|position|half)$/,
          field: "lights",
          expr: (index, member) => "glState.lights[" + index + "]." +
              (member === "half" ? "halfVector" : member) },
        { pattern: /^light\[(\d+)\]\.attenuation$/, field: "lights",
          expr: index => "vec4<f32>(glState.lights[" + index +
              "].constantAttenuation, glState.lights[" + index +
              "].linearAttenuation, glState.lights[" + index +
              "].quadraticAttenuation, glState.lights[" + index +
              "].spotExponent)" },
        { pattern: /^light\[(\d+)\]\.spot\.direction$/, field: "lights",
          expr: index => "vec4<f32>(glState.lights[" + index +
              "].spotDirection, glState.lights[" + index + "].spotCosCutoff)" },
        { pattern: /^lightprod\[(\d+)\]\.(ambient|diffuse|specular)$/,
          field: "frontLightProduct",
          expr: (index, member) => "glState.frontLightProduct[" + index + "]." +
              member },
        { pattern: /^lightprod\[(\d+)\]\.back\.(ambient|diffuse|specular)$/,
          field: "backLightProduct",
          expr: (index, member) => "glState.backLightProduct[" + index + "]." +
              member },
        { pattern: /^texenv\[(\d+)\]\.color$/, field: "texEnvColor",
          expr: index => "glState.texEnvColor[" + index + "]" },
        { pattern: /^texenv\.color$/, field: "texEnvColor",
          expr: () => "glState.texEnvColor[0]" },
        { pattern: /^clip\[(\d+)\]\.plane$/, field: "clipPlanes",
          expr: index => "glState.clipPlanes[" + index + "]" },
        { pattern: /^texgen\[(\d+)\]\.(eye|object)\.([stqr])$/,
          field: "texGenPlanes",
          expr: (index, space, coord) => "glState.texGenPlanes[" +
              (parseInt(index, 10) * 8 + (space === "eye" ? 4 : 0) +
               "stqr".indexOf(coord)) + "]" },
    ];

    /* Vertex program inputs, and the attribute slot each occupies. */
    const VERTEX_ATTRIBUTES = {
        "vertex.position": { name: "gl_Vertex", location: ATTR.gl_Vertex },
        "vertex.weight": { name: "gl_Weight", location: 1 },
        "vertex.normal": { name: "gl_Normal", location: ATTR.gl_Normal,
                           components: 3 },
        "vertex.color": { name: "gl_Color", location: ATTR.gl_Color },
        "vertex.color.primary": { name: "gl_Color", location: ATTR.gl_Color },
        "vertex.color.secondary": { name: "gl_SecondaryColor",
                                    location: ATTR.gl_SecondaryColor },
        "vertex.fogcoord": { name: "gl_FogCoord", location: ATTR.gl_FogCoord,
                             components: 1 },
    };

    const VERTEX_RESULTS = {
        "result.position": "position",
        "result.color": "frontColor",
        "result.color.primary": "frontColor",
        "result.color.front": "frontColor",
        "result.color.front.primary": "frontColor",
        "result.color.secondary": "frontSecondary",
        "result.color.front.secondary": "frontSecondary",
        "result.color.back": "backColor",
        "result.color.back.primary": "backColor",
        "result.color.back.secondary": "backSecondary",
        "result.fogcoord": "fogCoord",
        "result.pointsize": "pointSize",
    };

    const FRAGMENT_RESULTS = {
        "result.color": "color0",
        "result.depth": "depth",
    };

    /* ================================================================== */
    /* Parser                                                             */
    /* ================================================================== */

    class Parser {
        constructor(source) {
            this.text = source;
            this.tokens = lex(source);
            this.pos = 0;
            this.target = null;
            this.options = new Set();
            this.aliases = new Map();     // name -> resolved binding text
            this.temporaries = new Set();
            this.addresses = new Set();
            this.params = new Map();      // name -> {size, values|binding}
            this.attribs = new Map();
            this.outputs = new Map();
            this.instructions = [];
            this.declarations = new Set();
        }

        peek(offset) { return this.tokens[this.pos + (offset || 0)]; }
        /* Where the parser stopped, for GL_PROGRAM_ERROR_POSITION_ARB. Errors
         * thrown deeper in the parse do not each have to carry an offset: the
         * token the parser was looking at when it gave up is the position the
         * extension asks for. */
        currentOffset() {
            const token = this.tokens[Math.min(this.pos, this.tokens.length - 1)];
            return token && token.at !== undefined ? token.at : 0;
        }
        next() { return this.tokens[this.pos++]; }
        get line() { return this.peek().line; }
        at(value) {
            const token = this.peek();
            return token.value === value;
        }
        accept(value) {
            if (this.at(value)) { ++this.pos; return true; }
            return false;
        }
        expect(value) {
            if (!this.accept(value))
                throw new ARBError("expected '" + value + "' but found '" +
                    String(this.peek().value) + "'", this.line);
        }
        expectIdent() {
            const token = this.next();
            if (token.type !== "ident")
                throw new ARBError("expected an identifier", token.line);
            return token.value;
        }

        parse() {
            const header = this.next();
            if (header.type !== "header")
                throw new ARBError("a program must begin with !!ARBvp1.0 or " +
                    "!!ARBfp1.0", header.line, header.at);
            this.target = header.value === "!!ARBvp1.0" ? "vertex" : "fragment";
            for (;;) {
                const token = this.peek();
                if (token.type === "eof")
                    throw new ARBError("the program has no END", token.line);
                if (token.type === "ident" && token.value === "END") {
                    this.next();
                    break;
                }
                this.statement();
            }
            return this;
        }

        statement() {
            const token = this.peek();
            if (token.type !== "ident")
                throw new ARBError("unexpected token '" + String(token.value) + "'",
                    token.line);
            switch (token.value) {
            case "OPTION": {
                this.next();
                this.options.add(this.expectIdent());
                this.expect(";");
                return;
            }
            case "TEMP": {
                this.next();
                do { this.declareTemporary(this.expectIdent()); }
                while (this.accept(","));
                this.expect(";");
                return;
            }
            case "ADDRESS": {
                this.next();
                if (this.target !== "vertex") throw new ARBError("fragment programs have no address registers", this.line);
                do { const name = this.expectIdent(); this.declare(name); this.addresses.add(name); }
                while (this.accept(","));
                this.expect(";");
                return;
            }
            case "ALIAS": {
                this.next();
                const name = this.expectIdent();
                this.expect("=");
                const original = this.expectIdent();
                if (!this.declarations.has(original)) throw new ARBError("alias refers to an undeclared variable", this.line);
                this.declare(name); this.aliases.set(name, original);
                this.expect(";");
                return;
            }
            case "ATTRIB": {
                this.next();
                const name = this.expectIdent();
                this.declare(name);
                this.expect("=");
                const binding = this.bindingPath();
                if (!binding.startsWith(this.target === "vertex" ? "vertex." : "fragment."))
                    throw new ARBError("invalid attribute binding", this.line);
                this.attribs.set(name, binding);
                this.expect(";");
                return;
            }
            case "OUTPUT": {
                this.next();
                const name = this.expectIdent();
                this.declare(name);
                this.expect("=");
                const binding = this.bindingPath();
                if (!binding.startsWith("result.")) throw new ARBError("invalid output binding", this.line);
                this.outputs.set(name, binding);
                this.expect(";");
                return;
            }
            case "PARAM": {
                this.next();
                return this.paramStatement();
            }
            default:
                return this.instruction();
            }
        }

        declareTemporary(name) {
            this.declare(name);
            if (this.temporaries.size >= MAX_TEMPORARIES)
                throw new ARBError("more than " + MAX_TEMPORARIES +
                    " temporaries", this.line);
            this.temporaries.add(name);
        }

        declare(name) {
            if (this.declarations.has(name)) throw new ARBError("variable '" + name + "' declared twice", this.line);
            this.declarations.add(name);
        }

        /*
         * A dotted binding path such as vertex.color.secondary or
         * state.light[0].position, kept as text for the resolver.
         *
         * `stopAtSwizzle` is what separates `result.color.secondary` (a path)
         * from `result.color.xz` (a path plus a write mask). No ARB path
         * component is spelled with only swizzle letters, so the test is exact
         * rather than a heuristic.
         */
        bindingPath(stopAtSwizzle) {
            let text = this.expectIdent();
            for (;;) {
                if (this.at(".") && stopAtSwizzle) {
                    const after = this.peek(1);
                    if (after && after.type === "ident" &&
                            /^(?:[xyzw]{1,4}|[rgba]{1,4})$/.test(after.value))
                        break;
                }
                if (this.accept(".")) {
                    const token = this.next();
                    text += "." + String(token.value);
                    continue;
                }
                if (this.at("[")) {
                    // the text between the brackets as written: the lexer
                    // reads 0..4 as two numbers and a.x-1 as a and -1
                    // (review S2-48)
                    const open = this.next();
                    while (!this.at("]")) {
                        const token = this.next();
                        if (token.type === "eof")
                            throw new ARBError("unterminated '['", token.line);
                    }
                    const inner = this.text.slice(open.at + 1, this.peek().at).replace(/\s+/g, "");
                    this.expect("]");
                    text += "[" + inner + "]";
                    continue;
                }
                break;
            }
            return text;
        }

        /*
         * PARAM has two shapes that look alike and mean different things:
         *
         *   PARAM half = { 0.5, 0.5, 0.5, 1.0 };     one four-component constant
         *   PARAM mvp[4] = { state.matrix.mvp };     four registers, from a matrix
         *
         * The braces are the same; what decides is the [n] and whether the
         * contents are bare numbers. Reading the first as four separate
         * registers is the mistake that makes a transform silently use 0.5 as
         * its whole matrix.
         */
        paramStatement() {
            const name = this.expectIdent();
            this.declare(name);
            let arraySize = 0;
            if (this.accept("[")) {
                arraySize = this.at("]") ? -1 : this.next().value;
                this.expect("]");
                if (!Number.isInteger(arraySize) || arraySize !== -1 && arraySize < 1 || arraySize > MAX_PROGRAM_BINDINGS)
                    throw new ARBError("invalid parameter array size", this.line);
            }
            this.expect("=");
            const items = [];
            if (this.accept("{")) {
                do { items.push(this.paramItem()); } while (this.accept(","));
                this.expect("}");
            } else {
                items.push(this.paramItem());
            }
            this.expect(";");

            let entries;
            if (!arraySize && items.every(item => item.kind === "number")) {
                const values = items.map(item => item.value);
                if (values.length > 4) throw new ARBError("a constant vector has more than four components", this.line);
                /*
                 * ARB program scalar constants are scalar vector initialisers:
                 *
                 *     PARAM half = 0.5;
                 *
                 * names { 0.5, 0.5, 0.5, 0.5 }, not the conventional GL
                 * vertex expansion { 0.5, 0, 0, 1 }.  glview's bump-map
                 * vertex program uses exactly that spelling for both operands
                 * of its final MAD; expanding it like a vertex made the DOT3
                 * combiner receive a mostly negative light vector and reduced
                 * the whole 1.4/1.5 test to the clear colour.
                 */
                if (values.length === 1) {
                    values.push(values[0], values[0], values[0]);
                } else {
                    while (values.length < 4)
                        values.push(values.length === 3 ? 1 : 0);
                }
                entries = [{ kind: "constant", values }];
            } else {
                entries = items.map(item => {
                    if (item.kind === "number")
                        return { kind: "constant",
                                 values: [item.value, item.value, item.value,
                                          item.value] };
                    if (item.kind === "vector") {
                        const values = item.values.slice();
                        if (values.length > 4) throw new ARBError("a constant vector has more than four components", this.line);
                        while (values.length < 4)
                            values.push(values.length === 3 ? 1 : 0);
                        return { kind: "constant", values };
                    }
                    if (!/^(state\.|program\.(env|local)\[)/.test(item.path)) throw new ARBError("invalid parameter binding", this.line);
                    return { kind: "binding", path: item.path };
                });
                // program.local[0..4]: one register a parameter (S2-48)
                entries = entries.flatMap(entry => {
                    const range = entry.kind === "binding" && /^(.*)\[(\d+)\.\.(\d+)\]$/.exec(entry.path);
                    if (!range) return [entry];
                    if (+range[3] < +range[2] || +range[3] - +range[2] >= MAX_PROGRAM_BINDINGS)
                        throw new ARBError("invalid parameter range", this.line);
                    const out = [];
                    for (let i = +range[2]; i <= +range[3]; ++i)
                        out.push({ kind: "binding", path: range[1] + "[" + i + "]" });
                    return out;
                });
            }
            this.params.set(name, { arraySize, entries });
        }

        paramItem() {
            if (this.accept("{")) {
                const values = [];
                do { values.push(this.signedNumber()); } while (this.accept(","));
                this.expect("}");
                return { kind: "vector", values };
            }
            if (this.peek().type === "number" || this.at("-") || this.at("+"))
                return { kind: "number", value: this.signedNumber() };
            return { kind: "binding", path: this.bindingPath() };
        }

        signedNumber() {
            let sign = 1;
            while (this.at("-") || this.at("+")) {
                if (this.next().value === "-") sign = -sign;
            }
            const token = this.next();
            if (token.type !== "number")
                throw new ARBError("expected a number", token.line);
            return sign * token.value;
        }

        instruction() {
            const token = this.next();
            let opcode = token.value;
            let saturate = false;
            if (opcode.endsWith("_SAT")) {
                saturate = true;
                opcode = opcode.slice(0, -4);
            }
            const info = INSTRUCTIONS[opcode];
            if (!info)
                throw new ARBError("unknown instruction '" + opcode + "'",
                    token.line);
            if (info.vertexOnly && this.target !== "vertex")
                throw new ARBError(opcode + " is vertex-program only", token.line);
            if (info.fragmentOnly && this.target !== "fragment")
                throw new ARBError(opcode + " is fragment-program only", token.line);

            const instruction = { opcode, info, saturate, line: token.line,
                                  at: token.at, sources: [] };
            if (!info.kill) {
                instruction.destination = this.destination();
                this.expect(",");
            }
            const sourceCount = info.sources;
            for (let i = 0; i < sourceCount; ++i) {
                const source = this.source();
                if (info.scalar && source.swizzle?.length !== 1)
                    throw new ARBError(opcode + " requires one scalar source component", source.line, source.at);
                instruction.sources.push(source);
                if (i + 1 < sourceCount) this.expect(",");
            }
            if (info.extendedSwizzle) instruction.extendedSwizzle = this.extendedSwizzle();
            if (info.texture) {
                this.expect(",");
                instruction.texture = this.bindingPath();       // texture[n]
                this.expect(",");
                // "2D" lexes as a number followed by an identifier, so the
                // target is read as raw tokens up to the terminator.
                let target = "";
                while (!this.at(";") && this.peek().type !== "eof")
                    target += String(this.next().value);
                instruction.textureTarget = target;
            }
            this.expect(";");
            this.instructions.push(instruction);
        }

        /*
         * A constant written in an instruction, `MUL r, r, 2.0;` or
         * `MOV r, {0, 0, 0, 1};`, as an anonymous PARAM (review S2-48: the
         * parser wanted a name). A scalar stands for all four components; a
         * short vector is filled out with 0 and a w of 1.
         */
        inlineConstant() {
            const item = this.paramItem();
            const values = item.kind === "vector" ? item.values.slice() :
                [item.value, item.value, item.value, item.value];
            if (values.length > 4) throw new ARBError("a constant vector has more than four components", this.line);
            while (values.length < 4) values.push(values.length === 3 ? 1 : 0);
            const name = "$constant" + this.params.size;
            this.params.set(name, { arraySize: 0, entries: [{ kind: "constant", values }] });
            return name;
        }

        // (each operand keeps where it starts: the generator's errors about
        // it report that position)
        destination() {
            const at = this.peek().at;
            const name = this.bindingPath(true);
            let mask = "xyzw";
            if (this.accept(".")) mask = normalizeMask(this.expectIdent());
            if (!/^(?=.+)x?y?z?w?$/.test(mask)) throw new ARBError("invalid destination mask", this.line);
            return { name, mask, line: this.line, at };
        }

        /*
         * SWZ's four components after the source, `x,-y,0,1`: each a source
         * component or 0 or 1, each with its own sign (review S2-48: read as
         * a dotted swizzle).
         */
        extendedSwizzle() {
            const components = [];
            for (let i = 0; i < 4; ++i) {
                this.expect(",");
                let negate = false;
                while (this.at("-") || this.at("+"))
                    if (this.next().value === "-") negate = !negate;
                const token = this.next();
                let component = String(token.value);
                if (token.type === "number" && token.value < 0) {
                    negate = !negate;
                    component = String(-token.value);
                }
                if (!/^(?:[01]|[xyzwrgba])$/.test(component))
                    throw new ARBError("'" + component + "' is not an extended swizzle component", token.line);
                components.push({ negate, component });
            }
            return components;
        }

        source() {
            let negate = false;
            let absolute = false;
            while (this.at("-") || this.at("+")) {
                if (this.next().value === "-") negate = !negate;
            }
            if (this.accept("|")) absolute = true;
            const at = this.peek().at;
            const name = this.at("{") || this.peek().type === "number" ?
                this.inlineConstant() : this.bindingPath(true);
            let swizzle = null;
            if (this.accept(".")) swizzle = String(this.next().value);
            if (swizzle && !/^[xyzwrgba]{1,4}$/.test(swizzle)) throw new ARBError("invalid source swizzle", this.line);
            if (absolute) this.expect("|");
            return { name, swizzle, negate, absolute, line: this.line, at };
        }
    }

    /* ================================================================== */
    /* Generator                                                          */
    /* ================================================================== */

    class Generator {
        constructor(parsed, options) {
            this.parsed = parsed;
            this.options = options || {};
            this.stage = parsed.target;
            if (this.stage === "vertex" && !parsed.options.has("ARB_position_invariant"))
                this.options = { ...this.options, clipPlaneCount: 0, clipPlaneMask: 0 };
            this.lines = [];
            this.helpers = new Set();
            this.stateFields = new Set();
            this.constants = [];          // local PARAM constants, inlined
            this.paramArrays = new Map();
            this.registerArrays = new Map();   // relative addressing, by array name
            this.usedAttributes = new Map();
            this.usedResults = new Set();
            this.textureUnits = new Map();  // unit -> target
            this.usesKill = false;
            this.usesAddress = false;
            this.envUsed = false;
            this.localUsed = false;
            this.parameterBindings = new Set();
            this.inputBindings = new Set();
            this.resolvingSources = new Set();
            this.relativeArrays = new Set();
            this.relativeStates = new Set();
            this.relativeConstants = new Set();
            this.relativeConstantCopies = 0;
            this.temporary = 0;
            for (const param of parsed.params.values()) {
                const count = param.entries.reduce((n, entry) => n + (entry.kind === "binding" &&
                    this.stateMatrix(entry.path.replace(/^state\./, "")) ? 4 : 1), 0);
                if (param.arraySize > 0 && param.arraySize !== count || !param.arraySize && count !== 1)
                    throw new ARBError("parameter array size does not match its initializer", 0);
                for (const entry of param.entries)
                    if (entry.kind === "binding" && !this.stateMatrix(entry.path.replace(/^state\./, ""))) this.resolveSource(entry.path);
            }
            for (const name of parsed.attribs.keys()) this.resolveSource(name);
            for (const name of parsed.outputs.keys()) this.resolveDestination({ name, mask: "xyzw", line: 0 });
            this.stateFields.clear(); this.usedAttributes.clear(); this.usedResults.clear();
            this.envUsed = this.localUsed = false;
            // Declared here rather than where it is emitted: the state layout
            // is built before the shell, so a field added later would not be
            // in the struct the shell then reads from.
            if (parsed.options.has("ARB_position_invariant")) {
                this.stateFields.add("mvp");
                if (this.options.vertexCapture) this.stateFields.add("modelview");
                this.useAttribute("vertex.position",
                    VERTEX_ATTRIBUTES["vertex.position"]);
            }
        }

        emit(text) { this.lines.push(text); }

        generate() {
            const body = [];
            for (const instruction of this.parsed.instructions)
                body.push(...this.located(instruction, () => this.instruction(instruction)));
            if (this.parsed.instructions.length > 4096) throw new ARBError("too many program instructions", 0);
            if (this.parsed.addresses.size > MAX_ADDRESS_REGISTERS) throw new ARBError("too many address registers", 0);
            if (this.parameterCount() > MAX_PROGRAM_BINDINGS)
                throw new ARBError("too many parameter bindings", 0);

            // WebGPU validates the inter-stage interface strictly. Desktop GL
            // leaves an unwritten ARB vertex result undefined, so when a
            // fixed-function fragment stage consumes an enabled unit we
            // materialise its missing output using this shell's existing zero
            // value for undefined results.
            if (this.stage === "vertex") {
                if (this.options.vertexCapture) this.usedResults.add("texcoord0");
                if (this.options.vertexCaptureRaster) for (let i = 1; i < 8; ++i) this.usedResults.add("texcoord" + i);
                for (const unit of this.options.forceVertexTexCoords || []) {
                    if (unit >= 0 && unit < MAX_TEXTURE_UNITS)
                        this.usedResults.add("texcoord" + unit);
                }
            }
            // Both stages share one compact GLState uniform. Supplying the
            // union makes the canonical field offsets identical in each WGSL
            // module even when the stages read different pieces of GL state.
            for (const field of this.options.forceStateFields || [])
                this.stateFields.add(field);
            if (this.stage === "fragment" && this.fogMode()) {
                this.stateFields.add("fogParams");
                this.stateFields.add("fogColor");
            }

            if (this.textureUnits.size) this.stateFields.add("samplerState");
            if (this.options.pointSprite) for (const field of ["pointParams", "viewportParams"]) this.stateFields.add(field);
            if (this.options.clipPlaneCount) for (const field of ["modelview", "clipPlanes"]) this.stateFields.add(field);
            if (this.options.alphaTest && !["always", "never"].includes(this.options.alphaTest)) this.stateFields.add("alphaRef");
            const out = [];
            out.push("diagnostic(off, derivative_uniformity);");
            out.push("// generated by gl_arb_program.js r" + ARB_REVISION +
                " (" + this.stage + " program)");
            out.push("");
            const layout = stateLayout.buildLayout([...this.stateFields]);
            if (this.stateFields.size) {
                out.push(layout.structText);
                out.push("@group(1) @binding(0) var<uniform> glState : GLState;");
                out.push("");
            }
            /*
             * program.env and program.local share one binding: they have the
             * same shape and the guest writes both through the same entry
             * point.  Do not declare the binding when neither namespace is
             * read.  WebGPU's automatic pipeline layout drops unused globals;
             * retaining the declaration here made the executor submit a
             * binding which was absent from the resulting layout.
             *
             * The two stages take *different* bindings because program.env and
             * program.local are per-program namespaces: a bound vertex and
             * fragment program each carry their own, and a single shared block
             * left one stage reading the other program's parameters.
             */
            if (this.envUsed || this.localUsed) {
                out.push("struct ARBParams {");
                out.push("    env : array<vec4<f32>, " +
                    MAX_PROGRAM_PARAMETERS + ">,");
                out.push("    local : array<vec4<f32>, " +
                    MAX_PROGRAM_PARAMETERS + ">,");
                out.push("}");
                out.push("@group(1) @binding(" +
                    PARAMETER_BINDING[this.stage] +
                    ") var<uniform> arbParams : ARBParams;");
                out.push("");
            }
            for (const [unit, target] of this.textureUnits) {
                out.push("@group(2) @binding(" + (unit * 2) + ") var t" + unit +
                    " : " + wgslTextureType(target) + ";");
                out.push("@group(2) @binding(" + (unit * 2 + 1) + ") var s" +
                    unit + " : " + (target.startsWith("SHADOW") ? "sampler_comparison" : "sampler") + ";");
            }
            if (this.textureUnits.size) out.push("");
            for (const [unit, target] of this.textureUnits) out.push(stateLayout.textureSamplingWGSL("gl_arb_sample_" + unit,
                target === "CUBE" ? "Cube" : target === "3D" ? "3D" : "2D", target.startsWith("SHADOW"), "fragment"));
            for (const helper of this.helpers) out.push(HELPERS[helper], "");
            if (this.stage === "fragment" && this.options.logicOp && this.options.logicOp !== 0x1503) {
                for (let i = 0; i < (this.options.colorTargets || 1); ++i) out.push("@group(3) @binding(" + i + ") var glLogicTarget" + i + " : texture_2d<f32>;");
                out.push("fn glApplyLogic(src:vec4<f32>,dst:vec4<f32>)->vec4<f32>{",
                    "let s=vec4<u32>(round(clamp(src,vec4<f32>(0.0),vec4<f32>(1.0))*255.0));",
                    "let d=vec4<u32>(round(clamp(dst,vec4<f32>(0.0),vec4<f32>(1.0))*255.0));",
                    "return vec4<f32>((" + translator.logicOpExpression(this.options.logicOp) + ")&vec4<u32>(255u))/255.0;}");
            }

            if (this.stage === "vertex") this.emitVertexShell(out, body);
            else this.emitFragmentShell(out, body);
            return out.join("\n") + "\n";
        }

        // the OPTION ARB_fog_* a fragment program asks for, or null
        fogMode() {
            const options = this.parsed.options;
            return options.has("ARB_fog_linear") ? "linear" : options.has("ARB_fog_exp") ? "exp" :
                options.has("ARB_fog_exp2") ? "exp2" : null;
        }

        emitVertexShell(out, body) {
            const capture = !!this.options.vertexCapture;
            const flatLocations = new Set(this.options.forceFlatVaryings || []);
            const varying = location => (flatLocations.has(location) ?
                "    @interpolate(flat) " : "    ") +
                "@location(" + location + ") ";
            if (this.options.pointSprite && !capture) out.push(stateLayout.pointRasterWGSL(true, this.options.depthClamp, this.options.pointRasterRounded));
            out.push("struct VSIn {");
            for (const [, info] of this.usedAttributes)
                out.push("    " + (capture ? "" : "@location(" + info.location + ") ") + info.field +
                    " : " + vecType(info.components || 4) + ",");
            if (this.options.pointSprite && !capture) out.push("    @builtin(vertex_index) cornerIndex:u32,");
            else if (!this.usedAttributes.size)
                out.push("    " + (capture ? "" : "@builtin(vertex_index) ") + "vertexIndex : u32,");
            out.push("}");
            out.push("");
            if (capture) {
                out.push(translator.vertexCaptureBindings(this.options.vertexCaptureRaster));
                out.push("@compute @workgroup_size(" + translator.VERTEX_CAPTURE.workgroupSize + ")");
                out.push("fn capture_main(@builtin(global_invocation_id) id : vec3<u32>) {");
                out.push("    if (id.x >= arrayLength(&glCaptureOutput)) { return; }");
                out.push("    var vin : VSIn;");
                for (const [, info] of this.usedAttributes)
                    out.push("    vin." + info.field + " = glCaptureInput[id.x].attributes[" + info.location + "]." + "xyzw".slice(0, info.components || 4) + ";");
            } else {
            out.push("struct VSOut {");
            out.push("    @invariant @builtin(position) position : vec4<f32>,");
            out.push(varying(0) + "frontColor : vec4<f32>,");
            out.push(varying(1) + "frontSecondary : vec4<f32>,");
            out.push(varying(2) + "fogCoord : vec4<f32>,");
            if (this.options.twoSided) {
                out.push(varying(11) + "backColor:vec4<f32>,");
                out.push(varying(12) + "backSecondary:vec4<f32>,");
            }
            if (this.options.pointSprite) out.push("    @location(13) pointCoord:vec2<f32>,");
            if (this.options.clipPlaneCount) out.push("    @location(14) clipDist:vec4<f32>,");
            if (this.options.clipPlaneCount > 4) out.push("    @location(15) clipExtra:vec2<f32>,");
            for (let i = 0; i < 8; ++i)
                if (this.usedResults.has("texcoord" + i))
                    out.push(varying(3 + i) + "texcoord" + i +
                        " : vec4<f32>,");
            out.push("}");
            out.push("");
            out.push("@vertex");
            out.push("fn vs_main(vin : VSIn) -> VSOut {");
            }
            out.push("    var result_position = vec4<f32>(0.0, 0.0, 0.0, 1.0);");
            out.push("    var frontColor = vec4<f32>(1.0);");
            out.push("    var frontSecondary = vec4<f32>(0.0);");
            out.push("    var backColor = vec4<f32>(1.0);");
            out.push("    var backSecondary = vec4<f32>(0.0);");
            out.push("    var fogCoordValue = vec4<f32>(0.0);");
            out.push("    var pointSize = vec4<f32>(1.0);");
            for (let i = 0; i < 8; ++i)
                if (this.usedResults.has("texcoord" + i))
                    out.push("    var texcoord" + i + " = vec4<f32>(0.0);");
            for (const name of this.parsed.temporaries)
                out.push("    var " + safe(name) + " = vec4<f32>(0.0);");
            for (const name of this.parsed.addresses)
                out.push("    var " + safe(name) + " = vec4<i32>(0);");
            for (const declaration of this.registerArrays.values()) out.push("    " + declaration);
            for (const line of body) out.push("    " + line);
            if (this.parsed.options.has("ARB_position_invariant")) {
                // The option says the program does not write result.position
                // and the fixed-function transform supplies it -- and it must
                // be the *same* expression, or a depth pre-pass z-fights.
                out.push("    result_position = glState.mvp * vin.gl_Vertex;");
            }
            if (capture) {
                out.push("    glCaptureOutput[id.x].clip = result_position;");
                out.push("    glCaptureOutput[id.x].color = saturate(frontColor);");
                out.push("    glCaptureOutput[id.x].backColor = saturate(backColor);");
                out.push("    glCaptureOutput[id.x].tex = texcoord0;");
                // ARB_vertex_program 2.11 disables user clip planes unless
                // ARB_position_invariant supplies the fixed transformation.
                out.push("    glCaptureOutput[id.x].eye = " + (this.parsed.options.has("ARB_position_invariant") ?
                    "glState.modelview * vin.gl_Vertex" : "vec4<f32>(0.0)") + ";");
                if (this.options.vertexCaptureRaster) {
                    out.push("    glCaptureOutput[id.x].secondary = saturate(frontSecondary);");
                    out.push("    glCaptureOutput[id.x].fog = fogCoordValue;");
                    for (let i = 1; i < 8; ++i) out.push("    glCaptureOutput[id.x].texExtra[" + (i - 1) + "] = texcoord" + i + ";");
                }
                out.push("}");
                return;
            }
            out.push("    var out : VSOut;");
            out.push("    var clip = result_position;");
            if (this.options.pointSprite) {
                out.push("    let corner=gl_point_corner(vin.cornerIndex);");
                out.push("    let size=clamp(" + (this.options.programPointSize ? "pointSize.x" : "glState.pointParams.x") + ",1.0,64.0);");
                out.push("    clip=gl_point_position(clip,size,corner,glState.viewportParams);");
                out.push("    out.pointCoord=vec2<f32>(corner.x*0.5+0.5," + (this.options.pointCoordLowerLeft ? "corner.y*0.5+0.5" : "0.5-corner.y*0.5") + ");");
            }
            out.push("    clip.z = (clip.z + clip.w) * 0.5;");
            out.push("    clip.y = -clip.y;");
            out.push("    out.position = clip;");
            // (colours are clamped to [0, 1] before they are interpolated)
            out.push("    out.frontColor = saturate(frontColor);");
            out.push("    out.frontSecondary = saturate(frontSecondary);");
            if (this.options.twoSided) {
                out.push("    out.backColor=saturate(backColor);");
                out.push("    out.backSecondary=saturate(backSecondary);");
            }
            if (this.options.clipPlaneCount) {
                const mask = this.options.clipPlaneMask ?? (1 << this.options.clipPlaneCount) - 1,
                    distance = i => mask & 1 << i ? "dot(glState.modelview*vin.gl_Vertex,glState.clipPlanes[" + i + "])" : "1.0";
                out.push("    out.clipDist=vec4<f32>(" + Array.from({ length: 4 }, (_, i) => distance(i)).join(",") + ");");
                if (this.options.clipPlaneCount > 4) out.push("    out.clipExtra=vec2<f32>(" + [4, 5].map(distance).join(",") + ");");
            }
            out.push("    out.fogCoord = fogCoordValue;");
            for (let i = 0; i < 8; ++i)
                if (this.usedResults.has("texcoord" + i))
                    out.push("    out.texcoord" + i + " = texcoord" + i + ";");
            out.push("    return out;");
            out.push("}");
        }

        emitFragmentShell(out, body) {
            out.push("struct FSIn {");
            out.push("    @builtin(position) position : vec4<f32>,");
            const flat = new Set(this.options.forceFlatVaryings || []), varying = slot => "    " + (flat.has(slot) ? "@interpolate(flat) " : "") + "@location(" + slot + ") ";
            out.push(varying(0) + "frontColor : vec4<f32>,");
            out.push(varying(1) + "frontSecondary : vec4<f32>,");
            if (this.options.twoSided) {
                out.push("    @builtin(front_facing) frontFacing:bool,");
                out.push(varying(11) + "backColor:vec4<f32>,");
                out.push(varying(12) + "backSecondary:vec4<f32>,");
            }
            out.push("    @location(2) fogCoord : vec4<f32>,");
            if (this.options.pointSprite) out.push("    @location(13) pointCoord:vec2<f32>,");
            if (this.options.clipPlaneCount) out.push("    @location(14) clipDist:vec4<f32>,");
            if (this.options.clipPlaneCount > 4) out.push("    @location(15) clipExtra:vec2<f32>,");

            for (let i = 0; i < 8; ++i)
                if (this.usedResults.has("in_texcoord" + i))
                    out.push("    @location(" + (3 + i) + ") texcoord" + i +
                        " : vec4<f32>,");
            out.push("}");
            out.push("");
            out.push("struct FSOut {");
            for (let i = 0; i < (this.options.colorTargets || 1); ++i) out.push("    @location(" + i + ") color" + i + " : vec4<f32>,");
            if (this.usedResults.has("depth"))
                out.push("    @builtin(frag_depth) depth : f32,");
            out.push("}");
            out.push("");
            out.push("@fragment");
            out.push("fn fs_main(fin : FSIn) -> FSOut {");
            if (this.options.clipPlaneCount) out.push("    if(any(fin.clipDist<vec4<f32>(0.0))){discard;}");
            if (this.options.clipPlaneCount > 4) out.push("    if(any(fin.clipExtra<vec2<f32>(0.0))){discard;}");
            out.push("    var color0 = vec4<f32>(0.0, 0.0, 0.0, 1.0);");
            out.push("    var depthValue = fin.position.z;");
            for (const name of this.parsed.temporaries)
                out.push("    var " + safe(name) + " = vec4<f32>(0.0);");
            for (const line of body) out.push("    " + line);
            const fog = this.fogMode();
            if (fog) {
                // OPTION ARB_fog_*: the fixed fog after the program, from
                // fragment.fogcoord (review S2-50: ignored)
                const c = "fin.fogCoord.x";
                const factor = fog === "linear" ? "(glState.fogParams.z - " + c + ") * glState.fogParams.w" :
                    fog === "exp" ? "exp(-glState.fogParams.x * " + c + ")" :
                        "exp(-(glState.fogParams.x * " + c + ") * (glState.fogParams.x * " + c + "))";
                out.push("    color0 = vec4<f32>(mix(glState.fogColor.rgb, color0.rgb, clamp(" + factor +
                    ", 0.0, 1.0)), color0.a);");
            }
            out.push("    var out : FSOut;");
            if (this.options.alphaTest === "never") out.push("    discard;");
            const alpha = {less:"<",equal:"==",lequal:"<=",greater:">",notequal:"!=",gequal:">="}[this.options.alphaTest];
            if (alpha) out.push("    if(!(color0.a " + alpha + " glState.alphaRef.x)){discard;}");
            for (let i = 0; i < (this.options.colorTargets || 1); ++i) out.push("    out.color" + i + " = " +
                (this.options.logicOp && this.options.logicOp !== 0x1503 ? "glApplyLogic(color0,textureLoad(glLogicTarget" + i + ",vec2<i32>(floor(fin.position.xy)),0))" : "color0") + ";");
            if (this.usedResults.has("depth"))
                out.push("    out.depth = depthValue;");
            out.push("    return out;");
            out.push("}");
        }

        instruction(instruction) {
            const info = instruction.info;
            const lines = [];

            if (info.kill) {
                const source = this.sourceExpression(instruction.sources[0]);
                lines.push("if (any(" + source + " < vec4<f32>(0.0))) { discard; }");
                this.usesKill = true;
                return lines;
            }

            let value;
            if (instruction.extendedSwizzle) {
                const source = this.sourceExpression(instruction.sources[0]);
                value = "vec4<f32>(" + instruction.extendedSwizzle.map(({ negate, component }) => {
                    const c = component === "0" ? "0.0" : component === "1" ? "1.0" :
                        "(" + source + ")." + "xyzw"["xyzwrgba".indexOf(component) % 4];
                    return negate ? "-(" + c + ")" : c;
                }).join(", ") + ")";
            } else if (info.texture) {
                value = this.textureInstruction(instruction);
            } else if (info.address) {
                const source = this.sourceExpression(instruction.sources[0]);
                const target = this.resolveDestination(instruction.destination);
                if (!target.address || !["x", "xyzw"].includes(instruction.destination.mask)) throw new ARBError("ARL must write an address register's x component", instruction.line);
                this.usesAddress = true;
                lines.push(target.name + " = vec4<i32>(i32(floor(" + source +
                    ".x)));");
                return lines;
            } else {
                const sources = instruction.sources.map(source =>
                    this.sourceExpression(source));
                value = info.emit(sources);
                if (info.emit === HELPERS.arbLit) this.helpers.add("arbLit");
            }
            if (instruction.opcode === "LIT") this.helpers.add("arbLit");
            if (instruction.opcode === "LOG") this.helpers.add("arbLog");
            if (instruction.saturate)
                value = "clamp(" + value + ", vec4<f32>(0.0), vec4<f32>(1.0))";

            const destination = this.resolveDestination(instruction.destination);
            if (destination.address) throw new ARBError("only ARL can write an address register", instruction.line);
            const mask = instruction.destination.mask;
            if (destination.name === "depthValue") {
                // result.depth is a scalar: its z component is the depth
                // (review S2-49: .z and whole writes made invalid WGSL)
                if (mask === "" || mask.includes("z")) lines.push("depthValue = (" + value + ").z;");
            } else if (mask === "xyzw" || mask === "") {
                lines.push(destination.name + " = " + value + ";");
            } else {
                const temporary = "_a" + (this.temporary++);
                lines.push("let " + temporary + " = " + value + ";");
                for (const component of mask)
                    lines.push(destination.name + "." + component + " = " +
                        temporary + "." + component + ";");
            }
            return lines;
        }

        textureInstruction(instruction) {
            const unit = this.textureUnit(instruction.texture);
            const target = normalizeTextureTarget(instruction.textureTarget);
            this.textureUnits.set(unit, target);
            const source = this.sourceExpression(instruction.sources[0]);
            const shadow = target.startsWith("SHADOW"), one = target === "1D" || target === "SHADOW1D",
                triple = target === "CUBE" || target === "3D", projected = instruction.info.texture === "proj",
                bias = instruction.info.texture === "bias" ? "(" + source + ").w" : "0.0",
                q = projected ? " / (" + source + ").w" : "",
                rawCoordinate = one ? "vec2<f32>((" + source + ").x" + q + ", 0.5)" :
                    "(" + source + ")." + (triple ? "xyz" : "xy") + q,
                coordinate = target.endsWith("RECT") ? "(" + rawCoordinate + " / vec2<f32>(textureDimensions(t" + unit + ")))" : rawCoordinate,
                call = "gl_arb_sample_" + unit + "(t" + unit + ", s" + unit + ", " + coordinate + ", " + unit +
                    "u, 0.0, " + bias + ", false" + (shadow ? ", (" + source + ").z" + q : "") + ")";
            return shadow ? "vec4<f32>(vec3<f32>(" + call + "), 1.0)" : call;
        }

        textureUnit(path) {
            const match = /^texture\[(\d+)\]$/.exec(path) ||
                /^texture$/.exec(path);
            if (!match) throw new ARBError("unknown texture '" + path + "'", 0);
            const unit = match[1] ? parseInt(match[1], 10) : 0;
            if (unit < 0 || unit >= MAX_TEXTURE_UNITS)
                throw new ARBError("texture unit " + unit + " is out of range", 0);
            return unit;
        }

        // An error from fn without a position of its own is at `operand`
        located(operand, fn) {
            try {
                return fn();
            } catch (error) {
                if (error instanceof ARBError && error.arbOffset < 0 && operand.at !== undefined) {
                    error.arbOffset = operand.at;
                    if (!error.arbLine) error.arbLine = operand.line;
                }
                throw error;
            }
        }

        sourceExpression(source) {
            let text = this.located(source, () => this.resolveSource(source.name));
            this.recordBindings(source.name, text);
            if (source.swizzle) text = applySwizzle(text, source.swizzle);
            if (source.absolute) text = "abs(" + text + ")";
            if (source.negate) text = "(-(" + text + "))";
            return text;
        }

        resolveSource(name) {
            if (this.resolvingSources.has(name)) throw new ARBError("cyclic source alias '" + name + "'", 0);
            this.resolvingSources.add(name);
            try { return this.resolveSourceValue(name); }
            finally { this.resolvingSources.delete(name); }
        }

        recordBindings(name, expression) {
            while (this.parsed.aliases.has(name) || this.parsed.attribs.has(name))
                name = this.parsed.aliases.get(name) || this.parsed.attribs.get(name);
            const indexed = /^([A-Za-z_$][A-Za-z0-9_$]*)\[(.+)\]$/.exec(name);
            if (indexed && this.parsed.params.has(indexed[1]) && !/^\d+$/.test(indexed[2].trim())) {
                const array = indexed[1];
                if (!this.relativeArrays.has(array)) {
                    this.relativeArrays.add(array);
                    for (const value of this.paramRegisters(array)) {
                        this.parameterBindings.add(value);
                        if (/^vec4<f32>\([0-9+eE., \-]+\)$/.test(value)) {
                            this.relativeConstants.add(value); ++this.relativeConstantCopies;
                        } else {
                            if (this.relativeStates.has(value)) throw new ARBError("state bound twice in relatively addressed arrays", 0);
                            this.relativeStates.add(value);
                        }
                    }
                }
            } else if (name.startsWith("program.") || name.startsWith("state.") || this.parsed.params.has(name) ||
                    indexed && this.parsed.params.has(indexed[1])) {
                const relative = /^program\.(env|local)\[(.+)\]$/.exec(name);
                if (relative && !/^\d+$/.test(relative[2].trim())) {
                    for (let i = 0; i < parameterLimit(this.stage); ++i)
                        this.parameterBindings.add("arbParams." + relative[1] + "[" + i + "]");
                } else this.parameterBindings.add(expression);
            }
            if (name.startsWith("fragment.")) this.inputBindings.add(expression);
        }

        parameterCount() {
            return this.parameterBindings.size + this.relativeConstantCopies - this.relativeConstants.size;
        }

        resolveSourceValue(name) {
            const alias = this.parsed.aliases.get(name);
            if (alias) return this.resolveSource(alias);

            // A PARAM array element: `PARAM mvp[4] = { state.matrix.mvp };`
            // then `DP4 r0.x, mvp[0], pos;`. The matrix expands to its four
            // rows, which is the whole reason the canonical transform is
            // written as four DP4s.
            const indexed = /^([A-Za-z_$][A-Za-z0-9_$]*)\[(.+)\]$/.exec(name);
            if (indexed && this.parsed.params.has(indexed[1])) {
                const registers = this.paramRegisters(indexed[1]);
                const index = /^\s*(\d+)\s*$/.exec(indexed[2]);
                if (!index) {
                    // relative addressing, A0.x + k (review S2-48): the
                    // registers as an array the address register indexes,
                    // out of range reading as zero
                    const relative = this.relativeIndex(indexed[2]);
                    this.usesAddress = true;
                    const array = safe(indexed[1]) + "_registers";
                    if (!this.registerArrays.has(array))
                        this.registerArrays.set(array, "var " + array + " = array<vec4<f32>, " +
                            (registers.length + 1) + ">(" + registers.join(", ") + ", vec4<f32>(0.0));");
                    return array + "[select(" + registers.length + ", " + relative + ", " + relative +
                        " >= 0 && " + relative + " < " + registers.length + ")]";
                }
                const at = parseInt(index[1], 10);
                if (at >= registers.length)
                    throw new ARBError("PARAM " + indexed[1] + "[" + at +
                        "] is past the end of the array", 0);
                return registers[at];
            }

            if (this.parsed.temporaries.has(name)) return safe(name);
            if (this.parsed.addresses.has(name)) return safe(name);
            const attrib = this.parsed.attribs.get(name);
            if (attrib) return this.resolveSource(attrib);
            const param = this.parsed.params.get(name);
            if (param) return this.resolveParam(name, param);

            // program.env[n] / program.local[n], with optional relative
            // addressing through the address register.
            const parameter = /^program\.(env|local)\[(.+)\]$/.exec(name);
            if (parameter) {
                const which = parameter[1];
                if (which === "env") this.envUsed = true; else this.localUsed = true;
                return "arbParams." + which + "[" +
                    this.parameterIndex(parameter[2]) + "]";
            }
            if (name.startsWith("state."))
                return this.resolveState(name.slice(6));
            if (this.stage === "vertex") {
                const attribute = VERTEX_ATTRIBUTES[name];
                if (attribute) return this.useAttribute(name, attribute);
                const texcoord = /^vertex\.texcoord\[?(\d*)\]?$/.exec(name);
                if (texcoord) {
                    const unit = texcoord[1] ? parseInt(texcoord[1], 10) : 0;
                    return this.useAttribute(name, {
                        name: "gl_MultiTexCoord" + unit,
                        location: ATTR.gl_MultiTexCoord0 + unit,
                    });
                }
                const generic = /^vertex\.attrib\[(\d+)\]$/.exec(name);
                if (generic) {
                    const index = parseInt(generic[1], 10);
                    return this.useAttribute(name, {
                        name: "attrib" + index, location: index,
                    });
                }
            } else {
                if (name === "fragment.color" || name === "fragment.color.primary")
                    return this.options.twoSided ? "select(fin.backColor,fin.frontColor," + (this.options.backFacing ? "false" : "fin.frontFacing") + ")" : "fin.frontColor";
                if (name === "fragment.color.secondary") return this.options.twoSided ? "select(fin.backSecondary,fin.frontSecondary," + (this.options.backFacing ? "false" : "fin.frontFacing") + ")" : "fin.frontSecondary";
                if (name === "fragment.fogcoord") return "fin.fogCoord";
                if (name === "fragment.position") return "fin.position";
                const texcoord = /^fragment\.texcoord\[?(\d*)\]?$/.exec(name);
                if (texcoord) {
                    const unit = texcoord[1] ? parseInt(texcoord[1], 10) : 0;
                    if (unit >= MAX_TEXTURE_UNITS) throw new ARBError("texture coordinate is out of range", 0);
                    this.usedResults.add("in_texcoord" + unit);
                    if (this.options.pointSprite && this.options.pointCoordReplace?.includes(unit)) return "vec4<f32>(fin.pointCoord,0.0,1.0)";
                    return "fin.texcoord" + unit;
                }
            }
            throw new ARBError("unknown source '" + name + "'", 0);
        }

        parameterIndex(text) {
            const numeric = /^\d+$/.exec(text.trim());
            if (numeric) {
                const index = parseInt(numeric[0], 10);
                if (index >= parameterLimit(this.stage))
                    throw new ARBError("program parameter " + index +
                        " is beyond the " + parameterLimit(this.stage) +
                        " this implementation provides", 0);
                return String(index);
            }
            // Relative addressing: A0.x + k. WGSL indexes uniform arrays
            // dynamically, so this maps straight across.
            this.usesAddress = true;
            return "clamp(" + this.relativeIndex(text) + ", 0, " + (parameterLimit(this.stage) - 1) + ")";
        }

        relativeIndex(text) {
            const index = addressIndex(text), name = /^([A-Za-z_$][A-Za-z0-9_$]*)\.x(?:[+-]\d+)?$/.exec(text.replace(/\s+/g, ""));
            if (!name || !this.parsed.addresses.has(name[1])) throw new ARBError("relative access needs a declared address register's x component", 0);
            return index;
        }

        /* Flattens a PARAM declaration into the registers it occupies. */
        paramRegisters(name) {
            if (this.paramArrays.has(name)) return this.paramArrays.get(name);
            const param = this.parsed.params.get(name);
            const registers = [];
            for (const entry of param.entries) {
                if (entry.kind === "constant") {
                    registers.push("vec4<f32>(" +
                        entry.values.map(formatFloat).join(", ") + ")");
                    continue;
                }
                const path = entry.path.startsWith("state.") ?
                    entry.path.slice(6) : entry.path;
                const matrix = this.stateMatrix(path);
                if (matrix) {
                    this.stateFields.add(matrix.field);
                    for (let row = 0; row < 4; ++row)
                        registers.push("vec4<f32>(" + [0, 1, 2, 3].map(column =>
                            matrix.matrix + "[" + column + "][" + row + "]")
                            .join(", ") + ")");
                    continue;
                }
                registers.push(this.resolveSource(entry.path));
            }
            this.paramArrays.set(name, registers);
            return registers;
        }

        /* The matrix behind a state.matrix.* binding, or null if the path is
         * not a whole matrix. */
        stateMatrix(path) {
            path = normalizeStatePath(path);
            const binding = STATE_BINDINGS[path] || arrayMatrixBinding(path);
            return binding && binding.matrix ? binding : null;
        }

        resolveParam(name, param) {
            if (param.entries.length === 1 &&
                    param.entries[0].kind === "constant" && !param.arraySize) {
                const values = param.entries[0].values;
                return "vec4<f32>(" + values.map(formatFloat).join(", ") + ")";
            }
            if (param.entries.length === 1 &&
                    param.entries[0].kind === "binding" && !param.arraySize)
                return this.resolveSource(param.entries[0].path);
            // Naming a PARAM array without an index means its first register,
            // which is how a program written for one row spells it.
            return this.paramRegisters(name)[0];
        }

        resolveState(path) {
            path = normalizeStatePath(path);
            const direct = STATE_BINDINGS[path] || arrayMatrixBinding(path);
            if (direct) {
                this.stateFields.add(direct.field);
                if (direct.vector) return direct.vector;
                // A matrix binding without a row selector means row 0; the
                // usual spelling is state.matrix.mvp[i] via a PARAM array.
                return "vec4<f32>(" + direct.matrix + "[0][0], " +
                    direct.matrix + "[1][0], " + direct.matrix + "[2][0], " +
                    direct.matrix + "[3][0])";
            }
            const row = /^(matrix\..*?)\.row\[(\d+)\]$/.exec(path);
            if (row) {
                const binding = STATE_BINDINGS[row[1]] || arrayMatrixBinding(row[1]);
                if (binding && binding.matrix) {
                    this.stateFields.add(binding.field);
                    const index = parseInt(row[2], 10);
                    if (index > 3) throw new ARBError("matrix row is out of range", 0);
                    // ARB rows are the matrix's rows; WGSL indexes columns, so
                    // the row is gathered rather than indexed.
                    return "vec4<f32>(" + [0, 1, 2, 3].map(c =>
                        binding.matrix + "[" + c + "][" + index + "]").join(", ") + ")";
                }
            }
            for (const entry of INDEXED_STATE) {
                const match = entry.pattern.exec(path);
                if (!match) continue;
                const max = { lights: 8, frontLightProduct: 8, backLightProduct: 8, texEnvColor: 8, clipPlanes: 6, texGenPlanes: 8 }[entry.field];
                if (+match[1] >= max) throw new ARBError("state index is out of range", 0);
                this.stateFields.add(entry.field);
                return entry.expr(match[1], match[2], match[3]);
            }
            throw new ARBError("unsupported state binding 'state." + path + "'", 0);
        }

        useAttribute(path, info) {
            if (info.location < 0 || info.location >= 16) throw new ARBError("vertex attribute is out of range", 0);
            const field = info.name;
            if (!this.usedAttributes.has(field))
                this.usedAttributes.set(field, {
                    field, location: info.location,
                    components: info.components || 4,
                });
            void path;
            const components = info.components || 4;
            if (components === 4) return "vin." + field;
            if (components === 3) return "vec4<f32>(vin." + field + ", 1.0)";
            return "vec4<f32>(vin." + field + ", 0.0, 0.0, 1.0)";
        }

        resolveDestination(destination) {
            return this.located(destination, () => this.resolveDestinationName(destination));
        }

        resolveDestinationName(destination) {
            const name = destination.name;
            const alias = this.parsed.aliases.get(name);
            if (alias) return this.resolveDestination({ ...destination, name: alias });
            if (this.parsed.temporaries.has(name)) return { name: safe(name) };
            if (this.parsed.addresses.has(name)) return { name: safe(name), address: true };
            const output = this.parsed.outputs.get(name);
            if (output) return this.resolveDestination({ ...destination, name: output });

            if (this.stage === "vertex") {
                const texcoord = /^result\.texcoord\[?(\d*)\]?$/.exec(name);
                if (texcoord) {
                    const unit = texcoord[1] ? parseInt(texcoord[1], 10) : 0;
                    if (unit >= MAX_TEXTURE_UNITS) throw new ARBError("texture coordinate result is out of range", 0);
                    this.usedResults.add("texcoord" + unit);
                    return { name: "texcoord" + unit };
                }
                const mapped = VERTEX_RESULTS[name];
                if (mapped) {
                    this.usedResults.add(mapped);
                    if (mapped === "position") return { name: "result_position" };
                    if (mapped === "fogCoord") return { name: "fogCoordValue" };
                    return { name: mapped };
                }
            } else {
                const indexed = /^result\.color\[(\d+)\]$/.exec(name);
                if (indexed && indexed[1] === "0") return { name: "color0" };
                const mapped = FRAGMENT_RESULTS[name];
                if (mapped) {
                    this.usedResults.add(mapped);
                    return { name: mapped === "depth" ? "depthValue" : mapped };
                }
            }
            throw new ARBError("unknown destination '" + name + "'",
                destination.line);
        }
    }

    /* GL writes masks in xyzw or rgba; the generator only speaks xyzw. */
    /*
     * The spellings ARB_vertex_program allows for one binding, reduced to the
     * table's (review S2-48): matrix.modelview[0] is the modelview (it was
     * read as row 0), matrix.texture the unit's 0, lightprod and material
     * name a face, texgen without an index is unit 0's.
     */
    function normalizeStatePath(path) {
        return path
            .replace(/^matrix\.modelview\[0\]/, "matrix.modelview")
            .replace(/^matrix\.texture(?!\[)/, "matrix.texture[0]")
            .replace(/^lightprod\[(\d+)\]\.front\./, "lightprod[$1].")
            .replace(/^material\.front\./, "material.")
            .replace(/^texgen\.(eye|object)\./, "texgen[0].$1.");
    }

    // Program and texture state share the same row/modifier grammar. The
    // inverse variants are computed by the renderer, since WGSL has no inverse.
    function arrayMatrixBinding(path) {
        const match = /^matrix\.(texture|program)\[(\d+)\](?:\.(inverse|transpose|invtrans))?$/.exec(path);
        if (!match || +match[2] >= (match[1] === "program" ? stateLayout.MAX_PROGRAM_MATRICES : MAX_TEXTURE_UNITS)) return null;
        const suffix = { inverse: "Inverse", transpose: "Transpose", invtrans: "InverseTranspose" }[match[3]] || "";
        const field = match[1] + "Matrix" + suffix;
        return { field, matrix: "glState." + field + "[" + match[2] + "]" };
    }

    // `A0.x`, `A0.x+3`, `A0.x-1` as a WGSL integer expression
    function addressIndex(text) {
        const relative = /^([A-Za-z_$][A-Za-z0-9_$]*)\.([xyzw])(?:([+-])(\d+))?$/.exec(text.replace(/\s+/g, ""));
        if (!relative)
            throw new ARBError("unsupported parameter index '" + text + "'", 0);
        return "(" + safe(relative[1]) + "." + relative[2] + (relative[3] ? " " + relative[3] + " " +
            relative[4] : "") + ")";
    }

    function normalizeMask(text) {
        return String(text).replace(/[rgba]/g,
            ch => "xyzw"["rgba".indexOf(ch)]);
    }

    function safe(name) {
        return "arb_" + name.replace(/[^A-Za-z0-9_]/g, "_");
    }

    function vecType(components) {
        return components === 1 ? "f32" : "vec" + components + "<f32>";
    }

    function formatFloat(value) {
        if (Number.isInteger(value)) return value.toFixed(1);
        return String(value);
    }

    function wgslTextureType(target) {
        if (target.startsWith("SHADOW")) return "texture_depth_2d";
        switch (target) {
        case "CUBE": return "texture_cube<f32>";
        case "3D": return "texture_3d<f32>";
        default: return "texture_2d<f32>";
        }
    }

    function normalizeTextureTarget(text) {
        const upper = String(text);
        if (["1D", "2D", "3D", "CUBE", "RECT", "SHADOW1D", "SHADOW2D", "SHADOWRECT"].includes(upper)) return upper;
        throw new ARBError("unknown texture target '" + text + "'", 0);
    }

    /* ARB swizzles are one to four components, and a single component
     * replicates -- MOV r0, c0.x moves x into all four. */
    function applySwizzle(expression, swizzle) {
        const cleaned = swizzle.replace(/[^xyzwrgba01]/g, "")
            .replace(/[rgba]/g, ch => "xyzw"["rgba".indexOf(ch)]);
        if (!cleaned.length) return expression;
        if (/[01]/.test(cleaned)) {
            const parts = [...cleaned].map(ch =>
                ch === "0" ? "0.0" : (ch === "1" ? "1.0" :
                    "(" + expression + ")." + ch));
            while (parts.length < 4) parts.push("0.0");
            return "vec4<f32>(" + parts.join(", ") + ")";
        }
        if (cleaned.length === 1)
            return "vec4<f32>((" + expression + ")." + cleaned + ")";
        if (cleaned.length === 4) return "(" + expression + ")." + cleaned;
        const parts = [...cleaned].map(ch => "(" + expression + ")." + ch);
        while (parts.length < 4) parts.push(parts[parts.length - 1]);
        return "vec4<f32>(" + parts.join(", ") + ")";
    }

    /* ================================================================== */
    /* Public API                                                         */
    /* ================================================================== */

    // ARB_fragment_program 3.11.6 counts sequential nodes, including a
    // write-after-ALU-read hazard, rather than just texture dataflow depth.
    function instructionCounts(parsed) {
        const written = new Set(), aluUsed = new Set();
        let texture = 0, indirections = 1;
        const temporary = operand => {
            let name = operand && operand.name;
            while (parsed.aliases.has(name)) name = parsed.aliases.get(name);
            return parsed.temporaries.has(name) ? name : null;
        };
        for (const instruction of parsed.instructions) {
            const isTexture = instruction.info.texture || instruction.opcode === "KIL";
            const dest = temporary(instruction.destination), sources = instruction.sources.map(temporary).filter(Boolean);
            if (isTexture) {
                ++texture;
                if (sources.some(name => written.has(name)) || dest && aluUsed.has(dest)) {
                    ++indirections; written.clear(); aluUsed.clear();
                }
            } else {
                for (const name of sources) aluUsed.add(name);
                if (dest) aluUsed.add(dest);
            }
            if (dest) written.add(dest);
        }
        return { textureInstructionCount: texture, aluInstructionCount: parsed.instructions.length - texture,
            textureIndirections: indirections };
    }

    function compileARBProgram(source, options) {
        const result = { ok: false, log: "", target: null, errorPosition: -1 };
        const parser = new Parser(source);
        try {
            const parsed = parser.parse();
            const generator = new Generator(parsed, options);
            result.wgsl = generator.generate();
            result.target = parsed.target;
            result.stage = parsed.target;
            result.reflection = {
                ...instructionCounts(parsed),
                stateFields: [...generator.stateFields],
                attributes: [...generator.usedAttributes.values()],
                textures: [...generator.textureUnits.entries()].map(
                    ([unit, target]) => ({ unit, target })),
                // the texture coordinates a fragment program reads
                fragmentTexCoords: [...generator.usedResults].filter(name => name.startsWith("in_texcoord"))
                    .map(name => Number(name.slice(11))).sort((a, b) => a - b),
                usesKill: generator.usesKill,
                usesAddress: generator.usesAddress,
                usesEnv: generator.envUsed,
                usesLocal: generator.localUsed,
                instructionCount: parsed.instructions.length,
                temporaryCount: parsed.temporaries.size,
                parameterCount: generator.parameterCount(),
                attributeCount: parsed.target === "vertex" ? new Set([...generator.usedAttributes.values()].map(a => a.location)).size : generator.inputBindings.size,
                addressRegisterCount: parsed.addresses.size,
                positionInvariant: parsed.options.has("ARB_position_invariant"),
                maxParameters: parameterLimit(parsed.target),
            };
            result.ok = true;
        } catch (error) {
            if (!(error instanceof ARBError)) throw error;
            result.log = "ERROR: line " + (error.arbLine || 0) + ": " +
                error.message;
            result.errorPosition = error.arbOffset >= 0 ? error.arbOffset :
                parser.currentOffset();
        }
        return result;
    }

    const api = {
        ARB_REVISION, MAX_PROGRAM_PARAMETERS, MAX_FRAGMENT_PROGRAM_PARAMETERS, MAX_PROGRAM_BINDINGS, MAX_ADDRESS_REGISTERS, parameterLimit, MAX_TEMPORARIES,
        PARAMETER_BINDING,
        compileARBProgram, lex, Parser, Generator, ARBError,
        INSTRUCTIONS, STATE_BINDINGS, applySwizzle,
    };
    global.GLARBProgram = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The GL shadow's ARB assembly programs (docs/opengl-webgpu-plan.md C5):
// ARB_vertex_program's and ARB_fragment_program's objects, parameters and
// queries, on the CPU side and by the extensions' rules:
// - a program string that does not assemble leaves the program as it was
//   (Mesa's reading), and sets the error position and string;
// - environment parameters belong to the context and the target, shared
//   by every program of it and settable with none bound; local ones to the
//   program object;
// - program object 0 is a real (default) program of each target.
// The assembler is gl_arb_program.js's (its parse and its checks). The
// renderer uses the same target limits and parameter storage layout.

(function(global) {
    "use strict";

    const protocol = global.GLProtocol || require("./gl_protocol.js");
    const constants = global.GLWGConstants || require("./gl_constants.js");
    const arb = global.GLARBProgram || require("./gl_arb_program.js");
    const { GLShadow } = global.V86GLShadow || require("./gl_shadow.js");
    const { GLFN, C, F, RECORDS } = protocol;
    const GL = constants.GL;

    const OK = C.V86GL_SYNC_QUERY_STATUS_OK;
    const FAILED = C.V86GL_SYNC_QUERY_STATUS_FAILED;
    const ENV = C.V86GL_PROGRAM_PARAMETER_ENV, LOCAL = C.V86GL_PROGRAM_PARAMETER_LOCAL;
    // Physical storage is shared in shape; logical limits depend on target.
    const MAX_PARAMETERS = arb.MAX_PROGRAM_PARAMETERS;
    const TARGETS = { [GL.VERTEX_PROGRAM_ARB]: "vertex", [GL.FRAGMENT_PROGRAM_ARB]: "fragment" };
    const limit = target => arb.parameterLimit(TARGETS[target]);
    for (const name of ["GEN_PROGRAMS_ARB", "DELETE_PROGRAMS_ARB", "BIND_PROGRAM_ARB", "PROGRAM_STRING_ARB",
            "PROGRAM_PARAMETER_FV_ARB", "PROGRAM_PARAMETER_DV_ARB"]) GLShadow.rules({
        [GLFN[name]]: (shadow, bytes, view, offset, size) => {
            if (shadow.current.state && shadow.current.state.begin) return GL.INVALID_OPERATION;
            if (name === "GEN_PROGRAMS_ARB" || name === "DELETE_PROGRAMS_ARB")
                return shadow.names(name, view, offset, size) ? 0 : GL.INVALID_VALUE;
            if (name === "BIND_PROGRAM_ARB") {
                const a = shadow.args(name, view, offset, size), p = shadow.current.group.arbPrograms.get(a[1]);
                return !TARGETS[a[0]] ? GL.INVALID_ENUM : p && p.target && p.target !== a[0] ? GL.INVALID_OPERATION : 0;
            }
            const r = protocol.decodeRecord(name, view, offset);
            if (!TARGETS[r.target]) return GL.INVALID_ENUM;
            if (name === "PROGRAM_STRING_ARB") return r.format !== GL.PROGRAM_FORMAT_ASCII_ARB ? GL.INVALID_ENUM :
                r.length < 0 || RECORDS[name].size + r.length > size ? GL.INVALID_VALUE : 0;
            if (![ENV, LOCAL].includes(r.parameter_kind)) return GL.INVALID_ENUM;
            const count = name === "PROGRAM_PARAMETER_DV_ARB" ? 1 : r.count;
            const end = name === "PROGRAM_PARAMETER_DV_ARB" ? RECORDS[name].size : RECORDS[name].size + count * 16;
            return r.index >= limit(r.target) || count < 1 || count > limit(r.target) - r.index || end > size ? GL.INVALID_VALUE : 0;
        },
    });

    const newProgram = (name, target) => ({ name, target, source: "", compiled: null,
        local: new Float64Array(MAX_PARAMETERS * 4) });

    const proto = GLShadow.prototype;
    proto.arbQueryFailure = function(name, view, offset, error) {
        if (!this.current.error) this.current.error = error;
        return this.answered(name, view, offset, FAILED);
    };
    proto.arbQueryPending = function(name, view, offset, size) {
        if (!this.pending(name, view, offset, size)) return false;
        if (this.current.state && this.current.state.begin) {
            this.arbQueryFailure(name, view, offset, GL.INVALID_OPERATION); return false;
        }
        return true;
    };

    // The program bound to a target here (object 0 is the context's own)
    proto.arbProgram = function(target) {
        const c = this.current;
        if (!TARGETS[target]) return null;
        return c.arb.bindings[target] || c.arb.defaults[target];
    };

    proto.arbNamedProgram = function(name) {
        const group = this.current.group, programs = group.arbPrograms;
        if (!programs.has(name)) {
            const program = newProgram(name, 0); program.id = group.nextArbProgramId++; program.deleted = false;
            programs.set(name, program); group.arbObjects.set(program.id, program);
        }
        return programs.get(name);
    };

    proto.genProgramsARB = function(bytes, view, offset, size) {
        for (const name of this.names("GEN_PROGRAMS_ARB", view, offset, size) || [])
            if (name) this.arbNamedProgram(name);
    };

    // A deleted program bound here reverts to object 0
    proto.deleteProgramsARB = function(bytes, view, offset, size) {
        const c = this.current;
        for (const name of this.names("DELETE_PROGRAMS_ARB", view, offset, size) || []) {
            const program = c.group.arbPrograms.get(name);
            if (!name || !program) continue;
            c.group.arbPrograms.delete(name); program.deleted = true;
            for (const target of Object.keys(c.arb.bound)) if (c.arb.bindings[target] === program) {
                c.arb.bound[target] = 0; c.arb.bindings[target] = null;
            }
        }
    };

    proto.bindProgramARB = function(bytes, view, offset, size) {
        const a = this.args("BIND_PROGRAM_ARB", view, offset, size);
        if (!a || !TARGETS[a[0]]) return;
        const program = a[1] ? this.arbNamedProgram(a[1]) : null;
        // (a program of the other target cannot be bound to this one)
        if (program && program.target && program.target !== a[0]) return;
        if (program) program.target = a[0];
        this.current.arb.bound[a[0]] = a[1];
        this.current.arb.bindings[a[0]] = program;
    };

    // {target, format, length, reserved} then the text
    proto.programStringARB = function(bytes, view, offset, size) {
        const header = RECORDS.PROGRAM_STRING_ARB.size;
        if (size < header) return;
        const target = view.getUint32(offset + F.PROGRAM_STRING_ARB.target, true);
        const length = view.getInt32(offset + F.PROGRAM_STRING_ARB.length, true);
        const program = this.arbProgram(target);
        if (!program || length < 0 || header + length > size) return;
        let source = "";
        for (let i = 0; i < length && bytes[offset + header + i]; ++i) source += String.fromCharCode(bytes[offset + header + i]);
        const compiled = arb.compileARBProgram(source, {});
        const state = this.current.arb;
        if (compiled.ok && compiled.target !== TARGETS[target]) {
            compiled.ok = false;
            compiled.log = "ERROR: a " + compiled.target + " program cannot be loaded for the " + TARGETS[target] +
                " target";
            compiled.errorPosition = 0;
        }
        if (!compiled.ok) {
            state.errorPosition = compiled.errorPosition >= 0 ? compiled.errorPosition : 0;
            state.errorString = compiled.log;
            return GL.INVALID_OPERATION;
        }
        program.source = source;
        program.compiled = compiled;
        state.errorPosition = -1;
        state.errorString = "";
    };

    // {kind, target, index, count} then count * 4 floats, or {kind, target,
    // index, reserved} then four doubles
    proto.programParameterARB = function(doublePrecision) {
        return function(bytes, view, offset, size) {
            const name = doublePrecision ? "PROGRAM_PARAMETER_DV_ARB" : "PROGRAM_PARAMETER_FV_ARB";
            const R = F[name];
            if (size < RECORDS.PROGRAM_PARAMETER_FV_ARB.size) return;
            const target = view.getUint32(offset + R.target, true);
            const index = view.getUint32(offset + R.index, true);
            const count = doublePrecision ? 1 : Math.max(1, view.getUint32(offset + F.PROGRAM_PARAMETER_FV_ARB.count, true));
            const storage = this.arbParameters(view.getUint32(offset + R.parameter_kind, true), target);
            const at = doublePrecision ? F.PROGRAM_PARAMETER_DV_ARB.values : RECORDS.PROGRAM_PARAMETER_FV_ARB.size;
            const stride = doublePrecision ? 8 : 4;
            if (!storage || index + count > limit(target) || at + count * 4 * stride > size) return;
            for (let i = 0; i < count * 4; ++i)
                storage[index * 4 + i] = doublePrecision ? view.getFloat64(offset + at + i * 8, true) :
                    view.getFloat32(offset + at + i * 4, true);
        };
    };
    proto.programParameterFvARB = proto.programParameterARB(false);
    proto.programParameterDvARB = proto.programParameterARB(true);

    proto.arbParameters = function(kind, target) {
        if (!TARGETS[target]) return null;
        if (kind === ENV) return this.current.arb.env[target];
        const program = kind === LOCAL ? this.arbProgram(target) : null;
        return program ? program.local : null;
    };

    proto.programivARB = function(target, pname) {
        const program = this.arbProgram(target);
        const compiled = program && program.compiled;
        const reflection = compiled && compiled.reflection;
        if (target === GL.FRAGMENT_PROGRAM_ARB && pname >= GL.PROGRAM_ADDRESS_REGISTERS_ARB &&
                pname <= GL.MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB) return null;
        if (pname >= GL.PROGRAM_ALU_INSTRUCTIONS_ARB && pname <= GL.MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB) {
            if (target !== GL.FRAGMENT_PROGRAM_ARB) return null;
            if (pname >= GL.MAX_PROGRAM_ALU_INSTRUCTIONS_ARB) return 4096;
            const field = ["aluInstructionCount", "textureInstructionCount", "textureIndirections"][(pname - GL.PROGRAM_ALU_INSTRUCTIONS_ARB) % 3];
            return reflection ? reflection[field] : 0;
        }
        switch (pname) {
        case GL.PROGRAM_LENGTH_ARB: return program ? program.source.length : 0;
        case GL.PROGRAM_FORMAT_ARB: return GL.PROGRAM_FORMAT_ASCII_ARB;
        case GL.PROGRAM_BINDING_ARB: return this.current.arb.bound[target] || 0;
        case GL.PROGRAM_INSTRUCTIONS_ARB:
        case GL.PROGRAM_NATIVE_INSTRUCTIONS_ARB:
            return reflection ? reflection.instructionCount : 0;
        case GL.PROGRAM_TEMPORARIES_ARB:
        case GL.PROGRAM_NATIVE_TEMPORARIES_ARB:
            return reflection ? reflection.temporaryCount : 0;
        case GL.PROGRAM_PARAMETERS_ARB:
        case GL.PROGRAM_NATIVE_PARAMETERS_ARB:
            return reflection ? reflection.parameterCount : 0;
        case GL.PROGRAM_ATTRIBS_ARB:
        case GL.PROGRAM_NATIVE_ATTRIBS_ARB:
            return reflection ? reflection.attributeCount : 0;
        case GL.PROGRAM_ADDRESS_REGISTERS_ARB:
        case GL.PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB:
            return reflection ? reflection.addressRegisterCount : 0;
        case GL.MAX_PROGRAM_ATTRIBS_ARB:
        case GL.MAX_PROGRAM_NATIVE_ATTRIBS_ARB:
            return 16;
        case GL.MAX_PROGRAM_ADDRESS_REGISTERS_ARB:
        case GL.MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB:
            return arb.MAX_ADDRESS_REGISTERS;
        case GL.MAX_PROGRAM_PARAMETERS_ARB:
        case GL.MAX_PROGRAM_NATIVE_PARAMETERS_ARB:
            return arb.MAX_PROGRAM_BINDINGS;
        case GL.MAX_PROGRAM_ENV_PARAMETERS_ARB:
        case GL.MAX_PROGRAM_LOCAL_PARAMETERS_ARB:
            return limit(target);
        case GL.MAX_PROGRAM_INSTRUCTIONS_ARB:
        case GL.MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB:
            return 4096;
        case GL.MAX_PROGRAM_TEMPORARIES_ARB:
        case GL.MAX_PROGRAM_NATIVE_TEMPORARIES_ARB:
            return arb.MAX_TEMPORARIES;
        case GL.PROGRAM_UNDER_NATIVE_LIMITS_ARB: return compiled ? 1 : 0;
        case GL.PROGRAM_ERROR_POSITION_ARB: return this.current.arb.errorPosition;
        default: return null;
        }
    };

    // {target, pname, status, value}
    proto.queryProgramIvARB = function(bytes, view, offset, size) {
        const R = F.QUERY_PROGRAM_IV_ARB;
        if (!this.arbQueryPending("QUERY_PROGRAM_IV_ARB", view, offset, size)) return;
        const target = view.getUint32(offset + R.target, true);
        if (!TARGETS[target]) return this.arbQueryFailure("QUERY_PROGRAM_IV_ARB", view, offset, GL.INVALID_ENUM);
        const value = this.programivARB(target, view.getUint32(offset + R.pname, true));
        if (value === null) {
            return this.arbQueryFailure("QUERY_PROGRAM_IV_ARB", view, offset, GL.INVALID_ENUM);
        }
        view.setInt32(offset + R.value, value, true);
        this.answered("QUERY_PROGRAM_IV_ARB", view, offset, OK);
    };

    // {kind, target, index, status, data_size, reserved} then four floats or doubles
    proto.queryProgramParameterARB = function(doublePrecision) {
        return function(bytes, view, offset, size) {
            const name = doublePrecision ? "QUERY_PROGRAM_PARAMETER_DV_ARB" : "QUERY_PROGRAM_PARAMETER_FV_ARB";
            const R = F[name], header = RECORDS[name].size, stride = doublePrecision ? 8 : 4;
            if (!this.arbQueryPending(name, view, offset, size)) return;
            const index = view.getUint32(offset + R.index, true);
            const target = view.getUint32(offset + R.target, true);
            const kind = view.getUint32(offset + R.parameter_kind, true);
            if (!TARGETS[target] || ![ENV, LOCAL].includes(kind)) return this.arbQueryFailure(name, view, offset, GL.INVALID_ENUM);
            const storage = this.arbParameters(kind, target);
            if (!storage || index >= limit(target) || header + 4 * stride > size)
                return this.arbQueryFailure(name, view, offset, GL.INVALID_VALUE);
            for (let c = 0; c < 4; ++c) {
                if (doublePrecision) view.setFloat64(offset + header + c * 8, storage[index * 4 + c], true);
                else view.setFloat32(offset + header + c * 4, storage[index * 4 + c], true);
            }
            this.answered(name, view, offset, OK);
        };
    };
    proto.queryProgramParameterFvARB = proto.queryProgramParameterARB(false);
    proto.queryProgramParameterDvARB = proto.queryProgramParameterARB(true);

    // {target, pname, status, length, data_size, reserved} then the text,
    // unterminated (the caller sized it from PROGRAM_LENGTH_ARB)
    proto.queryProgramStringARB = function(bytes, view, offset, size) {
        const R = F.QUERY_PROGRAM_STRING_ARB, header = RECORDS.QUERY_PROGRAM_STRING_ARB.size;
        if (!this.arbQueryPending("QUERY_PROGRAM_STRING_ARB", view, offset, size)) return;
        const program = this.arbProgram(view.getUint32(offset + R.target, true));
        const capacity = view.getUint32(offset + R.data_size, true);
        if (!program || view.getUint32(offset + R.pname, true) !== GL.PROGRAM_STRING_ARB)
            return this.arbQueryFailure("QUERY_PROGRAM_STRING_ARB", view, offset, GL.INVALID_ENUM);
        if (header + capacity > size || capacity < program.source.length)
            return this.arbQueryFailure("QUERY_PROGRAM_STRING_ARB", view, offset, GL.INVALID_VALUE);
        const written = Math.min(program.source.length, capacity);
        for (let i = 0; i < written; ++i) bytes[offset + header + i] = program.source.charCodeAt(i) & 0xff;
        view.setUint32(offset + R.length, written, true);
        this.answered("QUERY_PROGRAM_STRING_ARB", view, offset, OK);
    };

    proto.programErrorPosition = function() {
        return this.current.arb.errorPosition;
    };

    proto.programErrorString = function() {
        return this.current.arb.errorString;
    };

    GLShadow.install({
        [GLFN.GEN_PROGRAMS_ARB]: "genProgramsARB",
        [GLFN.DELETE_PROGRAMS_ARB]: "deleteProgramsARB",
        [GLFN.BIND_PROGRAM_ARB]: "bindProgramARB",
        [GLFN.PROGRAM_STRING_ARB]: "programStringARB",
        [GLFN.PROGRAM_PARAMETER_FV_ARB]: "programParameterFvARB",
        [GLFN.PROGRAM_PARAMETER_DV_ARB]: "programParameterDvARB",
        [GLFN.QUERY_PROGRAM_IV_ARB]: "queryProgramIvARB",
        [GLFN.QUERY_PROGRAM_PARAMETER_FV_ARB]: "queryProgramParameterFvARB",
        [GLFN.QUERY_PROGRAM_PARAMETER_DV_ARB]: "queryProgramParameterDvARB",
        [GLFN.QUERY_PROGRAM_STRING_ARB]: "queryProgramStringARB",
    });
    GLShadow.queries({
        integers: { [GL.PROGRAM_ERROR_POSITION_ARB]: "programErrorPosition" },
        strings: { [GL.PROGRAM_ERROR_STRING_ARB]: "programErrorString" },
    });

    // (a snapshot keeps the sources; they assemble again on restore)
    const programOut = p => [p.name, p.target, p.source, [...p.local], p.id || 0, !!p.deleted];
    const programIn = ([name, target, source, local, id, deleted]) => {
        const program = newProgram(name, target);
        program.id = id || 0; program.deleted = !!deleted;
        program.source = source;
        program.local.set(local);
        if (source) program.compiled = arb.compileARBProgram(source, {});
        return program;
    };

    GLShadow.part({
        group(group) {
            group.arbPrograms = new Map();
            group.arbObjects = new Map(); group.nextArbProgramId = 1;
        },
        context(context) {
            context.arb = {
                bound: { [GL.VERTEX_PROGRAM_ARB]: 0, [GL.FRAGMENT_PROGRAM_ARB]: 0 },
                bindings: { [GL.VERTEX_PROGRAM_ARB]: null, [GL.FRAGMENT_PROGRAM_ARB]: null },
                defaults: { [GL.VERTEX_PROGRAM_ARB]: newProgram(0, GL.VERTEX_PROGRAM_ARB),
                    [GL.FRAGMENT_PROGRAM_ARB]: newProgram(0, GL.FRAGMENT_PROGRAM_ARB) },
                env: { [GL.VERTEX_PROGRAM_ARB]: new Float64Array(MAX_PARAMETERS * 4),
                    [GL.FRAGMENT_PROGRAM_ARB]: new Float64Array(MAX_PARAMETERS * 4) },
                errorPosition: -1,
                errorString: "",
            };
        },
        getState(shadow, state) {
            state.arb = {
                version: 2,
                groups: [...shadow.groups.values()].map(g => [g.key, [...g.arbObjects.values()].map(programOut),
                    [...g.arbPrograms].map(([name, p]) => [name, p.id]), g.nextArbProgramId]),
                contexts: [...shadow.contexts.values()].map(c => [c.key, c.arb.bound,
                    Object.values(c.arb.defaults).map(programOut),
                    [GL.VERTEX_PROGRAM_ARB, GL.FRAGMENT_PROGRAM_ARB].map(t => [...c.arb.env[t]]),
                    c.arb.errorPosition, c.arb.errorString,
                    Object.entries(c.arb.bindings).map(([target, p]) => [target, p?.id || 0])]),
            };
        },
        setState(shadow, state) {
            if (!state.arb) return;
            for (const [key, programs, names, nextId] of state.arb.groups) {
                const group = shadow.groups.get(key);
                if (!group) continue;
                for (const saved of programs) {
                    const p = programIn(saved); if (!p.id) p.id = group.nextArbProgramId++;
                    group.arbObjects.set(p.id, p);
                    if (!names) group.arbPrograms.set(p.name, p);
                }
                for (const [name, id] of names || []) group.arbPrograms.set(name, group.arbObjects.get(id));
                group.nextArbProgramId = nextId || group.nextArbProgramId;
            }
            for (const [key, bound, defaults, env, errorPosition, errorString, bindings] of state.arb.contexts) {
                const context = shadow.contexts.get(key);
                if (!context) continue;
                context.arb.bound = bound;
                for (const target of Object.keys(bound)) context.arb.bindings[target] = bindings ?
                    context.group.arbObjects.get(new Map(bindings).get(target)) || null : context.group.arbPrograms.get(bound[target]) || null;
                for (const saved of defaults) context.arb.defaults[saved[1]] = programIn(saved);
                context.arb.env[GL.VERTEX_PROGRAM_ARB].set(env[0]);
                context.arb.env[GL.FRAGMENT_PROGRAM_ARB].set(env[1]);
                context.arb.errorPosition = errorPosition;
                context.arb.errorString = errorString;
            }
        },
        afterBatch(shadow) {
            const held = new Set();
            for (const c of shadow.contexts.values()) for (const p of Object.values(c.arb.bindings)) if (p) held.add(p);
            for (const g of shadow.groups.values()) for (const [id, p] of g.arbObjects)
                if (p.deleted && !held.has(p)) g.arbObjects.delete(id);
        },
    });
})(typeof globalThis !== "undefined" ? globalThis : this);

;
// The v86gl virtio device: the CPU side of the graphics proxy, a descriptor
// for v86's virtio_devices. It runs where the CPU runs (the page, or the CPU
// worker), knows nothing of WebGPU, and talks to the renderer
// (graphics_proxy.js) over a channel. See docs/graphics-proxy-plugin-plan.md.
//
// The PCI identity, slot, ports, configuration space and request layout are
// v86gl.sys's ABI. The VGL2/GLWG/D9WG rendering protocols ride in the batches
// unchanged. See docs/glbridge.md.
//
// Channel messages, device -> host:
//   batch      { id, generation, ownerToken?, frameId, flags, commandCount, descAddr,
//                descLen, batchAddr, responseBase, submitCount, bytes }
//   released   { ownerToken }                 the process that mapped the arena
//                                             let it go (it exited or was killed)
// host -> device:
//   write      { generation, offset, bytes, inlineId? }  readback into the arena
//              inlineId: GL command-memory response, expires on next submit;
//              absent: dedicated response slots / heartbeat, arena lifetime
//   done       { id }                         the batch no longer needs its capacity
//   available  { value }                      whether a renderer takes batches
//   capabilities { value }                    what the renderer's adapter backs
//                                             (gl_caps.js), for the GL shadow
//   machine_reset {}                          the guest was reset: no GL context
//                                             outlives it
//   initializing { value }                   hold descriptors until startup finishes
//   glslang    { module } or { error }        glslang's WebAssembly.Module, for
//                                             the GL shadow's GLSL answers
//
// The GL shadow (gl-webgpu/gl_shadow.js, when loaded) reads each GL batch in
// guest memory before it is posted and answers the guest's synchronous
// queries there.

(function(global) {
    "use strict";

    const MAGIC = 0x324C4756;
    // v86gl_pci.js's snapshots (state[92] before virtio_devices)
    const LEGACY_STATE_MAGIC = 0x56514731;
    const STATE_VERSION = 4;
    const ARENA_BYTES = 16 * 1024 * 1024;
    const RESPONSE_AREA_BYTES = 4 * 1024 * 1024;
    const HEADER_BYTES = 32;
    const REQUEST_BYTES = 24;
    const RESPONSE_BYTES = 16;
    const F_SHARED_ARENA = 0;
    const F_D3D_OWNER_ARENAS = 1;
    const F_VERSION_1 = 32;
    const REGISTER_ARENA = 1;
    const SUBMIT = 2;
    const UNREGISTER_ARENA = 3;
    const OK = 0;
    const INVALID = 1;
    const NO_ARENA = 2;
    const NO_RENDERER = 3;

    // Flow control across threads only: on one thread the renderer takes each
    // batch as it comes, as it always has
    const MAX_INFLIGHT_BATCHES = 8;
    const MAX_INFLIGHT_BYTES = 32 * 1024 * 1024;

    /**
     * @param channel { remote, post(message, transfer), listen(handler) }
     * @return a virtio_devices descriptor
     */
    function createV86GLDevice(channel) {
        const remote = !!channel.remote;
        const shadow = global.V86GLShadow ? new global.V86GLShadow.GLShadow() : null;
        // Standalone shadow tests can check GLSL without a GPU. The guest's
        // device always waits for the real renderer before publishing a link.
        if (shadow) shadow.requireGPUProgramValidation = true;
        let dev = null;
        let arenaAddress = 0;
        let arenaBytes = 0;
        let arenaGeneration = 0;
        let nextGeneration = 0;
        const ownerArenas = new Map();
        const writeArenas = new Map();
        // A queue reset revokes physical mappings, but the process and its GPU
        // resources remain live until an explicit owner release.
        const logicalOwners = new Set();
        let lastFrameId = 0;
        let lastBytes = 0;
        let submitCount = 0;
        let hostAvailable = false;
        let initializing = false;
        let nextBatch = 0;
        let legacyBatch = 0;
        const inflight = new Map();
        let inflightBytes = 0;
        let processing = false;
        let processAgain = false;
        let heldRequest = null;

        if (shadow) shadow.writeResponse = (target, bytes, statusOffset) => {
            if (target.generation !== arenaGeneration || target.batchId !== legacyBatch || !arenaBytes) return;
            const offset = HEADER_BYTES + target.offset;
            if (offset < HEADER_BYTES || offset + bytes.length > arenaBytes) return;
            dev.write_memory(bytes.subarray(0, statusOffset), arenaAddress + offset);
            dev.write_memory(bytes.subarray(statusOffset + 4), arenaAddress + offset + statusOffset + 4);
            dev.write_memory(bytes.subarray(statusOffset, statusOffset + 4), arenaAddress + offset + statusOffset);
        };
        if (shadow) shadow.onCPUResume = (bytes, segments, metadata) => {
            if (metadata?.generation !== arenaGeneration || !arenaBytes) return;
            const offset = metadata.offset || 0;
            channel.post({ type: "batch", continuation: true, generated: !!metadata.generated, id: metadata.batchId, generation: arenaGeneration,
                frameId: metadata.frameId, flags: metadata.generated || shadow.cpuBarrier || !metadata.root ? 0 : metadata.flags,
                commandCount: metadata.commandCount, descAddr: arenaAddress, descLen: bytes.length + HEADER_BYTES,
                batchAddr: metadata.generated ? 0 : arenaAddress + HEADER_BYTES + offset,
                inlineOffset: metadata.generated ? 0 : offset,
                responseBase: arenaBytes - RESPONSE_AREA_BYTES - HEADER_BYTES - (metadata.generated ? 0 : offset),
                submitCount: metadata.submitCount, bytes, ...(segments ? { segments } : {}) }, remote && bytes.buffer.byteLength ? [bytes.buffer] : undefined);
        };
        if (shadow) shadow.onCPUComplete = metadata => {
            if (heldRequest?.id === metadata.batchId && heldRequest.generation === metadata.generation) completeHeld(OK);
        };

        function completeHeld(status) {
            if (!heldRequest) return;
            const held = heldRequest; heldRequest = null;
            const response = held.response.slice(); response[0] = status;
            held.request.write(new Uint8Array(response.buffer)); held.request.complete(); dev.flush(0);
            processQueue();
        }

        function releaseArena() {
            arenaGeneration = ++nextGeneration;
            arenaAddress = 0;
            arenaBytes = 0;
        }

        function releasePhysicalArenas() {
            releaseArena();
            ownerArenas.clear();
            writeArenas.clear();
        }

        function overlaps(address, bytes, exceptOwner) {
            if (exceptOwner !== 0 && arenaBytes && address < arenaAddress + arenaBytes && arenaAddress < address + bytes)
                return true;
            for (const [owner, arena] of ownerArenas)
                if (owner !== exceptOwner && address < arena.address + arena.bytes && arena.address < address + bytes) return true;
            return false;
        }

        function validArena(address, bytes) {
            return Number.isSafeInteger(address) && address > 0 && bytes === ARENA_BYTES && dev.is_ram(address, bytes);
        }

        function forgetInflight() {
            inflight.clear();
            inflightBytes = 0;
        }

        function canAccept() {
            return !heldRequest && !shadow?.cpuBarrier && (!remote || inflight.size < MAX_INFLIGHT_BATCHES &&
                inflightBytes <= MAX_INFLIGHT_BYTES - ARENA_BYTES);
        }

        channel.listen(message => {
            switch (message.type) {
                case "initializing":
                    initializing = !!message.value;
                    if (!initializing && dev) processQueue();
                    break;
                case "write": {
                    // A readback lands in the arena of the batch that asked for it
                    const offset = message.offset;
                    const bytes = message.bytes;
                    const arena = writeArenas.get(message.generation) ||
                        (message.generation === arenaGeneration && arenaBytes ? { address: arenaAddress, bytes: arenaBytes } : null);
                    if (!arena) return;
                    // A timed-out GL inline response must not overwrite the
                    // next command stream, even when its page-side callback
                    // ran before that newer batch reached the page.
                    if (message.inlineId !== undefined && (message.generation !== arenaGeneration || message.inlineId !== legacyBatch)) return;
                    if (!Number.isInteger(offset) || offset < 0 || offset + bytes.length > arena.bytes)
                        throw new RangeError("v86gl write outside registered arena");
                    dev.write_memory(bytes, arena.address + offset);
                    break;
                }
                case "done": {
                    const bytes = inflight.get(message.id);
                    if (bytes === undefined) return;
                    inflight.delete(message.id);
                    inflightBytes -= bytes;
                    // Descriptors left with the guest while the renderer was full
                    processQueue();
                    break;
                }
                case "gl_vertex_result":
                    if (message.generation === arenaGeneration && arenaBytes && shadow?.completeVertexCapture)
                        shadow.completeVertexCapture(message);
                    break;
                case "gl_program_link_result":
                    if (message.generation === arenaGeneration && arenaBytes && shadow?.completeProgramLink)
                        shadow.completeProgramLink(message);
                    break;
                case "capabilities":
                    // (a restored snapshot's stand: the guest was told those)
                    if (shadow && !shadow.capabilities) shadow.setCapabilities(message.value);
                    break;
                case "machine_reset":
                    releasePhysicalArenas();
                    logicalOwners.clear();
                    if (shadow) shadow.reset();
                    completeHeld(INVALID);
                    break;
                case "glslang":
                    if (shadow && global.V86GLGlslang) {
                        try {
                            if (!message.module) throw new Error(message.error || "no compiler module was supplied");
                            shadow.setGlslang(new global.V86GLGlslang.Glslang(message.module));
                        } catch (error) {
                            shadow.setGlslangFailure(error.message);
                        }
                    } else if (shadow) shadow.setGlslangFailure("the compiler loader is missing");
                    break;
                case "available":
                    hostAvailable = !!message.value;
                    if (!hostAvailable) { if (shadow?.cpuBarrier) shadow.cancelCPU(); completeHeld(NO_RENDERER); }
                    // A renderer that is gone acknowledges nothing: answer the
                    // descriptors left with the guest (NO_RENDERER) rather than
                    // leave its driver waiting until it resets the queue
                    if (!hostAvailable && inflight.size) {
                        forgetInflight();
                        if (dev) processQueue();
                    }
                    break;
            }
        });

        // A local renderer answers inside post(): its "done" comes back while
        // the queue is being processed
        function processQueue() {
            if (processing) {
                processAgain = true;
                return;
            }
            processing = true;
            try {
                do {
                    processAgain = false;
                    processRequests();
                } while (processAgain);
            } finally {
                processing = false;
            }
        }

        function processRequests() {
            if (initializing) return;
            if (!dev.is_feature_negotiated(F_SHARED_ARENA) || !dev.is_feature_negotiated(F_VERSION_1)) return;
            // Leave descriptors with the guest until the renderer has room
            while (canAccept() && dev.has_request(0)) {
                const request = dev.pop_request(0);
                if (!request) break;
                if (request.readable !== REQUEST_BYTES || request.writable !== RESPONSE_BYTES) {
                    dev.needs_reset();
                    break;
                }
                let result = INVALID;
                try {
                    result = handleRequest(request.read());
                } catch (error) {
                    console.error("[virtio-v86gl] request failed", error);
                }
                const response = new Uint32Array([result, lastFrameId, lastBytes, submitCount]);
                if (result === OK && shadow?.cpuBarrier) {
                    heldRequest = { request, response, generation: arenaGeneration, id: nextBatch };
                    break;
                }
                request.write(new Uint8Array(response.buffer));
                request.complete();
            }
            dev.flush(0);
        }

        function handleRequest(request) {
            const view = new DataView(request.buffer, request.byteOffset, REQUEST_BYTES);
            const op = view.getUint32(0, true);
            const address = view.getUint32(4, true);
            const high = view.getUint32(8, true);
            const length = view.getUint32(12, true);
            const flags = view.getUint32(16, true);
            const ownerToken = view.getUint32(20, true);
            if (ownerToken && !dev.is_feature_negotiated(F_D3D_OWNER_ARENAS)) return INVALID;
            if (op === REGISTER_ARENA) {
                // A 64-bit guest-physical address: RAM anywhere on the 36-bit bus.
                const address64 = address + high * 0x100000000;
                if (flags || !validArena(address64, length) || overlaps(address64, length, ownerToken)) return INVALID;
                if (ownerToken) {
                    const old = ownerArenas.get(ownerToken);
                    // The driver registers before each D3D submit, including
                    // after a queue reset. Repeating it must retain write leases.
                    if (old) return old.address === address64 && old.bytes === length ? OK : INVALID;
                    const arena = { address: address64, bytes: length, generation: ++nextGeneration };
                    ownerArenas.set(ownerToken, arena);
                    writeArenas.set(arena.generation, arena);
                } else {
                    if (arenaBytes) return INVALID;
                    arenaGeneration = ++nextGeneration;
                    arenaAddress = address64;
                    arenaBytes = length;
                }
                logicalOwners.add(ownerToken);
                return OK;
            }
            if (address || high) return INVALID;
            if (op === UNREGISTER_ARENA) {
                if (length || flags) return INVALID;
                if (logicalOwners.delete(ownerToken)) channel.post({ type: "released", ownerToken });
                if (ownerToken) {
                    const arena = ownerArenas.get(ownerToken);
                    if (arena) writeArenas.delete(arena.generation);
                    ownerArenas.delete(ownerToken);
                } else {
                    releaseArena();
                    if (shadow) shadow.reset();
                }
                return OK;
            }
            if (op !== SUBMIT || (flags & ~1)) return INVALID;
            const arena = ownerToken ? ownerArenas.get(ownerToken) :
                arenaBytes ? { address: arenaAddress, bytes: arenaBytes, generation: arenaGeneration } : null;
            if (!arena) return NO_ARENA;
            if (length < HEADER_BYTES || length > arena.bytes) return INVALID;

            // A view of guest RAM while the batch has one contiguous backing
            // (checked per submission: the physical map may have changed)
            const raw = dev.read_memory(arena.address, length);
            const header = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
            const commandBytes = header.getUint32(20, true);
            // VGL2 reserved words belong to the graphics protocol, not to the
            // virtqueue request. Shipping D3D8/D3D9/DDraw DLLs put D9WG_MAGIC in
            // reserved0; the old PCI transport ignored both words. Preserve that ABI.
            if (header.getUint32(0, true) !== MAGIC || header.getUint32(4, true) !== 1 ||
                    commandBytes !== length - HEADER_BYTES || (header.getUint32(8, true) & ~1))
                return INVALID;
            // Positive tokens are dedicated to the common DirectX runtime;
            // legacy OpenGL remains on owner zero with its synchronous shadow.
            if (ownerToken && (length < HEADER_BYTES + 8 || header.getUint32(16, true) !== 1 ||
                    header.getUint16(HEADER_BYTES, true) !== 0xffe1 || header.getUint16(HEADER_BYTES + 2, true) !== 0xffff ||
                    header.getUint32(HEADER_BYTES + 4, true) !== commandBytes - 8)) return INVALID;
            if (!hostAvailable) return NO_RENDERER;
            const id = ++nextBatch;
            if (!ownerToken) legacyBatch = id;
            const frameId = header.getUint32(12, true);

            // A GL batch's answers, written into the guest's memory before
            // it is copied (a D3D batch is one envelope record, 0xFFE1)
            let segments;
            if (!ownerToken && shadow && !(length >= HEADER_BYTES + 4 && (header.getUint16(HEADER_BYTES, true) & 0xFFF0) === 0xFFE0 &&
                    header.getUint16(HEADER_BYTES + 2, true) === 0xFFFF))
                segments = shadow.process(raw.subarray(HEADER_BYTES), { generation: arenaGeneration, batchId: id, root: true, offset: 0,
                    frameId, flags: header.getUint32(8, true) | flags, commandCount: header.getUint32(16, true), submitCount: submitCount + 1 });

            // Across threads a copy the renderer owns; never the guest's memory
            const bytes = remote ? raw.slice(HEADER_BYTES) : raw.subarray(HEADER_BYTES);
            inflight.set(id, bytes.byteLength);
            inflightBytes += bytes.byteLength;
            channel.post({
                type: "batch",
                id,
                generation: arena.generation,
                ...(ownerToken ? { ownerToken } : {}),
                frameId,
                flags: !ownerToken && shadow?.cpuBarrier ? 0 : header.getUint32(8, true) | flags,
                commandCount: header.getUint32(16, true),
                descAddr: arena.address,
                descLen: length,
                batchAddr: arena.address + HEADER_BYTES,
                responseBase: arena.bytes - RESPONSE_AREA_BYTES - HEADER_BYTES,
                submitCount: submitCount + 1,
                bytes,
                ...(segments ? { segments } : {}),
            }, remote ? [bytes.buffer] : undefined);
            lastFrameId = frameId;
            lastBytes = commandBytes;
            ++submitCount;
            // Availability may change reentrantly while a local host consumes
            // post(). This batch has already been accepted: reporting failure
            // now would invite the caller to execute it a second time.
            return OK;
        }

        return {
            name: "v86gl",
            // The slot keeps Windows' device instance, and so the installed driver
            pci_slot: 0x13,
            io_base: 0xF100,
            // Provisional, unallocated local ID. NOT the standard virtio-gpu ID.
            device_id: 0x107F,
            subsystem_device_id: 0x5686,
            features: [F_SHARED_ARENA, F_D3D_OWNER_ARENAS],
            queues: [{ size: 8 }],
            config: [
                { bytes: 4, name: "magic", read: () => MAGIC },
                { bytes: 4, name: "version", read: () => 1 },
                { bytes: 4, name: "arena_bytes", read: () => ARENA_BYTES },
                { bytes: 4, name: "max_batch_bytes", read: () => ARENA_BYTES },
            ],
            init(handle) {
                dev = handle;
            },
            notify() {
                processQueue();
            },
            // Batches the renderer has not acknowledged yet
            busy() {
                // A renderer acknowledgement can precede the CPU's delivery
                // of a vertex result. The retained IOCTL is still live even
                // after its original rendering batch has been acknowledged.
                return inflight.size + +(!!heldRequest || !!shadow?.cpuBarrier);
            },
            // What the GL shadow did (tests)
            shadow_stats() {
                return shadow ? shadow.stats : null;
            },
            reset() {
                heldRequest = null;
                shadow?.cancelCPU();
                releasePhysicalArenas();
                lastFrameId = lastBytes = submitCount = 0;
                forgetInflight();
                // (not the shadow's contexts: this is also the driver's queue
                // reset after a timeout, and the owner is still there; the
                // renderer says when the machine was reset)
            },
            get_state() {
                if (heldRequest || shadow?.cpuBarrier)
                    throw new Error("GL snapshot requires completed CPU state dependencies");
                return [STATE_VERSION, arenaAddress, arenaBytes, lastFrameId, lastBytes, submitCount,
                    shadow ? JSON.stringify(shadow.getState()) : "",
                    Array.from(ownerArenas, ([owner, arena]) => [owner, arena.address, arena.bytes]),
                    Array.from(logicalOwners)];
            },
            set_state(state) {
                // Versions 2/3 contain only the legacy arena.
                if (!Array.isArray(state) || ![2, 3, STATE_VERSION].includes(state[0]))
                    throw new Error("Unsupported virtio-v86gl state in snapshot");
                const [, address, bytes, frameId, last, count, shadowState] = state;
                const savedArenas = state[0] === STATE_VERSION ? state[7] : [];
                const savedOwners = state[0] === STATE_VERSION ? state[8] : bytes ? [0] : [];
                if (!Array.isArray(savedArenas) || !Array.isArray(savedOwners) ||
                        savedOwners.some(owner => !Number.isInteger(owner) || owner < 0 || owner > 0xffffffff) ||
                        new Set(savedOwners).size !== savedOwners.length)
                    throw new Error("Invalid virtio-v86gl owners in snapshot");
                const checked = new Map(), ranges = [];
                if (bytes) {
                    if (!validArena(address, bytes) || !savedOwners.includes(0))
                        throw new Error("Invalid virtio-v86gl arena in snapshot");
                    ranges.push([address, bytes]);
                }
                for (const entry of savedArenas) {
                    if (!Array.isArray(entry) || entry.length !== 3) throw new Error("Invalid virtio-v86gl owner arena");
                    const [owner, address, bytes] = entry;
                    if (!Number.isInteger(owner) || owner <= 0 || owner > 0xffffffff || checked.has(owner) ||
                            !savedOwners.includes(owner) || !validArena(address, bytes) ||
                            ranges.some(([start, size]) => address < start + size && start < address + bytes))
                        throw new Error("Invalid virtio-v86gl owner arena");
                    ranges.push([address, bytes]);checked.set(owner, { address, bytes });
                }
                if (shadow) shadow.setState(shadowState ? JSON.parse(shadowState) : null);
                heldRequest = null;
                releasePhysicalArenas();
                logicalOwners.clear();for (const owner of savedOwners) logicalOwners.add(owner);
                arenaAddress = bytes ? address : 0;
                arenaBytes = bytes;
                for (const [owner, arena] of checked) {
                    arena.generation = ++nextGeneration;
                    ownerArenas.set(owner, arena);writeArenas.set(arena.generation, arena);
                }
                lastFrameId = frameId;
                lastBytes = last;
                submitCount = count;
                forgetInflight();
            },
            // [magic, transport, arena address, arena bytes, frame id, bytes,
            //  submit count, 0, graphics checkpoint]
            upgrade_state(slot) {
                if (!Array.isArray(slot) || slot[0] !== LEGACY_STATE_MAGIC) return null;
                return [slot[1], [2, slot[2], slot[3], slot[4], slot[5], slot[6]], slot[8]];
            },
        };
    }

    const factories = global.V86VirtioDeviceFactories || (global.V86VirtioDeviceFactories = {});
    factories.v86gl = createV86GLDevice;
    if (typeof module !== "undefined" && module.exports)
        module.exports = { createV86GLDevice };
})(typeof globalThis !== "undefined" ? globalThis : this);
