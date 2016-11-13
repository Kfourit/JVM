#include "attributes.h"
#include "readfunctions.h"
#include "utf8.h"
#include "opcodes.h"
#include <stdlib.h>
#include <inttypes.h> // Usage of macro "PRId64" to print 64 bit integer

#define DECLARE_ATTR_FUNCS(attr) \
    uint8_t readAttribute##attr(JavaClassFile* jcf, attribute_info* entry); \
    void printAttribute##attr(JavaClassFile* jcf, attribute_info* entry, int identationLevel); \
    void freeAttribute##attr(attribute_info* entry);

DECLARE_ATTR_FUNCS(SourceFile)
DECLARE_ATTR_FUNCS(InnerClasses)
DECLARE_ATTR_FUNCS(LineNumberTable)
DECLARE_ATTR_FUNCS(ConstantValue)
DECLARE_ATTR_FUNCS(Code)
DECLARE_ATTR_FUNCS(Deprecated)
DECLARE_ATTR_FUNCS(Exceptions)

char readAttribute(JavaClassFile* jcf, attribute_info* entry)
{
    entry->info = NULL;

    if (!readu2(jcf, &entry->name_index) ||
        !readu4(jcf, &entry->length))
    {
        jcf->status = UNEXPECTED_EOF;
        return 0;
    }

    // Checks if name_index points to a valid name index in
    // the constant pool
    cp_info* cp = jcf->constantPool + entry->name_index - 1;

    if (entry->name_index == 0 ||
        entry->name_index >= jcf->constantPoolCount ||
        cp->tag != CONSTANT_Utf8)
    {
        jcf->status = INVALID_NAME_INDEX;
        return 0;
    }

    #define IF_ATTR_CHECK(name) \
        if (cmp_UTF8_Ascii(cp->Utf8.bytes, cp->Utf8.length, (uint8_t*)#name, sizeof(#name) - 1)) { \
            entry->attributeType = ATTR_##name; \
            result = readAttribute##name(jcf, entry); \
        }

    uint32_t totalBytesRead = jcf->totalBytesRead;
    char result;

    IF_ATTR_CHECK(ConstantValue)
    else IF_ATTR_CHECK(SourceFile)
    else IF_ATTR_CHECK(InnerClasses)
    else IF_ATTR_CHECK(Code)
    else IF_ATTR_CHECK(LineNumberTable)
    else IF_ATTR_CHECK(Deprecated)
    else IF_ATTR_CHECK(Exceptions)
    else
    {
        uint32_t u32;

        for (u32 = 0; u32 < entry->length; u32++)
        {
            if (fgetc(jcf->file) == EOF)
            {
                jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
                return 0;
            }

            jcf->totalBytesRead++;
        }

        result = 1;
        entry->attributeType = ATTR_Unknown;
    }

    if (jcf->totalBytesRead - totalBytesRead != entry->length)
    {
        jcf->status = ATTRIBUTE_LENGTH_MISMATCH;
        return 0;
    }

    #undef ATTR_CHECK
    return result;
}

void ident(int level)
{
    while (level-- > 0)
        printf("\t");
}

uint8_t readAttributeDeprecated(JavaClassFile* jcf, attribute_info* entry)
{
    entry->info = NULL;
    return 1;
}

void printAttributeDeprecated(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    ident(identationLevel);
    printf("This element is marked as deprecated and should no longer be used.");
}

void freeAttributeDeprecated(attribute_info* entry)
{

}

uint8_t readAttributeConstantValue(JavaClassFile* jcf, attribute_info* entry)
{
    att_ConstantValue_info* info = (att_ConstantValue_info*)malloc(sizeof(att_ConstantValue_info));
    entry->info = (void*)info;

    if (!info)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    if (!readu2(jcf, &info->constantvalue_index))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    if (info->constantvalue_index == 0 ||
        info->constantvalue_index >= jcf->constantPoolCount)
    {
        jcf->status = ATTRIBUTE_INVALID_CONSTANTVALUE_INDEX;
        return 0;
    }

    cp_info* cp = jcf->constantPool + info->constantvalue_index - 1;

    if (cp->tag != CONSTANT_String && cp->tag != CONSTANT_Float &&
        cp->tag != CONSTANT_Double && cp->tag != CONSTANT_Long &&
        cp->tag != CONSTANT_Integer)
    {
        jcf->status = ATTRIBUTE_INVALID_CONSTANTVALUE_INDEX;
        return 0;
    }

    return 1;
}

void printAttributeConstantValue(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    char buffer[48];

    att_ConstantValue_info* info = (att_ConstantValue_info*)entry->info;
    cp_info* cp = jcf->constantPool + info->constantvalue_index - 1;

    ident(identationLevel);
    printf("constantvalue_index: #%u <", info->constantvalue_index);

    switch (cp->tag)
    {
        case CONSTANT_Integer:
            printf("%d", (int32_t)cp->Integer.value);
            break;

        case CONSTANT_Long:
            printf("%" PRId64, ((int64_t)cp->Long.high << 32) | cp->Long.low);
            break;

        case CONSTANT_Float:
            printf("%e", readConstantPoolFloat(cp));
            break;

        case CONSTANT_Double:
            printf("%e", readConstantPoolDouble(cp));
            break;

        case CONSTANT_String:
            cp = jcf->constantPool + cp->String.string_index - 1;
            UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cp->Utf8.bytes, cp->Utf8.length);
            printf("%s", buffer);
            break;

        default:
            printf(" - unknown constant tag - ");
            break;
    }

    printf(">");
}

