include(GoogleTest)

add_executable(tva_unit_tests
  unit/sha1_test.cpp
)
target_link_libraries(tva_unit_tests PRIVATE tva_core GTest::gtest_main tva_warnings)
gtest_discover_tests(tva_unit_tests DISCOVERY_MODE PRE_TEST)
