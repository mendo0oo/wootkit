#pragma once

#define WOOTKIT_DEVICE_NAME      L"\\Device\\WootkitSensor"
#define WOOTKIT_SYMBOLIC_NAME    L"\\DosDevices\\WootkitSensor"
#define WOOTKIT_USER_DEVICE      L"\\\\.\\WootkitSensor"

#define WOOTKIT_IOCTL_INDEX 0x800
#define IOCTL_WOOTKIT_READ_EVENTS CTL_CODE(FILE_DEVICE_UNKNOWN, WOOTKIT_IOCTL_INDEX, METHOD_BUFFERED, FILE_READ_DATA)

#define WOOTKIT_MAX_IMAGE_PATH_CHARS 260
#define WOOTKIT_MAX_REG_PATH_CHARS 260

#define WOOTKIT_SEVERITY_INFO 0
#define WOOTKIT_SEVERITY_LOW 1
#define WOOTKIT_SEVERITY_MEDIUM 2
#define WOOTKIT_SEVERITY_HIGH 3

#define WOOTKIT_FLAG_SUSPICIOUS 0x00000001
#define WOOTKIT_FLAG_KERNEL_IMAGE 0x00000002

#define WOOTKIT_RULE_NONE 0
#define WOOTKIT_RULE_USER_WRITABLE_IMAGE 1
#define WOOTKIT_RULE_AUTORUN_VALUE 2
#define WOOTKIT_RULE_SERVICE_IMAGE_PATH 3
#define WOOTKIT_RULE_IFEO_DEBUGGER 4
#define WOOTKIT_RULE_WINLOGON_VALUE 5
#define WOOTKIT_RULE_APPINIT_DLLS 6

typedef enum _WOOTKIT_EVENT_TYPE {
    WootkitEventProcessCreate = 1,
    WootkitEventProcessExit = 2,
    WootkitEventImageLoad = 3,
    WootkitEventRegistrySetValue = 4,
    WootkitEventRegistryCreateKey = 5
} WOOTKIT_EVENT_TYPE;

typedef struct _WOOTKIT_EVENT {
    unsigned long Type;
    unsigned long ProcessId;
    unsigned long ParentProcessId;
    unsigned long ThreadId;
    unsigned long long Time100ns;
    unsigned long ImageBaseLow;
    unsigned long ImageBaseHigh;
    unsigned long ImageSize;
    unsigned long Severity;
    unsigned long Flags;
    unsigned long RuleId;
    wchar_t ImagePath[WOOTKIT_MAX_IMAGE_PATH_CHARS];
    wchar_t RegistryPath[WOOTKIT_MAX_REG_PATH_CHARS];
} WOOTKIT_EVENT;

typedef WOOTKIT_EVENT* PWOOTKIT_EVENT;