void freeAttributeConstantValue(attribute_info* entry)
{
    if (entry->info)
        free(entry->info);

    entry->info = NULL;
}

uint8_t readAttributeSourceFile(JavaClassFile* jcf, attribute_info* entry)
{
    att_SourceFile_info* info = (att_SourceFile_info*)malloc(sizeof(att_SourceFile_info));
    entry->info = (void*)info;

    if (!info)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    if (!readu2(jcf, &info->sourcefile_index))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    if (info->sourcefile_index == 0 ||
        info->sourcefile_index >= jcf->constantPoolCount ||
        jcf->constantPool[info->sourcefile_index - 1].tag != CONSTANT_Utf8)
    {
        jcf->status = ATTRIBUTE_INVALID_SOURCEFILE_INDEX;
        return 0;
    }

    return 1;
}

void printAttributeSourceFile(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    char buffer[48];
    att_SourceFile_info* info = (att_SourceFile_info*)entry->info;
    cp_info* cp = jcf->constantPool + info->sourcefile_index - 1;

    UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cp->Utf8.bytes, cp->Utf8.length);

    ident(identationLevel);
    printf("sourcefile_index: #%u <%s>", info->sourcefile_index, buffer);
}

void freeAttributeSourceFile(attribute_info* entry)
{
    if (entry->info)
        free(entry->info);

    entry->info = NULL;
}

uint8_t readAttributeInnerClasses(JavaClassFile* jcf, attribute_info* entry)
{
    att_InnerClasses_info* info = (att_InnerClasses_info*)malloc(sizeof(att_InnerClasses_info));
    entry->info = (void*)info;

    if (!info)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    info->inner_classes = NULL;

    if (!readu2(jcf, &info->number_of_classes))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    info->inner_classes = (InnerClassInfo*)malloc(info->number_of_classes * sizeof(InnerClassInfo));

    if (!info->inner_classes)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    uint16_t u16;
    InnerClassInfo* icf = info->inner_classes;

    for (u16 = 0; u16 < info->number_of_classes; u16++, icf++)
    {
        if (!readu2(jcf, &icf->inner_class_index) ||
            !readu2(jcf, &icf->outer_class_index) ||
            !readu2(jcf, &icf->inner_class_name_index) ||
            !readu2(jcf, &icf->inner_class_access_flags))
        {
            jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
            return 0;
        }

        if (icf->inner_class_index == 0 ||
            icf->inner_class_index >= jcf->constantPoolCount ||
            jcf->constantPool[icf->inner_class_index - 1].tag != CONSTANT_Class ||
            icf->outer_class_index >= jcf->constantPoolCount ||
            (icf->outer_class_index > 0 && jcf->constantPool[icf->outer_class_index - 1].tag != CONSTANT_Class) ||
            icf->inner_class_name_index == 0 ||
            icf->inner_class_name_index >= jcf->constantPoolCount ||
            jcf->constantPool[icf->inner_class_name_index - 1].tag != CONSTANT_Utf8)
        {
            jcf->status = ATTRIBUTE_INVALID_INNERCLASS_INDEXES;
            return 0;
        }
    }

    return 1;
}

