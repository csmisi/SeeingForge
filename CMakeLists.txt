cmake_minimum_required(VERSION 3.25)

project(SeeingForge VERSION 0.1.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Concurrent Gui Widgets Test)

add_library(seeingforge_core
    src/core/HotPixelDetector.cpp
    src/core/HotPixelDetector.h
    src/core/ObjectDetector.cpp
    src/core/ObjectDetector.h
    src/core/PreviewProcessor.cpp
    src/core/PreviewProcessor.h
    src/core/QualityAnalyzer.cpp
    src/core/QualityAnalyzer.h
    src/core/SerFile.cpp
    src/core/SerFile.h
    src/core/SerExporter.cpp
    src/core/SerExporter.h
)
target_link_libraries(seeingforge_core PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(seeingforge_core PUBLIC src)
target_compile_options(seeingforge_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion>
)

add_executable(seeingforge WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
)
target_link_libraries(seeingforge PRIVATE seeingforge_core Qt6::Concurrent Qt6::Widgets)
set_target_properties(seeingforge PROPERTIES
    OUTPUT_NAME SeeingForge
    MACOSX_BUNDLE_BUNDLE_NAME SeeingForge
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_GUI_IDENTIFIER org.seeingforge.SeeingForge
)

add_executable(seeingforge_ser_probe tools/SerProbe.cpp)
target_link_libraries(seeingforge_ser_probe PRIVATE seeingforge_core Qt6::Core)

include(CTest)
if(BUILD_TESTING)
    add_executable(serfile_tests tests/SerFileTests.cpp)
    target_link_libraries(serfile_tests PRIVATE seeingforge_core Qt6::Test)
    add_test(NAME serfile_tests COMMAND serfile_tests)
    set_tests_properties(serfile_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS seeingforge
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

qt_generate_deploy_app_script(
    TARGET seeingforge
    OUTPUT_SCRIPT seeingforge_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${seeingforge_deploy_script})
