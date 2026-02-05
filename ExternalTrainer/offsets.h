#pragma once
#include <string>

// Firmas para Deep Rock Galactic (Steam)
// Generadas a mano para que tengan la misma calidad que las de "SigMaker/LazyIDA"
// Usamos '?' para los offsets que cambian entre actualizaciones o reinicios (por el ASLR)

// Firma de GEngine
// Código: 48 8B 0D ? ? ? ? 49 8B D7 48 8B 01 (mov rcx, [rip+offset]; mov rdx, r15; mov rax, [rcx])
// Se encuentra en: FEngineLoop::Init (o en un código de inicialización parecido)
// Verificado como resultado único en 0x1408484D9
const std::string SIG_GENGINE = "48 8B 0D ? ? ? ? 49 8B D7 48 8B 01 FF 90 ? ? ? ? 48 8D 4D 40";

// Firma de GWorld
// Se suele encontrar buscando "mov rax, [rip+offset]" cerca de la cadena "World" o métodos de UWorld
// De momento es solo un marcador; usar GEngine -> GameViewport -> World es el camino más fiable
// const std::string SIG_GWORLD = "48 8B 05 ? ? ? ? 48 3B C? 48 0F 44 C?"; 

// Offsets dentro de la propia instrucción
const int GENGINE_OFFSET_TO_DISPLACEMENT = 3; // 48 8B 0D [El desplazamiento empieza aquí]
const int GENGINE_INSTRUCTION_SIZE = 7; // Tamaño total de la instrucción "mov rcx, [rip+offset]"

// Offsets de las Estructuras (Estos suelen ser estables en actualizaciones menores)
// UEngine
constexpr uintptr_t OFFSET_GAME_VIEWPORT = 0x780; // UGameViewportClient* GameViewport;

// UGameViewportClient
constexpr uintptr_t OFFSET_VIEWPORT_WORLD = 0x78; // UWorld* World;

// UWorld
constexpr uintptr_t OFFSET_PERSISTENT_LEVEL = 0x30;
constexpr uintptr_t OFFSET_GAME_INSTANCE = 0x180; // Corregido tras un escaneo de debug (antes era 0x1B8)

// ULevel
constexpr uintptr_t OFFSET_ACTOR_ARRAY = 0x98; // TArray<AActor*> Actors;
constexpr uintptr_t OFFSET_ACTOR_COUNT = 0xA0; // int32_t ActorCount;

// AActor
constexpr uintptr_t OFFSET_ACTOR_ID = 0x18; // int32_t ID (para comparar con FName)
constexpr uintptr_t OFFSET_ROOT_COMPONENT = 0x130; // USceneComponent* RootComponent

// UGameInstance
constexpr uintptr_t OFFSET_LOCAL_PLAYERS = 0x38;

// ULocalPlayer
constexpr uintptr_t OFFSET_PLAYER_CONTROLLER = 0x30;

// APlayerController
constexpr uintptr_t OFFSET_ACKNOWLEDGED_PAWN = 0x330; // puede ser 0x330 o 0x338
constexpr uintptr_t OFFSET_CONTROLLER_PAWN = 0x2A0; // o 0x2A8 (APlayerController -> Pawn)
constexpr uintptr_t OFFSET_CONTROL_ROTATION = 0x288; // FRotator ControlRotation
constexpr uintptr_t OFFSET_PLAYER_CAMERA_MANAGER = 0x340; // Suele ser 0x340 en versiones 4.26+

// APlayerCameraManager
constexpr uintptr_t OFFSET_CAMERA_CACHE = 0x1AE0; // FCameraCacheEntry CameraCachePrivate

// FCameraCacheEntry
constexpr uintptr_t OFFSET_POV = 0x10; // FMinimalViewInfo POV

// USceneComponent
constexpr uintptr_t OFFSET_RELATIVE_LOCATION = 0x11C; // FVector RelativeLocation
