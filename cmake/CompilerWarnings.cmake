# Shared warning flags, applied explicitly per-target (via pf_compiler_warnings)
# rather than globally, so third-party code pulled in via CPM isn't held to them.

add_library(pf_compiler_warnings INTERFACE)

if(MSVC)
  target_compile_options(pf_compiler_warnings INTERFACE /W4 /permissive-)
  if(PF_WARNINGS_AS_ERRORS)
    target_compile_options(pf_compiler_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(pf_compiler_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  )
  if(PF_WARNINGS_AS_ERRORS)
    target_compile_options(pf_compiler_warnings INTERFACE -Werror)
  endif()
endif()