void printAttributeInnerClasses(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    att_InnerClasses_info* info = (att_InnerClasses_info*)entry->info;
    cp_info* cp;
    char buffer[48];
    uint16_t index;
    InnerClassInfo* innerclass = info->inner_classes;

    ident(identationLevel);
    printf("number_of_classes: %u", info->number_of_classes);

    for (index = 0; index < info->number_of_classes; index++, innerclass++)
    {
        printf("\n\n");
        ident(identationLevel);
        printf("Inner Class #%u:\n\n", index + 1);

        // inner_class_index
        cp = jcf->constantPool + innerclass->inner_class_index - 1;
        cp = jcf->constantPool + cp->Class.name_index - 1;
        UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cp->Utf8.bytes, cp->Utf8.length);
        ident(identationLevel + 1);
        printf("inner_class_info_index:   cp index #%u <%s>\n", innerclass->inner_class_index, buffer);

        // outer_class_index
        ident(identationLevel + 1);
        printf("outer_class_info_index:   cp index #%u ", innerclass->outer_class_index);

        if (innerclass->outer_class_index == 0)
        {
            printf("(no outer class)\n");
        }
        else
        {
            cp = jcf->constantPool + innerclass->outer_class_index - 1;
            cp = jcf->constantPool + cp->Class.name_index - 1;
            UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cp->Utf8.bytes, cp->Utf8.length);
            printf("<%s>\n", buffer);
        }

        // inner_class_name_index
        ident(identationLevel + 1);
        printf("inner_name_index:         cp index #%u ", innerclass->inner_class_name_index);

        if (innerclass->inner_class_name_index == 0)
        {
            printf("(anonymous class)\n");
        }
        else
        {
            cp = jcf->constantPool + innerclass->inner_class_name_index - 1;
            UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cp->Utf8.bytes, cp->Utf8.length);
            printf("<%s>\n", buffer);
        }

        // inner_class_access_flags
        decodeAccessFlags(innerclass->inner_class_access_flags, buffer, sizeof(buffer), ACCT_INNERCLASS);
        ident(identationLevel + 1);
        printf("inner_class_access_flags: 0x%.4X [%s]", innerclass->inner_class_access_flags, buffer);
    }
}

void freeAttributeInnerClasses(attribute_info* entry)
{
    att_InnerClasses_info* info = (att_InnerClasses_info*)entry->info;

    if (info)
    {
        if (info->inner_classes)
            free(info->inner_classes);

        free(info);
    }

    entry->info = NULL;
}

uint8_t readAttributeLineNumberTable(JavaClassFile* jcf, attribute_info* entry)
{
    att_LineNumberTable_info* info = (att_LineNumberTable_info*)malloc(sizeof(att_LineNumberTable_info));
    entry->info = (void*)info;

    if (!info)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    info->line_number_table = NULL;

    if (!readu2(jcf, &info->line_number_table_length))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    info->line_number_table = (LineNumberTableEntry*)malloc(info->line_number_table_length * sizeof(LineNumberTableEntry));

    if (!info->line_number_table)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    uint16_t u16;
    LineNumberTableEntry* lnte = info->line_number_table;

    for (u16 = 0; u16 < info->line_number_table_length; u16++, lnte++)
    {
        if (!readu2(jcf, &lnte->start_pc) ||
            !readu2(jcf, &lnte->line_number))
        {
            jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
            return 0;
        }
    }

    return 1;
}

void printAttributeLineNumberTable(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    att_LineNumberTable_info* info = (att_LineNumberTable_info*)entry->info;
    LineNumberTableEntry* lnte = info->line_number_table;
    uint16_t index;

    printf("\n");
    ident(identationLevel);
    printf("line_number_table_length: %u\n\n", info->line_number_table_length);
    ident(identationLevel);
    printf("Table:\tIndex\tline_number\tstart_pc");

    for (index = 0; index < info->line_number_table_length; index++, lnte++)
    {
        printf("\n");
        ident(identationLevel);
        printf("\t%u\t%u\t\t%u", index + 1, lnte->line_number, lnte->start_pc);
    }
}

