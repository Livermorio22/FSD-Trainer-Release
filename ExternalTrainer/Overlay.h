#pragma once
#include <d3d11.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "GameData.h"

class Overlay {
private:
    // Datos básicos de la ventana (handles, resolución y nombres)
    HWND hWindow = NULL;
    HWND hGameWindow = NULL;
    int screenWidth = 1920;
    int screenHeight = 1080;
    const wchar_t* windowName = L"DRG Overlay";
    const wchar_t* className = L"DRG Overlay Class";

    // directX11 
    ID3D11Device* pd3dDevice = NULL;
    ID3D11DeviceContext* pd3dDeviceContext = NULL;
    IDXGISwapChain* pSwapChain = NULL;
    ID3D11RenderTargetView* mainRenderTargetView = NULL;

    // rutinas internas para configurar y limpiar el motor grafico
    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    // El gestor de eventos de la ventana (mensajes de Windows)
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

public:
    // general overlay
    bool isRunning = true;
    bool showMenu = true;
    
    // ESP Configuración
    bool bEspEnabled = true;
    bool bEspBox = true;
    bool bEspSnaplines = false;
    bool bEspNames = true;
    bool bEspHealth = true;
    float espDistance = 10000.0f;

    // Funciones principales para el ciclo de vida del overlay
    bool Initialize(HWND gameWindow);
    void Render(const std::vector<Entity>& entities);
    void Cleanup();
    void HandleInput();
};
