#include "Config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace novaon {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

} // namespace

bool loadConfig(const std::wstring& path, EngineConfig& out, std::string& error) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) {
        error = "não foi possível abrir o arquivo de configuração";
        return false;
    }

    out = EngineConfig{};
    enum class Section { None, Engine, Source, Bus } section = Section::None;
    std::string line;
    int lineNo = 0;

    auto fail = [&](const std::string& msg) {
        error = "linha " + std::to_string(lineNo) + ": " + msg;
        return false;
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (lineNo == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            line = line.substr(3); // BOM UTF-8
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') return fail("seção sem ']'");
            std::istringstream hs(line.substr(1, line.size() - 2));
            std::string kind, name;
            hs >> kind >> name;
            kind = lower(kind);
            if (kind == "engine") {
                section = Section::Engine;
            } else if (kind == "source" && !name.empty()) {
                section = Section::Source;
                out.sources.push_back(SourceConfig{});
                out.sources.back().name = name;
            } else if (kind == "bus" && !name.empty()) {
                section = Section::Bus;
                out.buses.push_back(BusConfig{});
                out.buses.back().name = name;
            } else {
                return fail("seção desconhecida: " + line);
            }
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) return fail("esperado chave = valor");
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));

        switch (section) {
        case Section::None:
            return fail("chave fora de uma seção");
        case Section::Engine:
            if (key == "latency_ms") {
                out.latencyMs = std::clamp(std::atoi(value.c_str()), 3, 200);
            } else {
                return fail("chave desconhecida em [engine]: " + key);
            }
            break;
        case Section::Source: {
            SourceConfig& s = out.sources.back();
            if (key == "type") {
                const std::string v = lower(value);
                if (v == "capture") s.type = SourceType::Capture;
                else if (v == "loopback") s.type = SourceType::Loopback;
                else if (v == "app") s.type = SourceType::App;
                else if (v == "app_exclude") s.type = SourceType::AppExclude;
                else if (v == "send") s.type = SourceType::Send;
                else return fail("type inválido: " + value);
            } else if (key == "device") {
                s.device = utf8ToWide(value);
            } else if (key == "exe") {
                s.exe = utf8ToWide(value);
            } else if (key == "input_channel") {
                const std::string v = lower(value);
                if (v == "1") s.inputChannel = 1;
                else if (v == "2") s.inputChannel = 2;
                else if (v == "both") s.inputChannel = 0;
                else return fail("input_channel deve ser 1, 2 ou both");
            } else if (key == "channel") {
                s.sendChannel = std::atoi(value.c_str());
                if (s.sendChannel < 1 || s.sendChannel > 8) return fail("channel deve ser de 1 a 8");
            } else {
                return fail("chave desconhecida em [source]: " + key);
            }
            break;
        }
        case Section::Bus: {
            char* end = nullptr;
            const float g = std::strtof(value.c_str(), &end);
            if (end == value.c_str()) return fail("ganho inválido para " + key);
            out.buses.back().gains.emplace_back(key, std::clamp(g, 0.0f, 4.0f));
            break;
        }
        }
    }

    // Validação cruzada.
    for (const auto& s : out.sources) {
        if ((s.type == SourceType::App || s.type == SourceType::AppExclude) && s.exe.empty()) {
            error = "fonte '" + s.name + "' precisa de exe =";
            return false;
        }
    }
    for (const auto& b : out.buses) {
        for (const auto& g : b.gains) {
            const bool found = std::any_of(out.sources.begin(), out.sources.end(),
                [&](const SourceConfig& s) { return lower(s.name) == g.first; });
            if (!found) {
                error = "bus '" + b.name + "' usa a fonte inexistente '" + g.first + "'";
                return false;
            }
        }
    }
    if (out.buses.empty()) {
        error = "nenhum [bus] definido";
        return false;
    }
    return true;
}

} // namespace novaon
