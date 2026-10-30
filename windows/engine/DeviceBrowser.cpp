#include <winsock2.h>
#include <ws2tcpip.h>

#include "DeviceBrowser.h"

namespace
{
    constexpr wchar_t ServiceType[] = L"_iphonewebcam._tcp.local";
    constexpr wchar_t ServiceSuffix[] = L"._iphonewebcam._tcp.local";
    constexpr DWORD RebrowseIntervalMs = 30'000;
    constexpr ULONGLONG ExpiryMs = 95'000;

    // Browse callbacks only get a generation number as context, so the (single, process-lifetime)
    // browser is reached through this pointer.
    DeviceBrowser* g_browser = nullptr;

    // "Lev\032iPhone._iphonewebcam._tcp.local" -> "Lev iPhone"
    std::wstring DisplayName(const std::wstring& instance)
    {
        std::wstring label = instance;
        size_t suffix = label.rfind(ServiceSuffix);
        if (suffix != std::wstring::npos) label.resize(suffix);

        std::wstring result;
        for (size_t i = 0; i < label.size(); ++i)
        {
            if (label[i] == L'\\' && i + 3 < label.size() && iswdigit(label[i + 1]) && iswdigit(label[i + 2]) && iswdigit(label[i + 3]))
            {
                result.push_back(static_cast<wchar_t>((label[i + 1] - L'0') * 100 + (label[i + 2] - L'0') * 10 + (label[i + 3] - L'0')));
                i += 3;
            }
            else if (label[i] == L'\\' && i + 1 < label.size())
            {
                result.push_back(label[++i]);
            }
            else
            {
                result.push_back(label[i]);
            }
        }
        return result;
    }

    std::wstring HostToIPv4(const wchar_t* host)
    {
        ADDRINFOW hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        ADDRINFOW* info = nullptr;
        if (GetAddrInfoW(host, nullptr, &hints, &info) != 0 || !info) return {};
        wchar_t buffer[INET_ADDRSTRLEN] = {};
        InetNtopW(AF_INET, &reinterpret_cast<sockaddr_in*>(info->ai_addr)->sin_addr, buffer, ARRAYSIZE(buffer));
        FreeAddrInfoW(info);
        return buffer;
    }
}

void DeviceBrowser::Start()
{
    std::lock_guard lock(m_lock);
    if (m_running) return;
    m_running = true;
    g_browser = this;
    StartBrowseLocked();
    m_thread = std::thread([this] { HousekeepingLoop(); });
}

void DeviceBrowser::Stop()
{
    {
        std::lock_guard lock(m_lock);
        if (!m_running) return;
        m_running = false;
        CancelBrowseLocked();
        ++m_generation;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void DeviceBrowser::Refresh()
{
    std::lock_guard lock(m_lock);
    if (!m_running) return;
    m_devices.clear();
    m_pendingResolves.clear();
    CancelBrowseLocked();
    StartBrowseLocked();
}

std::vector<DiscoveredDevice> DeviceBrowser::Snapshot()
{
    std::lock_guard lock(m_lock);
    std::vector<DiscoveredDevice> result;
    for (const auto& [key, device] : m_devices) result.push_back(device);
    return result;
}

std::optional<DiscoveredDevice> DeviceBrowser::FindByName(const std::wstring& name)
{
    std::lock_guard lock(m_lock);
    for (const auto& [key, device] : m_devices)
    {
        if (device.name == name) return device;
    }
    return std::nullopt;
}

void DeviceBrowser::StartBrowseLocked()
{
    ++m_generation;
    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = ServiceType;
    request.pBrowseCallback = OnBrowse;
    // The callback receives the generation; stale callbacks after a restart are ignored.
    request.pQueryContext = reinterpret_cast<PVOID>(static_cast<uintptr_t>(m_generation));
    m_browseCancel = {};
    m_browsing = DnsServiceBrowse(&request, &m_browseCancel) == DNS_REQUEST_PENDING;
}

void DeviceBrowser::CancelBrowseLocked()
{
    if (m_browsing)
    {
        DnsServiceBrowseCancel(&m_browseCancel);
        m_browsing = false;
    }
}

VOID WINAPI DeviceBrowser::OnBrowse(DWORD status, PVOID context, PDNS_RECORD records)
{
    uint64_t generation = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(context));
    DeviceBrowser* self = g_browser;
    if (status == ERROR_SUCCESS && self)
    {
        std::vector<std::wstring> toResolve;
        {
            std::lock_guard lock(self->m_lock);
            if (generation == self->m_generation)
            {
                for (PDNS_RECORD r = records; r; r = r->pNext)
                {
                    if (r->wType != DNS_TYPE_PTR || !r->Data.PTR.pNameHost) continue;
                    std::wstring instance = r->Data.PTR.pNameHost;
                    if (r->dwTtl == 0)
                    {
                        self->m_devices.erase(instance); // goodbye packet
                    }
                    else if (self->m_pendingResolves.insert(instance).second)
                    {
                        toResolve.push_back(instance);
                    }
                }
            }
        }
        for (const auto& instance : toResolve) self->Resolve(instance, generation);
    }
    if (records) DnsRecordListFree(records, DnsFreeRecordList);
}

void DeviceBrowser::Resolve(const std::wstring& instance, uint64_t generation)
{
    auto context = new ResolveContext{ this, generation, instance, {} };
    DNS_SERVICE_RESOLVE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = const_cast<PWSTR>(context->instance.c_str());
    request.pResolveCompletionCallback = OnResolve;
    request.pQueryContext = context;
    if (DnsServiceResolve(&request, &context->cancel) != DNS_REQUEST_PENDING)
    {
        std::lock_guard lock(m_lock);
        m_pendingResolves.erase(instance);
        delete context;
    }
}

VOID WINAPI DeviceBrowser::OnResolve(DWORD status, PVOID contextPointer, PDNS_SERVICE_INSTANCE instance)
{
    auto context = static_cast<ResolveContext*>(contextPointer);
    DeviceBrowser* self = context->owner;

    DiscoveredDevice device;
    bool ok = false;
    if (status == ERROR_SUCCESS && instance)
    {
        device.instance = context->instance;
        device.name = DisplayName(context->instance);
        device.port = instance->wPort;
        if (instance->ip4Address)
        {
            in_addr addr;
            addr.S_un.S_addr = *instance->ip4Address;
            wchar_t buffer[INET_ADDRSTRLEN] = {};
            InetNtopW(AF_INET, &addr, buffer, ARRAYSIZE(buffer));
            device.address = buffer;
        }
        else if (instance->pszHostName)
        {
            device.address = HostToIPv4(instance->pszHostName);
        }
        device.lastSeen = GetTickCount64();
        ok = !device.address.empty() && device.port != 0;
    }
    if (instance) DnsServiceFreeInstance(instance);

    {
        std::lock_guard lock(self->m_lock);
        self->m_pendingResolves.erase(context->instance);
        if (ok && self->m_running) self->m_devices[device.instance] = device;
    }
    delete context;
}

void DeviceBrowser::HousekeepingLoop()
{
    std::unique_lock lock(m_lock);
    while (m_running)
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(RebrowseIntervalMs), [this] { return !m_running; });
        if (!m_running) break;

        ULONGLONG now = GetTickCount64();
        for (auto it = m_devices.begin(); it != m_devices.end();)
        {
            it = now - it->second.lastSeen > ExpiryMs ? m_devices.erase(it) : std::next(it);
        }
        CancelBrowseLocked();
        StartBrowseLocked();
    }
}
