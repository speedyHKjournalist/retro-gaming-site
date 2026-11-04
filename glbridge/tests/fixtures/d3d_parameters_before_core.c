/* Actual shared function from site203054a,preprocessed separately with SDK8/9. */
static inline void parameters_before_core(
    D3DPRESENT_PARAMETERS* effective, const D3DPRESENT_PARAMETERS* requested,
    uint32_t width, uint32_t height, uint32_t format, HWND window)
{
    *effective = *requested;
    effective->BackBufferWidth = width;
    effective->BackBufferHeight = height;
    effective->BackBufferFormat = format;
    effective->BackBufferCount = requested->BackBufferCount ? requested->BackBufferCount : 1;
    effective->hDeviceWindow = window;
}
