//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::escape_c_string(const std::string& str) {
        std::stringstream ss;
        for (char c : str) {
            switch (c) {
                case '\\': ss << "\\\\"; break;
                case '"':  ss << "\\\""; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                    // Add other standard C escapes as needed
                default:
                    ss << c;
                    break;
            }
        }
        return ss.str();
    }

    std::string CTranspiler::sanitize_name(const std::string& name) {
        // A simple set of C keywords to avoid.
        static const std::set<std::string> c_keywords = {
                "auto", "break", "case", "char", "const", "continue", "default",
                "do", "double", "else", "enum", "extern", "float", "for", "goto",
                "if", "int", "long", "register", "return", "short", "signed",
                "sizeof", "static", "struct", "switch", "typedef", "union",
                "unsigned", "void", "volatile", "while"
        };
        if (c_keywords.count(name)) {
            return name + "_";
        }
        return name;
    }

    void CTranspiler::indent() {
        for (int i = 0; i < m_indent_level; ++i) {
            (*m_current_out) << "  ";
        }
    }

     const ClassType* CTranspiler::findPropertyOwner(const ClassType* klass, const std::string& prop_name) {
        if (!klass) return nullptr;
        if (klass->fields.count(prop_name) || klass->methods.count(prop_name)) {
            return klass;
        }
        return findPropertyOwner(klass->superclass.get(), prop_name);
    }

    std::string CTranspiler::getCType(const std::shared_ptr<Type>& angaraType) {
        if (!angaraType) return "void /* unknown type */";
        // All other reference types are AngaraObject.
        return "AngaraObject";
    }

    std::string CTranspiler::join_strings(const std::vector<std::string>& elements, const std::string& separator) {
        std::stringstream ss;
        for (size_t i = 0; i < elements.size(); ++i) {
            ss << elements[i];
            if (i < elements.size() - 1) {
                ss << separator;
            }
        }
        return ss.str();
    }

    const FuncStmt* CTranspiler::findMethodAst(const ClassStmt& class_stmt, const std::string& name) {
        // 1. Iterate through all the members defined in the class's AST node.
        for (const auto& member : class_stmt.members) {
            // 2. Try to cast the generic member to a MethodMember.
            if (auto method_member = std::dynamic_pointer_cast<const MethodMember>(member)) {
                // 3. If it's a method, check if its name matches the one we're looking for.
                if (method_member->declaration->name.lexeme == name) {
                    // 4. Found it. Return a raw pointer to the FuncStmt node.
                    return method_member->declaration.get();
                }
            }
        }
        // 5. If we've searched all members and haven't found it, return nullptr.
        return nullptr;
    }

#include "CTranspiler.h"

    // ... other helpers in this file ...

    std::string CTranspiler::getRawCType(const std::shared_ptr<Type>& angaraType) {
        if (!angaraType) {
            // This case should ideally not be hit if the type checker is correct,
            // but "void" is a safe fallback.
            return "void";
        }

        // Handle special (non-primitive) types first.
        if (angaraType->kind == TypeKind::NIL) {
            return "void";
        }
        if (angaraType->kind == TypeKind::C_PTR) {
            return "void*";
        }

        // Use the string representation for a simple mapping of primitive types.
        const std::string& typeName = angaraType->toString();

        // C String
        if (typeName == "string") return "const char*";

        // Signed Integers (from <stdint.h>)
        if (typeName == "i64") return "int64_t";
        if (typeName == "i32") return "int32_t";
        if (typeName == "i16") return "int16_t";
        if (typeName == "i8")  return "int8_t";

        // Unsigned Integers (from <stdint.h>)
        if (typeName == "u64") return "uint64_t";
        if (typeName == "u32") return "uint32_t";
        if (typeName == "u16") return "uint16_t";
        if (typeName == "u8")  return "uint8_t";

        // Floating-Point Types
        if (typeName == "f64") return "double";
        if (typeName == "f32") return "float";

        // Boolean Type (from <stdbool.h>, which angara_runtime.h includes)
        if (typeName == "bool") return "bool";

        // Default case: If the type is not a primitive that can be directly
        // mapped to C, it must be passed as an opaque AngaraObject handle.
        // This would apply to lists, records, class instances, etc.
        return "AngaraObject";
    }

    std::string CTranspiler::getCTypeNameForSizeof(const std::shared_ptr<Type>& angaraType) {
        if (auto data_type = std::dynamic_pointer_cast<DataType>(angaraType)) {
            if (data_type->is_foreign) {
                // For `sizeof<utsname>`, we need `struct utsname`.
                return "struct " + data_type->name;
            } else {
                // For `sizeof<MyData>`, we need `struct Angara_MyData`.
                return "struct Angara_" + data_type->name;
            }
        }
        // For primitives, fall back to the FFI type mapping.
        return getRawCType(angaraType);
    }

}