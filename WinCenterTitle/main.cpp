#include <iostream>
#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <tlhelp32.h>
#include <Psapi.h>
#include <assert.h>
#include <conio.h>

#define LIBRARY_NAME                        "\\WinCenterTitleLibrary.dll"
#define DWM_PROCESS_NAME                    "dwm.exe"
#define DWM_RESTART_WAIT_DELAY              1000
#define MODULE_ARRAY_INITIAL_SIZE           100
#define CLASS_NAME                          L"WinCenterTitle"
#define WM_DWM_CRASHED                      WM_USER + 1
#define ERROR_LOAD_LIBRARY                  0x2
#define ERROR_NO_MAIN_IN_INJECTION_LIB      0x21
#define ERROR_GETMODULEHANDLE_KERNEL32      0x3
#define ERROR_GETPROCADDRESS_LOADLIBRARYW   0x31
#define ERROR_GETPROCADDRESS_FREELIBRARY    0x32
#define ERROR_DWM_NOT_RUNNING               0x4
#define ERROR_DWM_OPENPROCESS               0x41
#define ERROR_DWM_VIRTUALALLOC              0x5
#define ERROR_DWM_WRITEPROCESSMEMORY        0x51
#define ERROR_FAILED_TO_INJECT              0x6
#define ERROR_FAILED_TO_RUN_ENTRY_POINT     0x61
#define ERROR_CANNOT_FIND_LIBRARY_IN_DWM    0x8
#define ERROR_MODULE_ARRAY_ALLOC            0x81
#define ERROR_DWM_MODULE_ENUM               0x82
#define ERROR_MODULE_ARRAY_REALLOC          0x83
#define ERROR_CANNOT_GET_ADDRESS_MODULE     0x84
#define ERROR_RUNNING_HOOK_ENTRY_POINT      0x9
#define ERROR_CANNOT_RUN_INJECTION_MAIN     0x91
#define ERROR_DELETE_SETTINGS               0x92
#define ERROR_CREATE_MESSAGE_WINDOW         0x10
#define ERROR_REGISTER_DWM_WATCH            0x11
#define ERROR_REGISTER_EXIT_HANDLER         0x12
#define ERROR_MESSAGE_QUEUE                 0x13
#define ERROR_DWM_CRASHED                   0x222
#define ERROR_FAILED_TO_CALL_FREELIBRARY    0x400
#define ERROR_FREELIBRARY_FAILED            0x401

HANDLE hProcess = NULL;
HMODULE hMod = NULL;
uintptr_t hInjection = 0;
BOOL firstCrash = TRUE;

LONG ExitHandler(LPEXCEPTION_POINTERS p)
{
    HANDLE hThread = NULL;
    DWORD dwThreadExitCode = 0;

    hThread = CreateRemoteThread(
        hProcess,
        NULL,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>
        ((uintptr_t)hMod + (uintptr_t)hInjection),
        NULL,
        0,
        NULL
    );
    WaitForSingleObject(
        hThread,
        INFINITE
    );
    GetExitCodeThread(
        hThread,
        &dwThreadExitCode
    );
    if (dwThreadExitCode)
    {

        wprintf(L"E. Error while unhooking DWM (%d).\n", dwThreadExitCode);

    }

    wprintf(L"E. Successfully unhooked DWM.\n");

    return EXCEPTION_EXECUTE_HANDLER;
}

LRESULT CALLBACK WindowProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch (uMsg)
    {
    case WM_CLOSE:
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_DWM_CRASHED:
        PostMessage(hWnd, WM_QUIT, ERROR_DWM_CRASHED, 0);
        break;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

VOID CALLBACK WaitForDWMToCrash(
    _In_ PVOID   lpParameter,
    _In_ BOOLEAN TimerOrWaitFired
)
{
    SendMessage(reinterpret_cast<HWND>(lpParameter), WM_DWM_CRASHED, 0, 0);
}

DWORD FreeRemoteLibrary(FARPROC hAddrFreeLibrary)
{
    DWORD dwThreadExitCode = 0;
    HANDLE hThread = CreateRemoteThread(
        hProcess,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)hAddrFreeLibrary,
        hMod,
        0,
        NULL
    );
    if (hThread == NULL)
    {

        wprintf(L"ERROR: Unable to call FreeLibrary.\n");

        TerminateProcess(hProcess, 0);
        return ERROR_FAILED_TO_CALL_FREELIBRARY;
    }
    WaitForSingleObject(
        hThread,
        INFINITE
    );
    BOOL bResult = GetExitCodeThread(
        hThread,
        &dwThreadExitCode
    );
    if (!dwThreadExitCode || !bResult)
    {

        wprintf(L"ERROR: FreeLibrary failed.\n");

        return ERROR_FREELIBRARY_FAILED;
    }

    wprintf(L"FreeLibrary OK.\n");

    return ERROR_SUCCESS;
}

