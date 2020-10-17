#include <valinet/ini/ini.h>
#include <valinet/pdb/pdb.h>
#include <stdio.h>
#include <stdbool.h>
#include <Windows.h>
#include <funchook.h>
#pragma comment(lib, "Psapi.lib") //required by funchook

#define MARGIN_OVERHANG_WIDTH_FIX           2
#define AERO_COLOR_OFFSET                   0x19
#define NUMBER_OF_REQUESTED_SYMBOLS         5
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
#define ADDR_FUNC2                          "CMatrixTransformProxy::Update"
#define ADDR_FUNC3                          "CText::ValidateResources"
#define ADDR_FUNC4                          "CText::UpdateAlignmentTransform"
#define SETTINGS_OS_SECTION_NAME            "OS"
#define OS_VERSION_STRING                   "Build"

#define SET_COLOR
#undef SET_COLOR
#define ENTIRE_TITLEBAR                     10000
#define TEXT_COLOR                          RGB(0, 0, 0)
#define TITLEBARS_WHITE_DEFAULT             1
#define TITLEBARS_COLORED                   0
#define TITLEBARS_DESIRED_COLOR             TITLEBARS_WHITE_DEFAULT

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

funchook_t* funchook = NULL;
HMODULE hModule = NULL;
BYTE* titlebar_color = NULL;
BYTE old_titlebar_color = 0;

typedef struct _milmatrix
{
    double s11;
    double s12;
    double s21;
    double s22;
    double dx;
    double dy;
} milmatrix;

void* g_CTextInstance;

static int64_t(*CTextUpdateAlignmentTransformFunc)(
    void* _this
    );

static int64_t(*CTextValidateResourcesFunc)(
    void* _this
    );

int64_t CTextValidateResourcesHook(
    void* _this
)
{
    int64_t rv;
    DWORD* val;

    // memorize address of this instance of CText, to be used in a subsequent
    // call to CMatrixTransformProxy::Update on chain
    // CText::ValidateResources - CText::UpdateAlignmentTransform
    g_CTextInstance = _this;
    // call original function
    rv = CTextValidateResourcesFunc(_this);
    // we need this hack so that we trick DWM into triggering an update of the
    // title label at each resize, as now the title is centered so its
    // position should change when the size of the window changes;
    // how this works is that it seems DWM decides whether to update the
    // label based on a check of whether the label size exceeded the window
    // size; so, by setting the label size to a very large value, DWM is
    // tricked into thinking the overflow is always happening
    val = ((DWORD*)g_CTextInstance + 100);
    if (*val < ENTIRE_TITLEBAR)
    {
        *val += ENTIRE_TITLEBAR;
    }
    return rv;
}

static int64_t(*CMatrixTransformProxyUpdateFunc)(
    void* _this,
    milmatrix* mat
    );

int64_t CMatrixTransformProxyUpdateHook(
    void* _this,
    milmatrix* mat
)
{
    // this approach is based on how DWM centered the title bar text in
    // Windows 8: instead of drawing the label from side to side with
    // DT_CENTER, as we also did previously, DWM used to move the position
    // where the label is rendered; although this was not necessary, it
    // enabled the possiblity of Aero to work correctly (by not blurring the
    // entire title bar, just the portion containing text), if Aero would
    // have made a comeback

    if (g_CTextInstance)
    {
        // this is the width of the title bar label containing the text
        double v1 = *((DWORD*)g_CTextInstance + 100);
        // this is the size available for the label (the size between the
        // window icon, or the left window margin, and the caption buttons, or the
        // right window margin)
        double v3 = *((DWORD*)g_CTextInstance + 30);
        // this is the height of the titlebar text (UNUSED)
        double v4 = *((DWORD*)g_CTextInstance + 31);
        // this is the width of the window icon
        double v6 = *((DWORD*)g_CTextInstance + 32);
        // this is the width of the window caption buttons (for example,
        // Close, Minimize, Maximize etc)
        double v5 = *((DWORD*)g_CTextInstance + 33);
        // this also contains the width and height:
        // tagSIZE st = *((tagSIZE*)g_CTextInstance + 15);
        // where tagSIZE is a struct of 2 LONGs (width, and height)

        // check if caller is CText::UpdateAlignmentTransfor
        // do not hook CTextUpdateAlignmentTransform for this to work
        // also, could be better improved by parsing the PE header
        // or looking for the ret instruction of this code block;
        // for now, for regular dwm compiles, it seems to work just fine like 
        // this
        if (
            (-1) * (uintptr_t)_ReturnAddress() >
            (-1) * (uintptr_t)CTextUpdateAlignmentTransformFunc - 120
            )
        {
            // take into account previous hack; we need the real dimension
            if (v1 > ENTIRE_TITLEBAR)
            {
                v1 = v1 - ENTIRE_TITLEBAR;
            }
            // we determine the "origin" of the label
            double dx = (v3 + v5 - v6 - v1) / 2;
            // this happens when the centered label overlaps the caption 
            // buttons in that case, we center classically, between icon and
            // caption buttons, same as Windows 8 did in this case
            if (dx + v1 > v3)
            {
                dx = (v3 - v1) / 2;
            }
            // if there is not enough space to display the title, we left
            // align,  and text ellipsis will be shown anyway
            if (dx < 0)
            {
                mat->dx = 0;
            }
            else
            {
                mat->dx = dx;
            }
        }
    }

    // call original function
    return CMatrixTransformProxyUpdateFunc(_this, mat);
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

/*
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
*/

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

        /*
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
        */

        // determine aero.msstyles code path flag location
        if (titlebar_color == NULL)
        {
            SIZE_T dwRet;
            char* symbolNames[NUMBER_OF_REQUESTED_SYMBOLS] = {
                ADDR_G_PDMINSTANCE, 
                ADDR_CDESKTOPMANAGER_LOADTHEME,
                ADDR_FUNC2,
                ADDR_FUNC3,
                ADDR_FUNC4
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
            addresses[2] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_FUNC2),
                0,
                wszSettingsPath
            );
            addresses[3] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_FUNC3),
                0,
                wszSettingsPath
            );
            addresses[4] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_FUNC4),
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

            if (addresses[0] == 0 || addresses[1] == 0 || addresses[2] == 0 || wcscmp(szReportedVersion, szStoredVersion))
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
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_FUNC2),
                    addresses[2],
                    wszSettingsPath
                );
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_FUNC3),
                    addresses[3],
                    wszSettingsPath
                );
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_FUNC4),
                    addresses[4],
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

            CMatrixTransformProxyUpdateFunc = (int64_t(*)(void*))(
                (uintptr_t)hudwm +
                (uintptr_t)addresses[2]
                );
            rv = funchook_prepare(
                funchook,
                (void**)&CMatrixTransformProxyUpdateFunc,
                CMatrixTransformProxyUpdateHook
            );
            if (rv != 0)
            {
                FreeLibraryAndExitThread(
                    hModule,
                    rv
                );
                return rv;
            }

            CTextValidateResourcesFunc = (int64_t(*)(void*))(
                (uintptr_t)hudwm +
                (uintptr_t)addresses[3]
                );
            rv = funchook_prepare(
                funchook,
                (void**)&CTextValidateResourcesFunc,
                CTextValidateResourcesHook
            );
            if (rv != 0)
            {
                FreeLibraryAndExitThread(
                    hModule,
                    rv
                );
                return rv;
            }

            CTextUpdateAlignmentTransformFunc = (int64_t(*)(void*))(
                (uintptr_t)hudwm +
                (uintptr_t)addresses[4]
                );
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