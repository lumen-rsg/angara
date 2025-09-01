//
// Created by cv2 on 9/1/25.
//

#include "LLVMCodeGenerator.h"

// --- LLVM Main Headers ---
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h" // Important for debugging generated IR

// --- LLVM Type Headers ---
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"

#include <stdexcept>

namespace angara {

// --- Constructor and Destructor ---

    LLVMCodeGenerator::LLVMCodeGenerator(TypeChecker& type_checker, ErrorHandler& errorHandler)
            : m_errorHandler(errorHandler), m_type_checker(type_checker) {
        // The constructor's job is to initialize the core LLVM objects.

        // 1. The LLVMContext is an opaque object that owns core LLVM data structures.
        m_context = std::make_unique<llvm::LLVMContext>();

        // 2. The Module is the top-level container for all generated code.
        //    We give it a name, which will be the name of the compilation unit.
        m_module = std::make_unique<llvm::Module>("AngaraModule", *m_context);

        // 3. The IRBuilder is our helper for creating LLVM instructions.
        //    We pass it the context.
        m_builder = std::make_unique<llvm::IRBuilder<>>(*m_context);

        // --- Pre-declare built-in functions like 'print' ---
        // Get the i8 type first.
        auto* char_type = llvm::Type::getInt8Ty(*m_context);
        // Then create a pointer to it using the modern API.
        auto* i8_ptr_type = llvm::PointerType::get(char_type, 0);

        llvm::FunctionType* print_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*m_context), // Return type: void
                { i8_ptr_type },                  // One parameter: i8*
                false                             // Not variadic
        );
        m_module->getOrInsertFunction("print", print_type);

    }

// The destructor is required for the std::unique_ptr to incomplete types pattern,
// but since we are including the headers now, a default one is fine.
    LLVMCodeGenerator::~LLVMCodeGenerator() = default;


// --- Main Entry Point ---

    std::unique_ptr<llvm::Module> LLVMCodeGenerator::generate(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_hadError = false;

        // --- NEW: Create the main function ---
        // All top-level code will be placed inside this function.
        // It will have the C signature 'int main(void)'.
        llvm::FunctionType* func_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*m_context), false);
        m_current_function = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, "main", m_module.get());

        // Create the entry basic block for this function.
        llvm::BasicBlock* entry_block = llvm::BasicBlock::Create(*m_context, "entry", m_current_function);
        m_builder->SetInsertPoint(entry_block);
        // --- END OF NEW ---

        // Iterate through all top-level statements and generate code for them.
        for (const auto& stmt : statements) {
            stmt->accept(*this, stmt);
            if (m_hadError) return nullptr;
        }

        // --- NEW: Terminate the main function ---
        // Every basic block must be terminated, e.g., with a return.
        // We'll have main return 0.
        m_builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*m_context), 0));
        // --- END OF NEW ---

        // --- Verification ---
        // After generating all the code, it's crucial to run the LLVM verifier.
        // This function performs a series of consistency checks on the generated IR.
        // If we made a mistake (e.g., a type mismatch), it will tell us.
        if (llvm::verifyModule(*m_module, &llvm::errs())) {
            m_hadError = true;
            // The verifier prints its own error messages to stderr.
            m_errorHandler.report(Token(), "LLVM module verification failed. See stderr for details.");
            return nullptr;
        }

        if (m_hadError) return nullptr;

        // Return ownership of the completed module to the caller.
        return std::move(m_module);
    }


// --- AST Visitor Implementations ---

