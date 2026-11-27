#include "game/debug_window.h"

#include "assets/arcade_categories.h"
#include "game/event_script.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hotd::game {

namespace {

using G = A::G;

// the panel's height in lines (the overlay's panel grows with its lines; the separate window's does too)
constexpr size_t kPanelHeight = 34;

std::string hex(uint32_t v, int digits = 8)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%0*X", digits, v);
    return buf;
}

std::string dec(int64_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    return buf;
}

// the game's own angle units: 0x10000 = 360 degrees (the Enemy's ang[1])
float angleDegrees(uint32_t a)
{
    return float(int32_t(a)) * 360.0f / 65536.0f;
}

} // namespace

std::string evtOpcodeName(const Runtime& rt, uint32_t opcode)
{
    if (!rt.mem.mapped(G::evt_handlers))
        return dec(opcode);
    const uint32_t handler = rt.mem.u32(G::evt_handlers + opcode * 4);
    auto it = rt.names.find(handler);
    if (it != rt.names.end())
        return it->second;
    char buf[32];
    std::snprintf(buf, sizeof buf, "op %u", opcode);
    return buf;
}

// the instructions of the section at `pc` in the live memory: the interpreter's own operand counts
// (game/event_script.h: evtOperandCount) decide how far each instruction reaches, so the walk stops exactly where
// the interpreter would stop (at the data tables that follow the code)
std::vector<DebugInstruction> DebugWindow::disassemble(const Runtime& rt, uint32_t pc, size_t max, uint32_t currentPc)
{
    std::vector<DebugInstruction> out;
    if (!rt.mem.mapped(pc))
        return out;
    uint32_t at = pc;
    // the walk is bounded: a malformed script must not spin here
    for (size_t guard = 0; guard < 4096 && out.size() < max; ++guard) {
        const uint32_t op = rt.mem.u32(at);
        if (op == kEvtEnd)
            break;
        DebugInstruction ins;
        ins.pc = at;
        ins.opcode = op;
        ins.name = evtOpcodeName(rt, op);
        ins.current = (at == currentPc);
        const int n = evtOperandCount(op);
        if (n == -3) {   // unknown: the data tables follow the code, not another instruction
            ins.valid = false;
            out.push_back(ins);
            break;
        }
        if (n == -2) {   // CCTB: the sub-operation decides the operand count (64: 5, 33: 3, else 2)
            const uint32_t sub = rt.mem.u32(at + 4);
            const int count = sub == 64 ? 5 : sub == 33 ? 3 : 2;
            for (int i = 0; i < count; ++i)
                ins.operands.push_back(int32_t(rt.mem.u32(at + 4 + 4 * uint32_t(i))));
            out.push_back(ins);
            at += 4 * uint32_t(1 + count);
            continue;
        }
        if (n == -1) {   // a list ended by kEvtEnd (the spawn opcodes' record pointers)
            uint32_t o = at + 4;
            int guard2 = 0;
            while (rt.mem.u32(o) != kEvtEnd && guard2++ < 4096) {
                ins.operands.push_back(int32_t(rt.mem.u32(o)));
                o += 4;
            }
            out.push_back(ins);
            at = o + 4;
            continue;
        }
        for (int i = 0; i < n; ++i)
            ins.operands.push_back(int32_t(rt.mem.u32(at + 4 + 4 * uint32_t(i))));
        out.push_back(ins);
        at += 4 * uint32_t(1 + n);
    }
    return out;
}