int WINAPI wWinMain(
    HINSTANCE hInstance, 
    HINSTANCE hPrevInstance, 
    PWSTR pCmdLine, 
    int nCmdShow
)
{
    FILE* conout = NULL;
    SIZE_T i = 0;
    BOOL bRet = FALSE;
    DWORD dwRet = 0;
    BOOL bErr = FALSE;
    LPTOP_LEVEL_EXCEPTION_FILTER pExitHandler = NULL;
    HMODULE hKernel32 = NULL;
    FARPROC hAdrLoadLibraryW = NULL;
    FARPROC hAdrFreeLibrary = NULL;
    void* pLibRemote = NULL;
    void* pShellCode = NULL;
    wchar_t szLibPath[_MAX_PATH];
    wchar_t szTmpLibPath[_MAX_PATH];
    BOOL bResult = FALSE;
    HANDLE hSnapshot = NULL;
    PROCESSENTRY32 stProcessEntry = { 0 };
    DWORD dwProcessId = 0;
    HMODULE* hMods = NULL;
    DWORD hModuleArrayInitialBytesInitial = 
        MODULE_ARRAY_INITIAL_SIZE * sizeof(HMODULE);
    DWORD hModuleArrayInitialBytes = hModuleArrayInitialBytesInitial;
    DWORD hModuleArrayBytesNeeded = 0;
    HMODULE hInjectionDll = NULL;
    FARPROC hInjectionMainFunc = NULL;
    WNDCLASS wc = { 0 };
    HWND hWnd = NULL;
    MSG msg = { 0 };
    HANDLE hWaitObject = NULL;
    HANDLE hThread = NULL;
    DWORD dwThreadExitCode = 0;
    SIZE_T dwBytesRead = 0;
    BYTE shellCode[] =
    {
        0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xEC, 0x20, 0x66, 0x83,
        0xE4, 0xC0, 0x48, 0xB9, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x48, 0xBA, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0xFF, 0xD2, 0x48, 0xBA, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x48, 0x89, 0x02, 0x48, 0x89, 0xDC,
        0x5B, 0xC3
    };
    // shell code injection technique on x64 courtesy of:
    // https://clymb3r.wordpress.com/2013/05/26/implementing-remote-loadlibrary-and-remote-getprocaddress-using-powershell-and-assembly/
    /* x64 assembly is:
    [SECTION .text]
    global _start
    _start:
        ; Save rsp and setup stack for function call
        push rbx
        mov rbx, rsp
        sub rsp, 0x20
        and sp, 0xffc0
 
        ; Call LoadLibraryA
        mov rcx, 0x4141414141414141    ; Ptr to string of library, set by injector
        mov rdx, 0x4141414141414141    ; Address of LoadLibrary, set by injector
        call rdx
 
        mov rdx, 0x4141414141414141    ; Ptr to save result, set by injector
        mov [rdx], rax
 
        ; Fix stack
        mov rsp, rbx
        pop rbx
        ret
     */

    // Step 0: Print debug info

    if (!AllocConsole());
    if (freopen_s(
        &conout, 
        "CONOUT$", 
        "w",
        stdout)
    );
    wprintf(TEXT("WinCenterTitle\n==============\n"));


    // Step 1: Format hook library path
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

    wprintf(
        L"1. Hook Library Path: %s\n", 
        szLibPath
    );


    // Step 2: Get DLL entry point address
    hInjectionDll = LoadLibrary(szLibPath);
    if (hInjectionDll == NULL)
    {

        wprintf(L"2. ERROR: Cannot load injection library.\n");

        return ERROR_LOAD_LIBRARY;
    }
    hInjectionMainFunc = GetProcAddress(
        hInjectionDll,
        "main"
    );
    if (hInjectionMainFunc == NULL)
    {

        wprintf(L"2. ERROR: Injection library lacks entry point.\n");

        return ERROR_NO_MAIN_IN_INJECTION_LIB;
    }
    hInjection = 
        reinterpret_cast<DWORD>(hInjectionMainFunc) -
        reinterpret_cast<DWORD>(hInjectionDll);
    FreeLibrary(hInjectionDll);

    wprintf(
        L"2. Hook Library Entry Point: 0x%x\n", 
        hInjection
    );


    // Step 3: Get address of LoadLibraryW & FreeLibrary
    hKernel32 = GetModuleHandle(L"Kernel32");
    if (hKernel32 == NULL)
    {

        wprintf(L"3. ERROR: Cannot find address of Kernel32.\n");

        return ERROR_GETMODULEHANDLE_KERNEL32;
    }
    hAdrLoadLibraryW = GetProcAddress(
        hKernel32,
        "LoadLibraryW"
    );
    if (hAdrLoadLibraryW == NULL)
    {

        wprintf(L"3. ERROR: Cannot find address of LoadLibraryW.\n");

        return ERROR_GETPROCADDRESS_LOADLIBRARYW;
    }

    wprintf(
        L"3. LoadLibraryW address: %d\n", 
        hAdrLoadLibraryW
    );

    hAdrFreeLibrary = GetProcAddress(
        hKernel32,
        "FreeLibrary"
    );
    if (hAdrFreeLibrary == NULL)
    {

        wprintf(L"3. ERROR: Cannot find address of FreeLibrary.\n");

        return ERROR_GETPROCADDRESS_FREELIBRARY;
    }

    wprintf(
        L"3. FreeLibrary address: %d\n",
        hAdrFreeLibrary
    );



    // Repeatedly inject DWM
    while (TRUE)
    {
        // Step 4: Find DWM.exe
        stProcessEntry.dwSize = sizeof(PROCESSENTRY32);
        hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (Process32First(hSnapshot, &stProcessEntry) == TRUE)
        {
            while (Process32Next(hSnapshot, &stProcessEntry) == TRUE)
            {
                if (!wcscmp(stProcessEntry.szExeFile, TEXT(DWM_PROCESS_NAME)))
                {
                    dwProcessId = stProcessEntry.th32ProcessID;
                    hProcess = OpenProcess(
                        PROCESS_ALL_ACCESS,
                        FALSE,
                        dwProcessId
                    );
                    if (hProcess == NULL)
                    {

                        wprintf(L"4. ERROR: Cannot get handle to dwm.exe.\n");

                        return ERROR_DWM_OPENPROCESS;
                    }

                    wprintf(
                        L"4. Found Desktop Window manager, PID: %d\n",
                        dwProcessId
                    );

                    break;
                }
            }
        }
        CloseHandle(hSnapshot);
        if (dwProcessId == 0)
        {

            wprintf(L"4. ERROR: Desktop Window Manager is not running.\n");

            return ERROR_DWM_NOT_RUNNING;
        }

        // Step 5: Write path to library in DWM's memory
        pLibRemote = VirtualAllocEx(
            hProcess,
            NULL,
            sizeof(szLibPath),
            MEM_COMMIT,
            PAGE_READWRITE
        );
        if (pLibRemote == NULL)
        {

            wprintf(L"5. ERROR: Cannot alloc memory in DWM.\n");

            return ERROR_DWM_VIRTUALALLOC;
        }
        bResult = WriteProcessMemory(
            hProcess,
            pLibRemote,
            (void*)szLibPath,
            sizeof(szLibPath),
            NULL
        );
        if (!bResult)
        {

            wprintf(L"5. ERROR: Cannot write memory in DWM.\n");

            VirtualFreeEx(
                hProcess,
                (LPVOID)pLibRemote,
                0,
                MEM_RELEASE
            );
            return ERROR_DWM_WRITEPROCESSMEMORY;
        }

        wprintf(L"5. Wrote library path in DWM's memory.\n");

#ifdef _WIN64
        // Step 6: Write shell code to DWM's memory
        pShellCode = VirtualAllocEx(
            hProcess,
            NULL,
            sizeof(szLibPath),
            MEM_COMMIT,
            PAGE_EXECUTE_READWRITE
        );
        if (pShellCode == NULL)
        {

            wprintf(L"6. ERROR: Cannot alloc memory in DWM.\n");

            VirtualFreeEx(
                hProcess,
                (LPVOID)pLibRemote,
                0,
                MEM_RELEASE
            );
            return ERROR_DWM_VIRTUALALLOC;
        }
        // Address of string containing path of module to load
        *((uintptr_t*)(shellCode + 14)) = (uintptr_t)pLibRemote;
        // Address of function to call (LoadLibraryW)
        *((uintptr_t*)(shellCode + 24)) = (uintptr_t)hAdrLoadLibraryW;
        // Address to write return value to
        // Writing on top of the path in order to spare some calls,
        // since the path is not required after calling LoadLibraryW
        *((uintptr_t*)(shellCode + 36)) = (uintptr_t)pLibRemote;
        bResult = WriteProcessMemory(
            hProcess,
            pShellCode,
            (void*)shellCode,
            sizeof(shellCode),
            NULL
        );
        if (!bResult)
        {

            wprintf(L"6. ERROR: Cannot write memory in DWM.\n");

            VirtualFreeEx(
                hProcess,
                (LPVOID)pLibRemote,
                0,
                MEM_RELEASE
            );
            VirtualFreeEx(
                hProcess,
                (LPVOID)pShellCode,
                0,
                MEM_RELEASE
            );
            return ERROR_DWM_WRITEPROCESSMEMORY;
        }

        wprintf(L"6. Wrote shell code in DWM's memory.\n");


        // Step 7: Call shell code
        hThread = CreateRemoteThread(
            hProcess,
            NULL,
            0,
            (LPTHREAD_START_ROUTINE)pShellCode,
            NULL,
            0,
            NULL
        );
        if (hThread == NULL)
        {

            wprintf(L"7. ERROR: Failed to inject library into DWM.\n");

            VirtualFreeEx(
                hProcess,
                (LPVOID)pLibRemote,
                0,
                MEM_RELEASE
            );
            VirtualFreeEx(
                hProcess,
                (LPVOID)pShellCode,
                0,
                MEM_RELEASE
            );
            return ERROR_FAILED_TO_INJECT;
        }
        WaitForSingleObject(
            hThread,
            INFINITE
        );
        bResult = GetExitCodeThread(
            hThread,
            &dwThreadExitCode
        );
        if (!dwThreadExitCode || !bResult)
        {

            wprintf(L"7. ERROR: Failed to run lib entry point in DWM.\n");

            bErr = TRUE;
        }

        wprintf(L"7. Successfully injected library into DWM.\n");


        // Step 8: Check result and cleanup
        bResult = ReadProcessMemory(
            hProcess,
            pLibRemote,
            &hMod,
            sizeof(HMODULE),
            &dwBytesRead
        );
        if (!bResult || dwBytesRead != sizeof(HMODULE))
        {

            wprintf(L"8. ERROR: Cannot get address of loaded module.\n");

            VirtualFreeEx(
                hProcess,
                (LPVOID)pLibRemote,
                0,
                MEM_RELEASE
            );
            VirtualFreeEx(
                hProcess,
                (LPVOID)pShellCode,
                0,
                MEM_RELEASE
            );
            TerminateProcess(hProcess, 0);
            return ERROR_CANNOT_GET_ADDRESS_MODULE;
        }
        VirtualFreeEx(
            hProcess,
            (LPVOID)pLibRemote,
            0,
            MEM_RELEASE
        );
        VirtualFreeEx(
            hProcess,
            (LPVOID)pShellCode,
            0,
            MEM_RELEASE
        );
        if (bErr)
        {
            if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
            {
                TerminateProcess(hProcess, 0);
                return dwRet;
            }
            return ERROR_FAILED_TO_RUN_ENTRY_POINT;
        }
#else
        // Step 6: Load library in DWM
        hThread = CreateRemoteThread(
            hProcess,
            NULL,
            0,
            (LPTHREAD_START_ROUTINE)hAdrLoadLibraryW,
            pLibRemote,
            0,
            NULL
        );
        if (hThread == NULL)
        {

            wprintf(L"6. ERROR: Failed to inject library into DWM.\n");

            return ERROR_FAILED_TO_INJECT;
        }
        WaitForSingleObject(
            hThread,
            INFINITE
        );
        bResult = GetExitCodeThread(
            hThread,
            &dwThreadExitCode
        );
        if (!dwThreadExitCode || !bResult)
        {

            wprintf(L"6. ERROR: Failed to run library entry point in DWM.\n");

            return ERROR_FAILED_TO_RUN_ENTRY_POINT;
        }
        hMod = (HMODULE)dwThreadExitCode;

        wprintf(L"6. Successfully injected library into DWM.\n");

        // Step 7: Free path from DWM's memory
        VirtualFreeEx(
            hProcess,
            (LPVOID)pLibRemote,
            0,
            MEM_RELEASE
        );

        wprintf(L"7. Freed path from DWM's memory.\n");


        // Step 8: Get address of library in DWM's memory
        // This is actually optional, but application is not tested without
        hModuleArrayInitialBytes = hModuleArrayInitialBytesInitial;
        hMods = (HMODULE*)calloc(
            hModuleArrayInitialBytes,
            1
        );
        if (hMods == NULL)
        {

            wprintf(L"8. ERROR: Cannot allocate module array.\n");

            if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
            {
                TerminateProcess(hProcess, 0);
                return dwRet;
            }
            return ERROR_MODULE_ARRAY_ALLOC;
        }
        bResult = EnumProcessModulesEx(
            hProcess,
            hMods,
            hModuleArrayInitialBytes,
            &hModuleArrayBytesNeeded,
            LIST_MODULES_ALL
        );
        if (!bResult)
        {

            wprintf(L"8. ERROR: Unable to enum modules in DWM.\n");

            if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
            {
                TerminateProcess(hProcess, 0);
                return dwRet;
            }
            return ERROR_DWM_MODULE_ENUM;
        }
        if (hModuleArrayInitialBytes < hModuleArrayBytesNeeded)
        {
            hMods = (HMODULE*)realloc(
                hMods,
                hModuleArrayBytesNeeded
            );
            if (hMods == NULL)
            {

                wprintf(L"8. ERROR: Cannot reallocate module array.\n");

                if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
                {
                    TerminateProcess(hProcess, 0);
                    return dwRet;
                }
                return ERROR_MODULE_ARRAY_REALLOC;
            }
            hModuleArrayInitialBytes = hModuleArrayBytesNeeded;
            bResult = EnumProcessModulesEx(
                hProcess,
                hMods,
                hModuleArrayInitialBytes,
                &hModuleArrayBytesNeeded,
                LIST_MODULES_ALL
            );
            if (!bResult)
            {

                wprintf(L"8. ERROR: Unable to enum modules in DWM.\n");

                if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
                {
                    TerminateProcess(hProcess, 0);
                    return dwRet;
                }
                return ERROR_DWM_MODULE_ENUM;
            }
        }
        CharLower(szLibPath);
        if (hModuleArrayBytesNeeded / sizeof(HMODULE) == 0)
        {
            i = -1;
        }
        else
        {
            for (i = 0; i <= hModuleArrayBytesNeeded / sizeof(HMODULE); ++i)
            {
                if (i == hModuleArrayBytesNeeded / sizeof(HMODULE))
                {
                    i = -1;
                    break;
                }
                bResult = GetModuleFileNameEx(
                    hProcess,
                    hMods[i],
                    szTmpLibPath,
                    _MAX_PATH
                );
                if (bResult)
                {
                    CharLower(szTmpLibPath);
                    if (!wcscmp(szTmpLibPath, szLibPath))
                    {
                        break;
                    }
                }
            }
        }
        if (i == -1)
        {

            wprintf(L"8. ERROR: Cannot find library in DWM's memory.\n");

            if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
            {
                TerminateProcess(hProcess, 0);
                return dwRet;
            }
            return ERROR_CANNOT_FIND_LIBRARY_IN_DWM;
        }

        wprintf(
            L"8. Found library in DWM's memory (%d/%d).\n",
            i,
            hModuleArrayBytesNeeded / sizeof(HMODULE)
        );

        hMod = hMods[i];
#endif

        // Step 9: Run DLL's entry point
        hThread = CreateRemoteThread(
            hProcess,
            NULL,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>
                ((uintptr_t)hMod + (uintptr_t)hInjection),
            NULL,
            0,
            NULL
        );
        if (hThread == NULL)
        {

            wprintf(L"9. ERROR: Cannot execute injection entry point.\n");

            if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
            {
                TerminateProcess(hProcess, 0);
                return dwRet;
            }
            return ERROR_CANNOT_RUN_INJECTION_MAIN;
        }
        WaitForSingleObject(
            hThread,
            INFINITE
        );
        bResult = GetExitCodeThread(
            hThread,
            &dwThreadExitCode
        );
        if (dwThreadExitCode || !bResult)
        {
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

                wprintf(
                    L"9. First time error while running entry point in DWM "
                    L"(%d). Maybe symbols have changed, will try to download "
                    L"latest symbols, and rehook.\n", 
                    dwThreadExitCode
                );

            }
            else
            {
                if (bResult)
                {

                    wprintf(
                        L"9. Cannot delete settings file.\n",
                        dwThreadExitCode
                    );

                    if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
                    {
                        TerminateProcess(hProcess, 0);
                        return dwRet;
                    }
                    return ERROR_DELETE_SETTINGS;
                }
                else
                {

                    wprintf(
                        L"9. Injection entry point failed in DWM (%d).\n",
                        dwThreadExitCode
                    );

                    if (dwRet = FreeRemoteLibrary(hAdrFreeLibrary))
                    {
                        TerminateProcess(hProcess, 0);
                        return dwRet;
                    }
                    return ERROR_RUNNING_HOOK_ENTRY_POINT;
                }
            }
        }

        wprintf(L"9. Successfully hooked DWM.\n");

        free(hMods);

        // Step 10: Register and create window
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClass(&wc);
        hWnd = CreateWindowEx(
            0,                      // Optional window styles
            CLASS_NAME,             // Window class
            TEXT(""),               // Window text
            WS_OVERLAPPEDWINDOW,    // Window style
            // Size and position
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            NULL,                   // Parent window    
            NULL,                   // Menu
            hInstance,              // Instance handle
            NULL                    // Additional application data
        );
        if (!hWnd)
        {

            wprintf(L"10. Failed to create message window (%d).\n", GetLastError());

            ExitHandler(NULL);
            return ERROR_CREATE_MESSAGE_WINDOW;
        }

        wprintf(L"10. Successfully created message window (%d).\n", hWnd);


        // Step 11: Listen for DWM crashes
        RegisterWaitForSingleObject(
            &hWaitObject,
            hProcess,
            reinterpret_cast<WAITORTIMERCALLBACK>(WaitForDWMToCrash),
            reinterpret_cast<PVOID>(hWnd),
            INFINITE,
            WT_EXECUTEONLYONCE
        );
        if (!hWaitObject)
        {

            wprintf(L"11. Unable to register for watching DWM.\n");

            ExitHandler(NULL);
            return ERROR_REGISTER_DWM_WATCH;
        }

        wprintf(L"11. Registered for watching DWM.\n");


        // Step 12: Register exception handler
        if (!pExitHandler)
        {
            pExitHandler = SetUnhandledExceptionFilter(
                reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(ExitHandler)
            );
            if (!pExitHandler)
            {

                wprintf(L"12. Failed to register exit handler.\n");

                ExitHandler(NULL);
                return ERROR_REGISTER_EXIT_HANDLER;
            }

            wprintf(
                L"12. Registered exception handler (%d).\n",
                pExitHandler
            );

        }

        // Step 13: Listen for messages

        wprintf(L"Listening for messages...\n");

        while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (bRet == -1)
            {
                ExitHandler(NULL);
                return ERROR_MESSAGE_QUEUE;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (msg.wParam != ERROR_DWM_CRASHED)
        {

            wprintf(L"Shutting down application...\n");

            ExitHandler(NULL);
            return ERROR_SUCCESS;
        }
        // not required; in fact, it will post a WM_QUIT 
        // for the next window we spawn; really stupid idea
        //DestroyWindow(hWnd);
        UnregisterClass(CLASS_NAME, hInstance);


        wprintf(L"DWM was restarted, rehooking...\n");


        // wait a bit for DWM to respawn
        Sleep(DWM_RESTART_WAIT_DELAY);
    }

	return ERROR_SUCCESS;
}