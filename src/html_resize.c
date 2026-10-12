// Tamanho e titulo da janela HTML de NPC (NCNPCHtmlViewer) definidos pelo proprio HTML da pagina.
// Tamanho: leitura de <html width height noscroll>, ApplyWindowSize e os hooks que o mantem
// (LoadHtml, NCNPCHtmlViewer::OnPaint, NCScrollWnd::ShowScrollBar).
// Titulo: leitura de <title color align>, ApplyTitleColor e o desvio da chamada que desenha o
// texto do titulo (NCFrameCtrl, NWindow.dll+0x1BC40).

#include "html_resize.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "MinHook.h"

// ---- enderecos no NWindow.dll (RVA) ----
#define RVA_LOADHTML            0x88440   // NCHtmlViewer::LoadHtml(const wchar_t*)
#define RVA_NPCVIEWER_ONPAINT   0x837D0   // NCNPCHtmlViewer::OnPaint(canvas)
#define RVA_SHOWSCROLLBAR       0x43C10   // NCScrollWnd::ShowScrollBar(int)
#define RVA_SCROLLWND_SETSIZE   0x439B0   // NCScrollWnd::SetSize(float, float)
#define RVA_DRAWTEXTURE         0x21A00   // canvas DrawTexture
#define RVA_PUSHORIGIN          0x2F40    // canvas PushOrigin
#define RVA_SETCLIPRECT         0x21DF0   // canvas SetClipRect
#define RVA_POPORIGIN           0x212E0   // canvas PopOrigin
#define RVA_NPCVIEWER_VTBL      0x24EF7C  // vtable do NCNPCHtmlViewer
#define RVA_TITLE_DRAWCALL      0x1BC40   // NCFrameCtrl: "call DrawTextWithColor" do texto do titulo (E8 5B 5B 00 00)
#define RVA_DRAWTEXTWITHCOLOR   0x217A0   // canvas DrawTextWithColor
#define RVA_GETTEXTSIZE         0x20BB0   // medida de texto (largura, altura)

#define TITLE_DEFAULT_COLOR     0xFFC8D2DC

typedef void (__thiscall* fnDrawTexture)(void* canvas, int x, int y, int w, int h, int u, int v, int uw, int vh, void* tex, int alpha, int mask);
typedef void (__thiscall* fnScrollWndSetSize)(void* scrollWnd, float width, float height);
typedef void (__thiscall* fnPushOrigin)(void* canvas, int x, int y);
typedef void (__thiscall* fnSetClipRect)(void* canvas, int x, int y, int w, int h);
typedef void (__thiscall* fnPopOrigin)(void* canvas);
typedef void (__thiscall* fnShowScrollBar)(void* window, int bShow);
typedef void (__cdecl* fnGetTextSize)(int* outSize, const wchar_t* text, int p3, int p4, int p5, int p6, int p7, int p8);

typedef struct HtmlPageMeta {
    int customWidth;
    int customHeight;
    int noScroll;
    int hasTitleColor;
    unsigned int titleColor;
    int titleAlign;             // 0 = padrao (centralizado se customWidth > 0), 1 = esquerda, 2 = centro, 3 = direita
} HtmlPageMeta;

typedef struct ControlEntry {
    int flag;
    void* control;
    void* parent;
    void* meta;
} ControlEntry;

static uintptr_t g_nwindowBase = 0;
static fnDrawTexture g_pfnDrawTexture = NULL;
static fnScrollWndSetSize g_pfnScrollWndSetSize = NULL;
static fnPushOrigin g_pfnPushOrigin = NULL;
static fnSetClipRect g_pfnSetClipRect = NULL;
static fnPopOrigin g_pfnPopOrigin = NULL;
static fnShowScrollBar g_pfnShowScrollBar = NULL;
static fnGetTextSize g_pfnGetTextSize = NULL;
void* g_pfnDrawTextWithColor __asm__("html_resize_drawtext") = NULL;

static int (__thiscall* Original_LoadHtml)(void* thisPtr, const wchar_t* html) = NULL;
static void (__thiscall* Original_NCNPCHtmlViewer_OnPaint)(void* thisPtr, void* canvas) = NULL;
static void (__thiscall* Original_ShowScrollBar)(void* thisPtr, int bShow) = NULL;

