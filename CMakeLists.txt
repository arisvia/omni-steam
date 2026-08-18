cmake_minimum_required(VERSION 3.20)
project(OmniSteam VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Project Options
option(ENABLE_LOGGING "Enable debug logging" ON)
option(BUILD_STATIC_DEPS "Statically link dependencies" ON)
option(BUILD_TESTS "Build verification test suite" ON)

# Fetch Dependencies
include(FetchContent)

# 1. tomlplusplus
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# 2. spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# 3. funchook (Cross-platform x86/x64/ARM Hook engine)
FetchContent_Declare(
    funchook
    GIT_REPOSITORY https://github.com/kubo/funchook.git
    GIT_TAG        v1.1.3
)
set(FUNCHOOK_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(FUNCHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(funchook)

# 4. Lua (embedded scripting engine)
find_package(Lua REQUIRED)

# Platform specific dependencies
if(WIN32)
    add_compile_definitions(OMNI_PLATFORM_WINDOWS)
    set(PLATFORM_LIBS winhttp bcrypt ws2_32)
elseif(APPLE)
    add_compile_definitions(OMNI_PLATFORM_MACOS)
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBS OpenSSL::Crypto OpenSSL::SSL CURL::libcurl Threads::Threads ${CMAKE_DL_LIBS})
else()
    add_compile_definitions(OMNI_PLATFORM_LINUX)
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBS OpenSSL::Crypto OpenSSL::SSL CURL::libcurl Threads::Threads ${CMAKE_DL_LIBS})
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
)

# ---------------------------------------------------------------------------
# OmniPlatform Library
# ---------------------------------------------------------------------------
set(OMNIPLATFORM_COMMON_SOURCES
    src/Platform/Common/Log.cpp
    src/Platform/Common/Encoding.cpp
    src/Platform/Common/Numbers.cpp
)

if(WIN32)
    set(OMNIPLATFORM_SOURCES
        ${OMNIPLATFORM_COMMON_SOURCES}
        src/Platform/Windows/Detour.cpp
        src/Platform/Windows/DynamicLibrary.cpp
        src/Platform/Windows/Memory.cpp
        src/Platform/Windows/BinaryParser.cpp
        src/Platform/Windows/ByteSearch.cpp
        src/Platform/Windows/DirectoryWatch.cpp
        src/Platform/Windows/Http.cpp
        src/Platform/Windows/Hash.cpp
        src/Platform/Windows/Process.cpp
        src/Platform/Windows/Thread.cpp
        src/Platform/Windows/CredentialStore.cpp
    )
elseif(APPLE)
    set(OMNIPLATFORM_SOURCES
        ${OMNIPLATFORM_COMMON_SOURCES}
        src/Platform/Darwin/Detour.cpp
        src/Platform/Darwin/DynamicLibrary.cpp
        src/Platform/Darwin/Memory.cpp
        src/Platform/Darwin/BinaryParser.cpp
        src/Platform/Darwin/ByteSearch.cpp
        src/Platform/Darwin/DirectoryWatch.cpp
        src/Platform/Darwin/Http.cpp
        src/Platform/Darwin/Hash.cpp
        src/Platform/Darwin/Process.cpp
        src/Platform/Darwin/Thread.cpp
        src/Platform/Darwin/CredentialStore.cpp
    )
else()
    set(OMNIPLATFORM_SOURCES
        ${OMNIPLATFORM_COMMON_SOURCES}
        src/Platform/Linux/Detour.cpp
        src/Platform/Linux/DynamicLibrary.cpp
        src/Platform/Linux/Memory.cpp
        src/Platform/Linux/BinaryParser.cpp
        src/Platform/Linux/ByteSearch.cpp
        src/Platform/Linux/DirectoryWatch.cpp
        src/Platform/Linux/Http.cpp
        src/Platform/Linux/Hash.cpp
        src/Platform/Linux/Process.cpp
        src/Platform/Linux/Thread.cpp
        src/Platform/Linux/CredentialStore.cpp
    )
endif()

add_library(OmniPlatform STATIC ${OMNIPLATFORM_SOURCES})
target_link_libraries(OmniPlatform PUBLIC
    funchook
    spdlog::spdlog
    ${PLATFORM_LIBS}
)

