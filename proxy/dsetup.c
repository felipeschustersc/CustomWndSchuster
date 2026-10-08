// dsetup.dll proxy: faz o cliente carregar a CustomWndSchuster.dll sem alterar o L2.exe.
//
// Por que a dsetup.dll?
//   O L2.exe do Interlude importa uma unica funcao da dsetup.dll (DirectX Setup da Microsoft):
//   DirectXSetupGetVersion, pelo ordinal 11. Como o Windows procura a DLL primeiro na pasta do
//   executavel (system), basta colocar ali uma dsetup.dll "falsa" que exporte essa funcao para
//   que ela seja carregada junto com o jogo, antes da janela abrir.
//
// O que este proxy faz (DLL_PROCESS_ATTACH):
//   1. descobre a pasta em que ele mesmo esta (a pasta system do cliente);
//   2. carrega a dsetup.dll original, renomeada para dsetup_base.dll, e guarda o endereco da
//      DirectXSetupGetVersion dela (ordinal 11);
//   3. carrega a CustomWndSchuster.dll da mesma pasta. Ela cria uma thread propria que espera o
//      NWindow.dll e instala os hooks; por isso nada mais precisa ser feito aqui.
//
// DirectXSetupGetVersion:
//   repassa a chamada para a dsetup_base.dll. Se ela nao existir, responde DirectX 9.0c
//   (4.09.00.0904), o que basta para o cliente abrir normalmente.
//
// Exportacao: dsetup.def exporta a funcao com o nome "DirectXSetupGetVersion" e o ordinal 11,
// igual a original (o L2.exe importa pelo ordinal).
//
// Para carregar outras DLLs alem da CustomWndSchuster.dll, repita o bloco do passo 3 com o nome
// de cada uma.

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (__stdcall *fnGetVersion)(DWORD*, DWORD*);
static HMODULE g_hBase = NULL;                   // dsetup_base.dll (a original da Microsoft)
static fnGetVersion g_pfnGetVersion = NULL;      // DirectXSetupGetVersion original

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    (void)lpReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);

        // 1. pasta deste proxy, com a barra final (ex.: "C:\Lineage II\system\")
        char dir[MAX_PATH] = {0};
        GetModuleFileNameA(hinstDLL, dir, MAX_PATH);
        char* lastSlash = strrchr(dir, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';

        // 2. dsetup.dll original, renomeada para dsetup_base.dll
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%sdsetup_base.dll", dir);
        g_hBase = LoadLibraryA(path);
        if (g_hBase) g_pfnGetVersion = (fnGetVersion)GetProcAddress(g_hBase, (LPCSTR)11);

        // 3. CustomWndSchuster.dll (se nao existir, o cliente abre normalmente, sem a funcionalidade)
        snprintf(path, sizeof(path), "%sCustomWndSchuster.dll", dir);
        LoadLibraryA(path);
    }
    return TRUE;
}

// Unica funcao que o L2.exe usa da dsetup.dll (ordinal 11).
__declspec(dllexport) int __stdcall DirectXSetupGetVersion(DWORD* pdwVersion, DWORD* pdwMinorVersion) {
    if (g_pfnGetVersion) return g_pfnGetVersion(pdwVersion, pdwMinorVersion);
    if (pdwVersion) *pdwVersion = 0x00040009;       // DirectX 9.0c
    if (pdwMinorVersion) *pdwMinorVersion = 904;
    return 1;
}
