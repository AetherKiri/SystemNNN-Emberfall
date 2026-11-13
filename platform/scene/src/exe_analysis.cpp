#include "pc_save.hpp"
#include "nnn/game_config.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace emberfall::scene::pc {
namespace {
using Bytes=std::vector<nnn::Byte>;
constexpr std::size_t max_executable_bytes=32U*1024U*1024U;
constexpr std::size_t max_directory_bytes=64U*1024U*1024U;

bool contains(std::size_t size,std::size_t offset,std::size_t length) {
    return offset<=size && length<=size-offset;
}

std::uint32_t word(const Bytes& bytes,std::size_t offset) {
    if (!contains(bytes.size(),offset,4)) throw std::runtime_error("Truncated PE field");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset+1])<<8U) |
        (std::uint32_t(bytes[offset+2])<<16U) | (std::uint32_t(bytes[offset+3])<<24U);
}

unsigned halfword(const Bytes& bytes,std::size_t offset) {
    if (!contains(bytes.size(),offset,2)) throw std::runtime_error("Truncated PE field");
    return unsigned(bytes[offset]) | (unsigned(bytes[offset+1])<<8U);
}

struct Section {
    std::size_t offset,size;
    std::uint32_t address;
    bool executable;
    bool includes(std::size_t at,std::size_t length) const {
        return at>=offset && contains(size,at-offset,length);
    }
};

std::vector<Section> sections(const Bytes& bytes) {
    if (bytes.size()<64 || bytes.size()>max_executable_bytes || halfword(bytes,0)!=0x5a4d) return {};
    const auto pe=static_cast<std::size_t>(word(bytes,60));
    if (!contains(bytes.size(),pe,24) || word(bytes,pe)!=0x4550 || halfword(bytes,pe+4)!=0x14c) return {};
    const auto characteristics=halfword(bytes,pe+22);
    if ((characteristics&0x0002U)==0 || (characteristics&0x2000U)!=0) return {}; // EXE image, excluding DLLs.
    const auto count=halfword(bytes,pe+6),optional_size=halfword(bytes,pe+20);
    const auto optional=pe+24;
    if (!count || count>96 || optional_size<96 || !contains(bytes.size(),optional,optional_size) ||
        halfword(bytes,optional)!=0x10b) return {};
    const auto table=optional+optional_size;
    if (!contains(bytes.size(),table,count*40U)) return {};
    const auto base=word(bytes,optional+28);
    const auto entry=std::uint64_t(base)+word(bytes,optional+16);
    std::vector<Section> result;
    bool executable_entry=false;
    for (unsigned i=0;i<count;++i) {
        const auto at=table+i*40U;
        const auto size=static_cast<std::size_t>(word(bytes,at+16));
        if (!size) continue;
        const auto offset=static_cast<std::size_t>(word(bytes,at+20));
        const auto address=std::uint64_t(base)+word(bytes,at+12);
        if (offset<table+count*40U || !contains(bytes.size(),offset,size) ||
            address+size>std::uint64_t(UINT32_MAX)+1U) return {};
        for (const auto& previous:result) {
            if (offset<previous.offset+previous.size && previous.offset<offset+size) return {};
            if (address<std::uint64_t(previous.address)+previous.size && previous.address<address+size) return {};
        }
        const bool executable=(word(bytes,at+36)&0x20000000U)!=0;
        if (executable && entry>=address && entry<address+size) executable_entry=true;
        result.push_back({offset,size,static_cast<std::uint32_t>(address),executable});
    }
    if (!executable_entry) return {};
    return result;
}

std::size_t modrm_length(const Bytes& bytes,const Section& code,std::size_t at) {
    if (!code.includes(at,1)) return 0;
    const unsigned modrm=bytes[at],mod=modrm>>6U,rm=modrm&7U;
    if (mod==3U) return 1;
    const std::size_t sib=rm==4U ? 1U : 0U;
    if (!code.includes(at,1+sib)) return 0;
    const bool no_base=mod==0U && (rm==5U || (sib && (bytes[at+1]&7U)==5U));
    const std::size_t disp=no_base || mod==2U ? 4U : (mod==1U ? 1U : 0U);
    return code.includes(at,1+sib+disp) ? 1+sib+disp : 0;
}

