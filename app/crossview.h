#pragma once

#include <Windows.h>
#include <string>
#include <vector>

struct CrossViewSnapshot {
    bool win32Ok = false;
    bool mftOk = false;
    bool directOpenAttempted = false;
    bool directOpenOk = false;
    DWORD win32Error = ERROR_SUCCESS;
    DWORD mftError = ERROR_SUCCESS;
    DWORD directOpenError = ERROR_SUCCESS;
    unsigned long long directoryReference = 0;
    std::vector<std::wstring> win32Entries;
    std::vector<std::wstring> mftEntries;
    std::vector<std::wstring> missingFromWin32;
    std::wstring directOpenTarget;
    std::wstring classification = L"inconclusive";
};

CrossViewSnapshot CaptureCrossView();
bool NamesEqualInsensitive(const std::wstring& left, const std::wstring& right);
bool ContainsNameInsensitive(
    const std::vector<std::wstring>& values,
    const std::wstring& name);
