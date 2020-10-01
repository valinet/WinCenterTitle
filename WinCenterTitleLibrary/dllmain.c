#include <valinet/ini/ini.h>
#include <valinet/pdb/pdb.h>
#include <stdio.h>
#include <Windows.h>
#include <funchook.h>
#pragma comment(lib, "Psapi.lib") //required by funchook

#define MARGIN_OVERHANG_WIDTH_FIX           2
#define AERO_COLOR_OFFSET                   0x19
#define NUMBER_OF_REQUESTED_SYMBOLS         2
#define DOWNLOAD_SYMBOLS_ERROR              0x100
#define GET_SYMBOLS_ERROR                   0x101
#define GET_OS_VERSION_ERROR                0x102
#define SETTINGS_RELATIVE_PATH              "\\symbols\\settings.ini"
#define SETTINGS_ADDRESSES_SECTION_NAME     "Addresses"
#define MODULE_NAME_UDWM                    "uDWM"
#define DLL_NAME                            "uDWM.dll"
#define SYMBOL_NAME_MAX_LEN                 200
#define ADDR_G_PDMINSTANCE                  "g_pdmInstance"
#define ADDR_CDESKTOPMANAGER_LOADTHEME      "CDesktopManager::LoadTheme"
#define SETTINGS_OS_SECTION_NAME            "OS"
#define OS_VERSION_STRING                   "Build"

#define SET_COLOR
#undef SET_COLOR
#define ENTIRE_TITLEBAR                     10000
#define TEXT_COLOR                          RGB(0, 0, 0)
#define TITLEBARS_WHITE_DEFAULT             1
#define TITLEBARS_COLORED                   0
#define TITLEBARS_DESIRED_COLOR             TITLEBARS_WHITE_DEFAULT

funchook_t* funchook = NULL;
HMODULE hModule = NULL;
BYTE* titlebar_color = NULL;
BYTE old_titlebar_color = 0;

typedef LONG NTSTATUS, * PNTSTATUS;
#define STATUS_SUCCESS (0x00000000)

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

BOOL GetOSVersion(PRTL_OSVERSIONINFOW lpRovi)
{
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod != NULL)
    {
        RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)GetProcAddress(
            hMod, 
            "RtlGetVersion"
        );
        if (fxPtr != NULL)
        {
            lpRovi->dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
            if (STATUS_SUCCESS == fxPtr(lpRovi)) 
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

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
    int rv = 0;

    if (!(format & DT_CALCRECT))
    {
        // fixes title label sometimes overlapping right margin of window
        lprc->right -= MARGIN_OVERHANG_WIDTH_FIX;
#ifdef SET_COLOR
        SetTextColor(hdc, TEXT_COLOR);
#endif
    }
    rv = DrawTextWFunc(
        hdc,
        lpchText,
        cchText,
        lprc,
        (format & (~DT_LEFT)) | DT_CENTER
    );
    if (format & DT_CALCRECT)
    {
        lprc->right = ENTIRE_TITLEBAR;
    }
    return rv;
}

static int64_t(*CDesktopManagerLoadThemeFunc)(
    void* _this
    );

int64_t CDesktopManagerLoadThemeHook(
    void* _this
)
{
    int64_t ret = CDesktopManagerLoadThemeFunc(_this);
    *titlebar_color = TITLEBARS_DESIRED_COLOR;
    return ret;
}

