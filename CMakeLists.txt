cmake_minimum_required(VERSION 3.24)
project(CausticsAnalysis LANGUAGES CXX CUDA)

add_subdirectory(owl EXCLUDE_FROM_ALL)

add_executable(app main.cpp objLoader.cpp sceneLoader.cpp vtkExport.cpp)
include_directories(${CMAKE_SOURCE_DIR})
target_link_libraries(app PRIVATE owl::owl)

# ---- Copy OWL DLL ----
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:owl>
        $<TARGET_FILE_DIR:app>/owl.dll
    COMMENT "Copying OWL DLL to app directory"
)

# ---- Copy scene.json ----
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/scene.json
        $<TARGET_FILE_DIR:app>/scene.json
    COMMENT "Copying scene.json to output directory"    
)

# ---- Copy all .obj files ----
file(GLOB OBJ_FILES "${CMAKE_SOURCE_DIR}/*.obj")

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OBJ_FILES}
        $<TARGET_FILE_DIR:app>
    COMMENT "Copying all .obj files to output directory"
)

embed_ptx(
    OUTPUT_TARGET simple-ptx
    PTX_LINK_LIBRARIES owl::owl
    SOURCES ${CMAKE_SOURCE_DIR}/deviceCode.cu
)

target_link_libraries(app PRIVATE simple-ptx)