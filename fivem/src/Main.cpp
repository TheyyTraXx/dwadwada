#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <thread>

#include "Windows/window.hpp"
#include "mem/memify.h"
#include "game/game.h"

// ─────────────────────────────────────────────
//  Configuración del ESP
// ─────────────────────────────────────────────
namespace cfg
{
    bool espEnabled      = true;
    bool espBoxes        = true;       // caja 2D
    bool espNames        = true;       // nombre encima
    bool espHealth       = true;       // barra de vida
    bool espDistance     = true;       // distancia en metros
    bool espSkeletons    = false;      // skeleton (opcional)
    float maxDistance    = 300.f;      // metros máximos para dibujar
    ImVec4 colorEnemy    = { 1.f, 0.f, 0.f, 1.f }; // rojo
    ImVec4 colorFriendly = { 0.f, 1.f, 0.f, 1.f }; // verde
}

// ─────────────────────────────────────────────
//  DrawESP — dibuja todo sobre ImGui background draw list
// ─────────────────────────────────────────────
static void DrawESP(const std::vector<PlayerESP>& players)
{
    if (!cfg::espEnabled) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    for (const PlayerESP& p : players)
    {
        if (!p.valid)                     continue;
        if (p.distance > cfg::maxDistance) continue;

        // Determinamos si la caja es dibujable
        // Altura de caja en píxeles = diferencia Y entre cabeza y pies
        float boxHeight = p.feetScreen.y - p.headScreen.y;
        if (boxHeight < 5.f) continue; // demasiado pequeño / detrás

        float boxWidth  = boxHeight * 0.4f; // proporción típica de persona
        float boxLeft   = p.headScreen.x - boxWidth * 0.5f;
        float boxTop    = p.headScreen.y;
        float boxRight  = p.headScreen.x + boxWidth * 0.5f;
        float boxBottom = p.feetScreen.y;

        ImU32 mainColor = IM_COL32(255, 0, 0, 255); // rojo por defecto

        // ── Caja 2D ────────────────────────────────
        if (cfg::espBoxes)
        {
            // sombra (desplazada 1px para legibilidad)
            drawList->AddRect(
                { boxLeft + 1.f, boxTop + 1.f },
                { boxRight + 1.f, boxBottom + 1.f },
                IM_COL32(0, 0, 0, 180), 0.f, 0, 1.5f
            );
            // caja principal
            drawList->AddRect(
                { boxLeft, boxTop },
                { boxRight, boxBottom },
                mainColor, 0.f, 0, 1.5f
            );
        }

        // ── Nombre ─────────────────────────────────
        if (cfg::espNames && !p.name.empty())
        {
            // Texto centrado sobre la caja
            ImVec2 textPos = { p.headScreen.x, boxTop - 14.f };

            // Calcula ancho del texto para centrarlo
            ImVec2 textSize = ImGui::CalcTextSize(p.name.c_str());
            textPos.x -= textSize.x * 0.5f;

            // sombra
            drawList->AddText({ textPos.x + 1.f, textPos.y + 1.f },
                IM_COL32(0, 0, 0, 220), p.name.c_str());
            // texto
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), p.name.c_str());
        }

        // ── Distancia ──────────────────────────────
        if (cfg::espDistance)
        {
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%.0fm", p.distance);

            ImVec2 distSize = ImGui::CalcTextSize(distBuf);
            ImVec2 distPos  = { p.headScreen.x - distSize.x * 0.5f, boxBottom + 2.f };

            drawList->AddText({ distPos.x + 1.f, distPos.y + 1.f },
                IM_COL32(0, 0, 0, 220), distBuf);
            drawList->AddText(distPos, IM_COL32(220, 220, 220, 255), distBuf);
        }

        // ── Barra de vida ──────────────────────────
        if (cfg::espHealth && p.maxHealth > 0.f)
        {
            float hpRatio = p.health / p.maxHealth;
            if (hpRatio < 0.f) hpRatio = 0.f;
            if (hpRatio > 1.f) hpRatio = 1.f;

            float barX      = boxLeft - 5.f;
            float barTop    = boxTop;
            float barBottom = boxBottom;
            float barHeight = barBottom - barTop;
            float filledH   = barHeight * hpRatio;

            // fondo de la barra
            drawList->AddRectFilled(
                { barX - 1.f, barTop - 1.f },
                { barX + 2.f, barBottom + 1.f },
                IM_COL32(0, 0, 0, 200)
            );

            // color de la barra: verde→amarillo→rojo
            uint8_t r = static_cast<uint8_t>((1.f - hpRatio) * 255.f);
            uint8_t g = static_cast<uint8_t>(hpRatio * 255.f);
            drawList->AddRectFilled(
                { barX, barBottom - filledH },
                { barX + 1.f, barBottom },
                IM_COL32(r, g, 0, 255)
            );
        }
    }
}

