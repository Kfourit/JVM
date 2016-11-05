#include "readfunctions.h"
#include "utf8.h"
#include "validity.h"
#include <math.h>

uint8_t readu4(JavaClassFile* jcf, uint32_t* out)
{
    int byte;
    uint32_t u4 = 0;
    uint8_t i;

    for (i = 0; i < 4; i++)
    {
        byte = fgetc(jcf->file);

        if (byte == EOF)
            break;

        u4 <<= 8;
        u4 |= byte;
        jcf->totalBytesRead++;
    }

    if (out)
        *out = u4;

    return i == 4;
}

uint8_t readu2(JavaClassFile* jcf, uint16_t* out)
{
    int byte;
    uint16_t u2 = 0;
    uint8_t i;

    for (i = 0; i < 2; i++)
    {
        byte = fgetc(jcf->file);

        if (byte == EOF)
            break;

        u2 <<= 8;
        u2 |= byte;
        jcf->totalBytesRead++;
    }

    if (out)
        *out = u2;

    return i == 2;
}

int32_t readFieldDescriptor(uint8_t* utf8_bytes, int32_t utf8_len, char checkValidClassIdentifier)
{
    int32_t totalBytesRead = 0;
    uint32_t utf8_char;
    uint8_t used_bytes;

    do
    {
        used_bytes = nextUTF8Character(utf8_bytes, utf8_len, &utf8_char);

        if (used_bytes == 0)
            return 0;

        utf8_bytes += used_bytes;
        utf8_len -= used_bytes;
        totalBytesRead += used_bytes;

    } while (utf8_char == '[');

    switch (utf8_char)
    {
        case 'B': case 'C': case 'D': case 'F': case 'I': case 'J': case 'S': case 'Z': break;
        case 'L':
        {
            uint8_t* identifierBegin = utf8_bytes;
            int32_t identifierLength = 0;

            do
            {
                used_bytes = nextUTF8Character(utf8_bytes, utf8_len, &utf8_char);

                if (used_bytes == 0)
                    return 0;

                utf8_bytes += used_bytes;
                utf8_len -= used_bytes;
                totalBytesRead += used_bytes;
                identifierLength += used_bytes;

            } while (utf8_char != ';');

            if (checkValidClassIdentifier && !isValidJavaIdentifier(identifierBegin, identifierLength - 1, 1))
                return 0;

            break;
        }

        default:
            return 0;
    }

    return totalBytesRead;
}

int32_t readMethodDescriptor(uint8_t* utf8_bytes, int32_t utf8_len, char checkValidClassIdentifier)
{
    int32_t bytesProcessed = 0;
    uint32_t utf8_char;
    uint8_t used_bytes;

    used_bytes = nextUTF8Character(utf8_bytes, utf8_len, &utf8_char);

    if (used_bytes == 0 || utf8_char != '(')
        return 0;

    utf8_bytes += used_bytes;
    utf8_len -= used_bytes;
    bytesProcessed += used_bytes;

    int32_t field_descriptor_length;

    do
    {
        field_descriptor_length = readFieldDescriptor(utf8_bytes, utf8_len, checkValidClassIdentifier);

        if (field_descriptor_length == 0)
        {
            used_bytes = nextUTF8Character(utf8_bytes, utf8_len, &utf8_char);

            if (used_bytes == 0 || utf8_char != ')')
                return 0;

            utf8_bytes += used_bytes;
            utf8_len -= used_bytes;
            bytesProcessed += used_bytes;

            break;
        }

        utf8_bytes += field_descriptor_length;
        utf8_len -= field_descriptor_length;
        bytesProcessed += field_descriptor_length;

    } while (1);

    field_descriptor_length = readFieldDescriptor(utf8_bytes, utf8_len, 1);

    if (field_descriptor_length == 0)
    {
        used_bytes = nextUTF8Character(utf8_bytes, utf8_len, &utf8_char);

        if (used_bytes == 0 || utf8_char != 'V')
            return 0;

        utf8_len -= used_bytes;
        bytesProcessed += used_bytes;
    }
    else
    {
        utf8_len -= field_descriptor_length;
        bytesProcessed += field_descriptor_length;
    }

    return utf8_len == 0 ? bytesProcessed : 0;
}

float readConstantPoolFloat(cp_info* entry)
{
    if (entry->Float.bytes == 0x7F800000UL)
        return INFINITY;

    if (entry->Float.bytes == 0xFF800000UL)
        return -INFINITY;

    if ((entry->Float.bytes >= 0x7F800000UL && entry->Float.bytes <= 0x7FFFFFFFUL) ||
        (entry->Float.bytes >= 0xFF800000UL && entry->Float.bytes <= 0xFFFFFFFFUL))
    {
        return NAN;
    }

    int32_t s = (entry->Float.bytes >> 31) == 0 ? 1 : -1;
    int32_t e = (entry->Float.bytes >> 23) & 0xFF;
    int32_t m = (e == 0) ?
        (entry->Float.bytes & 0x7FFFFFUL) << 1 :
        (entry->Float.bytes & 0x7FFFFFUL) | 0x800000;

    return s * (float)ldexp((double)m, e - 150);
}

double readConstantPoolDouble(cp_info* entry)
{
    uint64_t bytes = entry->Double.high;
    bytes <<= 32;
    bytes |= entry->Double.low;

    if (bytes == 0x7FF0000000000000ULL)
        return INFINITY;

    if (bytes == 0xFFF0000000000000ULL)
        return -INFINITY;

    if ((bytes >= 0x7FF0000000000001ULL && bytes <= 0x7FFFFFFFFFFFFFFFULL) ||
        (bytes >= 0xFFF0000000000001ULL && bytes <= 0xFFFFFFFFFFFFFFFFULL))
    {
        return NAN;
    }

    int32_t s = (bytes >> 63) == 0 ? 1 : -1;
    int32_t e = (bytes >> 52) & 0x7FF;
    int64_t m = (e == 0) ?
        (bytes & 0xFFFFFFFFFFFFFULL) << 1 :
        (bytes & 0xFFFFFFFFFFFFFULL) | 0x10000000000000ULL;

    return s * ldexp((double)m, e - 1075);
}
