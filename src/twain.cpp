#pragma ide diagnostic ignored "hicpp-signed-bitwise"
#include "twain.hpp"

#include "exception/dsm_exception.hpp"
#include "twain/memory_transfer.hpp"
#include "application.hpp"

#include <cstring>
#include <list>
#include <loguru.hpp>

using dasa::gliese::scanner::Twain;
using namespace dasa::gliese::scanner::exception;
extern dasa::gliese::scanner::Application *application;

Twain::~Twain() {
    closeDSM();
}

void Twain::fillIdentity() {
    identity.Id = 0;
    identity.Version.Country = TWCY_BRAZIL;
    strncpy(identity.Version.Info, "1.0.0", 32);
    identity.Version.Language = TWLG_PORTUGUESE_BRAZIL;
    identity.Version.MajorNum = 1;
    identity.Version.MinorNum = 0;
    identity.ProtocolMajor = 2;
    identity.ProtocolMinor = 4;
    identity.SupportedGroups = DF_APP2 | DG_CONTROL | DG_IMAGE;
    strncpy(identity.Manufacturer, "Diagnosticos da America SA", 32);
    strncpy(identity.ProductFamily, "Gliese", 32);
    strncpy(identity.ProductName, "Scanner Integration", 32);
}

void Twain::loadDSM(const char *path) {
    if (state > 1) {
        LOG_S(WARNING) << "Trying to load DSM library when it is already loaded";
        return;
    }

    try {
        DSM.load(path);
        entry = DSM.get<TW_UINT16(pTW_IDENTITY, pTW_IDENTITY, TW_UINT32, TW_UINT16, TW_UINT16, TW_MEMREF)>("DSM_Entry");
        state = 2;
    }
    catch (std::exception &e) {
        ABORT_S() << "Failed to open TWAIN library at path [" << path << "]: " << e.what();
    }
}

void Twain::openDSM() {
    if (state > 3) {
        LOG_S(WARNING) << "Trying to open DSM connection when a connection is already open";
        return;
    }
    if (state == 1) {
        ABORT_S() << "Trying to open DSM connection when DSM library is not loaded";
        return;
    }

    auto result = entry(getIdentity(), nullptr, DG_CONTROL, DAT_PARENT, MSG_OPENDSM, nullptr);
    if (result != TWRC_SUCCESS) {
        throw DSMOpenException(result);
    }

    if ((identity.SupportedGroups & DF_DSM2) == DF_DSM2) {
        entrypoint.Size = sizeof(TW_ENTRYPOINT);
        result = entry(getIdentity(), nullptr, DG_CONTROL, DAT_ENTRYPOINT, MSG_GET, &entrypoint);
        if (result != TWRC_SUCCESS) {
            throw DSMOpenException(result);
        }
    }

    state = 3;
    LOG_S(INFO) << "DSM connection open";
}

void Twain::closeDSM() {
    if (state < 3) {
        LOG_S(WARNING) << "Trying to close the DSM when it is not open";
        return;
    }

    if (entry(getIdentity(), nullptr, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, nullptr) != TWRC_SUCCESS) {
        LOG_S(ERROR) << "Failed to close the DSM connection";
    } else {
        LOG_S(INFO) << "DSM connection closed";
        state = 2;
    }
}

std::list<TW_IDENTITY> Twain::listSources() {
    std::list<TW_IDENTITY> sources;

    if (state < 3) {
        LOG_S(ERROR) << "Trying to list sources when DSM is not active";
        return sources;
    }

    TW_IDENTITY current;
    memset(&current, 0, sizeof(TW_IDENTITY));
    auto rc = entry(&identity, 0, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, (TW_MEMREF)&current);
    if (rc != TWRC_SUCCESS) {
        return sources;
    }

    do {
        sources.push_back(current);
        memset(&current, 0, sizeof(TW_IDENTITY));
    } while (entry(&identity, 0, DG_CONTROL, DAT_IDENTITY, MSG_GETNEXT, (TW_MEMREF)&current) == TWRC_SUCCESS);

    return sources;
}

TW_IDENTITY Twain::getDefaultDataSource() {
    TW_IDENTITY current;
    memset(&current, 0, sizeof(TW_IDENTITY));

    if (state < 3) {
        LOG_S(ERROR) << "Trying to get default source when DSM is not active";
        return current;
    }

    auto rc = entry(getIdentity(), 0, DG_CONTROL, DAT_IDENTITY, MSG_GETDEFAULT, (TW_MEMREF)&current);
    if (rc != TWRC_SUCCESS) {
        getStatus(rc);
    }
    return current;
}