# ---------------------------------------------------------------------------
# Core OmniSteam Target (DLL / Shared Library)
# ---------------------------------------------------------------------------
set(OMNISTEAM_SOURCES
    src/main.cpp
    src/Utils/Config/Config.cpp
    src/Utils/Config/LuaConfig.cpp
    src/Utils/Config/ConfigFileWatcher.cpp
    src/Utils/Config/LuaFileWatcher.cpp
    src/Utils/Metadata/ManifestClient.cpp
    src/Utils/Metadata/PatternLoader.cpp
    src/Utils/Metadata/IPCLoader.cpp
    src/Hook/HookManager.cpp
    src/Hook/Hooks_IPC.cpp
    src/Hook/Hooks_Decryption.cpp
    src/Hook/Hooks_Manifest.cpp
    src/Hook/Hooks_Package.cpp
    src/Hook/Hooks_KeyValues.cpp
    src/Hook/Hooks_CallBack.cpp
    src/Hook/Hooks_SteamUI.cpp
)

add_library(OmniSteam SHARED ${OMNISTEAM_SOURCES})
set_target_properties(OmniSteam PROPERTIES
    OUTPUT_NAME "omnisteam"
    PREFIX "lib"
)

target_link_libraries(OmniSteam PRIVATE
    OmniPlatform
    tomlplusplus::tomlplusplus
    ${LUA_LIBRARIES}
    spdlog::spdlog
    ${PLATFORM_LIBS}
)

if(ENABLE_LOGGING)
    target_compile_definitions(OmniSteam PRIVATE OMNI_LOGGING_ENABLED)
    target_compile_definitions(OmniPlatform PRIVATE OMNI_LOGGING_ENABLED)
endif()

# Windows proxy DLLs for seamless launch
if(WIN32)
    add_library(dwmapi SHARED src/Platform/Windows/Proxy/dwmapi.cpp)
    target_link_libraries(dwmapi PRIVATE OmniSteam)
endif()

# ---------------------------------------------------------------------------
# OmniSteam Manager Application (Decoupled Dashboard)
# ---------------------------------------------------------------------------
set(MANAGER_SOURCES
    apps/manager/src/main.cpp
    apps/manager/src/SteamApi.cpp
    apps/manager/src/ScriptManager.cpp
    apps/manager/src/WebServer.cpp
    apps/manager/src/SavePathResolver.cpp
    apps/manager/src/WebDavClient.cpp
    apps/manager/src/CloudSaveManager.cpp
)
add_executable(omnisteam-manager ${MANAGER_SOURCES})
target_include_directories(omnisteam-manager PRIVATE apps/manager/src)
target_link_libraries(omnisteam-manager PRIVATE
    OmniPlatform
    spdlog::spdlog
    ${PLATFORM_LIBS}
)

# ---------------------------------------------------------------------------
# Test Suite
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_platform tests/test_platform.cpp)
    target_link_libraries(test_platform PRIVATE OmniPlatform)
    add_test(NAME PlatformTests COMMAND test_platform)
    add_executable(test_phase2 tests/test_phase2.cpp src/Utils/Metadata/PatternLoader.cpp src/Utils/Metadata/ManifestClient.cpp src/Utils/Config/Config.cpp src/Utils/Config/LuaConfig.cpp)
    target_link_libraries(test_phase2 PRIVATE OmniPlatform tomlplusplus::tomlplusplus ${LUA_LIBRARIES})
    add_executable(test_phase3 tests/test_phase3.cpp apps/manager/src/ScriptManager.cpp)
    target_include_directories(test_phase3 PRIVATE apps/manager/src)
    target_link_libraries(test_phase3 PRIVATE OmniPlatform)
    add_test(NAME Phase3Tests COMMAND test_phase3)
    add_executable(test_phase4 tests/test_phase4.cpp apps/manager/src/SavePathResolver.cpp apps/manager/src/WebDavClient.cpp apps/manager/src/CloudSaveManager.cpp)
    target_include_directories(test_phase4 PRIVATE apps/manager/src)
    target_link_libraries(test_phase4 PRIVATE OmniPlatform CURL::libcurl)
    add_test(NAME Phase4Tests COMMAND test_phase4)
    add_test(NAME Phase2Tests COMMAND test_phase2)
    add_executable(test_phase5 tests/test_phase5.cpp)
    target_link_libraries(test_phase5 PRIVATE OmniPlatform)
    add_test(NAME Phase5Tests COMMAND test_phase5)
endif()

# ---------------------------------------------------------------------------
# CPack Multi-Platform Packaging
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "OmniSteam")
set(CPACK_PACKAGE_VENDOR "arisvia")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Universal Cross-Platform Steam Enhancement & Unlocker")
set(CPACK_GENERATOR "TGZ;ZIP")
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "arisvia")
endif()
include(CPack)
