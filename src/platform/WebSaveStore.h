#pragma once

#include <string>

namespace rocket {

std::string loadBrowserSave();
void storeBrowserSave(const std::string& saveData);
void clearBrowserSave();
void setBrowserPanelHtml(const std::string& html);

} // namespace rocket

