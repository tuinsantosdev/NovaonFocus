#include "AudioDevices.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace novaon {

namespace {

// Definidos aqui para não depender de INITGUID nem de libs extras.
// {a45c254e-df1c-4efd-8020-67d146a850e0}, pid 14
const PROPERTYKEY kDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

template <class T> void safeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

std::wstring lowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    return s;
}

std::wstring deviceName(IMMDevice* dev) {
    std::wstring name;
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(kDeviceFriendlyName, &v)) && v.vt == VT_LPWSTR)
            name = v.pwszVal;
        PropVariantClear(&v);
        props->Release();
    }
    return name;
}

std::wstring deviceId(IMMDevice* dev) {
    std::wstring id;
    LPWSTR raw = nullptr;
    if (SUCCEEDED(dev->GetId(&raw)) && raw) {
        id = raw;
        CoTaskMemFree(raw);
    }
    return id;
}

IMMDeviceEnumerator* makeEnumerator() {
    IMMDeviceEnumerator* e = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&e));
    return e;
}

} // namespace

std::vector<DeviceInfo> listDevices() {
    std::vector<DeviceInfo> out;
    IMMDeviceEnumerator* en = makeEnumerator();
    if (!en) return out;
    for (EDataFlow flow : {eCapture, eRender}) {
        std::wstring defId;
        IMMDevice* def = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &def))) {
            defId = deviceId(def);
            def->Release();
        }
        IMMDeviceCollection* col = nullptr;
        if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) continue;
        UINT count = 0;
        col->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(col->Item(i, &dev))) continue;
            DeviceInfo info;
            info.id = deviceId(dev);
            info.name = deviceName(dev);
            info.isCapture = flow == eCapture;
            info.isDefault = info.id == defId;
            out.push_back(std::move(info));
            dev->Release();
        }
        col->Release();
    }
    en->Release();
    return out;
}

IMMDevice* findDevice(const std::wstring& fragment, bool capture) {
    IMMDeviceEnumerator* en = makeEnumerator();
    if (!en) return nullptr;
    const EDataFlow flow = capture ? eCapture : eRender;
    IMMDevice* found = nullptr;
    const std::wstring needle = lowerW(fragment);
    if (needle.empty() || needle == L"default") {
        en->GetDefaultAudioEndpoint(flow, eConsole, &found);
    } else {
        IMMDeviceCollection* col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) {
            UINT count = 0;
            col->GetCount(&count);
            for (UINT i = 0; i < count && !found; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(col->Item(i, &dev))) continue;
                if (lowerW(deviceName(dev)).find(needle) != std::wstring::npos) found = dev;
                else dev->Release();
            }
            col->Release();
        }
    }
    en->Release();
    return found;
}

std::vector<AudioAppInfo> listAudioApps() {
    std::vector<AudioAppInfo> out;
    IMMDeviceEnumerator* en = makeEnumerator();
    if (!en) return out;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) {
        UINT count = 0;
        col->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(col->Item(i, &dev))) continue;
            const std::wstring devName = deviceName(dev);
            IAudioSessionManager2* mgr = nullptr;
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(&mgr))) &&
                SUCCEEDED(mgr->GetSessionEnumerator(&sessions))) {
                int n = 0;
                sessions->GetCount(&n);
                for (int s = 0; s < n; ++s) {
                    IAudioSessionControl* ctl = nullptr;
                    IAudioSessionControl2* ctl2 = nullptr;
                    if (FAILED(sessions->GetSession(s, &ctl))) continue;
                    if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                      reinterpret_cast<void**>(&ctl2)))) {
                        DWORD pid = 0;
                        if (SUCCEEDED(ctl2->GetProcessId(&pid)) && pid != 0) {
                            AudioAppInfo app;
                            app.pid = pid;
                            app.exe = processExeName(pid);
                            app.device = devName;
                            out.push_back(std::move(app));
                        }
                        ctl2->Release();
                    }
                    ctl->Release();
                }
            }
            safeRelease(sessions);
            safeRelease(mgr);
            dev->Release();
        }
        col->Release();
    }
    en->Release();
    return out;
}

uint32_t findRootProcess(const std::wstring& exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    std::map<DWORD, DWORD> parentOf; // pid -> parent pid, só do exe procurado
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    const std::wstring target = lowerW(exe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (lowerW(pe.szExeFile) == target) parentOf[pe.th32ProcessID] = pe.th32ParentProcessID;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    // Raiz = processo cujo pai não é outra instância do mesmo exe.
    for (const auto& [pid, parent] : parentOf)
        if (parentOf.find(parent) == parentOf.end()) return pid;
    return parentOf.empty() ? 0 : parentOf.begin()->first;
}

bool processAlive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

std::wstring processExeName(uint32_t pid) {
    std::wstring name;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return name;
    wchar_t buf[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        name.assign(buf, size);
        const size_t slash = name.find_last_of(L"\\/");
        if (slash != std::wstring::npos) name = name.substr(slash + 1);
    }
    CloseHandle(h);
    return name;
}

} // namespace novaon
