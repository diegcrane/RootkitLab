#pragma once

#include <Windows.h>
#include <string>
#include <vector>

#include "../shared/filter_protocol.h"

class DriverClient {
public:
    DriverClient() noexcept;
    ~DriverClient();

    DriverClient(const DriverClient&) = delete;
    DriverClient& operator=(const DriverClient&) = delete;

    bool Connect(std::wstring& error);
    void Disconnect() noexcept;
    bool IsConnected() const noexcept;

    bool Status(RKL_FILTER_RESPONSE& response, std::wstring& error);
    bool Enable(RKL_FILTER_RESPONSE& response, std::wstring& error);
    bool Disable(RKL_FILTER_RESPONSE& response, std::wstring& error);
    bool ClearCounters(RKL_FILTER_RESPONSE& response, std::wstring& error);
    bool ReplaceRules(
        const std::vector<std::wstring>& names,
        RKL_FILTER_RESPONSE& response,
        std::wstring& error);

private:
    bool Send(
        RKL_FILTER_COMMAND command,
        const std::vector<std::wstring>* names,
        RKL_FILTER_RESPONSE& response,
        std::wstring& error);

    HANDLE port_;
};

std::wstring FormatWindowsError(HRESULT result);
