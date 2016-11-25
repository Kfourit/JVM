#include "jvm.h"
#include "utf8.h"
#include "natives.h"
#include "instructions.h"

#include "memoryinspect.h"
#include <string.h>

/// @brief Initializes a JavaVirtualMachine structure.
///
/// @param JavaVirtualMachine* jvm - pointer to the structure to be
/// initialized.
///
/// This function must be called before calling other JavaVirtualMachine functions.
///
/// @see executeJVM(), deinitJVM()
void initJVM(JavaVirtualMachine* jvm)
{
    jvm->status = JVM_STATUS_OK;
    jvm->simulatingSystemAndStringClasses = 0;
    jvm->frames = NULL;
    jvm->classes = NULL;
    jvm->objects = NULL;
}

/// @brief Deallocates all memory used by the JavaVirtualMachine structure.
///
/// @param JavaVirtualMachine* jvm - pointer to the structure to be
/// deallocated.
///
/// All loaded classes and objects created during the execution of the JVM
/// will be freed.
///
/// @see initJVM()
void deinitJVM(JavaVirtualMachine* jvm)
{
    freeFrameStack(&jvm->frames);

    LoadedClasses* classnode = jvm->classes;
    LoadedClasses* classtmp;

    while (classnode)
    {
        classtmp = classnode;
        classnode = classnode->next;
        closeClassFile(classtmp->jc);
        free(classtmp->jc);

        if (classtmp->staticFieldsData)
            free(classtmp->staticFieldsData);

        free(classtmp);
    }

    ReferenceTable* refnode = jvm->objects;
    ReferenceTable* reftmp;

    while (refnode)
    {
        reftmp = refnode;
        refnode = refnode->next;
        deleteReference(reftmp->obj);
        free(reftmp);
    }

    jvm->objects = NULL;
    jvm->classes = NULL;
}

/// @brief Executes the main method of a given class.
///
/// @param JavaVirtualMachine* jvm - pointer to an
/// already initialized JVM structure.
/// @param JavaClass* mainClass - pointer to a class
/// that contains the public void static main method.
///
/// If \c mainClass is a null pointer, the class
/// at the top of the stack of loaded classes will be
/// used as entry point. If no classes are loaded, then
/// the status of the JVM will be changed to
/// \c JVM_STATUS_MAIN_METHOD_NOT_FOUND.
/// If \c mainClass is not a null pointer, the class must
/// have been previously resolved with a call to \c resolveClass().
///
/// @see resolveClass(), JavaClass
void executeJVM(JavaVirtualMachine* jvm, JavaClass* mainClass)
{
    if (!mainClass)
    {
        if (!jvm->classes || !jvm->classes->jc)
        {
            jvm->status = JVM_STATUS_NO_CLASS_LOADED;
            return;
        }

        mainClass = jvm->classes->jc;
    }

    const uint8_t name[] = "main";
    const uint8_t descriptor[] = "([Ljava/lang/String;)V";

    method_info* method = getMethodMatching(mainClass, name, sizeof(name) - 1, descriptor, sizeof(descriptor) - 1, ACC_STATIC);

    if (!method)
    {
        jvm->status = JVM_STATUS_MAIN_METHOD_NOT_FOUND;
        return;
    }

    if (!runMethod(jvm, mainClass, method, 0))
        return;
}