// A bounded, straight-line subset used only to follow a stack-local size.
// Unsupported instructions and control-flow joins leave the result unknown.
std::size_t size_instruction_length(const Bytes& bytes,const Section& code,std::size_t at) {
    if (!code.includes(at,1)) return 0;
    const auto op=bytes[at];
    if ((op>=0x40 && op<=0x5f) || op==0x90) return 1;
    if (op==0x6a) return code.includes(at,2) ? 2U : 0U;
    if (op==0x68 || op==0xe8 || (op>=0xb8 && op<=0xbf)) return code.includes(at,5) ? 5U : 0U;
    const bool wide=op==0x0f;
    if (wide && (!code.includes(at,2) || (bytes[at+1]!=0xb6 && bytes[at+1]!=0xb7 && bytes[at+1]!=0xbe && bytes[at+1]!=0xbf))) return 0;
    if (!wide && op!=0x8b && op!=0x89 && op!=0x8d && op!=0x31 && op!=0x33 &&
        op!=0x81 && op!=0x83 && op!=0xc7 && op!=0xff) return 0;
    const std::size_t prefix=wide ? 2U : 1U;
    const auto modrm=modrm_length(bytes,code,at+prefix);
    if (!modrm) return 0;
    if (op==0xff) {
        const auto group=(bytes[at+1]>>3U)&7U;
        if (group!=2 && group!=6) return 0; // CALL and PUSH only; no JMP.
    }
    if (op==0xc7 && (bytes[at+1]&0x38U)!=0) return 0;
    const std::size_t immediate=(op==0x81 || op==0xc7) ? 4U : op==0x83 ? 1U : 0U;
    const auto length=prefix+modrm+immediate;
    return code.includes(at,length) ? length : 0U;
}

std::optional<std::uint32_t> stack_size_constant(const Bytes& bytes,const Section& code,
                                               std::size_t end,nnn::Byte local) {
    const auto begin=end-code.offset>1024 ? end-1024 : code.offset;
    std::optional<std::size_t> prologue;
    for (auto at=begin;at+3<=end;++at)
        if (bytes[at]==0x55 && bytes[at+1]==0x8b && bytes[at+2]==0xec) prologue=at;
    if (!prologue) return {};
    std::optional<std::uint32_t> value;
    for (auto at=*prologue+3;at<end;) {
        const auto length=size_instruction_length(bytes,code,at);
        if (!length || length>end-at) return {};
        const auto op=bytes[at];
        // A changed EBP would make later stack displacements refer to another address.
        if (op==0x45 || op==0x4d || op==0x5d || op==0xbd) return {};
        const auto mr=length>1 ? bytes[at+(op==0x0f ? 2U : 1U)] : 0U;
        if (((op==0x8b || op==0x8d || op==0x33 || op==0x0f) && ((mr>>3U)&7U)==5) ||
            ((op==0x89 || op==0x31 || op==0x81 || op==0x83 || op==0xc7) && (mr&0xc7U)==0xc5U)) return {};
        if (length>=3 && op!=0x0f && ((mr&0xc7U)==0x45U || (mr&0xc7U)==0x85U)) {
            const auto offset=(mr&0xc0U)==0x40U ? static_cast<std::int32_t>(static_cast<std::int8_t>(bytes[at+2]))
                : static_cast<std::int32_t>(word(bytes,at+2));
            const auto target=static_cast<std::int32_t>(static_cast<std::int8_t>(local));
            if (op==0x8d && offset==target) return {}; // Address escapes; a call could overwrite the local.
            if (std::int64_t(offset)<target+4 && std::int64_t(target)<std::int64_t(offset)+4) {
                if (op==0xc7 && offset==target) value=word(bytes,at+length-4);
                else if (op==0xc7 || op==0x89 || op==0x31 || op==0x81 || op==0x83) value.reset();
            }
        }
        at+=length;
    }
    return value;
}

bool calls_make_header(const Bytes& bytes,const std::vector<Section>& image,const Section& code,std::size_t call);

