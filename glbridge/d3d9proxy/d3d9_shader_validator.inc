/* The shader validator is a CPU-only runtime service, independent of the
 * rendering device. Preserve XP's complete bytecode diagnostics by using its
 * installed runtime rather than maintaining a second shader compiler here.
 * Microsoft identifies this export as internal; the pinned Wine device tests
 * exercise its actual Begin/Instruction/End interface and diagnostics.
 * https://learn.microsoft.com/windows/win32/direct3d9/direct3dshadervalidatorcreate9
 */
static HMODULE g_native_shader_validator_module;

void *WINAPI Direct3DShaderValidatorCreate9(void)
{
    typedef void *(WINAPI *CreateValidator)(void);
    HMODULE module = (HMODULE)InterlockedCompareExchangePointer(
            (PVOID volatile *)&g_native_shader_validator_module, NULL, NULL);
    CreateValidator create;
    union { FARPROC generic; CreateValidator validator; } entry;
    if (!module) {
        WCHAR path[MAX_PATH];
        static const WCHAR suffix[] = L"\\d3d9.dll";
        UINT count = GetSystemDirectoryW(path, MAX_PATH);
        if (!count || count >= MAX_PATH
                || count > MAX_PATH - sizeof(suffix) / sizeof(*suffix))
            return NULL;
        CopyMemory(path + count, suffix, sizeof(suffix));
        module = LoadLibraryW(path);
        if (!module) return NULL;
        entry.generic = GetProcAddress(module, "Direct3DShaderValidatorCreate9");
        create = entry.validator;
        /* A system-wide proxy installation must not recurse into itself. */
        if (module == g_module_instance || !create
                || create == Direct3DShaderValidatorCreate9) {
            FreeLibrary(module);
            return NULL;
        }
        HMODULE previous = (HMODULE)InterlockedCompareExchangePointer(
                (PVOID volatile *)&g_native_shader_validator_module, module, NULL);
        if (previous) { FreeLibrary(module);module = previous; }
        /* Retain one runtime module reference for the process: returned
         * native COM vtables/callback strings can outlive this DLL call. */
    }
    entry.generic = GetProcAddress(module, "Direct3DShaderValidatorCreate9");
    create = entry.validator;
    return create ? create() : NULL;
}
