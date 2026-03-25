cmake_minimum_required(VERSION 3.24)
project(CausticsAnalysis LANGUAGES CXX CUDA)

add_subdirectory(owl EXCLUDE_FROM_ALL)

add_executable(app main.cpp objLoader.cpp)
include_directories(${CMAKE_SOURCE_DIR})
target_link_libraries(app PRIVATE owl::owl)

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:owl>
        $<TARGET_FILE_DIR:app>/owl.dll
    COMMENT "Copying OWL DLL to app directory"
)

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/Monkey.obj
        $<TARGET_FILE_DIR:app>/Monkey.obj
    COMMENT "Copying Monkey.obj to output directory"
)

embed_ptx(
    OUTPUT_TARGET simple-ptx
    PTX_LINK_LIBRARIES owl::owl
    SOURCES ${CMAKE_SOURCE_DIR}/deviceCode.cu
)

target_link_libraries(app PRIVATE simple-ptx)