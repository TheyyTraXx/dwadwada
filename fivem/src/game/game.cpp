#include "game.h"
#include <cmath>
#include <cstring>
#include <Psapi.h>

constexpr int BONE_IDX_HEAD   = 0x10;
constexpr uintptr_t SKELETON_BONE_TRANSFORMS = 0x20;
constexpr uintptr_t BONE_MATRIX_SIZE         = 0x40;

Game::Game(memify* mem)
    : m_mem(mem), m_base(0), m_localPed(0),
      m_viewMatrixAddr(0), m_localPedAddr(0), m_pedFactoryAddr(0),
      m_screenW(0), m_screenH(0)
{
    m_base    = m_mem->GetBase("FiveM_b3258_GTAProcess.exe");
    m_screenW = GetSystemMetrics(SM_CXSCREEN);
    m_screenH = GetSystemMetrics(SM_CYSCREEN);
    memset(&m_viewProj, 0, sizeof(m_viewProj));
    memset(&m_localPos, 0, sizeof(m_localPos));
}

// ─────────────────────────────────────────────
//  AOB Scanner — lee el módulo completo a un buffer local
//  y busca el patrón con wildcards (??)
// ─────────────────────────────────────────────
uintptr_t Game::ScanPattern(const char* pattern, size_t scanSize) const
{
    if (!m_base) return 0;

    // Parsear patrón "AA BB ?? CC ??"
    std::vector<int> bytes;
    const char* p = pattern;
    while (*p)
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?' && (p[1] == '?' || p[1] == ' ' || !p[1])) {
            bytes.push_back(-1);
            p += (p[1] == '?') ? 2 : 1;
        } else {
            bytes.push_back((int)strtol(p, nullptr, 16));
            p += 2;
        }
    }

    // Leer memoria del proceso en bloques de 4MB
    constexpr size_t CHUNK = 0x400000;
    std::vector<uint8_t> buf(CHUNK);

    for (size_t offset = 0; offset < scanSize; offset += CHUNK)
    {
        size_t toRead = (offset + CHUNK > scanSize) ? (scanSize - offset) : CHUNK;
        if (!m_mem->ReadRaw(m_base + offset, buf.data(), toRead))
            continue;

        for (size_t i = 0; i + bytes.size() <= toRead; i++)
        {
            bool match = true;
            for (size_t j = 0; j < bytes.size(); j++) {
                if (bytes[j] != -1 && buf[i + j] != (uint8_t)bytes[j]) {
                    match = false; break;
                }
            }
            if (match) return m_base + offset + i;
        }
    }
    return 0;
}

// Resuelve LEA/MOV rax,[rip+disp32]: devuelve la dirección absoluta del operando
uintptr_t Game::ResolveRip(uintptr_t instrAddr, int offsetPos, int instrLen) const
{
    int32_t disp = m_mem->Read<int32_t>(instrAddr + offsetPos);
    return instrAddr + instrLen + disp;
}

// ─────────────────────────────────────────────
//  Init — escanea AOB y resuelve punteros
// ─────────────────────────────────────────────
bool Game::Init()
{
    if (!m_base) {
        printf("[>>] Base no encontrada\n");
        return false;
    }
    printf("[>>] Base: 0x%llX\n", (unsigned long long)m_base);

    // ── ViewMatrix ────────────────────────────
    uintptr_t vmSig = ScanPattern(patterns::ViewMatrix);
    if (vmSig) {
        uintptr_t viewportPtr = ResolveRip(vmSig, 3, 7);
        // viewportPtr → ptr a viewport object; matrix @ +0x24
        uintptr_t viewportObj = m_mem->Read<uintptr_t>(viewportPtr);
        m_viewMatrixAddr = viewportObj + offsets::ViewMatrixInObj;
        printf("[>>] ViewMatrix @ 0x%llX\n", (unsigned long long)m_viewMatrixAddr);
    } else {
        printf("[>>] ViewMatrix pattern no encontrado\n");
    }

    // ── LocalPed ─────────────────────────────
    uintptr_t lpSig = ScanPattern(patterns::LocalPedPtr);
    if (lpSig) {
        m_localPedAddr = ResolveRip(lpSig, 3, 7);
        printf("[>>] LocalPedPtr @ 0x%llX\n", (unsigned long long)m_localPedAddr);
    } else {
        printf("[>>] LocalPed pattern no encontrado\n");
    }

    // ── PedFactory ────────────────────────────
    uintptr_t pfSig = ScanPattern(patterns::PedFactory);
    if (pfSig) {
        m_pedFactoryAddr = ResolveRip(pfSig, 3, 7);
        printf("[>>] PedFactory @ 0x%llX\n", (unsigned long long)m_pedFactoryAddr);
    } else {
        printf("[>>] PedFactory pattern no encontrado\n");
    }

    return (m_viewMatrixAddr && m_localPedAddr && m_pedFactoryAddr);
}

