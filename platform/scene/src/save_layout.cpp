#include "pc_save.hpp"
#include "nnn/resource_path.hpp"
#include "nnn/runtime_parameter_defaults.hpp"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <fstream>
#include <locale>
#include <mutex>
#include <sstream>

namespace emberfall::scene::pc {
namespace fs=std::filesystem;
namespace {
constexpr char config_name[]="emberfall-system.cfg";
constexpr int config_version=3;
constexpr std::array<std::string_view,4> system_sources{{"","exe","org","game_files"}};
std::mutex layout_mutex;

struct CachedLayout {
    FileSaveLayout files;
    // An empty result after a completed EXE scan is also cached.
    std::optional<ExecutableSaveLayout> executable;
    SystemSaveFormat system;
    std::optional<std::uint32_t> extension_code_base;
};

struct LocatedLayout {
    CachedLayout cache;
    fs::path file;
};

template<class T>
bool field(std::istream& in,std::string_view name,T& value) {
    std::string key;
    return (in>>key>>value) && key==name;
}

bool valid_layers(int layers) { return layers==classic_layer_count || layers==layer_count; }
bool valid_width(int width) {
    return width==legacy_message_width || width==classic_message_width || width==expanded_message_width;
}
bool valid_status(int size) {
    return size==legacy_status_bytes || size==classic_status_bytes || size==preview_status_bytes;
}

std::optional<CachedLayout> read_layout(const fs::path& file) {
    std::error_code error;
    if (!fs::is_regular_file(fs::symlink_status(file,error))) return {};
    std::ifstream input(file,std::ios::binary);
    if (!input) return {};
    std::array<char,1024> text{};
    input.read(text.data(),static_cast<std::streamsize>(text.size()));
    if (input.bad() || !input.eof()) return {};
    std::istringstream fields(std::string(text.data(),static_cast<std::size_t>(input.gcount())));
    fields.imbue(std::locale::classic());

    int version,layers,width,status,okikae,system,analyzed;
    if (!field(fields,"emberfall_system",version) || version<1 || version>config_version ||
        !field(fields,"file_layer_records",layers) || !valid_layers(layers) ||
        !field(fields,"file_message_width",width) || !valid_width(width) ||
        !field(fields,"file_status_bytes",status) || !valid_status(status) ||
        !field(fields,"file_okikae",okikae) || (okikae!=0 && okikae!=1) ||
        !field(fields,"file_system_save_version",system) || system<1 || system>3 ||
        !field(fields,"exe_analyzed",analyzed) || (analyzed!=0 && analyzed!=1)) return {};

    CachedLayout layout{{static_cast<unsigned>(layers),static_cast<std::size_t>(width),
                         static_cast<std::size_t>(status),okikae!=0,system},std::nullopt,{},std::nullopt};
    if (analyzed) {
        if (!field(fields,"exe_layer_records",layers) || (layers!=0 && !valid_layers(layers)) ||
            !field(fields,"exe_message_width",width) || (width!=0 && !valid_width(width)) ||
            !field(fields,"exe_status_bytes",status) || (status!=0 && !valid_status(status)) ||
            !field(fields,"exe_okikae",okikae) || okikae<-1 || okikae>1 ||
            !field(fields,"exe_system_save_version",system) || system<0 || system>3 ||
            (layers==0 && (width!=0 || status!=0 || okikae!=-1 || system!=0))) return {};
        auto& exe=layout.executable.emplace();
        if (layers) exe.layer_records=static_cast<unsigned>(layers);
        if (width) exe.message_width=static_cast<std::size_t>(width);
        if (status) exe.status_bytes=static_cast<std::size_t>(status);
        if (okikae!=-1) exe.registers_okikae=okikae!=0;
        if (system) exe.system_save_version=system;
    }
    if (version>=2) {
        int trial;
        std::string source;
        if (!field(fields,"system_save_version",system) || system<1 || system>3 ||
            !field(fields,"system_save_trial",trial) || (trial!=0 && trial!=1) ||
            !field(fields,"system_save_source",source)) return {};
        const auto origin=std::find(system_sources.begin()+1,system_sources.end(),source);
        if (origin==system_sources.end()) return {};
        layout.system={system,trial!=0,static_cast<SystemSaveSource>(origin-system_sources.begin())};
    } else {
        // Existing configurations are authoritative. Upgrading never initiates
        // another EXE/template scan behind the user's back.
        const auto cached_version=layout.executable ? layout.executable->system_save_version : std::nullopt;
        layout.system={cached_version.value_or(layout.files.system_save_version),false,
            cached_version ? SystemSaveSource::Executable : SystemSaveSource::GameFiles};
    }
    if (version>=3) {
        std::string code;
        if (!field(fields,"ext_start_code",code)) return {};
        if (code==u8"无") layout.extension_code_base=0;
        else if (code!=u8"解析失败") {
            std::uint32_t number=0;
            const auto parsed=std::from_chars(code.data(),code.data()+code.size(),number);
            if (parsed.ec!=std::errc{} || parsed.ptr!=code.data()+code.size()) return {};
            if (number<min_legacy_extension_code || number>static_cast<std::uint32_t>(INT32_MAX)-max_extensions) return {};
            layout.extension_code_base=number;
        }
    }
    std::string extra;
    if (fields>>extra) return {};
    return layout;
}

bool write_layout(const fs::path& file,const CachedLayout& layout) {
    std::error_code error;
    fs::create_directory(file.parent_path(),error);
    if (error) return false;
    auto pending=file;
    pending+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::ofstream out(pending,std::ios::binary|std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    out<<"emberfall_system "<<config_version<<'\n'
       <<"file_layer_records "<<layout.files.layer_records<<'\n'
       <<"file_message_width "<<layout.files.message_width<<'\n'
       <<"file_status_bytes "<<layout.files.status_bytes<<'\n'
       <<"file_okikae "<<layout.files.registers_okikae<<'\n'
       <<"file_system_save_version "<<layout.files.system_save_version<<'\n'
       <<"exe_analyzed "<<layout.executable.has_value()<<'\n';
    if (const auto& exe=layout.executable) {
        out<<"exe_layer_records "<<exe->layer_records.value_or(0)<<'\n'
           <<"exe_message_width "<<exe->message_width.value_or(0)<<'\n'
           <<"exe_status_bytes "<<exe->status_bytes.value_or(0)<<'\n'
           <<"exe_okikae "<<(exe->registers_okikae ? static_cast<int>(*exe->registers_okikae) : -1)<<'\n'
           <<"exe_system_save_version "<<exe->system_save_version.value_or(0)<<'\n';
    }
    out<<"system_save_version "<<layout.system.version<<'\n'
       <<"system_save_trial "<<layout.system.trial<<'\n'
       <<"system_save_source "<<system_sources[static_cast<std::size_t>(layout.system.source)]<<'\n';
    out<<"ext_start_code "<<(layout.extension_code_base ?
        (*layout.extension_code_base ? std::to_string(*layout.extension_code_base) : u8"无") : u8"解析失败")<<'\n';
    out.close();
    if (out) fs::rename(pending,file,error);
    if (out && !error) return true;
    fs::remove(pending,error);
    return false;
}

fs::path config_directory(const fs::path& root,const char* name) {
    try { return nnn::resource::find_directory(root,name); }
    catch (const fs::filesystem_error&) { return {}; }
}

std::array<fs::path,2> config_directories(const fs::path& root) {
    return {config_directory(root,"init2"),config_directory(root,"init")};
}

DirectorySaveLayout layout_values(const CachedLayout& cached) {
    return {cached.files,cached.executable.value_or(ExecutableSaveLayout{}),cached.system,cached.extension_code_base};
}

std::optional<LocatedLayout> find_layout(const std::array<fs::path,2>& directories,bool strict=true) {
    bool invalid=false;
    for (const auto& directory:directories) {
        if (directory.empty()) continue;
        const auto file=directory/config_name;
        if (const auto layout=read_layout(file)) return LocatedLayout{*layout,file};
        std::error_code error;
        if (fs::is_regular_file(file,error) || fs::is_symlink(fs::symlink_status(file,error))) invalid=true;
    }
    if (strict && invalid) throw std::runtime_error("Invalid emberfall-system.cfg; manually regenerate the configuration");
    return {};
}

bool valid_system_template(const std::vector<nnn::Byte>& bytes,int version) {
    struct Block { nnn::Word code; std::size_t size; const char* name; };
    constexpr std::array<Block,16> blocks{{
        {0,64,"FILE INFO      "},{1,2048,"SYSTEM DATA    "},{5,32800,"MES FLAG       "},
        {6,4128,"SYSTEMVAR      "},{7,1312,"CGFLAG         "},{8,2080,"SCENEFLAG      "},
        {9,16416,"FILMFLAG       "},{10,1056,"VOICEFLAG      "},{11,1056,"MOVIEFLAG      "},
        {12,1056,"MUSICFLAG      "},{13,6432,"OKIKAE         "},{14,4128,"ACHIEVEMENT    "},
        {15,4128,"TERM           "},{16,65568,"CHARAVOICEFLAG "},{17,1056,"SYSTEMFLAG2    "},
        {18,4128,"SELECTMESSAGE  "}
    }};
    using nnn::runtime_defaults_detail::signature;
    using nnn::runtime_defaults_detail::word;
    if (bytes.size()<blocks[0].size) return false;
    const auto count=word(bytes,32);
    if (count<10 || count>blocks.size() || (version==2 && count>14) || (version==3 && count!=16)) return false;
    std::size_t offset=0;
    for (std::size_t i=0;i<count;++i) {
        const auto& block=blocks[i];
        const auto size=block.code==6 && version==1 ? 544U : block.size;
        if (size>bytes.size()-offset || word(bytes,offset)!=size || word(bytes,offset+4)!=block.code ||
            !signature(bytes,offset,block.name)) return false;
        offset+=size;
    }
    return offset==bytes.size();
}

std::optional<int> template_version(const fs::path& root,bool trial) {
    const auto directory=config_directory(root,"sav");
    if (directory.empty()) return {};
    const std::string base=trial ? "tsysfile" : "sysfile";
    std::optional<int> selected;
    for (int version=1;version<=3;++version) {
        const auto name=base+(version==1 ? std::string{} : std::to_string(version))+".org";
        try {
            const auto file=nnn::game_config::find_file(directory,name);
            if (file.empty() || !fs::is_regular_file(fs::symlink_status(file)) || fs::file_size(file)>147456 ||
                !valid_system_template(nnn::runtime_defaults_detail::read_file(file),version)) continue;
        } catch (const std::runtime_error&) {
            continue;
        }
        if (selected) return {}; // Multiple valid layouts leave the version undecided.
        selected=version;
    }
    return selected;
}

SystemSaveFormat system_format(const fs::path& root,const FileSaveLayout& files,std::optional<int> executable) {
    if (executable) return {*executable,files.trial,SystemSaveSource::Executable};
    if (const auto version=template_version(root,files.trial)) return {*version,files.trial,SystemSaveSource::Template};
    return {files.system_save_version,files.trial,SystemSaveSource::GameFiles};
}

CachedLayout analyze_layout(const fs::path& root,bool analyze_executable,
                            const std::function<FileSaveLayout()>& file_defaults) {
    CachedLayout layout{file_defaults(),std::nullopt,{},std::nullopt};
    if (analyze_executable) layout.executable=executable_save_layout(root);
    layout.system=system_format(root,layout.files,layout.executable ? layout.executable->system_save_version : std::nullopt);
    const bool okikae=layout.executable && layout.executable->registers_okikae ?
        *layout.executable->registers_okikae : layout.files.registers_okikae;
    const auto count=layout.files.configured_extensions+(okikae ? layout.files.configured_okikae_extensions+1U : 0U);
    if (count>max_extensions) throw std::runtime_error("Too many PC extensions");
    if (count==0) layout.extension_code_base=0;
    else if (layout.executable) layout.extension_code_base=layout.executable->extension_code_base;
    return layout;
}

fs::path store_layout(const fs::path& root,const std::array<fs::path,2>& directories,
                      const CachedLayout& layout) {
    for (const auto& directory:directories) if (!directory.empty()) {
        const auto file=directory/config_name;
        if (write_layout(file,layout)) return file;
    }
    if (!root.empty() && directories[0].empty() && directories[1].empty()) {
        const auto file=root/"init"/config_name;
        if (write_layout(file,layout)) return file;
    }
    return {};
}
} // namespace

DirectorySaveLayout directory_save_layout(const fs::path& root,bool analyze_executable,
                                          const std::function<FileSaveLayout()>& file_defaults) {
    const std::lock_guard<std::mutex> lock(layout_mutex);
    const auto directories=config_directories(root);
    if (auto cached=find_layout(directories)) {
        return layout_values(cached->cache);
    }
    const auto layout=analyze_layout(root,analyze_executable,file_defaults);
    store_layout(root,directories,layout);
    return layout_values(layout);
}

std::optional<SaveLayoutConfig> read_save_layout(const fs::path& root) {
    const std::lock_guard<std::mutex> lock(layout_mutex);
    const auto cached=find_layout(config_directories(root));
    if (!cached) return {};
    return SaveLayoutConfig{layout_values(cached->cache),cached->file};
}

std::optional<SaveLayoutConfig> regenerate_save_layout(const fs::path& root,
                                                       const std::function<FileSaveLayout()>& file_defaults) {
    const std::lock_guard<std::mutex> lock(layout_mutex);
    const auto directories=config_directories(root);
    const auto cached=find_layout(directories,false);
    const auto layout=analyze_layout(root,true,file_defaults);
    if (cached) {
        if (!write_layout(cached->file,layout)) return {};
        return SaveLayoutConfig{layout_values(layout),cached->file};
    }
    auto file=store_layout(root,directories,layout);
    if (file.empty()) return {};
    return SaveLayoutConfig{layout_values(layout),std::move(file)};
}
} // namespace emberfall::scene::pc
