#include <ntifs.h>
#include <ntddk.h>
#include "../shared.h"

#define WOOTKIT_TAG 'tkoW'
#define WOOTKIT_RING_SIZE 1024
#define WOOTKIT_ALTITUDE L"385720.42"

typedef struct _WOOTKIT_STATE {
    PDEVICE_OBJECT DeviceObject;
    UNICODE_STRING SymbolicLink;
    FAST_MUTEX Lock;
    WOOTKIT_EVENT Events[WOOTKIT_RING_SIZE];
    ULONG WriteIndex;
    ULONG Count;
    LARGE_INTEGER RegistryCookie;
    BOOLEAN ProcessCallbackSet;
    BOOLEAN ImageCallbackSet;
    BOOLEAN RegistryCallbackSet;
} WOOTKIT_STATE;

static WOOTKIT_STATE g_State;

static VOID WootkitAddEvent(_In_ const WOOTKIT_EVENT* Event)
{
    ExAcquireFastMutex(&g_State.Lock);
    g_State.Events[g_State.WriteIndex] = *Event;
    g_State.WriteIndex = (g_State.WriteIndex + 1) % WOOTKIT_RING_SIZE;
    if (g_State.Count < WOOTKIT_RING_SIZE) {
        g_State.Count++;
    }
    ExReleaseFastMutex(&g_State.Lock);
}

static VOID WootkitCopyUnicode(_Out_writes_(DestChars) WCHAR* Dest, _In_ ULONG DestChars, _In_opt_ PCUNICODE_STRING Src)
{
    ULONG chars;
    if (DestChars == 0) {
        return;
    }
    RtlZeroMemory(Dest, DestChars * sizeof(WCHAR));
    if (Src == NULL || Src->Buffer == NULL || Src->Length == 0) {
        return;
    }
    chars = min((ULONG)(Src->Length / sizeof(WCHAR)), DestChars - 1);
    RtlCopyMemory(Dest, Src->Buffer, chars * sizeof(WCHAR));
    Dest[chars] = L'\0';
}

static ULONG WootkitStringLength(_In_reads_z_(MaxChars) const WCHAR* Text, _In_ ULONG MaxChars)
{
    ULONG len;

    for (len = 0; len < MaxChars && Text[len] != L'\0'; len++) {
    }

    return len;
}

static ULONG WootkitLiteralLength(_In_z_ PCWSTR Text)
{
    ULONG len;

    for (len = 0; Text[len] != L'\0'; len++) {
    }

    return len;
}

static VOID WootkitAppendUnicode(_Inout_updates_(DestChars) WCHAR* Dest, _In_ ULONG DestChars, _In_opt_ PCUNICODE_STRING Src)
{
    ULONG used;
    ULONG chars;

    if (DestChars == 0 || Src == NULL || Src->Buffer == NULL || Src->Length == 0) {
        return;
    }

    used = WootkitStringLength(Dest, DestChars);
    if (used >= DestChars - 1) {
        return;
    }

    chars = min((ULONG)(Src->Length / sizeof(WCHAR)), DestChars - used - 1);
    RtlCopyMemory(Dest + used, Src->Buffer, chars * sizeof(WCHAR));
    Dest[used + chars] = L'\0';
}

static VOID WootkitAppendLiteral(_Inout_updates_(DestChars) WCHAR* Dest, _In_ ULONG DestChars, _In_z_ PCWSTR Text)
{
    UNICODE_STRING text;
    RtlInitUnicodeString(&text, Text);
    WootkitAppendUnicode(Dest, DestChars, &text);
}

