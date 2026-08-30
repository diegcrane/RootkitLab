#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include "../shared/filter_protocol.h"

#define RKL_FILTER_TAG 'fLKR'
#define RKL_TARGET_DIRECTORY_MAX_CHARS 512u

DRIVER_INITIALIZE DriverEntry;

typedef struct _RKL_FILTER_GLOBALS {
    PFLT_FILTER Filter;
    PFLT_PORT ServerPort;
    PFLT_PORT ClientPort;
    EX_PUSH_LOCK RuleLock;
    ULONG RuleCount;
    ULONG RuleRevision;
    RKL_FILTER_RULE Rules[RKL_FILTER_MAX_RULES];
    volatile LONG Enabled;
    volatile LONG64 DirectoryQueries;
    volatile LONG64 TargetDirectoryQueries;
    volatile LONG64 HiddenEntries;
    volatile LONG64 EnableTransitions;
    volatile LONG64 DisableTransitions;
    volatile LONG64 RuleUpdates;
    volatile LONG64 RejectedCommands;
    UNICODE_STRING TargetDirectoryName;
    WCHAR TargetDirectoryBuffer[RKL_TARGET_DIRECTORY_MAX_CHARS];
} RKL_FILTER_GLOBALS;

static RKL_FILTER_GLOBALS g_RklFilter;

static const UNICODE_STRING g_SandboxSuffix =
    RTL_CONSTANT_STRING(L"\\RootkitLabSandbox");

static NTSTATUS RklInitializeTargetDirectoryName(VOID)
{
    UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\C:");
    UNICODE_STRING target;
    OBJECT_ATTRIBUTES attributes;
    HANDLE linkHandle = NULL;
    NTSTATUS status;

    RtlZeroMemory(&target, sizeof(target));
    target.Buffer = g_RklFilter.TargetDirectoryBuffer;
    target.MaximumLength = (USHORT)(
        sizeof(g_RklFilter.TargetDirectoryBuffer) -
        g_SandboxSuffix.Length -
        sizeof(WCHAR));

    InitializeObjectAttributes(
        &attributes,
        &linkName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);
    status = ZwOpenSymbolicLinkObject(
        &linkHandle,
        GENERIC_READ,
        &attributes);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = ZwQuerySymbolicLinkObject(linkHandle, &target, NULL);
    ZwClose(linkHandle);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if ((ULONG)target.Length + g_SandboxSuffix.Length + sizeof(WCHAR) >
        sizeof(g_RklFilter.TargetDirectoryBuffer)) {
        return STATUS_NAME_TOO_LONG;
    }

    RtlCopyMemory(
        (PUCHAR)target.Buffer + target.Length,
        g_SandboxSuffix.Buffer,
        g_SandboxSuffix.Length);
    target.Length = (USHORT)(target.Length + g_SandboxSuffix.Length);
    target.Buffer[target.Length / sizeof(WCHAR)] = L'\0';
    target.MaximumLength = (USHORT)sizeof(g_RklFilter.TargetDirectoryBuffer);
    g_RklFilter.TargetDirectoryName = target;
    return STATUS_SUCCESS;
}

static NTSTATUS RklEnsureTargetDirectoryName(VOID)
{
    if (g_RklFilter.TargetDirectoryName.Length != 0) {
        return STATUS_SUCCESS;
    }
    return RklInitializeTargetDirectoryName();
}

