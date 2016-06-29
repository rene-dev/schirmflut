@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x64
cd /d %~dp0
cl -nologo -MTd -O2 -Oi -fp:fast -Gm- -EHsc -GR- -WX -W4 -wd4100 -wd4127 -FC client.c /link -incremental:no -opt:ref
del client.obj
pause