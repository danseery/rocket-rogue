#include "platform/WebSaveStore.h"

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

EM_JS(void, rr_store_save_js, (const char* savePtr), {
    const value = UTF8ToString(savePtr);
    if (window.RocketBridge && window.RocketBridge.storeSave) {
        window.RocketBridge.storeSave(value);
    }
});

EM_JS(void, rr_clear_save_js, (), {
    if (window.RocketBridge && window.RocketBridge.clearSave) {
        window.RocketBridge.clearSave();
    }
});

EM_JS(void, rr_set_panel_js, (const char* htmlPtr), {
    const value = UTF8ToString(htmlPtr);
    if (window.RocketBridge && window.RocketBridge.setPanel) {
        window.RocketBridge.setPanel(value);
    }
});

#endif

std::string loadBrowserSave()
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

void storeBrowserSave(const std::string& saveData)
{
#ifdef __EMSCRIPTEN__
    rr_store_save_js(saveData.c_str());
#else
    (void)saveData;
#endif
}

void clearBrowserSave()
{
#ifdef __EMSCRIPTEN__
    rr_clear_save_js();
#endif
}

void setBrowserPanelHtml(const std::string& html)
{
#ifdef __EMSCRIPTEN__
    rr_set_panel_js(html.c_str());
#else
    (void)html;
#endif
}

} // namespace rocket