static BOOLEAN RklMarkerExists(VOID)
{
    UNICODE_STRING markerPath =
        RTL_CONSTANT_STRING(L"\\??\\C:\\RootkitLabSandbox\\.rootkitlab-lab");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    HANDLE handle = NULL;
    NTSTATUS status;

    InitializeObjectAttributes(
        &attributes,
        &markerPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    status = ZwCreateFile(
        &handle,
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0);

    if (NT_SUCCESS(status)) {
        ZwClose(handle);
        return TRUE;
    }
    return FALSE;
}

static VOID RklFillResponse(
    _Out_ PRKL_FILTER_RESPONSE Response,
    _In_ NTSTATUS CommandStatus)
{
    ULONG index;

    RtlZeroMemory(Response, sizeof(*Response));
    Response->Magic = RKL_FILTER_MAGIC;
    Response->StructSize = sizeof(*Response);
    Response->AbiVersion = RKL_FILTER_ABI_VERSION;
    Response->CommandStatus = CommandStatus;
    Response->Enabled = (ULONG)(InterlockedCompareExchange(
        &g_RklFilter.Enabled, 0, 0) != 0);
    Response->MarkerPresent = (ULONG)RklMarkerExists();
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_RklFilter.RuleLock);
    Response->RuleCount = g_RklFilter.RuleCount;
    Response->RuleRevision = g_RklFilter.RuleRevision;
    for (index = 0; index < g_RklFilter.RuleCount; ++index) {
        Response->Rules[index] = g_RklFilter.Rules[index];
    }
    ExReleasePushLockShared(&g_RklFilter.RuleLock);
    KeLeaveCriticalRegion();
    Response->DirectoryQueries = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.DirectoryQueries, 0, 0);
    Response->TargetDirectoryQueries = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.TargetDirectoryQueries, 0, 0);
    Response->HiddenEntries = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.HiddenEntries, 0, 0);
    Response->EnableTransitions = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.EnableTransitions, 0, 0);
    Response->DisableTransitions = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.DisableTransitions, 0, 0);
    Response->RuleUpdates = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.RuleUpdates, 0, 0);
    Response->RejectedCommands = (ULONGLONG)InterlockedCompareExchange64(
        &g_RklFilter.RejectedCommands, 0, 0);
}

static NTSTATUS RklPortConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionPortCookie)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);

    g_RklFilter.ClientPort = ClientPort;
    *ConnectionPortCookie = NULL;
    return STATUS_SUCCESS;
}

static VOID RklPortDisconnect(_In_opt_ PVOID ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ConnectionCookie);
    if (g_RklFilter.ClientPort != NULL) {
        FltCloseClientPort(g_RklFilter.Filter, &g_RklFilter.ClientPort);
    }
}