DebugSnapshot DebugWindow::collect(const Runtime& rt) const
{
    DebugSnapshot s;
    s.frame = rt.frameCount;
    auto& mem = rt.mem;
    if (!mem.mapped(G::game_mode))
        return s;
    s.mode = mem.at<int16_t>(G::game_mode);
    s.submode = mem.at<int16_t>(G::game_submode);
    // the scene
    s.chapter = mem.at<int8_t>(G::chapter) + 1;
    s.event = mem.at<int8_t>(G::event_index);
    s.sceneNumber = mem.at<int16_t>(G::scene_number);
    s.sceneState = mem.at<uint8_t>(G::scene_state);
    s.routeChoice = mem.at<int8_t>(G::route_choice);
    s.drawZone = mem.at<uint8_t>(G::draw_zone);
    s.textureArea = rt.textureArea;
    // the camera
    if (mem.mapped(G::camera_pos))
        for (int i = 0; i < 3; ++i)
            s.cameraPos[i] = mem.f32(G::camera_pos + 4 * uint32_t(i));
    s.cameraMode = mem.at<int32_t>(G::camera_mode);
    s.cameraDriveMode = mem.at<int32_t>(G::camera_drive_mode);
    // the event script
    s.evtPc = mem.u32(G::evt_pc);
    s.evtOpcode = mem.u32(G::evt_opcode);
    s.evtName = evtOpcodeName(rt, s.evtOpcode);
    s.disasm = disassemble(rt, s.evtPc, 256, s.evtPc);
    s.trace = rt.evtTrace;
    // the creatures
    s.enemiesAlive = mem.at<int16_t>(G::enemies_alive);
    s.enemyCount = mem.at<int16_t>(G::enemy_count);
    for (uint32_t o : rt.shootable) {
        if (!mem.mapped(o))
            continue;
        DebugMonster m;
        m.obj = o;
        m.type = mem.at<int16_t>(o + 0xCC);
        m.name = CreatureCategories::typeName(m.type);
        for (int i = 0; i < 3; ++i)
            m.pos[i] = mem.f32(o + 0x20 + 4 * uint32_t(i));
        m.facing = angleDegrees(mem.u32(o + 0x30));
        m.state = mem.at<int16_t>(o + 0x5C);
        m.hp = mem.at<int16_t>(o + 0x70);
        m.hpMax = mem.at<int16_t>(o + 0x72);
        m.motion = mem.at<int16_t>(o + 0xD0);
        m.frame = mem.at<int16_t>(o + 0xF4);
        m.flags = mem.u32(o + 0x58);
        m.slot = mem.at<int8_t>(o + 0x2E6);
        m.killer = mem.at<int8_t>(o + 0x2EB);
        s.monsters.push_back(m);
    }
    // the players
    for (int p = 0; p < 4; ++p) {
        const uint32_t pl = rt.player_rec(p);
        if (!mem.mapped(pl))
            continue;
        DebugSnapshot::Seat& seat = s.seats[p];
        seat.state = mem.at<int32_t>(pl);
        seat.score = mem.at<int32_t>(pl + 8);
        seat.lives = mem.at<int32_t>(pl + 0xC);
        seat.rounds = mem.at<uint8_t>(pl + 0x3D);
    }
    s.seatsInGame = mem.at<uint16_t>(G::players_in_game);
    // the counters
    s.drawList = rt.drawList.size();
    s.shootable = rt.shootable.size();
    s.boundaryCount = rt.boundaryHits.size();
    s.missingCount = rt.missingCount.size();
    // the most-called boundaries: the ones a developer wants to see first
    std::vector<std::pair<uint64_t, std::string>> top;
    for (const auto& [name, hit] : rt.boundaryHits)
        top.emplace_back(hit.calls, name);
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < top.size() && i < 8; ++i)
        s.boundaryTop.push_back(dec(int64_t(top[i].first)) + "  " + top[i].second);
    return s;
}

std::string DebugWindow::panelTitle() const
{
    switch (page) {
    case DebugPage::Monsters: return "MONSTERS";
    case DebugPage::Scene: return "SCENE";
    case DebugPage::Opcodes: return "OPCODES";
    case DebugPage::Debug: return "DEBUG";
    }
    return "DEBUG";
}

