# NovaonFocus

Roteador de áudio para a **Focusrite Scarlett 2i2 (3ª geração)** e o **OBS**, no Windows.

Ele separa as fontes de áudio (microfone, jogo, música, Discord, qualquer programa), mistura cada uma com o volume que você quiser em **barramentos** e entrega cada barramento ao OBS como uma fonte de áudio separada. Não é um driver: roda como um programa comum, sobre o driver oficial da Focusrite.

## Por que não usar só o loopback do Focusrite Control?

O loopback nativo da 3ª geração entrega ao Windows **uma única mistura estéreo** de tudo o que toca na Scarlett. Com ele não dá para:

- separar o jogo, a música e o Discord em faixas diferentes no OBS;
- tirar ou baixar um programa só na live sem mudar o que você ouve no fone;
- gravar o microfone isolado enquanto transmite a mistura.

O NovaonFocus faz isso capturando cada programa direto do Windows (*process loopback*), sem cabo virtual.

## Como funciona

```
 Scarlett (entrada 1) ─┐
 Jogo (game.exe)      ─┤     ┌──────────┐     memória       ┌───────────────┐
 Spotify.exe          ─┼──▶  │  Mixer   │ ─▶ compartilhada ─▶│ Plugin do OBS │
 Discord.exe          ─┤     │ 48 kHz   │    (1 por bus)     │ 1 fonte / bus │
 Áudio do PC          ─┘     └──────────┘                    └───────────────┘
```

- **Captura:** WASAPI em modo compartilhado, com eventos. Cada fonte roda na sua própria thread com prioridade MMCSS "Pro Audio" e se **reconecta sozinha** se o USB cair ou o programa fechar.
- **Mixer:** relógio próprio de 48 kHz (a mesma taxa do OBS), buffers lock-free e nenhuma alocação no caminho do áudio. Se uma fonte acumula atraso por deriva de relógio, o excesso é descartado para a latência não crescer.
- **Entrega ao OBS:** cada `[bus]` vira um bloco de memória compartilhada que o plugin lê. Não há driver virtual, que é justamente o tipo de componente que mais causa travamentos.

Tipos de fonte:

| `type`        | O que captura                                         |
|---------------|-------------------------------------------------------|
| `capture`     | Uma entrada (ex.: microfone da Scarlett)              |
| `loopback`    | Tudo o que toca numa saída                            |
| `app`         | Só um programa (e os processos filhos dele)           |
| `app_exclude` | Tudo, **menos** um programa (ex.: PC sem o Discord)   |

| `send`        | O que o plugin VST3 **NovaonFocus Send** manda da DAW |

`app` e `app_exclude` precisam do Windows 10 versão 2004 ou mais novo.

## DAW no Focusrite USB ASIO (FL Studio, Reaper, Ableton...)

O driver ASIO da Focusrite manda o som direto para a placa, por fora do mixer do Windows. Por isso **nenhum** loopback (nem o do Windows, nem o `app` daqui, nem o do OBS) enxerga o som da DAW. Para continuar no ASIO com a latência mínima:

