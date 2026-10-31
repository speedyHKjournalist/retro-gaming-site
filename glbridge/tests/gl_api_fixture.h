/* Small host ABI fixtures isolate entry bodies from the Win32 call guard.
 * gl_thread_guard_abi_test.js exercises the actual guard and TLS scope. */
#ifndef V86GL_API_CALL
#define V86GL_API_CALL() ((void)0)
#endif
#ifndef current_buffer_scope
#define current_buffer_scope() 0u
#endif