static BOOLEAN WootkitContainsLiteral(_In_reads_(TextChars) const WCHAR* Text, _In_ ULONG TextChars, _In_z_ PCWSTR Needle)
{
    ULONG needleChars = WootkitLiteralLength(Needle);
    ULONG i;
    ULONG j;

    if (needleChars == 0 || TextChars < needleChars) {
        return FALSE;
    }

    for (i = 0; i <= TextChars - needleChars; i++) {
        for (j = 0; j < needleChars; j++) {
            WCHAR a = RtlUpcaseUnicodeChar(Text[i + j]);
            WCHAR b = RtlUpcaseUnicodeChar(Needle[j]);
            if (a != b) {
                break;
            }
        }
        if (j == needleChars) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN WootkitPathContains(_In_reads_z_(MaxChars) const WCHAR* Path, _In_ ULONG MaxChars, _In_z_ PCWSTR Needle)
{
    ULONG len = WootkitStringLength(Path, MaxChars);
    return WootkitContainsLiteral(Path, len, Needle);
}

static VOID WootkitMarkSuspicious(_Inout_ WOOTKIT_EVENT* Event, _In_ ULONG Severity, _In_ ULONG RuleId)
{
    Event->Flags |= WOOTKIT_FLAG_SUSPICIOUS;
    Event->Severity = max(Event->Severity, Severity);
    if (Event->RuleId == WOOTKIT_RULE_NONE) {
        Event->RuleId = RuleId;
    }
}

static VOID WootkitClassifyImagePath(_Inout_ WOOTKIT_EVENT* Event)
{
    if (WootkitPathContains(Event->ImagePath, WOOTKIT_MAX_IMAGE_PATH_CHARS, L"\\USERS\\") ||
        WootkitPathContains(Event->ImagePath, WOOTKIT_MAX_IMAGE_PATH_CHARS, L"\\APPDATA\\") ||
        WootkitPathContains(Event->ImagePath, WOOTKIT_MAX_IMAGE_PATH_CHARS, L"\\TEMP\\")) {
        WootkitMarkSuspicious(Event, WOOTKIT_SEVERITY_LOW, WOOTKIT_RULE_USER_WRITABLE_IMAGE);
    }
}

static VOID WootkitClassifyRegistryPath(_Inout_ WOOTKIT_EVENT* Event)
{
    BOOLEAN hasServices = WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\SYSTEM\\CURRENTCONTROLSET\\SERVICES\\");
    BOOLEAN hasRun = WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\CURRENTVERSION\\RUN");
    BOOLEAN hasIfeo = WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\IMAGE FILE EXECUTION OPTIONS\\");
    BOOLEAN hasWinlogon = WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\WINLOGON\\");
    BOOLEAN hasAppInit = WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\WINDOWS NT\\CURRENTVERSION\\WINDOWS\\APPINIT_DLLS");

    if (hasServices && WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\IMAGEPATH")) {
        WootkitMarkSuspicious(Event, WOOTKIT_SEVERITY_MEDIUM, WOOTKIT_RULE_SERVICE_IMAGE_PATH);
    } else if (hasIfeo && WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\DEBUGGER")) {
        WootkitMarkSuspicious(Event, WOOTKIT_SEVERITY_HIGH, WOOTKIT_RULE_IFEO_DEBUGGER);
    } else if (hasWinlogon &&
        (WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\SHELL") ||
            WootkitPathContains(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\USERINIT"))) {
        WootkitMarkSuspicious(Event, WOOTKIT_SEVERITY_HIGH, WOOTKIT_RULE_WINLOGON_VALUE);
    } else if (hasAppInit) {
        WootkitMarkSuspicious(Event, WOOTKIT_SEVERITY_HIGH, WOOTKIT_RULE_APPINIT_DLLS);
    } else if (hasRun) {
        WootkitMarkSuspicious(Event, WOOTKIT_SEVERITY_MEDIUM, WOOTKIT_RULE_AUTORUN_VALUE);
    }
}

static VOID WootkitCopyRegistrySetValuePath(_Inout_ WOOTKIT_EVENT* Event, _In_ PREG_SET_VALUE_KEY_INFORMATION Info)
{
    PCUNICODE_STRING keyName = NULL;
    NTSTATUS status;

    RtlZeroMemory(Event->RegistryPath, sizeof(Event->RegistryPath));
    status = CmCallbackGetKeyObjectIDEx(&g_State.RegistryCookie, Info->Object, NULL, &keyName, 0);
    if (NT_SUCCESS(status) && keyName != NULL) {
        WootkitCopyUnicode(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, keyName);
        if (Info->ValueName != NULL && Info->ValueName->Length > 0) {
            WootkitAppendLiteral(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, L"\\");
            WootkitAppendUnicode(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, Info->ValueName);
        }
        CmCallbackReleaseKeyObjectIDEx(keyName);
    } else {
        WootkitCopyUnicode(Event->RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, Info->ValueName);
    }
}

static VOID WootkitStamp(_Inout_ WOOTKIT_EVENT* Event)
{
    LARGE_INTEGER now;
    KeQuerySystemTimePrecise(&now);
    Event->Time100ns = (ULONGLONG)now.QuadPart;
    Event->ThreadId = HandleToULong(PsGetCurrentThreadId());
}

static VOID WootkitProcessNotify(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    WOOTKIT_EVENT event;
    UNREFERENCED_PARAMETER(Process);

    RtlZeroMemory(&event, sizeof(event));
    WootkitStamp(&event);
    event.ProcessId = HandleToULong(ProcessId);

    if (CreateInfo != NULL) {
        event.Type = WootkitEventProcessCreate;
        event.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
        WootkitCopyUnicode(event.ImagePath, WOOTKIT_MAX_IMAGE_PATH_CHARS, CreateInfo->ImageFileName);
        WootkitClassifyImagePath(&event);
    } else {
        event.Type = WootkitEventProcessExit;
    }

    WootkitAddEvent(&event);
}

static VOID WootkitImageLoadNotify(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo)
{
    WOOTKIT_EVENT event;

    RtlZeroMemory(&event, sizeof(event));
    WootkitStamp(&event);
    event.Type = WootkitEventImageLoad;
    event.ProcessId = HandleToULong(ProcessId);
    if (ImageInfo != NULL) {
        ULONG_PTR imageBase = (ULONG_PTR)ImageInfo->ImageBase;
        event.ImageBaseLow = (ULONG)(imageBase & 0xffffffff);
        event.ImageBaseHigh = (ULONG)(imageBase >> 32);
        event.ImageSize = (ULONG)min(ImageInfo->ImageSize, (SIZE_T)MAXULONG);
        if (ImageInfo->SystemModeImage) {
            event.Flags |= WOOTKIT_FLAG_KERNEL_IMAGE;
        }
    }
    WootkitCopyUnicode(event.ImagePath, WOOTKIT_MAX_IMAGE_PATH_CHARS, FullImageName);
    WootkitClassifyImagePath(&event);
    WootkitAddEvent(&event);
}

static NTSTATUS WootkitRegistryCallback(
    _In_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    REG_NOTIFY_CLASS op = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;
    WOOTKIT_EVENT event;
    UNREFERENCED_PARAMETER(CallbackContext);

    RtlZeroMemory(&event, sizeof(event));
    WootkitStamp(&event);
    event.ProcessId = HandleToULong(PsGetCurrentProcessId());

    if (op == RegNtPreSetValueKey && Argument2 != NULL) {
        PREG_SET_VALUE_KEY_INFORMATION info = (PREG_SET_VALUE_KEY_INFORMATION)Argument2;
        event.Type = WootkitEventRegistrySetValue;
        WootkitCopyRegistrySetValuePath(&event, info);
        WootkitClassifyRegistryPath(&event);
        WootkitAddEvent(&event);
    } else if (op == RegNtPreCreateKeyEx && Argument2 != NULL) {
        PREG_CREATE_KEY_INFORMATION info = (PREG_CREATE_KEY_INFORMATION)Argument2;
        event.Type = WootkitEventRegistryCreateKey;
        WootkitCopyUnicode(event.RegistryPath, WOOTKIT_MAX_REG_PATH_CHARS, info->CompleteName);
        WootkitClassifyRegistryPath(&event);
        WootkitAddEvent(&event);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS WootkitCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS WootkitDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG outLen;
    ULONG toCopy;

    UNREFERENCED_PARAMETER(DeviceObject);
    stack = IoGetCurrentIrpStackLocation(Irp);
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_WOOTKIT_READ_EVENTS) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    if (outLen < sizeof(WOOTKIT_EVENT)) {
        status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    ExAcquireFastMutex(&g_State.Lock);
    toCopy = min(g_State.Count, outLen / sizeof(WOOTKIT_EVENT));
    if (toCopy > 0) {
        ULONG start = (g_State.WriteIndex + WOOTKIT_RING_SIZE - g_State.Count) % WOOTKIT_RING_SIZE;
        PWOOTKIT_EVENT out = (PWOOTKIT_EVENT)Irp->AssociatedIrp.SystemBuffer;
        ULONG i;
        for (i = 0; i < toCopy; i++) {
            out[i] = g_State.Events[(start + i) % WOOTKIT_RING_SIZE];
        }
        g_State.Count -= toCopy;
    }
    ExReleaseFastMutex(&g_State.Lock);

    Irp->IoStatus.Information = toCopy * sizeof(WOOTKIT_EVENT);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID WootkitUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (g_State.RegistryCallbackSet) {
        CmUnRegisterCallback(g_State.RegistryCookie);
    }
    if (g_State.ImageCallbackSet) {
        PsRemoveLoadImageNotifyRoutine(WootkitImageLoadNotify);
    }
    if (g_State.ProcessCallbackSet) {
        PsSetCreateProcessNotifyRoutineEx(WootkitProcessNotify, TRUE);
    }
    if (g_State.SymbolicLink.Buffer != NULL) {
        IoDeleteSymbolicLink(&g_State.SymbolicLink);
    }
    if (g_State.DeviceObject != NULL) {
        IoDeleteDevice(g_State.DeviceObject);
    }
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    UNICODE_STRING deviceName;
    UNICODE_STRING altitude;

    UNREFERENCED_PARAMETER(RegistryPath);
    RtlZeroMemory(&g_State, sizeof(g_State));
    ExInitializeFastMutex(&g_State.Lock);

    RtlInitUnicodeString(&deviceName, WOOTKIT_DEVICE_NAME);
    RtlInitUnicodeString(&g_State.SymbolicLink, WOOTKIT_SYMBOLIC_NAME);

    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_State.DeviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = IoCreateSymbolicLink(&g_State.SymbolicLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_State.DeviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = WootkitCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = WootkitCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WootkitDeviceControl;
    DriverObject->DriverUnload = WootkitUnload;

    status = PsSetCreateProcessNotifyRoutineEx(WootkitProcessNotify, FALSE);
    if (NT_SUCCESS(status)) {
        g_State.ProcessCallbackSet = TRUE;
    }

    status = PsSetLoadImageNotifyRoutine(WootkitImageLoadNotify);
    if (NT_SUCCESS(status)) {
        g_State.ImageCallbackSet = TRUE;
    }

    RtlInitUnicodeString(&altitude, WOOTKIT_ALTITUDE);
    status = CmRegisterCallbackEx(WootkitRegistryCallback, &altitude, DriverObject, NULL, &g_State.RegistryCookie, NULL);
    if (NT_SUCCESS(status)) {
        g_State.RegistryCallbackSet = TRUE;
    }

    return STATUS_SUCCESS;
}
