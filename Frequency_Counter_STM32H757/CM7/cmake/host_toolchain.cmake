# host_toolchain.cmake — native build for tests/examples (PNG output).
# The system gcc/clang is sufficient; this file exists for symmetry and to
# define PRIM_HOST_BUILD so glow/screen caches drop the .sdram section.

add_compile_definitions(PRIM_HOST_BUILD=1)