/// @brief Loads a .class file.
///
///
uint8_t resolveClass(JavaVirtualMachine* jvm, const uint8_t* className_utf8_bytes, int32_t utf8_len, LoadedClasses** outClass)
{
    JavaClass* jc;
    cp_info* cpi;
    char path[1024];
    uint8_t success = 1;
    uint16_t u16;

    if (jvm->simulatingSystemAndStringClasses &&
        cmp_UTF8(className_utf8_bytes, utf8_len, (const uint8_t*)"java/lang/String", 16))
    {
        return 1;
    }

    // Arrays can also be a class in the constant pool that need
    // to be resolved.
    if (utf8_len > 0 && *className_utf8_bytes == '[')
    {
        do {
            utf8_len--;
            className_utf8_bytes++;
        } while (utf8_len > 0 && *className_utf8_bytes == '[');

        if (utf8_len == 0)
        {
            // Something wrong, as the class name was something like
            // "[["?
            return 0;
        }
        else if (*className_utf8_bytes != 'L')
        {
            // This class is an array of a primitive type, which
            // require no resolution.
            if (outClass)
                *outClass = NULL;
            return 1;
        }
        else
        {
            // This class is an array of instance of a certain class,
            // for example: "[Ljava/lang/String;", so we just try to
            // resolve the class java/lang/String.
            return resolveClass(jvm, className_utf8_bytes + 1, utf8_len - 2, outClass);
        }
    }

    LoadedClasses* loadedClass = isClassLoaded(jvm, className_utf8_bytes, utf8_len);

    if (loadedClass)
    {
        if (outClass)
            *outClass = loadedClass;

        return 1;
    }

#ifdef DEBUG
    printf("debug resolveClass %.*s\n", utf8_len, className_utf8_bytes);
#endif // DEBUG

    snprintf(path, sizeof(path), "%.*s.class", utf8_len, className_utf8_bytes);

    jc = (JavaClass*)malloc(sizeof(JavaClass));
    openClassFile(jc, path);

    if (jc->status != CLASS_STATUS_OK)
    {

#ifdef DEBUG
    printf("   class '%.*s' loading failed\n", utf8_len, className_utf8_bytes);
    printf("   status: %s\n", decodeJavaClassStatus(jc->status));
#endif // DEBUG

        success = 0;
    }
    else
    {
        if (jc->superClass)
        {
            cpi = jc->constantPool + jc->superClass - 1;
            cpi = jc->constantPool + cpi->Class.name_index - 1;
            success = resolveClass(jvm, cpi->Utf8.bytes, cpi->Utf8.length, NULL);
        }

        for (u16 = 0; success && u16 < jc->interfaceCount; u16++)
        {
            cpi = jc->constantPool + jc->interfaces[u16] - 1;
            cpi = jc->constantPool + cpi->Class.name_index - 1;
            success = resolveClass(jvm, cpi->Utf8.bytes, cpi->Utf8.length, NULL);
        }
    }

    if (success)
    {
        loadedClass = addClassToLoadedClasses(jvm, jc);
        success = loadedClass != NULL;
    }

    if (success)
    {

#ifdef DEBUG
    printf("   class file '%s' loaded\n", path);
#endif // DEBUG

        method_info* clinit = getMethodMatching(jc, (uint8_t*)"<clinit>", 8, (uint8_t*)"()V", 3, ACC_STATIC);

        if (clinit)
        {
            if (!runMethod(jvm, jc, clinit, 0))
                success = 0;
        }

        if (outClass)
            *outClass = loadedClass;
    }
    else
    {
        jvm->status = JVM_STATUS_CLASS_RESOLUTION_FAILED;
        closeClassFile(jc);
        free(jc);
    }

    return success;
}

