
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. ^
-G "Visual Studio 17 2022" ^
-A x64 ^
-DCMAKE_PREFIX_PATH="C:/opt/dev/qt/6.7.0/msvc2019_64"


:: ninja builder
:: "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -G "Ninja" ^
:: -DCMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe" ^
:: -DCMAKE_PREFIX_PATH="C:/opt/dev/qt/6.7.0/msvc2019_64"
::

@echo - run do-cmake.bat to create the make Files/Microsoft
@echo do-cmake.bat

@echo compile 
@echo ninja

@echo - run winddeployqt to prepare the executable with the required DLLs
@echo C:\opt\dev\qt\6.7.0\msvc2019_64\bin\windeployqt.exe StockChart.exe

@echo - finally run the executable
@echo StockChart.exe