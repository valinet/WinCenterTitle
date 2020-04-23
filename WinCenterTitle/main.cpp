#include <iostream>
#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <tlhelp32.h>
#include <Psapi.h>
#include <assert.h>
#include <conio.h>

#define CLASS_NAME L"WinCenterTitle"

#define DWM_CRASHED -2222
#define WM_DWM_CRASHED WM_USER + 1

#ifdef _DEBUG
#define DEBUG
#endif

HANDLE hThread = NULL;
HANDLE hProcess = NULL;
HMODULE hMod = NULL;
uint64_t hInjection = 0;
DWORD hLibModule = 0;

LONG ExitHandler(LPEXCEPTION_POINTERS p)
{
    HMODULE hKernel32 = NULL;
    FARPROC hAdrFreeLibrary = NULL;

    hThread = CreateRemoteThread(
        hProcess,
        NULL,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>
        ((uint64_t)hMod + (uint64_t)hInjection),
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
        &hLibModule
    );
    if (hLibModule)
    {
#ifdef DEBUG
        wprintf(L"E. Error while unhooking DWM (%d).\n", hLibModule);
#endif
    }
#ifdef DEBUG
    wprintf(L"E. Successfully unhooked DWM.\n");
#endif
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
        PostMessage(hWnd, WM_QUIT, DWM_CRASHED, 0);
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

int WINAPI wWinMain(
    HINSTANCE hInstance, 
    HINSTANCE hPrevInstance, 
    PWSTR pCmdLine, 
    int nCmdShow
)
{
    LPTOP_LEVEL_EXCEPTION_FILTER pExitHandler = NULL;
    HMODULE hKernel32 = NULL;
    FARPROC hAdrLoadLibrary = NULL;
    void* pLibRemote = NULL;
    wchar_t szLibPath[_MAX_PATH];
    wchar_t szTmpLibPath[_MAX_PATH];
    BOOL bResult = FALSE;
    HANDLE snapshot = NULL;
    PROCESSENTRY32 entry;
    DWORD processId = 0;
    HMODULE* hMods = NULL;
    DWORD hModuleArrayInitialBytesInitial = 100 * sizeof(HMODULE);
    DWORD hModuleArrayInitialBytes = hModuleArrayInitialBytesInitial;
    DWORD hModuleArrayBytesNeeded = 0;
    FILE* conout = NULL;
    HMODULE hInjectionDll = NULL;
    FARPROC hInjectionMainFunc = NULL;
    SIZE_T i = 0;
    BOOL bRet = FALSE;
    WNDCLASS wc = { };
    HWND hWnd;
    MSG msg = { };
    HANDLE waitObject = NULL;
    HHOOK hook = NULL;

    // Step 0: Print debug info
#ifdef DEBUG
    if (!AllocConsole());
    if (freopen_s(&conout, "CONOUT$", "w", stdout));
    wprintf(L"Center Windows Titlebars\n========================\n");
#endif

    // Step 1: Format hook library path
    GetModuleFileName(
        GetModuleHandle(NULL),
        szLibPath,
        _MAX_PATH
    );
    PathRemoveFileSpec(szLibPath);
    lstrcat(
        szLibPath,
        L"\\WinCenterTitleLibrary.dll"
    );
#ifdef DEBUG
    wprintf(
        L"1. Hook Library Path: %s\n", 
        szLibPath
    );
#endif

    // Step 2: Get DLL entry point address
    hInjectionDll = LoadLibrary(szLibPath);
    hInjectionMainFunc = GetProcAddress(
        hInjectionDll,
        "main"
    );
    hInjection = reinterpret_cast<DWORD>(hInjectionMainFunc) -
        reinterpret_cast<DWORD>(hInjectionDll);
    FreeLibrary(hInjectionDll);
#ifdef DEBUG
    wprintf(
        L"2. Hook Library Entry Point: 0x%x\n", 
        hInjection
    );
#endif

    // Step 3: Get address of LoadLibrary
    hKernel32 = GetModuleHandle(L"Kernel32");
    assert(hKernel32 != NULL);
    hAdrLoadLibrary = GetProcAddress(
        hKernel32,
        "LoadLibraryW"
    );
    assert(hAdrLoadLibrary != NULL);
#ifdef DEBUG
    wprintf(
        L"3. LoadLibraryW address: %d\n", 
        hAdrLoadLibrary
    );
#endif

    while (TRUE)
    {
        // Step 4: Find DWM.exe
        entry.dwSize = sizeof(PROCESSENTRY32);
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (Process32First(snapshot, &entry) == TRUE)
        {
            while (Process32Next(snapshot, &entry) == TRUE)
            {
                if (!wcscmp(entry.szExeFile, L"dwm.exe"))
                {
                    processId = entry.th32ProcessID;
                    hProcess = OpenProcess(
                        PROCESS_ALL_ACCESS,
                        FALSE,
                        processId
                    );
                    assert(hProcess != NULL);
#ifdef DEBUG
                    wprintf(
                        L"4. Found Desktop Window manager, PID: %d\n",
                        processId
                    );
#endif
                    break;
                }
            }
        }
        CloseHandle(snapshot);
        if (processId == 0)
        {
#ifdef DEBUG
            wprintf(L"4. ERROR: Desktop Window Manager is not running.\n");
#endif
            return -4;
        }

        // Step 5: Marshall path to library in DWM's memory
        pLibRemote = VirtualAllocEx(
            hProcess,
            NULL,
            sizeof(szLibPath),
            MEM_COMMIT,
            PAGE_READWRITE
        );
        assert(pLibRemote != NULL);
        bResult = WriteProcessMemory(
            hProcess,
            pLibRemote,
            (void*)szLibPath,
            sizeof(szLibPath),
            NULL
        );
        assert(bResult == TRUE);
#ifdef DEBUG
        wprintf(L"5. Marshalled library path in DWM's memory.\n");
#endif

        // Step 6: Load library in DWM
        hThread = CreateRemoteThread(
            hProcess,
            NULL,
            0,
            (LPTHREAD_START_ROUTINE)hAdrLoadLibrary,
            pLibRemote,
            0,
            NULL
        );
        assert(hThread != NULL);
        WaitForSingleObject(
            hThread,
            INFINITE
        );
        GetExitCodeThread(
            hThread,
            &hLibModule
        );
        assert(hLibModule != NULL);
        if (!hLibModule)
        {
#ifdef DEBUG
            wprintf(L"6. ERROR: Failed to inject library into DWM.\n");
#endif
            return -6;
        }
#ifdef DEBUG
        wprintf(
            L"6. Successfully injected library into DWM (%d).\n",
            hLibModule
        );
#endif
        // Step 7: Free path from DWM's memory
        VirtualFreeEx(
            hProcess,
            (LPVOID)pLibRemote,
            0,
            MEM_RELEASE
        );
#ifdef DEBUG
        wprintf(L"7. Freed path from DWM's memory.\n");
#endif

        // Step 8: Get address of library in DWM's memory
        hModuleArrayInitialBytes = hModuleArrayInitialBytesInitial;
        hMods = (HMODULE*)calloc(
            hModuleArrayInitialBytes, 1
        );
        assert(hMods != NULL);
        bResult = EnumProcessModulesEx(
            hProcess,
            hMods,
            hModuleArrayInitialBytes,
            &hModuleArrayBytesNeeded,
            LIST_MODULES_ALL
        );
        assert(bResult == TRUE);
        if (hModuleArrayInitialBytes < hModuleArrayBytesNeeded)
        {
            hMods = (HMODULE*)realloc(
                hMods,
                hModuleArrayBytesNeeded
            );
            assert(hMods != NULL);
            hModuleArrayInitialBytes = hModuleArrayBytesNeeded;
            bResult = EnumProcessModulesEx(
                hProcess,
                hMods,
                hModuleArrayInitialBytes,
                &hModuleArrayBytesNeeded,
                LIST_MODULES_ALL
            );
            assert(bResult == TRUE);
        }
        CharLower(szLibPath);
        if (hModuleArrayBytesNeeded / sizeof(HMODULE) == 0)
        {
            i = -1;
        }
        else
        {
            for (i = 0; i < hModuleArrayBytesNeeded / sizeof(HMODULE); ++i)
            {
                bResult = GetModuleFileNameEx(
                    hProcess,
                    hMods[i],
                    szTmpLibPath,
                    _MAX_PATH
                );
                CharLower(szTmpLibPath);
                if (!wcscmp(szTmpLibPath, szLibPath))
                {
                    break;
                }
            }
        }
        if (i == -1)
        {
#ifdef DEBUG
            wprintf(L"8. ERROR: Cannot find library in DWM's memory.\n");
#endif
            return -8;
        }
#ifdef DEBUG
        wprintf(
            L"8. Found library in DWM's memory (%d/%d).\n",
            i,
            hModuleArrayBytesNeeded / sizeof(HMODULE)
        );
#endif
        // Step 9: Run DLL's entry point
        hMod = hMods[i];
        hThread = CreateRemoteThread(
            hProcess,
            NULL,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>
                ((uint64_t)hMod + (uint64_t)hInjection),
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
            &hLibModule
        );
        assert(hLibModule != NULL);
        if (hLibModule)
        {
#ifdef DEBUG
            wprintf(L"9. Error while hooking DWM (%d).\n", hLibModule);
#endif
            return -9;
        }
#ifdef DEBUG
        wprintf(L"9. Successfully hooked DWM.\n");
#endif
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
            CLASS_NAME,          // Window class
            TEXT(""),                    // Window text
            WS_OVERLAPPEDWINDOW,    // Window style
            // Size and position
            100,
            100,
            300,
            300,
            NULL,       // Parent window    
            NULL,       // Menu
            hInstance,  // Instance handle
            NULL      // Additional application data
        );
        if (!hWnd)
        {
#ifdef DEBUG
            wprintf(L"10. Failed to create message window (%d).\n", GetLastError());
            ExitHandler(NULL);
            return -10;
#endif
        }
#ifdef DEBUG
        wprintf(L"10. Successfully created message window (%d).\n", hWnd);
#endif

        // Step 11: Listen for DWM crashes
        RegisterWaitForSingleObject(
            &waitObject,
            hProcess,
            reinterpret_cast<WAITORTIMERCALLBACK>(WaitForDWMToCrash),
            reinterpret_cast<PVOID>(hWnd),
            INFINITE,
            WT_EXECUTEONLYONCE
        );
        if (!waitObject)
        {
#ifdef DEBUG
            wprintf(L"11. Unable to register for watching DWM.\n");
            ExitHandler(NULL);
            return -11;
#endif
        }
#ifdef DEBUG
        wprintf(L"11. Registered for watching DWM.\n");
#endif

        // Step 12: Register exception handler
        if (!pExitHandler)
        {
            pExitHandler = SetUnhandledExceptionFilter(
                reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(ExitHandler)
            );
#ifdef DEBUG
            wprintf(
                L"12. Registered exception handler (%d).\n",
                pExitHandler
            );
#endif
        }
#ifdef DEBUG
        wprintf(L"Listening for messages...\n");
#endif
        while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (bRet == -1)
            {
                ExitHandler(NULL);
                return 0;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (msg.wParam != DWM_CRASHED)
        {
#ifdef DEBUG
            wprintf(L"Shutting down application...\n");
#endif
            ExitHandler(NULL);
            return DWM_CRASHED;
        }
        DestroyWindow(hWnd);
        UnregisterClass(CLASS_NAME, hInstance);

#ifdef DEBUG
        wprintf(L"DWM was restarted, rehooking...\n");
#endif
    }

	return 0;
}