cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# Define plugin name and version
target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE PLUGIN_NAME="${CMAKE_PROJECT_NAME}" PLUGIN_VERSION="${_version}"
)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(Threads REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Threads::Threads)

# Platform specific networking libraries
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
endif()

# Find Jansson for JSON parsing (optional)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(JANSSON jansson)
  if(JANSSON_FOUND)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${JANSSON_INCLUDE_DIRS})
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${JANSSON_LIBRARIES})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE JANSSON_FOUND)
  endif()
endif()

# If pkg-config didn't find it, try to find it manually
if(NOT JANSSON_FOUND)
  find_path(JANSSON_INCLUDE_DIR NAMES jansson.h)
  find_library(JANSSON_LIBRARY NAMES jansson)

  if(JANSSON_INCLUDE_DIR AND JANSSON_LIBRARY)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${JANSSON_INCLUDE_DIR})
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${JANSSON_LIBRARY})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE JANSSON_FOUND)
    set(JANSSON_FOUND TRUE)
  else()
    message(STATUS "Jansson library not found. Using built-in JSON parser.")
  endif()
endif()

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/websocket-client.c src/entei-caption-provider.c)

# Add compiler warnings for better code quality
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wformat=2
      -Wno-unused-parameter
      -Wstrict-prototypes
      -Wold-style-definition
      -Werror=pointer-arith
      -Werror=implicit-function-declaration
      -Werror=format-security
  )
elseif(MSVC)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      /W4
      /wd4100 # unreferenced formal parameter
      /wd4996 # deprecated functions
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Install locale files
install(DIRECTORY data/locale DESTINATION data/obs-plugins/${CMAKE_PROJECT_NAME} FILES_MATCHING PATTERN "*.ini")
