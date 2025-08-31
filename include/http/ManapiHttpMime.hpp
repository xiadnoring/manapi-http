/**
 * @file http/ManapiHttpMime.hpp
 * @brief That file provides functions to work with MIME types
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <map>
#include <set>

#include "../ManapiUtils.hpp"

namespace manapi::mime {
    static constexpr struct {
        std::string_view TEXT_PLAIN                          = "text/plain";
        std::string_view TEXT_HTML                           = "text/html";
        std::string_view TEXT_CSS                            = "text/css";
        std::string_view TEXT_JS                             = "text/javascript";
        std::string_view TEXT_CSV                            = "text/csv";

        std::string_view APPLICATION_JS                      = "application/javascript";
        std::string_view APPLICATION_JSON                    = "application/json";
        std::string_view APPLICATION_JSON_LD                 = "application/ld+json";
        std::string_view APPLICATION_OCTET_STREAM            = "application/octet-stream";
        std::string_view APPLICATION_GZIP                    = "application/gzip";
        std::string_view APPLICATION_PDF                     = "application/pdf";
        std::string_view APPLICATION_RAR                     = "application/vnd.rar";
        std::string_view APPLICATION_SHELL                   = "application/x-sh";
        std::string_view APPLICATION_ZIP                     = "application/zip";
        std::string_view APPLICATION_TAR                     = "application/x-tar";

        std::string_view MULTIPART_FORM_DATA                 = "multipart/form-data";

        std::string_view APPLICATION_X_WWW_FORM_URLENCODED   = "application/x-www-form-urlencoded";

        std::string_view VIDEO_MP4                           = "video/mp4";
        std::string_view VIDEO_MPEG                          = "video/mpeg";
        std::string_view VIDEO_WEBM                          = "audio/webm";

        std::string_view AUDIO_MP3                           = "audio/mp3";
        std::string_view AUDIO_AAC                           = "audio/aac";
        std::string_view AUDIO_WAV                           = "audio/wav";
        std::string_view AUDIO_WEBA                          = "audio/webm";

        std::string_view IMAGE_GIF                           = "image/gif";
        std::string_view IMAGE_JPEG                          = "image/jpeg";
        std::string_view IMAGE_PNG                           = "image/png";
        std::string_view IMAGE_SVG                           = "image/svg+xml";
        std::string_view IMAGE_WEBP                          = "image/webp";
        std::string_view IMAGE_BMP                           = "image/bmp";

        std::string_view FONT_TTF                            = "font/ttf";
        std::string_view FONT_WOFF                           = "font/woff";
        std::string_view FONT_WOFF2                          = "font/woff2";
        std::string_view FONT_OTF                            = "font/otf";

    } types;

    /**
     * MIME type classifier
     *
     * - For font/ttf it returns font
     * - For image/png it returns image
     *
     * Return empty string if '/' doesn't exists
     *
     * @param mime MIME
     * @return MIME class
     * 
     */
    DLLExportImport std::string_view mime_type (std::string_view mime);

    /**
     * MIME media type classifier
     *
     * @param mime MIME
     * @return true if the MIME type is a media type
     */
    DLLExportImport bool mime_media (std::string_view mime);

    /**
     * Recommends to use partitial methods
     * with provided MIME type.
     *
     * @param mime MIME type
     * @return true if it recommends to use partitial methods
     */
    DLLExportImport bool mime_partitial_data (std::string_view mime);

    /**
     * Get MIME type from the file path
     *
     * @param path the file path
     * @return MIME type
     */
    DLLExportImport std::string_view mime_by_file_path (std::string_view path);

    /**
     * Get MIME type by the file extension
     *
     * @param ext the file extension
     * @return MIME type
     */
    DLLExportImport std::string_view mime_by_file_extension (std::string_view ext);
}