uint8_t resolveMethod(JavaVirtualMachine* jvm, JavaClass* jc, cp_info* cp_method, LoadedClasses** outClass)
{
#ifdef DEBUG
    {
        char debugbuffer[256];
        uint32_t length = 0;
        cp_info* debugcpi = jc->constantPool + cp_method->Methodref.class_index - 1;
        debugcpi = jc->constantPool + debugcpi->Class.name_index - 1;
        length += snprintf(debugbuffer + length, sizeof(debugbuffer) - length, "%.*s.", debugcpi->Utf8.length, debugcpi->Utf8.bytes);
        debugcpi = jc->constantPool + cp_method->Methodref.name_and_type_index - 1;
        debugcpi = jc->constantPool + debugcpi->NameAndType.name_index - 1;
        length += snprintf(debugbuffer + length, sizeof(debugbuffer) - length, "%.*s:", debugcpi->Utf8.length, debugcpi->Utf8.bytes);
        debugcpi = jc->constantPool + cp_method->Methodref.name_and_type_index - 1;
        debugcpi = jc->constantPool + debugcpi->NameAndType.descriptor_index - 1;
        length += snprintf(debugbuffer + length, sizeof(debugbuffer) - length, "%.*s", debugcpi->Utf8.length, debugcpi->Utf8.bytes);
        printf("debug resolveMethod %s\n", debugbuffer);
    }
#endif // DEBUG

    cp_info* cpi;

    cpi = jc->constantPool + cp_method->Methodref.class_index - 1;
    cpi = jc->constantPool + cpi->Class.name_index - 1;

    // Resolve the class the method belongs to
    if (!resolveClass(jvm, cpi->Utf8.bytes, cpi->Utf8.length, outClass))
        return 0;

    // Get method descriptor
    cpi = jc->constantPool + cp_method->Methodref.name_and_type_index - 1;
    cpi = jc->constantPool + cpi->NameAndType.descriptor_index - 1;

    uint8_t* descriptor_bytes = cpi->Utf8.bytes;
    int32_t descriptor_len = cpi->Utf8.length;
    int32_t length;

    while (descriptor_len > 0)
    {
        // We increment our descriptor here. This will make the first
        // character to be lost, but the first character in a method
        // descriptor is a parenthesis, so it doesn't matter.
        descriptor_bytes++;
        descriptor_len--;

        switch(*descriptor_bytes)
        {
            // if the method has a class as parameter or as return type,
            // that class must be resolved
            case 'L':

                length = -1;

                do {
                    descriptor_bytes++;
                    descriptor_len--;
                    length++;
                } while (*descriptor_bytes != ';');

                if (!resolveClass(jvm, descriptor_bytes - length, length, NULL))
                    return 0;

                break;

            default:
                break;
        }
    }

    return 1;
}

uint8_t resolveField(JavaVirtualMachine* jvm, JavaClass* jc, cp_info* cp_field, LoadedClasses** outClass)
{

#ifdef DEBUG
    {
        char debugbuffer[256];
        uint32_t length = 0;
        cp_info* debugcpi = jc->constantPool + cp_field->Fieldref.class_index - 1;
        debugcpi = jc->constantPool + debugcpi->Class.name_index - 1;
        length += snprintf(debugbuffer + length, sizeof(debugbuffer) - length, "%.*s.", debugcpi->Utf8.length, debugcpi->Utf8.bytes);
        debugcpi = jc->constantPool + cp_field->Fieldref.name_and_type_index - 1;
        debugcpi = jc->constantPool + debugcpi->NameAndType.name_index - 1;
        length += snprintf(debugbuffer + length, sizeof(debugbuffer) - length, "%.*s:", debugcpi->Utf8.length, debugcpi->Utf8.bytes);
        debugcpi = jc->constantPool + cp_field->Fieldref.name_and_type_index - 1;
        debugcpi = jc->constantPool + debugcpi->NameAndType.descriptor_index - 1;
        length += snprintf(debugbuffer + length, sizeof(debugbuffer) - length, "%.*s", debugcpi->Utf8.length, debugcpi->Utf8.bytes);
        printf("debug resolveField %s\n", debugbuffer);
    }
#endif // DEBUG

    cp_info* cpi;

    cpi = jc->constantPool + cp_field->Fieldref.class_index - 1;
    cpi = jc->constantPool + cpi->Class.name_index - 1;

    // Resolve the class the field belongs to
    if (!resolveClass(jvm, cpi->Utf8.bytes, cpi->Utf8.length, outClass))
        return 0;

    // Get field descriptor
    cpi = jc->constantPool + cp_field->Fieldref.name_and_type_index - 1;
    cpi = jc->constantPool + cpi->NameAndType.descriptor_index - 1;

    uint8_t* descriptor_bytes = cpi->Utf8.bytes;
    int32_t descriptor_len = cpi->Utf8.length;

    // Skip '[' characters, in case this field is an array
    while (*descriptor_bytes == '[')
    {
        descriptor_bytes++;
        descriptor_len--;
    }

    // If the type of this field is a class, then that
    // class must also be resolved
    if (*descriptor_bytes == 'L')
    {
        if (!resolveClass(jvm, descriptor_bytes + 1, descriptor_len - 2, NULL))
            return 0;
    }

    return 1;
}