// We need a helper to convert Angara types to LLVM types.
    llvm::Type* LLVMCodeGenerator::getLLVMType(const std::shared_ptr<Type>& angaraType) {
        // We use a visitor pattern on our own Type hierarchy to resolve this.
        // This is more robust than string comparison.

        switch (angaraType->kind) {
            case TypeKind::PRIMITIVE: {
                const auto& name = angaraType->toString();
                if (name == "i8" || name == "u8") return llvm::Type::getInt8Ty(*m_context);
                if (name == "i16" || name == "u16") return llvm::Type::getInt16Ty(*m_context);
                if (name == "i32" || name == "u32") return llvm::Type::getInt32Ty(*m_context);
                if (name == "i64" || name == "int" || name == "u64" || name == "uint") return llvm::Type::getInt64Ty(*m_context);
                if (name == "f32") return llvm::Type::getFloatTy(*m_context);
                if (name == "f64" || name == "float") return llvm::Type::getDoubleTy(*m_context);
                if (name == "bool") return llvm::Type::getInt1Ty(*m_context); // Bools are 1-bit integers in LLVM
                if (name == "string") {
                    // Strings are complex. A common representation is a pointer to a character.
                    // For now, let's use i8*.
                    auto* char_type = llvm::Type::getInt8Ty(*m_context);
                    return llvm::PointerType::get(char_type, 0);
                }
                if (name == "void") return llvm::Type::getVoidTy(*m_context);
                break;
            }
            case TypeKind::LIST: {
                // A list would be a pointer to a struct representing the list runtime object.
                // e.g., struct AngaraList { T* data; i64 length; i64 capacity; }
                // For now, we can use an opaque pointer.
                return llvm::Type::getVoidTy(*m_context);
            }
                // TODO: Add cases for RECORD, FUNCTION, etc.
            default:
                break;
        }

        // Default/fallback for unknown types.
        return llvm::Type::getVoidTy(*m_context);
    }


    std::any LLVMCodeGenerator::visit(const Literal& expr) {
        llvm::Value* value = nullptr;
        switch (expr.token.type) {
            case TokenType::NUMBER_INT: {
                // Create an LLVM constant integer.
                long long int_val = std::stoll(expr.token.lexeme);
                value = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_context), int_val, true);
                break;
            }
            case TokenType::NUMBER_FLOAT: {
                double double_val = std::stod(expr.token.lexeme);
                value = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*m_context), double_val);
                break;
            }
            case TokenType::STRING: {
                // --- THIS IS THE FIX for the deprecation warning ---
                // 1. Create a global variable containing the string data.
                llvm::GlobalVariable* gv = m_builder->CreateGlobalString(
                        expr.token.lexeme, ".str", 0, m_module.get());

                // 2. Get a pointer to the first element of that string array (an i8*).
                //    This is the "pointer decay" that C does automatically.
                value = m_builder->CreateConstInBoundsGEP2_32(gv->getValueType(), gv, 0, 0, "decay");
                // --- END OF FIX ---
                break;
            }

                // TODO: Handle bools, nil
            default:
                // For now, don't handle others.
                break;
        }

        // We return the generated llvm::Value* by wrapping it in std::any.
        return std::any(value);
    }


    void LLVMCodeGenerator::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        // This visitor handles 'let x = 10;' at the global scope.

        // 1. Generate the code for the initializer expression.
        stmt->initializer->accept(*this);
        auto initializer_result = stmt->initializer->accept(*this);
        llvm::Constant* initializer_val = static_cast<llvm::Constant*>(std::any_cast<llvm::Value*>(initializer_result));

        if (!initializer_val) {
            m_hadError = true;
            // error(...)
            return;
        }

        // 2. Create a new GlobalVariable in our LLVM Module.
        auto* global_var = new llvm::GlobalVariable(
                *m_module,
                initializer_val->getType(), // The type is derived from the initializer
                false, // isConstant -> false since it's a 'let'
                llvm::GlobalValue::ExternalLinkage,
                initializer_val, // The initial value
                stmt->name.lexeme // The name of the global
        );

        // 3. Store this global in our symbol table for later use.
        m_named_values[stmt->name.lexeme] = global_var;
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        // The purpose of an expression statement is its side effect.
        // We just generate the code for the expression and do nothing with the result.
        // LLVM will automatically handle dead code elimination if the expression
        // truly has no side effects.
        stmt->expression->accept(*this);
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const BlockStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const IfStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const WhileStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const ForStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const ForInStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const FuncStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const ReturnStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const ClassStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const TraitStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const AttachStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const ThrowStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const TryStmt> stmt) {
        // TODO: Implement
    }

    void LLVMCodeGenerator::visit(std::shared_ptr<const EmptyStmt> stmt) {
        // An empty statement generates no code, so this one is effectively complete.
    }


