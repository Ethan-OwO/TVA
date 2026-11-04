include(GoogleTest)

add_executable(tva_unit_tests
  unit/commit_test.cpp
  unit/config_test.cpp
  unit/ignore_test.cpp
  unit/materialize_test.cpp
  unit/object_database_test.cpp
  unit/object_test.cpp
  unit/refs_test.cpp
  unit/repository_test.cpp
  unit/rev_parse_test.cpp
  unit/sha1_test.cpp
  unit/tree_diff_test.cpp
  unit/tree_test.cpp
  unit/zlib_codec_test.cpp
)
target_link_libraries(tva_unit_tests PRIVATE tva_core GTest::gtest_main tva_warnings)
gtest_discover_tests(tva_unit_tests DISCOVERY_MODE PRE_TEST)

# Integration tests: bash scripts that drive the tva binary and compare
# against real git. TVA_BIN tells them which binary to run.
find_program(BASH_PROGRAM bash REQUIRED)
find_program(GIT_PROGRAM git REQUIRED)
foreach(name hash_object write_tree commit_log fsck restore)
  add_test(NAME integration.${name}
           COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_${name}.sh)
  set_tests_properties(integration.${name} PROPERTIES ENVIRONMENT "TVA_BIN=$<TARGET_FILE:tva>")
endforeach()