uint8_t runMethod(JavaVirtualMachine* jvm, JavaClass* jc, method_info* method, uint8_t numberOfParameters)
{
#ifdef DEBUG
    {
        char debugbuffer[256];
        decodeAccessFlags(method->access_flags, debugbuffer, sizeof(debugbuffer), ACCT_METHOD);
        cp_info* debug_cpi = jc->constantPool + method->name_index - 1;
        printf("debug runMethod %s %.*s, params: %u",  debugbuffer, debug_cpi->Utf8.length, debug_cpi->Utf8.bytes, numberOfParameters);
    }
#endif // DEBUG

    Frame* callerFrame = jvm->frames ? jvm->frames->frame : NULL;
    Frame* frame = newFrame(jc, method);

#ifdef DEBUG
    printf(", len: %u, frame %X%s\n", frame->code_length, frame, frame->code_length == 0 ? " ####### Native Method": "");
#endif // DEBUG

    if (!frame || !pushFrame(&jvm->frames, frame))
    {
        jvm->status = JVM_STATUS_OUT_OF_MEMORY;
        return 0;
    }

    if (method->access_flags & ACC_NATIVE)
    {
        // TODO: run native
    }
    else
    {
        uint8_t parameterIndex;
        int32_t parameter;

        for (parameterIndex = 0; parameterIndex < numberOfParameters; parameterIndex++)
        {
            popOperand(&callerFrame->operands, &parameter, NULL);
            frame->localVariables[numberOfParameters - parameterIndex - 1] = parameter;
        }

        InstructionFunction function;

        while (frame->pc < frame->code_length)
        {

            uint8_t opcode = *(frame->code + frame->pc++);
            function = fetchOpcodeFunction(opcode);

#ifdef DEBUG
    printf("   instruction '%s' at offset %u of frame %X\n", getOpcodeMnemonic(opcode), frame->pc - 1, frame);
#endif // DEBUG

            if (function == NULL)
            {

#ifdef DEBUG
    printf("   unknown instruction '%s'\n", getOpcodeMnemonic(opcode));
#endif // DEBUG

                jvm->status = JVM_STATUS_UNKNOWN_INSTRUCTION;
                break;
            }
            else if (!function(jvm, frame))
            {
                return 0;
            }
        }

        if (frame->returnCount > 0 && callerFrame)
        {
            // At most, two operands can be returned
            OperandStack parameters[2];
            uint8_t index;

            for (index = 0; index < frame->returnCount; index++)
                popOperand(&frame->operands, &parameters[index].value, &parameters[index].type);

            while (frame->returnCount-- > 0)
            {
                if (!pushOperand(&callerFrame->operands, parameters[frame->returnCount].value, parameters[frame->returnCount].type))
                {
                    jvm->status = JVM_STATUS_OUT_OF_MEMORY;
                    return 0;
                }
            }
        }
    }

    popFrame(&jvm->frames, NULL);
    freeFrame(frame);
    return jvm->status == JVM_STATUS_OK;
}

uint8_t getMethodDescriptorParameterCount(const uint8_t* descriptor_utf8, int32_t utf8_len)
{
    uint8_t parameterCount = 0;

    while (utf8_len > 0)
    {
        switch (*descriptor_utf8)
        {
            case '(': break;
            case ')': return parameterCount;

            case 'J': case 'D':
                parameterCount += 2;
                break;

            case 'L':

                parameterCount++;

                do {
                    utf8_len--;
                    descriptor_utf8++;
                } while (utf8_len > 0 && *descriptor_utf8 != ';');

                break;

            case '[':

                parameterCount++;

                do {
                    utf8_len--;
                    descriptor_utf8++;
                } while (utf8_len > 0 && *descriptor_utf8 == '[');

                do {
                    utf8_len--;
                    descriptor_utf8++;
                } while (utf8_len > 0 && *descriptor_utf8 != ';');

                break;

            case 'F': // float
            case 'B': // byte
            case 'C': // char
            case 'I': // int
            case 'S': // short
            case 'Z': // boolean
                parameterCount++;
                break;

            default:
                break;
        }

        descriptor_utf8++;
        utf8_len--;
    }

    return parameterCount;
}

