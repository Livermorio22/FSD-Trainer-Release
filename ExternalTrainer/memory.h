#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <string>
// El handle del proceso que vamos a manipular globalmente
extern HANDLE hProcess;
// Función para buscar el ID de un proceso por su nombre (ej: "Juego.exe")
DWORD GetProcessId(const std::wstring& processName) {
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (processName == pe.szExeFile) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return pid;
}
// Para encontrar la dirección base de un módulo (el .exe o una .dll) dentro del proceso
uintptr_t GetModuleBaseAddress(DWORD pid, const std::wstring& moduleName) {
    uintptr_t modBase = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me;
        me.dwSize = sizeof(me);
        if (Module32FirstW(hSnap, &me)) {
            do {
                if (moduleName == me.szModule) {
                    modBase = (uintptr_t)me.modBaseAddr;
                    break;
                }
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }
    return modBase;
}

template <typename T>
T ReadMemory(uintptr_t address) {
    T value;
    ReadProcessMemory(hProcess, (LPCVOID)address, &value, sizeof(T), NULL);
    return value;
}

// Función auxiliar para cuando necesitemos leer un bloque de datos grande de un tirón
inline bool ReadMemoryBlock(uintptr_t address, void* buffer, size_t size) {
    return ReadProcessMemory(hProcess, (LPCVOID)address, buffer, size, NULL);
}
// Lo mismo que el de lectura, pero para escribir o parchear valores en la memoria
template <typename T>
void WriteMemory(uintptr_t address, T value) {
    WriteProcessMemory(hProcess, (LPVOID)address, &value, sizeof(T), NULL);
}
