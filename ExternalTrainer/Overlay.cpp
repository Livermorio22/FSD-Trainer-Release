#include "Overlay.h"
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI Overlay::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            // Manejar el redimensionado si hace falta, por ahora nos ajustamos al tamaño de la ventana del juego
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) //  Desactivar el menú de la aplicación al pulsar ALT
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool Overlay::Initialize(HWND gameWindow) {
    hGameWindow = gameWindow;

    // Obtenemos el tamaño de la ventana del juego
    RECT rect;
    GetClientRect(hGameWindow, &rect);
    screenWidth = rect.right - rect.left;
    screenHeight = rect.bottom - rect.top;

    // Registramos la clase de la ventana para nuestro overlay
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, className, NULL };
    RegisterClassExW(&wc);

    // Creamos la ventana del overlay (siempre encima, transparente y que ignore clics si no hace falta)
    hWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW, 
        className, windowName, 
        WS_POPUP, 
        0, 0, screenWidth, screenHeight, 
        NULL, NULL, wc.hInstance, NULL
    );

    if (!hWindow) return false;

    // transparencia del fondo
    SetLayeredWindowAttributes(hWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);
    SetLayeredWindowAttributes(hWindow, 0, 255, LWA_ALPHA);

    // Extendemos el marco al área cliente usando DWM
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hWindow, &margins);

    // Arrancamos DirectX
    if (!CreateDeviceD3D(hWindow)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // mostramos la ventana
    ShowWindow(hWindow, SW_SHOWDEFAULT);
    UpdateWindow(hWindow);

    // Inicializacion de ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hWindow);
    ImGui_ImplDX11_Init(pd3dDevice, pd3dDeviceContext);

    return true;
}

void Overlay::Render(const std::vector<Entity>& entities) {
    // procesamiento de los mensajes de la ventana
    MSG msg;
    while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) isRunning = false;
    }

    // seguimiento de la posicion de nuestro overlay para que siga la posición de la ventana del juego
    RECT rect;
    GetClientRect(hGameWindow, &rect);
    POINT pt = { 0, 0 };
    ClientToScreen(hGameWindow, &pt);
    MoveWindow(hWindow, pt.x, pt.y, rect.right - rect.left, rect.bottom - rect.top, TRUE);
    screenWidth = rect.right - rect.left;
    screenHeight = rect.bottom - rect.top;

    // Iniciamos el frame de ImGui
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Dibujamos ESP
    if (bEspEnabled) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        
        for (const auto& entity : entities) {
             // Dejamos que se dibuje aunque esté fuera de pantalla (ImGui ya lo recorta) para que los enemigos cercanos se vean 
            
            if (entity.screenPos.x == -1000.0f) continue; 

            // NOTA: Quité la comprobación de muerte porque hacía que enemigos vivos con 0 HP leídos por error desaparecieran.
            // Calculamos el tamaño de la caja basándonos en la distancia


            float dist = entity.distance;
            if (dist < 1.0f) dist = 1.0f; 

            // Fórmula: AlturaProyectada = (AlturaMundo * FactorEscala) / Distancia
            // Usamos un factor de ~10.0f porque queda bien visualmente para objetos de unos 2 metros
            float boxHeight = (entity.height * 10.0f) / dist; 
            float boxWidth = (entity.radius * 2.0f * 10.0f) / dist;
            if (boxHeight > 500) boxHeight = 500; 
            if (boxHeight < 5) boxHeight = 5;
            if (boxWidth > 500) boxWidth = 500;
            if (boxWidth < 5) boxWidth = 5;

            float x = entity.screenPos.x - boxWidth / 2;
            float y = entity.screenPos.y - boxHeight / 2;

            ImU32 color = entity.isPlayer ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
            
            if (bEspBox)
                drawList->AddRect(ImVec2(x, y), ImVec2(x + boxWidth, y + boxHeight), color);
            
            if (bEspSnaplines)
                drawList->AddLine(ImVec2(screenWidth / 2, screenHeight), ImVec2(entity.screenPos.x, entity.screenPos.y), color);

            if (bEspNames)
                drawList->AddText(ImVec2(x, y - 15), IM_COL32(255, 255, 255, 255), entity.name.c_str());

            if (bEspHealth && entity.health > 0) {
                char hpText[32];
                sprintf(hpText, "HP: %.0f", entity.health);
                drawList->AddText(ImVec2(x, y + boxHeight + 5), IM_COL32(255, 255, 255, 255), hpText);
            }
        }
    }

    // menú de configuración
    if (showMenu) {
        ImGui::Begin("Deep Rock Galactic Trainer", &showMenu);
        ImGui::Checkbox("ESP Enabled", &bEspEnabled);
        if (bEspEnabled) {
            ImGui::Checkbox("Box", &bEspBox);
            ImGui::Checkbox("Snaplines", &bEspSnaplines);
            ImGui::Checkbox("Names", &bEspNames);
            ImGui::Checkbox("Health", &bEspHealth);
        }
        ImGui::End();
    }

    // Renderizado final
    ImGui::Render();
    const float clear_color_with_alpha[4] = { 0, 0, 0, 0 }; // Transparent
    pd3dDeviceContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    pd3dDeviceContext->ClearRenderTargetView(mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    pSwapChain->Present(0, 0); 
}

void Overlay::HandleInput() {
    if (GetAsyncKeyState(VK_INSERT) & 1) {
        showMenu = !showMenu;
        
        // si el menú está abierto, la ventana captura el ratón
        LONG_PTR style = GetWindowLongPtr(hWindow, GWL_EXSTYLE);
        if (showMenu) {
            style &= ~WS_EX_TRANSPARENT;
            SetWindowLongPtr(hWindow, GWL_EXSTYLE, style);
            SetForegroundWindow(hWindow);
        } else {
            style |= WS_EX_TRANSPARENT;
            SetWindowLongPtr(hWindow, GWL_EXSTYLE, style);
            SetForegroundWindow(hGameWindow);
        }
    }
}

void Overlay::Cleanup() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWindow);
    UnregisterClassW(className, GetModuleHandle(NULL));
}

// directX Boilerplate
bool Overlay::CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &pSwapChain, &pd3dDevice, &featureLevel, &pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void Overlay::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (pSwapChain) { pSwapChain->Release(); pSwapChain = NULL; }
    if (pd3dDeviceContext) { pd3dDeviceContext->Release(); pd3dDeviceContext = NULL; }
    if (pd3dDevice) { pd3dDevice->Release(); pd3dDevice = NULL; }
}

void Overlay::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
    pBackBuffer->Release();
}

void Overlay::CleanupRenderTarget() {
    if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = NULL; }
}
