#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <iostream>
#include <TlHelp32.h>
#include <cstdint>
#include <algorithm>

class PatternScanner {
public:
    static uintptr_t FindPattern(HANDLE hProcess, uintptr_t moduleBase, DWORD moduleSize, const std::string& signature) {
        std::vector<int> patternBytes = ParseSignature(signature);
        
        // Cargamos todo el módulo en nuestra memoria para que el escaneo sea mucho más rápido.
        // El ejecutable del juego (FSD-Win64-Shipping.exe) es grande, pero cabe de sobra en la RAM (unos 80-100MB).
        // Leer por trozos sería eterno debido a la sobrecarga de ReadProcessMemory y al lío de manejar los bordes.
        std::vector<uint8_t> buffer(moduleSize);
        SIZE_T bytesRead = 0;
        
        if (!ReadProcessMemory(hProcess, (LPCVOID)moduleBase, buffer.data(), moduleSize, &bytesRead) || bytesRead == 0) {
            return 0;
        }

        for (size_t i = 0; i < bytesRead - patternBytes.size(); i++) {
            bool found = true;
            for (size_t k = 0; k < patternBytes.size(); k++) {
                if (patternBytes[k] != -1 && patternBytes[k] != buffer[i + k]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return moduleBase + i;
            }
        }
        return 0;
    }

    static uintptr_t ResolveRipRelative(HANDLE hProcess, uintptr_t instructionAddress, int offsetToDisplacement, int instructionSize) {
        int32_t displacement = 0;
        if (ReadProcessMemory(hProcess, (LPCVOID)(instructionAddress + offsetToDisplacement), &displacement, sizeof(displacement), NULL)) {
            return instructionAddress + instructionSize + displacement;
        }
        return 0;
    }

    static DWORD GetModuleSize(DWORD pid, const std::wstring& moduleName) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (hSnap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me;
            me.dwSize = sizeof(me);
            if (Module32FirstW(hSnap, &me)) {
                do {
                    if (moduleName == me.szModule) {
                        CloseHandle(hSnap);
                        return me.modBaseSize;
                    }
                } while (Module32NextW(hSnap, &me));
            }
            CloseHandle(hSnap);
        }
        return 0;
    }

private:
    static std::vector<int> ParseSignature(const std::string& signature) {
        std::vector<int> bytes;
        std::string current;
        for (char c : signature) {
            if (c == ' ') continue;
            if (c == '?') {
                if (!current.empty()) {
                    bytes.push_back(std::stoi(current, nullptr, 16));
                    current.clear();
                }
                bytes.push_back(-1);
            } else {
                current += c;
                if (current.length() == 2) {
                    bytes.push_back(std::stoi(current, nullptr, 16));
                    current.clear();
                }
            }
        }
        if (!current.empty()) {
             bytes.push_back(std::stoi(current, nullptr, 16));
        }
        return bytes;
    }
};
