/* Native D3D7 snapshot and Wine Windows stateblock tables use bit 7. */
static void light_enable_query_checks(void)
{
    BOOL enabled=(BOOL)0xccccccccu;check(FAILED(CALL(GetLightEnable)(device,0,&enabled))&&(DWORD)enabled==0xccccccccu,"unknown enable query atomic");check(FAILED(CALL(GetLightEnable)(device,0,NULL)),"null enable query rejected");
    Light value;ZeroMemory(&value,sizeof(value));value.Type=D3DLIGHT_DIRECTIONAL;value.Direction.z=1;value.Diffuse.r=value.Diffuse.g=value.Diffuse.b=1;
    check(SUCCEEDED(CALL(SetLight)(device,6,&value)),"query create light");check(SUCCEEDED(CALL(GetLightEnable)(device,6,&enabled))&&enabled==0,"created light disabled query");check(SUCCEEDED(CALL(LightEnable)(device,6,7)),"query enable noncanonical BOOL");check(SUCCEEDED(CALL(GetLightEnable)(device,6,&enabled))&&enabled==128,"native enabled BOOL is bit seven");check(SUCCEEDED(CALL(LightEnable)(device,6,FALSE)),"query disable light");check(SUCCEEDED(CALL(GetLightEnable)(device,6,&enabled))&&enabled==FALSE,"disabled query zero");
    report("PASS shared light-enable queries: unknown/null failure, untouched output, disabled zero, enabled 128 and noncanonical BOOL input");
}