void freeAttributeLineNumberTable(attribute_info* entry)
{
    att_LineNumberTable_info* info = (att_LineNumberTable_info*)entry->info;

    if (info)
    {
        if (info->line_number_table)
            free(info->line_number_table);

        free(info);
        entry->info = NULL;
    }
}

uint8_t readAttributeCode(JavaClassFile* jcf, attribute_info* entry)
{
    att_Code_info* info = (att_Code_info*)malloc(sizeof(att_Code_info));
    entry->info = (void*)info;
    uint32_t u32;

    if (!info)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    info->code = NULL;
    info->exception_table = NULL;

    if (!readu2(jcf, &info->max_stack) ||
        !readu2(jcf, &info->max_locals) ||
        !readu4(jcf, &info->code_length))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    if (info->code_length == 0 || info->code_length >= 65536)
    {
        jcf->status = ATTRIBUTE_INVALID_CODE_LENGTH;
        return 0;
    }

    info->code = (uint8_t*)malloc(info->code_length);

    if (!info->code)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    for (u32 = 0; u32 < info->code_length; u32++)
    {
        int byte = fgetc(jcf->file);

        if (byte == EOF)
        {
            jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
            return 0;
        }

        jcf->totalBytesRead++;
        *(info->code + u32) = (uint8_t)byte;
    }

    // TODO: check if all instructions are valid and have correct parameters.

    if (!readu2(jcf, &info->exception_table_length))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    info->exception_table = (ExceptionTableEntry*)malloc(info->exception_table_length * sizeof(ExceptionTableEntry));

    if (!info->exception_table)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    ExceptionTableEntry* except = info->exception_table;

    for (u32 = 0; u32 < info->exception_table_length; u32++)
    {
        if (!readu2(jcf, &except->start_pc) ||
            !readu2(jcf, &except->end_pc) ||
            !readu2(jcf, &except->handler_pc) ||
            !readu2(jcf, &except->catch_type))
        {
            jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
            return 0;
        }

        // TODO: check if start_pc, end_pc and handler_pc are valid program counters inside
        // the code. Also check if catch_type is a pointer to a valid class with Throwable as
        // parent, or if it is NULL (finally block).
    }

    if (!readu2(jcf, &info->attributes_count))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    info->attributes = (attribute_info*)malloc(info->attributes_count * sizeof(attribute_info));

    if (!info->attributes)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    for (u32 = 0; u32 < info->attributes_count; u32++)
    {
        if (!readAttribute(jcf, info->attributes + u32))
            return 0;
    }

    return 1;
}