// ─────────────────────────────────────────────
//  WorldToScreen
// ─────────────────────────────────────────────
bool Game::WorldToScreen(const Vector3& world, Vector2& screen) const
{
    const float* m = &m_viewProj.m[0][0];
    float clipX = world.x*m[0]  + world.y*m[4]  + world.z*m[8]  + m[12];
    float clipY = world.x*m[1]  + world.y*m[5]  + world.z*m[9]  + m[13];
    float clipW = world.x*m[3]  + world.y*m[7]  + world.z*m[11] + m[15];
    if (clipW < 0.001f) return false;
    screen.x = (( clipX / clipW) + 1.0f) * 0.5f * m_screenW;
    screen.y = ((-clipY / clipW) + 1.0f) * 0.5f * m_screenH;
    return true;
}

// ─────────────────────────────────────────────
//  ReadViewMatrix
// ─────────────────────────────────────────────
void Game::ReadViewMatrix()
{
    if (!m_viewMatrixAddr) return;
    m_mem->ReadRaw(m_viewMatrixAddr, &m_viewProj, sizeof(Matrix44));
}

// ─────────────────────────────────────────────
//  ReadLocalPlayer
// ─────────────────────────────────────────────
void Game::ReadLocalPlayer()
{
    if (!m_localPedAddr) return;
    // *localPedAddr → CPlayerInfo* → ped
    uintptr_t playerInfoMgr = m_mem->Read<uintptr_t>(m_localPedAddr);
    if (!playerInfoMgr) return;
    m_localPed = m_mem->Read<uintptr_t>(playerInfoMgr + 0x08);
    if (!m_localPed) return;
    m_localPos.x = m_mem->Read<float>(m_localPed + offsets::PedPosX);
    m_localPos.y = m_mem->Read<float>(m_localPed + offsets::PedPosY);
    m_localPos.z = m_mem->Read<float>(m_localPed + offsets::PedPosZ);
}

// ─────────────────────────────────────────────
//  ReadBonePos
// ─────────────────────────────────────────────
Vector3 Game::ReadBonePos(uintptr_t pedAddr, int boneIdx)
{
    uintptr_t skelPtr = m_mem->Read<uintptr_t>(pedAddr + offsets::PedBoneMatrix);
    if (!skelPtr) return {0,0,0};
    uintptr_t bonesArr = m_mem->Read<uintptr_t>(skelPtr + SKELETON_BONE_TRANSFORMS);
    if (!bonesArr) return {0,0,0};
    uintptr_t boneBase = bonesArr + (uintptr_t)boneIdx * BONE_MATRIX_SIZE;
    Vector3 pos;
    pos.x = m_mem->Read<float>(boneBase + 0x30);
    pos.y = m_mem->Read<float>(boneBase + 0x34);
    pos.z = m_mem->Read<float>(boneBase + 0x38);
    // bones en espacio local → sumar posición del ped
    pos.x += m_mem->Read<float>(pedAddr + offsets::PedPosX);
    pos.y += m_mem->Read<float>(pedAddr + offsets::PedPosY);
    pos.z += m_mem->Read<float>(pedAddr + offsets::PedPosZ);
    return pos;
}

