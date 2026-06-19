# libui host test suite (links libprim). Run: ctest in build dir.

add_executable(test_pill test_pill.c)
target_link_libraries(test_pill PRIVATE ui prim)
set_strict_warnings(test_pill)
add_test(NAME ui::test_pill COMMAND test_pill)
