cmake_minimum_required(VERSION 3.24)
project(CausticsAnalysis LANGUAGES CXX CUDA)

# Add OWL as subdirectory
add_subdirectory(owl EXCLUDE_FROM_ALL)

# Create executable and link OWL
add_executable(app main.cpp)
target_link_libraries(app PRIVATE owl::owl)

# Copy OWL DLL to the app's output directory after build
add_custom_command(TARGET app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:owl>
    $<TARGET_FILE_DIR:app>
  COMMENT "Copying OWL DLL to app directory"
)

# Embed minimal device code into a PTX target and link into the app
embed_ptx(
  OUTPUT_TARGET
  simple-ptx

  PTX_LINK_LIBRARIES
  owl::owl

  SOURCES
  ${CMAKE_SOURCE_DIR}/deviceCode.cu)

target_link_libraries(app PRIVATE simple-ptx)