// ─────────────────────────────────────────────
//  DrawMenu — ventana ImGui con opciones
// ─────────────────────────────────────────────
static void DrawMenu(bool* open)
{
    if (!*open) return;

    ImGui::SetNextWindowSize({ 280.f, 320.f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("FiveM ESP", open,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    if (ImGui::CollapsingHeader("ESP", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Activar ESP",      &cfg::espEnabled);
        ImGui::Checkbox("Cajas 2D",         &cfg::espBoxes);
        ImGui::Checkbox("Nombres",          &cfg::espNames);
        ImGui::Checkbox("Barra de vida",    &cfg::espHealth);
        ImGui::Checkbox("Distancia",        &cfg::espDistance);
        ImGui::SliderFloat("Dist. max (m)", &cfg::maxDistance, 50.f, 1000.f);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────
int main()
{
    // Consola de debug (quita esto en release)
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    printf("[>>] FiveM ESP - iniciando\n");

    // ── Esperar al proceso con snapshot directo ────────────
    static const char* PROC_NAME = "FiveM_b3258_GTAProcess.exe";

    printf("[>>] Esperando proceso: %s\n", PROC_NAME);
    while (true)
    {
        PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        bool found = false;
        if (snap != INVALID_HANDLE_VALUE)
        {
            if (Process32First(snap, &pe))
                do { if (_stricmp(pe.szExeFile, PROC_NAME) == 0) { found = true; break; } }
                while (Process32Next(snap, &pe));
            CloseHandle(snap);
        }
        if (found) break;
        Sleep(1000);
    }
    printf("[>>] Proceso encontrado\n");
    Sleep(2000); // esperar a que el juego termine de cargar modulos

    // ── Inicializar memoria ────────────────────
    memify mem(std::vector<std::string>{ PROC_NAME });

    // ── Inicializar overlay ────────────────────
    Overlay overlay;
    overlay.SetupOverlay("FiveM ESP Overlay");

    // ── Inicializar juego ─────────────────────
    Game game(&mem);
    printf("[>>] Base: 0x%llX\n", game.GetBase());

    // Escanear patrones AOB — reintentar hasta que el juego cargue los módulos
    printf("[>>] Escaneando patrones...\n");
    while (!game.Init()) {
        printf("[>>] Patrones no encontrados, reintentando en 3s...\n");
        Sleep(3000);
    }
    printf("[>>] Patrones resueltos, iniciando ESP\n");

    // ── Loop principal ─────────────────────────
    while (overlay.shouldRun)
    {
        // Leer memoria del juego
        game.Update();

        // Procesar mensajes Windows + ImGui frame
        overlay.StartRender();
        {
            // Siempre buscamos que el overlay siga encima del juego
            HWND gameWnd = FindWindowA(nullptr, "FiveM");
            if (!gameWnd) gameWnd = FindWindowA(nullptr, "Grand Theft Auto V");
            if (!gameWnd) gameWnd = FindWindowA("grcWindow", nullptr); // clase de ventana RAGE
            if (gameWnd)  overlay.SetForeground(gameWnd);

            // ── Dibujar ESP en background ──────
            DrawESP(game.GetPlayers());

            // ── Menú ImGui (con INSERT) ────────
            if (overlay.RenderMenu)
                DrawMenu(&overlay.RenderMenu);
        }
        overlay.EndRender();
    }

    printf("[>>] Cerrando\n");
    return 0;
}