TW_STATUSUTF8 Twain::getStatus(TW_UINT16) {
    TW_STATUSUTF8 twStatus;
    memset(&twStatus, 0, sizeof(TW_STATUSUTF8));

    if (state < 3) {
        LOG_S(ERROR) << "Trying to get status when DSM is not active";
        return twStatus;
    }

    entry(getIdentity(), 0, DG_CONTROL, DAT_STATUSUTF8, MSG_GET, &twStatus);
    return twStatus;
}

static TW_UINT16 DSMCallback(pTW_IDENTITY origin, pTW_IDENTITY /*dest*/, TW_UINT32 /*dg*/, TW_UINT16 /*dat*/, TW_UINT16 message, TW_MEMREF /*data*/) {
    auto& twain = application->getTwain();
    if (origin == nullptr || origin->Id != twain.getDataSouce()->Id) {
        return TWRC_FAILURE;
    }

    switch (message) {
        case MSG_XFERREADY:
            twain.setState(6);
        case MSG_CLOSEDSREQ:
        case MSG_CLOSEDSOK:
        case MSG_NULL:
            LOG_S(INFO) << "Got message from DSM: " << message;
            break;
        default:
            LOG_S(INFO) << "Got unknown message from DSM: " << message;
            return TWRC_FAILURE;
    }

    return TWRC_SUCCESS;
}

bool Twain::loadDataSource(TW_UINT32 id) {
    if (state < 3) {
        LOG_S(ERROR) << "Trying to load DS when DSM is not active";
        return false;
    }
    if (state > 3) {
        LOG_S(WARNING) << "A source is already open";
        return false;
    }

    currentDS.reset(nullptr);
    auto sources = listSources();
    for (auto & source : sources) {
        if (source.Id == id) {
            currentDS = std::make_unique<TW_IDENTITY>();
            memcpy(currentDS.get(), &source, sizeof(TW_IDENTITY));
            break;
        }
    }

    if (!currentDS) {
        LOG_S(ERROR) << "Could not find DS with id " << id;
        return false;
    }

    TW_CALLBACK callback;
    memset(&callback, 0, sizeof(TW_CALLBACK));
    callback.CallBackProc = (TW_MEMREF)&DSMCallback;
    callback.RefCon = 0;
    useCallbacks = false;

    auto resultCode = entry(getIdentity(), nullptr, DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, currentDS.get());

    if (resultCode != TWRC_SUCCESS) {
        LOG_S(ERROR) << "Failed to open DataSource";
        currentDS.reset(nullptr);
        return false;
    }

    state = 4;

#if defined(WIN32) || defined(WIN64) || defined (_WINDOWS)
    if ((getIdentity()->SupportedGroups & DF_DSM2) && (currentDS->SupportedGroups & DF_DSM2)) {
        resultCode = entry(getIdentity(), currentDS.get(), DG_CONTROL, DAT_CALLBACK, MSG_REGISTER_CALLBACK, &callback);
        if (resultCode != TWRC_SUCCESS) {
            LOG_S(ERROR) << "Failed to register callback: " << resultCode;
            return false;
        } else {
            useCallbacks = true;
        }
    } else {
        LOG_S(WARNING) << "MSG_REGISTER_CALLBACK not supported";
    }
#else
    resultCode = entry(getIdentity(), currentDS.get(), DG_CONTROL, DAT_CALLBACK, MSG_REGISTER_CALLBACK, &callback);
    if (resultCode != TWRC_SUCCESS) {
        LOG_S(ERROR) << "Failed to register callback: " << resultCode;
        return false;
    } else {
        useCallbacks = true;
    }
#endif

    return true;
}

bool Twain::enableDataSource(TW_HANDLE handle, bool showUI) {
    if (state < 4) {
        LOG_S(ERROR) << "Trying to enable DS but it was not opened";
        return false;
    }

    ui.ShowUI = showUI;
    ui.ModalUI = false;
    ui.hParent = handle;

    auto resultCode = entry(getIdentity(), currentDS.get(), DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS, &ui);
    if (resultCode != TWRC_SUCCESS && resultCode != TWRC_CHECKSTATUS) {
        LOG_S(ERROR) << "Failed to enable DS: " << resultCode;
        return false;
    }

    state = 5;
    return true;
}

