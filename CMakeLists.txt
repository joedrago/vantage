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
        fonts
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/ext/colorist/lib/include
        ${CMAKE_SOURCE_DIR}/ext/DirectXTK/Inc
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

        # DirectXTK
        ${CMAKE_SOURCE_DIR}/ext/DirectXTK/Src/BinaryReader.cpp
        ${CMAKE_SOURCE_DIR}/ext/DirectXTK/Src/CommonStates.cpp
        ${CMAKE_SOURCE_DIR}/ext/DirectXTK/Src/SpriteBatch.cpp
        ${CMAKE_SOURCE_DIR}/ext/DirectXTK/Src/SpriteFont.cpp
        ${CMAKE_SOURCE_DIR}/ext/DirectXTK/Src/VertexTypes.cpp
    )
    target_link_libraries(vantage colorist)
endif()

if(APPLE)
    include_directories(
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/ext/colorist/lib/include
    )

    add_executable(
        Vantage MACOSX_BUNDLE
        osx/AppDelegate.m
        osx/GameViewController.m
        osx/main.m
        osx/Renderer.m
        osx/VantageView.m

        osx/Assets.xcassets
        osx/Shaders.metal
        osx/Main.storyboard
    )

    set_source_files_properties(
        osx/Shaders.metal
        osx/Main.storyboard
        osx/Assets.xcassets
        PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )

    set_target_properties(
        Vantage
        PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST
        ${CMAKE_CURRENT_LIST_DIR}/osx/Info.plist
    )

    target_link_libraries(Vantage colorist)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
      -framework AppKit \
      -framework Metal \
      -framework MetalKit \
      -framework ModelIO"
    )
endif()
