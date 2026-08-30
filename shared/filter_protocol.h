#pragma once

#ifdef _KERNEL_MODE
#include <fltKernel.h>
#else
#include <Windows.h>
#endif

#define RKL_FILTER_PORT_NAME L"\\RootkitLabFilterPort"
#define RKL_FILTER_MAGIC 0x464C4B52u /* "RKLF" */
#define RKL_FILTER_ABI_VERSION 0x00020000u

#define RKL_FILTER_SANDBOX_PATH L"C:\\RootkitLabSandbox"
#define RKL_FILTER_MARKER_PATH L"C:\\RootkitLabSandbox\\.rootkitlab-lab"
#define RKL_FILTER_MARKER_NAME L".rootkitlab-lab"

#define RKL_FILTER_MAX_RULES 16u
#define RKL_FILTER_MAX_NAME_CHARS 256u

typedef enum _RKL_FILTER_COMMAND {
    RklFilterCommandStatus = 1,
    RklFilterCommandEnable = 2,
    RklFilterCommandDisable = 3,
    RklFilterCommandClearCounters = 4,
    RklFilterCommandReplaceRules = 5
} RKL_FILTER_COMMAND;

typedef struct _RKL_FILTER_RULE {
    ULONG NameLengthBytes;
    WCHAR Name[RKL_FILTER_MAX_NAME_CHARS];
} RKL_FILTER_RULE, *PRKL_FILTER_RULE;

typedef struct _RKL_FILTER_REQUEST {
    ULONG Magic;
    ULONG StructSize;
    ULONG AbiVersion;
    ULONG Command;
    ULONG RuleCount;
    ULONG Reserved0;
    RKL_FILTER_RULE Rules[RKL_FILTER_MAX_RULES];
} RKL_FILTER_REQUEST, *PRKL_FILTER_REQUEST;

typedef struct _RKL_FILTER_RESPONSE {
    ULONG Magic;
    ULONG StructSize;
    ULONG AbiVersion;
    LONG CommandStatus;
    ULONG Enabled;
    ULONG MarkerPresent;
    ULONG RuleCount;
    ULONG RuleRevision;
    ULONGLONG DirectoryQueries;
    ULONGLONG TargetDirectoryQueries;
    ULONGLONG HiddenEntries;
    ULONGLONG EnableTransitions;
    ULONGLONG DisableTransitions;
    ULONGLONG RuleUpdates;
    ULONGLONG RejectedCommands;
    RKL_FILTER_RULE Rules[RKL_FILTER_MAX_RULES];
} RKL_FILTER_RESPONSE, *PRKL_FILTER_RESPONSE;