static HtmlPageMeta s_currentPage;
static int s_bWasCustomSized = 0;
static int s_bWasTitleColored = 0;
static void* g_pColoredFrameCtrl = NULL;
static unsigned int g_customTitleColor = TITLE_DEFAULT_COLOR;

// NCHtmlViewer e titulo da janela de NPC. Outros viewers (Community Board etc.) passam pelos mesmos
// LoadHtml/ShowScrollBar e nao podem mexer no estado da pagina do NPC.
static void* s_npcHtmlViewer = NULL;
static void* s_npcTitleCtrl = NULL;

static int IsValidPointer(void* ptr) {
    if (!ptr) return 0;
    uintptr_t val = (uintptr_t)ptr;
    if (val < 0x10000 || val >= 0x7FFF0000) return 0;
    return 1;
}

// ---- leitura do <html ...> ----

static int ExtractAttrValue(const wchar_t* tagStart, const wchar_t* tagEnd, const wchar_t* attrName, wchar_t* outVal, int maxLen) {
    if (!tagStart || !tagEnd || !attrName || !outVal || maxLen <= 0) return 0;
    outVal[0] = L'\0';

    const wchar_t* p = tagStart;
    size_t attrLen = wcslen(attrName);

    while (p < tagEnd) {
        if (_wcsnicmp(p, attrName, attrLen) == 0) {
            wchar_t prevChar = (p == tagStart) ? L' ' : *(p - 1);
            wchar_t nextChar = *(p + attrLen);
            if ((prevChar == L' ' || prevChar == L'\t' || prevChar == L'<' || prevChar == L'\n' || prevChar == L'\r') &&
                (nextChar == L'=' || nextChar == L' ' || nextChar == L'\t' || nextChar == L'>')) {

                const wchar_t* eq = p + attrLen;
                while (eq < tagEnd && (*eq == L' ' || *eq == L'\t')) eq++;

                if (eq < tagEnd && *eq == L'=') {
                    eq++;
                    while (eq < tagEnd && (*eq == L' ' || *eq == L'\t')) eq++;

                    int outIdx = 0;
                    if (eq < tagEnd && (*eq == L'"' || *eq == L'\'')) {
                        wchar_t quote = *eq++;
                        while (eq < tagEnd && *eq != quote && outIdx < maxLen - 1) {
                            outVal[outIdx++] = *eq++;
                        }
                    } else {
                        while (eq < tagEnd && *eq != L' ' && *eq != L'\t' && *eq != L'>' && *eq != L'\r' && *eq != L'\n' && outIdx < maxLen - 1) {
                            outVal[outIdx++] = *eq++;
                        }
                    }
                    outVal[outIdx] = L'\0';
                    return 1;
                } else {
                    // atributo booleano (ex.: noscroll)
                    outVal[0] = L'1';
                    outVal[1] = L'\0';
                    return 1;
                }
            }
        }
        p++;
    }
    return 0;
}

static unsigned int ParseColorString(const wchar_t* str) {
    if (!str || !str[0]) return TITLE_DEFAULT_COLOR;

    while (*str == L' ' || *str == L'\t') str++;
    if (*str == L'#') str++;
    else if (_wcsnicmp(str, L"0x", 2) == 0) str += 2;

    if (_wcsicmp(str, L"LEVEL") == 0) return 0xFFFFCC00;   // dourado do LEVEL do Interlude
    if (_wcsicmp(str, L"WHITE") == 0) return 0xFFFFFFFF;
    if (_wcsicmp(str, L"YELLOW") == 0 || _wcsicmp(str, L"GOLD") == 0) return 0xFFFFFF00;
    if (_wcsicmp(str, L"RED") == 0) return 0xFFFF0000;
    if (_wcsicmp(str, L"GREEN") == 0) return 0xFF00FF00;
    if (_wcsicmp(str, L"BLUE") == 0) return 0xFF0000FF;
    if (_wcsicmp(str, L"ORANGE") == 0) return 0xFFFF8000;
    if (_wcsicmp(str, L"CYAN") == 0) return 0xFF00FFFF;
    if (_wcsicmp(str, L"GRAY") == 0 || _wcsicmp(str, L"GREY") == 0) return 0xFF808080;

    // hexadecimal RRGGBB -> 0xFFRRGGBB
    unsigned int val = 0;
    swscanf(str, L"%x", &val);
    return 0xFF000000 | (val & 0x00FFFFFF);
}

