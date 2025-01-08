#pragma once

#include <string>
#include <map>
#include <set>

namespace manapi::net {
    static const struct {
        std::string TEXT_PLAIN                          = "text/plain";
        std::string TEXT_HTML                           = "text/html";
        std::string TEXT_CSS                            = "text/css";
        std::string TEXT_JS                             = "text/javascript";
        std::string TEXT_CSV                            = "text/csv";

        std::string APPLICATION_JS                      = "application/javascript";
        std::string APPLICATION_JSON                    = "application/json";
        std::string APPLICATION_JSON_LD                 = "application/ld+json";
        std::string APPLICATION_OCTET_STREAM            = "application/octet-stream";
        std::string APPLICATION_GZIP                    = "application/gzip";
        std::string APPLICATION_PDF                     = "application/pdf";
        std::string APPLICATION_RAR                     = "application/vnd.rar";
        std::string APPLICATION_SHELL                   = "application/x-sh";
        std::string APPLICATION_ZIP                     = "application/zip";
        std::string APPLICATION_TAR                     = "application/x-tar";

        std::string MULTIPART_FORM_DATA                 = "multipart/form-data";

        std::string APPLICATION_X_WWW_FORM_URLENCODED   = "application/x-www-form-urlencoded";

        std::string VIDEO_MP4                           = "video/mp4";
        std::string VIDEO_MPEG                          = "video/mpeg";
        std::string VIDEO_WEBM                          = "audio/webm";

        std::string AUDIO_MP3                           = "audio/mp3";
        std::string AUDIO_AAC                           = "audio/aac";
        std::string AUDIO_WAV                           = "audio/wav";
        std::string AUDIO_WEBA                          = "audio/webm";

        std::string IMAGE_GIF                           = "image/gif";
        std::string IMAGE_JPEG                          = "image/jpeg";
        std::string IMAGE_PNG                           = "image/png";
        std::string IMAGE_SVG                           = "image/svg+xml";
        std::string IMAGE_WEBP                          = "image/webp";
        std::string IMAGE_BMP                           = "image/bmp";

        std::string FONT_TTF                            = "font/ttf";
        std::string FONT_WOFF                           = "font/woff";
        std::string FONT_WOFF2                          = "font/woff2";
        std::string FONT_OTF                            = "font/otf";

    } HTTP_MIME;

    extern const std::map <std::string, std::string> mime_by_extension;
    extern const std::set <std::string> mime_types_media;
    extern const std::set <std::string> mimes_binary;

    const std::string_view type_mime (const std::string &mime);
    bool mime_media (const std::string &mime);
    bool mime_partitial_data (const std::string &mime);
    const std::string &mime_by_file_path (const std::string &path);
}