cmake_minimum_required(VERSION 3.10)
project(AscensionCompatibilityTests LANGUAGES CXX)
add_executable(AscensionCompatibilityTests main.cpp)
target_compile_features(AscensionCompatibilityTests PRIVATE cxx_std_17)
target_include_directories(AscensionCompatibilityTests PRIVATE ../..)
enable_testing()
add_test(NAME AscensionCompatibility COMMAND AscensionCompatibilityTests)

# Exercise error recovery with the client's actual Lua runtime, without
# starting the game or depending on installed archives.
file(GLOB LUA_SOURCES "../../Engine/Source/lua-5.1/src/*.c")
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_executable(FrameScriptErrorTests frame_script_errors.cpp ${LUA_SOURCES}
    ../../Engine/Source/Base/ConvertUTF.cpp)
target_compile_features(FrameScriptErrorTests PRIVATE cxx_std_17)
target_include_directories(FrameScriptErrorTests PRIVATE ../..)
target_compile_definitions(FrameScriptErrorTests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_definitions(FrameScriptErrorTests PRIVATE TEST_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME FrameScriptErrors COMMAND FrameScriptErrorTests)
