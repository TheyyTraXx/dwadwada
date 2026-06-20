#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include "../mem/memify.h"

// ─────────────────────────────────────────────
//  AOB patterns para GTA5 / FiveM b3258
//  Se resuelven en runtime — no dependen de offsets fijos
// ─────────────────────────────────────────────
namespace patterns
{
    // rage::grcViewport::sm_pInstance viewprojection matrix
    // 48 8B 05 ?? ?? ?? ?? 8B 48 50  → rip+3 → pViewport, matrix a +0x24
    constexpr const char* ViewMatrix   = "48 8B 05 ?? ?? ?? ?? 8B 48 50";

    // CWorld / WorldPtr
    // 48 8B 05 ?? ?? ?? ?? 45 ?? ?? ?? 48 8B 48 08
    constexpr const char* WorldPtr     = "48 8B 05 ?? ?? ?? ?? 45 ?? ?? ?? 48 8B 48 08";

    // LocalPed / CPlayerInfo
    // 48 8B 05 ?? ?? ?? ?? 48 8B 48 08 48 85 C9 74 ?? 48 8B 51 20
    constexpr const char* LocalPedPtr  = "48 8B 05 ?? ?? ?? ?? 48 8B 48 08 48 85 C9 74 ?? 48 8B 51 20";

    // CPedFactory — da acceso al pool de peds
    // 48 8B 05 ?? ?? ?? ?? 41 0F BF C8 0F BF 40 18
    constexpr const char* PedFactory   = "48 8B 05 ?? ?? ?? ?? 41 0F BF C8 0F BF 40 18";
}

// Offsets internos de structs (estables entre builds cercanas)
namespace offsets
{
    constexpr uintptr_t PedHealth       = 0x280;
    constexpr uintptr_t PedMaxHealth    = 0x284;
    // Posición en la transform matrix del ped (rage::phInst::mMatrix col3)
    constexpr uintptr_t PedPosX         = 0x90 + 0x30;  // matrix[3][0]
    constexpr uintptr_t PedPosY         = 0x90 + 0x50;  // matrix[3][1]  
    constexpr uintptr_t PedPosZ         = 0x90 + 0x70;  // matrix[3][2]
    constexpr uintptr_t PedBoneMatrix   = 0x430;
    constexpr uintptr_t PedPlayerInfo   = 0x10C8;

    // CPlayerInfo
    constexpr uintptr_t PlayerName      = 0xA4;

    // CPedFactory → pool
    constexpr uintptr_t PedFactoryPool  = 0x08;          // CPedFactory::m_pPedPool
    // fwBasePool
    constexpr uintptr_t PoolItems       = 0x10;          // pItems
    constexpr uintptr_t PoolSize        = 0x20;          // itemCount (uint32)
    constexpr uintptr_t PoolFlags       = 0x60;          // flags array

    // ViewMatrix offset dentro del viewport object
    constexpr uintptr_t ViewMatrixInObj = 0x24;
}

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Matrix44 { float m[4][4]; };

struct PlayerESP
{
    std::string  name;
    Vector3      headPos;
    Vector3      feetPos;
    Vector2      headScreen;
    Vector2      feetScreen;
    float        health;
    float        maxHealth;
    float        distance;
    bool         valid;
};

class Game
{
public:
    explicit Game(memify* mem);

    // Escanea patrones y resuelve punteros base — llamar una vez al init
    bool Init();

    // Actualiza cada frame
    void Update();

    bool WorldToScreen(const Vector3& world, Vector2& screen) const;

    const std::vector<PlayerESP>& GetPlayers() const { return m_players; }
    Vector3 GetLocalPos() const { return m_localPos; }
    uintptr_t GetBase() const { return m_base; }

private:
    memify*    m_mem;
    uintptr_t  m_base;

    // Punteros resueltos por AOB
    uintptr_t  m_viewMatrixAddr;   // dirección final de float[16]
    uintptr_t  m_localPedAddr;     // *ptr → CPed*
    uintptr_t  m_pedFactoryAddr;   // *ptr → CPedFactory*

    Matrix44   m_viewProj;
    Vector3    m_localPos;
    uintptr_t  m_localPed;
    int        m_screenW, m_screenH;

    std::vector<PlayerESP> m_players;

    // AOB scanner interno
    uintptr_t ScanPattern(const char* pattern, size_t scanSize = 0x4000000) const;
    // Resuelve instrucción RIP-relative: lea rax, [rip+offset] → devuelve la dirección final
    uintptr_t ResolveRip(uintptr_t instrAddr, int offsetPos = 3, int instrLen = 7) const;

    void ReadViewMatrix();
    void ReadLocalPlayer();
    void ReadPlayers();
    Vector3 ReadBonePos(uintptr_t pedAddr, int boneIdx);
};
