#include "sigil/gccjit_backend.hpp"

#if SIGIL_HAVE_GCCJIT
#include <libgccjit.h>
#endif

namespace sigil {

GccJitStatus gccjit_status() {
#if SIGIL_HAVE_GCCJIT
  gcc_jit_context* context = gcc_jit_context_acquire();
  if (!context) {
    return {false, "libgccjit was found at build time, but gcc_jit_context_acquire failed"};
  }
  gcc_jit_context_release(context);
  return {true, "libgccjit is available and can allocate a JIT context"};
#else
  return {false, "this binary was built without libgccjit; install libgccjit and reconfigure CMake"};
#endif
}

}  // namespace sigil
