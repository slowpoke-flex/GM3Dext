cmake_minimum_required(VERSION 3.20)
project(GM3Dext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Jolt Physics Configuration
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "" FORCE)

add_subdirectory(vendor/JoltPhysics/Build JoltBuild EXCLUDE_FROM_ALL)

# Core Engine Sources
set(GM3D_CORE_SOURCES
    src/core/EngineContext.cpp
    src/core/EngineContext.h
    src/core/SharedMemory.h
    src/core/GMExport.h
    src/physics/PhysicsWorld.cpp
    src/physics/PhysicsWorld.h
    src/render/RenderWorld.cpp
    src/render/RenderWorld.h
    src/render/MeshManager.cpp
    src/render/MeshManager.h
    src/render/TextureManager.cpp
    src/render/TextureManager.h
    src/render/ParticleManager.cpp
    src/render/ParticleManager.h
    src/render/TerrainSystem.cpp
    src/render/TerrainSystem.h
)

# DLL Library for GameMaker
add_library(GM3Dext SHARED
    src/dllmain.cpp
    ${GM3D_CORE_SOURCES}
)

target_include_directories(GM3Dext PRIVATE 
    src
    vendor/JoltPhysics
    vendor/cgltf
)

target_link_libraries(GM3Dext PRIVATE 
    Jolt
    d3d11
    dxgi
    d3dcompiler
    windowscodecs
    ole32
)

target_compile_definitions(GM3Dext PRIVATE 
    GM3D_EXPORTS
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(GM3Dext PRIVATE
        /W3
        /O2
        /fp:fast
        /permissive-
    )
endif()

set_target_properties(GM3Dext PROPERTIES
    PREFIX ""
    OUTPUT_NAME "GM3Dext"
)

# -------------------------------------------------------------
# GM3D Studio - Standalone 3D World Editor
# -------------------------------------------------------------
set(IMGUI_SOURCES
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
    vendor/imguizmo/src/ImGuizmo.cpp
)

set(STUDIO_SOURCES
    tools/studio/main.cpp
    tools/studio/StudioApp.cpp
    tools/studio/StudioApp.h
    tools/studio/SceneData.h
    tools/studio/ProjectData.h
    tools/studio/ProjectData.cpp
    tools/studio/SceneSerializer.cpp
    tools/studio/Heightmap.cpp
    tools/studio/SceneSerializer.h
)

add_executable(GM3DStudio WIN32
    ${STUDIO_SOURCES}
    ${IMGUI_SOURCES}
    ${GM3D_CORE_SOURCES}
)

target_include_directories(GM3DStudio PRIVATE 
    src
    tools/studio
    vendor
    vendor/JoltPhysics
    vendor/cgltf
    vendor/imgui
    vendor/imgui/backends
    vendor/imguizmo/src
    vendor/nlohmann
)

target_link_libraries(GM3DStudio PRIVATE 
    Jolt
    d3d11
    dxgi
    d3dcompiler
    windowscodecs
    ole32
    comdlg32
    shell32
)

target_compile_definitions(GM3DStudio PRIVATE 
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(GM3DStudio PRIVATE
        /W3
        /O2
        /fp:fast
        /permissive-
    )
endif()

# Standalone diagnostics: no editor window or GPU is required.
add_executable(GM3DCheck tools/studio/GM3DCheck.cpp tools/studio/Diagnostics.cpp
    tools/studio/SceneSerializer.cpp tools/studio/ProjectData.cpp tools/studio/Heightmap.cpp)
target_include_directories(GM3DCheck PRIVATE tools/studio vendor)
add_executable(GM3DShaderCheck tools/studio/GM3DShaderCheck.cpp)
target_link_libraries(GM3DShaderCheck PRIVATE d3dcompiler)
add_executable(GM3DGraphicsSmoke tools/studio/GM3DGraphicsSmoke.cpp)
target_link_libraries(GM3DGraphicsSmoke PRIVATE GM3Dext user32)
add_executable(GM3DTerrainCheck EXCLUDE_FROM_ALL tools/studio/GM3DTerrainCheck.cpp
    src/render/TerrainSystem.cpp src/render/MeshManager.cpp src/render/TextureManager.cpp src/physics/PhysicsWorld.cpp)
target_include_directories(GM3DTerrainCheck PRIVATE src vendor/JoltPhysics vendor/cgltf)
target_compile_definitions(GM3DTerrainCheck PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(GM3DTerrainCheck PRIVATE Jolt d3d11 dxgi windowscodecs ole32)
if (MSVC)
    target_compile_options(GM3DTerrainCheck PRIVATE /O2 /fp:fast /permissive-)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(SceneSerializerTests
        tests/SceneSerializerTests.cpp
        tools/studio/SceneSerializer.cpp
        tools/studio/ProjectData.cpp
        tools/studio/Diagnostics.cpp
        tools/studio/Heightmap.cpp
    )
    target_include_directories(SceneSerializerTests PRIVATE tools/studio vendor)
    add_test(NAME SceneSerializerTests COMMAND SceneSerializerTests)
    add_test(NAME ShaderCompilation COMMAND GM3DShaderCheck "${CMAKE_SOURCE_DIR}")
    add_test(NAME DiagnosticsCLI COMMAND ${CMAKE_COMMAND}
        "-DCHECK=$<TARGET_FILE:GM3DCheck>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/diagnostics-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/DiagnosticsCliTests.cmake")
    add_executable(EngineLifecycleTests tests/EngineLifecycleTests.cpp)
    target_link_libraries(EngineLifecycleTests PRIVATE GM3Dext)
    add_test(NAME EngineLifecycle COMMAND EngineLifecycleTests)
    set_tests_properties(EngineLifecycle PROPERTIES TIMEOUT 30)
endif()