// ─────────────────────────────────────────────
//  ReadPlayers — recorre el fwBasePool de peds
// ─────────────────────────────────────────────
void Game::ReadPlayers()
{
    m_players.clear();
    if (!m_pedFactoryAddr) return;

    // CPedFactory* → pool
    uintptr_t factory = m_mem->Read<uintptr_t>(m_pedFactoryAddr);
    if (!factory) return;
    uintptr_t pool = m_mem->Read<uintptr_t>(factory + offsets::PedFactoryPool);
    if (!pool) return;

    uint32_t poolSize = m_mem->Read<uint32_t>(pool + offsets::PoolSize);
    if (!poolSize || poolSize > 512) return;

    uintptr_t itemsPtr  = m_mem->Read<uintptr_t>(pool + offsets::PoolItems);
    uintptr_t flagsPtr  = m_mem->Read<uintptr_t>(pool + offsets::PoolFlags);
    if (!itemsPtr) return;

    for (uint32_t i = 0; i < poolSize; i++)
    {
        // En fwBasePool el flag bit 128 = slot libre
        if (flagsPtr) {
            uint8_t flag = m_mem->Read<uint8_t>(flagsPtr + i);
            if (flag & 0x80) continue; // slot libre
        }

        uintptr_t pedAddr = m_mem->Read<uintptr_t>(itemsPtr + (uintptr_t)i * 8);
        if (!pedAddr || pedAddr == m_localPed) continue;

        float health    = m_mem->Read<float>(pedAddr + offsets::PedHealth);
        float maxHealth = m_mem->Read<float>(pedAddr + offsets::PedMaxHealth);
        if (health <= 0.f || maxHealth <= 0.f || health > 1000.f) continue;

        Vector3 feetPos;
        feetPos.x = m_mem->Read<float>(pedAddr + offsets::PedPosX);
        feetPos.y = m_mem->Read<float>(pedAddr + offsets::PedPosY);
        feetPos.z = m_mem->Read<float>(pedAddr + offsets::PedPosZ);

        Vector3 headPos = ReadBonePos(pedAddr, BONE_IDX_HEAD);
        // Si el bone falla usamos feetPos + altura estimada
        if (headPos.x == 0.f && headPos.y == 0.f)
            headPos = { feetPos.x, feetPos.y, feetPos.z + 1.7f };

        // Nombre (solo si tiene CPlayerInfo — jugadores reales)
        std::string name;
        uintptr_t playerInfo = m_mem->Read<uintptr_t>(pedAddr + offsets::PedPlayerInfo);
        if (playerInfo) {
            char buf[32] = {};
            m_mem->ReadRaw(playerInfo + offsets::PlayerName, buf, sizeof(buf)-1);
            if (buf[0] != '\0') name = buf;
        }
        if (name.empty()) continue; // saltar NPCs sin nombre

        float dx = feetPos.x - m_localPos.x;
        float dy = feetPos.y - m_localPos.y;
        float dz = feetPos.z - m_localPos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        Vector2 headScreen{}, feetScreen{};
        bool hOk = WorldToScreen(headPos, headScreen);
        bool fOk = WorldToScreen(feetPos, feetScreen);
        if (!hOk && !fOk) continue;

        PlayerESP esp;
        esp.name       = name;
        esp.headPos    = headPos;
        esp.feetPos    = feetPos;
        esp.headScreen = headScreen;
        esp.feetScreen = feetScreen;
        esp.health     = health;
        esp.maxHealth  = maxHealth;
        esp.distance   = dist;
        esp.valid      = true;
        m_players.push_back(esp);
    }
}

// ─────────────────────────────────────────────
//  Update — llamar cada frame
// ─────────────────────────────────────────────
void Game::Update()
{
    if (!m_base) return;
    ReadViewMatrix();
    ReadLocalPlayer();
    ReadPlayers();
}
