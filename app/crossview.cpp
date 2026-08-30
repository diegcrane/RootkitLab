#include "crossview.h"

#include <winioctl.h>
#include <algorithm>
#include <vector>

#include "../shared/filter_protocol.h"

namespace {

constexpr DWORD kUsnBufferSize = 64u * 1024u;
constexpr size_t kMaximumEntries = 512u;

void SortAndUnique(std::vector<std::wstring>& values)
{
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    values.erase(
        std::unique(values.begin(), values.end(), [](const auto& left, const auto& right) {
            return NamesEqualInsensitive(left, right);
        }),
        values.end());
}

bool EnumerateWin32(std::vector<std::wstring>& entries, DWORD& error)
{
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = std::wstring(RKL_FILTER_SANDBOX_PATH) + L"\\*";
    HANDLE search = FindFirstFileW(pattern.c_str(), &data);

    error = ERROR_SUCCESS;
    if (search == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    do {
        if (wcscmp(data.cFileName, L".") != 0 &&
            wcscmp(data.cFileName, L"..") != 0) {
            entries.emplace_back(data.cFileName);
        }
    } while (FindNextFileW(search, &data));

    error = GetLastError();
    FindClose(search);
    if (error != ERROR_NO_MORE_FILES) {
        return false;
    }
    error = ERROR_SUCCESS;
    SortAndUnique(entries);
    return true;
}

bool GetDirectoryReferenceNumber(
    unsigned long long& reference,
    DWORD& error)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION information{};

    error = ERROR_SUCCESS;
    directory = CreateFileW(
        RKL_FILTER_SANDBOX_PATH,
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    if (!GetFileInformationByHandle(directory, &information)) {
        error = GetLastError();
        CloseHandle(directory);
        return false;
    }
    CloseHandle(directory);
    reference = (static_cast<unsigned long long>(information.nFileIndexHigh) << 32) |
        information.nFileIndexLow;
    return true;
}

bool EnumerateMftChildren(
    unsigned long long parentReference,
    std::vector<std::wstring>& entries,
    DWORD& error)
{
    HANDLE volume;
    MFT_ENUM_DATA_V0 enumeration{};
    std::vector<BYTE> buffer(kUsnBufferSize);
    bool completed = false;

    error = ERROR_SUCCESS;
    volume = CreateFileW(
        L"\\\\.\\C:",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (volume == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }

    enumeration.LowUsn = 0;
    enumeration.HighUsn = MAXLONGLONG;
    for (;;) {
        DWORD returnedBytes = 0;
        if (!DeviceIoControl(
                volume,
                FSCTL_ENUM_USN_DATA,
                &enumeration,
                sizeof(enumeration),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &returnedBytes,
                nullptr)) {
            error = GetLastError();
            if (error == ERROR_HANDLE_EOF) {
                error = ERROR_SUCCESS;
                completed = true;
            }
            break;
        }
        if (returnedBytes < sizeof(DWORDLONG)) {
            error = ERROR_INVALID_DATA;
            break;
        }

        enumeration.StartFileReferenceNumber =
            *reinterpret_cast<const DWORDLONG*>(buffer.data());
        const BYTE* cursor = buffer.data() + sizeof(DWORDLONG);
        const BYTE* end = buffer.data() + returnedBytes;
        while (cursor < end) {
            if (static_cast<size_t>(end - cursor) < sizeof(USN_RECORD_COMMON_HEADER)) {
                error = ERROR_INVALID_DATA;
                CloseHandle(volume);
                return false;
            }
            const auto* common =
                reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(cursor);
            if (common->RecordLength < sizeof(USN_RECORD_COMMON_HEADER) ||
                common->RecordLength > static_cast<DWORD>(end - cursor)) {
                error = ERROR_INVALID_DATA;
                CloseHandle(volume);
                return false;
            }

            if (common->MajorVersion == 2) {
                const auto* record = reinterpret_cast<const USN_RECORD_V2*>(cursor);
                const ULONG nameEnd =
                    static_cast<ULONG>(record->FileNameOffset) + record->FileNameLength;
                if (record->RecordLength < FIELD_OFFSET(USN_RECORD_V2, FileName) ||
                    nameEnd > record->RecordLength ||
                    (record->FileNameLength % sizeof(wchar_t)) != 0) {
                    error = ERROR_INVALID_DATA;
                    CloseHandle(volume);
                    return false;
                }
                if (record->ParentFileReferenceNumber == parentReference) {
                    const auto* name = reinterpret_cast<const wchar_t*>(
                        cursor + record->FileNameOffset);
                    entries.emplace_back(
                        name,
                        record->FileNameLength / sizeof(wchar_t));
                    if (entries.size() > kMaximumEntries) {
                        error = ERROR_MORE_DATA;
                        CloseHandle(volume);
                        return false;
                    }
                }
            }
            cursor += common->RecordLength;
        }
    }

    CloseHandle(volume);
    SortAndUnique(entries);
    return completed && error == ERROR_SUCCESS;
}

bool DirectOpenFile(const std::wstring& name, DWORD& error)
{
    const std::wstring path =
        std::wstring(RKL_FILTER_SANDBOX_PATH) + L"\\" + name;
    HANDLE file;

    error = ERROR_SUCCESS;
    file = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    CloseHandle(file);
    return true;
}

} // namespace

bool NamesEqualInsensitive(const std::wstring& left, const std::wstring& right)
{
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool ContainsNameInsensitive(
    const std::vector<std::wstring>& values,
    const std::wstring& name)
{
    return std::any_of(values.begin(), values.end(), [&](const auto& value) {
        return NamesEqualInsensitive(value, name);
    });
}

CrossViewSnapshot CaptureCrossView()
{
    CrossViewSnapshot snapshot;

    snapshot.win32Ok = EnumerateWin32(snapshot.win32Entries, snapshot.win32Error);
    if (GetDirectoryReferenceNumber(
            snapshot.directoryReference,
            snapshot.mftError)) {
        snapshot.mftOk = EnumerateMftChildren(
            snapshot.directoryReference,
            snapshot.mftEntries,
            snapshot.mftError);
    }

    if (snapshot.win32Ok && snapshot.mftOk) {
        for (const auto& name : snapshot.mftEntries) {
            if (!ContainsNameInsensitive(snapshot.win32Entries, name)) {
                snapshot.missingFromWin32.push_back(name);
            }
        }
    }

    if (!snapshot.missingFromWin32.empty()) {
        snapshot.directOpenTarget = snapshot.missingFromWin32.front();
    } else {
        for (const auto& name : snapshot.mftEntries) {
            if (!NamesEqualInsensitive(name, RKL_FILTER_MARKER_NAME)) {
                snapshot.directOpenTarget = name;
                break;
            }
        }
    }
    if (!snapshot.directOpenTarget.empty()) {
        snapshot.directOpenAttempted = true;
        snapshot.directOpenOk = DirectOpenFile(
            snapshot.directOpenTarget,
            snapshot.directOpenError);
    }

    if (!snapshot.win32Ok || !snapshot.mftOk) {
        snapshot.classification = L"inconclusive";
    } else if (!snapshot.missingFromWin32.empty() && snapshot.directOpenOk) {
        snapshot.classification = L"cross_view_inconsistency";
    } else if (snapshot.missingFromWin32.empty()) {
        snapshot.classification = L"consistent";
    } else {
        snapshot.classification = L"missing_object";
    }
    return snapshot;
}
