#include "driver_client.h"

#include <fltuser.h>
#include <cwchar>

DriverClient::DriverClient() noexcept : port_(INVALID_HANDLE_VALUE)
{
}

DriverClient::~DriverClient()
{
    Disconnect();
}

bool DriverClient::Connect(std::wstring& error)
{
    HRESULT result;

    Disconnect();
    result = FilterConnectCommunicationPort(
        RKL_FILTER_PORT_NAME,
        FLT_PORT_FLAG_SYNC_HANDLE,
        nullptr,
        0,
        nullptr,
        &port_);
    if (FAILED(result)) {
        port_ = INVALID_HANDLE_VALUE;
        error = FormatWindowsError(result);
        return false;
    }
    error.clear();
    return true;
}

void DriverClient::Disconnect() noexcept
{
    if (port_ != INVALID_HANDLE_VALUE) {
        CloseHandle(port_);
        port_ = INVALID_HANDLE_VALUE;
    }
}

bool DriverClient::IsConnected() const noexcept
{
    return port_ != INVALID_HANDLE_VALUE;
}

bool DriverClient::Status(RKL_FILTER_RESPONSE& response, std::wstring& error)
{
    return Send(RklFilterCommandStatus, nullptr, response, error);
}

bool DriverClient::Enable(RKL_FILTER_RESPONSE& response, std::wstring& error)
{
    return Send(RklFilterCommandEnable, nullptr, response, error);
}

bool DriverClient::Disable(RKL_FILTER_RESPONSE& response, std::wstring& error)
{
    return Send(RklFilterCommandDisable, nullptr, response, error);
}

bool DriverClient::ClearCounters(
    RKL_FILTER_RESPONSE& response,
    std::wstring& error)
{
    return Send(RklFilterCommandClearCounters, nullptr, response, error);
}

bool DriverClient::ReplaceRules(
    const std::vector<std::wstring>& names,
    RKL_FILTER_RESPONSE& response,
    std::wstring& error)
{
    return Send(RklFilterCommandReplaceRules, &names, response, error);
}

bool DriverClient::Send(
    RKL_FILTER_COMMAND command,
    const std::vector<std::wstring>* names,
    RKL_FILTER_RESPONSE& response,
    std::wstring& error)
{
    RKL_FILTER_REQUEST request{};
    DWORD returnedBytes = 0;
    HRESULT result;

    if (!IsConnected()) {
        error = L"No existe una conexión con el minifilter.";
        return false;
    }

    request.Magic = RKL_FILTER_MAGIC;
    request.StructSize = sizeof(request);
    request.AbiVersion = RKL_FILTER_ABI_VERSION;
    request.Command = static_cast<ULONG>(command);

    if (names != nullptr) {
        if (names->size() > RKL_FILTER_MAX_RULES) {
            error = L"La selección supera el máximo de reglas permitido.";
            return false;
        }
        request.RuleCount = static_cast<ULONG>(names->size());
        for (size_t index = 0; index < names->size(); ++index) {
            const std::wstring& name = (*names)[index];
            if (name.empty() || name.size() >= RKL_FILTER_MAX_NAME_CHARS) {
                error = L"Uno de los nombres no tiene una longitud válida.";
                return false;
            }
            request.Rules[index].NameLengthBytes =
                static_cast<ULONG>(name.size() * sizeof(wchar_t));
            if (wcscpy_s(
                    request.Rules[index].Name,
                    RKL_FILTER_MAX_NAME_CHARS,
                    name.c_str()) != 0) {
                error = L"No se pudo preparar una de las reglas.";
                return false;
            }
        }
    }

    ZeroMemory(&response, sizeof(response));
    result = FilterSendMessage(
        port_,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &returnedBytes);
    if (FAILED(result)) {
        error = FormatWindowsError(result);
        Disconnect();
        return false;
    }
    if (returnedBytes != sizeof(response) ||
        response.Magic != RKL_FILTER_MAGIC ||
        response.StructSize != sizeof(response) ||
        response.AbiVersion != RKL_FILTER_ABI_VERSION) {
        error = L"El minifilter devolvió una respuesta incompatible.";
        return false;
    }

    if (response.CommandStatus < 0) {
        wchar_t buffer[96]{};
        swprintf_s(
            buffer,
            L"El driver rechazó la orden (NTSTATUS 0x%08lX).",
            static_cast<ULONG>(response.CommandStatus));
        error = buffer;
        return false;
    }

    error.clear();
    return true;
}

std::wstring FormatWindowsError(HRESULT result)
{
    wchar_t* message = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD code = HRESULT_FACILITY(result) == FACILITY_WIN32
        ? HRESULT_CODE(result)
        : static_cast<DWORD>(result);
    std::wstring output;

    if (FormatMessageW(
            flags,
            nullptr,
            code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<wchar_t*>(&message),
            0,
            nullptr) != 0 &&
        message != nullptr) {
        output = message;
        LocalFree(message);
        while (!output.empty() &&
               (output.back() == L'\r' || output.back() == L'\n')) {
            output.pop_back();
        }
    } else {
        wchar_t fallback[64]{};
        swprintf_s(fallback, L"HRESULT 0x%08lX", static_cast<ULONG>(result));
        output = fallback;
    }
    return output;
}
