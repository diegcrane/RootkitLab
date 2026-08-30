#include <Windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <winsvc.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "crossview.h"
#include "driver_client.h"

namespace {

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gDeviceContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gRenderTarget = nullptr;
bool gSwapChainOccluded = false;
UINT gResizeWidth = 0;
UINT gResizeHeight = 0;

constexpr wchar_t kServiceName[] = L"RootkitLabFilter";

struct ServiceSnapshot {
    bool found = false;
    bool running = false;
    DWORD startType = SERVICE_DISABLED;
    DWORD error = ERROR_SUCCESS;
};

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (length <= 0) {
        return "<error de texto>";
    }
    std::string output(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        length,
        nullptr,
        nullptr);
    return output;
}

std::wstring HexStatus(LONG value)
{
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08lX", static_cast<ULONG>(value));
    return buffer;
}

ServiceSnapshot QueryService()
{
    ServiceSnapshot snapshot;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        snapshot.error = GetLastError();
        return snapshot;
    }
    SC_HANDLE service = OpenServiceW(
        manager,
        kServiceName,
        SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (service == nullptr) {
        snapshot.error = GetLastError();
        CloseServiceHandle(manager);
        return snapshot;
    }

    snapshot.found = true;
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<BYTE*>(&status),
            sizeof(status),
            &bytesNeeded)) {
        snapshot.running = status.dwCurrentState == SERVICE_RUNNING;
    } else {
        snapshot.error = GetLastError();
    }

    SetLastError(ERROR_SUCCESS);
    const BOOL initialQuery =
        QueryServiceConfigW(service, nullptr, 0, &bytesNeeded);
    const DWORD configurationError = GetLastError();
    if (!initialQuery &&
        configurationError == ERROR_INSUFFICIENT_BUFFER &&
        bytesNeeded != 0) {
        std::vector<BYTE> buffer(bytesNeeded);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (QueryServiceConfigW(service, config, bytesNeeded, &bytesNeeded)) {
            snapshot.startType = config->dwStartType;
        } else if (snapshot.error == ERROR_SUCCESS) {
            snapshot.error = GetLastError();
        }
    } else if (!initialQuery &&
        configurationError != ERROR_SUCCESS &&
        snapshot.error == ERROR_SUCCESS) {
        snapshot.error = configurationError;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return snapshot;
}

const char* StartTypeLabel(DWORD startType)
{
    switch (startType) {
    case SERVICE_BOOT_START: return "BOOT_START";
    case SERVICE_SYSTEM_START: return "SYSTEM_START";
    case SERVICE_AUTO_START: return "AUTO_START";
    case SERVICE_DEMAND_START: return "DEMAND_START";
    case SERVICE_DISABLED: return "DISABLED";
    default: return "DESCONOCIDO";
    }
}

std::string JsonEscape(const std::wstring& value)
{
    const std::string utf8 = Utf8(value);
    std::string output;
    output.reserve(utf8.size() + 8);
    for (const unsigned char character : utf8) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20u) {
                char encoded[8]{};
                sprintf_s(encoded, "\\u%04X", character);
                output += encoded;
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return output;
}

void AppendJsonNames(
    std::ostringstream& stream,
    const std::vector<std::wstring>& names)
{
    stream << '[';
    for (size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        stream << '"' << JsonEscape(names[index]) << '"';
    }
    stream << ']';
}

std::string DriverResponseJson(const RKL_FILTER_RESPONSE& response)
{
    std::ostringstream stream;
    stream << "{\n"
           << "  \"component\": \"RootkitLabFilter\",\n"
           << "  \"abi_version\": \"0x00020000\",\n"
           << "  \"command_status\": \""
           << JsonEscape(HexStatus(response.CommandStatus)) << "\",\n"
           << "  \"enabled\": " << (response.Enabled != 0 ? "true" : "false") << ",\n"
           << "  \"marker_present\": "
           << (response.MarkerPresent != 0 ? "true" : "false") << ",\n"
           << "  \"rule_revision\": " << response.RuleRevision << ",\n"
           << "  \"rules\": [";
    for (ULONG index = 0;
         index < response.RuleCount && index < RKL_FILTER_MAX_RULES;
         ++index) {
        if (index != 0) {
            stream << ',';
        }
        const auto& rule = response.Rules[index];
        const size_t characters = rule.NameLengthBytes / sizeof(wchar_t);
        stream << '"' << JsonEscape(std::wstring(rule.Name, characters)) << '"';
    }
    stream << "],\n"
           << "  \"counters\": {"
           << "\"directory_queries\":" << response.DirectoryQueries << ','
           << "\"target_directory_queries\":" << response.TargetDirectoryQueries << ','
           << "\"hidden_entries\":" << response.HiddenEntries << ','
           << "\"enable_transitions\":" << response.EnableTransitions << ','
           << "\"disable_transitions\":" << response.DisableTransitions << ','
           << "\"rule_updates\":" << response.RuleUpdates << ','
           << "\"rejected_commands\":" << response.RejectedCommands
           << "}\n}\n";
    return stream.str();
}

