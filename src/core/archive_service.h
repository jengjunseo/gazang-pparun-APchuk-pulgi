#pragma once
#ifdef _WIN32
#include "zip/zip_extract_win.h"
#include <vector>
namespace hx {
enum class Format { Zip, SevenZip, Rar4, Rar5, Unknown };
struct ArchiveItem { EntryMeta meta; bool crc_defined=false; uint32_t crc=0; };
Format probe_format(const std::wstring& path);
Status inspect_archive(const std::wstring& path,std::vector<ArchiveItem>& items,const zip::ExtractOptions& opt);
Status test_archive(const std::wstring& path,const zip::ExtractOptions& opt);
Status extract_archive(const std::wstring& path,const std::wstring& destination,const zip::ExtractOptions& opt,
                       zip::ExtractReport* report=nullptr,bool keep_partial=false);
Status sdk_archive(const std::wstring& path,const std::wstring& dest,const zip::ExtractOptions& opt,
                   std::vector<ArchiveItem>* listing,zip::ExtractReport* report);
std::wstring app_data_dir();
std::wstring unique_id();
Status remove_owned_tree(const std::wstring& root);
}
#endif
