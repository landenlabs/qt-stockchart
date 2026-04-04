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

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Network WebEngineWidgets)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    src/StockDataProvider.cpp
    src/AlphaVantageProvider.cpp
    src/FinnhubProvider.cpp
    src/PolygonProvider.cpp
    src/TwelveDataProvider.cpp
    src/YahooFinanceProvider.cpp
    src/FmpProvider.cpp
    src/YahooPageProvider.cpp
    src/StockCacheManager.cpp
    src/StockGroupManager.cpp
    src/ApiCallTracker.cpp
    src/ChartManager.cpp
    src/TableManager.cpp
    src/CsvPorter.cpp
    src/CrashHandler.cpp
    src/Logger.cpp
    src/WebBrowserWidget.cpp
    src/RequestInterceptor.cpp
    src/AdBlockDialog.cpp
    src/AppSettings.cpp
)

set(HEADERS
    src/MainWindow.h
    src/SettingsDialog.h
    src/StockDataProvider.h
    src/AlphaVantageProvider.h
    src/FinnhubProvider.h
    src/PolygonProvider.h
    src/TwelveDataProvider.h
    src/YahooFinanceProvider.h
    src/FmpProvider.h
    src/YahooPageProvider.h
    src/StockCacheManager.h
    src/StockGroupManager.h
    src/ApiCallTracker.h
    src/ChartManager.h
    src/TableManager.h
    src/CsvPorter.h
    src/CrashHandler.h
    src/Logger.h
    src/WebBrowserWidget.h
    src/RequestInterceptor.h
    src/AdBlockDialog.h
    src/AppSettings.h
)

option(BUILD_AS_BUNDLE "Build as a macOS App Bundle" ON)

# Determine executable type based on Platform
# MACOSX_BUNDLE creates the .app folder structure macdeployqt needs.
# WIN32 prevents a command prompt from appearing behind the GUI on Windows.
if(APPLE AND BUILD_AS_BUNDLE)
    add_executable(StockChart MACOSX_BUNDLE ${SOURCES} ${HEADERS})
elseif(WIN32)
    add_executable(StockChart WIN32 ${SOURCES} ${HEADERS})
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
    Qt6::WebEngineWidgets
)

# Windows: DbgHelp for stack walking in the crash handler
if(WIN32)
    target_link_libraries(StockChart PRIVATE DbgHelp)
endif()

qt_add_resources(StockChart "app_resources"
    PREFIX "/"
    FILES
        bg_graph.jpg
        web-pages.json
)
