# For an App builds: 
#  cd build
#    cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DBUILD_AS_BUNDLE=ON
#    cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DBUILD_AS_BUNDLE=OFF

cmake_minimum_required(VERSION 3.16)
project(StockChart VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Network)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    src/StockDataProvider.cpp
    src/AlphaVantageProvider.cpp
    src/FinnhubProvider.cpp
    src/PolygonProvider.cpp
    src/TwelveDataProvider.cpp
)

set(HEADERS
    src/MainWindow.h
    src/SettingsDialog.h
    src/StockDataProvider.h
    src/AlphaVantageProvider.h
    src/FinnhubProvider.h
    src/PolygonProvider.h
    src/TwelveDataProvider.h
)

option(BUILD_AS_BUNDLE "Build as a macOS App Bundle" ON)

if(APPLE AND BUILD_AS_BUNDLE)
    add_executable(StockChart MACOSX_BUNDLE ${SOURCES} ${HEADERS})
else()
    add_executable(StockChart ${SOURCES} ${HEADERS})
endif()

# Optional: Set Mac-specific bundle metadata
if(APPLE)
    set_target_properties(StockChart PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Qt StockChart"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.landenlabs.qt-stockchart"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

target_link_libraries(StockChart PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::Network
)
