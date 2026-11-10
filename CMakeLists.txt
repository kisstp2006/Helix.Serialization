cmake_minimum_required(VERSION 3.28)
project(Helix.Serialization LANGUAGES CXX)

if(NOT COMMAND helix_add_library)
    include("${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.CMake/Helix.cmake")
endif()

helix_require(Core)
helix_add_library(helix_serialization ALIAS Serialization PUBLIC_DEPS Helix::Core)

helix_add_test_suite(helix_serialization_tests DEPS Helix::Serialization)
helix_add_benchmark_suite(helix_serialization_bench DEPS Helix::Serialization)
helix_add_fuzz_suite(helix_serialization_fuzz DEPS Helix::Serialization ROUNDS 2000)