__declspec(dllexport) DWORD WINAPI main(
    _In_ LPVOID lpParameter
)
{
#ifdef _DEBUG
    FILE* conout;
    AllocConsole();
    freopen_s(
        &conout, 
        "CONOUT$", 
        "w", 
        stdout
    );
#endif

    int rv = 0;
   
    if (!funchook)
    {
        funchook = funchook_create();

        DrawTextWFunc = DrawTextW;
        rv = funchook_prepare(
            funchook, 
            (void**)&DrawTextWFunc, 
            DrawTextWHook
        );
        if (rv != 0) 
        {
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }

        // determine aero.msstyles code path flag location
        if (titlebar_color == NULL)
        {
            SIZE_T dwRet;
            char* symbolNames[NUMBER_OF_REQUESTED_SYMBOLS] = {
                ADDR_G_PDMINSTANCE, 
                ADDR_CDESKTOPMANAGER_LOADTHEME 
            };
            DWORD addresses[NUMBER_OF_REQUESTED_SYMBOLS];
            ZeroMemory(
                addresses, 
                NUMBER_OF_REQUESTED_SYMBOLS * sizeof(DWORD)
            );
            char szSettingsPath[_MAX_PATH];
            ZeroMemory(
                szSettingsPath, 
                (_MAX_PATH) * sizeof(char)
            );
            TCHAR wszSettingsPath[_MAX_PATH];
            ZeroMemory(
                wszSettingsPath, 
                (_MAX_PATH) * sizeof(TCHAR)
            );
            GetModuleFileNameA(
                hModule,
                szSettingsPath,
                _MAX_PATH
            );
            PathRemoveFileSpecA(szSettingsPath);
            strcat_s(
                szSettingsPath,
                _MAX_PATH,
                SETTINGS_RELATIVE_PATH
            );
            mbstowcs_s(
                &dwRet,
                wszSettingsPath,
                _MAX_PATH,
                szSettingsPath,
                _MAX_PATH
            );
            addresses[0] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_G_PDMINSTANCE),
                0,
                wszSettingsPath
            );
            addresses[1] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_CDESKTOPMANAGER_LOADTHEME),
                0,
                wszSettingsPath
            );

            // https://stackoverflow.com/questions/36543301/detecting-windows-10-version/36543774#36543774
            RTL_OSVERSIONINFOW rovi;
            if (!GetOSVersion(&rovi))
            {
                FreeLibraryAndExitThread(
                    hModule,
                    GET_OS_VERSION_ERROR
                );
                return GET_OS_VERSION_ERROR;
            }
            // https://stackoverflow.com/questions/47926094/detecting-windows-10-os-build-minor-version
            DWORD32 ubr = 0, ubr_size = sizeof(DWORD32);
            HKEY hKey;
            LONG lRes = RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                wcschr(
                    wcschr(
                        wcschr(
                            UNIFIEDBUILDREVISION_KEY, 
                            '\\'
                        ) + 1, 
                        '\\'
                    ) + 1, 
                    '\\'
                ) + 1,
                0,
                KEY_READ,
                &hKey
            );
            if (lRes == ERROR_SUCCESS)
            {
                RegQueryValueExW(
                    hKey,
                    UNIFIEDBUILDREVISION_VALUE,
                    0,
                    NULL,
                    &ubr,
                    &ubr_size
                );
            }            
            TCHAR szReportedVersion[_MAX_PATH];
            ZeroMemory(
                szReportedVersion,
                (_MAX_PATH) * sizeof(TCHAR)
            );
            TCHAR szStoredVersion[_MAX_PATH];
            ZeroMemory(
                szStoredVersion,
                (_MAX_PATH) * sizeof(TCHAR)
            );
            wsprintf(
                szReportedVersion,
                L"%d.%d.%d.%d",
                rovi.dwMajorVersion,
                rovi.dwMinorVersion,
                rovi.dwBuildNumber,
                ubr
            );
            VnGetString(
                TEXT(SETTINGS_OS_SECTION_NAME),
                TEXT(OS_VERSION_STRING),
                szStoredVersion,
                _MAX_PATH,
                _MAX_PATH,
                NULL,
                wszSettingsPath
            );

            if (addresses[0] == 0 || addresses[1] == 0 || wcscmp(szReportedVersion, szStoredVersion))
            {
                if (VnDownloadSymbols(
                    hModule,
                    DLL_NAME,
                    szSettingsPath,
                    _MAX_PATH
                ))
                {
                    FreeLibraryAndExitThread(
                        hModule, 
                        DOWNLOAD_SYMBOLS_ERROR
                    );
                    return DOWNLOAD_SYMBOLS_ERROR;
                }
                if (VnGetSymbols(
                    szSettingsPath,
                    addresses,
                    symbolNames,
                    NUMBER_OF_REQUESTED_SYMBOLS
                ))
                {
                    FreeLibraryAndExitThread(
                        hModule, 
                        GET_SYMBOLS_ERROR
                    );
                    return GET_SYMBOLS_ERROR;
                }
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_G_PDMINSTANCE),
                    addresses[0],
                    wszSettingsPath
                );
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CDESKTOPMANAGER_LOADTHEME),
                    addresses[1],
                    wszSettingsPath
                );
                VnWriteString(
                    TEXT(SETTINGS_OS_SECTION_NAME),
                    TEXT(OS_VERSION_STRING),
                    szReportedVersion,
                    wszSettingsPath
                );
            }

            HANDLE hudwm = GetModuleHandle(TEXT(MODULE_NAME_UDWM));
            uintptr_t* g_pdmInstance = (uintptr_t*)(
                (uintptr_t)hudwm + 
                (uintptr_t)addresses[0]
                );
            titlebar_color = (BYTE*)(
                (uintptr_t)(*g_pdmInstance) + 
                (uintptr_t)AERO_COLOR_OFFSET
                );
            old_titlebar_color = *titlebar_color;
            *titlebar_color = TITLEBARS_DESIRED_COLOR;

            CDesktopManagerLoadThemeFunc = (int64_t(*)(void*))(
                (uintptr_t)hudwm + 
                (uintptr_t)addresses[1]
                );
            rv = funchook_prepare(
                funchook, 
                (void**)&CDesktopManagerLoadThemeFunc, 
                CDesktopManagerLoadThemeHook
            );
            if (rv != 0)
            {
                FreeLibraryAndExitThread(
                    hModule, 
                    rv
                );
                return rv;
            }
        }

        rv = funchook_install(
            funchook, 
            0
        );
        if (rv != 0)
        {
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }
    }
    else
    {
        rv = funchook_uninstall(
            funchook, 
            0
        );
        if (rv != 0)
        {
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }

        rv = funchook_destroy(funchook);
        if (rv != 0)
        {
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }

        if (titlebar_color != NULL)
        {
            *titlebar_color = old_titlebar_color;
        }

        FreeLibraryAndExitThread(
            hModule, 
            0
        );
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