// Early MSVC builds (e.g. MSVC 6/7.1) schedule basic-block ALU/MOV instructions
// between the four MakeHeader stack pushes (name, type, size, header) and CALL.
std::optional<std::uint32_t> scheduled_header_size(const Bytes& bytes,const Section& code,
                                                  std::size_t reference,unsigned type) {
    if (reference<=code.offset || !code.includes(reference,10) || bytes[reference-1]!=0x68) return {};
    std::size_t at=reference+4;
    bool seen_type=false,seen_dest=false;
    std::optional<std::uint32_t> pushed_size;
    for (unsigned step=0;step<32;++step) {
        if (!code.includes(at,1)) return {};
        const unsigned op=bytes[at];
        if (op==0xe8) return code.includes(at,5) && seen_type && pushed_size && seen_dest ? pushed_size : std::nullopt;
        if (op==0x6a) {
            if (!code.includes(at,2) || seen_type || bytes[at+1]!=type) return {};
            seen_type=true;at+=2;continue;
        }
        if (op==0x68) {
            if (!code.includes(at,5) || !seen_type || pushed_size) return {};
            pushed_size=word(bytes,at+1);at+=5;continue;
        }
        if (op>=0x50 && op<=0x57) {
            if (!seen_type || !pushed_size || seen_dest) return {};
            seen_dest=true;++at;continue;
        }
        if (op>=0x40 && op<=0x4f) { ++at;continue; }
        if (op==0x0d || op==0x25 || op==0x35) {
            if (!code.includes(at,5)) return {};
            at+=5;continue;
        }
        if (op==0x31 || op==0x33 || op==0x89 || op==0x8b || op==0x8d) {
            const auto len=modrm_length(bytes,code,at+1);
            if (!len) return {};
            at+=1+len;continue;
        }
        if (op==0x81 || op==0xc7) {
            const auto len=modrm_length(bytes,code,at+1);
            if (!len || !code.includes(at,1+len+4)) return {};
            at+=1+len+4;continue;
        }
        if (op==0x83) {
            const auto len=modrm_length(bytes,code,at+1);
            if (!len || !code.includes(at,1+len+1)) return {};
            at+=1+len+1;continue;
        }
        if (op==0x0f && code.includes(at,2) &&
            (bytes[at+1]==0xb6 || bytes[at+1]==0xb7 || bytes[at+1]==0xbe || bytes[at+1]==0xbf)) {
            const auto len=modrm_length(bytes,code,at+2);
            if (!len) return {};
            at+=2+len;continue;
        }
        if (op==0x66 && code.includes(at,2) && (bytes[at+1]==0x89 || bytes[at+1]==0x8b)) {
            const auto len=modrm_length(bytes,code,at+2);
            if (!len) return {};
            at+=2+len;continue;
        }
        return {};
    }
    return {};
}

// Older MSVC builds pass name, type and sizeof(block) to MakeHeader.
std::optional<std::uint32_t> called_header_size(const Bytes& bytes,const std::vector<Section>& image,const Section& code,
                                               std::size_t reference,unsigned type) {
    if (reference<=code.offset || !code.includes(reference,10) || bytes[reference-1]!=0x68) return {};
    if (bytes[reference+4]!=0x6a || bytes[reference+5]!=type)
        return scheduled_header_size(bytes,code,reference,type);
    const auto argument=reference+6;
    if (code.includes(argument,13) && bytes[argument]==0x68 &&
        bytes[argument+5]>=0x50 && bytes[argument+5]<=0x57 && bytes[argument+8]==0xe8) return word(bytes,argument+1);
    if (!code.includes(argument,4) || bytes[argument]!=0x8b || (bytes[argument+1]&0xc7U)!=0x45U ||
        bytes[argument+3]!=0x50U+((bytes[argument+1]>>3U)&7U))
        return scheduled_header_size(bytes,code,reference,type);
    const auto local=bytes[argument+2];
    // The pointer argument can be a member load or an address calculation;
    // don't assume that CALL is at one fixed offset after PUSH size.
    unsigned destinations=0;
    for (auto at=argument+4;at-argument<48;) {
        if (!code.includes(at,1)) break;
        if (bytes[at]==0xe8) {
            if (destinations==1 && calls_make_header(bytes,image,code,at))
                if (const auto size=stack_size_constant(bytes,code,reference-1,local)) return size;
            break;
        }
        if (bytes[at]>=0x50 && bytes[at]<=0x57) ++destinations;
        else if (bytes[at]!=0x8b && bytes[at]!=0x8d && bytes[at]!=0x83) break;
        const auto length=size_instruction_length(bytes,code,at);
        if (!length || destinations>1) break;
        at+=length;
    }
    return {};
}

