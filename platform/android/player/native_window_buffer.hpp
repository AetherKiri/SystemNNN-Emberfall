#pragma once

#include <android/native_window.h>
#include <cstddef>
#include <cstring>
#include <limits>

namespace emberfall::android {

inline bool rgba_window_buffer(const ANativeWindow_Buffer& buffer,int width,int height) noexcept {
    return buffer.bits && width>0 && height>0 && buffer.width==width && buffer.height==height &&
        buffer.stride>=width && buffer.stride<=std::numeric_limits<int>::max()/4 &&
        buffer.format==WINDOW_FORMAT_RGBA_8888;
}

// A locked buffer must be returned even when geometry renegotiation is needed.
// Only touch formats whose storage size is known; an unknown format is never acknowledged as a frame.
inline void clear_window_buffer(const ANativeWindow_Buffer& buffer) noexcept {
    const std::size_t pixel_bytes=buffer.format==WINDOW_FORMAT_RGB_565 ? 2U :
        (buffer.format==WINDOW_FORMAT_RGBA_8888 || buffer.format==WINDOW_FORMAT_RGBX_8888) ? 4U : 0U;
    if (!pixel_bytes || !buffer.bits || buffer.width<=0 || buffer.height<=0 || buffer.stride<buffer.width) return;
    const auto stride=static_cast<std::size_t>(buffer.stride),height=static_cast<std::size_t>(buffer.height);
    if (stride>std::numeric_limits<std::size_t>::max()/pixel_bytes/height) return;
    std::memset(buffer.bits,0,stride*height*pixel_bytes);
}

} // namespace emberfall::android
