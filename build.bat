@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "MSVC_DIR=%OneDrive%\APPS\MSVC"
set "JBIN=C:\Program Files\Eclipse IDE\v.25.12\eclipse\plugins\org.eclipse.justj.openjdk.hotspot.jre.full.win32.x86_64_21.0.9.v20251105-0741\jre\bin"
set "JAVA_HOME=%JBIN%\.."
set "BUILD=build"
set "B3=%BUILD%\blake3"

if exist "%MSVC_DIR%\setup_x64.bat" call "%MSVC_DIR%\setup_x64.bat" || goto :err
where cl >nul 2>&1 || (echo cl.exe not found.&goto :err)
where link >nul 2>&1 || (echo link.exe not found.&goto :err)
where lib >nul 2>&1 || (echo lib.exe not found.&goto :err)

if exist "%BUILD%" rmdir /s /q "%BUILD%"
mkdir "%BUILD%" || goto :err

if not exist "%B3%\c\blake3.c" (
  where curl >nul 2>&1 || (echo curl.exe is required to download BLAKE3.&goto :err)
  where tar >nul 2>&1 || (echo tar.exe is required to extract BLAKE3.&goto :err)
  curl -L --fail --silent --show-error "https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/1.5.5.zip" -o "%BUILD%\blake3.zip" || goto :err
  tar -xf "%BUILD%\blake3.zip" -C "%BUILD%" || goto :err
  move "%BUILD%\BLAKE3-1.5.5" "%B3%" >nul || goto :err
)

echo Building BLAKE3...
cl /nologo /c /TC /O2 /MD /DBLAKE3_NO_SSE2 /DBLAKE3_NO_SSE41 /DBLAKE3_NO_AVX2 /DBLAKE3_NO_AVX512 /I"%B3%\c" "%B3%\c\blake3.c" "%B3%\c\blake3_dispatch.c" "%B3%\c\blake3_portable.c" /Fo%BUILD%\\ || goto :err

echo Building QuickIPC core...
cl /nologo /c /std:c++20 /O2 /EHsc /MD /W3 /permissive- /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I. pipe.cpp protocol.cpp routing.cpp runtime.cpp shared_vars.cpp value.cpp /Fo%BUILD%\\ || goto :err

lib /nologo /OUT:"%BUILD%\quickipc_core.lib" "%BUILD%\pipe.obj" "%BUILD%\protocol.obj" "%BUILD%\routing.obj" "%BUILD%\runtime.obj" "%BUILD%\shared_vars.obj" "%BUILD%\value.obj" "%BUILD%\blake3.obj" "%BUILD%\blake3_dispatch.obj" "%BUILD%\blake3_portable.obj" || goto :err

echo Building C++ programs...
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I. client.cpp "%BUILD%\quickipc_core.lib" bcrypt.lib ole32.lib /Fo%BUILD%\\ /Fe:"%BUILD%\quickipc_client.exe" || goto :err
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I. server.cpp "%BUILD%\quickipc_core.lib" bcrypt.lib ole32.lib /Fo%BUILD%\\ /Fe:"%BUILD%\quickipc_server.exe" || goto :err
cl /nologo /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I. tests.cpp "%BUILD%\quickipc_core.lib" bcrypt.lib ole32.lib /Fo%BUILD%\\ /Fe:"%BUILD%\quickipc_tests.exe" || goto :err
"%BUILD%\quickipc_tests.exe" || goto :err

where python >nul 2>&1
if not errorlevel 1 (
  echo Building Python extension...
  python -c "import pybind11" >nul 2>&1 || python -m pip install --user pybind11==2.13.6 || goto :err
  for /f "delims=" %%I in ('python -c "import pybind11; print(pybind11.get_include())"') do set "PYBIND_INC=%%I"
  for /f "delims=" %%I in ('python -c "import sysconfig; print(sysconfig.get_path('include'))"') do set "PY_INC=%%I"
  for /f "delims=" %%I in ('python -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR') or '')"') do set "PY_LIB=%%I"
  for /f "delims=" %%I in ('python -c "import sys; print('python%%d%%d.lib' %% sys.version_info[:2])"') do set "PY_LIBFILE=%%I"
  if not exist "%PY_INC%\Python.h" (echo Python.h not found; skipping Python binding.&goto :java)
  if not exist "%PY_LIB%\%PY_LIBFILE%" (echo %PY_LIBFILE% not found; skipping Python binding.&goto :java)
  cl /nologo /LD /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I. /I"%B3%\c" /I"%PY_INC%" /I"%PYBIND_INC%" module.cpp "%BUILD%\quickipc_core.lib" bcrypt.lib ole32.lib /Fo%BUILD%\\ /link /LIBPATH:"%PY_LIB%" "%PY_LIBFILE%" /OUT:"%BUILD%\quickipc.pyd" || goto :err
  set "PYTHONPATH=%CD%\%BUILD%"
  python -c "from quickipc import SharedVars; s=SharedVars('ci'); s.payload={'x':[1,2.5,True]}; assert s.payload['x'][0] == 1; del s.payload; assert 'payload' not in s" || goto :err
) else (
  echo Python not found; skipping Python binding.
)

:java
if not exist "%JBIN%\javac.exe" (
  echo javac.exe not found; skipping Java binding.
  goto :copy
)
if not exist "%JAVA_HOME%\include\jni.h" (
  echo JNI headers not found in "%JAVA_HOME%\include"; skipping Java binding.
  goto :copy
)
echo Building Java JNI binding...
cl /nologo /LD /std:c++20 /O2 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I. /I"%JAVA_HOME%\include" /I"%JAVA_HOME%\include\win32" jni.cpp "%BUILD%\quickipc_core.lib" bcrypt.lib ole32.lib /Fo%BUILD%\\ /Fe:"%BUILD%\quickipc_jni.dll" || goto :err
mkdir "%BUILD%\java" 2>nul
"%JBIN%\javac.exe" -d "%BUILD%\java" SharedVars.java JavaExample.java || goto :err
"%JBIN%\java.exe" "-Djava.library.path=%CD%\%BUILD%" -cp "%BUILD%\java" JavaExample || goto :err
"%JBIN%\jar.exe" cf "%BUILD%\quickipc.jar" -C "%BUILD%\java" . || goto :err

:copy
copy /y "%BUILD%\quickipc_client.exe" . >nul || goto :err
copy /y "%BUILD%\quickipc_server.exe" . >nul || goto :err
copy /y "%BUILD%\quickipc_tests.exe" . >nul || goto :err
if exist "%BUILD%\quickipc.pyd" copy /y "%BUILD%\quickipc.pyd" . >nul || goto :err
if exist "%BUILD%\quickipc_jni.dll" copy /y "%BUILD%\quickipc_jni.dll" . >nul || goto :err
if exist "%BUILD%\quickipc.jar" copy /y "%BUILD%\quickipc.jar" . >nul || goto :err
rmdir /s /q "%BUILD%"
echo Build OK.
exit /b 0

:err
echo Build FAILED.
exit /b 1