bool stores_name(const Bytes& bytes,const Section& code,std::size_t reference,unsigned base) {
    const auto next=reference+4;
    if (!code.includes(next,5)) return false;
    if (reference>code.offset && bytes[reference-1]==0xa1)
        return bytes[next]==0x89 && bytes[next+1]==0x40U+base && bytes[next+2]==16;
    // MOVQ xmmN,[name], followed by MOVQ [header+16],xmmN.
    if (reference>=code.offset+4 && bytes[reference-4]==0xf3 && bytes[reference-3]==0x0f &&
        bytes[reference-2]==0x7e && (bytes[reference-1]&0xc7U)==5) {
        const auto reg=(bytes[reference-1]>>3U)&7U;
        return bytes[next]==0x66 && bytes[next+1]==0x0f && bytes[next+2]==0xd6 &&
            bytes[next+3]==0x40U+(reg<<3U)+base && bytes[next+4]==16;
    }
    return false;
}

// Optimized builds inline MakeHeader and copy the name into that header.
std::optional<std::uint32_t> inline_header_size(const Bytes& bytes,const Section& code,
                                               std::size_t reference,unsigned type) {
    const auto begin=reference-code.offset>64 ? reference-64 : code.offset;
    for (auto at=begin;at+27<=reference;++at) {
        if (bytes[at]!=0xc7 || bytes[at+1]>7 || bytes[at+1]==4 || bytes[at+1]==5) continue;
        const auto base=bytes[at+1];
        if (bytes[at+6]!=0xc7 || bytes[at+7]!=0x40U+base || bytes[at+8]!=4 || word(bytes,at+9)!=type ||
            bytes[at+13]!=0xc7 || bytes[at+14]!=0x40U+base || bytes[at+15]!=8 || word(bytes,at+16)!=0 ||
            bytes[at+20]!=0xc7 || bytes[at+21]!=0x40U+base || bytes[at+22]!=12 || word(bytes,at+23)!=0 ||
            !stores_name(bytes,code,reference,base)) continue;
        return word(bytes,at+2);
    }
    return {};
}

struct BlockSize {
    std::optional<std::uint32_t> size;
    bool ambiguous=false;
};

BlockSize written_block_size(const Bytes& bytes,const std::vector<Section>& image,std::string_view name,
                             unsigned type) {
    BlockSize result;
    std::size_t names=0;
    for (const auto& data:image) {
        const std::string_view data_bytes(reinterpret_cast<const char*>(bytes.data()+data.offset),data.size);
        for (auto offset=data_bytes.find(name);offset!=data_bytes.npos;offset=data_bytes.find(name,offset+1)) {
            if (++names>32) return {std::nullopt,true};
            const auto address=data.address+static_cast<std::uint32_t>(offset);
            std::array<nnn::Byte,4> pointer{};
            for (unsigned i=0;i<4;++i) pointer[i]=static_cast<nnn::Byte>(address>>(8U*i));
            const std::string_view reference(reinterpret_cast<const char*>(pointer.data()),pointer.size());
            for (const auto& code:image) {
                if (!code.executable) continue;
                const std::string_view code_bytes(reinterpret_cast<const char*>(bytes.data()+code.offset),code.size);
                for (auto ref=code_bytes.find(reference);ref!=code_bytes.npos;ref=code_bytes.find(reference,ref+1)) {
                    const auto at=code.offset+ref;
                    auto size=called_header_size(bytes,image,code,at,type);
                    if (!size) size=inline_header_size(bytes,code,at,type);
                    if (!size) continue;
                    if (result.size && result.size!=size) return {std::nullopt,true};
                    result.size=size;
                }
            }
        }
    }
    return result;
}
enum class LayerLayout { Unknown, Classic, Expanded, Conflict };

LayerLayout image_layer_layout(const Bytes& bytes,const std::vector<Section>& image) {
    const auto effects=written_block_size(bytes,image,std::string_view("EFFECT\0",7),5);
    const auto filenames=written_block_size(bytes,image,std::string_view("EFFECTFILENAME\0",15),6);
    if (effects.ambiguous || filenames.ambiguous) return LayerLayout::Conflict;
    if (effects.size==header_size+classic_layer_count*effect_size &&
        filenames.size==header_size+classic_layer_count*filename_size) return LayerLayout::Classic;
    if (effects.size==header_size+layer_count*effect_size &&
        filenames.size==header_size+layer_count*filename_size) return LayerLayout::Expanded;
    if (effects.size || filenames.size) return LayerLayout::Conflict;
    return LayerLayout::Unknown;
}