std::string CrossViewJson(const CrossViewSnapshot& snapshot)
{
    std::ostringstream stream;
    stream << "{\n"
           << "  \"scope\": \"C:\\\\RootkitLabSandbox\",\n"
           << "  \"sources_independent\": true,\n"
           << "  \"win32_enumeration_ok\": "
           << (snapshot.win32Ok ? "true" : "false") << ",\n"
           << "  \"mft_enumeration_ok\": "
           << (snapshot.mftOk ? "true" : "false") << ",\n"
           << "  \"direct_open_target\": \""
           << JsonEscape(snapshot.directOpenTarget) << "\",\n"
           << "  \"direct_open_ok\": "
           << (snapshot.directOpenOk ? "true" : "false") << ",\n"
           << "  \"directory_reference_number\": "
           << snapshot.directoryReference << ",\n"
           << "  \"win32_entries\": ";
    AppendJsonNames(stream, snapshot.win32Entries);
    stream << ",\n  \"mft_entries\": ";
    AppendJsonNames(stream, snapshot.mftEntries);
    stream << ",\n  \"missing_from_win32\": ";
    AppendJsonNames(stream, snapshot.missingFromWin32);
    stream << ",\n  \"missing_count\": " << snapshot.missingFromWin32.size() << ",\n"
           << "  \"classification\": \""
           << JsonEscape(snapshot.classification) << "\",\n"
           << "  \"errors\": {"
           << "\"win32\":" << snapshot.win32Error << ','
           << "\"mft\":" << snapshot.mftError << ','
           << "\"direct_open\":" << snapshot.directOpenError
           << "}\n}\n";
    return stream.str();
}

void WriteStandardHandle(DWORD identifier, const std::string& text)
{
    HANDLE handle = GetStdHandle(identifier);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(
        handle,
        text.data(),
        static_cast<DWORD>(text.size()),
        &written,
        nullptr);
}

int RunCommandLine(int argumentCount, wchar_t** arguments)
{
    if (argumentCount == 2 && _wcsicmp(arguments[1], L"--snapshot") == 0) {
        const CrossViewSnapshot snapshot = CaptureCrossView();
        WriteStandardHandle(STD_OUTPUT_HANDLE, CrossViewJson(snapshot));
        return snapshot.win32Ok && snapshot.mftOk ? 0 : 2;
    }

    DriverClient client;
    std::wstring error;
    if (!client.Connect(error)) {
        WriteStandardHandle(
            STD_ERROR_HANDLE,
            "{\"error\":\"" + JsonEscape(error) + "\"}\n");
        return 3;
    }

    RKL_FILTER_RESPONSE response{};
    bool ok = false;
    if (argumentCount == 2 && _wcsicmp(arguments[1], L"--status") == 0) {
        ok = client.Status(response, error);
    } else if (argumentCount == 2 && _wcsicmp(arguments[1], L"--enable") == 0) {
        ok = client.Enable(response, error);
    } else if (argumentCount == 2 && _wcsicmp(arguments[1], L"--disable") == 0) {
        ok = client.Disable(response, error);
    } else if (argumentCount == 2 && _wcsicmp(arguments[1], L"--clear") == 0) {
        ok = client.ClearCounters(response, error);
    } else if (argumentCount >= 2 && _wcsicmp(arguments[1], L"--set-rules") == 0) {
        std::vector<std::wstring> names;
        for (int index = 2; index < argumentCount; ++index) {
            names.emplace_back(arguments[index]);
        }
        ok = client.ReplaceRules(names, response, error);
    } else {
        WriteStandardHandle(
            STD_ERROR_HANDLE,
            "Uso: RootkitLab.exe --status|--set-rules [nombres...]|--enable|--disable|--clear|--snapshot\n");
        return 2;
    }

    if (!ok) {
        WriteStandardHandle(
            STD_ERROR_HANDLE,
            "{\"error\":\"" + JsonEscape(error) + "\"}\n");
        return 4;
    }
    WriteStandardHandle(STD_OUTPUT_HANDLE, DriverResponseJson(response));
    return 0;
}

