/* Constructors supply metadata; all lifetime, binding, QI and query methods
 * below come from the production implementation. */
static D9VertexDeclaration *new_declaration(D9Device *device)
{
    D9VertexDeclaration *decl = HeapAlloc(NULL, 8, sizeof(*decl));
    decl->iface.lpVtbl = &g_decl_vtbl;decl->refcount = 1;
    decl->device = device;decl->handle = allocate_handle();decl->element_count = 2;
    decl->elements[0] = (D3DVERTEXELEMENT9){0,0,3,0,0,0};
    decl->elements[1] = (D3DVERTEXELEMENT9){0,16,4,0,10,0};
    decl->next_device_resource = device->vertex_declarations;device->vertex_declarations = decl;
    device_child_add_ref(device);return decl;
}
static D9Shader *new_shader(D9Device *device, BOOL pixel)
{
    D9Shader *shader = HeapAlloc(NULL, 8, sizeof(*shader));
    shader->iface.vertex.lpVtbl = pixel ? &g_pixel_shader_vtbl : &g_vertex_shader_vtbl;
    shader->device = device;shader->refcount = 1;shader->is_pixel = pixel;
    shader->handle = allocate_handle();shader->token_count = 2;
    shader->code = HeapAlloc(NULL, 8, 8);shader->code[0] = pixel ? 0xffff0200u : 0xfffe0200u;shader->code[1] = 0xffff;
    shader->next_device_resource = device->shaders;device->shaders = shader;
    device_child_add_ref(device);return shader;
}
static int shader_binding_cases(void)
{
    UINT profiles = 0, revivals = 0, old_checks = checks;
    for (UINT round = 0; round < 256; round++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);
        D9VertexDeclaration *decl = new_declaration(device);
        D9Shader *vs = new_shader(device, FALSE), *ps = new_shader(device, TRUE);
        Interface *di = &decl->iface, *vi = &vs->iface.vertex, *pi = &ps->iface.pixel;
        CHECK(!device_set_vertex_declaration(&device->iface, di) && !device_set_vertex_shader(&device->iface, vi) && !device_set_pixel_shader(&device->iface, pi));
        CHECK(decl->refcount == 1 && vs->refcount == 1 && ps->refcount == 1 && decl->binding_refs == 1 && vs->binding_refs == 1 && ps->binding_refs == 1 && device->refcount == 4 && device->child_parent_refs == 3);
        CHECK(!decl_release(di) && !vertex_shader_release(vi) && !pixel_shader_release(pi));
        CHECK(device->refcount == 1 && !device->child_parent_refs && !device_has_reset_blockers(device));
        CHECK(!decl_release(di) && !vertex_shader_release(vi) && !pixel_shader_release(pi));
        for (UINT cycle = 0; cycle < 8; cycle++) {
            void *dq = NULL, *vq = NULL, *pq = NULL;
            CHECK(!decl_query_interface(di, &IID_IUnknown, &dq) && dq == di);
            CHECK(!vertex_shader_query_interface(vi, &IID_IUnknown, &vq) && vq == vi);
            CHECK(!pixel_shader_query_interface(pi, &IID_IUnknown, &pq) && pq == pi);
            CHECK(device->refcount == 4 && device->child_parent_refs == 3 && decl->refcount == 1 && vs->refcount == 1 && ps->refcount == 1);
            Interface *dg = NULL, *vg = NULL, *pg = NULL;
            CHECK(!device_get_vertex_declaration(&device->iface, &dg) && dg == di);
            CHECK(!device_get_vertex_shader(&device->iface, &vg) && vg == vi);
            CHECK(!device_get_pixel_shader(&device->iface, &pg) && pg == pi);
            CHECK(decl->refcount == 2 && vs->refcount == 2 && ps->refcount == 2 && device->refcount == 4 && !device_has_reset_blockers(device));
            UINT count = 0;D3DVERTEXELEMENT9 elements[18];
            CHECK(!decl_get_declaration(di, elements, &count) && count == 3 && !memcmp(elements, decl->elements, 16) && elements[2].Stream == 255 && elements[2].Type == 17);
            DWORD code[2] = {0};UINT size = 0;
            CHECK(!vertex_shader_get_function(vi, NULL, &size) && size == 8);
            CHECK(!vertex_shader_get_function(vi, code, &size) && !memcmp(code, vs->code, 8));
            size = 8;CHECK(!pixel_shader_get_function(pi, code, &size) && !memcmp(code, ps->code, 8));
            Interface *owner = NULL;
            CHECK(!decl_get_device(di, &owner) && owner == &device->iface && device_release(owner) == 4);
            CHECK(!vertex_shader_get_device(vi, &owner) && owner == &device->iface && device_release(owner) == 4);
            CHECK(!pixel_shader_get_device(pi, &owner) && owner == &device->iface && device_release(owner) == 4);
            CHECK(decl_release(dg) == 1 && vertex_shader_release(vg) == 1 && pixel_shader_release(pg) == 1);
            CHECK(!decl_release(dq) && !vertex_shader_release(vq) && !pixel_shader_release(pq));
            CHECK(device->refcount == 1 && !device->child_parent_refs && !decl->refcount && !vs->refcount && !ps->refcount);
            CHECK(!device_set_vertex_declaration(&device->iface, di) && !device_set_vertex_shader(&device->iface, vi) && !device_set_pixel_shader(&device->iface, pi));
            CHECK(decl->binding_refs == 1 && vs->binding_refs == 1 && ps->binding_refs == 1);
            revivals += 3;
        }
        void *bad = di;int foreign_iid = 0;
        CHECK(decl_query_interface(di, &foreign_iid, &bad) == E_NOINTERFACE && !bad);
        CHECK(vertex_shader_query_interface(vi, &IID_IDirect3DPixelShader9, &bad) == E_NOINTERFACE && !bad);
        CHECK(pixel_shader_query_interface(pi, &IID_IDirect3DVertexShader9, &bad) == E_NOINTERFACE && !bad);
        CHECK(decl_query_interface(di, &IID_IUnknown, NULL) == E_POINTER && vertex_shader_query_interface(vi, &IID_IUnknown, NULL) == E_POINTER && pixel_shader_query_interface(pi, &IID_IUnknown, NULL) == E_POINTER);
        CHECK(device_get_vertex_declaration(&device->iface, NULL) == -1 && device_get_vertex_shader(&device->iface, NULL) == -1 && device_get_pixel_shader(&device->iface, NULL) == -1);
        D9Device foreign = {0};D9VertexDeclaration other_decl = {.iface = {&g_decl_vtbl}, .device = &foreign};D9Shader other_shader = {.iface.vertex = {&g_vertex_shader_vtbl}, .device = &foreign};
        CHECK(device_set_vertex_declaration(&device->iface, &other_decl.iface) == -1 && device_set_vertex_shader(&device->iface, &other_shader.iface.vertex) == -1);
        other_shader.iface.pixel.lpVtbl = &g_pixel_shader_vtbl;CHECK(device_set_pixel_shader(&device->iface, &other_shader.iface.pixel) == -1);
        UINT old_destroys = destroys;
        if (order == 0) {
            CHECK(!device_set_vertex_declaration(&device->iface, NULL) && !device_set_vertex_shader(&device->iface, NULL) && !device_set_pixel_shader(&device->iface, NULL));
            CHECK(destroys == old_destroys + 3 && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 3);
        } else if (order == 2) {
            Interface *dg = NULL, *vg = NULL, *pg = NULL;
            CHECK(!device_get_vertex_declaration(&device->iface, &dg) && !device_get_vertex_shader(&device->iface, &vg) && !device_get_pixel_shader(&device->iface, &pg));
            device_clear_bindings(device);
            CHECK(!decl->binding_refs && !vs->binding_refs && !ps->binding_refs && device->refcount == 4 && !device_has_reset_blockers(device) && destroys == old_destroys);
            CHECK(!device_set_vertex_declaration(&device->iface, dg) && !device_set_vertex_shader(&device->iface, vg) && !device_set_pixel_shader(&device->iface, pg));
            CHECK(!decl_release(dg) && !vertex_shader_release(vg) && !pixel_shader_release(pg));
            device_clear_bindings(device);CHECK(live == 1 && destroys == old_destroys + 3);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else {
            CHECK(!device_set_fvf(&device->iface, 2) && !device->vertex_declaration && device->fvf == 2 && destroys == old_destroys + 1);
            DWORD fvf = 0;CHECK(!device_get_fvf(&device->iface, &fvf) && fvf == 2);
            CHECK(decl_add_ref(new_declaration(device) ? &device->vertex_declarations->iface : NULL) == 2);
            D9VertexDeclaration *unbound = device->vertex_declarations;
            CHECK(decl_release(&unbound->iface) == 1);
            CHECK(device_release(&device->iface) == 1 && !decl_release(&unbound->iface) && !live && !parent.refs && destroys == old_destroys + 4);
        }
        profiles++;
    }
    printf("PASS actual D3D9 declaration/shader bindings: %u checks, %u final-owner profiles, %u public-zero QI revivals; exact public/device refs, metadata, getters, FVF, reset-style clear/rebind and complete storage teardown\n", checks - old_checks, profiles, revivals);
    return 0;
}
