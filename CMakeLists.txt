cmake_minimum_required(VERSION 3.24)
project(CausticsAnalysis LANGUAGES CXX CUDA)

add_subdirectory(owl EXCLUDE_FROM_ALL)

set(CMAKE_CUDA_ARCHITECTURES "native")

include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(app
    src/main.cpp
    src/objLoader.cpp
    src/sceneLoader.cpp
    src/vtkExport.cpp
)

target_link_libraries(app PRIVATE owl::owl)

# ---- Copy OWL DLL ----
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:owl>
        $<TARGET_FILE_DIR:app>/owl.dll
    COMMENT "Copying OWL DLL to app directory"
)

# ---- Copy all .obj files ----
file(GLOB OBJ_FILES "${CMAKE_SOURCE_DIR}/meshes/*.obj")

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OBJ_FILES}
        $<TARGET_FILE_DIR:app>
    COMMENT "Copying all .obj files to output directory"
)

embed_ptx(
    OUTPUT_TARGET simple-ptx
    PTX_LINK_LIBRARIES owl::owl
    SOURCES ${CMAKE_SOURCE_DIR}/src/deviceCode.cu
)

target_link_libraries(app PRIVATE simple-ptx)