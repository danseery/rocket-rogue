#include "platform/web/WebSaveStore.h"

#ifdef __EMSCRIPTEN__
#include <cstdlib>
#include <emscripten.h>
#endif

namespace rocket {

#ifdef __EMSCRIPTEN__

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

#endif

std::string WebSaveStore::load()
{
#ifdef __EMSCRIPTEN__
    char* data = rr_load_save_js();
    std::string result = data == nullptr ? "" : data;
    std::free(data);
    return result;
#else
    return {};
#endif
}

bool WebSaveStore::storeAtomic(std::string_view saveData)
{
#ifdef __EMSCRIPTEN__
    const std::string copy(saveData);
    if (rr_store_save_js(copy.c_str()) != 0) {
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser save storage failed.";
    return false;
#else
    (void)saveData;
    return false;
#endif
}

bool WebSaveStore::clear()
{
#ifdef __EMSCRIPTEN__
    if (rr_clear_save_js() != 0) {
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser save clear failed.";
    return false;
#else
    return false;
#endif
}

std::string WebSaveStore::lastError() const
{
    return lastError_;
}

} // namespace rocket
