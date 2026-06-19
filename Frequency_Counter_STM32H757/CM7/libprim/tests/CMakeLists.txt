# libprim host test suite (software path, no DMA2D). Run: ctest in build dir.
# These are assert-based smoke/invariant tests. PNG pixel-diff tests are added
# on top once a host reference render exists (post bring-up).

foreach(t test_fill test_shapes test_text)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE prim)
    set_strict_warnings(${t})
    add_test(NAME prim::${t} COMMAND ${t})
endforeach()
