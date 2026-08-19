cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
if(POLICY CMP0194)
    cmake_policy(SET CMP0194 NEW)
endif()

project(OmniSteam VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
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

# 2. spdlog (v1.15.3 bundles fmt 11 with full AppleClang Xcode 15/16 C++20 consteval support)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
)
FetchContent_GetProperties(spdlog)
if(NOT spdlog_POPULATED)
    FetchContent_Populate(spdlog)
    add_library(spdlog INTERFACE)
    add_library(spdlog::spdlog ALIAS spdlog)
    add_library(spdlog::spdlog_header_only ALIAS spdlog)
    target_include_directories(spdlog INTERFACE "${spdlog_SOURCE_DIR}/include")
endif()
# 3. Modern Capstone 5.0.1 (CMake 3.31+ / 4.x compatible, supports x86/x64/ARM64)
FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
    GIT_TAG        5.0.1
)
set(CAPSTONE_BUILD_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(capstone)

# 4. funchook (Build directly with Capstone 5.0 without legacy Capstone 4.0.2 download)
FetchContent_Declare(
    funchook_src
    GIT_REPOSITORY https://github.com/kubo/funchook.git
    GIT_TAG        v1.1.3
)
FetchContent_GetProperties(funchook_src)
if(NOT funchook_src_POPULATED)
    FetchContent_Populate(funchook_src)
    
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/funchook_include")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/funchook_config.h" "${CMAKE_CURRENT_BINARY_DIR}/funchook_include/config.h" COPYONLY)

    set(FUNCHOOK_SOURCES
        "${funchook_src_SOURCE_DIR}/src/funchook.c"
        "${funchook_src_SOURCE_DIR}/src/disasm_capstone.c"
    )
    if(WIN32)
        list(APPEND FUNCHOOK_SOURCES "${funchook_src_SOURCE_DIR}/src/funchook_windows.c")
    else()
        list(APPEND FUNCHOOK_SOURCES "${funchook_src_SOURCE_DIR}/src/funchook_unix.c")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        list(APPEND FUNCHOOK_SOURCES "${funchook_src_SOURCE_DIR}/src/funchook_arm64.c")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
        list(APPEND FUNCHOOK_SOURCES "${funchook_src_SOURCE_DIR}/src/funchook_arm.c")
    else()
        list(APPEND FUNCHOOK_SOURCES "${funchook_src_SOURCE_DIR}/src/funchook_x86.c")
    endif()

    add_library(funchook STATIC ${FUNCHOOK_SOURCES})
    target_include_directories(funchook PUBLIC 
        "${funchook_src_SOURCE_DIR}/include"
        "${capstone_SOURCE_DIR}/include"
    )
    target_include_directories(funchook PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/funchook_include")
    target_link_libraries(funchook PRIVATE capstone)
endif()
# 5. Lua 5.4 (Clean single-compilation unit without duplicate onelua.c symbols)
find_package(Lua QUIET)
if(LUA_FOUND)
    set(LUA_LIB_TARGET ${LUA_LIBRARIES})
    set(LUA_INC_DIR ${LUA_INCLUDE_DIR})
else()
    FetchContent_Declare(
        lua_source
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG        v5.4.7
    )
    FetchContent_GetProperties(lua_source)
    if(NOT lua_source_POPULATED)
        FetchContent_Populate(lua_source)
        file(GLOB LUA_SOURCES "${lua_source_SOURCE_DIR}/*.c")
        # Exclude CLI executables and the amalgamation onelua.c to prevent LNK4006 duplicate symbol warnings
        list(REMOVE_ITEM LUA_SOURCES 
            "${lua_source_SOURCE_DIR}/lua.c" 
            "${lua_source_SOURCE_DIR}/luac.c" 
            "${lua_source_SOURCE_DIR}/onelua.c"
        )
        add_library(lua_static STATIC ${LUA_SOURCES})
        target_include_directories(lua_static PUBLIC "${lua_source_SOURCE_DIR}")
        if(UNIX)
            target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
        endif()
        set(LUA_LIB_TARGET lua_static)
        set(LUA_INC_DIR "${lua_source_SOURCE_DIR}")
    endif()
endif()

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
    ${LUA_INC_DIR}
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
        src/Platform/Windows/WindowsUtils.cpp
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
        src/Platform/Linux/CredentialStore.cpp
    )
endif()

add_library(OmniPlatform STATIC ${OMNIPLATFORM_SOURCES})
target_link_libraries(OmniPlatform PUBLIC
    funchook
    spdlog::spdlog_header_only
    ${PLATFORM_LIBS}
)
# ---------------------------------------------------------------------------
# Core OmniSteam Target (DLL / Shared Library)
# ---------------------------------------------------------------------------
set(OMNISTEAM_SOURCES
    src/main.cpp
    src/Utils/Config/Config.cpp
    src/Utils/Config/LuaConfig.cpp
    src/Utils/Metadata/ManifestClient.cpp
    src/Utils/Metadata/PatternLoader.cpp
    src/Hook/HookManager.cpp
    src/Hook/Hooks_IPC.cpp
    src/Hook/Hooks_Decryption.cpp
    src/Hook/Hooks_Manifest.cpp
    src/Hook/Hooks_Package.cpp
)
add_library(OmniSteam SHARED ${OMNISTEAM_SOURCES})
set_target_properties(OmniSteam PROPERTIES
    OUTPUT_NAME "omnisteam"
    PREFIX "lib"
    ARCHIVE_OUTPUT_NAME "omnisteam"
    IMPORT_PREFIX "lib"
)