1. Copie `novaonfocus-send.vst3` para `C:\Program Files\Common Files\VST3\`.
2. No FL Studio, rode *Options > Manage plugins > Find more plugins*.
3. Coloque o **NovaonFocus Send** no último slot do **Master** e deixe em *Canal 1*.
4. No `novaonfocus.ini`:
   ```
   [source daw]
   type = send
   channel = 1
   ```
   e coloque `daw = 1.0` no `[bus live]`.

O plugin não muda o som nem adiciona latência na DAW: ele só copia o áudio para a memória compartilhada. O app converte a taxa da DAW (44,1 kHz, 96 kHz...) para 48 kHz e **corrige a diferença de relógio** entre a Scarlett e o mixer o tempo todo. Por isso o atraso fica fixo na folga do `latency_ms` mesmo em lives de muitas horas, em vez de ir acumulando como acontece com outros plugins de envio. Numa simulação de 4 horas com o relógio da placa 300 ppm fora, o atraso ficou entre 18 e 24 ms o tempo todo.

Para mandar partes diferentes (por exemplo só a voz e só o beat), use instâncias do plugin em canais diferentes do mixer do FL, cada uma com um *Canal* diferente (1 a 8), e uma fonte `send` para cada.

## Instalação

Baixe o **`NovaonFocus-x.y.z-Setup.exe`** na página de [versões](https://github.com/tuinsantosdev/NovaonFocus/releases) e rode. Ele instala:

- o NovaonFocus (interface e motor), com atalho no menu Iniciar;
- o plugin VST3 **NovaonFocus Send** em `C:\Program Files\Common Files\VST3\`;
- o plugin do OBS na pasta do OBS, se ele estiver instalado.

Feche o OBS e a DAW antes de instalar. A configuração fica em `%APPDATA%\NovaonFocus\novaonfocus.ini` e é mantida nas atualizações. No FL Studio, rode *Options > Manage plugins > Find more plugins* depois de instalar.

O instalador não tem assinatura digital: se o Windows mostrar "O Windows protegeu o computador", clique em *Mais informações > Executar assim mesmo*.

**Versão portátil:** o zip `NovaonFocus-windows-x64` de cada build traz os mesmos arquivos soltos. Nesse caso o `novaonfocus.ini` fica ao lado do `.exe`.

## Interface gráfica

Dê duplo clique no **`novaonfocus-gui.exe`**. Ele lê e grava o mesmo `novaonfocus.ini` e liga o roteamento sozinho ao abrir.

- **Mixer:** escolha o barramento no topo (`live`, `mic`...) e ajuste cada fonte no fader. Duplo clique no fader volta para 0 dB, a roda do mouse anda 1 dB, e o botão **MUDO** corta a fonte só naquele barramento. Fontes fora do barramento mostram **INCLUIR**. À direita fica o medidor da saída que vai para o OBS.
- **Fontes:** cada fonte com estado (rodando, conectando, aguardando; para o `send`, conectado ou desconectado), nível e falhas. Clique para editar; o tipo, o dispositivo e o programa são escolhidos em listas, sem digitar nomes de cabeça.
- **Send da DAW:** se houver fonte `send`, o Mixer mostra um cartão com o canal do plugin, a taxa da DAW, o atraso atual contra o alvo e a correção de relógio em ppm, com o histórico do último minuto.
- **OBS:** passo a passo para ligar o plugin e botão para copiar o nome de cada barramento.
- **Ajustes:** folga (latência), tabela de diagnóstico de falhas e travamentos, e atalho para a pasta do `.ini`.

Volumes e mudos mudam na hora e são gravados sozinhos. Criar, apagar ou editar fontes e barramentos pede **APLICAR MUDANÇAS** na barra de baixo, que reinicia o motor (corte de cerca de 1 segundo). O botão redondo da barra de baixo liga e desliga o roteamento.

O modo terminal (`novaonfocus.exe`) continua igual. Use um ou outro de cada vez, porque os dois escrevem nos mesmos barramentos.

## Uso

Dê **duplo clique** no `novaonfocus.exe` para rodar com o `novaonfocus.ini` da mesma pasta. Se algo estiver errado na configuração, a janela fica aberta mostrando o erro.

Num terminal (PowerShell ou Prompt de Comando, dentro da pasta):

```
novaonfocus devices     # nomes das entradas e saídas
novaonfocus apps        # programas tocando som agora (nome do .exe)
novaonfocus             # roda com novaonfocus.ini (igual ao duplo clique)
```

Edite o [`novaonfocus.ini`](config/novaonfocus.ini) com as suas fontes e barramentos. Com o programa rodando:

```
status                    fontes, picos dos barramentos e contadores de falha
set live musica 0.2       muda um volume na hora (0 = mudo, 1 = normal)
quit                      encerra
```

No OBS, adicione a fonte **NovaonFocus Bus** e digite o nome do barramento (`live`, `mic`, `jogo`...). Para gravar faixas separadas, adicione uma fonte para cada barramento e marque as faixas em *Propriedades avançadas de áudio*. Desative no OBS o "Áudio da área de trabalho" padrão para não duplicar o som.

## Diagnóstico de travamentos e estalos

1. **LatencyMon:** rode por 10 minutos com áudio tocando. Um driver com DPC alto (Wi-Fi, placa de vídeo, ACPI) causa cortes em qualquer programa de áudio. Atualize ou desative o culpado.
2. **Focusrite Control:** buffer de 128 ou 256 amostras e **48 kHz** em todo lugar (Focusrite, Windows e OBS).
3. **Energia:** plano "Alto desempenho" e **suspensão seletiva de USB desativada** (Opções de energia > Configurações avançadas > USB).
4. **USB:** porta traseira da placa-mãe, sem hub e sem extensão.
5. **Windows:** desative "Aprimoramentos de áudio" nas propriedades da Scarlett.
6. **No NovaonFocus:** se `status` mostrar falhas subindo, aumente `latency_ms` no `[engine]`. A mensagem "o mixer ficou parado mais de 100 ms" quase sempre indica o problema de DPC do item 1.

## Compilar

Precisa do Visual Studio 2022 e do CMake. A interface baixa o Dear ImGui sozinha na configuração (desligue com `-DNOVAON_BUILD_GUI=OFF`).

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

Para incluir o plugin do OBS, baixe só os headers do OBS e aponte para eles:

```
git clone --depth 1 --filter=blob:none --sparse --branch 31.0.0 https://github.com/obsproject/obs-studio obs-src
git -C obs-src sparse-checkout set libobs
cmake -S . -B build -A x64 -DNOVAON_BUILD_OBS_PLUGIN=ON -DNOVAON_OBS_SOURCE_DIR=%CD%/obs-src
```

## Próximos passos

- Ícone na bandeja do sistema e iniciar junto com o Windows.
- Barramento de monitoração tocando direto no fone da Scarlett.
- Recarregar o `.ini` sem reiniciar.
- Captura ASIO opcional para o microfone com o buffer mínimo.
