#include "test_llvm_simple.h"

// --- Data Constructor Implementations ---

// --- Data Equals Function Implementations ---

// --- Data Clone Function Implementations ---

// --- Enum Constructor Implementations ---

// --- Enum Equals Implementations ---

// --- Deep Clone Implementations ---
// --- Global Variable & Function Closure Storage ---
AngaraObject g_test_llvm_simple_add;
AngaraObject g_test_llvm_simple_factorial;
AngaraObject g_test_llvm_simple_fibonacci;
AngaraObject g_angara_main_closure;


// --- Internal Forward Declarations ---
static AngaraObject angara_f_test_llvm_simple_add(AngaraObject a, AngaraObject b);
static AngaraObject angara_w_angara_f_test_llvm_simple_add(int arg_count, AngaraObject args[]);
static AngaraObject angara_f_test_llvm_simple_factorial(AngaraObject n);
static AngaraObject angara_w_angara_f_test_llvm_simple_factorial(int arg_count, AngaraObject args[]);
static AngaraObject angara_f_test_llvm_simple_fibonacci(AngaraObject n);
static AngaraObject angara_w_angara_f_test_llvm_simple_fibonacci(int arg_count, AngaraObject args[]);
AngaraObject angara_f_main(void);
AngaraObject angara_w_angara_f_main(int arg_count, AngaraObject args[]);

void Angara_test_llvm_simple_init_globals(void) {
  g_test_llvm_simple_add = angara_closure_new(&angara_w_angara_f_test_llvm_simple_add, 2, false);
  g_test_llvm_simple_factorial = angara_closure_new(&angara_w_angara_f_test_llvm_simple_factorial, 1, false);
  g_test_llvm_simple_fibonacci = angara_closure_new(&angara_w_angara_f_test_llvm_simple_fibonacci, 1, false);
  g_angara_main_closure = angara_closure_new(&angara_w_angara_f_main, 0, false);
}

// --- Function Implementations ---
static AngaraObject angara_f_test_llvm_simple_add(AngaraObject a, AngaraObject b) {
  return angara_create_i64((AS_I64(a) + AS_I64(b)));
}

static AngaraObject angara_w_angara_f_test_llvm_simple_add(int arg_count, AngaraObject args[]) {
  return angara_f_test_llvm_simple_add(args[0], args[1]);
}

static AngaraObject angara_f_test_llvm_simple_factorial(AngaraObject n) {
  if (angara_is_truthy(angara_create_bool((AS_F64(n) <= AS_F64(angara_create_i64(1LL))))))   {
    return angara_create_i64(1LL);
  }
  return angara_create_i64((AS_I64(n) * AS_I64(angara_call(g_test_llvm_simple_factorial, 1, (AngaraObject[]){angara_create_i64((AS_I64(n) - AS_I64(angara_create_i64(1LL))))}))));
}

static AngaraObject angara_w_angara_f_test_llvm_simple_factorial(int arg_count, AngaraObject args[]) {
  return angara_f_test_llvm_simple_factorial(args[0]);
}

static AngaraObject angara_f_test_llvm_simple_fibonacci(AngaraObject n) {
  if (angara_is_truthy(angara_create_bool((AS_F64(n) <= AS_F64(angara_create_i64(1LL))))))   {
    return n;
  }
  return angara_create_i64((AS_I64(angara_call(g_test_llvm_simple_fibonacci, 1, (AngaraObject[]){angara_create_i64((AS_I64(n) - AS_I64(angara_create_i64(1LL))))})) + AS_I64(angara_call(g_test_llvm_simple_fibonacci, 1, (AngaraObject[]){angara_create_i64((AS_I64(n) - AS_I64(angara_create_i64(2LL))))}))));
}

static AngaraObject angara_w_angara_f_test_llvm_simple_fibonacci(int arg_count, AngaraObject args[]) {
  return angara_f_test_llvm_simple_fibonacci(args[0]);
}

AngaraObject angara_f_main(void) {
  AngaraObject x = angara_call(g_test_llvm_simple_add, 2, (AngaraObject[]){angara_create_i64(3LL), angara_create_i64(4LL)});
  AngaraObject f = angara_call(g_test_llvm_simple_factorial, 1, (AngaraObject[]){angara_create_i64(10LL)});
  AngaraObject fib = angara_call(g_test_llvm_simple_fibonacci, 1, (AngaraObject[]){angara_create_i64(10LL)});
  return angara_create_nil();
}

AngaraObject angara_w_angara_f_main(int arg_count, AngaraObject args[]) {
  angara_f_main();
  return angara_create_nil();
}

// --- C Entry Point ---
int main(int argc, const char* argv[]) {
  angara_runtime_init();

  // --- Initialize All Modules ---
  Angara_test_llvm_simple_init_globals();

  // Call the user's Angara main function
    AngaraObject result = angara_call(g_angara_main_closure, 0, NULL);

  int exit_code = (int)AS_I64(result);
  angara_decref(result);

  angara_runtime_shutdown();
  return exit_code;
}
