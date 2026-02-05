#pragma once
#include <vector>
#include <string>
#include <cmath>

// manejo de vectores y matrices
struct Vector3 {
    float x, y, z;
};

struct Vector2 {
    float x = -1000.0f, y = -1000.0f;
};

struct FMinimalViewInfo {
    Vector3 Location;
    Vector3 Rotation;
    float FOV;
};

struct FMatrix {
    float M[4][4];
};

// funciones para lidia con trigonometria y matrices
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

inline FMatrix ToMatrix(Vector3 rot) {
    FMatrix matrix;
    float pitch = rot.x * M_PI / 180.0f;
    float yaw = rot.y * M_PI / 180.0f;
    float roll = rot.z * M_PI / 180.0f;

    float SP = sinf(pitch);
    float CP = cosf(pitch);
    float SY = sinf(yaw);
    float CY = cosf(yaw);
    float SR = sinf(roll);
    float CR = cosf(roll);

    matrix.M[0][0] = CP * CY;
    matrix.M[0][1] = CP * SY;
    matrix.M[0][2] = SP;
    matrix.M[0][3] = 0.f;

    matrix.M[1][0] = SR * SP * CY - CR * SY;
    matrix.M[1][1] = SR * SP * SY + CR * CY;
    matrix.M[1][2] = -SR * CP;
    matrix.M[1][3] = 0.f;

    matrix.M[2][0] = -(CR * SP * CY + SR * SY);
    matrix.M[2][1] = CY * SR - CR * SP * SY;
    matrix.M[2][2] = CR * CP;
    matrix.M[2][3] = 0.f;

    matrix.M[3][0] = 0.f;
    matrix.M[3][1] = 0.f;
    matrix.M[3][2] = 0.f;
    matrix.M[3][3] = 1.f;

    return matrix;
}

inline FMatrix MatrixMultiplication(FMatrix pM1, FMatrix pM2) {
    FMatrix pOut = { 0 };
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                pOut.M[i][j] += pM1.M[i][k] * pM2.M[k][j];
            }
        }
    }
    return pOut;
}

inline bool WorldToScreen(Vector3 worldLoc, FMinimalViewInfo camera, int width, int height, Vector2& outScreenPos) {
    // Ojo con el pitch de UE4. Si las cosas salen invertidas o recortadas, puede que necesitemos ajustar el eje vertical.

    Vector3 adjustedRot = camera.Rotation;
    // adjustedRot.x = -adjustedRot.x; // PRUEBA: Descomentar esto si la imagen sale completamente al revés
    
    // construi la matriz estandar basandome en la rotacion
    FMatrix tempMatrix = ToMatrix(adjustedRot);
    
    Vector3 vAxisX = { tempMatrix.M[0][0], tempMatrix.M[0][1], tempMatrix.M[0][2] };
    Vector3 vAxisY = { tempMatrix.M[1][0], tempMatrix.M[1][1], tempMatrix.M[1][2] };
    Vector3 vAxisZ = { tempMatrix.M[2][0], tempMatrix.M[2][1], tempMatrix.M[2][2] };

    Vector3 vDelta = { worldLoc.x - camera.Location.x, worldLoc.y - camera.Location.y, worldLoc.z - camera.Location.z };
    
    Vector3 vTransformed = {
        vDelta.x * vAxisY.x + vDelta.y * vAxisY.y + vDelta.z * vAxisY.z,
        vDelta.x * vAxisZ.x + vDelta.y * vAxisZ.y + vDelta.z * vAxisZ.z,
        vDelta.x * vAxisX.x + vDelta.y * vAxisX.y + vDelta.z * vAxisX.z
    };

    // Si el punto está a nuestras espaldas (o demasiado cerca), no tiene sentido dibujarlo
    if (vTransformed.z < 1.0f) {
        return false;
    }

    float fov = camera.FOV;
    float screenCenterX = width / 2.0f;
    float screenCenterY = height / 2.0f;

    float tanFov = tanf(fov * (float)M_PI / 360.0f);
    
    outScreenPos.x = screenCenterX + vTransformed.x * (screenCenterX / tanFov) / vTransformed.z;
    outScreenPos.y = screenCenterY - vTransformed.y * (screenCenterX / tanFov) / vTransformed.z;

    // DEBUG: Verificamos si nos estamos pasando del borde superior
    // Si Y es positivo, estamos en la mitad superior de la pantalla, si es negativo, en la inferior
    
    return true;
}

// Estructura para organizar los datos de los personajes que nos interesan
struct Entity {
    uintptr_t baseAddress;
    Vector3 location;
    float health;
    std::string name;
    bool isPlayer;
    bool isEnemy;
    Vector2 screenPos; // calculo para saber donde dinujar el overlay
    float distance;    // que tan lejos esta de nuestra camara
    float height = 100.0f; // altura estándar (aprox. el doble de la mitad de altura)
    float radius = 50.0f;  // radio estándar para el personaje
};
