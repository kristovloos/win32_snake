@echo off

set TARGET=../src/main.c
set BINARY=snake.exe

set COMPILER_FLAGS=%TARGET% -nologo -FC -Od -Oi -Z7 -Gw -GS- -Gs9999999
set LINKER_FLAGS=-driver -align:16 -stack:0x100000,0x100000 -incremental:no -opt:ref -emitpogophaseinfo -subsystem:windows

pushd build

cl %COMPILER_FLAGS% -link %LINKER_FLAGS% -out:%BINARY%

popd