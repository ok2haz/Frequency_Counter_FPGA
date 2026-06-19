# libprim host examples (PPM output, no external deps).

add_executable(prim_ex_hello_rect 01_hello_rect.c)
target_link_libraries(prim_ex_hello_rect PRIVATE prim)
set_strict_warnings(prim_ex_hello_rect)
