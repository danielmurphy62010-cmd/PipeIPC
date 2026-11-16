@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined MSVC_DIR set "MSVC_DIR=%OneDrive%\APPS\MSVC"
if not defined JBIN set "JBIN=C:\Program Files\Eclipse IDE\v.25.12\eclipse\plugins\org.eclipse.justj.openjdk.hotspot.jre.full.win32.x86_64_21.0.9.v20251105-0741\jre\bin"
set "JAVA_HOME=%JBIN%\.."

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
cl /nologo /c /std:c++20 /O2 /EHsc /MD /W3 /permissive- /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I.. /Iblake3 ..\quickipc.cpp || goto :fail

echo Building C++ example...
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I.. ..\example.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /Fe:quickipc.exe || goto :fail

echo Running C++ tests...
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I.. ..\tests.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /Fe:quickipc_tests.exe || goto :fail
quickipc_tests.exe || goto :fail

where python >nul 2>&1
if errorlevel 1 goto :java
echo Building Python binding...
python -c "import pybind11" >nul 2>&1 || python -m pip install --user pybind11==2.13.6 || goto :fail
for /f "delims=" %%I in ('python -c "import pybind11; print(pybind11.get_include())"') do set "PYBIND_INC=%%I"
for /f "delims=" %%I in ('python -c "import sysconfig; print(sysconfig.get_path('include'))"') do set "PY_INC=%%I"
for /f "delims=" %%I in ('python -c "import sysconfig,sys,os; print(sysconfig.get_config_var('LIBDIR') or os.path.join(sys.base_prefix,'libs'))"') do set "PY_LIB=%%I"
for /f "delims=" %%I in ('python -c "import sysconfig,sys; print(sysconfig.get_config_var('LIBRARY') or ('python%d%d.lib' %% sys.version_info[:2]))"') do set "PY_LIBFILE=%%I"
if not exist "%PY_INC%\Python.h" (echo Python development headers unavailable; skipping binding.&goto :java)
if not exist "%PY_LIB%\%PY_LIBFILE%" (echo Python import library unavailable; skipping binding.&goto :java)
cl /nologo /LD /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I.. /Iblake3 /I"%PY_INC%" /I"%PYBIND_INC%" ..\module.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /link /LIBPATH:"%PY_LIB%" "%PY_LIBFILE%" /OUT:quickipc.pyd || goto :fail
set "PYTHONPATH=%CD%"
python -c "from quickipc import SharedVars; s=SharedVars('ci'); s.payload={'x':[1,2.5,True]}; assert s.payload['x'][0] == 1; del s.payload; assert 'payload' not in s" || goto :fail

:java
if not exist "%JBIN%\javac.exe" goto :copy
if not exist "%JAVA_HOME%\include\jni.h" (echo JNI headers unavailable; skipping Java binding.&goto :copy)
echo Building Java binding...
cl /nologo /LD /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I.. /I"%JAVA_HOME%\include" /I"%JAVA_HOME%\include\win32" ..\jni.cpp quickipc.obj blake3.obj blake3_dispatch.obj blake3_portable.obj bcrypt.lib ole32.lib /Fe:quickipc_jni.dll || goto :fail
mkdir java 2>nul
"%JBIN%\javac.exe" -d java ..\SharedVars.java ..\JavaExample.java || goto :fail
"%JBIN%\java.exe" "-Djava.library.path=%CD%" -cp java JavaExample || goto :fail
"%JBIN%\jar.exe" cf quickipc.jar -C java . || goto :fail

:copy
popd
copy /y build\quickipc.exe . >nul || goto :err
if exist build\quickipc.pyd copy /y build\quickipc.pyd . >nul || goto :err
if exist build\quickipc_jni.dll copy /y build\quickipc_jni.dll . >nul || goto :err
if exist build\quickipc.jar copy /y build\quickipc.jar . >nul || goto :err
rmdir /s /q build
echo Build OK.
exit /b 0

:fail
echo Build FAILED.
popd
exit /b 1

:err
echo Build FAILED.
exit /b 1
