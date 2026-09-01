#include "platform/web/WebSaveStore.h"

#include <cstdlib>
#include <emscripten.h>

namespace rocket {

EM_JS(char*, rr_load_save_js, (), {
    const value = (window.RocketBridge && window.RocketBridge.loadSave) ? window.RocketBridge.loadSave() : "";
    const length = lengthBytesUTF8(value) + 1;
    const ptr = _malloc(length);
    stringToUTF8(value, ptr, length);
    return ptr;
});

EM_JS(int, rr_store_save_js, (const char* savePtr), {
    const value = UTF8ToString(savePtr);
    try {
        if (window.RocketBridge && window.RocketBridge.storeSave) window.RocketBridge.storeSave(value);
        return 1;
    } catch (error) { return 0; }
});

EM_JS(int, rr_clear_save_js, (), {
    try {
        if (window.RocketBridge && window.RocketBridge.clearSave) window.RocketBridge.clearSave();
        return 1;
    } catch (error) { return 0; }
});

EM_JS(char*, rr_load_checkpoint_js, (), {
    const value = (window.RocketBridge && window.RocketBridge.loadCheckpoint)
        ? window.RocketBridge.loadCheckpoint() : "";
    const length = lengthBytesUTF8(value) + 1;
    const ptr = _malloc(length);
    stringToUTF8(value, ptr, length);
    return ptr;
});

EM_JS(int, rr_store_checkpoint_js, (const char* savePtr), {
    const value = UTF8ToString(savePtr);
    try {
        if (window.RocketBridge && window.RocketBridge.storeCheckpoint) {
            window.RocketBridge.storeCheckpoint(value);
            return 1;
        }
        return 0;
    } catch (error) { return 0; }
});

EM_JS(int, rr_clear_checkpoint_js, (), {
    try {
        if (window.RocketBridge && window.RocketBridge.clearCheckpoint) {
            window.RocketBridge.clearCheckpoint();
        }
        return 1;
    } catch (error) { return 0; }
});

std::string WebSaveStore::load()
{
    char* data = rr_load_save_js();
    std::string result = data == nullptr ? "" : data;
    std::free(data);
    return result;
}

bool WebSaveStore::storeAtomic(std::string_view saveData)
{
    const std::string copy(saveData);
    if (rr_store_save_js(copy.c_str()) != 0) {
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser save storage failed.";
    return false;
}

bool WebSaveStore::clear()
{
    if (rr_clear_save_js() != 0) {
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser save clear failed.";
    return false;
}

std::string WebSaveStore::loadCheckpoint()
{
    char* data = rr_load_checkpoint_js();
    std::string result = data == nullptr ? "" : data;
    std::free(data);
    return result;
}

bool WebSaveStore::storeCheckpointAtomic(std::string_view saveData)
{
    const std::string copy(saveData);
    if (rr_store_checkpoint_js(copy.c_str()) != 0) {
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser checkpoint storage failed.";
    return false;
}

bool WebSaveStore::clearCheckpoint()
{
    if (rr_clear_checkpoint_js() != 0) {
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser checkpoint clear failed.";
    return false;
}

std::string WebSaveStore::lastError() const
{
    return lastError_;
}

} // namespace rocket
