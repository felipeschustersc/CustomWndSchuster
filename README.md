# CustomWndSchuster

DLL para o cliente **Lineage II Interlude** que permite ao servidor definir, pelo próprio HTML,
o **tamanho** e o **fundo** da janela de diálogo de NPC e a **cor** e o **alinhamento** do título
dela.

```html
<html width=500 height=420 noscroll>
  <title color=LEVEL align=center>Minha janela</title>
  ...
</html>
```

- **Não precisa alterar o servidor.** Qualquer servidor (aCis, L2J, etc.) só envia o HTML; os
  atributos novos são lidos pelo cliente.
- **Não precisa alterar o `L2.exe` nem o `NWindow.dll`.** A DLL é carregada por um proxy da
  `dsetup.dll` (incluído aqui) e aplica tudo na memória.
- Páginas que não usam os atributos continuam exatamente como no cliente original.

---

## Índice

1. [Conteúdo do repositório](#1-conteúdo-do-repositório)
2. [Compatibilidade](#2-compatibilidade)
3. [Instalação no cliente](#3-instalação-no-cliente)
4. [Uso no HTML](#4-uso-no-html)
5. [Servidor](#5-servidor)
6. [Compilando](#6-compilando)
7. [O proxy dsetup.dll](#7-o-proxy-dsetupdll)
8. [Como a CustomWndSchuster.dll funciona](#8-como-a-customwndschusterdll-funciona)
9. [Usando o código em outra DLL](#9-usando-o-código-em-outra-dll)
10. [Solução de problemas](#10-solução-de-problemas)
11. [Desinstalação](#11-desinstalação)
12. [Licenças](#12-licenças)

---

## 1. Conteúdo do repositório

```
CustomWndSchuster/
├── src/
│   ├── html_resize.h        API pública: HtmlResize_Init / HtmlResize_Shutdown
│   ├── html_resize.c        toda a funcionalidade (leitura do HTML, tamanho, título, hooks)
│   └── main.c               DllMain da CustomWndSchuster.dll (espera o NWindow.dll e instala)
├── proxy/
│   ├── dsetup.c             proxy da dsetup.dll que carrega a CustomWndSchuster.dll
│   ├── dsetup.def           exportação da DirectXSetupGetVersion no ordinal 11
│   └── out/dsetup.dll       proxy já compilado
├── minhook/                 MinHook (biblioteca de hooks, licença BSD 2-Clause em minhook/LICENSE.txt)
├── build.bat                compila as duas DLLs com zig (x86)
├── LICENSE                  licença MIT deste projeto
├── CustomWndSchuster.dll    DLL já compilada
└── CustomWndSchuster.patch  src/, proxy/, build.bat e este README em formato diff
```

As DLLs já compiladas (`CustomWndSchuster.dll` e `proxy/out/dsetup.dll`) podem ser usadas
direto, sem compilar nada.

---

## 2. Compatibilidade

A DLL acessa funções internas do `NWindow.dll` (a biblioteca de interface do cliente) por
endereço fixo. Por isso, ela só funciona com o **`NWindow.dll` do Interlude** para o qual esses
endereços foram levantados.

**`NWindow.dll` de referência** (testado):

| | |
|---|---|
| Tamanho | 3.850.260 bytes |
| SHA-256 | `cb85f7a5de375f1e9156ec6559aae2a0a809b3abac4150142aafb14bbf02b47b` |

Um `NWindow.dll` com hash diferente ainda pode ser compatível, desde que o código nos endereços
abaixo seja o mesmo. Por exemplo, há clientes com poucos bytes alterados em outras partes. Neste
arquivo, o offset no disco é igual ao RVA, então dá para conferir com qualquer editor
hexadecimal:

| Offset / RVA | Bytes esperados | Função |
|---|---|---|
| `0x1BC40` | `E8 5B 5B 00 00` | chamada que desenha o texto do título |
| `0x88440` | `55 8D AC 24 70 FF FF FF` | `NCHtmlViewer::LoadHtml` |
| `0x837D0` | `55 8B EC 6A FF 68 F0 A5` | `NCNPCHtmlViewer::OnPaint` |
| `0x43C10` | `55 8B EC 6A FF 68 A0 EB` | `NCScrollWnd::ShowScrollBar` |

> **Atenção:** se esses bytes não baterem, **não use** a DLL nesse cliente. Os hooks seriam
> instalados em endereços errados e o cliente provavelmente fecharia com erro. Os endereços
> precisariam ser levantados de novo para esse `NWindow.dll` (veja a [seção 8](#8-como-a-customwndschusterdll-funciona)).
> Só o desvio do título confere os bytes antes de ser aplicado (os outros hooks não conferem).

Outros requisitos:
- cliente de 32 bits (todo cliente Interlude é);
- a pasta `system` do cliente precisa ter a `dsetup.dll`. O `L2.exe` do Interlude importa essa
  DLL.

---

## 3. Instalação no cliente

Todos os arquivos vão na pasta **`system`** do cliente (a mesma do `L2.exe` e do `NWindow.dll`).

### 3.1 Cliente com a `dsetup.dll` original (caso mais comum)

A `dsetup.dll` original é a da Microsoft (DirectX Setup). Ela tem cerca de 62 KB, e nas
propriedades do arquivo (aba Detalhes) aparece "Microsoft Corporation".

1. **Renomeie** a `dsetup.dll` original para **`dsetup_base.dll`**. Não apague: o proxy repassa
   a chamada para ela.
2. **Copie** `proxy/out/dsetup.dll` para a pasta `system`.
3. **Copie** `CustomWndSchuster.dll` para a pasta `system`.

Resultado:

```
system/
├── L2.exe
├── NWindow.dll
├── dsetup.dll              ← proxy (deste repositório)
├── dsetup_base.dll         ← dsetup.dll original da Microsoft, renomeada
└── CustomWndSchuster.dll   ← esta DLL
```

Abra o jogo normalmente pelo `L2.exe`.

### 3.2 Cliente que já tem uma `dsetup.dll` modificada

Alguns clientes já trocaram a `dsetup.dll` original por uma própria, para carregar proteção ou
outras DLLs. Nesse caso, escolha uma das opções:

- **Opção A (recomendada): carregar pela DLL que você já usa.** Se você tem o código dela, adicione
  `LoadLibraryA("CustomWndSchuster.dll");` (com o caminho da pasta `system`) no `DLL_PROCESS_ATTACH`.
  Não use o proxy deste repositório.
- **Opção B: encadear.** Renomeie a `dsetup.dll` atual para `dsetup_base.dll` e instale o proxy
  como no item 3.1. Isso só funciona se a DLL atual exportar o ordinal 11
  (`DirectXSetupGetVersion`) e **não** carregar ela mesma um arquivo chamado `dsetup_base.dll`.
  Se carregar, ela carregaria a si própria, e a chamada ficaria em recursão infinita.

### 3.3 Conferindo se carregou

Com o jogo aberto, abra qualquer NPC cujo HTML use `<html width=... height=...>`. A janela
deve abrir no tamanho informado. Para conferir só o carregamento, ferramentas como o
*Process Explorer* (Sysinternals) mostram as DLLs carregadas no processo `L2.exe`; procure
`CustomWndSchuster.dll`.

---

## 4. Uso no HTML

Os atributos são escritos nas tags `<html>` e `<title>` da página enviada pelo servidor.

### 4.1 Atributos

| Tag | Atributo | Valores | Efeito |
|---|---|---|---|
| `<html>` | `width` | número (px) | largura total da janela. Padrão do cliente: **310** |
| `<html>` | `height` | número (px) | altura total da janela, incluindo a barra de título. Padrão do cliente: **401** |
| `<html>` | `noscroll` | sem valor | esconde a barra de rolagem e devolve os 16 px dela para o conteúdo. `noscrollbar` também funciona |
| `<html>` | `background` | `Pacote.Textura` | textura de fundo da janela, no lugar do `NPCHtml_BACK` (veja [4.3](#43-fundo-personalizado)) |
| `<html>` | `bgwidth` | número (px) | largura da imagem dentro da textura. Padrão: largura da textura |
| `<html>` | `bgheight` | número (px) | altura da imagem dentro da textura. Padrão: altura da textura |
| `<title>` | `color` | `#RRGGBB`, `0xRRGGBB`, `RRGGBB` ou um nome (abaixo) | cor do texto do título |
| `<title>` | `align` | `left`, `center`, `right` | posição do texto do título |

Regras:
- `width` e `height` só têm efeito **juntos**. Com apenas um dos dois, a janela fica no tamanho
  padrão.
- Use `noscroll` junto com `width` e `height`. Numa janela de tamanho padrão, ele esconde a
  barra, mas o conteúdo não ganha a largura dela.
- `color` e `align` funcionam também em janelas de tamanho padrão.
- Sem `align`, o título fica **centralizado** quando a janela tem tamanho personalizado; caso
  contrário, fica na posição original do cliente.
- Nomes de tag e atributo não diferenciam maiúsculas e minúsculas. Valores podem ser escritos
  com ou sem aspas (`width=500`, `width="500"` e `width='500'` são equivalentes).
- Cada página é lida do zero. Quando o jogador abre uma página **sem** os atributos, a janela,
  a barra de rolagem e o título voltam ao padrão sozinhos.

Nomes de cor aceitos em `color`:

| Nome | Cor |
|---|---|
| `LEVEL` | `#FFCC00` (o mesmo dourado do `<font color="LEVEL">`) |
| `WHITE` | `#FFFFFF` |
| `YELLOW` ou `GOLD` | `#FFFF00` |
| `RED` | `#FF0000` |
| `GREEN` | `#00FF00` |
| `BLUE` | `#0000FF` |
| `ORANGE` | `#FF8000` |
| `CYAN` | `#00FFFF` |
| `GRAY` ou `GREY` | `#808080` |

Sem `color`, o título usa a cor original do cliente.

### 4.2 Medidas para montar o layout

A janela tem uma barra de título de 20 px, e o conteúdo começa 7 px à esquerda e 30 px abaixo do
topo. A área disponível para o HTML fica assim:

| | Com barra de rolagem | Com `noscroll` |
|---|---|---|
| Largura útil para o HTML | `width - 30` | `width - 14` |
| Altura visível | `height - 37` | `height - 37` |

Exemplo: `<html width=500 height=420 noscroll>` deixa 486 x 383 px para o conteúdo. Uma
`<table width=480>` cabe sem cortar. Na janela padrão (310 x 401, com barra), a largura útil é de
280 px.

O fundo padrão da janela (`NPCHtml_BACK`) é esticado em 9 partes: os cantos de 20 px mantêm o
tamanho, e as bordas e o centro esticam. Por isso, a moldura não deforma em nenhum tamanho. O
título, o botão de fechar e a barra de rolagem acompanham a janela, inclusive quando ela é
arrastada.

### 4.3 Fundo personalizado

`background` troca o fundo da janela por uma textura de uma `.utx` do cliente (por exemplo,
`background=MeuPacote.FundoLoja`). A moldura padrão da janela continua visível. A imagem é
desenhada em **tamanho real**, sem esticar, no canto superior esquerdo da área de conteúdo.
Essa é a mesma área do conteúdo HTML: começa 30 px abaixo do topo (barra de título de 20 px e a
faixa de 10 px abaixo dela continuam visíveis), deixa 7 px de moldura nas laterais e embaixo e
mede `width - 14` x `height - 37` px. O que passar dela é cortado. Se a imagem for
menor, o resto da área mostra o fundo padrão.

Texturas de Unreal costumam ter lados em potência de 2 (256, 512, 1024...). Para uma imagem que
não tem esse tamanho, salve-a no canto superior esquerdo de uma textura maior e informe o tamanho
real da imagem com `bgwidth` e `bgheight`. Só essa área é usada, e o resto da textura é ignorado.

| Imagem | Textura na `.utx` | Atributos |
|---|---|---|
| 512 x 512 | 512 x 512 | `background=Pct.Fundo` |
| 450 x 463 | 512 x 512 | `background=Pct.Fundo bgwidth=450 bgheight=463` |

Para a imagem preencher a área inteira, faça-a com `width - 14` x `height - 37` px. Exemplo: para
`<html width=464 height=500>`, uma imagem de 450 x 463.

Regras:
- Funciona com tamanho personalizado ou padrão. No tamanho padrão (310 x 401), a área de conteúdo
  é de 296 x 364.
- Se a textura não for encontrada (pacote ou nome errado), a janela usa o fundo padrão.

### 4.4 Exemplos

Janela larga, sem rolagem, título dourado centralizado:

```html
<html width=600 height=300 noscroll>
<title color=LEVEL align=center>Loja de Eventos</title>
<body>
  <table width=580>
    <tr><td align=center>Bem-vindo!</td></tr>
  </table>
</body>
</html>
```

Tamanho padrão, só com o título colorido à esquerda:

```html
<html>
<title color="#00FFFF" align=left>Teleporte</title>
<body>...</body>
</html>
```

Fundo próprio: imagem de 450 x 463 salva numa textura de 512 x 512:

```html
<html width=464 height=500 noscroll background=MeuPacote.FundoLoja bgwidth=450 bgheight=463>
<title color=LEVEL>Loja</title>
<body>...</body>
</html>
```

Janela alta com rolagem (listas longas):

```html
<html width=350 height=600>
<title>Ranking</title>
<body>...</body>
</html>
```

---

## 5. Servidor

Não é preciso alterar o código do servidor. O servidor só envia o texto do HTML, e quem
interpreta os atributos é o cliente. Basta editar os arquivos `.htm` (ou o HTML gerado pelo
código).

Em clientes sem a DLL, os atributos são simplesmente ignorados e a janela abre no tamanho padrão.
Por isso, dá para usar os atributos mesmo que nem todos os jogadores tenham a DLL.

Dois cuidados:
- **Cache de HTML:** depois de editar um `.htm`, recarregue o cache do servidor (por exemplo,
  `//reload htm`, conforme o servidor) ou reinicie-o.
- **Limite de tamanho:** alguns servidores limitam o tamanho de uma página HTML. Os
  atributos acrescentam poucos caracteres, mas janelas grandes tendem a ter páginas grandes.
  Confira o limite do seu servidor (por exemplo, a constante de tamanho máximo em
  `NpcHtmlMessage`).

---

## 6. Compilando

Só é preciso compilar se você alterar o código. As DLLs compiladas já acompanham o repositório.

### 6.1 Requisitos

- [zig](https://ziglang.org/download/) (testado com a 0.16). Ele traz o compilador C e as
  bibliotecas do Windows; não precisa de Visual Studio.
- Windows (o `build.bat` é um script do `cmd`).

### 6.2 Compilar

```bat
build.bat
```

O script procura o zig na variável de ambiente `ZIG` (por exemplo,
`set ZIG=C:\zig\zig.exe`) e, se ela não existir, no `PATH`. Ele gera:

- `CustomWndSchuster.dll`;
- `proxy\out\dsetup.dll`.

Use `build.bat --no-pause` para não esperar uma tecla no final (útil em scripts).

Comandos equivalentes, para rodar à mão:

```bat
zig cc -target x86-windows-gnu -O2 -shared "-Wl,--image-base=0x24000000" -Iminhook -Iminhook\include ^
    -o CustomWndSchuster.dll src\main.c src\html_resize.c ^
    minhook\buffer.c minhook\hook.c minhook\trampoline.c minhook\HDE\hde32.c

zig cc -target x86-windows-gnu -O2 -shared "-Wl,--image-base=0x20000000" ^
    proxy\dsetup.def -o proxy\out\dsetup.dll proxy\dsetup.c
```

- `-target x86-windows-gnu`: obrigatório, porque o cliente é de 32 bits.
- `--image-base`: endereço preferido de carga de cada DLL, escolhido para não disputar espaço
  com as DLLs do cliente. Se o Windows precisar, ele realoca a DLL normalmente.
- Um aviso sobre maiúsculas/minúsculas em um `#include` do MinHook pode aparecer. É inofensivo.

### 6.3 Outros compiladores

O código é C comum e também compila com MinGW (i686) ou com clang, usando as mesmas opções. O
desvio do título usa assembly no estilo GCC/clang (`__attribute__((naked))` e `__asm__`), então
o MSVC não compila o `html_resize.c` sem adaptar essa função.

---

## 7. O proxy dsetup.dll

Arquivos: [proxy/dsetup.c](proxy/dsetup.c) e [proxy/dsetup.def](proxy/dsetup.def).

### 7.1 Por que a dsetup.dll

O `L2.exe` do Interlude importa uma única função da `dsetup.dll` (DirectX Setup, da Microsoft):
`DirectXSetupGetVersion`, pelo **ordinal 11**. Ao abrir um programa, o Windows procura as DLLs
primeiro na pasta do executável. Assim, uma `dsetup.dll` colocada na pasta `system` é carregada
junto com o jogo, antes de a janela abrir, e pode carregar outras DLLs. Isso dispensa injetores
e alterações no `L2.exe`.

### 7.2 O que ele faz

Ao ser carregado (`DLL_PROCESS_ATTACH`):

1. descobre a pasta em que está, que é a `system` (`GetModuleFileNameA`);
2. carrega `dsetup_base.dll` dessa pasta (a original renomeada) e guarda o endereço da
   `DirectXSetupGetVersion` dela (ordinal 11);
3. carrega `CustomWndSchuster.dll` dessa pasta.

A função exportada `DirectXSetupGetVersion(DWORD* versao, DWORD* revisao)` repassa a chamada
para a original. Se a `dsetup_base.dll` não existir, ela mesma responde DirectX 9.0c
(`0x00040009`, revisão `904`), e isso basta para o cliente abrir. Se a `CustomWndSchuster.dll`
não existir, o cliente abre normalmente, só sem a funcionalidade.

O arquivo `dsetup.def` exporta a função com o mesmo nome e o mesmo ordinal (11) da original:

```
LIBRARY dsetup
EXPORTS
    DirectXSetupGetVersion = DirectXSetupGetVersion@8 @11
```

`@8` é a decoração do `__stdcall`: dois parâmetros de 4 bytes.

### 7.3 Carregar outras DLLs pelo mesmo proxy

Para carregar mais DLLs, repita o passo 3 do `DllMain` com o nome de cada uma e recompile:

```c
snprintf(path, sizeof(path), "%sOutraDll.dll", dir);
LoadLibraryA(path);
```

---

## 8. Como a CustomWndSchuster.dll funciona

### 8.1 Inicialização

[src/main.c](src/main.c): no `DLL_PROCESS_ATTACH`, a DLL cria uma thread que espera o
`NWindow.dll` ser carregado. Ela verifica a cada 100 ms, por até 120 s. Depois, espera mais
300 ms para o `NWindow.dll` terminar a própria inicialização e chama
`HtmlResize_Init(base do NWindow.dll)`. No `DLL_PROCESS_DETACH`, chama `HtmlResize_Shutdown()`,
que desfaz os hooks.

`HtmlResize_Init` ([src/html_resize.c](src/html_resize.c)):

1. calcula o endereço das funções internas usadas (base + RVA);
2. troca a chamada que desenha o texto do título por um desvio, depois de conferir os bytes
   originais (`E8 5B 5B 00 00`);
3. instala três hooks com o MinHook: `LoadHtml`, `OnPaint` da janela de NPC e `ShowScrollBar`.

### 8.2 Fluxo ao abrir uma página

1. **`NCHtmlViewer::LoadHtml`** (hook): só age quando o viewer pertence à janela de NPC. Outras
   janelas HTML que usam o mesmo `NCHtmlViewer`, como o Community Board (Alt+B), passam direto
   para o original e não alteram o estado da página do NPC. Antes de o cliente montar a página,
   `ParseHtmlPage` lê `width`, `height`, `noscroll`, `background`, `bgwidth`, `bgheight`, `color`
   e `align`.
   - `ApplyWindowSize` redimensiona a janela de NPC (`NCNPCHtmlViewer`), a barra de título, o
     botão de fechar, a área de conteúdo e a barra de rolagem.
   - `ApplyTitleColor` registra a cor do título.
   - As duas funções rodam de novo **depois** do `LoadHtml` original, que recalcula a barra de
     rolagem e desfaria o ajuste.
   - Uma página sem os atributos restaura o padrão: 310 x 401, conteúdo de 296 x 364 e barra
     visível.
2. **`NCNPCHtmlViewer::OnPaint`** (hook), com tamanho personalizado: mantém o título, o
   botão de fechar e a barra de rolagem na posição certa (a janela pode ter sido arrastada) e
   desenha o fundo `NPCHtml_BACK` esticado em 9 partes (`Draw9SliceBackground`) e, se houver,
   a textura de `background` dentro da moldura (`DrawPageBackground`). Com o tamanho padrão, o
   desenho original é usado, e a textura de `background` é desenhada dentro da moldura. A textura é
   buscada pelo nome (`GetTexture`) só quando o nome muda, e não a cada quadro.
3. **Texto do título** (desvio em `NWindow.dll+0x1BC40`): o `NCFrameCtrl` desenha o título com
   `DrawTextWithColor`. A instrução `call` foi trocada por `Hook_DrawFrameTitleText`, que chama
   `AdjustFrameTitle(frame, &x, &cor, texto)`. Essa função troca a cor (só no título da janela de
   NPC), mede o texto e calcula o `x` para centralizar ou alinhar à direita. Depois, segue para o
   `DrawTextWithColor` original.
4. **`NCScrollWnd::ShowScrollBar`** (hook): com `noscroll`, sempre esconde a barra da janela de
   NPC, porque o cliente tenta mostrá-la de novo em vários momentos. A barra das outras janelas
   não é afetada.

### 8.3 Endereços usados (RVAs no `NWindow.dll`)

Definidos no início de [src/html_resize.c](src/html_resize.c):

| Constante | RVA | Função |
|---|---|---|
| `RVA_LOADHTML` | `0x88440` | `NCHtmlViewer::LoadHtml(const wchar_t*)` |
| `RVA_NPCVIEWER_ONPAINT` | `0x837D0` | `NCNPCHtmlViewer::OnPaint(canvas)` |
| `RVA_SHOWSCROLLBAR` | `0x43C10` | `NCScrollWnd::ShowScrollBar(int)` |
| `RVA_SCROLLWND_SETSIZE` | `0x439B0` | `NCScrollWnd::SetSize(float, float)` |
| `RVA_DRAWTEXTURE` | `0x21A00` | canvas `DrawTexture` |
| `RVA_PUSHORIGIN` | `0x2F40` | canvas `PushOrigin` |
| `RVA_SETCLIPRECT` | `0x21DF0` | canvas `SetClipRect` |
| `RVA_POPORIGIN` | `0x212E0` | canvas `PopOrigin` |
| `RVA_NPCVIEWER_VTBL` | `0x24EF7C` | vtable do `NCNPCHtmlViewer` (identifica a janela de NPC) |
| `RVA_TITLE_DRAWCALL` | `0x1BC40` | `call DrawTextWithColor` do título no `NCFrameCtrl` |
| `RVA_DRAWTEXTWITHCOLOR` | `0x217A0` | canvas `DrawTextWithColor` |
| `RVA_GETTEXTSIZE` | `0x20BB0` | medida de texto (largura, altura) |
| `RVA_GETTEXTURE` | `0x3F630` | busca de textura pelo nome (`Pacote.Textura`) |

O código também usa offsets de campos dos objetos do `NWindow.dll`, por exemplo `+0x80/+0x84`
(posição X/Y), `+0x88/+0x8C` (largura/altura), `+0xC0` (barra de título) e `+0x1E0..+0x204`
(barra de rolagem). Os comentários ao lado de cada uso explicam o campo. Para portar a DLL para
outro `NWindow.dll`, é preciso levantar de novo os RVAs **e** esses offsets.

---

## 9. Usando o código em outra DLL

Se você já tem uma DLL própria no cliente, não precisa de uma DLL separada:

1. copie `src/html_resize.c` e `src/html_resize.h`, e também a pasta `minhook/` se o projeto
   ainda não usa o MinHook;
2. depois que o `NWindow.dll` estiver carregado, chame:

   ```c
   #include "html_resize.h"
   HtmlResize_Init((uintptr_t)GetModuleHandleA("NWindow.dll"));   // retorna 1 se ok
   ```

3. ao descarregar, chame `HtmlResize_Shutdown();`.

Se o seu projeto já inicializa o MinHook, não há problema: `HtmlResize_Init` aceita
`MH_ERROR_ALREADY_INITIALIZED`. Mas `HtmlResize_Shutdown` chama `MH_Uninitialize()`, então, se
outros hooks seus dependem do MinHook, chame-a só no final ou remova essa linha.

Não carregue duas DLLs que enganchem as mesmas funções do `NWindow.dll`, como duas cópias deste
código ou outra DLL com funcionalidade parecida: os hooks entrariam em conflito.

Para usar o patch em formato diff: `git apply CustomWndSchuster.patch`, em uma pasta vazia,
recria `src/`, `proxy/`, `build.bat`, o `LICENSE` e este README. O MinHook não vai no diff. Copie a pasta
`minhook/` ou baixe-o em <https://github.com/TsudaKageyu/minhook>.

---

## 10. Solução de problemas

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Cliente não abre e diz que falta uma DLL ou um ponto de entrada | o proxy foi copiado sem a original, ou o arquivo errado foi renomeado | confira a [seção 3.1](#31-cliente-com-a-dsetupdll-original-caso-mais-comum): `dsetup.dll` = proxy, `dsetup_base.dll` = original |
| Cliente fecha ao abrir ou ao falar com um NPC | `NWindow.dll` diferente do suportado | confira os bytes da [seção 2](#2-compatibilidade). Se não baterem, remova a DLL |
| A janela não muda de tamanho | faltou `width` ou `height`, a DLL não carregou, ou o servidor está com o HTML antigo em cache | use os dois atributos, confira o carregamento ([seção 3.3](#33-conferindo-se-carregou)) e recarregue o HTML no servidor |
| A DLL não carrega | o antivírus bloqueou ou apagou a DLL (é comum com DLLs que fazem hooks), ou a pasta não é a `system` | adicione uma exceção no antivírus e confira se os arquivos estão ao lado do `L2.exe` |
| O conteúdo fica cortado à direita | a tabela é mais larga que a área de conteúdo | use as medidas da [seção 4.2](#42-medidas-para-montar-o-layout) |
| A cor do título não muda | valor de cor inválido | use `#RRGGBB` ou um dos nomes da tabela |
| Com outra DLL de interface, algo some ou fica estranho | duas DLLs enganchando as mesmas funções | use só uma delas |

---

## 11. Desinstalação

Na pasta `system`:

1. apague `CustomWndSchuster.dll`;
2. apague a `dsetup.dll` (o proxy);
3. renomeie `dsetup_base.dll` de volta para `dsetup.dll`.

Nada mais é alterado no cliente. Os arquivos do jogo e o `NWindow.dll` no disco nunca são
modificados.

---

## 12. Licenças

- **CustomWndSchuster** (`src/`, `proxy/`, `build.bat` e a documentação): licença **MIT**,
  Copyright (c) 2026 Felipe Schuster. O texto está em [LICENSE](LICENSE). Você pode usar, alterar
  e redistribuir, inclusive em projetos fechados, desde que mantenha o aviso de copyright e a
  licença.
- **MinHook** (`minhook/`): Copyright (C) 2009-2017 Tsuda Kageyu, licença **BSD 2-Clause**. Inclui
  o HDE (Hacker Disassembler Engine), Copyright (c) 2008-2009 Vyacheslav Patkov, também sob BSD
  2-Clause. O texto está em [minhook/LICENSE.txt](minhook/LICENSE.txt).
  - O MinHook vai compilado **dentro** das duas DLLs. Por isso, ao distribuir a
    `CustomWndSchuster.dll` ou o `dsetup.dll` compilados (por exemplo, no patch do seu cliente),
    inclua junto o `minhook/LICENSE.txt` ou reproduza o aviso dele na documentação.
- **Arquivos do cliente:** a `dsetup.dll` original (`dsetup_base.dll`) é da Microsoft, e o
  `NWindow.dll` e os demais arquivos do jogo são da NCsoft. Eles **não** fazem parte deste
  repositório e não são cobertos pelas licenças acima.
