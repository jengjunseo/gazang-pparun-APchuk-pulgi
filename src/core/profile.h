#pragma once
#ifdef _WIN32
#include "zip/zip_extract_win.h"
#include <string>
namespace hx {
std::string profile_fingerprint(const std::wstring& output_parent);
bool load_zip_profile(zip::ExtractOptions& opt,const std::wstring& output_parent);
Status save_zip_profile(unsigned workers,unsigned closers,unsigned samples,bool quick,const std::wstring& volume_path);
}
#endif
