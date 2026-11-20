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

`app` e `app_exclude` precisam do Windows 10 versão 2004 ou mais novo.

## Instalação

1. Baixe o `NovaonFocus-windows-x64` na aba **Actions** do GitHub (último build verde).
2. Copie `novaonfocus-obs.dll` para `C:\Program Files\obs-studio\obs-plugins\64bit\`.
3. Coloque `novaonfocus.exe` e `novaonfocus.ini` numa pasta sua.

## Uso

```
novaonfocus devices     # nomes das entradas e saídas
novaonfocus apps        # programas tocando som agora (nome do .exe)
novaonfocus run         # roda com novaonfocus.ini
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

Precisa do Visual Studio 2022 e do CMake.

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

- Interface gráfica (Dear ImGui) com faders, medidores e matriz de roteamento.
- Barramento de monitoração tocando direto no fone da Scarlett.
- Recarregar o `.ini` sem reiniciar.
- Captura ASIO opcional para o microfone com o buffer mínimo.