static BOOLEAN RklRuleTargetExists(_In_ const RKL_FILTER_RULE* Rule)
{
    const UNICODE_STRING prefix =
        RTL_CONSTANT_STRING(L"\\??\\C:\\RootkitLabSandbox\\");
    WCHAR pathBuffer[
        (sizeof(L"\\??\\C:\\RootkitLabSandbox\\") / sizeof(WCHAR)) +
        RKL_FILTER_MAX_NAME_CHARS];
    UNICODE_STRING path;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    HANDLE handle = NULL;
    NTSTATUS status;

    if (prefix.Length + Rule->NameLengthBytes > sizeof(pathBuffer) - sizeof(WCHAR)) {
        return FALSE;
    }

    RtlCopyMemory(pathBuffer, prefix.Buffer, prefix.Length);
    RtlCopyMemory(
        (PUCHAR)pathBuffer + prefix.Length,
        Rule->Name,
        Rule->NameLengthBytes);
    pathBuffer[(prefix.Length + Rule->NameLengthBytes) / sizeof(WCHAR)] = L'\0';

    path.Buffer = pathBuffer;
    path.Length = (USHORT)(prefix.Length + Rule->NameLengthBytes);
    path.MaximumLength = path.Length + sizeof(WCHAR);
    InitializeObjectAttributes(
        &attributes,
        &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    status = ZwCreateFile(
        &handle,
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }
    ZwClose(handle);
    return TRUE;
}

static BOOLEAN RklRuleNameIsValid(_In_ const RKL_FILTER_RULE* Rule)
{
    const UNICODE_STRING marker = RTL_CONSTANT_STRING(RKL_FILTER_MARKER_NAME);
    UNICODE_STRING name;
    ULONG characterCount;
    ULONG index;

    if (Rule->NameLengthBytes == 0 ||
        Rule->NameLengthBytes >
            (RKL_FILTER_MAX_NAME_CHARS - 1u) * sizeof(WCHAR) ||
        (Rule->NameLengthBytes % sizeof(WCHAR)) != 0) {
        return FALSE;
    }

    characterCount = Rule->NameLengthBytes / sizeof(WCHAR);
    if (Rule->Name[characterCount] != L'\0' ||
        Rule->Name[characterCount - 1u] == L'.' ||
        Rule->Name[characterCount - 1u] == L' ') {
        return FALSE;
    }

    for (index = 0; index < characterCount; ++index) {
        WCHAR character = Rule->Name[index];
        if (character < 0x20 ||
            character == L'\\' || character == L'/' ||
            character == L':' || character == L'*' ||
            character == L'?' || character == L'"' ||
            character == L'<' || character == L'>' ||
            character == L'|') {
            return FALSE;
        }
    }

    name.Buffer = (PWCH)Rule->Name;
    name.Length = (USHORT)Rule->NameLengthBytes;
    name.MaximumLength = name.Length;
    if (RtlEqualUnicodeString(&name, &marker, TRUE)) {
        return FALSE;
    }
    return RklRuleTargetExists(Rule);
}

static NTSTATUS RklReplaceRules(_In_ const RKL_FILTER_REQUEST* Request)
{
    PRKL_FILTER_RULE staged;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG index;
    ULONG previous;

    if (InterlockedCompareExchange(&g_RklFilter.Enabled, 0, 0) != 0) {
        return STATUS_DEVICE_BUSY;
    }
    if (!RklMarkerExists()) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    status = RklEnsureTargetDirectoryName();
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (Request->RuleCount > RKL_FILTER_MAX_RULES) {
        return STATUS_INVALID_PARAMETER;
    }

    staged = (PRKL_FILTER_RULE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(g_RklFilter.Rules),
        RKL_FILTER_TAG);
    if (staged == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(staged, sizeof(g_RklFilter.Rules));
    for (index = 0; index < Request->RuleCount; ++index) {
        staged[index] = Request->Rules[index];
        if (!RklRuleNameIsValid(&staged[index])) {
            status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        for (previous = 0; previous < index; ++previous) {
            UNICODE_STRING left;
            UNICODE_STRING right;

            left.Buffer = staged[index].Name;
            left.Length = (USHORT)staged[index].NameLengthBytes;
            left.MaximumLength = left.Length;
            right.Buffer = staged[previous].Name;
            right.Length = (USHORT)staged[previous].NameLengthBytes;
            right.MaximumLength = right.Length;
            if (RtlEqualUnicodeString(&left, &right, TRUE)) {
                status = STATUS_DUPLICATE_NAME;
                goto Exit;
            }
        }
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_RklFilter.RuleLock);
    RtlZeroMemory(g_RklFilter.Rules, sizeof(g_RklFilter.Rules));
    for (index = 0; index < Request->RuleCount; ++index) {
        g_RklFilter.Rules[index] = staged[index];
    }
    g_RklFilter.RuleCount = Request->RuleCount;
    ++g_RklFilter.RuleRevision;
    ExReleasePushLockExclusive(&g_RklFilter.RuleLock);
    KeLeaveCriticalRegion();
    InterlockedIncrement64(&g_RklFilter.RuleUpdates);

Exit:
    ExFreePoolWithTag(staged, RKL_FILTER_TAG);
    return status;
}

static ULONG RklGetRuleCount(VOID)
{
    ULONG count;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_RklFilter.RuleLock);
    count = g_RklFilter.RuleCount;
    ExReleasePushLockShared(&g_RklFilter.RuleLock);
    KeLeaveCriticalRegion();
    return count;
}

static NTSTATUS RklPortMessage(
    _In_opt_ PVOID PortCookie,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength)
        PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_ PULONG ReturnOutputBufferLength)
{
    PRKL_FILTER_REQUEST request = NULL;
    PRKL_FILTER_RESPONSE response = NULL;
    NTSTATUS commandStatus = STATUS_SUCCESS;
    NTSTATUS callbackStatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(PortCookie);
    *ReturnOutputBufferLength = 0;

    if (InputBuffer == NULL ||
        InputBufferLength < sizeof(RKL_FILTER_REQUEST) ||
        OutputBuffer == NULL ||
        OutputBufferLength < sizeof(RKL_FILTER_RESPONSE)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    request = (PRKL_FILTER_REQUEST)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(*request),
        RKL_FILTER_TAG);
    response = (PRKL_FILTER_RESPONSE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(*response),
        RKL_FILTER_TAG);
    if (request == NULL || response == NULL) {
        callbackStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    __try {
        RtlCopyMemory(request, InputBuffer, sizeof(*request));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        callbackStatus = GetExceptionCode();
        goto Exit;
    }

    if (request->Magic != RKL_FILTER_MAGIC ||
        request->StructSize != sizeof(*request) ||
        request->AbiVersion != RKL_FILTER_ABI_VERSION) {
        commandStatus = STATUS_REVISION_MISMATCH;
    } else {
        switch ((RKL_FILTER_COMMAND)request->Command) {
        case RklFilterCommandStatus:
            break;

        case RklFilterCommandEnable:
            commandStatus = RklEnsureTargetDirectoryName();
            if (!NT_SUCCESS(commandStatus)) {
                break;
            }
            if (!RklMarkerExists()) {
                commandStatus = STATUS_OBJECT_NAME_NOT_FOUND;
            } else if (RklGetRuleCount() == 0) {
                commandStatus = STATUS_NOT_FOUND;
            } else if (InterlockedExchange(&g_RklFilter.Enabled, 1) == 0) {
                InterlockedIncrement64(&g_RklFilter.EnableTransitions);
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "RootkitLabFilter: lab filter enabled\n");
            }
            break;

        case RklFilterCommandDisable:
            if (InterlockedExchange(&g_RklFilter.Enabled, 0) != 0) {
                InterlockedIncrement64(&g_RklFilter.DisableTransitions);
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "RootkitLabFilter: lab filter disabled\n");
            }
            break;

        case RklFilterCommandClearCounters:
            InterlockedExchange64(&g_RklFilter.DirectoryQueries, 0);
            InterlockedExchange64(&g_RklFilter.TargetDirectoryQueries, 0);
            InterlockedExchange64(&g_RklFilter.HiddenEntries, 0);
            InterlockedExchange64(&g_RklFilter.EnableTransitions, 0);
            InterlockedExchange64(&g_RklFilter.DisableTransitions, 0);
            InterlockedExchange64(&g_RklFilter.RuleUpdates, 0);
            InterlockedExchange64(&g_RklFilter.RejectedCommands, 0);
            break;

        case RklFilterCommandReplaceRules:
            commandStatus = RklReplaceRules(request);
            break;

        default:
            commandStatus = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
    }

    if (!NT_SUCCESS(commandStatus)) {
        InterlockedIncrement64(&g_RklFilter.RejectedCommands);
    }

    RklFillResponse(response, commandStatus);
    __try {
        RtlCopyMemory(OutputBuffer, response, sizeof(*response));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        callbackStatus = GetExceptionCode();
        goto Exit;
    }
    *ReturnOutputBufferLength = sizeof(*response);

Exit:
    if (response != NULL) {
        ExFreePoolWithTag(response, RKL_FILTER_TAG);
    }
    if (request != NULL) {
        ExFreePoolWithTag(request, RKL_FILTER_TAG);
    }
    return callbackStatus;
}

_Success_(return != FALSE)
static BOOLEAN RklGetDirectoryLayout(
    _In_ FILE_INFORMATION_CLASS InformationClass,
    _Out_ PULONG FileNameLengthOffset,
    _Out_ PULONG FileNameOffset)
{
    switch (InformationClass) {
    case FileDirectoryInformation:
        *FileNameLengthOffset = FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileNameLength);
        *FileNameOffset = FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);
        return TRUE;
    case FileFullDirectoryInformation:
        *FileNameLengthOffset = FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileNameLength);
        *FileNameOffset = FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);
        return TRUE;
    case FileBothDirectoryInformation:
        *FileNameLengthOffset = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileNameLength);
        *FileNameOffset = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
        return TRUE;
    case FileNamesInformation:
        *FileNameLengthOffset = FIELD_OFFSET(FILE_NAMES_INFORMATION, FileNameLength);
        *FileNameOffset = FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);
        return TRUE;
    case FileIdBothDirectoryInformation:
        *FileNameLengthOffset = FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileNameLength);
        *FileNameOffset = FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName);
        return TRUE;
    case FileIdFullDirectoryInformation:
        *FileNameLengthOffset = FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileNameLength);
        *FileNameOffset = FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName);
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOLEAN RklNameShouldBeHidden(
    _In_reads_bytes_(FileNameLength) const WCHAR* FileName,
    _In_ ULONG FileNameLength)
{
    UNICODE_STRING name;
    ULONG index;

    if (FileNameLength == 0 || FileNameLength > MAXUSHORT) {
        return FALSE;
    }

    name.Buffer = (PWCH)FileName;
    name.Length = (USHORT)FileNameLength;
    name.MaximumLength = name.Length;
    for (index = 0; index < g_RklFilter.RuleCount; ++index) {
        UNICODE_STRING rule;

        rule.Buffer = g_RklFilter.Rules[index].Name;
        rule.Length = (USHORT)g_RklFilter.Rules[index].NameLengthBytes;
        rule.MaximumLength = rule.Length;
        if (RtlEqualUnicodeString(&name, &rule, TRUE)) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN RklValidateDirectoryBuffer(
    _In_reads_bytes_(TotalBytes) const UCHAR* Buffer,
    _In_ ULONG TotalBytes,
    _In_ ULONG FileNameLengthOffset,
    _In_ ULONG FileNameOffset,
    _Out_ PULONG HiddenCount)
{
    ULONG readOffset = 0;
    ULONG hidden = 0;

    *HiddenCount = 0;
    for (;;) {
        const UCHAR* record;
        ULONG nextOffset;
        ULONG recordLength;
        ULONG nameLength;

        if (readOffset > TotalBytes || TotalBytes - readOffset < FileNameOffset) {
            return FALSE;
        }
        record = Buffer + readOffset;
        nextOffset = *(const ULONG*)record;
        recordLength = nextOffset != 0 ? nextOffset : TotalBytes - readOffset;

        if (recordLength < FileNameOffset ||
            recordLength > TotalBytes - readOffset ||
            (nextOffset != 0 && (nextOffset % sizeof(ULONG)) != 0) ||
            FileNameLengthOffset + sizeof(ULONG) > recordLength) {
            return FALSE;
        }
        nameLength = *(const ULONG*)(record + FileNameLengthOffset);
        if (nameLength > recordLength - FileNameOffset ||
            (nameLength % sizeof(WCHAR)) != 0) {
            return FALSE;
        }
        if (RklNameShouldBeHidden(
            (const WCHAR*)(record + FileNameOffset), nameLength)) {
            ++hidden;
        }

        if (nextOffset == 0) {
            break;
        }
        readOffset += nextOffset;
    }

    *HiddenCount = hidden;
    return TRUE;
}

static ULONG RklCompactDirectoryBuffer(
    _Inout_updates_bytes_(TotalBytes) UCHAR* Buffer,
    _In_ ULONG TotalBytes,
    _In_ ULONG FileNameLengthOffset,
    _In_ ULONG FileNameOffset)
{
    ULONG readOffset = 0;
    ULONG writeOffset = 0;
    ULONG previousVisible = MAXULONG;

    for (;;) {
        UCHAR* record = Buffer + readOffset;
        ULONG nextOffset = *(ULONG*)record;
        ULONG recordLength = nextOffset != 0 ? nextOffset : TotalBytes - readOffset;
        ULONG nameLength = *(ULONG*)(record + FileNameLengthOffset);
        BOOLEAN hidden = RklNameShouldBeHidden(
            (const WCHAR*)(record + FileNameOffset), nameLength);

        if (!hidden) {
            if (writeOffset != readOffset) {
                RtlMoveMemory(Buffer + writeOffset, record, recordLength);
            }
            if (previousVisible != MAXULONG) {
                *(ULONG*)(Buffer + previousVisible) = writeOffset - previousVisible;
            }
            previousVisible = writeOffset;
            writeOffset += recordLength;
        }

        if (nextOffset == 0) {
            break;
        }
        readOffset += nextOffset;
    }

    if (previousVisible != MAXULONG) {
        *(ULONG*)(Buffer + previousVisible) = 0;
    }
    if (writeOffset < TotalBytes) {
        RtlZeroMemory(Buffer + writeOffset, TotalBytes - writeOffset);
    }
    return writeOffset;
}

static BOOLEAN RklIsSandboxDirectory(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS status;
    BOOLEAN matches = FALSE;

    UNREFERENCED_PARAMETER(FltObjects);
    status = FltGetFileNameInformation(
        Data,
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
        &nameInfo);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }
    matches = RtlEqualUnicodeString(
        &nameInfo->Name,
        &g_RklFilter.TargetDirectoryName,
        TRUE);
    FltReleaseFileNameInformation(nameInfo);
    return matches;
}

static PVOID RklGetDirectoryBuffer(_Inout_ PFLT_CALLBACK_DATA Data)
{
    PMDL mdl = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress;
    NTSTATUS status;

    if (mdl != NULL) {
        return MmGetSystemAddressForMdlSafe(
            mdl, NormalPagePriority | MdlMappingNoExecute);
    }

    if (FLT_IS_SYSTEM_BUFFER(Data) || FLT_IS_FASTIO_OPERATION(Data)) {
        return Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;
    }

    status = FltLockUserBuffer(Data);
    if (!NT_SUCCESS(status)) {
        return NULL;
    }
    mdl = Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress;
    if (mdl == NULL) {
        return NULL;
    }
    return MmGetSystemAddressForMdlSafe(
        mdl, NormalPagePriority | MdlMappingNoExecute);
}

static FLT_POSTOP_CALLBACK_STATUS RklPostDirectoryControlSafe(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    FILE_INFORMATION_CLASS informationClass;
    ULONG fileNameLengthOffset;
    ULONG fileNameOffset;
    ULONG totalBytes;
    ULONG hiddenCount = 0;
    ULONG compactedBytes = 0;
    UCHAR* buffer;
    BOOLEAN modified = FALSE;

    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);

    if (!NT_SUCCESS(Data->IoStatus.Status) ||
        Data->IoStatus.Information == 0 ||
        InterlockedCompareExchange(&g_RklFilter.Enabled, 0, 0) == 0) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    informationClass =
        Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass;
    if (!RklGetDirectoryLayout(
        informationClass, &fileNameLengthOffset, &fileNameOffset)) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    if (!RklIsSandboxDirectory(Data, FltObjects)) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    if (!RklMarkerExists()) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    InterlockedIncrement64(&g_RklFilter.TargetDirectoryQueries);
    totalBytes = (ULONG)min(
        Data->IoStatus.Information,
        Data->Iopb->Parameters.DirectoryControl.QueryDirectory.Length);
    buffer = (UCHAR*)RklGetDirectoryBuffer(Data);
    if (buffer == NULL || totalBytes == 0) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&g_RklFilter.RuleLock);
    __try {
        if (g_RklFilter.RuleCount != 0 &&
            RklValidateDirectoryBuffer(
                buffer,
                totalBytes,
                fileNameLengthOffset,
                fileNameOffset,
                &hiddenCount) &&
            hiddenCount != 0) {
            compactedBytes = RklCompactDirectoryBuffer(
                buffer,
                totalBytes,
                fileNameLengthOffset,
                fileNameOffset);
            modified = TRUE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        modified = FALSE;
    }
    ExReleasePushLockShared(&g_RklFilter.RuleLock);
    KeLeaveCriticalRegion();

    if (!modified) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    InterlockedAdd64(&g_RklFilter.HiddenEntries, hiddenCount);
    Data->IoStatus.Information = compactedBytes;
    if (compactedBytes == 0) {
        Data->IoStatus.Status = STATUS_NO_MORE_FILES;
    }
    FltSetCallbackDataDirty(Data);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

static FLT_POSTOP_CALLBACK_STATUS RklPostDirectoryControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    FLT_POSTOP_CALLBACK_STATUS result;

    if ((Flags & FLTFL_POST_OPERATION_DRAINING) != 0 ||
        !NT_SUCCESS(Data->IoStatus.Status) ||
        Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL ||
        FLT_IS_SYSTEM_BUFFER(Data) ||
        FLT_IS_FASTIO_OPERATION(Data)) {
        return RklPostDirectoryControlSafe(
            Data, FltObjects, CompletionContext, Flags);
    }

    if (FltDoCompletionProcessingWhenSafe(
        Data,
        FltObjects,
        CompletionContext,
        Flags,
        RklPostDirectoryControlSafe,
        &result)) {
        return result;
    }
    return FLT_POSTOP_FINISHED_PROCESSING;
}

