# Third-party dependencies, fetched at configure time. Tags are pinned.
# GIT_REPOSITORY (rather than URL tarballs) keeps this working behind proxies
# that only allow git traffic.
include(FetchContent)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.5.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CLI11)

if(TVA_BUILD_TESTS)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()
