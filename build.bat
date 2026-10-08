@echo off
rem Compila a CustomWndSchuster.dll (tamanho e titulo da janela HTML de NPC) e o proxy
rem proxy\out\dsetup.dll, que faz o cliente carregar a DLL. Veja o README.md.
rem
rem Requer o zig (https://ziglang.org/download/). O script usa, nesta ordem:
rem   1. a variavel de ambiente ZIG (ex.: set ZIG=C:\zig\zig.exe), se existir;
rem   2. o zig.exe que estiver no PATH.
rem Uso: build.bat [--no-pause]

setlocal
cd /d "%~dp0"

if defined ZIG (
    set "ZIGEXE=%ZIG:"=%"
) else (
    for /f "delims=" %%i in ('where zig 2^>nul') do if not defined ZIGEXE set "ZIGEXE=%%i"
)
if not defined ZIGEXE (
    echo [ERRO] zig nao encontrado. Instale o zig ou defina a variavel ZIG com o caminho do zig.exe.
    goto :fail
)

echo [1/2] Compilando CustomWndSchuster.dll...
"%ZIGEXE%" cc -target x86-windows-gnu -O2 -shared "-Wl,--image-base=0x24000000" -Iminhook -Iminhook\include ^
    -o CustomWndSchuster.dll src\main.c src\html_resize.c minhook\buffer.c minhook\hook.c minhook\trampoline.c minhook\HDE\hde32.c || goto :fail

echo [2/2] Compilando proxy dsetup.dll...
if not exist proxy\out mkdir proxy\out
"%ZIGEXE%" cc -target x86-windows-gnu -O2 -shared "-Wl,--image-base=0x20000000" proxy\dsetup.def -o proxy\out\dsetup.dll proxy\dsetup.c || goto :fail

del /q *.lib *.pdb proxy\out\*.lib proxy\out\*.pdb 2>nul
echo [OK] CustomWndSchuster.dll e proxy\out\dsetup.dll gerados.
if not "%1"=="--no-pause" pause
exit /b 0

:fail
echo [ERRO] Build falhou.
if not "%1"=="--no-pause" pause
exit /b 1
