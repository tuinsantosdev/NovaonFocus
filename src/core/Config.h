#pragma once
// Leitura do arquivo de configuração (formato INI simples, UTF-8).
//
// [engine]
// latency_ms = 20
//
// [source mic]
// type = capture        ; capture | loopback | app | app_exclude | send
// device = Focusrite    ; trecho do nome do dispositivo, ou "default"
// input_channel = 1     ; 1, 2 ou both (só para capture)
//
// [source jogo]
// type = app
// exe = game.exe
//
// [source daw]
// type = send           ; plugin VST3 "NovaonFocus Send" na DAW
// channel = 1
//
// [bus live]
// mic = 1.0
// jogo = 0.7

#include <map>
#include <string>
#include <vector>

namespace novaon {

enum class SourceType { Capture, Loopback, App, AppExclude, Send };

struct SourceConfig {
    std::string name;
    SourceType type = SourceType::Capture;
    std::wstring device = L"default"; // capture/loopback
    std::wstring exe;                 // app/app_exclude
    int inputChannel = 0;             // 0 = both, 1 = esquerda, 2 = direita
    int sendChannel = 1;              // send: canal do plugin VST3 (1 a 8)
};

struct BusConfig {
    std::string name;
    std::vector<std::pair<std::string, float>> gains; // fonte -> ganho linear
};

struct EngineConfig {
    int latencyMs = 20;
    std::vector<SourceConfig> sources;
    std::vector<BusConfig> buses;
};

// Retorna false e preenche `error` se o arquivo for inválido.
bool loadConfig(const std::wstring& path, EngineConfig& out, std::string& error);

// Grava no mesmo formato que loadConfig lê. Retorna false se não conseguir.
bool saveConfig(const std::wstring& path, const EngineConfig& cfg, std::string& error);

const char* sourceTypeName(SourceType t); // "capture", "loopback", ...

// Onde fica o novaonfocus.ini:
//  - ao lado do .exe, se essa pasta aceita escrita (versão "portátil", zip);
//  - senão (instalado em Arquivos de Programas), em %APPDATA%\NovaonFocus,
//    copiando o .ini de exemplo do lado do .exe na primeira execução.
std::wstring resolveConfigPath();

std::wstring utf8ToWide(const std::string& s);
std::string wideToUtf8(const std::wstring& s);

} // namespace novaon
