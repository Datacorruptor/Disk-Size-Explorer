cmake_minimum_required(VERSION 3.16)

project(DiskSizeExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()


set(PROJECT_SOURCES
    main.cpp
    mainwindow.h
    mainwindow.cpp
    scanner.h
    scanner.cpp
    node.h
    fractionalprogressbar.h
    filetablemodel.h
    filetablemodel.cpp
    loadingdialog.h
    DiskSizeExplorer.rc
)

if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/DiskSizeExplorer.rc")
endif()


qt_add_executable(DiskSizeExplorer
    ${PROJECT_SOURCES}
    ${APP_ICON_RESOURCE_WINDOWS}
)


target_link_libraries(DiskSizeExplorer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)


set_target_properties(DiskSizeExplorer PROPERTIES
    WIN32_EXECUTABLE TRUE
)
