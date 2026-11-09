cmake_minimum_required(VERSION 3.20)
project(CreatureTerrainTiltFix LANGUAGES CXX)

add_library(CreatureTerrainTiltFix SHARED
    main.cpp
    shared/SafeWrite/SafeWrite.cpp
)
target_compile_features(CreatureTerrainTiltFix PRIVATE cxx_std_20)
target_include_directories(CreatureTerrainTiltFix PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(CreatureTerrainTiltFix PRIVATE /W4 /EHsc /FIcompat.hpp)
    target_link_libraries(CreatureTerrainTiltFix PRIVATE user32)
endif()

set_target_properties(CreatureTerrainTiltFix PROPERTIES
    OUTPUT_NAME CreatureTerrainTiltFix
    PREFIX ""
    SUFFIX ".dll"
)