// The upstream CAutoSaveDataList stores fixed 32-byte names, including a
// reserved first entry and an empty sentinel. A loose string match can also
// find the unrelated configuration key named okikae.
std::optional<bool> image_okikae(const Bytes& bytes,const std::vector<Section>& image) {
    std::array<char,64> prefix{};
    std::copy_n("dummy",5,prefix.begin());
    std::copy_n("face",4,prefix.begin()+32);
    std::array<char,32> okikae{};
    std::copy_n("okikae",6,okikae.begin());
    const std::array<char,32> empty{};
    const std::string_view first_two(prefix.data(),prefix.size());
    const std::string_view terminator(empty.data(),empty.size());
    const std::string_view name(okikae.data(),okikae.size());
    std::optional<bool> found;
    for (const auto& section:image) {
        if (section.executable) continue;
        const std::string_view data(reinterpret_cast<const char*>(bytes.data()+section.offset),section.size);
        for (auto pos=data.find(first_two);pos!=data.npos;pos=data.find(first_two,pos+1)) {
            const auto third=data.substr(pos+first_two.size(),32);
            if (found) return {};
            if (third==terminator) found=false;
            else if (third==name && data.substr(pos+first_two.size()+32,32)==terminator) found=true;
            else return {};
        }
    }
    return found;
}

bool loads_system_stem(const Bytes& bytes,const std::vector<Section>& image,std::string_view stem) {
    unsigned matches=0;
    for (const auto& data:image) {
        if (data.executable) continue;
        const std::string_view strings(reinterpret_cast<const char*>(bytes.data()+data.offset),data.size);
        for (auto pos=strings.find(stem);pos!=strings.npos;pos=strings.find(stem,pos+1)) {
            if (++matches>32) return false;
            if (pos && strings[pos-1]!=0) continue;
            const auto address=data.address+static_cast<std::uint32_t>(pos);
            std::array<char,4> pointer{};
            for (unsigned i=0;i<4;++i) pointer[i]=static_cast<char>(address>>(8U*i));
            const std::string_view reference(pointer.data(),pointer.size());
            for (const auto& code:image) {
                if (!code.executable) continue;
                const std::string_view instructions(reinterpret_cast<const char*>(bytes.data()+code.offset),code.size);
                for (auto ref=instructions.find(reference);ref!=instructions.npos;ref=instructions.find(reference,ref+1)) {
                    const auto at=code.offset+ref;
                    if (ref && bytes[at-1]>=0xb8 && bytes[at-1]<=0xbf) return true;
                    // Older constructors store the address directly in the
                    // save-name member: MOV dword ptr [register+disp32],imm32.
                    if (ref>=6 && bytes[at-6]==0xc7 && (bytes[at-5]&0xf8U)==0x80U &&
                        (bytes[at-5]&7U)!=4U) return true;
                }
            }
        }
    }
    return false;
}

std::optional<int> image_system_save_version(const Bytes& bytes,const std::vector<Section>& image) {
    // The constructor references the normal and trial names of one
    // generation. Other generation names may still be present in data.
    constexpr std::array<std::string_view,3> normal{{{"sysfile\0",8},{"sysfile2\0",9},{"sysfile3\0",9}}};
    constexpr std::array<std::string_view,3> trial{{{"tsysfile\0",9},{"tsysfile2\0",10},{"tsysfile3\0",10}}};
    std::optional<int> version;
    for (std::size_t i=0;i<normal.size();++i) {
        if (!loads_system_stem(bytes,image,normal[i]) || !loads_system_stem(bytes,image,trial[i])) continue;
        if (version) return {};
        version=static_cast<int>(i+1);
    }
    return version;
}

// Match bounded compiler instruction sequences, not game names, status sizes,
// or loose constants. -1 denotes an address/displacement operand.
bool instructions(const Bytes& bytes,const Section& code,std::size_t at,std::initializer_list<int> pattern) {
    if (!code.includes(at,pattern.size())) return false;
    for (int value:pattern) {
        if (value>=0 && bytes[at]!=value) return false;
        ++at;
    }
    return true;
}

