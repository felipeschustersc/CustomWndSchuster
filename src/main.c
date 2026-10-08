// CustomWndSchuster.dll - DLL independente com o tamanho e o titulo da janela HTML de NPC.
// Espera o NWindow.dll ser carregado pelo cliente e instala os hooks.
// Para embutir em outra DLL, nao use este arquivo: inclua html_resize.c/.h e chame
// HtmlResize_Init(base do NWindow.dll) depois que o NWindow.dll estiver carregado.

#include "html_resize.h"

static DWORD WINAPI InitThread(LPVOID param) {
    (void)param;
    HMODULE hNWnd = NULL;
    for (int i = 0; i < 1200 && !hNWnd; i++) {      // ate 120 s
        hNWnd = GetModuleHandleA("NWindow.dll");
        if (!hNWnd) Sleep(100);
    }
    if (!hNWnd) return 0;
    Sleep(300);                                      // deixa o NWindow.dll terminar a propria inicializacao
    HtmlResize_Init((uintptr_t)hNWnd);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    (void)lpReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        HtmlResize_Shutdown();
    }
    return TRUE;
}
