#include "ManapiHttpMime.hpp"

#include "ManapiFilesystem.hpp"

const std::map <std::string, std::string> manapi::net::mime_by_extension = {
    {"txt", HTTP_MIME.TEXT_PLAIN},
    {"mp4", HTTP_MIME.VIDEO_MP4},
    {"js",  HTTP_MIME.TEXT_JS},
    {"mjs", HTTP_MIME.TEXT_JS},
    {"css", HTTP_MIME.TEXT_CSS},
    {"mp4", HTTP_MIME.VIDEO_MP4},
    {"mp3", HTTP_MIME.AUDIO_MP3},
    {"html", HTTP_MIME.TEXT_HTML},
    {"zip", HTTP_MIME.APPLICATION_ZIP},
    {"gzip", HTTP_MIME.APPLICATION_GZIP},
    {"tar", HTTP_MIME.APPLICATION_TAR},
    {"png", HTTP_MIME.IMAGE_PNG},
    {"jpeg", HTTP_MIME.IMAGE_JPEG},
    {"jpg", HTTP_MIME.IMAGE_JPEG},
    {"svg", HTTP_MIME.IMAGE_SVG},
    {"ttf", HTTP_MIME.FONT_TTF},
    {"woff", HTTP_MIME.FONT_WOFF},
    {"woff2", HTTP_MIME.FONT_WOFF2},
    {"otf", HTTP_MIME.FONT_OTF},
    {"webp", HTTP_MIME.IMAGE_WEBP},
    {"webm", HTTP_MIME.VIDEO_WEBM},
    {"weba", HTTP_MIME.AUDIO_WEBA},
    {"pdf", HTTP_MIME.APPLICATION_PDF},
    {"json", HTTP_MIME.APPLICATION_JSON},
    {"htm", HTTP_MIME.TEXT_HTML},
    {"gif", HTTP_MIME.IMAGE_GIF},
    {"bmp", HTTP_MIME.IMAGE_BMP},
    {"bin", HTTP_MIME.APPLICATION_OCTET_STREAM},
    {"",    HTTP_MIME.APPLICATION_OCTET_STREAM}
};

const std::set <std::string> manapi::net::mime_types_media = {"video", "audio", "image"};
const std::set <std::string> manapi::net::mimes_binary = {HTTP_MIME.APPLICATION_OCTET_STREAM, HTTP_MIME.APPLICATION_ZIP, HTTP_MIME.APPLICATION_GZIP, HTTP_MIME.APPLICATION_TAR, HTTP_MIME.APPLICATION_RAR};

const std::string_view manapi::net::type_mime(const std::string &mime) {
    auto it = mime.find('/');
    if (it == std::string::npos) { return std::string_view (mime.data(), 0); }
    return std::string_view (mime.data(), it);
}

bool manapi::net::mime_media (const std::string &mime) {
    // string_view cannot be used
    return mime_types_media.contains(std::string(type_mime(mime)));
}

bool manapi::net::mime_partitial_data(const std::string &mime) {
    // string_view cannot be used
    const auto type = std::string(type_mime(mime));
    return mime_types_media.contains(type) || mimes_binary.contains(mime);
}

const std::string & manapi::net::mime_by_file_path(const std::string &path) {
    const std::string extension = manapi::filesystem::extension(path);

    if (manapi::net::mime_by_extension.contains(extension))
    {
        return manapi::net::mime_by_extension.at(extension);
    }

    return manapi::net::mime_by_extension.at("bin");
}
