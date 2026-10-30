#pragma once

#include <windows.h>
#include <windns.h>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct DiscoveredDevice
{
    std::wstring instance; // full mDNS instance name (unique key)
    std::wstring name;     // human-readable name
    std::wstring address;  // IPv4
    uint16_t port = 0;
    ULONGLONG lastSeen = 0;
};

// Continuously browses for "_iphonewebcam._tcp.local" using Windows' built-in mDNS client.
// Discovery is re-run periodically so phones that vanish without a goodbye are pruned.
class DeviceBrowser
{
public:
    void Start();
    void Stop();
    void Refresh(); // clears the list and browses again

    std::vector<DiscoveredDevice> Snapshot();
    std::optional<DiscoveredDevice> FindByName(const std::wstring& name);

private:
    struct ResolveContext
    {
        DeviceBrowser* owner;
        uint64_t generation;
        std::wstring instance;
        DNS_SERVICE_CANCEL cancel;
    };

    static VOID WINAPI OnBrowse(DWORD status, PVOID context, PDNS_RECORD records);
    static VOID WINAPI OnResolve(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance);
    void StartBrowseLocked();
    void CancelBrowseLocked();
    void Resolve(const std::wstring& instance, uint64_t generation);
    void HousekeepingLoop();

    std::mutex m_lock;
    std::condition_variable m_cv;
    std::thread m_thread;
    bool m_running = false;
    bool m_browsing = false;
    uint64_t m_generation = 0;
    DNS_SERVICE_CANCEL m_browseCancel{};
    std::map<std::wstring, DiscoveredDevice> m_devices;
    std::set<std::wstring> m_pendingResolves;
};
