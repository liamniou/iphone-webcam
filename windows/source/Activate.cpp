#include "MediaSource.h"

#include <wrl/module.h>
#include <string>

using namespace Microsoft::WRL;

// Activation object registered under the virtual camera CLSID. The Frame Server
// sets attributes on it and calls ActivateObject to get the media source.
class __declspec(uuid("6C7E1E8B-3B0F-4C5A-9F1D-2A6B8E4D9C31")) SourceActivate
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IMFActivate, IMFAttributes>>
{
public:
    SourceActivate()
    {
        MFCreateAttributes(&m_attributes, 4);
    }

    // IMFActivate
    IFACEMETHODIMP ActivateObject(REFIID riid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (!m_attributes) return E_OUTOFMEMORY;

        std::lock_guard lock(m_lock);
        if (!m_source)
        {
            HRESULT hr = MakeAndInitialize<MediaSource>(&m_source, m_attributes.Get());
            if (FAILED(hr)) return hr;
        }
        return m_source.CopyTo(riid, object);
    }

    IFACEMETHODIMP ShutdownObject() override
    {
        std::lock_guard lock(m_lock);
        if (m_source)
        {
            m_source->Shutdown();
            m_source.Reset();
        }
        return S_OK;
    }

    IFACEMETHODIMP DetachObject() override
    {
        std::lock_guard lock(m_lock);
        m_source.Reset();
        return S_OK;
    }

    // IMFAttributes (forwarded to an internal attribute store)
    IFACEMETHODIMP GetItem(REFGUID key, PROPVARIANT* value) override { return m_attributes->GetItem(key, value); }
    IFACEMETHODIMP GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override { return m_attributes->GetItemType(key, type); }
    IFACEMETHODIMP CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* result) override { return m_attributes->CompareItem(key, value, result); }
    IFACEMETHODIMP Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL* result) override { return m_attributes->Compare(theirs, type, result); }
    IFACEMETHODIMP GetUINT32(REFGUID key, UINT32* value) override { return m_attributes->GetUINT32(key, value); }
    IFACEMETHODIMP GetUINT64(REFGUID key, UINT64* value) override { return m_attributes->GetUINT64(key, value); }
    IFACEMETHODIMP GetDouble(REFGUID key, double* value) override { return m_attributes->GetDouble(key, value); }
    IFACEMETHODIMP GetGUID(REFGUID key, GUID* value) override { return m_attributes->GetGUID(key, value); }
    IFACEMETHODIMP GetStringLength(REFGUID key, UINT32* length) override { return m_attributes->GetStringLength(key, length); }
    IFACEMETHODIMP GetString(REFGUID key, LPWSTR value, UINT32 size, UINT32* length) override { return m_attributes->GetString(key, value, size, length); }
    IFACEMETHODIMP GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) override { return m_attributes->GetAllocatedString(key, value, length); }
    IFACEMETHODIMP GetBlobSize(REFGUID key, UINT32* size) override { return m_attributes->GetBlobSize(key, size); }
    IFACEMETHODIMP GetBlob(REFGUID key, UINT8* buffer, UINT32 size, UINT32* blobSize) override { return m_attributes->GetBlob(key, buffer, size, blobSize); }
    IFACEMETHODIMP GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) override { return m_attributes->GetAllocatedBlob(key, buffer, size); }
    IFACEMETHODIMP GetUnknown(REFGUID key, REFIID riid, LPVOID* object) override { return m_attributes->GetUnknown(key, riid, object); }
    IFACEMETHODIMP SetItem(REFGUID key, REFPROPVARIANT value) override { return m_attributes->SetItem(key, value); }
    IFACEMETHODIMP DeleteItem(REFGUID key) override { return m_attributes->DeleteItem(key); }
    IFACEMETHODIMP DeleteAllItems() override { return m_attributes->DeleteAllItems(); }
    IFACEMETHODIMP SetUINT32(REFGUID key, UINT32 value) override { return m_attributes->SetUINT32(key, value); }
    IFACEMETHODIMP SetUINT64(REFGUID key, UINT64 value) override { return m_attributes->SetUINT64(key, value); }
    IFACEMETHODIMP SetDouble(REFGUID key, double value) override { return m_attributes->SetDouble(key, value); }
    IFACEMETHODIMP SetGUID(REFGUID key, REFGUID value) override { return m_attributes->SetGUID(key, value); }
    IFACEMETHODIMP SetString(REFGUID key, LPCWSTR value) override { return m_attributes->SetString(key, value); }
    IFACEMETHODIMP SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) override { return m_attributes->SetBlob(key, buffer, size); }
    IFACEMETHODIMP SetUnknown(REFGUID key, IUnknown* value) override { return m_attributes->SetUnknown(key, value); }
    IFACEMETHODIMP LockStore() override { return m_attributes->LockStore(); }
    IFACEMETHODIMP UnlockStore() override { return m_attributes->UnlockStore(); }
    IFACEMETHODIMP GetCount(UINT32* count) override { return m_attributes->GetCount(count); }
    IFACEMETHODIMP GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) override { return m_attributes->GetItemByIndex(index, key, value); }
    IFACEMETHODIMP CopyAllItems(IMFAttributes* destination) override { return m_attributes->CopyAllItems(destination); }

private:
    std::mutex m_lock;
    ComPtr<IMFAttributes> m_attributes;
    ComPtr<MediaSource> m_source;
};

CoCreatableClass(SourceActivate);

namespace
{
    HMODULE g_module = nullptr;

    std::wstring ClsidKeyPath()
    {
        return std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + vcam::CLSID_IPhoneWebcamSourceString;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* object)
{
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, object);
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(g_module, path, ARRAYSIZE(path));
    if (length == 0 || length >= ARRAYSIZE(path)) return HRESULT_FROM_WIN32(GetLastError());

    std::wstring key = ClsidKeyPath();
    LSTATUS status = RegSetKeyValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, REG_SZ,
                                     vcam::FriendlyName, static_cast<DWORD>((wcslen(vcam::FriendlyName) + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    std::wstring inproc = key + L"\\InprocServer32";
    status = RegSetKeyValueW(HKEY_LOCAL_MACHINE, inproc.c_str(), nullptr, REG_SZ,
                             path, static_cast<DWORD>((length + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    const wchar_t threading[] = L"Both";
    status = RegSetKeyValueW(HKEY_LOCAL_MACHINE, inproc.c_str(), L"ThreadingModel", REG_SZ,
                             threading, sizeof(threading));
    return HRESULT_FROM_WIN32(status);
}

STDAPI DllUnregisterServer()
{
    LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClsidKeyPath().c_str());
    if (status == ERROR_FILE_NOT_FOUND) return S_OK;
    return HRESULT_FROM_WIN32(status);
}