bool Twain::closeDS() {
    if (state < 4) {
        LOG_S(ERROR) << "Trying to close DS but it was not opened";
        return false;
    }

    auto resultCode = entry(getIdentity(), nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, currentDS.get());

    if (resultCode != TWRC_SUCCESS) {
        LOG_S(ERROR) << "Failed to close DS";
        return false;
    }

    state = 3;
    LOG_S(INFO) << "DS closed";
    currentDS = nullptr;
    return true;
}

TW_UINT16 Twain::setCapability(TW_UINT16 capability, int value, TW_UINT16 type) {
    TW_UINT16 returnCode = TWRC_FAILURE;

    TW_CAPABILITY cap;
    cap.Cap = capability;
    cap.ConType = TWON_ONEVALUE;
    cap.hContainer = DSM_MemAllocate(sizeof(TW_ONEVALUE));
    if (cap.hContainer == nullptr) {
        ABORT_S() << "Error allocating memory";
        return returnCode;
    }

    auto pValue = (pTW_ONEVALUE)DSM_LockMemory(cap.hContainer);
    pValue->ItemType = type;

    switch (type) {
        case TWTY_INT8:
            *(TW_INT8*)&pValue->Item = (TW_INT8)value;
            break;

        case TWTY_INT16:
            *(TW_INT16*)&pValue->Item = (TW_INT16)value;
            break;

        case TWTY_INT32:
            *(TW_INT32*)&pValue->Item = (TW_INT32)value;
            break;

        case TWTY_UINT8:
            *(TW_UINT8*)&pValue->Item = (TW_UINT8)value;
            break;

        case TWTY_UINT16:
            *(TW_UINT16*)&pValue->Item = (TW_UINT16)value;
            break;

        case TWTY_UINT32:
            memcpy(&pValue->Item,&value,sizeof(TW_UINT32));
            break;

        case TWTY_BOOL:
            memcpy(&pValue->Item,&value,sizeof(TW_BOOL));
            break;

        default:
            goto finishing;
    }

    returnCode = entry(getIdentity(), getDataSouce(), DG_CONTROL, DAT_CAPABILITY, MSG_SET, &cap);
    if (returnCode == TWRC_FAILURE) {
        LOG_S(ERROR) << "Failed to set capability";
    }

finishing:
    DSM_UnlockMemory(cap.hContainer);
    DSM_Free(cap.hContainer);

    return returnCode;
}

void Twain::startScan(concurrency::streams::ostream &os) {
    if (state != 6) {
        LOG_S(ERROR) << "Cannot start scanning unless if scanner is ready";
        return;
    }

    setCapability(ICAP_XFERMECH, TWSX_MEMORY, TWTY_UINT16);

    TW_CAPABILITY xferCap;
    xferCap.Cap = ICAP_XFERMECH;
    auto rc = getCapability(xferCap);
    TW_UINT32 mech;

    if (rc == TWRC_FAILURE) {
        return;
    }
    if (!getCurrent(&xferCap, mech)) {
        LOG_S(ERROR) << "Failed to get current ICAP_XFERMECH value";
        return;
    }

    if (mech != TWSX_MEMORY) {
        LOG_S(ERROR) << "The only supported ICAP_XFERMECH is TWSX_MEMORY";
        return;
    }

    auto transfer = std::make_unique<dasa::gliese::scanner::twain::MemoryTransfer>(this, os);
    transfer->transfer();
}

TW_INT16 Twain::getCapability(TW_CAPABILITY& _cap, TW_UINT16 _msg) {
    if(_msg != MSG_GET && _msg != MSG_GETCURRENT && _msg != MSG_GETDEFAULT && _msg != MSG_RESET) {
        LOG_S(ERROR) << "Bad message";
        return TWRC_FAILURE;
    }

    if(state < 4) {
        LOG_S(ERROR) << "You need to open a data source first.";
        return TWRC_FAILURE;
    }

    // Check if this capability structure has memory already alloc'd.
    // If it does, free that memory before the call else we'll have a memory
    // leak because the source allocates memory during a MSG_GET.
    if (_cap.hContainer)
    {
        DSM_Free(_cap.hContainer);
        _cap.hContainer = nullptr;
    }

    _cap.ConType = TWON_DONTCARE16;

    // capability structure is set, make the call to the source now
    TW_UINT16 twrc = entry(getIdentity(), getDataSouce(), DG_CONTROL, DAT_CAPABILITY, _msg, &_cap);

    return twrc;
}

