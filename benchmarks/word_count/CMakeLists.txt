################################################################################
# benchmarks/word_count/CMakeLists.txt
#
# CMake file for Thrill benchmark programs.
################################################################################

thrill_build_prog(word_count)
thrill_build_prog(line_count)

file(COPY "${CMAKE_SOURCE_DIR}/tests/inputs/wordcount.in"
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

file(COPY "${CMAKE_SOURCE_DIR}/tests/inputs/headwords"
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

################################################################################
