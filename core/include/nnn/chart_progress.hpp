#pragma once

#include "nnn/script.hpp"
#include <vector>

namespace nnn::chart {

inline constexpr std::size_t max_progress_slots=256U*1024U;
inline constexpr std::size_t move_road_bytes(std::size_t slots) noexcept {
    return (slots+15U)&~std::size_t{15};
}
inline constexpr std::size_t selection_words(std::size_t slots) noexcept {
    return (((slots+31U)/32U)+3U)&~std::size_t{3};
}
inline constexpr std::size_t max_selection_words=selection_words(max_progress_slots);
enum class StorageLayout { Standard, LegacyChart };
inline constexpr std::size_t legacy_slot_capacity=10000,legacy_selection_words=320,legacy_seen_bytes=10004;
inline constexpr bool valid_progress_extent(std::size_t slots,std::size_t seen,StorageLayout layout) noexcept {
    return layout==StorageLayout::LegacyChart ? slots==legacy_slot_capacity && seen==legacy_seen_bytes :
        slots<=max_progress_slots && seen==((slots+3U)&~std::size_t{3});
}
inline constexpr bool valid_selection_extent(std::size_t slots,std::size_t words,StorageLayout layout) noexcept {
    return layout==StorageLayout::LegacyChart ? slots==legacy_slot_capacity && words==legacy_selection_words :
        slots>0 && slots<=max_progress_slots && words==selection_words(slots);
}

// One byte per source node in each of the original mita/tsuka tables.
struct Progress {
    std::vector<Byte> seen,traversed;
};
struct SelectionSeed {
    std::size_t slots;
    std::vector<Word> enabled;
};

} // namespace nnn::chart