bool SelectedContains(
    const std::vector<std::wstring>& selected,
    const std::wstring& name)
{
    return ContainsNameInsensitive(selected, name);
}

void RemoveSelected(
    std::vector<std::wstring>& selected,
    const std::wstring& name)
{
    selected.erase(
        std::remove_if(selected.begin(), selected.end(), [&](const auto& value) {
            return NamesEqualInsensitive(value, name);
        }),
        selected.end());
}

class ApplicationState {
public:
    void Initialize()
    {
        Connect();
        RefreshViews();
        service_ = QueryService();
        lastServicePoll_ = GetTickCount64();
    }

    void Tick()
    {
        const ULONGLONG now = GetTickCount64();
        if (client_.IsConnected() && now - lastStatusPoll_ >= 250) {
            RKL_FILTER_RESPONSE response{};
            std::wstring error;
            if (client_.Status(response, error)) {
                const ULONG previousRevision = status_.RuleRevision;
                status_ = response;
                if (previousRevision != response.RuleRevision) {
                    SyncSelectionFromDriver();
                }
                connected_ = true;
            } else {
                connected_ = false;
                lastError_ = error;
            }
            lastStatusPoll_ = now;
        }
        if (now - lastServicePoll_ >= 1000) {
            service_ = QueryService();
            lastServicePoll_ = now;
        }
        if (autoRefresh_ && now - lastViewPoll_ >= 2000) {
            RefreshViews();
        }
    }

    void Render()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin(
            "RootkitLab 2.0",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings);

        RenderHeader();
        ImGui::Spacing();
        if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, 420.0f);
            ImGui::TableSetupColumn("views", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            RenderControlPanel();
            ImGui::TableNextColumn();
            RenderViewsPanel();
            ImGui::EndTable();
        }
        ImGui::End();
    }