target_link_libraries(OmniSteam PRIVATE
    OmniPlatform
    tomlplusplus::tomlplusplus
    ${LUA_LIB_TARGET}
    spdlog::spdlog_header_only
    ${PLATFORM_LIBS}
)

if(ENABLE_LOGGING)
    target_compile_definitions(OmniSteam PRIVATE OMNI_LOGGING_ENABLED)
    target_compile_definitions(OmniPlatform PRIVATE OMNI_LOGGING_ENABLED)
endif()

# Windows proxy DLLs for seamless launch
if(WIN32)
    add_library(dwmapi SHARED src/Platform/Windows/Proxy/dwmapi.cpp)
    set_target_properties(dwmapi PROPERTIES PREFIX "")
    # DelayLoad / dynamic resolution: dwmapi forwards calls and does not statically depend on OmniSteam import lib
    target_include_directories(dwmapi PRIVATE src)
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
    apps/manager/src/ConfigManager.cpp
    apps/manager/src/DepotKeyStore.cpp
)

add_executable(omnisteam ${MANAGER_SOURCES})
target_include_directories(omnisteam PRIVATE apps/manager/src)
target_link_libraries(omnisteam PRIVATE
    OmniPlatform
    tomlplusplus::tomlplusplus
    spdlog::spdlog_header_only
    ${PLATFORM_LIBS}
)
# ---------------------------------------------------------------------------
# Test Suite
# ---------------------------------------------------------------------------
# Test Suite (Domain & Module Organized)
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()

    # 1. Platform Abstraction Layer (Memory, ByteSearch, Crypto, CredentialStore)
    add_executable(test_platform tests/test_platform.cpp)
    target_link_libraries(test_platform PRIVATE OmniPlatform)
    add_test(NAME PlatformTests COMMAND test_platform)

    # 2. Steam IPC Protocol & Signature / Manifest Metadata Engine
    add_executable(test_ipc_metadata tests/test_ipc_metadata.cpp src/Utils/Metadata/PatternLoader.cpp src/Utils/Metadata/ManifestClient.cpp src/Utils/Config/Config.cpp src/Utils/Config/LuaConfig.cpp)
    target_link_libraries(test_ipc_metadata PRIVATE OmniPlatform tomlplusplus::tomlplusplus ${LUA_LIB_TARGET})
    add_test(NAME IpcMetadataTests COMMAND test_ipc_metadata)

    # 3. Lua Script Manager (Unlock Code Generation, Lifecycle & Hot-Reload)
    add_executable(test_script_manager tests/test_script_manager.cpp apps/manager/src/ScriptManager.cpp)
    target_include_directories(test_script_manager PRIVATE apps/manager/src)
    target_link_libraries(test_script_manager PRIVATE OmniPlatform)
    add_test(NAME ScriptManagerTests COMMAND test_script_manager)

    # 4. Cloud Save & WebDAV Synchronization Engine
    add_executable(test_cloud_save tests/test_cloud_save.cpp apps/manager/src/SavePathResolver.cpp apps/manager/src/WebDavClient.cpp apps/manager/src/CloudSaveManager.cpp)
    target_include_directories(test_cloud_save PRIVATE apps/manager/src)
    target_link_libraries(test_cloud_save PRIVATE OmniPlatform tomlplusplus::tomlplusplus ${PLATFORM_LIBS})
    add_test(NAME CloudSaveTests COMMAND test_cloud_save)

    # 5. Packaging, Signatures & Decky Plugin Integration
    add_executable(test_packaging_integration tests/test_packaging_integration.cpp)
    target_link_libraries(test_packaging_integration PRIVATE OmniPlatform)
    add_test(NAME PackagingIntegrationTests COMMAND test_packaging_integration)

    # 6. Depot Key Local / Remote Auto-Fetch Store
    add_executable(test_depot_keys tests/test_depot_keys.cpp apps/manager/src/DepotKeyStore.cpp)
    target_include_directories(test_depot_keys PRIVATE apps/manager/src)
    target_link_libraries(test_depot_keys PRIVATE OmniPlatform)
    add_test(NAME DepotKeyTests COMMAND test_depot_keys)
endif()

# ---------------------------------------------------------------------------
# Code Formatting & Static Analysis Targets (Native CMake)
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT_BIN clang-format
    HINTS
        "C:/Program Files/LLVM/bin"
        "C:/Program Files (x86)/LLVM/bin"
        "/usr/local/opt/llvm/bin"
        "/opt/homebrew/opt/llvm/bin"
        "/usr/bin"
        "/usr/local/bin"
)
if(CLANG_FORMAT_BIN)
    file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/apps/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_CXX_SOURCE_FILES}
        COMMENT "Running clang-format on all C++ source files..."
    )
    add_custom_target(check-format
        COMMAND ${CLANG_FORMAT_BIN} --dry-run --Werror ${ALL_CXX_SOURCE_FILES}
        COMMENT "Checking code formatting style..."
    )
endif()

find_program(CLANG_TIDY_BIN clang-tidy
    HINTS
        "C:/Program Files/LLVM/bin"
        "C:/Program Files (x86)/LLVM/bin"
        "/usr/local/opt/llvm/bin"
        "/opt/homebrew/opt/llvm/bin"
        "/usr/bin"
        "/usr/local/bin"
)
if(CLANG_TIDY_BIN)
    add_custom_target(lint
        COMMAND ${CLANG_TIDY_BIN} -p ${CMAKE_BINARY_DIR} ${ALL_CXX_SOURCE_FILES}
        COMMENT "Running clang-tidy static analysis..."
    )
endif()
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
