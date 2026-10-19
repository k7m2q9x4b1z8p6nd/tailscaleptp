@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
rc /nologo resource.rc
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /DNDEBUG /W3 taildrop.cpp resource.res /link /OUT:taildrop.exe /SUBSYSTEM:WINDOWS
