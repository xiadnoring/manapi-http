#include "ManapiHttpMime.hpp"

#include "ManapiFilesystem.hpp"

const std::map <std::string, std::string> manapi::mime::mime_by_extension = {
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

const std::set <std::string> manapi::mime::mime_types_media = {"video", "audio", "image"};
const std::set <std::string> manapi::mime::mimes_binary = {manapi::mime::types.APPLICATION_OCTET_STREAM, manapi::mime::types.APPLICATION_ZIP, manapi::mime::types.APPLICATION_GZIP, manapi::mime::types.APPLICATION_TAR, manapi::mime::types.APPLICATION_RAR};

const std::string_view manapi::mime::type_mime(const std::string &mime) {
    auto it = mime.find('/');
    if (it == std::string::npos) { return std::string_view (mime.data(), 0); }
    return std::string_view (mime.data(), it);
}

bool manapi::mime::mime_media (const std::string &mime) {
    // string_view cannot be used
    return mime_types_media.contains(std::string(type_mime(mime)));
}

bool manapi::mime::mime_partitial_data(const std::string &mime) {
    // string_view cannot be used
    const auto type = std::string(type_mime(mime));
    return mime_types_media.contains(type) || mimes_binary.contains(mime);
}

const std::string & manapi::mime::mime_by_file_path(const std::string &path) {
    const std::string extension = manapi::filesystem::path::extension(path);

    if (manapi::mime::mime_by_extension.contains(extension))
    {
        return manapi::mime::mime_by_extension.at(extension);
    }

    return manapi::mime::mime_by_extension.at("bin");
}
