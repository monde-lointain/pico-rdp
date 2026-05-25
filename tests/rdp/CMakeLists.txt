# RDP renderer pitfall micro-tests (Stream A.5): RDRAM XOR addressing, hidden-bit
# machinery, irand LCG, and bit-exact-neutrality of boundable hidden-RDRAM.
#
# rdram.c is NOT a standalone library source — it is #included into a single TU
# under N64VIDEO_C. These tests therefore compile dedicated test TUs
# (rdram_test_tu.cc / rdram_bounded_test_tu.cc) that supply a minimal config
# harness and #include rdram.c. Test code is gtest/C++ (not orthodox-enforced),
# but MUST carry -fno-strict-aliasing since it compiles the aliasing RDRAM code.

include(GoogleTest)

set(RDP_SRC_DIR ${PROJECT_SOURCE_DIR}/src/rdp)

# Shared test TUs that #include rdram.c (full + bounded hidden modes).
add_library(rdp_rdram_test_tus OBJECT
    rdram_test_tu.cc
    rdram_bounded_test_tu.cc
)
target_include_directories(rdp_rdram_test_tus PUBLIC
    ${RDP_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rdp_rdram_test_tus PRIVATE -fno-strict-aliasing)
endif()

# rdram_xor_test: byte-swizzle for BYTE_ADDR_XOR=3 / WORD_ADDR_XOR=1.
add_executable(rdram_xor_test rdram_xor_test.cc)
target_link_libraries(rdram_xor_test PRIVATE rdp_rdram_test_tus gtest_main)
target_include_directories(rdram_xor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(rdram_xor_test)

# rdram_hidden_test: pair-write sequences vs reference model (flip 0/1, delayed).
add_executable(rdram_hidden_test rdram_hidden_test.cc)
target_link_libraries(rdram_hidden_test PRIVATE rdp_rdram_test_tus gtest_main)
target_include_directories(rdram_hidden_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(rdram_hidden_test)

# irand_test: LCG sequence from seed 3 matches the formula.
add_executable(irand_test irand_test.cc)
target_link_libraries(irand_test PRIVATE gtest_main)
target_include_directories(irand_test PRIVATE ${RDP_SRC_DIR})
gtest_discover_tests(irand_test)

# hidden_bounded_equiv_test: bounded mode == full mode (byte-identical).
add_executable(hidden_bounded_equiv_test hidden_bounded_equiv_test.cc)
target_link_libraries(hidden_bounded_equiv_test PRIVATE rdp_rdram_test_tus gtest_main)
target_include_directories(hidden_bounded_equiv_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(hidden_bounded_equiv_test)
