cmake_minimum_required(VERSION 3.21)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/qpm_defines.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/qpm_defines.cmake")
endif()

if(NOT DEFINED MOD_ID)
  set(MOD_ID "twitch-bsr")
endif()
if(NOT DEFINED MOD_VERSION)
  set(MOD_VERSION "0.1.0")
endif()
if(NOT DEFINED COMPILE_ID)
  set(COMPILE_ID "twitch-bsr")
endif()
if(NOT DEFINED EXTERN_DIR)
  set(EXTERN_DIR "${CMAKE_CURRENT_LIST_DIR}/extern")
endif()
if(NOT DEFINED SHARED_DIR)
  set(SHARED_DIR "${CMAKE_CURRENT_LIST_DIR}/shared")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(${COMPILE_ID})

add_compile_options(-frtti -fexceptions -fvisibility=default -O2)
add_compile_definitions(VERSION="${MOD_VERSION}" MOD_ID="${MOD_ID}")

file(GLOB_RECURSE cpp_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
add_library(${COMPILE_ID} SHARED ${cpp_files})
target_include_directories(${COMPILE_ID} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(${COMPILE_ID} PRIVATE log z)

include("${CMAKE_CURRENT_LIST_DIR}/extern.cmake")