static void ParseHtmlPage(const wchar_t* html) {
    memset(&s_currentPage, 0, sizeof(s_currentPage));
    if (!html) return;

    const wchar_t* p = html;
    wchar_t tempBuf[64];

    while (*p) {
        if (*p == L'<') {
            const wchar_t* tagStart = p + 1;
            while (*tagStart == L' ' || *tagStart == L'\t') tagStart++;

            const wchar_t* tagEnd = wcschr(tagStart, L'>');
            if (!tagEnd) break;

            if (_wcsnicmp(tagStart, L"html", 4) == 0 && (tagStart[4] == L' ' || tagStart[4] == L'\t' || tagStart[4] == L'>')) {
                if (ExtractAttrValue(tagStart, tagEnd, L"width", tempBuf, 64)) {
                    s_currentPage.customWidth = _wtoi(tempBuf);
                }
                if (ExtractAttrValue(tagStart, tagEnd, L"height", tempBuf, 64)) {
                    s_currentPage.customHeight = _wtoi(tempBuf);
                }
                if (ExtractAttrValue(tagStart, tagEnd, L"noscroll", tempBuf, 64) ||
                    ExtractAttrValue(tagStart, tagEnd, L"noscrollbar", tempBuf, 64)) {
                    s_currentPage.noScroll = 1;
                }
            }
            else if (_wcsnicmp(tagStart, L"title", 5) == 0 && (tagStart[5] == L' ' || tagStart[5] == L'\t' || tagStart[5] == L'>')) {
                if (ExtractAttrValue(tagStart, tagEnd, L"color", tempBuf, 64)) {
                    s_currentPage.hasTitleColor = 1;
                    s_currentPage.titleColor = ParseColorString(tempBuf);
                }
                if (ExtractAttrValue(tagStart, tagEnd, L"align", tempBuf, 64)) {
                    if (_wcsicmp(tempBuf, L"left") == 0) s_currentPage.titleAlign = 1;
                    else if (_wcsicmp(tempBuf, L"center") == 0) s_currentPage.titleAlign = 2;
                    else if (_wcsicmp(tempBuf, L"right") == 0) s_currentPage.titleAlign = 3;
                }
            }
            p = tagEnd + 1;
        } else {
            p++;
        }
    }
}

// ---- redimensionamento ----

static void FindNpcControls(void* htmlViewer, void** outNpcViewer) {
    *outNpcViewer = NULL;
    if (!htmlViewer) return;

    ControlEntry* entries = *(ControlEntry**)((char*)htmlViewer + 0xB8);
    if (!IsValidPointer(entries)) return;

    for (int i = 0; i < 16; i++) {
        if (entries[i].flag != 1) break;
        void* ctrl = entries[i].control;
        if (!IsValidPointer(ctrl)) break;

        void** vtbl = *(void***)ctrl;
        if (!IsValidPointer(vtbl)) continue;

        if ((uintptr_t)vtbl - g_nwindowBase == RVA_NPCVIEWER_VTBL) {
            *outNpcViewer = ctrl;
            break;
        }
    }
}

typedef void (__thiscall* fnSetFrameSize)(void*, float, float);