static FLT_PREOP_CALLBACK_STATUS RklPreDirectoryControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    InterlockedIncrement64(&g_RklFilter.DirectoryQueries);
    if (Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY ||
        InterlockedCompareExchange(&g_RklFilter.Enabled, 0, 0) == 0) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

static NTSTATUS RklInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);

    if (VolumeFilesystemType != FLT_FSTYPE_NTFS) {
        return STATUS_FLT_DO_NOT_ATTACH;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS RklFilterUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    InterlockedExchange(&g_RklFilter.Enabled, 0);
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&g_RklFilter.RuleLock);
    g_RklFilter.RuleCount = 0;
    RtlZeroMemory(g_RklFilter.Rules, sizeof(g_RklFilter.Rules));
    ExReleasePushLockExclusive(&g_RklFilter.RuleLock);
    KeLeaveCriticalRegion();
    if (g_RklFilter.ServerPort != NULL) {
        FltCloseCommunicationPort(g_RklFilter.ServerPort);
        g_RklFilter.ServerPort = NULL;
    }
    if (g_RklFilter.ClientPort != NULL) {
        FltCloseClientPort(g_RklFilter.Filter, &g_RklFilter.ClientPort);
    }
    if (g_RklFilter.Filter != NULL) {
        FltUnregisterFilter(g_RklFilter.Filter);
        g_RklFilter.Filter = NULL;
    }
    return STATUS_SUCCESS;
}

