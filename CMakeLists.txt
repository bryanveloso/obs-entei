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
# Try to find Websocketpp, fallback to FetchContent if target not available
find_package(Websocketpp 0.8.2 QUIET)
if(NOT TARGET Websocketpp::Websocketpp)
  include(FetchContent)
  FetchContent_Declare(websocketpp GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git GIT_TAG 0.8.2)
  # Populate content but don't add to install
  FetchContent_Populate(websocketpp)
  if(NOT websocketpp_POPULATED)
    FetchContent_Populate(websocketpp)
  endif()
  add_library(Websocketpp::Websocketpp INTERFACE IMPORTED)
  target_include_directories(Websocketpp::Websocketpp INTERFACE ${websocketpp_SOURCE_DIR})
  # Disable warnings for WebSocket++ headers
  target_compile_options(
    Websocketpp::Websocketpp
    INTERFACE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<CXX_COMPILER_ID:MSVC>:/w>
  )
endif()
# Try to find Asio, fallback to FetchContent if target not available
find_package(Asio QUIET)
if(NOT TARGET Asio::Asio)
  include(FetchContent)
  FetchContent_Declare(asio GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git GIT_TAG asio-1-28-0)
  # Populate content but don't add to install
  FetchContent_Populate(asio)
  if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
  endif()
  add_library(Asio::Asio INTERFACE IMPORTED)
  target_include_directories(Asio::Asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
  # Disable warnings for Asio headers
  target_compile_options(
    Asio::Asio
    INTERFACE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<CXX_COMPILER_ID:MSVC>:/w>
  )
endif()
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OpenSSL::SSL OpenSSL::Crypto Websocketpp::Websocketpp Asio::Asio
)
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
  PRIVATE
    src/plugin-main.c
    src/websocket-client.cpp
    src/phoenix-protocol.c
    src/cJSON.c
    src/entei-tools.cpp
    src/entei-dialog.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