static void PlaceCloseButton(void* npchtmlViewer, void* titleCtrl, float targetW, int show) {
    // botao fechar (FrameCloseBtn) em [titleCtrl + 0x2F8] ou [titleCtrl + 0x2FC]
    void* closeBtn = *(void**)((char*)titleCtrl + 0x2F8);
    if (!closeBtn || !IsValidPointer(closeBtn)) {
        closeBtn = *(void**)((char*)titleCtrl + 0x2FC);
    }
    if (closeBtn && IsValidPointer(closeBtn)) {
        int originX = *(int*)((char*)npchtmlViewer + 0x80);
        int originY = *(int*)((char*)npchtmlViewer + 0x84);
        *(int*)((char*)closeBtn + 0x80) = originX + (int)targetW - 22;
        *(int*)((char*)closeBtn + 0x84) = originY + 4;
        *(float*)((char*)closeBtn + 0x88) = 15.0f;
        *(float*)((char*)closeBtn + 0x8C) = 15.0f;
        if (show) *(int*)((char*)closeBtn + 0xAC) |= 2;
    }
}

static void HideControl(void* ctrl) {
    if (ctrl && IsValidPointer(ctrl)) {
        *(int*)((char*)ctrl + 0x80) = -2000;
        *(int*)((char*)ctrl + 0x84) = -2000;
        *(int*)((char*)ctrl + 0xAC) &= ~2;
    }
}

static void PlaceScrollButton(void* btn, int x, int y) {
    if (btn && IsValidPointer(btn)) {
        *(int*)((char*)btn + 0x80) = x;
        *(int*)((char*)btn + 0x84) = y;
        *(float*)((char*)btn + 0x88) = 15.0f;
        *(float*)((char*)btn + 0x8C) = 15.0f;
        *(int*)((char*)btn + 0xAC) |= 2;
    }
}