std::vector<std::string> DebugWindow::panelLines(const DebugSnapshot& s) const
{
    std::vector<std::string> lines;
    char buf[256];
    switch (page) {
    case DebugPage::Monsters:
        std::snprintf(buf, sizeof buf, "alive %d / count %d    Tab: page  F2: close", s.enemiesAlive, s.enemyCount);
        lines.push_back(buf);
        if (s.monsters.empty())
            lines.push_back("(no creatures in the depth list)");
        for (const DebugMonster& m : s.monsters) {
            std::snprintf(buf, sizeof buf, "%s  %-22s hp %d/%d  st %d  mot %d f%d", hex(m.obj).c_str(), m.name.c_str(), m.hp, m.hpMax,
                          m.state, m.motion, m.frame);
            lines.push_back(buf);
            std::snprintf(buf, sizeof buf, "   pos %.1f %.1f %.1f  face %.0f  slot %d  fl 0x%X", m.pos[0], m.pos[1], m.pos[2],
                          m.facing, m.slot, m.flags);
            lines.push_back(buf);
        }
        break;
    case DebugPage::Scene:
        std::snprintf(buf, sizeof buf, "chapter %d  event %d  scene %d  state %d  route %d", s.chapter, s.event, s.sceneNumber,
                      s.sceneState, s.routeChoice);
        lines.push_back(buf);
        std::snprintf(buf, sizeof buf, "draw zone %d  texture area %d  camera mode %d  drive %d", s.drawZone, s.textureArea,
                      s.cameraMode, s.cameraDriveMode);
        lines.push_back(buf);
        std::snprintf(buf, sizeof buf, "camera %.1f %.1f %.1f", s.cameraPos[0], s.cameraPos[1], s.cameraPos[2]);
        lines.push_back(buf);
        std::snprintf(buf, sizeof buf, "evt pc %s  opcode %u  %s", hex(s.evtPc).c_str(), s.evtOpcode, s.evtName.c_str());
        lines.push_back(buf);
        break;
    case DebugPage::Opcodes: {
        std::snprintf(buf, sizeof buf, "pc %s  op %u  %s", hex(s.evtPc).c_str(), s.evtOpcode, s.evtName.c_str());
        lines.push_back(buf);
        // the disassembly of the current section, the executing instruction marked
        for (const DebugInstruction& i : s.disasm) {
            std::string ops;
            for (size_t k = 0; k < i.operands.size() && k < 8; ++k) {
                if (!ops.empty())
                    ops += " ";
                ops += dec(i.operands[k]);
            }
            std::snprintf(buf, sizeof buf, "%s %s %-14s %s", i.current ? ">" : " ", hex(i.pc).c_str(), i.name.c_str(),
                          ops.c_str());
            lines.push_back(buf);
        }
        // the trace: the last instructions that ran, newest last
        lines.push_back("-- trace (newest last) --");
        int shown = 0;
        for (const Runtime::EvtTraceEntry& e : s.trace) {
            std::snprintf(buf, sizeof buf, "  f%-8llu %s op %-3u %s", static_cast<unsigned long long>(e.frame),
                          hex(e.pc).c_str(), e.opcode, e.handlerName.c_str());
            lines.push_back(buf);
            if (++shown >= 16)
                break;
        }
        break;
    }
    case DebugPage::Debug:
        std::snprintf(buf, sizeof buf, "frame %llu  mode %d / %d  drawn %zu  shootable %zu",
                      static_cast<unsigned long long>(s.frame), s.mode, s.submode, s.drawList, s.shootable);
        lines.push_back(buf);
        for (int p = 0; p < 4; ++p) {
            if (!s.seats[p].lives && !s.seats[p].score && !s.seats[p].rounds)
                continue;
            std::snprintf(buf, sizeof buf, "P%d  lives %d  rounds %d  score %d  state %d", p + 1, s.seats[p].lives,
                          s.seats[p].rounds, s.seats[p].score, s.seats[p].state);
            lines.push_back(buf);
        }
        std::snprintf(buf, sizeof buf, "boundaries %zu  missing %zu", s.boundaryCount, s.missingCount);
        lines.push_back(buf);
        for (const std::string& b : s.boundaryTop)
            lines.push_back("  " + b);
        break;
    }
    // the panel's height is bounded: the longest lines are cut
    if (lines.size() > kPanelHeight)
        lines.resize(kPanelHeight);
    return lines;
}