void printAttributeCode(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    att_Code_info* info = (att_Code_info*)entry->info;
    uint32_t code_offset;

    printf("\n");
    ident(identationLevel);
    printf("max_stack: %u, max_locals: %u, code_length: %u\n", info->max_stack, info->max_locals, info->code_length);
    ident(identationLevel);
    printf("exception_table_length: %u, attribute_count: %u\n\n", info->exception_table_length, info->attributes_count);
    ident(identationLevel);
    printf("Code:\tOffset\tMnemonic\tParameters");

    identationLevel++;

    char buffer[48];
    uint8_t opcode;
    uint16_t u16;
    cp_info* cpi;

    for (code_offset = 0; code_offset < info->code_length; code_offset++)
    {
        opcode = *(info->code + code_offset);

        printf("\n");
        ident(identationLevel);
        printf("%u\t%s", code_offset, getOpcodeMnemonic(opcode));

        #define OPCODE_INTERVAL(begin, end) (opcode >= opcode_##begin && opcode <= opcode_##end)

        // These are all the opcodes that have no parameters
        if (OPCODE_INTERVAL(nop, aconst_null) || OPCODE_INTERVAL(iconst_0, dconst_1) ||
            OPCODE_INTERVAL(iload_0, saload) || OPCODE_INTERVAL(istore_0, lxor) ||
            OPCODE_INTERVAL(i2l, lcmp) || OPCODE_INTERVAL(ireturn, return) ||
            OPCODE_INTERVAL(arraylength, athrow) || OPCODE_INTERVAL(monitorenter, monitorexit))
        {
            continue;
        }

        #undef OPCODE_INTERVAL

        #define NEXTBYTE (*(info->code + ++code_offset))

        switch (opcode)
        {
            case opcode_iload: case opcode_fload: case opcode_dload:
            case opcode_lload: case opcode_aload: case opcode_istore:
            case opcode_lstore: case opcode_fstore: case opcode_dstore:
            case opcode_astore: case opcode_ret:

                printf("\t%u", NEXTBYTE);
                break;

            case opcode_newarray:

                u16 = NEXTBYTE;
                printf("\t%u (array of %s)", u16, decodeOpcodeNewarrayType(u16));
                break;

            case opcode_bipush:
                printf("\t%d", (int8_t)NEXTBYTE);
                break;

            case opcode_sipush:
                u16 = (uint16_t)NEXTBYTE << 8;
                printf("\t%d", (int16_t)u16 | NEXTBYTE);
                break;

            case opcode_ldc:
            case opcode_ldc_w:
            case opcode_ldc2_w:

                u16 = NEXTBYTE;

                if (opcode == opcode_ldc_w)
                    u16 = (u16 << 8) | NEXTBYTE;

                printf("\t#%u");

                cpi = jcf->constantPool + u16 - 1;

                if (opcode == opcode_ldc2_w)
                {
                    if (cpi->tag == CONSTANT_Long)
                        printf("\tlong:    %" PRId64, ((int64_t)cpi->Long.high << 32) | cpi->Long.low);
                    else if (cpi->tag == CONSTANT_Double)
                        printf("\tdouble:  %e", readConstantPoolDouble(cpi));
                    else
                        printf("\t%s (invalid)", decodeTag(cpi->tag));
                }
                else
                {
                    if (cpi->tag == CONSTANT_Class)
                    {
                        cpi = jcf->constantPool + cpi->Class.name_index - 1;
                        UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cpi->Utf8.bytes, cpi->Utf8.length);
                        printf("\tstring:  <%s>", buffer);
                    }
                    else if (cpi->tag == CONSTANT_Integer)
                    {
                        printf("\tinteger: %d", (int32_t)cpi->Integer.value);
                    }
                    else if (cpi->tag == CONSTANT_Float)
                    {
                        printf("\tfloat:   %e", readConstantPoolFloat(cpi));
                    }
                    else
                    {
                        printf("\t%s (invalid)", decodeTag(cpi->tag));
                    }
                }

                break;

                // TODO: finish all instructions parameters

            default:
                printf("\n");
                ident(identationLevel);
                printf("- last instruction was not recognized, can't continue -");
                // To stop the for loop, as not knowing the opcode could lead to wrong interpretation of bytes
                code_offset = info->code_length;
                break;
        }
    }

    printf("\n");

    identationLevel--;

    if (info->attributes_count > 0)
    {
        for (u16 = 0; u16 < info->attributes_count; u16++)
        {
            attribute_info* atti = info->attributes + u16;
            cpi = jcf->constantPool + atti->name_index - 1;
            UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cpi->Utf8.bytes, cpi->Utf8.length);

            printf("\n");
            ident(identationLevel);
            printf("Code Attribute #%u - %s:\n", u16 + 1, buffer);
            printAttribute(jcf, atti, identationLevel + 1);
        }
    }

    // TODO: print code attributes
    // TODO: print exception table
}

void freeAttributeCode(attribute_info* entry)
{
    att_Code_info* info = (att_Code_info*)entry->info;

    if (info)
    {
        if (info->code)
            free(info->code);

        if (info->exception_table)
            free(info->exception_table);

        free(info);
        entry->info = NULL;
    }
}

