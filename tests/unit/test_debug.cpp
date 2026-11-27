// the live debug window's pure logic (game/debug_window.h, game/event_script.h) on synthetic memory, no ROMs: the
// operand-count table the interpreter and the disassembler share, the disassembler's instruction walk (fixed,
// list-ended and CCTB operand shapes, the current instruction's mark, the data-section terminator) and the bounded
// opcode trace the port's evt_trace hook records only while the window is open
#include "check.h"

#include "game/debug_window.h"
#include "game/event_script.h"
#include "port/mem.h"
#include "port/tgp.h"

#include <cstring>
#include <memory>

using namespace hotd;

namespace {

struct World {
    port::Mem mem;
    std::vector<uint32_t> copro = std::vector<uint32_t>(0x8000, 0);
    std::unique_ptr<port::Tgp> tgp;
    std::unique_ptr<game::Runtime> rt;
    World()
    {
        mem.map(port::Mem::kRam, port::Mem::kRamSize);
        mem.map(0xC0000, 0x10000);   // the arcade ROM area: evt_handlers (0xC6510) lives here
        tgp = std::make_unique<port::Tgp>(copro.data(), copro.size());
        rt = std::make_unique<game::Runtime>(mem, *tgp);
    }
};

} // namespace

TEST(debug, operands)
{
    // tools/evt_dis.py SPEC, the counts the interpreter's own switch uses
    CHECK_EQ(evtOperandCount(93), 0);   // NEXTEV
    CHECK_EQ(evtOperandCount(20), 1);   // BGTB
    CHECK_EQ(evtOperandCount(68), 2);
    CHECK_EQ(evtOperandCount(9), -1);   // ENTB: a list of record pointers
    CHECK_EQ(evtOperandCount(60), -2);  // CCTB: by sub-operation
    CHECK_EQ(evtOperandCount(999), -3); // not code: the event's data tables
}

TEST(debug, disassemble)
{
    World w;
    // the handler table: opcode 20 -> a named routine; opcode 21 -> an unnamed one
    constexpr uint32_t kNamed = 0x66C60;   // evt_BGTB
    w.mem.set32(game::A::G::evt_handlers + 20 * 4, kNamed);
    w.mem.set32(game::A::G::evt_handlers + 21 * 4, 0xDEAD0);
    w.rt->names[kNamed] = "evt_BGTB";

    // a script in work RAM: BGTB 7; CCTB 64 start end path mode; ENTB rec..., -1; then the data section
    constexpr uint32_t base = port::Mem::kRam + 0x1000;
    const uint32_t words[] = {
        20, 7,                          // +0: BGTB (one operand)
        60, 64, 10, 20, 3, 0,           // +8: CCTB sub 64 (five operands)
        9, base + 0x100, kEvtEnd,       // +32: ENTB's list ends with -1
        999, 123,                       // +44: unknown: the data section starts
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        w.mem.set32(base + 4 * uint32_t(i), words[i]);

    const auto ins = game::DebugWindow::disassemble(*w.rt, base, 16, base + 8);
    CHECK_EQ(ins.size(), size_t(4));
    CHECK_EQ(ins[0].opcode, 20u);
    CHECK_EQ(ins[0].name, std::string("evt_BGTB"));
    CHECK_EQ(ins[0].operands.size(), size_t(1));
    CHECK_EQ(ins[0].operands[0], 7);
    CHECK(!ins[0].current);
    CHECK(ins[1].current);             // currentPc = base + 8
    CHECK_EQ(ins[1].opcode, 60u);
    CHECK_EQ(ins[1].operands.size(), size_t(5));
    CHECK_EQ(ins[1].operands[0], 64);   // the CCTB sub-operation is an operand too
    CHECK_EQ(ins[1].operands[3], 3);
    CHECK_EQ(ins[2].opcode, 9u);
    CHECK_EQ(ins[2].operands.size(), size_t(1));
    CHECK_EQ(ins[2].operands[0], int32_t(base + 0x100));
    CHECK_EQ(ins[3].opcode, 999u);
    CHECK(!ins[3].valid);              // the walk stopped at the data section
}

TEST(debug, trace)
{
    World w;
    CHECK(!w.rt->traceEnabled);
    w.rt->evt_trace(0x1000, 20);       // the window is closed: no trace recorded
    CHECK(w.rt->evtTrace.empty());

    w.rt->traceEnabled = true;
    w.mem.set32(game::A::G::evt_handlers + 20 * 4, 0x66C60);
    w.rt->names[0x66C60] = "evt_BGTB";
    w.rt->evt_trace(0x1000, 20);
    CHECK_EQ(w.rt->evtTrace.size(), size_t(1));
    CHECK_EQ(w.rt->evtTrace[0].pc, 0x1000u);
    CHECK_EQ(w.rt->evtTrace[0].opcode, 20u);
    CHECK_EQ(w.rt->evtTrace[0].handlerName, std::string("evt_BGTB"));

    // bounded: a long game cannot grow the ring without end (newest last)
    for (uint32_t i = 0; i < game::Runtime::kEvtTraceMax + 20; ++i)
        w.rt->evt_trace(0x2000 + i * 4, 21);
    CHECK_EQ(w.rt->evtTrace.size(), game::Runtime::kEvtTraceMax);
    CHECK_EQ(w.rt->evtTrace.back().pc, 0x2000u + (game::Runtime::kEvtTraceMax + 19) * 4u);
}
