static void d3d8_declaration_only_checks(void)
{
    DWORD baseline, recorded, a, b, c, got, size, words[16];
    const DWORD declaration[] = { D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3), D3DVSD_STREAM(1), D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL2, D3DVSDT_FLOAT3), D3DVSD_END() };
    check(SUCCEEDED(CALL(CreateStateBlock)(device, D3DSBT_ALL, &baseline)), "declaration-only baseline");
    check(SUCCEEDED(CALL(CreateVertexShader)(device, declaration, NULL, &a, 0)) && SUCCEEDED(CALL(CreateVertexShader)(device, declaration, NULL, &b, 0)), "CreateVertexShader NULL function creates fixed declarations");
    size = 0;
    check(SUCCEEDED(CALL(GetVertexShaderDeclaration)(device, a, NULL, &size)) && size == sizeof(declaration), "fixed declaration size");
    check(SUCCEEDED(CALL(GetVertexShaderDeclaration)(device, a, words, &size)), "fixed declaration query");
    for (unsigned i = 0; i < sizeof(declaration) / 4; ++i)
        check(words[i] == declaration[i], "fixed declaration preserves SDK tokens");
    size = 0xcccccccc;
    check(SUCCEEDED(CALL(GetVertexShaderFunction)(device, a, NULL, &size)) && size == 0, "fixed declaration has no shader code");
    words[0] = 0xcccccccc;
    size = sizeof(words);
    check(SUCCEEDED(CALL(GetVertexShaderFunction)(device, a, words, &size)) && size == 0 && words[0] == 0xcccccccc, "empty fixed function preserves caller data");
    check(SUCCEEDED(CALL(SetVertexShader)(device, a)) && SUCCEEDED(CALL(GetVertexShader)(device, &got)) && got == a, "fixed declaration binds under public shader handle");
    check(SUCCEEDED(CALL(BeginStateBlock)(device)) && SUCCEEDED(CALL(SetVertexShader)(device, b)) && SUCCEEDED(CALL(GetVertexShader)(device, &got)) && got == a, "fixed declaration recording leaves live binding");
    check(SUCCEEDED(CALL(EndStateBlock)(device, &recorded)) && SUCCEEDED(CALL(DeleteVertexShader)(device, b)), "block retains deleted fixed declaration");
    check(FAILED(CALL(SetVertexShader)(device, b)) && FAILED(CALL(DeleteVertexShader)(device, b)), "deleted fixed public handle expires");
    size = 0xcccccccc;
    check(FAILED(CALL(GetVertexShaderDeclaration)(device, b, NULL, &size)) && size == 0xcccccccc, "deleted fixed declaration query preserves size");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device, recorded)) && SUCCEEDED(CALL(GetVertexShader)(device, &got)) && got == b, "apply retained fixed declaration");
    check(SUCCEEDED(CALL(CaptureStateBlock)(device, recorded)) && SUCCEEDED(CALL(CaptureStateBlock)(device, recorded)) && SUCCEEDED(CALL(DeleteStateBlock)(device, recorded)), "fixed declaration repeated Capture and block deletion");
    check(SUCCEEDED(CALL(DeleteVertexShader)(device, a)) && SUCCEEDED(CALL(SetVertexShader)(device, 0)), "last fixed binding releases declaration");
    const DWORD constants[] = { D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_CONST(3, 1), 0x3e800000, 0x3f000000, 0x3f400000, 0x3f800000, D3DVSD_END() };
    check(SUCCEEDED(CALL(CreateVertexShader)(device, constants, NULL, &c, 0)) && SUCCEEDED(CALL(SetVertexShader)(device, c)) && SUCCEEDED(CALL(DeleteVertexShader)(device, c)), "fixed declaration constants decode and bind");
    const DWORD bad_normal[] = { D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT2), D3DVSD_END() };
    got = 0xcccccccc;
    check(FAILED(CALL(CreateVertexShader)(device, bad_normal, NULL, &got, 0)) && got == 0, "fixed function requires FLOAT3 primary normal");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device, baseline)) && SUCCEEDED(CALL(DeleteStateBlock)(device, baseline)), "restore fixed declaration baseline");
    report("PASS D3D8 declaration-only shaders: NULL-function creation, exact declaration/empty code queries, deferred retained lifetime, constants and validation");
}
