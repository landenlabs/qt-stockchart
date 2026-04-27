# For an App builds: 
#  cd build
#    cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DBUILD_AS_BUNDLE=ON
#    cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DBUILD_AS_BUNDLE=OFF

cmake_minimum_required(VERSION 3.16)
project(StockChart VERSION 0.1 LANGUAGES CXX)

if(APPLE)
    # Target Apple Silicon (M2/M3) explicitly
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /Z7 embeds debug info in each .obj (no shared PDB during compilation),
# avoiding the Ninja parallel-write race that corrupts /Zi's shared PDB.
if(MSVC)
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Network WebEngineWidgets)

set(SOURCES
    # Top-level
    src/main.cpp
    src/MainWindow.cpp
    # providers/ — data provider implementations
    src/providers/ProviderRegistry.cpp
    src/providers/StockDataProvider.cpp
    src/providers/AlphaVantageProvider.cpp
    src/providers/FinnhubProvider.cpp
    src/providers/PolygonProvider.cpp
    src/providers/TwelveDataProvider.cpp
    src/providers/YahooFinanceProvider.cpp
    src/providers/FmpProvider.cpp
    src/providers/YahooPageProvider.cpp
    src/providers/MorningstarProvider.cpp
    src/providers/MorningstarWebProvider.cpp
    src/providers/GoogleFinanceWebProvider.cpp
    # stock/ — stock data, caching, grouping
    src/stock/StockCacheManager.cpp
    src/stock/StockGroupManager.cpp
    src/stock/CsvPorter.cpp
    # ui/ — dialogs and UI manager components
    src/ui/SettingsDialog.cpp
    src/ui/ApiCallTracker.cpp
    src/ui/ChartManager.cpp
    src/ui/TableManager.cpp
    src/ui/JsonViewerDialog.cpp
    # util/ — general utilities
    src/util/Logger.cpp
    src/util/CrashHandler.cpp
    src/util/AppSettings.cpp
    src/util/JSettings.cpp
    # web/ — browser widget and network interception
    src/web/WebBrowserWidget.cpp
    src/web/RequestInterceptor.cpp
    src/web/AdBlockDialog.cpp
)

set(HEADERS
    # Top-level
    src/MainWindow.h
    # providers/
    src/providers/ProviderRegistry.h
    src/providers/StockDataProvider.h
    src/providers/AlphaVantageProvider.h
    src/providers/FinnhubProvider.h
    src/providers/PolygonProvider.h
    src/providers/TwelveDataProvider.h
    src/providers/YahooFinanceProvider.h
    src/providers/FmpProvider.h
    src/providers/YahooPageProvider.h
    src/providers/MorningstarProvider.h
    src/providers/MorningstarWebProvider.h
    src/providers/GoogleFinanceWebProvider.h
    # stock/
    src/stock/StockCacheManager.h
    src/stock/StockGroupManager.h
    src/stock/CsvPorter.h
    # ui/
    src/ui/SettingsDialog.h
    src/ui/ApiCallTracker.h
    src/ui/ChartManager.h
    src/ui/TableManager.h
    src/ui/JsonViewerDialog.h
    # util/
    src/util/Logger.h
    src/util/CrashHandler.h
    src/util/AppSettings.h
    src/util/JSettings.h
    # web/
    src/web/WebBrowserWidget.h
    src/web/RequestInterceptor.h
    src/web/AdBlockDialog.h
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

# Add each subdirectory to the include path so existing #include "Foo.h" style
# includes resolve without needing relative path prefixes in any source file.
target_include_directories(StockChart PRIVATE
    src
    src/providers
    src/stock
    src/ui
    src/util
    src/web
)

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
        landenlabs.png
        landenlabs.webp
        providers.json
)
