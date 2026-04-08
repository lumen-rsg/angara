//
// Angara Runtime — Main coordinator
//
// All runtime implementations have been split into logical units:
//   rt_core.c       — Value constructors, ARC, GC, memory management
//   rt_string.c     — String operations
//   rt_list.c       — List operations
//   rt_record.c     — Record (dynamic map) operations
//   rt_exception.c  — Exception handling (try/catch/throw)
//   rt_thread.c     — Threading and concurrency
//   rt_conversion.c — Type conversions, equality, typeof, deep_clone
//   rt_ffi.c        — FFI boxing/unboxing, closures, bound methods, call dispatch
//   rt_print.c      — Printing and display
//
// This file is retained for backward compatibility and as the central
// include point. All implementation lives in the rt_*.c files.
//

#include "rt_internal.h"