#include "http/ManapiHttpMime.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "../include/ManapiUtils.hpp"

const std::map <std::string_view, std::string_view> mime_by_extension = {
    {"txt", manapi::mime::types.TEXT_PLAIN},
    {"mp4", manapi::mime::types.VIDEO_MP4},
    {"js",  manapi::mime::types.TEXT_JS},
    {"mjs", manapi::mime::types.TEXT_JS},
    {"css", manapi::mime::types.TEXT_CSS},
    {"mp4", manapi::mime::types.VIDEO_MP4},
    {"mp3", manapi::mime::types.AUDIO_MP3},
    {"html", manapi::mime::types.TEXT_HTML},
    {"zip", manapi::mime::types.APPLICATION_ZIP},
    {"gzip", manapi::mime::types.APPLICATION_GZIP},
    {"tar", manapi::mime::types.APPLICATION_TAR},
    {"png", manapi::mime::types.IMAGE_PNG},
    {"jpeg", manapi::mime::types.IMAGE_JPEG},
    {"jpg", manapi::mime::types.IMAGE_JPEG},
    {"svg", manapi::mime::types.IMAGE_SVG},
    {"ttf", manapi::mime::types.FONT_TTF},
    {"woff", manapi::mime::types.FONT_WOFF},
    {"woff2", manapi::mime::types.FONT_WOFF2},
    {"otf", manapi::mime::types.FONT_OTF},
    {"webp", manapi::mime::types.IMAGE_WEBP},
    {"webm", manapi::mime::types.VIDEO_WEBM},
    {"weba", manapi::mime::types.AUDIO_WEBA},
    {"pdf", manapi::mime::types.APPLICATION_PDF},
    {"json", manapi::mime::types.APPLICATION_JSON},
    {"htm", manapi::mime::types.TEXT_HTML},
    {"gif", manapi::mime::types.IMAGE_GIF},
    {"bmp", manapi::mime::types.IMAGE_BMP},
    {"bin", manapi::mime::types.APPLICATION_OCTET_STREAM},
    {"",    manapi::mime::types.APPLICATION_OCTET_STREAM}
};

const std::set <std::string_view> mime_types_media = {"video", "audio", "image"};
const std::set <std::string_view> mimes_binary = {manapi::mime::types.APPLICATION_OCTET_STREAM, manapi::mime::types.APPLICATION_ZIP, manapi::mime::types.APPLICATION_GZIP, manapi::mime::types.APPLICATION_TAR, manapi::mime::types.APPLICATION_RAR};

std::string_view manapi::mime::mime_type(std::string_view mime) {
    auto const it = mime.find('/');
    if (it == std::string::npos) { return std::string_view{}; }
    return {mime.data(), it};
}

bool manapi::mime::mime_media (std::string_view mime) {
    // string_view cannot be used
    return mime_types_media.contains(std::string(mime_type(mime)));
}

bool manapi::mime::mime_partitial_data(std::string_view mime) {
    // string_view cannot be used
    const auto type = std::string(mime_type(mime));
    return mime_types_media.contains(type) || mimes_binary.contains(mime);
}

std::string_view manapi::mime::mime_by_file_path(std::string_view path) {
    return mime_by_file_extension(manapi::filesystem::path::extension(path));
}

std::string_view manapi::mime::mime_by_file_extension(std::string_view ext) {
    auto const mit = mime_by_extension.find(ext);
    if (mit == mime_by_extension.end())
        return mime::types.APPLICATION_OCTET_STREAM;
    return mit->second;
}