bool calls_make_header(const Bytes& bytes,const std::vector<Section>& image,const Section& code,std::size_t call) {
    if (!code.includes(call,5) || bytes[call]!=0xe8) return false;
    const auto target=static_cast<std::int64_t>(code.address)+static_cast<std::int64_t>(call-code.offset)+5+
        static_cast<std::int32_t>(word(bytes,call+1));
    for (const auto& callee:image) {
        if (!callee.executable || target<callee.address || target>=static_cast<std::int64_t>(callee.address)+static_cast<std::int64_t>(callee.size)) continue;
        const auto at=callee.offset+static_cast<std::size_t>(target-callee.address);
        // MakeHeader(header, size, type, name): size -> +0, type -> +4.
        return instructions(bytes,callee,at,{0x8b,0x44,0x24,4,0x8b,0x4c,0x24,8,0x8b,0x54,0x24,12,0x56,0x89,8,0x89,0x50,4}) ||
            instructions(bytes,callee,at,{0x55,0x8b,0xec,0x83,0xec,12,0x89,0x4d,0xf4,0x8b,0x45,8,0x89,0x45,0xf8,
                0x8b,0x4d,0xf8,0x8b,0x55,12,0x89,0x11,0x8b,0x45,0xf8,0x8b,0x4d,16,0x89,0x48,4});
    }
    return false;
}

std::optional<std::uint32_t> extension_writer_base(const Bytes& bytes,const std::vector<Section>& image,
                                                  const Section& code,std::size_t ref) {
    if (ref<code.offset+7) return {};
    // Unoptimized MSVC: EXT(index+1), then MakeHeader(...,index+base,...).
    if (instructions(bytes,code,ref-7,{0x8b,0x55,8,0x83,0xc2,1,0x52}) &&
        instructions(bytes,code,ref+5,{0x8d,0x45,0xd8,0x50,0xff,0x15,-1,-1,-1,-1,0x83,0xc4,12,
            0x8d,0x4d,0xd8,0x51,0x8b,0x55,8,0x83,0xc2,-1,0x52,0x8b,0x45,0xd4,0x50,
            0x8b,0x4d,0xd0,0x8b,0x91,-1,-1,-1,-1,0x52,0x8b,0x4d,0xd0,0xe8}) &&
        calls_make_header(bytes,image,code,ref+46)) return bytes[ref+27];
    // Older optimized MSVC keeps the zero-based index in EBP.
    if (ref>=code.offset+8 && instructions(bytes,code,ref-8,{0x8d,0x45,1,0x8d,0x4c,0x24,16,0x50}) &&
        instructions(bytes,code,ref+5,{0x51,0xff,0x15,-1,-1,-1,-1,0x8b,0x8e,-1,-1,-1,-1,
            0x83,0xc4,12,0x8d,0x54,0x24,16,0x8d,0x45,-1,0x52,0x50,0x53,0x51,0x8b,0xce,0xe8}) &&
        calls_make_header(bytes,image,code,ref+34)) return bytes[ref+27];
    // Inlined header: index in EBX, name buffer EBP-36, type stored at +4.
    if (instructions(bytes,code,ref-7,{0x8d,0x43,1,0x50,0x8d,0x45,0xdc}) &&
        instructions(bytes,code,ref+5,{0x50,0xff,0x15,-1,-1,-1,-1,0x8b,0x8e,-1,-1,-1,-1,
            0x8d,0x43,-1,0x89,0x41,4,0x8d,0x51,16,0x89,0x39,
            0xc7,0x41,8,0,0,0,0,0xc7,0x41,12,0,0,0,0,0x8d,0x45,0xdc})) return bytes[ref+20];
    // Newer optimized MSVC computes type first: ESI=index+base;
    // wsprintf gets ESI+(1-base). Verify both arithmetic operands agree.
    if (ref>=code.offset+10 && instructions(bytes,code,ref-10,{0x8d,0x73,-1,0x8d,0x46,-1,0x50,0x8d,0x45,0xdc}) &&
        instructions(bytes,code,ref+5,{0x50,0xff,0x15,-1,-1,-1,-1,0x8b,0x87,-1,-1,-1,-1,0x0f,0x57,0xc0,
            0x8b,0x8d,-1,-1,-1,-1,0x83,0xc4,24,0x89,0x70,4,0x8d,0x50,16,0x89,8,
            0xc7,0x40,8,0,0,0,0,0xc7,0x40,12,0,0,0,0,0x8d,0x45,0xdc,0x0f,0x11,2}) &&
        static_cast<int>(bytes[ref-8])+static_cast<std::int8_t>(bytes[ref-5])==1) return bytes[ref-8];
    return {};
}

