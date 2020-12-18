#include <valinet/hooking/exeinject.h>
#include <stdio.h>
#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#define HEADER_NAME                         "WinCenterTitle\n==============\n"
#define LIBRARY_NAME                        "\\WinCenterTitleLibrary.dll"
#define DWM_PROCESS_NAME                    "dwm.exe"
#define DWM_RESTART_WAIT_DELAY              1000
#define CLASS_NAME                          "WinCenterTitle"
#define ERROR_DELETE_SETTINGS               0x1000
#define ERROR_RUNNING_HOOK_ENTRY_POINT      0x1001

FILE* stream = NULL;
BOOL firstCrash = TRUE;

DWORD CrashHandler(LPVOID dwThreadExitCode)
{
    BOOL bResult = FALSE;
    TCHAR wszLibPath[_MAX_PATH];
    ZeroMemory(
        wszLibPath,
        _MAX_PATH * sizeof(TCHAR)
    );
    GetModuleFileName(
        GetModuleHandle(NULL),
        wszLibPath,
        _MAX_PATH
    );
    PathRemoveFileSpec(wszLibPath);
    wcscat_s(
        wszLibPath,
        _MAX_PATH,
        TEXT("\\symbols\\settings.ini")
    );
    bResult = DeleteFile(wszLibPath);
    if (firstCrash && bResult)
    {
        firstCrash = FALSE;
        fprintf(
            stream,
            "First time error while running entry point in DWM "
            "(%d). Maybe symbols have changed, will try to download "
            "latest symbols, and rehook.\n",
            dwThreadExitCode
        );
    }
    else
    {
        if (bResult)
        {
            fprintf(
                stream,
                "Cannot delete settings file.\n",
                dwThreadExitCode
            );
            return ERROR_DELETE_SETTINGS;
        }
        else
        {
            fprintf(
                stream,
                L"Injection entry point failed in DWM (%d).\n",
                dwThreadExitCode
            );
            return dwThreadExitCode; // ERROR_RUNNING_HOOK_ENTRY_POINT
        }
    }
    return ERROR_SUCCESS;
}

int main(int argc, char** argv)
{
    FILE* conout;
    TCHAR szLibPath[_MAX_PATH];

    stream = stdout;
    if (!AllocConsole());
    if (freopen_s(
        &conout, 
        "CONOUT$", 
        "w",
        stdout)
    );
    fprintf(
        stream,
        HEADER_NAME
    );
    GetModuleFileName(
        GetModuleHandle(NULL),
        szLibPath,
        _MAX_PATH
    );
    PathRemoveFileSpec(szLibPath);
    wcscat_s(
        szLibPath,
        _MAX_PATH,
        TEXT(LIBRARY_NAME)
    );
    return VnInjectAndMonitorProcess(
        szLibPath,
        sizeof(szLibPath),
        "main",
        TEXT(DWM_PROCESS_NAME),
        TEXT(CLASS_NAME),
        CrashHandler,
        GetModuleHandle(NULL),
        stream,
        DWM_RESTART_WAIT_DELAY,
        NULL,
        FALSE,
        1000,
        0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        NULL,
        NULL
    );
}