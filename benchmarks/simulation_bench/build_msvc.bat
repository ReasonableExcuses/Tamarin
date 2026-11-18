@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if "%1"=="c_normal" (
    cl /O2 /arch:AVX2 /W3 /Fe:"%~dp0nbody_c_msvc_normal.exe" "%~dp0nbody_c_normal.c"
) else if "%1"=="c_expert" (
    cl /O2 /arch:AVX2 /fp:fast /W3 /Fe:"%~dp0nbody_c_msvc_expert.exe" "%~dp0nbody_c_expert.c"
) else if "%1"=="cpp_normal" (
    cl /O2 /arch:AVX2 /std:c++20 /W3 /EHsc /Fe:"%~dp0nbody_cpp_msvc_normal.exe" "%~dp0nbody_cpp_normal.cpp"
) else if "%1"=="cpp_expert" (
    cl /O2 /arch:AVX2 /fp:fast /std:c++20 /W3 /EHsc /Fe:"%~dp0nbody_cpp_msvc_expert.exe" "%~dp0nbody_cpp_expert.cpp"
)
