//
// Created by cv2 on 08.09.2025.
//

// fs.c - A native Angara module written in C

#include "AngaraABI.h" // Include the ABI header
#include <stdio.h>
#include <stdlib.h>

// 1. Implement the native function with the correct signature.
AngaraObject fs_read_file(int arg_count, AngaraObject* args) {
    // 2. Perform manual type and arity checking.
    if (arg_count != 1) {
        angara_throw_error("read_file() expects 1 argument.");
        return angara_create_nil();
    }
    AngaraObject path_obj = args[0];
    if (path_obj.type != VAL_OBJ || ((Object*)path_obj.as.obj)->type != OBJ_STRING) {
        angara_throw_error("read_file() argument must be a string.");
        return angara_create_nil();
    }

    const char* path = ANGARA_AS_CSTRING(path_obj);

    // 3. Do the actual work.
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return angara_create_nil(); // Return nil on failure
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(file_size + 1);
    fread(buffer, sizeof(char), file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    // 4. Return an AngaraObject.
    AngaraObject result = angara_create_string(buffer);
    free(buffer);
    return result;
}

// 5. Create the definition array.
static const AngaraFuncDef FS_FUNCTIONS[] = {
    {"read_file", fs_read_file, 1},
    {NULL, NULL, 0} // Sentinel to mark the end of the array
};

// 6. Implement the exported entry point.
const AngaraFuncDef* AngaraModule_Init(int* def_count) {
    *def_count = (sizeof(FS_FUNCTIONS) / sizeof(AngaraFuncDef)) - 1;
    return FS_FUNCTIONS;
}