static void ApplyWindowSize(void* htmlViewer, int width, int height, int noScroll) {
    if (!htmlViewer) return;

    // pagina padrao e janela nunca redimensionada: nao mexe em nada
    if (width <= 0 && height <= 0 && !s_bWasCustomSized) return;

    void* npchtmlViewer = NULL;
    FindNpcControls(htmlViewer, &npchtmlViewer);

    if (width > 0 && height > 0) {
        s_bWasCustomSized = 1;
        float targetW = (float)width;
        float targetH = (float)height;

        // 1. janela NCNPCHtmlViewer + barra de titulo
        if (npchtmlViewer && IsValidPointer(npchtmlViewer)) {
            *(float*)((char*)npchtmlViewer + 0x88) = targetW;
            *(float*)((char*)npchtmlViewer + 0x8C) = targetH;

            void* titleCtrl = *(void**)((char*)npchtmlViewer + 0xC0);   // NCNameCtrl
            if (titleCtrl && IsValidPointer(titleCtrl)) {
                *(float*)((char*)titleCtrl + 0x88) = targetW;
                *(float*)((char*)titleCtrl + 0x8C) = 20.0f;

                // SetFrameSize nativo via vtable[0xA8] (RVA 0x59010)
                void** vtbl = *(void***)npchtmlViewer;
                if (IsValidPointer(vtbl) && IsValidPointer(vtbl[0xA8 / 4])) {
                    ((fnSetFrameSize)vtbl[0xA8 / 4])(npchtmlViewer, targetW, 20.0f);
                }
                PlaceCloseButton(npchtmlViewer, titleCtrl, targetW, 1);
            }
        }

        // 2. area de conteudo NCHtmlViewer: fica em (X+7, Y+30), logo W = largura - 14, H = altura - 37.
        // Com noscroll, devolve os 16 px que o NWindow.dll desconta da barra de rolagem no layout.
        float contentW = targetW - 14.0f + (noScroll ? 16.0f : 0.0f);
        float contentH = targetH - 37.0f;
        if (contentW > 0.0f && contentH > 0.0f) {
            if (g_pfnScrollWndSetSize) {
                g_pfnScrollWndSetSize(htmlViewer, contentW, contentH);
            } else {
                *(float*)((char*)htmlViewer + 0x88) = contentW;
                *(float*)((char*)htmlViewer + 0x8C) = contentH;
            }
        }

        // 3. barra de rolagem
        if (noScroll) {
            *(int*)((char*)htmlViewer + 0x1E8) = 0;   // bShowScrollBar
            *(int*)((char*)htmlViewer + 0x1E0) = 0;   // largura da barra
            *(int*)((char*)htmlViewer + 0x204) = 0;
            if (g_pfnShowScrollBar) g_pfnShowScrollBar(htmlViewer, 0);
            HideControl(*(void**)((char*)htmlViewer + 0x1EC));   // botao para cima
            HideControl(*(void**)((char*)htmlViewer + 0x1F0));   // botao para baixo
            HideControl(*(void**)((char*)htmlViewer + 0x1FC));   // slider
        } else {
            *(int*)((char*)htmlViewer + 0x1E8) = 1;
            *(int*)((char*)htmlViewer + 0x1E0) = 15;
            *(int*)((char*)htmlViewer + 0x204) = 15;
            if (npchtmlViewer && IsValidPointer(npchtmlViewer)) {
                int originX = *(int*)((char*)npchtmlViewer + 0x80);
                int originY = *(int*)((char*)npchtmlViewer + 0x84);
                int barX = originX + (int)targetW - 22;
                PlaceScrollButton(*(void**)((char*)htmlViewer + 0x1EC), barX, originY + 30);
                PlaceScrollButton(*(void**)((char*)htmlViewer + 0x1F0), barX, originY + (int)targetH - 22);
            }
        }
    } else if (s_bWasCustomSized) {
        // volta ao tamanho padrao do dialogo de NPC (310 x 401)
        s_bWasCustomSized = 0;

        if (npchtmlViewer && IsValidPointer(npchtmlViewer)) {
            *(float*)((char*)npchtmlViewer + 0x88) = 310.0f;
            *(float*)((char*)npchtmlViewer + 0x8C) = 401.0f;

            void* titleCtrl = *(void**)((char*)npchtmlViewer + 0xC0);
            if (titleCtrl && IsValidPointer(titleCtrl)) {
                *(float*)((char*)titleCtrl + 0x88) = 310.0f;
                *(float*)((char*)titleCtrl + 0x8C) = 20.0f;
                *(unsigned int*)((char*)titleCtrl + 0x348) = 0xFFFFFFFF;

                void** vtbl = *(void***)npchtmlViewer;
                if (IsValidPointer(vtbl) && IsValidPointer(vtbl[0xA8 / 4])) {
                    ((fnSetFrameSize)vtbl[0xA8 / 4])(npchtmlViewer, 310.0f, 20.0f);
                }
                PlaceCloseButton(npchtmlViewer, titleCtrl, 310.0f, 0);
            }
        }

        if (g_pfnScrollWndSetSize) {
            g_pfnScrollWndSetSize(htmlViewer, 296.0f, 364.0f);
        } else {
            *(float*)((char*)htmlViewer + 0x88) = 296.0f;
            *(float*)((char*)htmlViewer + 0x8C) = 364.0f;
        }
        *(int*)((char*)htmlViewer + 0x1E8) = 1;
        *(int*)((char*)htmlViewer + 0x1E0) = 15;
        *(int*)((char*)htmlViewer + 0x204) = 15;
    }
}

// ---- cor do titulo ----
static void ApplyTitleColor(void* htmlViewer, int hasColor, unsigned int color) {
    if (!htmlViewer) return;
    if (!hasColor && !s_bWasTitleColored) return;

    void* npchtmlViewer = NULL;
    FindNpcControls(htmlViewer, &npchtmlViewer);
    if (!npchtmlViewer || !IsValidPointer(npchtmlViewer)) {
        if (!hasColor) {
            g_pColoredFrameCtrl = NULL;
            g_customTitleColor = TITLE_DEFAULT_COLOR;
        }
        return;
    }

    void* titleCtrl = *(void**)((char*)npchtmlViewer + 0xC0);
    if (titleCtrl && IsValidPointer(titleCtrl)) {
        if (hasColor) {
            s_bWasTitleColored = 1;
            g_pColoredFrameCtrl = titleCtrl;
            g_customTitleColor = color;
        } else if (s_bWasTitleColored) {
            s_bWasTitleColored = 0;
            g_pColoredFrameCtrl = NULL;
            g_customTitleColor = TITLE_DEFAULT_COLOR;
        }
    }
}

