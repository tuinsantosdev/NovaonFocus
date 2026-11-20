#pragma once
// Utilitários de enumeração: dispositivos de áudio e programas tocando som.

#include <cstdint>
#include <string>
#include <vector>

struct IMMDevice;

namespace novaon {

struct DeviceInfo {
    std::wstring id;
    std::wstring name;
    bool isCapture = false;
    bool isDefault = false;
};

struct AudioAppInfo {
    uint32_t pid = 0;
    std::wstring exe;
    std::wstring device;
};

// COM precisa estar inicializado na thread chamadora.
std::vector<DeviceInfo> listDevices();
std::vector<AudioAppInfo> listAudioApps();

// Acha um dispositivo ativo pelo trecho do nome ("default" = padrão do
// Windows). Retorna nullptr se não achar. O chamador faz Release().
IMMDevice* findDevice(const std::wstring& nameFragment, bool capture);

// PID raiz de um executável (ignora processos filhos do mesmo exe, como
// as várias instâncias do Chrome ou do Discord). 0 se não estiver aberto.
uint32_t findRootProcess(const std::wstring& exe);
bool processAlive(uint32_t pid);
std::wstring processExeName(uint32_t pid);

} // namespace novaon
