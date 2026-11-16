// link.c: what a build links in beside the core, compiled for each link
// with its defines. The core's own objects are the same for every app (a
// prebuilt archive, or one built once into the build cache); what differs
// from build to build is named here (vm.h's ak_link_app, ak_link_ffi):
//
//   AK_APP_COMPILED  an app is compiled in (lang/cgen's asuka_app_module);
//                    without, the build is a player, which loads bytecode
//   AK_FFI           the glue of the app's extern declarations (lang/cffi's
//                    asuka_ffi_register)
//
// Naming them keeps their objects in a static library's link.
#include <stddef.h>

typedef struct ak_module ak_module;

#if defined(AK_APP_COMPILED)
ak_module *asuka_app_module(void);
ak_module *(*const ak_link_app)(void) = asuka_app_module;
#else
ak_module *(*const ak_link_app)(void) = NULL;
#endif

#if defined(AK_FFI)
void asuka_ffi_register(void);
void (*const ak_link_ffi)(void) = asuka_ffi_register;
#else
void (*const ak_link_ffi)(void) = NULL;
#endif
