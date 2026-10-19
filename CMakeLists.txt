cmake_minimum_required(VERSION 3.25)
project(MacroLegitAssist VERSION 1.0.0)

find_package(Geode REQUIRED)

include_directories(${Geode_INCLUDE_DIRS})

add_geode_mod(macro_legit_assist
    main.cpp
)
