# CMake generated Testfile for 
# Source directory: C:/Games/Repositories/omni-steam
# Build directory: C:/Games/Repositories/omni-steam/cmake-build-debug
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("PlatformTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_platform.exe")
set_tests_properties("PlatformTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;381;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("IpcMetadataTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_ipc_metadata.exe")
set_tests_properties("IpcMetadataTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;386;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("ScriptManagerTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_script_manager.exe")
set_tests_properties("ScriptManagerTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;391;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("CloudSaveTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_cloud_save.exe")
set_tests_properties("CloudSaveTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;397;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("PackagingIntegrationTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_packaging_integration.exe")
set_tests_properties("PackagingIntegrationTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;402;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("DepotKeyTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_depot_keys.exe")
set_tests_properties("DepotKeyTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;408;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("PicsTokenTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_pics_token.exe")
set_tests_properties("PicsTokenTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;413;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
add_test("StatsProtoTests" "C:/Games/Repositories/omni-steam/cmake-build-debug/bin/test_stats_proto.exe")
set_tests_properties("StatsProtoTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Games/Repositories/omni-steam/CMakeLists.txt;417;add_test;C:/Games/Repositories/omni-steam/CMakeLists.txt;0;")
subdirs("_deps/tomlplusplus-build")
subdirs("_deps/capstone-build")
