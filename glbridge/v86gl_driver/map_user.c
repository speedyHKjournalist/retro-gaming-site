/* Build this single boundary with Clang's i686 MS SEH support. The catch uses
 * ntoskrnl's _except_handler3, with no C runtime. User-mode MDL mapping can
 * raise on allocation failure even with BugCheckOnFailure=FALSE. */
void *v86gl_try_map_user(void *mdl, void *(*map)(void *))
{
    __try { return map(mdl); }
    __except(1) { return 0; }
}