bool Twain::getCurrent(TW_CAPABILITY *pCap, TW_UINT32& val)
{
    if (!pCap->hContainer) {
        return false;
    }

    bool ret = false;
    if (pCap->ConType == TWON_ENUMERATION) {
        auto pCapPT = (pTW_ENUMERATION)DSM_LockMemory(pCap->hContainer);
        switch(pCapPT->ItemType) {
            case TWTY_INT32:
                val = (TW_INT32)((pTW_INT32)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;

            case TWTY_UINT32:
                val = (TW_INT32)((pTW_UINT32)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;

            case TWTY_INT16:
                val = (TW_INT32)((pTW_INT16)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;

            case TWTY_UINT16:
                val = (TW_INT32)((pTW_UINT16)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;

            case TWTY_INT8:
                val = (TW_INT32)((pTW_INT8)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;

            case TWTY_UINT8:
                val = (TW_INT32)((pTW_UINT8)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;

            case TWTY_BOOL:
                val = (TW_INT32)((pTW_BOOL)(&pCapPT->ItemList))[pCapPT->CurrentIndex];
                ret = true;
                break;
        }
        DSM_UnlockMemory(pCap->hContainer);
    }
    else if(TWON_ONEVALUE == pCap->ConType) {
        auto pCapPT = (pTW_ONEVALUE)DSM_LockMemory(pCap->hContainer);
        if(pCapPT->ItemType < TWTY_FIX32) {
            val = pCapPT->Item;
            ret = true;
        }
        DSM_UnlockMemory(pCap->hContainer);
    }
    else if(TWON_RANGE == pCap->ConType) {
        auto pCapPT = (pTW_RANGE)DSM_LockMemory(pCap->hContainer);
        if(pCapPT->ItemType < TWTY_FIX32) {
            val = pCapPT->CurrentValue;
            ret = true;
        }
        DSM_UnlockMemory(pCap->hContainer);
    }

    return ret;
}


TW_HANDLE Twain::DSM_MemAllocate(TW_UINT32 size) {
    if (entrypoint.DSM_MemAllocate) {
        return entrypoint.DSM_MemAllocate(size);
    }

#ifdef TWH_CMP_MSC
    return ::GlobalAlloc(GPTR, _size);
#else
    return nullptr;
#endif
}

void Twain::DSM_Free(TW_HANDLE _hMemory)
{
    if(entrypoint.DSM_MemFree)
    {
        return entrypoint.DSM_MemFree(_hMemory);
    }

#ifdef TWH_CMP_MSC
    ::GlobalFree(_hMemory);
#endif
}


TW_MEMREF Twain::DSM_LockMemory(TW_HANDLE memory) {
    if (entrypoint.DSM_MemLock) {
        return entrypoint.DSM_MemLock(memory);
    }

#ifdef TWH_CMP_MSC
    return (TW_MEMREF)::GlobalLock(_hMemory);
#else
    return nullptr;
#endif
}

void Twain::DSM_UnlockMemory(TW_HANDLE _hMemory)
{
    if(entrypoint.DSM_MemUnlock)
    {
        return entrypoint.DSM_MemUnlock(_hMemory);
    }

#ifdef TWH_CMP_MSC
    ::GlobalUnlock(_hMemory);
#endif
}

web::json::value deviceToJson(TW_IDENTITY device) {
    auto deviceJson = web::json::value::object();
    deviceJson[U("id")] = web::json::value((uint32_t)device.Id);
    deviceJson[U("productName")] = web::json::value(utility::conversions::to_string_t(device.ProductName));
    deviceJson[U("manufacturer")] = web::json::value(utility::conversions::to_string_t(device.Manufacturer));
    deviceJson[U("productFamily")] = web::json::value(utility::conversions::to_string_t(device.ProductFamily));
    return deviceJson;
}

std::ostream& operator<<(std::ostream& os, const TW_IDENTITY& identity) {
    os << "Device " << identity.Id;
    if (identity.Id != 0) {
        os << ": " << identity.Manufacturer << ", " << identity.ProductName << ", " << identity.ProductFamily;
    } else {
        os << " has no data";
    }
    return os;
}
