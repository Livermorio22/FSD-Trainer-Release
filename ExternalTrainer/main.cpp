#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include "memory.h"
#include "offsets.h"
#include "scanner.h"
#include "Overlay.h"
#include "GameData.h"

// Global variables for game process
DWORD pid = 0;
HANDLE hProcess = NULL;
uintptr_t moduleBase = 0;
DWORD moduleSize = 0;

// Global window finder
HWND g_hGameWindow = NULL;
int g_Width = 1600;
int g_Height = 900;

// Multithreading globals
std::mutex entitiesMutex;
std::vector<Entity> sharedEntities;
std::atomic<bool> shouldExit(false);

// Cached Pointers for Thread
uintptr_t g_GEngineGlobalAddr = 0; // Added for refreshing
uintptr_t g_GWorld = 0;
uintptr_t g_GNames = 0;
uintptr_t g_PersistentLevel = 0;

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));
    if (strstr(title, "Deep Rock Galactic") != NULL) {
        RECT r;
        if (GetClientRect(hwnd, &r)) {
            int w = r.right - r.left;
            int h = r.bottom - r.top;
            if (w > 800) { // Filter out small launcher windows/hidden windows
                g_hGameWindow = hwnd;
                g_Width = w;
                g_Height = h;
                return FALSE; // Found the main window
            }
        }
    }
    return TRUE;
}

// Helper to validate a pointer (check if it points to valid module memory)
bool IsValidCodePointer(uintptr_t ptr) {
    return (ptr >= moduleBase && ptr < moduleBase + moduleSize);
}

// --- FNamePool Decoding Helper ---
std::string GetNameFromFName(uint32_t key, uintptr_t gNamesAddr) {
    if (gNamesAddr == 0) return "NULL_GNAMES";

    uint32_t chunkOffset = key >> 16;
    uint16_t nameOffset = (uint16_t)key;

    uintptr_t namePoolChunk = ReadMemory<uintptr_t>(gNamesAddr + 0x10 + (chunkOffset * 8));
    if (namePoolChunk == 0) return "NULL_CHUNK";

    uintptr_t entryOffset = namePoolChunk + (2 * nameOffset);
    
    uint16_t nameHeader = ReadMemory<uint16_t>(entryOffset);
    int len = nameHeader >> 6;
    
    if (len <= 0 || len > 250) return "INVALID_LEN";

    bool isWide = (nameHeader & 1) != 0; 
    
    if (isWide) {
        return "WIDE_CHAR_TODO"; 
    } else {
        char buffer[256];
        if (ReadProcessMemory(hProcess, (LPCVOID)(entryOffset + 2), buffer, len, NULL)) {
            buffer[len] = '\0';
            return std::string(buffer);
        }
    }
    return "READ_FAIL";
}

// Helper to get robust actor location
Vector3 GetActorLocation(uintptr_t actor) {
    Vector3 loc = {0,0,0};
    
    // Attempt 1: Root Component
    uintptr_t rootComp = ReadMemory<uintptr_t>(actor + OFFSET_ROOT_COMPONENT);
    if (rootComp != 0) {
        // Attempt 1.1: ComponentToWorld (Absolute Location) at 0x1D0 + 0x10 (Translation) = 0x1E0
        // FTransform: Rotation(16) + Translation(12) + Scale(12)
        Vector3 absLoc = ReadMemory<Vector3>(rootComp + 0x1E0);
        
        // Sanity Check: If Absolute Location looks valid (not 0,0,0 and not huge), use it.
        if ((std::abs(absLoc.x) > 1.0f || std::abs(absLoc.y) > 1.0f) && 
            std::abs(absLoc.x) < 500000.0f) {
            return absLoc;
        }

        // Attempt 1.2: RelativeLocation (0x11C or 0x128) - Fallback
        loc = ReadMemory<Vector3>(rootComp + OFFSET_RELATIVE_LOCATION);
        bool isSuspicious = (std::abs(loc.x) < 1.0f && std::abs(loc.y) < 1.0f && std::abs(loc.z) < 1.0f);
        if (!isSuspicious) return loc;
    }

    // Attempt 2: Search for a Mesh Component and read its Absolute Location (0x1E0)
    int meshOffsets[] = { 0x280, 0x288, 0x290, 0x298, 0x2A0 };
    for (int offset : meshOffsets) {
        uintptr_t meshComp = ReadMemory<uintptr_t>(actor + offset);
        if (meshComp > 0x1000000 && meshComp < 0x7FFFFFFFFFFF) {
            // Try Absolute Location (0x1E0)
            Vector3 meshAbsLoc = ReadMemory<Vector3>(meshComp + 0x1E0);
            if (std::abs(meshAbsLoc.x) > 100.0f || std::abs(meshAbsLoc.y) > 100.0f) {
                return meshAbsLoc;
            }

            // Fallback to RelativeLocation (0x11C)
            Vector3 meshRelLoc = ReadMemory<Vector3>(meshComp + OFFSET_RELATIVE_LOCATION);
            if (std::abs(meshRelLoc.x) > 100.0f || std::abs(meshRelLoc.y) > 100.0f) {
                return meshRelLoc;
            }
        }
    }
    
    // Attempt 3: AttachChildren (Fallback)
    if (rootComp != 0) {
        uintptr_t attachChildrenArray = ReadMemory<uintptr_t>(rootComp + 0x140); 
        int32_t attachChildrenCount = ReadMemory<int32_t>(rootComp + 0x148);

        if (attachChildrenCount > 0 && attachChildrenCount < 100 && attachChildrenArray > 0x1000000) {
             for (int i=0; i < attachChildrenCount; i++) {
                  uintptr_t child = ReadMemory<uintptr_t>(attachChildrenArray + i*8);
                  if (child == 0) continue;
                  
                  Vector3 childLoc = ReadMemory<Vector3>(child + OFFSET_RELATIVE_LOCATION);
                  if (std::abs(childLoc.x) > 100.0f || std::abs(childLoc.y) > 100.0f) {
                      return childLoc; 
                  }
                  
                  // Also try Absolute
                  Vector3 childAbs = ReadMemory<Vector3>(child + 0x1E0);
                  if (std::abs(childAbs.x) > 100.0f || std::abs(childAbs.y) > 100.0f) {
                      return childAbs;
                  }
             }
        }
    }
    
    return loc;
}

