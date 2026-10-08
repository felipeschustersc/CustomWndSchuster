#ifndef HTML_RESIZE_H
#define HTML_RESIZE_H

// Tamanho e titulo da janela HTML de NPC (NCNPCHtmlViewer) definidos pelo proprio HTML
// (CustomWndSchuster.dll). Uso no HTML enviado pelo servidor:
//
//     <html width=500 height=420 noscroll>
//       <title color=LEVEL align=center>Minha janela</title> ...
//     </html>
//
//   width / height : tamanho total da janela (padrao do cliente: 310 x 401)
//   noscroll       : esconde a barra de rolagem (tambem aceita "noscrollbar")
//   color          : cor do titulo: #RRGGBB, 0xRRGGBB ou LEVEL, WHITE, YELLOW/GOLD, RED, GREEN,
//                    BLUE, ORANGE, CYAN, GRAY/GREY
//   align          : left, center ou right (sem align: centralizado quando ha width/height)
//
// Paginas sem esses atributos voltam automaticamente ao padrao.
// Depende do build do NWindow.dll do cliente (enderecos RVA em html_resize.c).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Instala os hooks no NWindow.dll ja carregado (base = endereco do modulo). Retorna 1 se ok.
int  HtmlResize_Init(uintptr_t nwindowBase);
void HtmlResize_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // HTML_RESIZE_H