static const FLT_OPERATION_REGISTRATION g_Operations[] = {
    {
        IRP_MJ_DIRECTORY_CONTROL,
        0,
        RklPreDirectoryControl,
        RklPostDirectoryControl
    },
    { IRP_MJ_OPERATION_END }
};

static const FLT_REGISTRATION g_FilterRegistration = {
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    0,
    NULL,
    g_Operations,
    RklFilterUnload,
    RklInstanceSetup,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING portName = RTL_CONSTANT_STRING(RKL_FILTER_PORT_NAME);
    OBJECT_ATTRIBUTES attributes;
    PSECURITY_DESCRIPTOR securityDescriptor = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);
    RtlZeroMemory(&g_RklFilter, sizeof(g_RklFilter));
    ExInitializePushLock(&g_RklFilter.RuleLock);

    status = FltRegisterFilter(
        DriverObject,
        &g_FilterRegistration,
        &g_RklFilter.Filter);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = FltBuildDefaultSecurityDescriptor(
        &securityDescriptor,
        FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(status)) {
        FltUnregisterFilter(g_RklFilter.Filter);
        g_RklFilter.Filter = NULL;
        return status;
    }

    InitializeObjectAttributes(
        &attributes,
        &portName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        securityDescriptor);
    status = FltCreateCommunicationPort(
        g_RklFilter.Filter,
        &g_RklFilter.ServerPort,
        &attributes,
        NULL,
        RklPortConnect,
        RklPortDisconnect,
        RklPortMessage,
        1);
    FltFreeSecurityDescriptor(securityDescriptor);
    if (!NT_SUCCESS(status)) {
        FltUnregisterFilter(g_RklFilter.Filter);
        g_RklFilter.Filter = NULL;
        return status;
    }

    status = FltStartFiltering(g_RklFilter.Filter);
    if (!NT_SUCCESS(status)) {
        FltCloseCommunicationPort(g_RklFilter.ServerPort);
        g_RklFilter.ServerPort = NULL;
        FltUnregisterFilter(g_RklFilter.Filter);
        g_RklFilter.Filter = NULL;
        return status;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "RootkitLabFilter: v2.0 loaded; disabled with no rules\n");
    return STATUS_SUCCESS;
}