// Helper to find Health Component and read Health
float GetActorHealth(uintptr_t actor, uintptr_t gNames) {
    uintptr_t arrayPtr = ReadMemory<uintptr_t>(actor + 0x190);
    int32_t count = ReadMemory<int32_t>(actor + 0x198);
    
    if (count > 0 && count < 50 && arrayPtr > 0x1000000) {
        for (int i=0; i<count; i++) {
            uintptr_t comp = ReadMemory<uintptr_t>(arrayPtr + i*8);
            if (comp == 0) continue;
            
            uint32_t id = ReadMemory<uint32_t>(comp + OFFSET_ACTOR_ID);
            std::string cName = GetNameFromFName(id, gNames);
            
            if (cName.find("Health") != std::string::npos) {
                float health = ReadMemory<float>(comp + 0x160);
                return health; 
            }
        }
    }
    return -1.0f; // Unknown/No Health
}

// Global for robust LocalPawn detection
uintptr_t g_LocalPawn = 0;
// Pass PC to GameLogicLoop via global (hacky but effective for this structure)
static uintptr_t s_PlayerController = 0;

// --- Background Thread for Actor Reading ---
void GameLogicLoop() {
    std::cout << "[Thread] GameLogicLoop started." << std::endl;
    
    // Cache for Name IDs
    std::map<uint32_t, std::string> nameCache;
    
    // PERSISTENT ENTITY MAP (Anti-Flicker)
    // Key: Actor Address, Value: Entity
    std::unordered_map<uintptr_t, Entity> persistentEntitiesMap;
    std::unordered_map<uintptr_t, int> entityMissingCount; // Frames missing

    int loopCounter = 0;

    while (!shouldExit) {
        loopCounter++;

        // REFRESH POINTERS (Critical for Level Changes)
        if (loopCounter % 10 == 0) { // Every 10 iterations (~100ms)
            uintptr_t gEngine = ReadMemory<uintptr_t>(g_GEngineGlobalAddr);
            if (gEngine) {
                uintptr_t gameViewport = ReadMemory<uintptr_t>(gEngine + OFFSET_GAME_VIEWPORT);
                if (gameViewport) {
                    uintptr_t world = ReadMemory<uintptr_t>(gameViewport + OFFSET_VIEWPORT_WORLD);
                    if (world) {
                        g_GWorld = world;
                        g_PersistentLevel = ReadMemory<uintptr_t>(world + OFFSET_PERSISTENT_LEVEL);
                    }
                }
            }
        }

        if (g_GWorld == 0 || g_PersistentLevel == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Reset LocalPawn at start of each scan to ensure freshness
        // g_LocalPawn = 0; // Don't zero it blindly, let it persist but overwrite if found.
        
        uintptr_t currentFrameLocalPawn = 0;

        // Local list for this frame
        std::vector<Entity> localEntities;
        std::unordered_map<uintptr_t, bool> visitedAddresses; // Address -> IsEnemy

        uintptr_t actorArray = ReadMemory<uintptr_t>(g_PersistentLevel + OFFSET_ACTOR_ARRAY);
        int32_t actorCount = ReadMemory<int32_t>(g_PersistentLevel + OFFSET_ACTOR_COUNT);
        
        // DEBUG: Print count periodically
        if (loopCounter % 100 == 0) {
            std::cout << "[Debug] ActorCount: " << actorCount << " | Array: " << std::hex << actorArray << std::dec << std::endl;
        }

        // Safety limit (DRG usually has < 2000 actors)
        if (actorCount > 2500) {
             if (loopCounter % 100 == 0) std::cout << "[Warning] ActorCount " << actorCount << " > 2500. Clamping to 2500 to prevent lag." << std::endl;
             actorCount = 2500;
        }

        // BATCH READ OPTIMIZATION: Read all actor pointers in one go
        std::vector<uintptr_t> actorPointers(actorCount);
        if (actorCount > 0) {
            ReadMemoryBlock(actorArray, actorPointers.data(), actorCount * sizeof(uintptr_t));
        }

        // Process Actors
        for (int i = 0; i < actorCount; i++) {
            if (shouldExit) break; // Exit fast

            // Use batch read value instead of single read
            uintptr_t actorObj = actorPointers[i];
            if (actorObj == 0) continue;

            // OPTIMIZATION: Read ID first
            int32_t actorID = ReadMemory<int32_t>(actorObj + OFFSET_ACTOR_ID);
            
            // Check Cache: Address + ID must match
            // If we have a cached entry for this address, and the ID matches, we reuse the name.
            // If ID differs, it means the actor at this address changed (memory reuse).
            std::string cName = "Unknown";
            
            // Simple struct for cache
            struct CachedActorInfo {
                int32_t id;
                std::string name;
            };
            static std::unordered_map<uintptr_t, CachedActorInfo> fastActorCache;

            bool cacheHit = false;
            auto it = fastActorCache.find(actorObj);
            if (it != fastActorCache.end()) {
                if (it->second.id == actorID) {
                    cName = it->second.name;
                    cacheHit = true;
                }
            }

            if (!cacheHit) {
                // Name resolution is slow, do it only on cache miss
                cName = GetNameFromFName(actorID, g_GNames);
                if (cName != "INVALID_LEN" && cName != "READ_FAIL" && cName != "NULL_CHUNK") {
                    fastActorCache[actorObj] = { actorID, cName };
                }
            }
            
            // DEBUG: DUMP ALL NAMES ONCE
            if (loopCounter == 300) {
                 /*
                 std::cout << "[Debug] DUMPING ALL ACTOR NAMES:" << std::endl;
                 std::cout << "Index | Name" << std::endl;
                 std::cout << "----------------" << std::endl;
                 std::cout << i << " | " << cName << std::endl; // This will print for every i in this loop
                 */
            } else if (loopCounter % 200 == 0 && i < 5) {
                 std::cout << "[Debug] Actor[" << i << "] Name: " << cName << std::endl;
            }

            // DEBUG: Scan specifically for enemy-like names occasionally to verify filter
            if (loopCounter % 500 == 0) {
                 if (cName.find("Spider") != std::string::npos || 
                     cName.find("Pawn") != std::string::npos || 
                     cName.find("Enemy") != std::string::npos ||
                     cName.find("Character") != std::string::npos) {
                      std::cout << "[Debug-Scan] Found potential enemy/player: " << cName << " at index " << i 
                                << " Addr: 0x" << std::hex << actorObj << std::dec << std::endl;
                 }
            }

            // Filter interesting actors
            // BROADEN FILTER FOR DEBUGGING: "Spider" might be case sensitive or have prefix
            bool isPlayer = (cName.find("PlayerCharacter") != std::string::npos) ||
                            (cName.find("BP_DrillerCharacter") != std::string::npos) ||
                            (cName.find("BP_GunnerCharacter") != std::string::npos) ||
                            (cName.find("BP_EngineerCharacter") != std::string::npos) ||
                            (cName.find("BP_ScoutCharacter") != std::string::npos) ||
                            (cName.find("BP_NavigatorCharacter") != std::string::npos) || // Scout Class
                            (cName.find("BP_Bosco") != std::string::npos) ||       // Drone Ally
                            (cName.find("Drone") != std::string::npos) ||          // Generic Drone
                            (cName.find("BeastMaster") != std::string::npos);      // Steve (Tamed Glyphid)
            
            // Try to detect LocalPawn by matching Controller
            // ALWAYS update if we find a match, to ensure we have the latest alive pawn
            if (isPlayer && s_PlayerController != 0) {
                // Scan for Controller pointer in this actor
                // UE4 Pawn->Controller is usually around 0x250-0x300
                for (int off = 0x250; off <= 0x320; off += 8) {
                    if (ReadMemory<uintptr_t>(actorObj + off) == s_PlayerController) {
                        currentFrameLocalPawn = actorObj;
                        break;
                    }
                }
            }

            bool isEnemy = (cName.find("Spider") != std::string::npos) || 
                           (cName.find("Enemy") != std::string::npos) || 
                           (cName.find("Pawn") != std::string::npos) ||
                           (cName.find("Glyphid") != std::string::npos) ||
                           (cName.find("Mactera") != std::string::npos) ||
                           (cName.find("Bug") != std::string::npos) ||
                           (cName.find("Infected") != std::string::npos) || // Rockpox
                           (cName.find("PatrolBot") != std::string::npos) || // Robots
                           (cName.find("Shredder") != std::string::npos) || // Drones
                           (cName.find("ENE_") != std::string::npos) || // Universal Enemy Prefix
                           (cName.find("CaveLeech") != std::string::npos) || 
                           (cName.find("Hydra") != std::string::npos) ||
                           (cName.find("BP_") != std::string::npos && cName.find("Enemy") != std::string::npos) ||
                           (cName.find("DeepPathfinderCharacter") != std::string::npos);

            // EXCLUSIONS: Remove dead parts, legs, and non-threats
            if (cName.find("Leg") != std::string::npos || 
                cName.find("Corpse") != std::string::npos || 
                cName.find("Gib") != std::string::npos ||
                cName.find("Dead") != std::string::npos ||
                cName.find("Web") != std::string::npos ||
                cName.find("Maggot") != std::string::npos ||   // Ambient Maggots
                cName.find("LootBug") != std::string::npos ||  // Passive LootBugs (User requested cleanup)
                cName.find("Critter") != std::string::npos) {  // Other small critters
                isEnemy = false;
            }

            // Track that we visited this address and whether it is an enemy
            visitedAddresses[actorObj] = (isPlayer || isEnemy);

            if (isPlayer || isEnemy) {
                // IMPORTANT: We only store the address here. The position will be read in the render loop for max freshness.
                // This eliminates the "lag" where the box trails behind the enemy.
                
                Entity e;
                e.baseAddress = actorObj;
                e.name = cName;
                e.isPlayer = isPlayer;
                e.isEnemy = isEnemy;
                e.location = {0, 0, 0}; // Placeholder, read in Render Loop
                
                // Read Health only if close/relevant (Optimization) - Health doesn't change as fast as position
                // e.health = GetActorHealth(actorObj, g_GNames); 
                e.health = 100.0f; // Default to 100 since offset is unreliable currently

                // DEBUG PRINT
                if (loopCounter % 600 == 0) { // Don't spam too much, but show some
                     std::cout << "[MATCH] Name: " << cName << std::endl;
                }

                // DEBUG SCANNER FOR BOX EXTENT (Single run Dump for analysis)
                static bool hasDumped = false;
                // Scan Praetorians, Oppressors, or Dreadnoughts (Big enemies)
                if (!hasDumped && (cName.find("Praetorian") != std::string::npos || 
                                   cName.find("Oppressor") != std::string::npos || 
                                   cName.find("Dreadnought") != std::string::npos ||
                                   cName.find("Spider_Tank") != std::string::npos || 
                                   cName.find("Tank") != std::string::npos)) { 
                    
                    uintptr_t rootComp = ReadMemory<uintptr_t>(actorObj + 0x130);
                    if (rootComp != 0) {
                        hasDumped = true; // Mark as done immediately
                        std::cout << "\n[DUMP] Raw float dump for " << cName << " (Root: 0x" << std::hex << rootComp << std::dec << ")" << std::endl;
                        std::cout << "Offset | Value" << std::endl;
                        std::cout << "-------|------" << std::endl;
                        
                        // Dump 0x80 to 0x300
                        for (int i = 0x80; i < 0x300; i += 4) { 
                             float f1 = ReadMemory<float>(rootComp + i);
                             // Filter extremely small/large values to reduce noise (keep only plausible sizes)
                             // Keep values between 10.0 and 10000.0 (and negatives)
                             if ((f1 > 10.0f && f1 < 10000.0f) || (f1 < -10.0f && f1 > -10000.0f)) {
                                  std::cout << "0x" << std::hex << i << std::dec << " : " << f1 << std::endl;
                             }
                        }
                        std::cout << "[DUMP] End of dump." << std::endl;
                    }
                }

                localEntities.push_back(e);
            }
        }

        // MERGE LOGIC (Anti-Flicker)
        // 1. Mark all existing as potentially missing
        for (auto& pair : entityMissingCount) {
            pair.second++;
        }

        // 2. Remove entities that were visited but are no longer valid enemies (e.g. turned into Corpse)
        for (auto it = persistentEntitiesMap.begin(); it != persistentEntitiesMap.end(); ) {
            auto visitedIt = visitedAddresses.find(it->first);
            if (visitedIt != visitedAddresses.end()) {
                if (visitedIt->second == false) {
                    // Visited, but not an enemy anymore (Corpse/Dead) -> REMOVE IMMEDIATELY
                    entityMissingCount.erase(it->first);
                    it = persistentEntitiesMap.erase(it);
                    continue; 
                }
            }
            ++it;
        }

        // 3. Update/Insert current frame entities
        for (const auto& e : localEntities) {
            persistentEntitiesMap[e.baseAddress] = e;
            entityMissingCount[e.baseAddress] = 0; // Reset missing count
        }

        // 4. Prune old entities (missing for > 15 frames / ~0.25s)
        for (auto it = persistentEntitiesMap.begin(); it != persistentEntitiesMap.end(); ) {
            if (entityMissingCount[it->first] > 15) { // Reduced from 60 to 15 to fix ghosting
                 entityMissingCount.erase(it->first);
                 it = persistentEntitiesMap.erase(it);
            } else {
                 ++it;
            }
        }

        // 5. SAFETY NET: If we lost ALL entities suddenly (ActorCount=0 glitch), 
        // keep the old shared list for a moment (handled by persistentMap automatically via missingCount)
        // BUT if persistentMap is empty, we must clear sharedEntities.
        
        // Update Global Pawn SAFELY
        if (currentFrameLocalPawn != 0) {
            g_LocalPawn = currentFrameLocalPawn;
        }

        // Update shared entities
        {
            std::lock_guard<std::mutex> lock(entitiesMutex);
            // Convert map to vector
            std::vector<Entity> exportList;
            exportList.reserve(persistentEntitiesMap.size());
            for (const auto& kv : persistentEntitiesMap) {
                exportList.push_back(kv.second);
            }
            sharedEntities = exportList;
        }

        // Sleep a bit to not burn CPU (Reduced to 1ms for better sync)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "Deep Rock Galactic External Trainer v2.0 (Multithreaded)" << std::endl;
    std::cout << "Waiting for FSD-Win64-Shipping.exe..." << std::endl;

    while (pid == 0) {
        pid = GetProcessId(L"FSD-Win64-Shipping.exe");
        Sleep(1000);
    }

    std::cout << "Process found! PID: " << pid << std::endl;
    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "Failed to open process." << std::endl;
        return 1;
    }

    moduleBase = GetModuleBaseAddress(pid, L"FSD-Win64-Shipping.exe");
    moduleSize = PatternScanner::GetModuleSize(pid, L"FSD-Win64-Shipping.exe");
    std::cout << "Module Base: 0x" << std::hex << moduleBase << std::endl;
    std::cout << "Module Size: 0x" << std::hex << moduleSize << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    // 1. Scan for GEngine Signature
    std::cout << "[STEP 1] Scanning for GEngine signature..." << std::endl;
    uintptr_t gEngineSigAddress = PatternScanner::FindPattern(hProcess, moduleBase, moduleSize, SIG_GENGINE);
    
    if (gEngineSigAddress == 0) {
        std::cerr << "[-] Failed to find GEngine signature!" << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    std::cout << "[+] Signature found at: 0x" << std::hex << gEngineSigAddress << std::endl;

    // 2. Resolve GEngine
    std::cout << "\n[STEP 2] Resolving GEngine Pointer..." << std::endl;
    g_GEngineGlobalAddr = PatternScanner::ResolveRipRelative(hProcess, gEngineSigAddress, GENGINE_OFFSET_TO_DISPLACEMENT, GENGINE_INSTRUCTION_SIZE);
    std::cout << "[+] GEngine Global Variable Address: 0x" << std::hex << g_GEngineGlobalAddr << std::endl;
    
    uintptr_t gEngine = ReadMemory<uintptr_t>(g_GEngineGlobalAddr);
    std::cout << "[+] GEngine Pointer Value: 0x" << std::hex << gEngine << std::endl;

    // 3. Traverse to GWorld
    std::cout << "\n[STEP 3] Traversing to GWorld..." << std::endl;
    if (gEngine == 0) {
        std::cerr << "[-] GEngine is NULL. Cannot proceed." << std::endl;
        return 1;
    }

    uintptr_t gameViewport = ReadMemory<uintptr_t>(gEngine + OFFSET_GAME_VIEWPORT);
    std::cout << "[+] GameViewport: 0x" << std::hex << gameViewport << std::endl;
    
    if (gameViewport == 0) {
        std::cerr << "[-] GameViewport is NULL. Game might be initializing or offset is wrong." << std::endl;
        return 1;
    } else {
        g_GWorld = ReadMemory<uintptr_t>(gameViewport + OFFSET_VIEWPORT_WORLD);
        std::cout << "[+] GWorld: 0x" << std::hex << g_GWorld << std::endl;

        if (g_GWorld != 0) {
            g_PersistentLevel = ReadMemory<uintptr_t>(g_GWorld + OFFSET_PERSISTENT_LEVEL);
            std::cout << "    -> PersistentLevel: 0x" << std::hex << g_PersistentLevel << std::endl;

            if (g_PersistentLevel != 0) {
                // --- STEP 4: GNames (Signature Scan) ---
                std::cout << "\n[STEP 4] Scanning for GNames..." << std::endl;

                std::string gNamesSig = "48 8B 05 ? ? ? ? 48 85 C0 75 ? 48 8D 15 ? ? ? ? 48 8B C8 E8";
                uintptr_t gNamesRef = PatternScanner::FindPattern(hProcess, moduleBase, moduleSize, gNamesSig);

                if (gNamesRef) {
                    int32_t offset = ReadMemory<int32_t>(gNamesRef + 3);
                    g_GNames = gNamesRef + 7 + offset;
                    std::cout << "[+] Found GNames Signature at: 0x" << std::hex << gNamesRef << std::endl;
                } else {
                    std::cout << "[-] Failed to find GNames signature. Trying fallback..." << std::endl;
                    std::string gNamesSig2 = "48 8D 35 ? ? ? ? EB 16";
                    gNamesRef = PatternScanner::FindPattern(hProcess, moduleBase, moduleSize, gNamesSig2);
                    if (gNamesRef) {
                         int32_t offset = ReadMemory<int32_t>(gNamesRef + 3);
                         g_GNames = gNamesRef + 7 + offset;
                         std::cout << "[+] Found GNames Signature (Fallback) at: 0x" << std::hex << gNamesRef << std::endl;
                    }
                }
                std::cout << "    -> GNames Global: 0x" << std::hex << g_GNames << std::endl;

                // --- INITIALIZE OVERLAY ---
                std::cout << "\n[STEP 5] Initializing Overlay..." << std::endl;
                Overlay overlay;
                
                // Wait for game window
                EnumWindows(EnumWindowsCallback, 0);
                while (g_hGameWindow == NULL) {
                    std::cout << "Waiting for game window..." << std::endl;
                    Sleep(1000);
                    EnumWindows(EnumWindowsCallback, 0);
                }

                if (!overlay.Initialize(g_hGameWindow)) {
                    std::cerr << "[-] Failed to initialize Overlay!" << std::endl;
                    return 1;
                }
                std::cout << "[+] Overlay Initialized!" << std::endl;

                // --- START DATA THREAD ---
                shouldExit = false;
                std::thread dataThread(GameLogicLoop);

                // --- MAIN RENDER LOOP ---
                static uintptr_t cachedCameraCacheOffset = 0;
                static uintptr_t cachedGameInstanceOffset = 0;
                
                while (overlay.isRunning) {
                    // Update Window Info
                    EnumWindows(EnumWindowsCallback, 0); 
                    
                    // Handle Overlay Input
                    overlay.HandleInput();

                    // --- READ CAMERA (Must be in Render Loop for smoothness) ---
                    FMinimalViewInfo camera;
                    bool cameraFound = false;
                    
                    // Resolve GameInstance & PCM
                    uintptr_t gameInstance = 0;
                    if (cachedGameInstanceOffset != 0) {
                         gameInstance = ReadMemory<uintptr_t>(g_GWorld + cachedGameInstanceOffset);
                    } else {
                         gameInstance = ReadMemory<uintptr_t>(g_GWorld + OFFSET_GAME_INSTANCE);
                    }

                    uintptr_t localPlayers = ReadMemory<uintptr_t>(gameInstance + OFFSET_LOCAL_PLAYERS);
                    uintptr_t localPlayer = ReadMemory<uintptr_t>(localPlayers);
                    uintptr_t playerController = ReadMemory<uintptr_t>(localPlayer + OFFSET_PLAYER_CONTROLLER);
                    
                    s_PlayerController = playerController;
                    uintptr_t playerCameraManager = 0;

                    // Define potential PCM offsets locally for robustness
                    uintptr_t potentialPCM1 = 0;
                    uintptr_t potentialPCM2 = 0;

                    if (playerController) {
                         // Read both common offsets
                         potentialPCM1 = ReadMemory<uintptr_t>(playerController + 0x340);
                         potentialPCM2 = ReadMemory<uintptr_t>(playerController + 0x348);
                         
                         // Default to the one defined in offsets, or the first valid one
                         playerCameraManager = (potentialPCM1 > 0) ? potentialPCM1 : potentialPCM2;
                    }

                    // Get Local Pawn for Self-Exclusion in ESP (Declared BEFORE usage in IsCameraValid)
                    // Use GLOBAL found pawn if available, otherwise fallback to Controller reading
                    uintptr_t localPawn = (g_LocalPawn != 0) ? g_LocalPawn : ReadMemory<uintptr_t>(playerController + 0x250); 
                    if (localPawn == 0) localPawn = ReadMemory<uintptr_t>(playerController + 0x2A0); // Controlled Pawn
                    
                    // DEBUG: Diagnose Camera Chain
                    static int camDebugCounter = 0;
                    
                    // Camera Persistence
                    static int cameraFailCount = 0;
                    static FMinimalViewInfo lastValidCamera;

                    camDebugCounter++;
                    
                    auto IsCameraValid = [&](const FMinimalViewInfo& cam) -> bool {
                         // 1. Check FOV
                         if (cam.FOV < 10.0f || cam.FOV > 160.0f) return false;
                         
                         // 2. Check Coordinates (Sanity)
                         if (std::abs(cam.Location.x) > 500000.0f || std::abs(cam.Location.y) > 500000.0f || std::abs(cam.Location.z) > 500000.0f) return false;
                         if (std::isnan(cam.Location.x)) return false;

                         // REJECT (0,0,0) or (0,0,Z) - Camera is almost never exactly at world origin X/Y in DRG
                         if (std::abs(cam.Location.x) < 10.0f && std::abs(cam.Location.y) < 10.0f) return false;

                         // DISTANCE CHECK FROM LOCAL PAWN (Robustness)
                         if (localPawn != 0) {
                             Vector3 pawnLoc = GetActorLocation(localPawn);
                             float dx = cam.Location.x - pawnLoc.x;
                             float dy = cam.Location.y - pawnLoc.y;
                             float dz = cam.Location.z - pawnLoc.z;
                             float dist = sqrt(dx*dx + dy*dy + dz*dz);
                             
                             // STRICTER CHECK: If camera is > 15 meters (1500 units) from pawn, it's likely a Lobby/Static camera.
                             // Active PlayerCamera is usually < 500 units from Pawn head.
                             // We reject it to force Fallback (PawnLoc + ControlRot) which is synced.
                             if (dist > 1500.0f) {
                                 // std::cout << "[CameraRejected] Dist: " << dist << " > 1500.0f" << std::endl;
                                 return false;
                             }
                         }

                         // 3. Check Rotation (Roll shouldn't be extreme usually)
                         // Normalize Roll to -180..180
                         float roll = cam.Rotation.z;
                         while (roll > 180.f) roll -= 360.f;
                         while (roll < -180.f) roll += 360.f;
                         
                         if (std::abs(roll) > 50.0f) return false; 

                         return true;
                    };

                    // Helper lambda to scan a PCM pointer
                    // 0. Try Cached Offset First (Fastest & Most Stable)
                    if (!cameraFound && cachedCameraCacheOffset != 0 && playerCameraManager != 0) {
                        Vector3 cLoc = ReadMemory<Vector3>(playerCameraManager + cachedCameraCacheOffset);
                        float cFOV = ReadMemory<float>(playerCameraManager + cachedCameraCacheOffset + 24);
                        Vector3 cRot = ReadMemory<Vector3>(playerCameraManager + cachedCameraCacheOffset + 12);

                        FMinimalViewInfo tempCam;
                        tempCam.Location = cLoc;
                        tempCam.Rotation = cRot;
                        tempCam.FOV = cFOV;
                        
                        if (IsCameraValid(tempCam)) {
                             camera = tempCam;
                             cameraFound = true;
                        } else {
                             // Cache invalid, reset
                             cachedCameraCacheOffset = 0; 
                        }
                    }

                    auto TryScanPCM = [&](uintptr_t pcmPtr, std::string label) -> bool {
                         if (pcmPtr < 0x10000000000) return false;
                         for (int i = 0x1000; i < 0x3000; i += 4) { // Scan relevant range
                              float val = ReadMemory<float>(pcmPtr + i);
                              if (val >= 70.0f && val <= 130.0f) { // Valid FOV range
                                   Vector3 loc = ReadMemory<Vector3>(pcmPtr + i - 24);
                                   Vector3 rot = ReadMemory<Vector3>(pcmPtr + i - 12);
                                   
                                   FMinimalViewInfo tempCam;
                                   tempCam.Location = loc;
                                   tempCam.Rotation = rot;
                                   tempCam.FOV = val;

                                   if (IsCameraValid(tempCam)) {
                                        if (camDebugCounter % 200 == 0) {
                                             std::cout << "   [PCM MATCH] " << label << " Offset 0x" << std::hex << (i - 24) 
                                                       << " FOV:" << val << " Loc:" << loc.x << "," << loc.y << std::dec << std::endl;
                                        }
                                        cachedCameraCacheOffset = i - 24;
                                        playerCameraManager = pcmPtr;
                                        camera = tempCam;
                                        return true;
                                   }
                              }
                         }
                         return false;
                    };

                    // 1. Try to find camera in PCM1 or PCM2
                    // PRIORITIZE DIRECT OFFSET 0x1AE0 (CameraCachePrivate) if verified
                    // Note: If scanner says "Offset 0x1AE0", it means Location is at 0x1AE0.
                    // (Scanner finds FOV at Offset+24, Location at Offset).
                    // Debug: Read ControlRotation
                    Vector3 controlRot = ReadMemory<Vector3>(playerController + 0x288);

                    if (!cameraFound) {
                        // Check PCM1
                        if (potentialPCM1 > 0x10000000) {
                            FMinimalViewInfo tempCam;
                            tempCam.Location = ReadMemory<Vector3>(potentialPCM1 + 0x1AE0); 
                            tempCam.Rotation = ReadMemory<Vector3>(potentialPCM1 + 0x1AE0 + 12); 
                            tempCam.FOV = ReadMemory<float>(potentialPCM1 + 0x1AE0 + 24); 

                            if (IsCameraValid(tempCam)) {
                                camera = tempCam;
                                cameraFound = true;
                                playerCameraManager = potentialPCM1;
                                cachedCameraCacheOffset = 0x1AE0;
                            }
                        }
                        
                        // Check PCM2 if PCM1 failed
                        if (!cameraFound && potentialPCM2 > 0x10000000) {
                            FMinimalViewInfo tempCam;
                            tempCam.Location = ReadMemory<Vector3>(potentialPCM2 + 0x1AE0);
                            tempCam.Rotation = ReadMemory<Vector3>(potentialPCM2 + 0x1AE0 + 12);
                            tempCam.FOV = ReadMemory<float>(potentialPCM2 + 0x1AE0 + 24);

                            if (IsCameraValid(tempCam)) {
                                camera = tempCam;
                                cameraFound = true;
                                playerCameraManager = potentialPCM2;
                                cachedCameraCacheOffset = 0x1AE0;
                            }
                        }
                        
                        // Try 0x1AE0 + 0x10 (Standard UE4 FCameraCacheEntry) if above failed
                        if (!cameraFound && potentialPCM1 > 0x10000000) {
                             FMinimalViewInfo tempCam;
                             tempCam.Location = ReadMemory<Vector3>(potentialPCM1 + 0x1AE0 + 0x10);
                             tempCam.Rotation = ReadMemory<Vector3>(potentialPCM1 + 0x1AE0 + 0x10 + 12);
                             tempCam.FOV = ReadMemory<float>(potentialPCM1 + 0x1AE0 + 0x10 + 24);
                             
                             if (IsCameraValid(tempCam)) {
                                  camera = tempCam;
                                  cameraFound = true;
                                  playerCameraManager = potentialPCM1;
                                  cachedCameraCacheOffset = 0x1AE0 + 0x10;
                             }
                        }
                    }

                    // Fallback to Scanner if Direct Offset failed
                    if (!cameraFound) {
                         if (TryScanPCM(potentialPCM1, "PCM_0x340")) {
                              cameraFound = true;
                         } else if (TryScanPCM(potentialPCM2, "PCM_0x348")) {
                              cameraFound = true;
                         }
                    }

                         // 2. Fallback to Pawn Location if PCM fails
                         if (!cameraFound) {
                              // Try to get Pawn from Controller OR Global
                              uintptr_t pawn = (g_LocalPawn != 0) ? g_LocalPawn : ReadMemory<uintptr_t>(playerController + 0x250); 
                              if (pawn == 0) pawn = ReadMemory<uintptr_t>(playerController + 0x2A0); // Controlled Pawn
                              
                              if (camDebugCounter % 200 == 0) {
                                   std::cout << "[Debug-Fallback] PC: " << std::hex << playerController 
                                             << " Pawn: " << pawn << std::dec << std::endl;
                              }

                              if (pawn > 0) {
                                   // Get RootComponent
                                   uintptr_t root = ReadMemory<uintptr_t>(pawn + 0x130); // RootComponent
                                   
                                   if (camDebugCounter % 200 == 0) {
                                        std::cout << "   -> Root: " << std::hex << root << std::dec << std::endl;
                                   }

                                   if (root > 0) {
                                        // 0x1E0 was reading (1,1,1) which is SCALE.
                                        // Location is usually 0x10 bytes before Scale in FTransform (Rot, Loc, Scale) or Loc, Rot, Scale.
                                        // FTransform: Rot(16), Loc(12), Scale(12).
                                        // If Scale is at 0x1E0, then Loc is at 0x1D0 (0x1E0 - 0x10).
                                        // Let's try 0x1D0.
                                        Vector3 loc = ReadMemory<Vector3>(root + 0x1D0); 
                                        
                                        // Debug Log Location Read
                                        if (camDebugCounter % 200 == 0) {
                                            std::cout << "   -> Read Loc @ 0x1D0: " << loc.x << ", " << loc.y << ", " << loc.z << std::endl;
                                        }

                                        // AUTO-SCANNER FOR LOCATION IF 0,0,0 (or Scale-like 1,1,1)
                                        // We consider (1,1,1) invalid for location too in this context (too close to origin)
                                        if (std::abs(loc.x) < 10.0f && std::abs(loc.y) < 10.0f) {
                                             // Try standard relative offset
                                             loc = ReadMemory<Vector3>(root + 0x11C); 
                                             if (camDebugCounter % 200 == 0) std::cout << "   -> Read Loc @ 0x11C: " << loc.x << ", " << loc.y << ", " << loc.z << std::endl;

                                             // If still 0, SCAN memory for valid float triplets
                                             if (std::abs(loc.x) < 10.0f && std::abs(loc.y) < 10.0f) {
                                                 // Scan 0x100 to 0x300
                                                 for (int offset = 0x100; offset < 0x300; offset += 4) {
                                                     Vector3 v = ReadMemory<Vector3>(root + offset);
                                                     // Heuristic: X and Y are usually large (>1000) in maps, Z varies
                                                     // But simpler check: Not 0,0 and not huge
                                                     if (std::abs(v.x) > 100.0f && std::abs(v.x) < 500000.0f &&
                                                         std::abs(v.y) > 100.0f && std::abs(v.y) < 500000.0f &&
                                                         std::abs(v.z) < 500000.0f) {
                                                             
                                                         loc = v;
                                                         if (camDebugCounter % 200 == 0) {
                                                             std::cout << "   [SCAN MATCH] Found valid Loc at Offset 0x" << std::hex << offset << std::dec 
                                                                       << " : " << loc.x << ", " << loc.y << std::endl;
                                                         }
                                                         break; // Found it
                                                     }
                                                 }
                                             }
                                        }

                                        // Use Control Rotation as fallback (Better than 0,0,0)
                                        Vector3 rot = controlRot;

                                        // Only accept if location looks valid (not 0,0,0)
                                        if (std::abs(loc.x) > 1.0f || std::abs(loc.y) > 1.0f) {
                                            if (camDebugCounter % 200 == 0) {
                                                 std::cout << "[Fallback] PC: 0x" << std::hex << playerController 
                                                           << " Pawn Loc: " << loc.x << "," << loc.y << std::dec 
                                                           << " Rot: " << rot.x << "," << rot.y << "," << rot.z << std::endl;
                                            }
                                            camera.Location = loc;
                                            camera.Location.z += 60.0f; // Eye height approx
                                            camera.Rotation = rot;
                                            camera.FOV = 90.0f; // Default FOV
                                            cameraFound = true;
                                        }
                                   }
                         }
                    }

                    // CAMERA PERSISTENCE (Anti-Flicker)
                    if (cameraFound) {
                        lastValidCamera = camera;
                        cameraFailCount = 0;
                        if (camDebugCounter % 200 == 0) {
                             std::string mode = (playerCameraManager != 0) ? "PCM (Synced)" : "Fallback (Desync Risk)";
                             std::cout << "[Camera Status] Mode: " << mode << " | Loc: " << camera.Location.x << "," << camera.Location.y << "," << camera.Location.z << std::endl;
                        }
                    } else if (cameraFailCount < 60 && lastValidCamera.FOV > 10.0f) {
                        // Use last valid camera for up to 1 second
                        camera = lastValidCamera;
                        cameraFound = true;
                        cameraFailCount++;
                    }

                    // 3. If we found a camera via PCM scan (and not already read), read it
                    if (cameraFound && cachedCameraCacheOffset != 0 && playerCameraManager != 0) {
                          // This block is only needed if we want to support dynamic offsets found by scanner
                          // But we already populated 'camera' in the direct check block above.
                          // Let's only overwrite if scanner was used (which sets cachedCameraCacheOffset)
                          // AND we didn't find it via direct offset (which might happen if direct fails but scan succeeds)
                          
                          // Actually, if TryScanPCM returns true, it sets 'playerCameraManager' and 'cachedCameraCacheOffset'.
                          // But it does NOT fill 'camera' struct.
                          // So we DO need to read it here if scanner was used.
                          
                          // Check if we already filled camera (FOV != 0)
                          if (camera.FOV == 0.0f) {
                               camera.Location = ReadMemory<Vector3>(playerCameraManager + cachedCameraCacheOffset);
                               camera.Rotation = ReadMemory<Vector3>(playerCameraManager + cachedCameraCacheOffset + 12);
                               camera.FOV = ReadMemory<float>(playerCameraManager + cachedCameraCacheOffset + 24);
                          }
                    }

                    // --- RENDER ---
                    std::vector<Entity> renderEntities;
                    
                    // Get latest data
                    {
                        std::lock_guard<std::mutex> lock(entitiesMutex);
                        renderEntities = sharedEntities;
                    }

                    // Calculate Screen Positions using LATEST Camera
                    if (cameraFound) {
                        // DEBUG: Print camera info occasionally
                        static int frameCount = 0;
                        frameCount++;
                        if (frameCount % 200 == 0) {
                              Vector3 debugCR = ReadMemory<Vector3>(playerController + 0x288);
                              std::cout << "[Debug] Cam Loc: " << camera.Location.x << "," << camera.Location.y << "," << camera.Location.z 
                                       << " FOV: " << camera.FOV 
                                       << " Rot: " << camera.Rotation.x << "," << camera.Rotation.y << "," << camera.Rotation.z 
                                       << " | CR: " << debugCR.x << "," << debugCR.y << "," << debugCR.z << std::endl;
                          }

                        int entitiesOnScreen = 0;
                        int debugCount = 0;
                        for (auto& entity : renderEntities) {
                            // UPDATE POSITION HERE (Render Loop) for zero lag
                            if (entity.baseAddress != 0) {
                                entity.location = GetActorLocation(entity.baseAddress);
                            }

                            bool isVisible = WorldToScreen(entity.location, camera, g_Width, g_Height, entity.screenPos);
                            
                            if (!isVisible) {
                                entity.screenPos = { -1000.0f, -1000.0f };
                            }

                            // Calculate Distance (Meters)
                            float dx = entity.location.x - camera.Location.x;
                            float dy = entity.location.y - camera.Location.y;
                            float dz = entity.location.z - camera.Location.z;
                            entity.distance = sqrt(dx*dx + dy*dy + dz*dz) / 100.0f; 

                            if (isVisible && 
                                entity.screenPos.x > -500 && entity.screenPos.x < g_Width + 500 && 
                                entity.screenPos.y > -500 && entity.screenPos.y < g_Height + 500) { // Allow wider range for drawing
                                entitiesOnScreen++;
                                if (frameCount % 60 == 0) {
                                     std::cout << "   [Visible] " << entity.name << " World: " 
                                               << entity.location.x << "," << entity.location.y << "," << entity.location.z 
                                               << " -> Screen: " << entity.screenPos.x << "," << entity.screenPos.y 
                                               << " Dist: " << entity.distance << "m" 
                                               << " HP: " << entity.health << std::endl;
                                }
                            }
                        }
                        
                        if (frameCount % 200 == 0) {
                             std::cout << "   Total Rendered Entities: " << entitiesOnScreen << "/" << renderEntities.size() << std::endl;
                        }
                    } else {
                        renderEntities.clear(); // Don't draw if no camera
                    }
                    
                    overlay.Render(renderEntities);
                }

                shouldExit = true;
                if (dataThread.joinable()) dataThread.join();
                
                overlay.Cleanup();
            }
        }
    }

    CloseHandle(hProcess);
    return 0;
}
