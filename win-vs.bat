
:: open x64 native tools command prompt
rmdir /s /q build_win_vs
mkdir build_win_vs

cd build_win_vs

@echo - create the make Files/Microsoft
c:\opt\cmake\bin\cmake.exe .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:/opt/dev/qt/6.7.0/msvc2019_64"

:: ninja builder
:: "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -G "Ninja" ^
:: -DCMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe" ^
:: -DCMAKE_PREFIX_PATH="C:/opt/dev/qt/6.7.0/msvc2019_64"
::



@echo compile
cmake --build . --config Debug

@echo - run winddeployqt to prepare the executable with the required DLLs
C:\opt\dev\qt\6.7.0\msvc2019_64\bin\windeployqt.exe .\Debug\StockChart.exe

@echo - finally run the executable
StockChart.exe

cd ..