std::optional<std::uint32_t> image_extension_base(const Bytes& bytes,const std::vector<Section>& image) {
    std::optional<std::uint32_t> result;
    unsigned names=0,references=0;
    for (const auto& data:image) {
        if (data.executable) continue;
        const std::string_view strings(reinterpret_cast<const char*>(bytes.data()+data.offset),data.size);
        const std::string_view format("EXT%d\0",6);
        for (auto pos=strings.find(format);pos!=strings.npos;pos=strings.find(format,pos+1)) {
            if (++names>32) return {};
            const auto address=data.address+static_cast<std::uint32_t>(pos);
            std::array<char,5> push{{0x68,0,0,0,0}};
            for (unsigned i=0;i<4;++i) push[i+1]=static_cast<char>(address>>(8U*i));
            for (const auto& code:image) {
                if (!code.executable) continue;
                const std::string_view instructions_view(reinterpret_cast<const char*>(bytes.data()+code.offset),code.size);
                const std::string_view needle(push.data(),push.size());
                for (auto ref=instructions_view.find(needle);ref!=instructions_view.npos;ref=instructions_view.find(needle,ref+1)) {
                    if (++references>128) return {};
                    const auto base=extension_writer_base(bytes,image,code,code.offset+ref);
                    if (!base) continue;
                    if (*base<min_legacy_extension_code || *base>127 || (result && *result!=*base)) return {};
                    result=base;
                }
            }
        }
    }
    return result;
}
} // namespace

ExecutableSaveLayout executable_save_layout(const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::directory_iterator entries(root,error);
    if (error) return {};
    ExecutableSaveLayout result;
    std::size_t candidates=0,total_bytes=0;
    for (const auto end=std::filesystem::directory_iterator{};entries!=end;entries.increment(error)) {
        if (error) return {};
        const auto& file=*entries;
        if (nnn::game_config::ascii_lower(file.path().extension().u8string())!=".exe" || !file.is_regular_file(error)) continue;
        if (++candidates>32) return {};
        const auto size=file.file_size(error);
        if (error || size<64 || size>max_executable_bytes) { error.clear();continue; }
        if (size>max_directory_bytes-total_bytes) return {};
        total_bytes+=static_cast<std::size_t>(size);
        std::ifstream input(file.path(),std::ios::binary);
        if (!input) continue;
        Bytes bytes(static_cast<std::size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())) ||
            input.peek()!=std::ifstream::traits_type::eof() || input.bad()) continue;
        try {
            const auto image=sections(bytes);
            const auto layout=image_layer_layout(bytes,image);
            if (layout==LayerLayout::Unknown) continue;
            if (layout==LayerLayout::Conflict) return {};
            const auto count=static_cast<unsigned>(layout==LayerLayout::Classic ? classic_layer_count : layer_count);
            if (result.layer_records && *result.layer_records!=count) return {};

            const auto message=written_block_size(bytes,image,std::string_view("MESSAGE\0",8),7);
            const auto width=!message.ambiguous && message.size ?
                message_width_from_block_size(*message.size) : std::nullopt;
            const auto status=written_block_size(bytes,image,std::string_view("GAMESTATUS\0",11),2);
            const auto status_bytes=!status.ambiguous && status.size && supported_status_size(*status.size)
                ? std::optional<std::size_t>{*status.size} : std::nullopt;
            const auto okikae=image_okikae(bytes,image);
            const auto system_version=image_system_save_version(bytes,image);
            const auto extension_base=image_extension_base(bytes,image);
            if (!result.layer_records) {
                result.message_width=width;
                result.status_bytes=status_bytes;
                result.registers_okikae=okikae;
                result.system_save_version=system_version;
                result.extension_code_base=extension_base;
            } else {
                if (result.message_width!=width) result.message_width.reset();
                if (result.status_bytes!=status_bytes) result.status_bytes.reset();
                if (result.registers_okikae!=okikae) result.registers_okikae.reset();
                if (result.system_save_version!=system_version) result.system_save_version.reset();
                if (result.extension_code_base!=extension_base) result.extension_code_base.reset();
            }
            result.layer_records=count;
        } catch (const std::runtime_error&) {
            // Ignore an image whose headers cannot be read safely.
        }
    }
    if (error) return {};
    return result;
}
} // namespace emberfall::scene::pc