uint8_t readAttributeExceptions(JavaClassFile* jcf, attribute_info* entry)
{
    att_Exceptions_info* info = (att_Exceptions_info*)malloc(sizeof(att_Exceptions_info));
    entry->info = (void*)info;

    if (!info)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    info->exception_index_table = NULL;

    if (!readu2(jcf, &info->number_of_exceptions))
    {
        jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
        return 0;
    }

    info->exception_index_table = (uint16_t*)malloc(info->number_of_exceptions * sizeof(uint16_t));

    if (!info->exception_index_table)
    {
        jcf->status = MEMORY_ALLOCATION_FAILED;
        return 0;
    }

    uint16_t u16;
    uint16_t* exception_index = info->exception_index_table;

    for (u16 = 0; u16 < info->number_of_exceptions; u16++, exception_index++)
    {
        if (!readu2(jcf, exception_index))
        {
            jcf->status = UNEXPECTED_EOF_READING_ATTRIBUTE_INFO;
            return 0;
        }

        if (*exception_index == 0 ||
            *exception_index >= jcf->constantPoolCount ||
            jcf->constantPool[*exception_index - 1].tag != CONSTANT_Class)
        {
            jcf->status = ATTRIBUTE_INVALID_EXCEPTIONS_CLASS_INDEX;
            return 0;
        }
    }

    return 1;
}

void printAttributeExceptions(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    att_Exceptions_info* info = (att_Exceptions_info*)entry->info;
    uint16_t* exception_index = info->exception_index_table;
    uint16_t index;
    char buffer[48];
    cp_info* cpi;

    ident(identationLevel);
    printf("number_of_exceptions: %u", info->number_of_exceptions);

    for (index = 0; index < info->number_of_exceptions; index++, exception_index++)
    {
        cpi = jcf->constantPool + *exception_index - 1;
        cpi = jcf->constantPool + cpi->Class.name_index - 1;
        UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cpi->Utf8.bytes, cpi->Utf8.length);

        printf("\n\n");
        ident(identationLevel + 1);
        printf("Exception #%u: cp index #%u <%s>\n", index + 1, *exception_index, buffer);
    }
}

void freeAttributeExceptions(attribute_info* entry)
{
    att_Exceptions_info* info = (att_Exceptions_info*)entry->info;

    if (info)
    {
        if (info->exception_index_table)
            free(info->exception_index_table);

        free(info);
        entry->info = NULL;
    }
}

void freeAttributeInfo(attribute_info* entry)
{
    #define ATTR_CASE(attr) case ATTR_##attr: freeAttribute##attr(entry); return;

    switch (entry->attributeType)
    {
        ATTR_CASE(Code)
        ATTR_CASE(LineNumberTable)
        ATTR_CASE(SourceFile)
        ATTR_CASE(InnerClasses)
        ATTR_CASE(ConstantValue)
        ATTR_CASE(Deprecated)
        ATTR_CASE(Exceptions)
        default:
            break;
    }

    #undef ATTR_CASE
}

void printAttribute(JavaClassFile* jcf, attribute_info* entry, int identationLevel)
{
    #define ATTR_CASE(attr) case ATTR_##attr: printAttribute##attr(jcf, entry, identationLevel); break;

    switch (entry->attributeType)
    {
        ATTR_CASE(Code)
        ATTR_CASE(ConstantValue)
        ATTR_CASE(InnerClasses)
        ATTR_CASE(SourceFile)
        ATTR_CASE(LineNumberTable)
        ATTR_CASE(Deprecated)
        ATTR_CASE(Exceptions)
        default:
            ident(identationLevel);
            printf("Attribute not implemented and ignored.");
            break;
    }

    printf("\n");

    #undef ATTR_CASE
}

void printAllAttributes(JavaClassFile* jcf)
{
    if (jcf->attributeCount == 0)
        return;

    uint16_t u16;
    char buffer[48];
    cp_info* cp;
    attribute_info* atti;

    printf("\n---- Class Attributes ----");

    for (u16 = 0; u16 < jcf->attributeCount; u16++)
    {
        atti = jcf->attributes + u16;
        cp = jcf->constantPool + atti->name_index - 1;
        UTF8_to_Ascii((uint8_t*)buffer, sizeof(buffer), cp->Utf8.bytes, cp->Utf8.length);

        printf("\n\n\tAttribute #%u - %s:\n\n", u16 + 1, buffer);
        printAttribute(jcf, atti, 2);
    }
}
