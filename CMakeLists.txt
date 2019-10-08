cmake_minimum_required(VERSION 3.5)
project(vantage)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_SUPPRESS_REGENERATION TRUE)

if(WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4996")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
endif()

add_subdirectory(ext)

if(WIN32)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/vertexShader.h
        COMMAND ${CMAKE_SOURCE_DIR}/ext/fxc/fxc.exe /nologo /T vs_4_0 /E VS /Fh${CMAKE_BINARY_DIR}/vertexShader.h ${CMAKE_SOURCE_DIR}/src/win32/shaders.fx
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ext/fxc
        MAIN_DEPENDENCY src/win32/shaders.fx
        COMMENT "Compiling vertex shader..."
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/pixelShader.h
        COMMAND ${CMAKE_SOURCE_DIR}/ext/fxc/fxc.exe /nologo /T ps_4_0 /E PS /Fh${CMAKE_BINARY_DIR}/pixelShader.h ${CMAKE_SOURCE_DIR}/src/win32/shaders.fx
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ext/fxc
        MAIN_DEPENDENCY src/win32/shaders.fx
        COMMENT "Compiling pixel shader..."
        VERBATIM
    )

    include_directories(
        src/common
        src/win32
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/ext/dyn/src
        ${CMAKE_SOURCE_DIR}/ext/colorist/lib/include
    )
    add_executable(vantage WIN32
        src/common/mono.c
        src/common/vantage.c
        src/common/vantage.h

        src/win32/res/vantage.ico

        ${CMAKE_BINARY_DIR}/vertexShader.h
        ${CMAKE_BINARY_DIR}/pixelShader.h

        src/win32/main.cpp
        src/win32/resource.h
        src/win32/targetver.h
        src/win32/vantage.rc
    )
    target_link_libraries(vantage dyn colorist)
endif()

if(APPLE)
    include_directories(
        src/common
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/ext/dyn/src
        ${CMAKE_SOURCE_DIR}/ext/colorist/lib/include
    )

    add_executable(
        Vantage MACOSX_BUNDLE

        src/common/mono.c
        src/common/vantage.c
        src/common/vantage.h

        src/osx/AppDelegate.m
        src/osx/GameViewController.m
        src/osx/main.m
        src/osx/Renderer.m
        src/osx/VantageView.m

        src/osx/Assets.xcassets
        src/osx/Shaders.metal
        src/osx/Main.storyboard
    )

    set_source_files_properties(
        src/osx/Shaders.metal
        src/osx/Main.storyboard
        src/osx/Assets.xcassets
        PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )

    set_target_properties(
        Vantage
        PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST
        ${CMAKE_CURRENT_LIST_DIR}/src/osx/Info.plist
    )

    target_link_libraries(Vantage dyn colorist)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
      -framework AppKit \
      -framework Metal \
      -framework MetalKit \
      -framework QuartzCore \
      -framework ModelIO"
    )
endif()
