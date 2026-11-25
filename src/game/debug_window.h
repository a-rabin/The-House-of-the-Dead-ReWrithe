// The live debug window (developer aid): what the game is doing right now - the creatures on screen, the scene and
// camera state, the event script's instructions (the live trace and the current section's disassembly), and the
// runtime's counters.
//
// Two views of ONE snapshot, so they can never disagree:
//   - the in-game overlay: text panels drawn over the picture with the Overlay (render/overlay.h), like the HUD's own
//     debug-lines panel (F1);
//   - a separate SDL window (software renderer): the same panels in a window of their own, for a wide screen.
//
// Everything here reads the game's memory; nothing writes it. The window is off by default and has no effect on the
// game. The opcode trace is recorded by the port's evt_trace hook (src/port/event.cpp) only while the window is open.
#pragma once

#include "game/runtime.h"
#include "port/mem.h"
#include "render/overlay.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace hotd::game {

// the panels the window shows (Tab cycles them)
enum class DebugPage { Monsters = 0, Scene = 1, Opcodes = 2, Debug = 3 };

// one creature as it is now (the live depth list's objects)
struct DebugMonster {
    uint32_t obj = 0;        // the object's address in the original memory
    int type = 0;            // the creature type (the BO_ table)
    std::string name;        // the type's short name ("Kenfis (dog)")
    float pos[3] = {};       // the world position
    float facing = 0;        // the facing in degrees
    int state = 0;           // the main state (the kind's state table)
    int hp = 0, hpMax = 0;   // the hit points (the spawn record's)
    int motion = 0, frame = 0;   // the motion and its frame
    uint32_t flags = 0;      // the object's flags (bit 0 = shootable)
    int slot = -1;           // the attack slot (or -1)
    int killer = -1;         // the player who killed it (-1 = nobody)
};

// one decoded event-script instruction
struct DebugInstruction {
    uint32_t pc = 0;         // the instruction's address
    uint32_t opcode = 0;     // the opcode
    std::string name;        // the handler's name ("evt_ENTB")
    std::vector<int32_t> operands;
    bool current = false;    // the instruction the interpreter is at now
    bool valid = true;       // false: the walk stopped (the data tables follow)
};

// everything the window shows, collected from the runtime in one go
struct DebugSnapshot {
    uint64_t frame = 0;          // the game frame
    int mode = 0, submode = 0;   // the game mode and its sub-mode
    // the scene
    int chapter = 0, event = 0, sceneNumber = 0, sceneState = 0;
    int routeChoice = 0, drawZone = 0, textureArea = -1;
    float cameraPos[3] = {};
    int cameraMode = 0, cameraDriveMode = 0;
    // the event script
    uint32_t evtPc = 0, evtOpcode = 0;
    std::string evtName;
    std::vector<DebugInstruction> disasm;    // the current section's instructions
    std::deque<Runtime::EvtTraceEntry> trace;   // the last instructions that ran (newest last)
    // the creatures
    std::vector<DebugMonster> monsters;
    int enemiesAlive = 0, enemyCount = 0;
    // the players
    struct Seat { int lives = 0, rounds = 0, score = 0, state = 0; };
    Seat seats[4];
    int seatsInGame = 0;
    // the counters
    size_t drawList = 0, shootable = 0;
    size_t boundaryCount = 0, missingCount = 0;
    std::vector<std::string> boundaryTop;   // the most-called boundaries
};

// the live debug window: the snapshot's collector, the panels' builder, and the two views
class DebugWindow {
public:
    // the window's state (the key toggle): the overlay is drawn while `open`; the separate window follows it
    bool open = false;
    DebugPage page = DebugPage::Monsters;

    // the panels of the current page (the shared model: the overlay and the separate window draw these lines)
    std::vector<std::string> panelLines(const DebugSnapshot& s) const;
    // the panel's title (the separate window's title bar and the overlay's first line)
    std::string panelTitle() const;
    // the snapshot of the runtime now
    DebugSnapshot collect(const Runtime& rt) const;

    // the overlay: draws the panels over the picture (the window's corners). The Overlay needs a GL context: it is
    // created on the first draw
    void drawOverlay(const DebugSnapshot& s, int windowWidth, int windowHeight);
    void shutdownOverlay();

    // the separate window: a software-rendered SDL window beside the game. False when the platform refuses one
    // (headless / CI): the overlay still works
    bool openSeparate(SDL_Window* gameWindow);
    void closeSeparate();
    bool separateOpen() const { return separate_ != nullptr; }
    // draws the panels into the separate window (and pumps its events); call once a frame
    void drawSeparate(const DebugSnapshot& s);
    // the separate window's events (its own close button); true when it asked to close
    bool handleEvent(const SDL_Event& ev);

    // the key toggle: opens / closes the window (the overlay and the separate one)
    void toggle(SDL_Window* gameWindow);
    // the Tab key: the next page
    void nextPage();

    // the disassembler: the instructions of the section at `pc` in the live memory (up to `max` of them)
    static std::vector<DebugInstruction> disassemble(const Runtime& rt, uint32_t pc, size_t max, uint32_t currentPc);

private:
    Overlay overlay_;
    bool overlayInit_ = false;
    SDL_Window* separate_ = nullptr;
    SDL_Renderer* separateRenderer_ = nullptr;
};

// the opcode's name, from the runtime's names and the event script's handler table ("evt_ENTB", or the opcode's
// number when the handler has no name)
std::string evtOpcodeName(const Runtime& rt, uint32_t opcode);

} // namespace hotd::game