void DebugWindow::drawOverlay(const DebugSnapshot& s, int windowWidth, int windowHeight)
{
    if (!open || windowWidth <= 0 || windowHeight <= 0)
        return;
    if (!overlayInit_) {
        std::string err;
        overlayInit_ = overlay_.init(err);
        if (!overlayInit_)
            return;
        overlay_.translate = false;   // the debug text is not translated
        overlay_.setStyle(190, 235, 235, 220);
    }
    std::vector<std::string> lines = panelLines(s);
    lines.insert(lines.begin(), "=== " + panelTitle() + " (live debug) ===");
    overlay_.setText(lines, windowHeight >= 720 ? 2 : 1);
    overlay_.draw(windowWidth, windowHeight);
}

void DebugWindow::shutdownOverlay()
{
    if (overlayInit_)
        overlay_.shutdown();
    overlayInit_ = false;
}

bool DebugWindow::openSeparate(SDL_Window* gameWindow)
{
    if (separate_)
        return true;
    const int w = 760, h = 560;
    int x = 40, y = 40;
    if (gameWindow) {
        int gw = 0, gh = 0;
        SDL_GetWindowSize(gameWindow, &gw, &gh);
        int gx = 0, gy = 0;
        SDL_GetWindowPosition(gameWindow, &gx, &gy);
        x = gx + gw + 8;   // beside the game window
        y = gy;
    }
    separate_ = SDL_CreateWindow("THOTD ReWrithe - Live Debug", w, h,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!separate_)
        return false;   // the platform refused one (headless / CI): the overlay still works
    separateRenderer_ = SDL_CreateRenderer(separate_, "software");
    if (!separateRenderer_) {
        SDL_DestroyWindow(separate_);
        separate_ = nullptr;
        return false;
    }
    SDL_SetWindowPosition(separate_, x, y);
    SDL_ShowWindow(separate_);
    return true;
}

void DebugWindow::closeSeparate()
{
    if (separateRenderer_) {
        SDL_DestroyRenderer(separateRenderer_);
        separateRenderer_ = nullptr;
    }
    if (separate_) {
        SDL_DestroyWindow(separate_);
        separate_ = nullptr;
    }
}

bool DebugWindow::handleEvent(const SDL_Event& ev)
{
    if (!separate_)
        return false;
    if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(separate_)) {
        closeSeparate();
        return true;
    }
    return false;
}

void DebugWindow::drawSeparate(const DebugSnapshot& s)
{
    if (!separate_ || !separateRenderer_)
        return;
    int w = 0, h = 0;
    SDL_GetWindowSize(separate_, &w, &h);
    if (w <= 0 || h <= 0)
        return;
    SDL_SetRenderDrawColor(separateRenderer_, 0x23, 0x28, 0x30, 255);
    SDL_RenderClear(separateRenderer_);
    const std::vector<std::string> lines = panelLines(s);
    const int ch = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, gap = 3, pad = 8;
    int y = pad;
    const std::string title = "=== " + panelTitle() + " (live debug) ===";
    SDL_SetRenderDrawColor(separateRenderer_, 220, 235, 245, 255);
    SDL_RenderDebugText(separateRenderer_, float(pad), float(y), title.c_str());
    y += ch + gap;
    for (const std::string& l : lines) {
        if (y > h - ch - pad)
            break;
        SDL_SetRenderDrawColor(separateRenderer_, 200, 210, 220, 255);
        SDL_RenderDebugText(separateRenderer_, float(pad), float(y), l.c_str());
        y += ch + gap;
    }
    SDL_RenderPresent(separateRenderer_);
}

void DebugWindow::toggle(SDL_Window* gameWindow)
{
    open = !open;
    if (open)
        openSeparate(gameWindow);
    else
        closeSeparate();
}

void DebugWindow::nextPage()
{
    page = DebugPage((int(page) + 1) % 4);
}

} // namespace hotd::game
