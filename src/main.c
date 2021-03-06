#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "javaclass.h"
#include "jvm.h"
#include "memoryinspect.h"

int main(int argc, char* args[])
{
    if (argc <= 1)
    {
        printf("First parameter should be the path to a .class file.\n");
        printf("Following parameters could be:\n");
        printf(" -c \t Shows the content of the .class file\n");
        printf(" -e \t Execute the method 'main' from the class\n");
        printf(" -b \t Adds UTF-8 BOM to the output\n");
        return 0;
    }

    if (sizeof(int32_t*) > sizeof(int32_t))
    {
        printf("Warning: pointers cannot be stored in an int32_t.\n");
        printf("This is very likely to malfunction when handling references.\n");
        printf("Continue anyway? (Y/N)\n");

        int input;

        do {

            input = getchar();

            if (input == 'Y' || input == 'y')
                break;

            if (input == 'N' || input == 'n')
                exit(0);

            while (getchar() != '\n');

        } while (1);

    }

    uint8_t printClassContent = 0;
    uint8_t executeClassMain = 0;
    uint8_t includeBOM = 0;

    int argIndex;

    for (argIndex = 2; argIndex < argc; argIndex++)
    {
        if (!strcmp(args[argIndex], "-c"))
            printClassContent = 1;
        else if (!strcmp(args[argIndex], "-e"))
            executeClassMain = 1;
        else if (!strcmp(args[argIndex], "-b"))
            includeBOM = 1;
        else
            printf("Unknown argument #%d ('%s')\n", argIndex, args[argIndex]);
    }

    // If requested, outputs the three byte sequence that tells that
    // the data is encoded as UTF-8.
    // This is required by some programs (like notepad) to correctly
    // render UTF-8 characters. This is useful when the output of this
    // program is redirected to a file. With this parameter given, the file
    // will also contain the UTF-8 BOM.
    if (includeBOM)
        printf("%c%c%c", 0xEF, 0xBB, 0xBF);

    if (printClassContent)
    {
        // Open .class file given as parameter
        JavaClass jc;
        openClassFile(&jc, args[1]);

        // If something went wrong, show debug information
        if (jc.status != CLASS_STATUS_OK)
            printClassFileDebugInfo(&jc);
        // Otherwise, is requested by the user, print the class content
        else if (printClassContent)
            printClassFileInfo(&jc);

        closeClassFile(&jc);
    }

    if (executeClassMain)
    {
        JavaVirtualMachine jvm;
        initJVM(&jvm);

        size_t inputLength = strlen(args[1]);

        // This is to remove the ".class" from the file name. Example:
        //   "C:/mypath/file.class" becomes "C:/mypath/file"
        // That is done because the resolveClass function adds the .class
        // when looking for files to open. This is the best behavior because
        // other classes paths from within the class file don't have the
        // .class file extension.
        if (inputLength > 6 && strcmp(args[1] + inputLength - 6, ".class") == 0)
        {
            inputLength -= 6;
            args[1][inputLength] = '\0';
        }

        LoadedClasses* mainLoadedClass;

        setClassPath(&jvm, args[1]);

        if (resolveClass(&jvm, (const uint8_t*)args[1], inputLength, &mainLoadedClass))
            executeJVM(&jvm, mainLoadedClass);

#ifdef DEBUG
        printf("Execution finished. Status: %d\n", jvm.status);
#endif // DEBUG

        deinitJVM(&jvm);
    }

    return 0;
}

/// @mainpage Java Virtual Machine
///
/// <pre>
/// Group members:
///     Bruno Osorio
///     Joao Araujo
///     Leticia Ribeiro
///     Tiago Kfouri
///     Victoria Goularte
/// </pre>
/// @section intro Introduction
/// Simple implementation of java virtual machine that is able to read class files and
/// display their content and execute the class' main method.
/// <br>
/// Not all instructions have been implemented and there are other limitations.
/// Refer to section @ref limitations Limitations to see more.
/// <br>
///
///
/// @section instructions Instructions Implementation
/// Each instruction is implemented entirely within its single C function. They are all
/// declared in file instructions.c.
/// <br>
/// All instructions functions share the prefix "instfunc_" and have the same
/// function prototype, which is:
/// @code
/// uint8_t instfunc_<instruction name>(JavaVirtualMachine* jvm, Frame* frame)
/// @endcode
/// A Frame structure contains all necessary information to run a method, i.e. the
/// method's bytecode along with its length, the method's program counter, an array
/// storing all local variables and a stack of operands (OperandStack).
/// The frame is used to manipulate the operands and the local variables, and read
/// instructions parameters from the method's bytecode.
/// <br>
/// Instructions use the JavaVirtualMachine pointer to set errors (like insufficient
/// memory), resolve and look for classes, and create new objects
/// (arrays, class instances, strings).
/// <br>
///
///
/// @section limitations Limitations
/// There are a few instructions that haven't been implemented. They are listed below:
///     - invokeinterface - will produce error if executed
///     - invokedynamic - will produce error if executed
///     - checkcast - will produce error if executed
///     - instanceof - will produce error if executed
///     - monitorenter - ignored
///     - monitorexit - ignored
///
/// There is also no support for exceptions nor try/catch/finally blocks.
/// Class "java/lang/System" and "java/lang/String" can be used, but they are
/// also limited.
/// <br>
/// It is possible to print data to stdout using java/lang/System.out.println(),
/// but all other System's methods are unavailable.
/// <br>
/// Strings have no methods implemented, therefore they can only be created
/// and printed. Other common instructions that deal with with objects will
/// also work for strings, like comparing them with 'null'.
