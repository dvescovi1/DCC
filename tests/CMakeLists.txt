include(GoogleTest)

file(GLOB_RECURSE SRC *.cpp)
add_executable(DCCTests ${SRC})

sanitize(address,undefined)

target_common_warnings(DCCTests PRIVATE)

target_compile_options(DCC INTERFACE -Werror;-Wno-error=deprecated-declarations)

cpmaddpackage(URI "gh:google/googletest#main" OPTIONS "INSTALL_GTEST OFF")

target_link_libraries(DCCTests PRIVATE DCC::DCC GTest::gtest_main GTest::gmock)

gtest_discover_tests(DCCTests)