// ---- texto do titulo: cor e alinhamento ----
// Chamado pelo desvio no lugar do "call DrawTextWithColor" do NCFrameCtrl: troca a cor e a posicao X
// antes do desenho original.
void AdjustFrameTitle(void* frameCtrl, int* pX, unsigned int* pColor, const wchar_t* text) __asm__("html_resize_adjust_title");
void AdjustFrameTitle(void* frameCtrl, int* pX, unsigned int* pColor, const wchar_t* text) {
    if (!frameCtrl) return;

    if (frameCtrl == g_pColoredFrameCtrl && pColor) {
        *pColor = g_customTitleColor;
    }

    if (frameCtrl == s_npcTitleCtrl && pX && text && text[0] && g_pfnGetTextSize) {
        int shouldCenter = (s_currentPage.titleAlign == 2) || (s_currentPage.titleAlign == 0 && s_currentPage.customWidth > 0);
        int shouldRight = (s_currentPage.titleAlign == 3);

        if (shouldCenter || shouldRight) {
            int winW = (int)*(float*)((char*)frameCtrl + 0x88);
            if (winW > 0) {
                int outSize[2] = {0, 0};
                g_pfnGetTextSize(outSize, text, 0, 0, 0, 0, 0, 0);
                int textW = outSize[0];
                if (textW > 0 && textW < winW - 30) {
                    *pX = shouldCenter ? (winW - textW) / 2 : winW - textW - 25;
                    *(int*)((char*)frameCtrl + 0x320) = *pX;
                }
            }
        }
    }
}