LoadedClasses* addClassToLoadedClasses(JavaVirtualMachine* jvm, JavaClass* jc)
{
    LoadedClasses* node = (LoadedClasses*)malloc(sizeof(LoadedClasses));

    if (node)
    {
        node->jc = jc;

        if (jc->staticFieldCount > 0)
            node->staticFieldsData = (int32_t*)malloc(sizeof(int32_t) * jc->staticFieldCount);
        else
            node->staticFieldsData = NULL;

        node->next = jvm->classes;

        jvm->classes = node;
    }

    return node;
}

LoadedClasses* isClassLoaded(JavaVirtualMachine* jvm, const uint8_t* utf8_bytes, int32_t utf8_len)
{
    LoadedClasses* classes = jvm->classes;
    JavaClass* jc;
    cp_info* cpi;

    while (classes)
    {
        jc = classes->jc;
        cpi = jc->constantPool + jc->thisClass - 1;
        cpi = jc->constantPool + cpi->Class.name_index - 1;

        if (cmp_UTF8(cpi->Utf8.bytes, cpi->Utf8.length, utf8_bytes, utf8_len))
            return classes;

        classes = classes->next;
    }

    return NULL;
}

JavaClass* getSuperClass(JavaVirtualMachine* jvm, JavaClass* jc)
{
    LoadedClasses* superLoadedClass;
    cp_info* cp1;

    cp1 = jc->constantPool + jc->superClass - 1;
    cp1 = jc->constantPool + cp1->Class.name_index - 1;

    superLoadedClass = isClassLoaded(jvm, cp1->Utf8.bytes, cp1->Utf8.length);

    return superLoadedClass ? superLoadedClass->jc : NULL;
}

/// @pre Both \c super and \c jc must be classes that have already
/// been loaded.
/// @note If \c super and \c jc point to the same class, the function
/// returns false.
uint8_t isClassSuperOf(JavaVirtualMachine* jvm, JavaClass* super, JavaClass* jc)
{
    cp_info* cp1;
    cp_info* cp2;
    LoadedClasses* classes;

    cp2 = super->constantPool + super->thisClass - 1;
    cp2 = super->constantPool + cp2->Class.name_index - 1;

    while (jc && jc->superClass)
    {
        cp1 = jc->constantPool + jc->superClass - 1;
        cp1 = jc->constantPool + cp1->Class.name_index - 1;

        if (cmp_UTF8(cp1->Utf8.bytes, cp1->Utf8.length, cp2->Utf8.bytes, cp2->Utf8.length))
            return 1;

        classes = isClassLoaded(jvm, cp1->Utf8.bytes, cp1->Utf8.length);

        if (classes)
            jc = classes->jc;
        else
            break;
    }

    return 0;
}

Reference* newString(JavaVirtualMachine* jvm, const uint8_t* str, int32_t strlen)
{
    Reference* r = (Reference*)malloc(sizeof(Reference));
    ReferenceTable* node = (ReferenceTable*)malloc(sizeof(ReferenceTable));

    if (!node || !r)
    {
        if (node) free(node);
        if (r) free(r);

        return NULL;
    }

    r->type = REFTYPE_STRING;
    r->str.len = strlen;

    if (strlen)
    {
        r->str.utf8_bytes = (uint8_t*)malloc(strlen);

        if (!r->str.utf8_bytes)
        {
            free(r);
            free(node);
            return NULL;
        }

        memcpy(r->str.utf8_bytes, str, strlen);
    }
    else
    {
        r->str.utf8_bytes = NULL;
    }

    node->next = jvm->objects;
    node->obj = r;
    jvm->objects = node;

    return r;
}

Reference* newClassInstance(JavaVirtualMachine* jvm, JavaClass* jc)
{
    Reference* r = (Reference*)malloc(sizeof(Reference));
    ReferenceTable* node = (ReferenceTable*)malloc(sizeof(ReferenceTable));

    if (!node || !r)
    {
        if (node) free(node);
        if (r) free(r);

        return NULL;
    }

    r->type = REFTYPE_CLASSINSTANCE;
    r->ci.c = jc;
    r->ci.data = (uint8_t*)malloc(jc->instanceFieldCount);

    if (!r->ci.data)
    {
        free(r);
        free(node);
        return NULL;
    }

    node->next = jvm->objects;
    node->obj = r;
    jvm->objects = node;

    return r;
}