private:
    void AddLog(const std::wstring& message)
    {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t prefix[16]{};
        swprintf_s(prefix, L"%02u:%02u:%02u  ", time.wHour, time.wMinute, time.wSecond);
        log_.push_back(Utf8(std::wstring(prefix) + message));
        if (log_.size() > 80) {
            log_.erase(log_.begin());
        }
    }

    void Connect()
    {
        std::wstring error;
        connected_ = client_.Connect(error);
        if (!connected_) {
            lastError_ = error;
            AddLog(L"No se pudo conectar con el minifilter: " + error);
            return;
        }

        RKL_FILTER_RESPONSE response{};
        if (!client_.Status(response, error)) {
            connected_ = false;
            lastError_ = error;
            AddLog(L"La conexión no devolvió un estado válido: " + error);
            return;
        }
        status_ = response;
        SyncSelectionFromDriver();
        lastError_.clear();
        AddLog(L"Canal con el minifilter conectado.");
    }

    void SyncSelectionFromDriver()
    {
        selected_.clear();
        for (ULONG index = 0;
             index < status_.RuleCount && index < RKL_FILTER_MAX_RULES;
             ++index) {
            const auto& rule = status_.Rules[index];
            const size_t characters = rule.NameLengthBytes / sizeof(wchar_t);
            if (characters != 0 && characters < RKL_FILTER_MAX_NAME_CHARS) {
                selected_.emplace_back(rule.Name, characters);
            }
        }
    }

    void RefreshViews()
    {
        views_ = CaptureCrossView();
        lastViewPoll_ = GetTickCount64();
    }

    void SendSimple(RKL_FILTER_COMMAND command, const wchar_t* successMessage)
    {
        if (!client_.IsConnected()) {
            Connect();
            if (!client_.IsConnected()) {
                return;
            }
        }

        RKL_FILTER_RESPONSE response{};
        std::wstring error;
        bool ok = false;
        switch (command) {
        case RklFilterCommandEnable:
            ok = client_.Enable(response, error);
            break;
        case RklFilterCommandDisable:
            ok = client_.Disable(response, error);
            break;
        case RklFilterCommandClearCounters:
            ok = client_.ClearCounters(response, error);
            break;
        default:
            error = L"Orden no admitida por la interfaz.";
            break;
        }

        if (ok) {
            status_ = response;
            lastError_.clear();
            AddLog(successMessage);
            RefreshViews();
        } else {
            lastError_ = error;
            AddLog(L"Orden rechazada: " + error);
        }
    }

    void ApplySelection()
    {
        RKL_FILTER_RESPONSE response{};
        std::wstring error;
        if (!client_.ReplaceRules(selected_, response, error)) {
            lastError_ = error;
            AddLog(L"No se pudo aplicar la selección: " + error);
            return;
        }
        status_ = response;
        SyncSelectionFromDriver();
        lastError_.clear();
        AddLog(
            L"Selección aplicada: " +
            std::to_wstring(status_.RuleCount) + L" archivo(s).");
        RefreshViews();
    }

    void RenderHeader()
    {
        ImGui::TextUnformatted("ROOTKITLAB 2.0");
        ImGui::SameLine();
        ImGui::TextDisabled("Minifilter de ocultación selectiva y verificación cross-view");
        ImGui::Separator();

        ImGui::TextUnformatted("Driver");
        ImGui::SameLine();
        ImGui::TextColored(
            connected_ ? ImVec4(0.35f, 0.85f, 0.48f, 1.0f)
                       : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
            "%s",
            connected_ ? "CONECTADO" : "SIN CONEXIÓN");
        ImGui::SameLine(190.0f);
        ImGui::TextUnformatted("Estado");
        ImGui::SameLine();
        ImGui::TextColored(
            status_.Enabled != 0
                ? ImVec4(0.95f, 0.67f, 0.20f, 1.0f)
                : ImVec4(0.45f, 0.72f, 0.95f, 1.0f),
            "%s",
            status_.Enabled != 0 ? "OCULTACIÓN ACTIVA" : "DESACTIVADO");
        ImGui::SameLine(430.0f);
        ImGui::Text("Reglas: %lu/%u", status_.RuleCount, RKL_FILTER_MAX_RULES);
        ImGui::SameLine(570.0f);
        ImGui::Text(
            "Servicio: %s · %s",
            service_.running ? "RUNNING" : "STOPPED",
            StartTypeLabel(service_.startType));
        ImGui::SameLine(850.0f);
        ImGui::Text(
            "Marker: %s",
            status_.MarkerPresent != 0 ? "presente" : "ausente");
    }

    void RenderControlPanel()
    {
        ImGui::BeginChild("control-panel", ImVec2(0, 0), true);
        ImGui::TextUnformatted("CONTROL DEL MINIFILTER");
        ImGui::Separator();

        if (ImGui::Button("Reconectar", ImVec2(125, 34))) {
            Connect();
        }
        ImGui::SameLine();
        if (ImGui::Button("Actualizar vistas", ImVec2(155, 34))) {
            RefreshViews();
            AddLog(L"Vistas Win32 y MFT actualizadas.");
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Archivos disponibles en el sandbox");
        ImGui::TextDisabled("La selección se guarda en el driver, no en disco.");
        ImGui::BeginChild("file-selection", ImVec2(0, 190), true);
        const std::vector<std::wstring>& available =
            views_.mftOk ? views_.mftEntries : views_.win32Entries;
        for (const auto& name : available) {
            if (NamesEqualInsensitive(name, RKL_FILTER_MARKER_NAME)) {
                continue;
            }
            bool selected = SelectedContains(selected_, name);
            const std::string label = Utf8(name) + "##select";
            if (ImGui::Checkbox(label.c_str(), &selected)) {
                if (selected) {
                    if (selected_.size() < RKL_FILTER_MAX_RULES) {
                        selected_.push_back(name);
                    } else {
                        AddLog(L"Se alcanzó el máximo de reglas.");
                    }
                } else {
                    RemoveSelected(selected_, name);
                }
            }
        }
        if (available.empty()) {
            ImGui::TextDisabled("No se pudieron enumerar archivos.");
        }
        ImGui::EndChild();

        const bool canApply = connected_ && status_.Enabled == 0;
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button("Aplicar selección", ImVec2(-1, 38))) {
            ApplySelection();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        const bool canEnable = connected_ &&
            status_.Enabled == 0 &&
            status_.RuleCount != 0 &&
            status_.MarkerPresent != 0;
        ImGui::BeginDisabled(!canEnable);
        if (ImGui::Button("ACTIVAR OCULTACIÓN", ImVec2(195, 42))) {
            SendSimple(RklFilterCommandEnable, L"Ocultación activada.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!connected_ || status_.Enabled == 0);
        if (ImGui::Button("DESACTIVAR", ImVec2(165, 42))) {
            SendSimple(RklFilterCommandDisable, L"Ocultación desactivada.");
        }
        ImGui::EndDisabled();

        if (ImGui::Button("Limpiar contadores", ImVec2(180, 30))) {
            SendSimple(RklFilterCommandClearCounters, L"Contadores reiniciados.");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Autoactualizar vistas", &autoRefresh_);

        ImGui::Spacing();
        ImGui::TextUnformatted("Contadores del minifilter");
        if (ImGui::BeginTable("counters", 2, ImGuiTableFlags_BordersInnerH)) {
            const auto row = [](const char* label, unsigned long long value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", value);
            };
            row("Consultas de directorio", status_.DirectoryQueries);
            row("Consultas al sandbox", status_.TargetDirectoryQueries);
            row("Entradas retiradas", status_.HiddenEntries);
            row("Cambios de reglas", status_.RuleUpdates);
            row("Órdenes rechazadas", status_.RejectedCommands);
            ImGui::EndTable();
        }

        if (!lastError_.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                "%s",
                Utf8(lastError_).c_str());
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Registro de la sesión");
        ImGui::BeginChild("log", ImVec2(0, 0), true);
        for (const auto& line : log_) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::EndChild();
    }

    void RenderNameList(
        const char* id,
        const std::vector<std::wstring>& entries,
        const std::vector<std::wstring>& highlighted)
    {
        ImGui::BeginChild(id, ImVec2(0, 270), true);
        for (const auto& name : entries) {
            const bool highlight = ContainsNameInsensitive(highlighted, name);
            if (highlight) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.42f, 0.30f, 1.0f),
                    "%s",
                    Utf8(name).c_str());
            } else {
                ImGui::TextUnformatted(Utf8(name).c_str());
            }
        }
        if (entries.empty()) {
            ImGui::TextDisabled("Sin resultados");
        }
        ImGui::EndChild();
    }

    void RenderViewsPanel()
    {
        ImGui::BeginChild("views-panel", ImVec2(0, 0), true);
        ImGui::TextUnformatted("COMPROBACIÓN INDEPENDIENTE");
        ImGui::SameLine();
        const bool inconsistent =
            views_.classification == L"cross_view_inconsistency";
        ImGui::TextColored(
            inconsistent ? ImVec4(0.95f, 0.42f, 0.30f, 1.0f)
                         : ImVec4(0.35f, 0.85f, 0.48f, 1.0f),
            "%s",
            Utf8(views_.classification).c_str());
        ImGui::Separator();

        if (ImGui::BeginTable("view-columns", 2, ImGuiTableFlags_Resizable)) {
            ImGui::TableNextColumn();
            ImGui::Text("Vista Win32 · %zu entradas", views_.win32Entries.size());
            ImGui::TextDisabled("FindFirstFileW / FindNextFileW");
            RenderNameList("win32-list", views_.win32Entries, {});
            ImGui::TableNextColumn();
            ImGui::Text("Vista MFT · %zu entradas", views_.mftEntries.size());
            ImGui::TextDisabled("FSCTL_ENUM_USN_DATA");
            RenderNameList("mft-list", views_.mftEntries, views_.missingFromWin32);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Text("Ausentes en Win32: %zu", views_.missingFromWin32.size());
        if (!views_.missingFromWin32.empty()) {
            for (const auto& name : views_.missingFromWin32) {
                ImGui::BulletText("%s", Utf8(name).c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Apertura directa");
        if (views_.directOpenAttempted) {
            ImGui::Text("Objetivo: %s", Utf8(views_.directOpenTarget).c_str());
            ImGui::SameLine();
            ImGui::TextColored(
                views_.directOpenOk
                    ? ImVec4(0.35f, 0.85f, 0.48f, 1.0f)
                    : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                "%s",
                views_.directOpenOk ? "ACCESIBLE" : "ERROR");
        } else {
            ImGui::TextDisabled("No hay un archivo disponible para la prueba.");
        }

        ImGui::Spacing();
        ImGui::TextDisabled(
            "Ruta fijada en código: C:\\RootkitLabSandbox · máximo 16 reglas · estado seguro tras reinicio");
        ImGui::TextDisabled(
            "ABI %s · revisión de reglas %lu · NTSTATUS %s",
            "0x00020000",
            status_.RuleRevision,
            Utf8(HexStatus(status_.CommandStatus)).c_str());
        ImGui::EndChild();
    }

    DriverClient client_;
    RKL_FILTER_RESPONSE status_{};
    CrossViewSnapshot views_;
    ServiceSnapshot service_;
    std::vector<std::wstring> selected_;
    std::vector<std::string> log_;
    std::wstring lastError_;
    bool connected_ = false;
    bool autoRefresh_ = true;
    ULONGLONG lastStatusPoll_ = 0;
    ULONGLONG lastViewPoll_ = 0;
    ULONGLONG lastServicePoll_ = 0;
};

bool CreateDeviceD3D(HWND window)
{
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selectedLevel{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &description,
        &gSwapChain,
        &gDevice,
        &selectedLevel,
        &gDeviceContext);
    if (result == DXGI_ERROR_UNSUPPORTED) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            &description,
            &gSwapChain,
            &gDevice,
            &selectedLevel,
            &gDeviceContext);
    }
    if (FAILED(result)) {
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(gSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return false;
    }
    const HRESULT viewResult =
        gDevice->CreateRenderTargetView(backBuffer, nullptr, &gRenderTarget);
    backBuffer->Release();
    return SUCCEEDED(viewResult);
}

void CleanupRenderTarget()
{
    if (gRenderTarget != nullptr) {
        gRenderTarget->Release();
        gRenderTarget = nullptr;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(gSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        gDevice->CreateRenderTargetView(backBuffer, nullptr, &gRenderTarget);
        backBuffer->Release();
    }
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (gSwapChain != nullptr) {
        gSwapChain->Release();
        gSwapChain = nullptr;
    }
    if (gDeviceContext != nullptr) {
        gDeviceContext->Release();
        gDeviceContext = nullptr;
    }
    if (gDevice != nullptr) {
        gDevice->Release();
        gDevice = nullptr;
    }
}

LRESULT WINAPI WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

_Use_decl_annotations_
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments != nullptr && argumentCount > 1) {
        const int result = RunCommandLine(argumentCount, arguments);
        LocalFree(arguments);
        return result;
    }
    if (arguments != nullptr) {
        LocalFree(arguments);
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    const WNDCLASSEXW windowClass = {
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        WindowProcedure,
        0,
        0,
        instance,
        LoadIconW(nullptr, IDI_APPLICATION),
        LoadCursorW(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"RootkitLabWindow",
        nullptr
    };
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"RootkitLab 2.0 - Ocultación selectiva",
        WS_OVERLAPPEDWINDOW,
        70,
        50,
        1360,
        860,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr || !CreateDeviceD3D(window)) {
        CleanupDeviceD3D();
        UnregisterClassW(windowClass.lpszClassName, instance);
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.ItemSpacing = ImVec2(9.0f, 7.0f);
    style.WindowPadding = ImVec2(15.0f, 13.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.085f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.09f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.39f, 0.65f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.49f, 0.79f, 1.0f);
    io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf",
        17.0f,
        nullptr,
        io.Fonts->GetGlyphRangesDefault());

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(gDevice, gDeviceContext);

    ApplicationState state;
    state.Initialize();
    bool done = false;
    while (!done) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        if (gSwapChainOccluded &&
            gSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        gSwapChainOccluded = false;

        if (gResizeWidth != 0 && gResizeHeight != 0) {
            CleanupRenderTarget();
            gSwapChain->ResizeBuffers(
                0,
                gResizeWidth,
                gResizeHeight,
                DXGI_FORMAT_UNKNOWN,
                0);
            gResizeWidth = 0;
            gResizeHeight = 0;
            CreateRenderTarget();
        }

        state.Tick();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        state.Render();
        ImGui::Render();

        const float clearColor[4] = {0.03f, 0.04f, 0.055f, 1.0f};
        gDeviceContext->OMSetRenderTargets(1, &gRenderTarget, nullptr);
        gDeviceContext->ClearRenderTargetView(gRenderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT present = gSwapChain->Present(1, 0);
        gSwapChainOccluded = present == DXGI_STATUS_OCCLUDED;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return 0;
}

namespace {

LRESULT WINAPI WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return TRUE;
    }
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            gResizeWidth = LOWORD(lParam);
            gResizeHeight = HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0u) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace
