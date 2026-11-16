@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined MSVC_DIR set "MSVC_DIR=%OneDrive%\APPS\MSVC"
set "JBIN=C:\Program Files\Eclipse IDE\v.25.12\eclipse\plugins\org.eclipse.justj.openjdk.hotspot.jre.full.win32.x86_64_21.0.9.v20251105-0741\jre\bin"
set "JAVA_HOME=%JBIN%\.."
set "PY_STATUS=not built"
set "JAVA_STATUS=not built"

if exist "%MSVC_DIR%\setup_x64.bat" call "%MSVC_DIR%\setup_x64.bat" || goto :err
where cl >nul 2>&1 || (echo cl.exe not found.&goto :err)
where link >nul 2>&1 || (echo link.exe not found.&goto :err)
where curl >nul 2>&1 || (echo curl.exe not found.&goto :err)

if exist build rmdir /s /q build
mkdir build\blake3 || goto :err
pushd build

echo Fetching BLAKE3...
curl -fsSL "https://raw.githubusercontent.com/BLAKE3-team/BLAKE3/1.5.5/c/blake3.h" -o blake3\blake3.h || goto :fail
curl -fsSL "https://raw.githubusercontent.com/BLAKE3-team/BLAKE3/1.5.5/c/blake3_impl.h" -o blake3\blake3_impl.h || goto :fail
curl -fsSL "https://raw.githubusercontent.com/BLAKE3-team/BLAKE3/1.5.5/c/blake3.c" -o blake3\blake3.c || goto :fail
curl -fsSL "https://raw.githubusercontent.com/BLAKE3-team/BLAKE3/1.5.5/c/blake3_dispatch.c" -o blake3\blake3_dispatch.c || goto :fail
curl -fsSL "https://raw.githubusercontent.com/BLAKE3-team/BLAKE3/1.5.5/c/blake3_portable.c" -o blake3\blake3_portable.c || goto :fail

echo Compiling...
cl /nologo /c /TC /O2 /MD /DBLAKE3_NO_SSE2 /DBLAKE3_NO_SSE41 /DBLAKE3_NO_AVX2 /DBLAKE3_NO_AVX512 /Iblake3 blake3\blake3.c blake3\blake3_dispatch.c blake3\blake3_portable.c || goto :fail
cl /nologo /c /std:c++20 /O2 /EHsc /MD /W3 /permissive- /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\src /Iblake3 ..\src\quickipc.cpp || goto :fail

echo Building C++ example...
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\src ..\src\example.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /Fe:quickipc.exe || goto :fail

echo Running C++ tests...
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\src ..\src\tests.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /Fe:quickipc_tests.exe || goto :fail
quickipc_tests.exe || goto :fail

where python >nul 2>&1
if errorlevel 1 (set "PY_STATUS=skipped - Python not found"&goto :java)
echo Building Python binding...
python -m pip install --user --upgrade pybind11==3.1.0 || goto :fail
for /f "delims=" %%I in ('python -c "import pybind11; print(pybind11.get_include())"') do set "PYBIND_INC=%%I"
for /f "delims=" %%I in ('python -c "import sysconfig; print(sysconfig.get_path('include'))"') do set "PY_INC=%%I"
for /f "delims=" %%I in ('python -c "import sysconfig,sys,os; print(sysconfig.get_config_var('LIBDIR') or os.path.join(sys.base_prefix,'libs'))"') do set "PY_LIB=%%I"
for /f "delims=" %%I in ('python -c "import sys; print('.'.join(map(str,sys.version_info[:3])))"') do set "PY_VER=%%I"
for /f "delims=" %%I in ('python -c "import sys; print('python' + str(sys.version_info[0]) + str(sys.version_info[1]) + '.lib')"') do set "PY_LIBFILE=%%I"
for /f "delims=" %%I in ('python -c "import sys; print('python' + str(sys.version_info[0]) + str(sys.version_info[1]) + '.dll')"') do set "PY_DLLFILE=%%I"
set "PY_DLL="
for /f "delims=" %%I in ('where python') do if exist "%%~dpI..\%PY_DLLFILE%" set "PY_DLL=%%~dpI..\%PY_DLLFILE%"
if not defined PY_DLL for /f "delims=" %%I in ('python -c "import os,sys; p=os.path.join(sys.base_prefix, 'python' + str(sys.version_info[0]) + str(sys.version_info[1]) + '.dll'); print(os.path.abspath(p) if os.path.isfile(p) else '')"') do set "PY_DLL=%%I"

if not defined PY_DLL (
  set "PY_STATUS=skipped - Python runtime DLL missing"
  echo Could not find %PY_DLLFILE% in the parent directory of any "where python" result or in sys.base_prefix.
  goto :java
)
for %%I in ("%PY_DLL%") do set "PY_DLL_DIR=%%~dpI"
set "PATH=%PY_DLL_DIR%;%PATH%"

if exist "%PY_INC%\Python.h" if exist "%PY_LIB%\%PY_LIBFILE%" goto :py_ready
echo Fetching matching CPython %PY_VER% development package...
curl -fsSL "https://api.nuget.org/v3-flatcontainer/python/%PY_VER%/python.%PY_VER%.nupkg" -o python-dev.nupkg
if errorlevel 1 (set "PY_STATUS=skipped - CPython dev package unavailable"&echo Matching Python development package unavailable; skipping binding.&goto :java)
mkdir python-dev 2>nul
tar -xf python-dev.nupkg -C python-dev || goto :fail
set "PY_INC=%CD%\python-dev\tools\include"
set "PY_LIB=%CD%\python-dev\tools\libs"

:py_ready
if not exist "%PY_INC%\Python.h" (set "PY_STATUS=skipped - Python.h unavailable"&echo Python.h unavailable; skipping binding.&goto :java)
if not exist "%PY_LIB%\%PY_LIBFILE%" (set "PY_STATUS=skipped - %PY_LIBFILE% unavailable"&echo Python import library "%PY_LIBFILE%" unavailable; skipping binding.&goto :java)
cl /nologo /LD /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\src /Iblake3 /I"%PY_INC%" /I"%PYBIND_INC%" ..\src\module.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /link /LIBPATH:"%PY_LIB%" "%PY_LIBFILE%" /OUT:quickipc.pyd || goto :fail
set "PYTHONPATH=%CD%"
python -c "from quickipc import SharedVars; s=SharedVars('ci'); s.payload={'x':[1,2.5,True]}; assert s.payload['x'][0] == 1; del s.payload; assert 'payload' not in s" || goto :fail
set "PY_STATUS=built"

:java
if not exist "%JBIN%\javac.exe" (set "JAVA_STATUS=skipped - JDK javac.exe missing"&echo JDK javac.exe missing; skipping Java binding.&goto :copy)
if not exist "%JAVA_HOME%\include\jni.h" (set "JAVA_STATUS=skipped - JDK JNI headers missing"&echo JDK JNI headers unavailable; skipping Java binding.&goto :copy)
echo Building Java binding...
cl /nologo /LD /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\src /I"%JAVA_HOME%\include" /I"%JAVA_HOME%\include\win32" ..\src\jni.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /Fe:quickipc_jni.dll || goto :fail
mkdir java 2>nul
"%JBIN%\javac.exe" -d java ..\src\SharedVars.java ..\src\JavaExample.java || goto :fail
"%JBIN%\java.exe" "-Djava.library.path=%CD%" -cp java JavaExample || goto :fail
"%JBIN%\jar.exe" cf quickipc.jar -C java . || goto :fail
set "JAVA_STATUS=built"

:copy
popd
copy /y build\quickipc.exe . >nul || goto :err
if exist build\quickipc.pyd copy /y build\quickipc.pyd . >nul || goto :err
if exist build\quickipc_jni.dll copy /y build\quickipc_jni.dll . >nul || goto :err
if exist build\quickipc.jar copy /y build\quickipc.jar . >nul || goto :err
rmdir /s /q build
echo Native C++ build: OK.
echo Python binding: %PY_STATUS%.
echo Java binding: %JAVA_STATUS%.
exit /b 0

:fail
echo Build FAILED.
popd
exit /b 1

:err
echo Build FAILED.
exit /b 1
