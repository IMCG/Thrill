################################################################################
# benchmarks/net/CMakeLists.txt
#
# CMake file for Thrill benchmark programs.
################################################################################

thrill_build_prog(broadcast)
thrill_build_prog(prefixsum)
thrill_build_prog(net_benchmark)

thrill_test_single(net_benchmark_ping_pong_local3 "THRILL_LOCAL=3"
  net_benchmark ping_pong 10)

thrill_test_single(net_benchmark_ping_pong_local4 "THRILL_LOCAL=4"
  net_benchmark ping_pong 10)

thrill_test_single(net_benchmark_bandwidth_local3 "THRILL_LOCAL=3"
  net_benchmark bandwidth 10)

thrill_test_single(net_benchmark_bandwidth_local4 "THRILL_LOCAL=4"
  net_benchmark bandwidth 10)

################################################################################