Reference* newArray(JavaVirtualMachine* jvm, uint32_t length, Opcode_newarray_type type)
{
    if (length == 0)
        return NULL;

    size_t elementSize;

    switch (type)
    {
        case T_BOOLEAN:
        case T_BYTE:
            elementSize = sizeof(uint8_t);
            break;

        case T_SHORT:
        case T_CHAR:
            elementSize = sizeof(uint16_t);
            break;

        case T_FLOAT:
        case T_INT:
            elementSize = sizeof(uint32_t);
            break;

        case T_DOUBLE:
        case T_LONG:
            elementSize = sizeof(uint64_t);
            break;

        default:
            // Can't create array of other data type
            return NULL;
    }

    Reference* r = (Reference*)malloc(sizeof(Reference));
    ReferenceTable* node = (ReferenceTable*)malloc(sizeof(ReferenceTable));

    if (!node || !r)
    {
        if (node) free(node);
        if (r) free(r);

        return NULL;
    }

    r->type = REFTYPE_ARRAY;
    r->arr.length = length;
    r->arr.type = type;
    r->arr.data = (uint8_t*)malloc(elementSize * length);

    if (!r->arr.data)
    {
        free(r);
        free(node);
        return NULL;
    }

    uint8_t* data = (uint8_t*)r->arr.data;

    while (length-- > 0)
        *data++ = 0;

    node->next = jvm->objects;
    node->obj = r;
    jvm->objects = node;

    return r;
}

Reference* newObjectArray(JavaVirtualMachine* jvm, uint32_t length, const uint8_t* utf8_className, int32_t utf8_len)
{
    if (utf8_len <= 0)
        return NULL;

    Reference* r = (Reference*)malloc(sizeof(Reference));
    ReferenceTable* node = (ReferenceTable*)malloc(sizeof(ReferenceTable));

    if (!node || !r)
    {
        if (node) free(node);
        if (r) free(r);

        return NULL;
    }

    r->type = REFTYPE_OBJARRAY;
    r->oar.dimensions = 1;

    // This is a hack, we don't actually allocate 1 byte, but rather
    // just use the address as the dimension length, since there is
    // only 1 dimension.
    r->oar.dims_length = (uint32_t*)length;
    r->oar.elementCount = length;
    r->oar.utf8_className = (uint8_t*)malloc(utf8_len);
    r->oar.elements = (Reference**)malloc(length * sizeof(Reference));
    r->oar.utf8_len = utf8_len;

    if (!r->oar.utf8_className || !r->oar.elements)
    {
        free(r);
        free(node);

        if (r->oar.utf8_className) free(r->oar.utf8_className);
        if (r->oar.elements) free(r->oar.elements);

        return NULL;
    }

    // Copy class name
    while (utf8_len-- > 0)
        r->oar.utf8_className[utf8_len] = utf8_className[utf8_len];

    // Initializes all references to null
    while (length-- > 0)
        r->oar.elements[length] = NULL;

    node->next = jvm->objects;
    node->obj = r;
    jvm->objects = node;

    return r;
}

void deleteReference(Reference* obj)
{
    switch (obj->type)
    {
        case REFTYPE_STRING:
            if (obj->str.utf8_bytes)
                free(obj->str.utf8_bytes);
            break;

        case REFTYPE_ARRAY:
            free(obj->arr.data);
            break;

        case REFTYPE_CLASSINSTANCE:
            free(obj->ci.data);
            break;

        case REFTYPE_OBJARRAY:
        {
            uint32_t numberOfElements = 0;
            uint8_t dimIndex;

            if (obj->oar.dimensions > 1)
            {
                for (dimIndex = 0, numberOfElements = 1; dimIndex < obj->oar.dimensions; dimIndex++)
                    numberOfElements *= obj->oar.dims_length[dimIndex];

                free(obj->oar.dims_length);
            }

            free(obj->oar.utf8_className);

            while (numberOfElements-- > 0)
                deleteReference(obj->oar.elements[numberOfElements]);

            free(obj->oar.elements);
            break;
        }

        default:
            return;
    }

    free(obj);
}
