
#include <stdlib.h>
#ifdef __DREAMCAST__
#include <kos.h>
#include "brender.h"
void * BR_RESIDENT_ENTRY HostImageLoad(char *name)
{
	return NULL;
}

void BR_RESIDENT_ENTRY HostImageUnload(void *image)
{
}

void * BR_RESIDENT_ENTRY HostImageLookupName(void *img, char *name, br_uint_32 hint)
{
	return NULL;
}

void * BR_RESIDENT_ENTRY HostImageLookupOrdinal(void *img, br_uint_32 ordinal)
{
	return NULL;
}
#endif

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#include <windows.h>
#endif

#include "brender.h"

extern void Harness_Init(int* argc, char* argv[]);
extern int original_main(int pArgc, char* pArgv[]);

void BR_CALLBACK _BrBeginHook(void) {
    struct br_device* BR_EXPORT BrDrv1SoftPrimBegin(char* arguments);
    struct br_device* BR_EXPORT BrDrv1SoftRendBegin(char* arguments);

    BrDevAddStatic(NULL, (br_device_begin_fn *)BrDrv1SoftPrimBegin, NULL);
    BrDevAddStatic(NULL, (br_device_begin_fn *)BrDrv1SoftRendBegin, NULL);
    // BrDevAddStatic(NULL, BrDrv1SDL2Begin, NULL);
}

void BR_CALLBACK _BrEndHook(void) {
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    /* Attach to the console that started us if any */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        /* We attached successfully, lets redirect IO to the consoles handles if not already redirected */
        if (_fileno(stdout) == -2 || _get_osfhandle(_fileno(stdout)) == -2) {
            freopen("CONOUT$", "w", stdout);
        }

        if (_fileno(stderr) == -2 || _get_osfhandle(_fileno(stderr)) == -2) {
            freopen("CONOUT$", "w", stderr);
        }

        if (_fileno(stdin) == -2 || _get_osfhandle(_fileno(stdin)) == -2) {
            freopen("CONIN$", "r", stdin);
        }
    }
#endif
// #ifdef __DREAMCAST__
//     fs_chdir("/cd/dethrace");
// #endif    
    Harness_Init(&argc, argv);

    return original_main(argc, argv);
}
