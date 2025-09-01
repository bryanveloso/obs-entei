cmake_minimum_required(VERSION 3.28...3.31)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Enable C++17 for all platforms
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# Add OBS Studio's cmake/finders to module path for FindAsio.cmake (if it exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/cmake/finders")
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/cmake/finders")
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(OpenSSL REQUIRED)
# Try to find WebSocket++ and Asio like obs-websocket does
find_package(Websocketpp 0.8 QUIET)
find_package(Asio 1.12.1 QUIET)

# If not found, use FetchContent as fallback
if(NOT Websocketpp_FOUND OR NOT Asio_FOUND)
  include(FetchContent)

  if(NOT Asio_FOUND)
    FetchContent_Declare(asio GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git GIT_TAG asio-1-12-1)
  endif()

  if(NOT Websocketpp_FOUND)
    FetchContent_Declare(websocketpp GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git GIT_TAG 0.8.2)
  endif()

  # Handle Asio first
  if(NOT Asio_FOUND)
    FetchContent_MakeAvailable(asio)
  endif()

  # Handle WebSocket++ with patching
  if(NOT Websocketpp_FOUND)
    FetchContent_GetProperties(websocketpp)
    if(NOT websocketpp_POPULATED)
      # Allow deprecated FetchContent_Populate for manual patching
      cmake_policy(PUSH)
      cmake_policy(SET CMP0169 OLD)
      FetchContent_Populate(websocketpp)
      cmake_policy(POP)

      # Patch the CMake minimum version
      file(READ ${websocketpp_SOURCE_DIR}/CMakeLists.txt WEBSOCKETPP_CMAKE)
      string(
        REPLACE
        "cmake_minimum_required (VERSION 2.6)"
        "cmake_minimum_required (VERSION 3.5)"
        WEBSOCKETPP_CMAKE
        "${WEBSOCKETPP_CMAKE}"
      )
      file(WRITE ${websocketpp_SOURCE_DIR}/CMakeLists.txt "${WEBSOCKETPP_CMAKE}")

      # WebSocket++ is header-only, we don't need to build or install anything
      # Just make the headers available
      set(websocketpp_POPULATED TRUE CACHE BOOL "WebSocket++ populated" FORCE)
    endif()
  endif()
endif()
# Add include directories based on how deps were found
if(Websocketpp_FOUND)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Websocketpp::Websocketpp)
else()
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${websocketpp_SOURCE_DIR})
endif()

if(Asio_FOUND)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Asio::Asio)
else()
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${asio_SOURCE_DIR}/asio/include)
endif()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ASIO_STANDALONE)

# Windows-specific settings for WebSocket++
if(WIN32)
  target_compile_definitions(
    ${CMAKE_PROJECT_NAME}
    PRIVATE _WIN32_WINNT=0x0603 _WEBSOCKETPP_CPP11_STL_ NOMINMAX _CRT_SECURE_NO_WARNINGS
  )
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /wd4267)
  # Link Windows socket libraries required by WebSocket++
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 mswsock)
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

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.c src/websocket-client.cpp src/cJSON.c src/entei-tools.cpp src/entei-dialog.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
