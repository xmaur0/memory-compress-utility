#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <iostream>

typedef LONG NTSTATUS;

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// Definições nativas extraídas do código-fonte do Mem Reduct
typedef enum _SYSTEM_INFORMATION_CLASS_CUSTOM {
    SystemMemoryListInformation = 80,
    SystemFileCacheInformationEx = 81,
    SystemRegistryReconciliationInformation = 76,
    SystemCombinePhysicalMemoryInformation = 130
} SYSTEM_INFORMATION_CLASS_CUSTOM;

typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
    MemoryZeroWorkingSets = 1,
    MemoryFlushModifiedList = 2,
    MemoryPurgeStandbyList = 4,
    MemoryPurgeLowPriorityStandbyList = 5,
    MemoryEmptyWorkingSets = 2
} SYSTEM_MEMORY_LIST_COMMAND;

typedef struct _SYSTEM_FILECACHE_INFORMATION {
    SIZE_T CurrentSize;
    SIZE_T PeakSize;
    ULONG PageFaultCount;
    SIZE_T MinimumWorkingSet;
    SIZE_T MaximumWorkingSet;
    SIZE_T CurrentShadowSize;
    SIZE_T PeakShadowSize;
    ULONG ResponseTime;
} SYSTEM_FILECACHE_INFORMATION, *PSYSTEM_FILECACHE_INFORMATION;

typedef struct _MEMORY_COMBINE_INFORMATION_EX {
    HANDLE Handle;
    ULONGLONG PagesCombined;
    ULONG Flags;
} MEMORY_COMBINE_INFORMATION_EX, *PMEMORY_COMBINE_INFORMATION_EX;

typedef NTSTATUS (WINAPI *pfnNtSetSystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength
);

// Intervalo fixo de 24 horas em milissegundos (86400000 ms)
const ULONGLONG MEMORY_CLEAN_INTERVAL_MS = 86400000ULL;     
const DWORD CHECK_INTERVAL_MS = 5000; // Intervalo de espera do loop principal

HANDLE hSingleInstanceMutex = NULL;
const wchar_t* MUTEX_NAME = L"Local\\MemReduct_24h_Mutex_UniqueKey";

bool CheckSingleInstance() {
    hSingleInstanceMutex = CreateMutex(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hSingleInstanceMutex) {
            CloseHandle(hSingleInstanceMutex);
            hSingleInstanceMutex = NULL;
        }
        return false;
    }
    return true;
}

// Habilita privilégios necessários para limpeza em nível de kernel (como no Mem Reduct)
bool EnablePrivileges() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LookupPrivilegeValue(NULL, SE_PROF_SINGLE_PROCESS_NAME, &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(hToken);
        return true;
    }
    return false;
}

// Rotina oficial de limpeza com os parâmetros extraídos diretamente do Mem Reduct
void ExecuteMemReductCleanup() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return;

    pfnNtSetSystemInformation NtSetSysInfo = (pfnNtSetSystemInformation)GetProcAddress(hNtdll, "NtSetSystemInformation");
    if (!NtSetSysInfo) return;

    MEMORYSTATUSEX memBefore = { sizeof(memBefore) };
    GlobalMemoryStatusEx(&memBefore);

    std::wcout << L"\n[+] Executando rotina oficial de limpeza (Mem Reduct style)..." << std::endl;

    // 1. Working Set
    SYSTEM_MEMORY_LIST_COMMAND cmdWorkingSet = MemoryEmptyWorkingSets;
    NtSetSysInfo(SystemMemoryListInformation, &cmdWorkingSet, sizeof(cmdWorkingSet));

    // 2 & 3. System File Cache
    SYSTEM_FILECACHE_INFORMATION sfci = { 0 };
    sfci.MinimumWorkingSet = (SIZE_T)-1;
    sfci.MaximumWorkingSet = (SIZE_T)-1;
    NtSetSysInfo(SystemFileCacheInformationEx, &sfci, sizeof(sfci));

    // 4. Modified Page List
    SYSTEM_MEMORY_LIST_COMMAND cmdModList = MemoryFlushModifiedList;
    NtSetSysInfo(SystemMemoryListInformation, &cmdModList, sizeof(cmdModList));

    // 5. Standby List
    SYSTEM_MEMORY_LIST_COMMAND cmdStandby = MemoryPurgeStandbyList;
    NtSetSysInfo(SystemMemoryListInformation, &cmdStandby, sizeof(cmdStandby));

    // 6. Standby Priority 0 List
    SYSTEM_MEMORY_LIST_COMMAND cmdStandbyLow = MemoryPurgeLowPriorityStandbyList;
    NtSetSysInfo(SystemMemoryListInformation, &cmdStandbyLow, sizeof(cmdStandbyLow));

    // 7. Registry Cache (Win 8.1+)
    NtSetSysInfo(SystemRegistryReconciliationInformation, NULL, 0);

    // 8. Combine Memory Lists (Win 10+)
    MEMORY_COMBINE_INFORMATION_EX combineInfo = { 0 };
    NtSetSysInfo(SystemCombinePhysicalMemoryInformation, &combineInfo, sizeof(combineInfo));

    MEMORYSTATUSEX memAfter = { sizeof(memAfter) };
    GlobalMemoryStatusEx(&memAfter);

    std::wcout << L"[+] Limpeza concluida! RAM Livre Antes: " << (memBefore.ullAvailPhys / 1024 / 1024) 
              << L" MB | Depois: " << (memAfter.ullAvailPhys / 1024 / 1024) << L" MB" << std::endl << std::endl;
}

int main() {
    SetConsoleOutputCP(65001);

    if (!CheckSingleInstance()) {
        std::wcout << L"[!] Ja existe uma instancia deste programa em execucao. Fechando..." << std::endl;
        Sleep(2000);
        return 0;
    }

    EnablePrivileges();

    std::cout << "====================================================" << std::endl;
    std::cout << "       LIBERADOR DE MEMORIA 24H (MEM REDUCT)        " << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "Execute como ADMINISTRADOR!" << std::endl;
    std::cout << "====================================================" << std::endl << std::endl;

    // Dispara a limpeza IMEDIATAMENTE ao abrir e agenda para cada 24h infinitamente
    ULONGLONG lastMemoryCleanTick = GetTickCount64() - MEMORY_CLEAN_INTERVAL_MS;

    while (true) {
        ULONGLONG currentTick = GetTickCount64();

        // Ciclo automático a cada 24 horas
        if (currentTick - lastMemoryCleanTick >= MEMORY_CLEAN_INTERVAL_MS) {
            ExecuteMemReductCleanup();
            lastMemoryCleanTick = GetTickCount64();
        }

        Sleep(CHECK_INTERVAL_MS);
    }

    if (hSingleInstanceMutex) {
        ReleaseMutex(hSingleInstanceMutex);
        CloseHandle(hSingleInstanceMutex);
    }

    return 0;
}