// --- Expressions ---
// For expressions, we must return a std::any, which we'll leave empty for the stubs.

    std::any LLVMCodeGenerator::visit(const Unary& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const Binary& expr) {
        // 1. Generate the LLVM Value for the left and right operands.
        auto lhs_val_any = expr.left->accept(*this);
        auto rhs_val_any = expr.right->accept(*this);
        auto* L = std::any_cast<llvm::Value*>(lhs_val_any);
        auto* R = std::any_cast<llvm::Value*>(rhs_val_any);

        if (!L || !R) {
            return std::any(nullptr); // Error occurred in a sub-expression
        }

        // 2. Get the static type of the operands from the Type Checker.
        auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());

        // 3. Create the correct instruction based on the type and operator.
        //    We'll assume for now if either is a float, the result is a float.
        bool is_float = (lhs_type->toString() == "f64"); // Simplified check

        switch (expr.op.type) {
            case TokenType::PLUS:
                return std::any(is_float ? m_builder->CreateFAdd(L, R, "faddtmp")
                                         : m_builder->CreateAdd(L, R, "addtmp"));
            case TokenType::MINUS:
                return std::any(is_float ? m_builder->CreateFSub(L, R, "fsubtmp")
                                         : m_builder->CreateSub(L, R, "subtmp"));
            case TokenType::STAR:
                return std::any(is_float ? m_builder->CreateFMul(L, R, "fmultmp")
                                         : m_builder->CreateMul(L, R, "multmp"));
            case TokenType::SLASH:
                // Integer division is signed (sdiv), float is fdiv.
                return std::any(is_float ? m_builder->CreateFDiv(L, R, "fdivtmp")
                                         : m_builder->CreateSDiv(L, R, "sdivtmp"));

                // TODO: Comparisons (e.g., CreateICmpULT, CreateFCmpULT)

            default:
                // error(...)
                return std::any(nullptr);
        }
    }

    std::any LLVMCodeGenerator::visit(const LogicalExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const Grouping& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const VarExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const AssignExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const UpdateExpr& expr) {
        // TODO: Implement
        return {};
    }


    std::any LLVMCodeGenerator::visit(const CallExpr& expr) {
        // 1. Look up the function in the module's symbol table.
        llvm::Function* callee_func = m_module->getFunction(
                // Simplification: assuming the callee is a simple variable name.
                std::dynamic_pointer_cast<const VarExpr>(expr.callee)->name.lexeme
        );

        if (!callee_func) {
            // error(...)
            return std::any(nullptr);
        }

        // 2. Generate code for all the arguments.
        std::vector<llvm::Value*> args_values;
        for (const auto& arg : expr.arguments) {
            auto arg_val_any = arg->accept(*this);
            args_values.push_back(std::any_cast<llvm::Value*>(arg_val_any));
        }

        // --- THIS IS THE FIX ---
        // 3. Check the function's return type to generate the correct call.
        if (callee_func->getReturnType()->isVoidTy()) {
            // If the function returns void, create a call instruction that does NOT
            // try to assign the result to a variable. We do this by omitting the name argument.
            llvm::Value* call_val = m_builder->CreateCall(callee_func, args_values);
            // The result of this expression is a void value, which we can represent with a nullptr.
            return std::any(call_val);
        } else {
            // If the function returns a value, create a call instruction that assigns
            // the result to a temporary virtual register (e.g., %calltmp).
            llvm::Value* call_val = m_builder->CreateCall(callee_func, args_values, "calltmp");
            return std::any(call_val);
        }
        // --- END OF FIX ---
    }

    std::any LLVMCodeGenerator::visit(const GetExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const ListExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const RecordExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const SubscriptExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const TernaryExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const ThisExpr& expr) {
        // TODO: Implement
        return {};
    }

    std::any LLVMCodeGenerator::visit(const SuperExpr& expr) {
        // TODO: Implement
        return {};
    }

} // namespace angara