#include <iostream>
#include <Windows.h>
#include <funchook.h>

#define ENTIRE_TITLEBAR 10000

funchook_t* funchook = NULL;
HMODULE hModule = NULL;

static int(*DrawTextWFunc)(
    HDC     hdc,
    LPCWSTR lpchText,
    int     cchText,
    LPRECT  lprc,
    UINT    format
    );

int DrawTextWHook(
    HDC     hdc,
    LPCWSTR lpchText,
    int     cchText,
    LPRECT  lprc,
    UINT    format
)
{
    int ret;

    ret = DrawTextWFunc(
        hdc,
        lpchText,
        cchText,
        lprc,
        (format & (~DT_LEFT)) | DT_CENTER
    );
    if (format & DT_CALCRECT) lprc->right = ENTIRE_TITLEBAR;
    return ret;
}

__declspec(dllexport) DWORD WINAPI main(
    _In_ LPVOID lpParameter
)
{
    FILE* conout;
    int rv;
   
    if (!funchook)
    {
        funchook = funchook_create();

        DrawTextWFunc = DrawTextW;
        rv = funchook_prepare(funchook, (void**)&DrawTextWFunc, DrawTextWHook);
        if (rv != 0) 
        {
            return rv;
        }

        rv = funchook_install(funchook, 0);
        if (rv != 0) 
        {
            return rv;
        }
    }
    else
    {
        rv = funchook_uninstall(funchook, 0);
        if (rv != 0)
        {
            return rv;
        }

        rv = funchook_destroy(funchook);
        if (rv != 0)
        {
            return rv;
        }

        FreeLibraryAndExitThread(hModule, 0);
    }

    return 0;
}

BOOL WINAPI DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD     fdwReason,
    _In_ LPVOID    lpvReserved
)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        hModule = hinstDLL;
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}