// Desvio de "call DrawTextWithColor" (NWindow.dll+0x1BC40): esi = NCFrameCtrl; chama
// AdjustFrameTitle(frameCtrl, &x, &cor, texto) e segue para o DrawTextWithColor original.
__attribute__((naked, used)) static void Hook_DrawFrameTitleText(void) {
    __asm__ volatile (
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushl 24(%esp)\n\t"
        "leal 24(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "leal 20(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "pushl %esi\n\t"
        "call html_resize_adjust_title\n\t"
        "addl $16, %esp\n\t"
        "popl %edx\n\t"
        "popl %ecx\n\t"
        "jmpl *html_resize_drawtext\n\t"
    );
}

static const unsigned char kTitleCallBytes[5] = { 0xE8, 0x5B, 0x5B, 0x00, 0x00 };

static int PatchTitleCall(uintptr_t base, int install) {
    unsigned char* site = (unsigned char*)(base + RVA_TITLE_DRAWCALL);
    unsigned char code[5];
    if (install) {
        if (memcmp(site, kTitleCallBytes, 5) != 0) return 0;     // NWindow.dll diferente: nao mexe
        int rel = (int)((uintptr_t)Hook_DrawFrameTitleText - ((uintptr_t)site + 5));
        code[0] = 0xE8;
        memcpy(code + 1, &rel, 4);
    } else {
        memcpy(code, kTitleCallBytes, 5);
    }
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(site, code, 5);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 1;
}

// ---- fundo esticado em 9 partes ----
// Na textura NPCHtml_BACK a area visivel e 310 x 381; o fundo comeca em Y = 20 (abaixo do titulo).
static void Draw9SliceBackground(void* canvas, void* tex, int winW, int winH) {
    if (!canvas || !tex || !g_pfnDrawTexture) return;

    int srcW = 310, srcH = 381;
    int cL = 20, cR = 20, cT = 20, cB = 20;   // cantos

    int destW = winW;
    int destH = winH - 20;
    int destCW = destW - cL - cR;
    int destCH = destH - cT - cB;
    int srcCW = srcW - cL - cR;
    int srcCH = srcH - cT - cB;

    if (destCW <= 0 || destCH <= 0) {
        g_pfnDrawTexture(canvas, 0, 20, destW, destH, 0, 0, srcW, srcH, tex, 255, 1);
        return;
    }

    g_pfnDrawTexture(canvas, 0, 20, cL, cT, 0, 0, cL, cT, tex, 255, 1);                                      // canto sup. esq.
    g_pfnDrawTexture(canvas, cL, 20, destCW, cT, cL, 0, srcCW, cT, tex, 255, 1);                             // borda superior
    g_pfnDrawTexture(canvas, winW - cR, 20, cR, cT, srcW - cR, 0, cR, cT, tex, 255, 1);                      // canto sup. dir.
    g_pfnDrawTexture(canvas, 0, 20 + cT, cL, destCH, 0, cT, cL, srcCH, tex, 255, 1);                         // borda esquerda
    g_pfnDrawTexture(canvas, cL, 20 + cT, destCW, destCH, cL, cT, srcCW, srcCH, tex, 255, 1);                // centro
    g_pfnDrawTexture(canvas, winW - cR, 20 + cT, cR, destCH, srcW - cR, cT, cR, srcCH, tex, 255, 1);         // borda direita
    g_pfnDrawTexture(canvas, 0, winH - cB, cL, cB, 0, srcH - cB, cL, cB, tex, 255, 1);                       // canto inf. esq.
    g_pfnDrawTexture(canvas, cL, winH - cB, destCW, cB, cL, srcH - cB, srcCW, cB, tex, 255, 1);              // borda inferior
    g_pfnDrawTexture(canvas, winW - cR, winH - cB, cR, cB, srcW - cR, srcH - cB, cR, cB, tex, 255, 1);       // canto inf. dir.
}

// ---- hooks ----

// NCHtmlViewer::LoadHtml: le tamanho e titulo e aplica antes e depois (o UpdateScrollBar interno desfaria)
static int __fastcall Hooked_LoadHtml(void* thisPtr, void* edx, const wchar_t* html) {
    (void)edx;
    void* npcViewer = NULL;
    FindNpcControls(thisPtr, &npcViewer);
    if (!npcViewer || !IsValidPointer(npcViewer)) {
        return Original_LoadHtml(thisPtr, html);   // nao e a janela de NPC: nao toca em nada
    }
    s_npcHtmlViewer = thisPtr;
    void* titleCtrl = *(void**)((char*)npcViewer + 0xC0);
    s_npcTitleCtrl = IsValidPointer(titleCtrl) ? titleCtrl : NULL;

    ParseHtmlPage(html);
    ApplyWindowSize(thisPtr, s_currentPage.customWidth, s_currentPage.customHeight, s_currentPage.noScroll);
    ApplyTitleColor(thisPtr, s_currentPage.hasTitleColor, s_currentPage.titleColor);
    int res = Original_LoadHtml(thisPtr, html);
    ApplyWindowSize(thisPtr, s_currentPage.customWidth, s_currentPage.customHeight, s_currentPage.noScroll);
    ApplyTitleColor(thisPtr, s_currentPage.hasTitleColor, s_currentPage.titleColor);
    return res;
}

// NCNPCHtmlViewer::OnPaint: com tamanho personalizado, desenha o fundo esticado e mantem titulo,
// botao fechar e barra de rolagem acompanhando a janela (ela pode ser arrastada)
static void __fastcall Hooked_NCNPCHtmlViewer_OnPaint(void* thisPtr, void* edx, void* canvas) {
    (void)edx;
    if (!thisPtr || !canvas) return;

    if (s_currentPage.customWidth <= 0 || s_currentPage.customHeight <= 0) {
        Original_NCNPCHtmlViewer_OnPaint(thisPtr, canvas);
        return;
    }

    int winX = *(int*)((char*)thisPtr + 0x80);
    int winY = *(int*)((char*)thisPtr + 0x84);
    int winW = (int)*(float*)((char*)thisPtr + 0x88);
    int winH = (int)*(float*)((char*)thisPtr + 0x8C);

    void* titleCtrl = *(void**)((char*)thisPtr + 0xC0);
    if (titleCtrl && IsValidPointer(titleCtrl)) {
        *(float*)((char*)titleCtrl + 0x88) = (float)winW;
        *(float*)((char*)titleCtrl + 0x8C) = 20.0f;
        PlaceCloseButton(thisPtr, titleCtrl, (float)winW, 1);
    }

    void* htmlViewer = *(void**)((char*)thisPtr + 0x1CC);
    if (htmlViewer && IsValidPointer(htmlViewer)) {
        void* btnUp = *(void**)((char*)htmlViewer + 0x1EC);
        void* btnDown = *(void**)((char*)htmlViewer + 0x1F0);
        void* slider = *(void**)((char*)htmlViewer + 0x1FC);
        if (s_currentPage.noScroll) {
            HideControl(btnUp);
            HideControl(btnDown);
            HideControl(slider);
        } else {
            int barX = winX + winW - 22;
            PlaceScrollButton(btnUp, barX, winY + 30);
            PlaceScrollButton(btnDown, barX, winY + winH - 22);
        }
    }

    if (g_pfnPushOrigin) g_pfnPushOrigin(canvas, winX, winY);
    if (g_pfnSetClipRect) g_pfnSetClipRect(canvas, winX, winY, winW, winH);

    void* defaultTex = *(void**)((char*)thisPtr + 0x1D0);   // NPCHtml_BACK
    if (defaultTex) Draw9SliceBackground(canvas, defaultTex, winW, winH);

    *(int*)((char*)canvas + 0x38) = 0;
    *(int*)((char*)canvas + 0x3C) = 0;
    if (g_pfnPopOrigin) g_pfnPopOrigin(canvas);
}

// NCScrollWnd::ShowScrollBar: com noscroll a barra nunca volta
static void __fastcall Hooked_ShowScrollBar(void* thisPtr, void* edx, int bShow) {
    (void)edx;
    Original_ShowScrollBar(thisPtr, (s_currentPage.noScroll && thisPtr == s_npcHtmlViewer) ? 0 : bShow);
}

// ---- instalacao ----

int HtmlResize_Init(uintptr_t base) {
    if (!base) return 0;
    g_nwindowBase = base;

    g_pfnDrawTexture = (fnDrawTexture)(base + RVA_DRAWTEXTURE);
    g_pfnScrollWndSetSize = (fnScrollWndSetSize)(base + RVA_SCROLLWND_SETSIZE);
    g_pfnPushOrigin = (fnPushOrigin)(base + RVA_PUSHORIGIN);
    g_pfnSetClipRect = (fnSetClipRect)(base + RVA_SETCLIPRECT);
    g_pfnPopOrigin = (fnPopOrigin)(base + RVA_POPORIGIN);
    g_pfnShowScrollBar = (fnShowScrollBar)(base + RVA_SHOWSCROLLBAR);
    g_pfnGetTextSize = (fnGetTextSize)(base + RVA_GETTEXTSIZE);
    g_pfnDrawTextWithColor = (void*)(base + RVA_DRAWTEXTWITHCOLOR);

    PatchTitleCall(base, 1);          // cor/alinhamento do titulo (ignorado se o NWindow.dll for outro)

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return 0;

    if (MH_CreateHook((void*)(base + RVA_LOADHTML), (void*)&Hooked_LoadHtml, (void**)&Original_LoadHtml) != MH_OK) return 0;
    if (MH_CreateHook((void*)(base + RVA_NPCVIEWER_ONPAINT), (void*)&Hooked_NCNPCHtmlViewer_OnPaint, (void**)&Original_NCNPCHtmlViewer_OnPaint) != MH_OK) return 0;
    if (MH_CreateHook((void*)(base + RVA_SHOWSCROLLBAR), (void*)&Hooked_ShowScrollBar, (void**)&Original_ShowScrollBar) != MH_OK) return 0;

    // depois de criado o hook, o ShowScrollBar chamado pelo ApplyWindowSize passa pelo original
    g_pfnShowScrollBar = Original_ShowScrollBar;

    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

void HtmlResize_Shutdown(void) {
    if (!g_nwindowBase) return;
    if (memcmp((void*)(g_nwindowBase + RVA_TITLE_DRAWCALL), kTitleCallBytes, 5) != 0)
        PatchTitleCall(g_nwindowBase, 0);
    MH_DisableHook((void*)(g_nwindowBase + RVA_LOADHTML));
    MH_DisableHook((void*)(g_nwindowBase + RVA_NPCVIEWER_ONPAINT));
    MH_DisableHook((void*)(g_nwindowBase + RVA_SHOWSCROLLBAR));
    MH_Uninitialize();
    g_nwindowBase = 0;
}
