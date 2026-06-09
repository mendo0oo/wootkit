#include <windows.h>
#include <iostream>
#include <vector>
#include "../shared.h"

static const wchar_t* event_type(unsigned long t)
{
    switch (t) {
    case WootkitEventProcessCreate: return L"process_create";
    case WootkitEventProcessExit: return L"process_exit";
    case WootkitEventImageLoad: return L"image_load";
    case WootkitEventRegistrySetValue: return L"registry_set_value";
    case WootkitEventRegistryCreateKey: return L"registry_create_key";
    default: return L"unknown";
    }
}

static const wchar_t* severity_name(unsigned long severity)
{
    switch (severity) {
    case WOOTKIT_SEVERITY_LOW: return L"low";
    case WOOTKIT_SEVERITY_MEDIUM: return L"medium";
    case WOOTKIT_SEVERITY_HIGH: return L"high";
    case WOOTKIT_SEVERITY_INFO:
    default: return L"info";
    }
}

static const wchar_t* rule_name(unsigned long rule)
{
    switch (rule) {
    case WOOTKIT_RULE_USER_WRITABLE_IMAGE: return L"user_writable_image_path";
    case WOOTKIT_RULE_AUTORUN_VALUE: return L"autorun_registry_value";
    case WOOTKIT_RULE_SERVICE_IMAGE_PATH: return L"service_image_path_modified";
    case WOOTKIT_RULE_IFEO_DEBUGGER: return L"ifeo_debugger_modified";
    case WOOTKIT_RULE_WINLOGON_VALUE: return L"winlogon_value_modified";
    case WOOTKIT_RULE_APPINIT_DLLS: return L"appinit_dlls_modified";
    case WOOTKIT_RULE_NONE:
    default: return L"none";
    }
}

static std::wstring json_escape(const wchar_t* input)
{
    std::wstring out;
    if (input == nullptr) {
        return out;
    }

    for (const wchar_t* p = input; *p != L'\0'; ++p) {
        switch (*p) {
        case L'\\': out += L"\\\\"; break;
        case L'"': out += L"\\\""; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out += *p; break;
        }
    }

    return out;
}

int wmain()
{
    HANDLE h = CreateFileW(WOOTKIT_USER_DEVICE, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to open " << WOOTKIT_USER_DEVICE << L". Is WootkitSensor loaded?\n";
        return 1;
    }

    std::vector<WOOTKIT_EVENT> events(256);
    DWORD returned = 0;
    if (!DeviceIoControl(h, IOCTL_WOOTKIT_READ_EVENTS, nullptr, 0,
        events.data(), (DWORD)(events.size() * sizeof(WOOTKIT_EVENT)), &returned, nullptr)) {
        std::wcerr << L"DeviceIoControl failed: " << GetLastError() << L"\n";
        CloseHandle(h);
        return 1;
    }

    DWORD count = returned / sizeof(WOOTKIT_EVENT);
    for (DWORD i = 0; i < count; i++) {
        const auto& e = events[i];
        std::wcout << L"{"
            << L"\"type\":\"" << event_type(e.Type) << L"\","
            << L"\"pid\":" << e.ProcessId << L","
            << L"\"ppid\":" << e.ParentProcessId << L","
            << L"\"tid\":" << e.ThreadId << L","
            << L"\"severity\":\"" << severity_name(e.Severity) << L"\","
            << L"\"suspicious\":" << ((e.Flags & WOOTKIT_FLAG_SUSPICIOUS) ? L"true" : L"false") << L","
            << L"\"kernel_image\":" << ((e.Flags & WOOTKIT_FLAG_KERNEL_IMAGE) ? L"true" : L"false") << L","
            << L"\"rule\":\"" << rule_name(e.RuleId) << L"\","
            << L"\"image\":\"" << json_escape(e.ImagePath) << L"\","
            << L"\"registry\":\"" << json_escape(e.RegistryPath) << L"\""
            << L"}\n";
    }

    CloseHandle(h);
    return 0;
}
