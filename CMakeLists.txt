cmake_minimum_required(VERSION 3.5)
project(vantage)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4996")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_SUPPRESS_REGENERATION TRUE)

add_subdirectory(ext)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/vertexShader.h
    COMMAND ${CMAKE_SOURCE_DIR}/ext/fxc/fxc.exe /nologo /T vs_4_0 /E VS /Fh${CMAKE_BINARY_DIR}/vertexShader.h ${CMAKE_SOURCE_DIR}/src/shaders.fx
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ext/fxc
    MAIN_DEPENDENCY src/shaders.fx
    COMMENT "Compiling vertex shader..."
    VERBATIM
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/pixelShader.h
    COMMAND ${CMAKE_SOURCE_DIR}/ext/fxc/fxc.exe /nologo /T ps_4_0 /E PS /Fh${CMAKE_BINARY_DIR}/pixelShader.h ${CMAKE_SOURCE_DIR}/src/shaders.fx
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ext/fxc
    MAIN_DEPENDENCY src/shaders.fx
    COMMENT "Compiling pixel shader..."
    VERBATIM
)

include_directories(
    src
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/ext/colorist/lib/include
    ${CMAKE_SOURCE_DIR}/ext/colorist/ext/openjpeg/thirdparty/liblcms2/include # this is awful! fix this in colorist
)
add_executable(vantage WIN32
    res/small.ico
    res/vantage.ico

    ${CMAKE_BINARY_DIR}/vertexShader.h
    ${CMAKE_BINARY_DIR}/pixelShader.h

    src/main.cpp
    src/resource.h
    src/targetver.h
    src/vantage.rc
    src/vantage.h

    src/vantage.cpp
    src/vantage_d3d.cpp
    src/vantage_image.cpp
    src/vantage_win.cpp
)
target_link_libraries(vantage colorist)
