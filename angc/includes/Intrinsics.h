// ============================================================================
// Intrinsics.h — the single source of truth for Angara's intrinsic set.
//
// This pure-data header (no LLVM dependency) defines every compiler-recognized
// intrinsic: its name, signature, safety gate, target architecture, expected
// codegen result kind, and a one-line note. It is consumed by:
//
//   • the LSP server (hover text + completion items),
//   • the LLVM backend (codegen dispatch + coverage self-check),
//   • the docs (the tables in 23-bare-metal.md mirror this array).
//
// Adding a new intrinsic means adding one row here AND one lowering arm in
// ExprCodegen.cpp's name-ladder. The codegen self-check
// (LLVMBackend::verifyIntrinsicCoverage) asserts that every name in
// kIntrinsics[] has a matching arm, so the two cannot silently drift.
//
// The lowering arm itself stays in the codegen ladder — the 7 distinct
// lowering styles (void-asm, asm-with-input, register-read, MMIO peek/poke,
// LLVM intrinsic, LLVM atomic IR, special multi-step) are too varied for a
// function-pointer table.
// ============================================================================

#pragma once

#include <cstddef>
#include <string>

namespace angara {

// Safety gate required to call the intrinsic at the source level.
enum class IntrinsicGate {
    None,        // callable anywhere (e.g. halt, nop, clz)
    Unsafe,      // requires an @unsafe block (most system ops)
    Privileged,  // requires a @privileged block (eret/set_spsr/set_elr)
};

// Target architecture the intrinsic lowers for. Most integer/atomic/bit
// intrinsics are target-agnostic (LLVM picks the lowering); the system-register
// and cache ops are AArch64-only.
enum class IntrinsicArch {
    Any,      // LLVM IR — portable (atomics, bit ops, halt, nop)
    AArch64,  // inline asm (mrs/msr/dc/ic/tlbi/eret/daif/wfi/…) — AArch64 only
};

// What the codegen lowering returns. Used by the coverage self-check to verify
// each arm produces the declared kind.
enum class IntrinsicResult {
    Nil,
    I64,
};

struct IntrinsicInfo {
    const char* name;
    const char* signature;   // "(addr as i64) -> i64" etc. — LSP hover + type-check
    IntrinsicGate gate;
    IntrinsicArch arch;
    IntrinsicResult result;
    const char* note;        // one-line description — LSP hover + docs
};

// ---------------------------------------------------------------------------
// The intrinsic table. Ordered to match the codegen name-ladder
// (ExprCodegen.cpp) and the docs tables (23-bare-metal.md):
//   MMIO → control/barriers → CPU registers → atomics → bit manipulation →
//   DAIF → cache → context/vector → MMU → TLB → privilege switching
// ---------------------------------------------------------------------------
inline constexpr IntrinsicInfo kIntrinsics[] = {
    // ── MMIO (volatile load/store, width-parametric) ──
    {"peek8",  "(addr as i64) -> i64",               IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::I64, "volatile load, 8-bit"},
    {"peek16", "(addr as i64) -> i64",               IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::I64, "volatile load, 16-bit"},
    {"peek32", "(addr as i64) -> i64",               IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::I64, "volatile load, 32-bit"},
    {"peek64", "(addr as i64) -> i64",               IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::I64, "volatile load, 64-bit"},
    {"poke8",  "(addr as i64, val as i64) -> nil",   IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::Nil, "volatile store, 8-bit"},
    {"poke16", "(addr as i64, val as i64) -> nil",   IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::Nil, "volatile store, 16-bit"},
    {"poke32", "(addr as i64, val as i64) -> nil",   IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::Nil, "volatile store, 32-bit"},
    {"poke64", "(addr as i64, val as i64) -> nil",   IntrinsicGate::Unsafe,     IntrinsicArch::Any,     IntrinsicResult::Nil, "volatile store, 64-bit"},

    // ── Termination / no-op ──
    {"halt", "() -> nil",  IntrinsicGate::None, IntrinsicArch::Any, IntrinsicResult::Nil, "llvm.trap + infinite loop (terminal)"},
    {"nop",  "() -> nil",  IntrinsicGate::None, IntrinsicArch::Any, IntrinsicResult::Nil, "llvm.donothing"},

    // ── Barriers / wait (AArch64 instructions) ──
    {"wfi",    "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "wait for interrupt"},
    {"wfe",    "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "wait for event"},
    {"sev",    "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "send event (wake other cores)"},
    {"dmb",    "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "data memory barrier (full-system)"},
    {"dsb",    "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "data sync barrier (full-system)"},
    {"isb",    "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "instruction sync barrier"},
    {"dmb_st", "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "store-store barrier (ishst)"},
    {"dsb_st", "() -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "store-store sync (ishst)"},

    // ── CPU register reads ──
    {"get_el",    "() -> i64", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "CurrentEL >> 2 (exception level 0-3)"},
    {"get_mpidr", "() -> i64", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "MPIDR_EL1 (CPU affinity)"},
    {"cntfrq",    "() -> i64", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "CNTFRQ_EL0 (timer frequency)"},
    {"cntpct",    "() -> i64", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "CNTPCT_EL0 (physical timer count)"},

    // ── Atomic memory operations (monotonic, LLVM atomic IR) ──
    {"atomic_load",  "(addr as i64) -> i64",                              IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "monotonic load"},
    {"atomic_store", "(addr as i64, val as i64) -> nil",                  IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::Nil, "monotonic store"},
    {"atomic_cas",   "(addr as i64, expected as i64, desired as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "cmpxchg; returns old value (spinlock: while(cas(p,0,1)!=0){})"},
    {"atomic_add",   "(addr as i64, val as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "atomicrmw add; returns old"},
    {"atomic_sub",   "(addr as i64, val as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "atomicrmw sub; returns old"},
    {"atomic_or",    "(addr as i64, val as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "atomicrmw or; returns old"},
    {"atomic_and",   "(addr as i64, val as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "atomicrmw and; returns old"},
    {"atomic_xor",   "(addr as i64, val as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "atomicrmw xor; returns old"},
    {"atomic_xchg",  "(addr as i64, val as i64) -> i64", IntrinsicGate::Unsafe, IntrinsicArch::Any, IntrinsicResult::I64, "atomicrmw xchg; returns old"},

    // ── Bit manipulation (LLVM intrinsics — target-agnostic) ──
    {"clz",  "(x as i64) -> i64", IntrinsicGate::None, IntrinsicArch::Any, IntrinsicResult::I64, "count leading zeros (llvm.ctlz)"},
    {"ctz",  "(x as i64) -> i64", IntrinsicGate::None, IntrinsicArch::Any, IntrinsicResult::I64, "count trailing zeros (llvm.cttz)"},
    {"rev",  "(x as i64) -> i64", IntrinsicGate::None, IntrinsicArch::Any, IntrinsicResult::I64, "byte-reverse (llvm.bswap)"},
    {"rbit", "(x as i64) -> i64", IntrinsicGate::None, IntrinsicArch::Any, IntrinsicResult::I64, "bit-reverse (llvm.bitreverse)"},

    // ── Interrupt control (DAIF) ──
    {"enable_irq",  "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "daifclr #2"},
    {"disable_irq", "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "daifset #2"},
    {"enable_fiq",  "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "daifclr #1"},
    {"disable_fiq", "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "daifset #1"},
    {"get_daif",    "() -> i64",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "mrs daif"},
    {"set_daif",    "(daif as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "msr daif"},

    // ── Cache maintenance ──
    {"dc_ivac",  "(addr as i64) -> nil",                          IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate D-cache to PoC"},
    {"dc_cvac",  "(addr as i64) -> nil",                          IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "clean D-cache to PoC"},
    {"dc_civac", "(addr as i64) -> nil",                          IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "clean+invalidate D-cache"},
    {"ic_ivau",  "(addr as i64) -> nil",                          IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate I-cache to PoU"},
    {"dc_csw",   "(set as i64, way as i64, level as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set/way maintenance (encodes CCSIDR geometry)"},

    // ── Context switching / vector table ──
    {"get_sp",    "() -> i64",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "current stack pointer"},
    {"get_fp",    "() -> i64",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "frame pointer (x29)"},
    {"ttbr0_el1", "() -> i64",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "translation table base (MMU)"},
    {"set_vbar",  "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set exception vector base (msr vbar_el1)"},

    // ── MMU control registers ──
    {"set_ttbr0", "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set translation table base 0 (msr ttbr0_el1)"},
    {"set_mair",  "(val as i64) -> nil",  IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set memory attribute indirection (msr mair_el1)"},
    {"set_tcr",   "(val as i64) -> nil",  IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set translation control (msr tcr_el1)"},
    {"set_sctlr", "(val as i64) -> nil",  IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set system control (msr sctlr_el1; bit 0 = MMU enable)"},
    {"get_sctlr", "() -> i64",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::I64, "read system control (mrs sctlr_el1)"},

    // ── TLB management (full EL1 TLBI instruction space) ──
    {"tlbi_vmalle1",   "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate all TLB entries, EL1 (local)"},
    {"tlbi_vmalle1is", "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate all TLB entries, EL1 (inner-shareable)"},
    {"tlbi_alle1",     "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate current-ASID entries (local)"},
    {"tlbi_alle1is",   "() -> nil",            IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate current-ASID entries (IS)"},
    {"tlbi_vae1",      "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate by VA, ASID-specific (local)"},
    {"tlbi_vae1is",    "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate by VA, ASID-specific (IS)"},
    {"tlbi_vaae1",     "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate by VA, ASID-agnostic (local)"},
    {"tlbi_vaae1is",   "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate by VA, ASID-agnostic (IS)"},
    {"tlbi_aside1",    "(asid as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate by ASID (local)"},
    {"tlbi_aside1is",  "(asid as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate by ASID (IS)"},
    {"tlbi_vale1",     "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate last-level by VA, ASID-specific (local)"},
    {"tlbi_vale1is",   "(addr as i64) -> nil", IntrinsicGate::Unsafe, IntrinsicArch::AArch64, IntrinsicResult::Nil, "invalidate last-level by VA, ASID-specific (IS)"},

    // ── Privilege switching (@privileged) ──
    {"eret",     "() -> nil",            IntrinsicGate::Privileged, IntrinsicArch::AArch64, IntrinsicResult::Nil, "exception return (terminal; jumps to elr_el1 at spsr_el1 pstate)"},
    {"set_spsr", "(daif as i64) -> nil", IntrinsicGate::Privileged, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set saved pstate (msr spsr_el1)"},
    {"set_elr",  "(addr as i64) -> nil", IntrinsicGate::Privileged, IntrinsicArch::AArch64, IntrinsicResult::Nil, "set exception link (msr elr_el1)"},
};

inline constexpr size_t kIntrinsicCount = std::size(kIntrinsics);

// Linear lookup by name. Returns nullptr if not found. The table is small
// (~60 entries); a binary search or hash is not worth the complexity.
inline const IntrinsicInfo* findIntrinsic(const std::string& name) {
    for (const auto& ii : kIntrinsics) {
        if (name == ii.name) return &ii;
    }
    return nullptr;
}

// Human-readable gate label for LSP/docs ("@unsafe", "@privileged", or "").
inline const char* intrinsicGateLabel(IntrinsicGate g) {
    switch (g) {
        case IntrinsicGate::Unsafe:     return "@unsafe";
        case IntrinsicGate::Privileged: return "@privileged";
        default:                        return "";
    }
}

} // namespace angara
