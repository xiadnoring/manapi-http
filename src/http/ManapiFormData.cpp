#include <memory.h>

#include "ManapiString.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "encoding/ManapiURL.hpp"
#include "http/ManapiFormData.hpp"
#include "http/ManapiHttpUtils.hpp"
#include "http/ManapiHttpMime.hpp"
#include "http/ManapiHttpTypes.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/http/ManapiHttp1.hpp"

const std::string SPECIAL_SYMBOLS_BOUNDARY = "\r\n--";

constexpr int boundary_payload_size = 32;
constexpr char boundary_end_symbols[] = "--";
constexpr char nline[] = "\r\n";

enum data_type {
    DATA_NONE = 0,
    DATA_FILE = 1,
    DATA_PLAIN = 2
};

enum content_type {
    CONTENT_TYPE_NONE = 0,
    CONTENT_TYPE_MULTIPART_FORM_DATA = 1,
    CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED = 2
};

enum form_data_mulitpart_states {
    FORMDATA_MULTI_INIT = 0,
    FORMDATA_MULTI_BOUNDARY,
    FORMDATA_MULTI_HEADER_KEY,
    FORMDATA_MULTI_HEADER_VALUE,
    FORMDATA_MULTI_R,
    FORMDATA_MULTI_N,
    FORMDATA_MULTI_R_OR_FIN,
    FORMDATA_MULTI_DATA,
    FORMDATA_MULTI_FINISH,
    FORMDATA_MULTI_ERR
};

enum form_data_urlencoded_states {
    FORMDATA_URLEN_INIT = 0,
    FORMDATA_URLEN_KEY,
    FORMDATA_URLEN_VALUE,
    FORMDATA_URLEN_ERR
};

struct manapi::net::formdata_recv::formdata_recv_headers_t {
    std::string s1, s2;
};

manapi::net::formdata_recv::formdata_recv(onrecv_cb_t onrecv_cb) : ctx_(), onrecv_cb_(std::move(onrecv_cb)) {
}

manapi::net::formdata_recv::~formdata_recv() = default;

manapi::net::formdata_recv::formdata_recv(formdata_recv &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::net::formdata_recv & manapi::net::formdata_recv::operator=(formdata_recv &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::future<manapi::status> manapi::net::formdata_recv::get(std::string_view content_type, onparam_cb_t cb) {
    assert(this->onparam_cb_ == nullptr);
    manapi::status status;

    try {
        this->onparam_cb_ = std::move(cb);
        int type = CONTENT_TYPE_NONE;

        {
            if (content_type.empty())
                co_return status_invalid_argument("formdata:Content-Type header is missing");

            auto rhs = http::parse_header_value(content_type);
            auto hparams = rhs.unwrap();
            if (hparams.size() != 1) {
                status = status_invalid_argument("formdata:Content-Type header is invalid");
                goto finish;
            }

            if (manapi::string::equals(hparams[0].value, mime::types.MULTIPART_FORM_DATA, 0b10)) {
                auto pit = hparams[0].params.find("boundary");
                if (pit == hparams[0].params.end()) {
                    status =  status_invalid_argument("formdata:boundary is missing");
                    goto finish;
                }

                this->ctx_.boundary = "\r\n--" + pit->second;

                type = CONTENT_TYPE_MULTIPART_FORM_DATA;
            }
            else if (manapi::string::equals(hparams[0].value, mime::types.APPLICATION_X_WWW_FORM_URLENCODED, 0b10)) {
                type = CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED;
            }
            else {
                status = status_invalid_argument("formdata:FormData is not supported for the current Content-Type");
                goto finish;
            }
        }

        switch (type) {
            case CONTENT_TYPE_MULTIPART_FORM_DATA: {
                status = co_await this->onrecv_cb_ ([this] (slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                        co_return co_await this->onrecv_multipart_(buffs);
                });

                if (!status)
                    goto finish;

                break;
            }
            case CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED: {
                status = co_await this->onrecv_cb_ ([this] (slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                        return this->onrecv_urlencoded_(buffs);
                });

                if (!status)
                    goto finish;

                if (this->ctx_.current != FORMDATA_URLEN_VALUE
                    && this->ctx_.current != FORMDATA_URLEN_INIT) {
                    status = status_invalid_argument("formdata:x-www-form-urlencoded data is not complete");
                    goto finish;
                }

                if (this->ctx_.current == FORMDATA_URLEN_VALUE) {
                    auto ucb = this->onparam_cb_ (std::move(this->ctx_.hctx->s1));
                    if (ucb) {
                        auto const size = this->ctx_.hctx->s2.size();

                        slice b;

                        b.push_back(this->ctx_.hctx->s2.data(), size);

                        if (static_cast<ssize_t>(size) != co_await ucb (slice_view(b), true)) {
                            status = status_invalid_argument("formdata:user callback returned an invalid result");
                            goto finish;
                        }

                        this->ctx_.hctx->s2.resize(0);
                    }
                }

                break;
            }
        }

        status = manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "formdata:Failed", e.what());
        status = manapi::status_internal("formdata:Failed");
    }

finish:
    this->onparam_cb_ = nullptr;
    this->ondata_cb_ = nullptr;
    co_return std::move(status);
}

manapi::net::formdata_recv::ondata_cb_t manapi::net::formdata_recv::save_file(std::string file, int mode, ssize_t maxlen, manapi::ctoken cancellation) {
    auto status = manapi::fs::fstream::create (std::move(file), std::move(cancellation));

    if (!status)
        return nullptr;

    return [maxlen, mode, stream = status.unwrap()] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
        if (maxlen >= 0) {
            maxlen -= static_cast<ssize_t>(buffs.size());

            if (maxlen < 0)
                co_return -1;
        }

        while (true) {
            if (stream->is_open())
                co_return co_await stream->fwrite(buffs);


            auto res = co_await stream->open(ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_NONBLOCK, mode);
            if (!res.ok())
                co_return -1;
        }
    };
}

manapi::net::formdata_recv::ondata_cb_t manapi::net::formdata_recv::save_string(std::string *str, ssize_t maxlen) {
    return [maxlen, str] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
        if (maxlen >= 0) {
            maxlen -= static_cast<ssize_t>(buffs.size());

            if (maxlen < 0)
                co_return -1;
        }

        auto const cursor = str->size();
        str->resize(buffs.size() + cursor);
        buffs.copy_to(str->data() + cursor, 0, buffs.size());

        co_return static_cast<ssize_t>(buffs.size());
    };
}

manapi::net::formdata_recv::ondata_cb_t manapi::net::formdata_recv::skip(ssize_t maxlen) {
    return [maxlen] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
        if (maxlen >= 0) {
            maxlen -= static_cast<ssize_t>(buffs.size());

            if (maxlen < 0)
                co_return -1;
        }
        co_return static_cast<ssize_t>(buffs.size());
    };
}

manapi::future<ssize_t> manapi::net::formdata_recv::onrecv_multipart_(slice_view buffs) {
    try {
        std::size_t total = 0;
        slice_ref slice_transfer;

        for (auto buff_it = buffs.begin(); buff_it != buffs.end(); ++buff_it) {
            std::size_t pos = 0;
            std::size_t const size = buff_it.size();
            auto buffer = static_cast<const char *>(buff_it.buffer());

            while (pos != size) {
                repeat: switch (this->ctx_.current) {
                    case FORMDATA_MULTI_INIT: {
                        this->ctx_.current = FORMDATA_MULTI_BOUNDARY;

                        this->ctx_.n1 = 2;
                        this->ctx_.n2 = 0;


                        break;
                    }
                    case FORMDATA_MULTI_BOUNDARY: {
                        auto const copy = std::min<uint32_t>(static_cast<uint32_t>(this->ctx_.boundary.size()) - this->ctx_.n1,
                            static_cast<uint32_t>(size - pos));

                        if (copy) {
                            if (0 != strncmp (this->ctx_.boundary.data() + this->ctx_.n1, buffer + pos, copy)) {
                                co_return -1;
                            }
                        }

                        this->ctx_.n1 += copy;
                        pos += copy;

                        if (this->ctx_.n1 == this->ctx_.boundary.size()) {
                            this->ctx_.n1 = 0;
                            this->ctx_.current = FORMDATA_MULTI_R_OR_FIN;
                        }

                        break;
                    }
                    case FORMDATA_MULTI_HEADER_KEY: {
                        while (pos < size) {
                            if (!this->ctx_.hctx->s1.empty() && this->ctx_.hctx->s1.back() == '\r') {
                                /**
                                 * RFC7230 (3.2.4) Field Parsing
                                 * Historically, HTTP header field values could be extended over multiple
                                 * lines by preceding each extra line with at least one space or horizontal tab (obs-fold).
                                 */
                                if (buffer[pos] == '\n') {
                                    if (this->ctx_.hctx->s1.size() != 1) {
                                        co_return -1;
                                    }

                                    this->ctx_.hctx->s1.pop_back();

                                    pos ++;

                                    this->ctx_.n2 = static_cast<uint32_t>(pos);

                                    auto hit = this->ctx_.headers->find(http::H_CONTENT_DISPOSITION);
                                    if (hit == this->ctx_.headers->end()) {
                                        co_return -1;
                                    }

                                    auto rhs = http::parse_header_value(hit->second);
                                    auto hparams = rhs.unwrap();

                                    if (hparams.size() != 1
                                        || hparams[0].value != "form-data")
                                        co_return -1;

                                    auto nit = hparams[0].params.find("name");
                                    if (nit == hparams[0].params.end())
                                        co_return -1;

                                    this->ondata_cb_ = this->onparam_cb_ (std::move(nit->second));

                                    this->ctx_.current = FORMDATA_MULTI_DATA;
                                    this->ctx_.next = FORMDATA_MULTI_ERR;

                                    goto repeat;
                                }

                                co_return -1;
                            }

                            if (buffer[pos] == '\r') {
                                this->ctx_.hctx->s1.push_back(buffer[pos]);
                                pos++;
                                continue;
                            }

                            if (buffer[pos] == ':') {
                                /**
                                 * RFC7230 (3.2) Header Fields
                                 * Each header field consists of a case-insensitive field name followed by a colon (":"),
                                 * optional leading whitespace, the field value, and optional trailing whitespace.
                                 */

                                if (this->ctx_.hctx->s1 != http::H_CONTENT_DISPOSITION
                                    && this->ctx_.hctx->s1 != http::H_CONTENT_TYPE)
                                    co_return -1;

                                pos++;
                                this->ctx_.current = FORMDATA_MULTI_HEADER_VALUE;

                                break;
                            }

                            if (!http::http_v1_1_is_token_char(buffer[pos])) {
                                /**
                                 * RFC7230 (3.2.4) Field Parsing
                                 * No whitespace is allowed between the header field-name and colon.
                                 *
                                 * RFC7230 (3.2.6) Field Value Components
                                 * Most HTTP header field values are defined using
                                 * common syntax components (token, quoted-string, and comment)
                                 * separated by whitespace or specific delimiting characters.
                                 * Delimiters are chosen from the set of US-ASCII visual characters
                                 * not allowed in a token (DQUOTE and "(),/:;<=>?@[\]{}").
                                 */
                                co_return -1;
                            }

                            if (this->ctx_.hctx->s1.size() > 24)
                                co_return -1;

                            this->ctx_.hctx->s1.push_back(static_cast<char>(std::tolower(buffer[pos])));
                            pos++;
                        }
                        break;
                    }
                    case FORMDATA_MULTI_HEADER_VALUE: {
                        if (buffer[pos] == ' ' && this->ctx_.hctx->s2.empty()) {
                            /**
                             * RFC7230 (3.2) Header Fields
                             * Optitional leading whitespace
                             */
                            pos++;
                        }

                        while (pos < size) {
                            if (!this->ctx_.hctx->s2.empty() && this->ctx_.hctx->s2.back() == '\r') {
                                if (buffer[pos] == '\n') {
                                    this->ctx_.hctx->s2.pop_back();
                                    pos++;

                                    if (!this->ctx_.hctx->s2.empty() && this->ctx_.hctx->s2.back() == ' ') {
                                        /**
                                         * RFC7230 (3.2) Header Fields
                                         * Optitional trailing whitespace
                                         */
                                        this->ctx_.hctx->s2.pop_back();
                                    }

                                    /* insert */
                                    auto it = this->ctx_.headers->find(this->ctx_.hctx->s1);
                                    if (it == this->ctx_.headers->end()) {
                                        this->ctx_.headers->insert({this->ctx_.hctx->s1, this->ctx_.hctx->s2});
                                    }
                                    else {
                                        /**
                                         * RFC7230 (3.2.2) Field Order
                                         * A recipient MAY combine multiple header fields with
                                         * the same field name into one "field-name: field-value" pair,
                                         * without changing the semantics of the message,
                                         * by appending each subsequent field value to the combined field value in order,
                                         * separated by a comma. The order in which header fields with the same
                                         * field name are received is therefore significant to
                                         * the interpretation of the combined field value;
                                         * a proxy MUST NOT change the order of these field values when forwarding a message.
                                         */

                                        // it->second.push_back(',');
                                        // it->second.append(this->ctx_.hctx->s2);

                                        /* but there it isn't allowed */
                                        co_return -1;
                                    }

                                    this->ctx_.hctx->s1.resize(0);
                                    this->ctx_.hctx->s2.resize(0);

                                    this->ctx_.current = FORMDATA_MULTI_HEADER_KEY;

                                    break;
                                }

                                co_return -1;
                            }

                            if (buffer[pos] == '\r') {
                                this->ctx_.hctx->s2.push_back(buffer[pos]);
                                pos++;
                                continue;
                            }

                            if (!isprint(buffer[pos])) {
                                /**
                                 * RFC7230 (3.2) Header Fields
                                 *
                                 * header-field   = field-name ":" OWS field-value OWS
                                 *
                                 * field-name     = token
                                 * field-value    = *( field-content / obs-fold )
                                 * field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
                                 * field-vchar    = VCHAR / obs-text
                                 *
                                 * obs-fold       = CRLF 1*( SP / HTAB )
                                 *                ; obsolete line folding
                                 *                ; see Section 3.2.4
                                 *
                                 * V[isible]CHAR
                                 */
                                co_return -1;
                            }

                            if (this->ctx_.hctx->s2.size() > 128)
                                co_return -1;

                            this->ctx_.hctx->s2.push_back(buffer[pos]);
                            pos++;
                        }
                        break;
                    }
                    case FORMDATA_MULTI_R: {
                        if (buffer[pos] != '\r') {
                            co_return -1;
                        }
                        pos++;
                        this->ctx_.current = FORMDATA_MULTI_N;
                        break;
                    }
                    case FORMDATA_MULTI_N: {
                        if (buffer[pos] != '\n') {
                            co_return -1;
                        }
                        pos++;
                        this->ctx_.current = this->ctx_.next;
                        this->ctx_.next = FORMDATA_MULTI_ERR;
                        break;
                    }
                    case FORMDATA_MULTI_R_OR_FIN: {
                        this->ctx_.n1 = 0;

                        if (!slice_transfer.empty()) {
                            if (static_cast<ssize_t>(slice_transfer.size()) != co_await this->ondata_cb_ (slice_view(slice_transfer), false)) {
                                manapi_log_trace(debug::LOG_TRACE_HIGH, "formdata:user callback returned an invalid result (size=%zu)", slice_transfer.size());
                                co_return -1;
                            }
                            slice_transfer.clear();
                        }

                        if (buffer[pos] == '\r') {
                            pos++;
                            this->ctx_.current = FORMDATA_MULTI_N;
                            this->ctx_.next = FORMDATA_MULTI_HEADER_KEY;

                            this->ctx_.headers = std::make_unique<decltype(this->ctx_.headers)::element_type>();
                            this->ctx_.hctx = std::make_unique<decltype(this->ctx_.hctx)::element_type>(
                                decltype(this->ctx_.hctx)::element_type{});

                            break;
                        }
                        if (buffer[pos] == '-') {
                            pos++;
                            this->ctx_.current = FORMDATA_MULTI_FINISH;
                            break;
                        }
                        co_return -1;
                    }
                    case FORMDATA_MULTI_DATA: {
                        /**
                         * n1 - boundary size
                         * n2 - payload start position
                         */

                        auto &n1 = this->ctx_.n1;
                        auto &boundary = this->ctx_.boundary;

                        while (pos != size) {
                            if (n1) {
                                if (buffer[pos] == boundary[n1]) {
                                    // if (!n1) {
                                    //     auto const beyond = (pos - n1);
                                    //     auto const copy = beyond - this->ctx_.n2;
                                    //     assert((copy >= 0));
                                    //     slice_transfer.push_back(buffer + this->ctx_.n2, copy);
                                    //
                                    //     this->ctx_.n2 = static_cast<int>(pos);
                                    // }

                                    n1++;
                                    pos++;

                                    if (n1 == boundary.size()) {
                                        this->ctx_.current = FORMDATA_MULTI_R_OR_FIN;
                                        break;
                                    }

                                    continue;
                                }

                                if (n1) {
                                    if (pos < n1) {
                                        auto const copy = n1;
                                        slice_transfer.push_back(this->ctx_.boundary.data(), copy);
                                        this->ctx_.n2 = static_cast<uint32_t>(pos);
                                    }

                                    n1 = 0;
                                }
                            }

                            std::string_view sv (buffer + pos, size - pos);
                            auto const res = sv.find(boundary[0]);
                            if (res == std::string_view::npos)
                                pos = size;
                            else {
                                pos += res + 1;
                                n1 = 1;
                            }
                        }

                        auto const beyond = (pos - n1);
                        auto const copy = beyond - this->ctx_.n2;

                        slice_transfer.push_back(buffer + this->ctx_.n2, copy);

                        this->ctx_.n2 = 0;

                        break;
                    }
                    case FORMDATA_MULTI_FINISH: {
                        if (buffer[pos] != '-')
                            co_return -1;

                        pos++;

                        this->ctx_.headers.reset();
                        this->ctx_.hctx.reset();

                        this->ctx_.current = FORMDATA_MULTI_R;
                        this->ctx_.next = FORMDATA_MULTI_ERR;

                        break;
                    }
                    default: {
                        co_return -1;
                    }
                }
            }

            total += pos;
        }

        if (!slice_transfer.empty()) {
            if (static_cast<ssize_t>(slice_transfer.size()) != co_await this->ondata_cb_ (slice_view(slice_transfer), false)) {
                manapi_log_trace(debug::LOG_TRACE_HIGH, "formdata:user callback returned an invalid result (size=%zu)", slice_transfer.size());
                co_return -1;
            }
            slice_transfer.clear();
        }

        co_return static_cast<ssize_t>(total);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "formdata multipart", e.what());
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::net::formdata_recv::onrecv_urlencoded_(slice_view buffs) {
    try {
        std::size_t total = 0;
        manapi::slice buffs_transfered;
        for (auto buff_it = buffs.begin(); buff_it != buffs.end(); ++buff_it) {
            std::size_t pos = 0;

            std::size_t const size = buff_it.size();
            auto buffer = static_cast<const char *>(buff_it.buffer());

            while (pos != size) {
                repeat: switch (this->ctx_.current) {
                    case FORMDATA_URLEN_INIT: {
                        this->ctx_.hctx = std::make_unique<decltype(this->ctx_.hctx)::element_type>(
                            decltype(this->ctx_.hctx)::element_type{});

                        this->ctx_.n1 = 0;
                        this->ctx_.n2 = 0;

                        this->ctx_.current = FORMDATA_URLEN_KEY;

                        break;
                    }
                    case FORMDATA_URLEN_KEY: {
                        while (pos != size) {
                            if (buffer[pos] == '=') {
                                encoding::decode_url(this->ctx_.hctx->s1,
                                    std::string_view (buffer + this->ctx_.n1, pos - this->ctx_.n1));

                                pos++;

                                this->ctx_.n1 = static_cast<uint32_t>(pos);
                                this->ctx_.current = FORMDATA_URLEN_VALUE;

                                goto repeat;
                            }

                            pos++;
                        }

                        if (pos == size) {
                            if (pos != this->ctx_.n1)
                                encoding::decode_url(this->ctx_.hctx->s1,
                                    std::string_view (buffer + this->ctx_.n1, pos - this->ctx_.n1));

                            this->ctx_.n1 = 0;
                        }

                        break;
                    }
                    case FORMDATA_URLEN_VALUE: {
                        while (pos != size) {
                            if (buffer[pos] == '&') {
                                encoding::decode_url(this->ctx_.hctx->s2,
                                    std::string_view (buffer + this->ctx_.n1, pos - this->ctx_.n1));

                                pos++;

                                this->ctx_.n1 = static_cast<uint32_t>(pos);
                                this->ctx_.current = FORMDATA_URLEN_KEY;

                                auto cb = this->onparam_cb_ (std::move(this->ctx_.hctx->s1));

                                if (cb) {
                                    buffs_transfered.push_back(this->ctx_.hctx->s2.data(), this->ctx_.hctx->s2.size());
                                }

                                this->ctx_.hctx->s2.resize(0);

                                if (!buffs_transfered.empty()) {
                                    if (static_cast<ssize_t>(buffs_transfered.size()) != co_await this->ondata_cb_ (slice_view(buffs_transfered), false)) {
                                        manapi_log_trace(debug::LOG_TRACE_HIGH, "formdata:user callback returned an invalid result (size=%zu)", buffs_transfered.size());
                                        co_return -1;
                                    }
                                }

                                goto repeat;
                            }

                            pos++;
                        }

                        if (pos == size) {
                            if (pos != this->ctx_.n1)
                                encoding::decode_url(this->ctx_.hctx->s2,
                                    std::string_view (buffer + this->ctx_.n1, pos - this->ctx_.n1));

                            this->ctx_.n1 = 0;
                        }

                        break;
                    }
                    default: {
                        co_return -1;
                    }
                }
            }

            total += pos;
        }

        co_return static_cast<ssize_t>(total);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "formdata urlencoded", e.what());
    }
    co_return -1;
}

manapi::net::formdata_send::formdata_send() {}

manapi::net::formdata_send::~formdata_send() = default;

manapi::net::formdata_send::formdata_send(formdata_send &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::net::formdata_send & manapi::net::formdata_send::operator=(formdata_send &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::status manapi::net::formdata_send::set_file(const std::string &name, std::string filepath) MANAPIHTTP_NOEXCEPT {
    try {
        auto filename = manapi::fs::path::basename(filepath);
        auto filemime = manapi::mime::mime_by_file_path(filename);

        this->data[name].push_back({DATA_FILE,
            std::move(filepath), data_file_storage{std::string{filename}, std::string{filemime}}});

        return status_ok();
    }
    catch (...) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::formdata_send::set_file(const std::string &name, std::string filepath, std::string filename, std::string filemime) MANAPIHTTP_NOEXCEPT {
    try {
        this->data[name].push_back({DATA_FILE,
            std::move(filepath), data_file_storage{std::move(filename), std::move(filemime)}});

        return status_ok();
    }
    catch (...) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::formdata_send::set_text(const std::string &name, std::string data) MANAPIHTTP_NOEXCEPT {
    try {
        this->data[name].push_back({DATA_PLAIN,
            std::move(data), data_file_storage{}});
        return status_ok();
    }
    catch (...) {
        return status_resource_exhausted();
    }
}

void manapi::net::formdata_send::erase(std::string_view name) MANAPIHTTP_NOEXCEPT {
    auto it = this->data.find(name);
    if (it != this->data.end())
        this->data.erase(it);
}

bool manapi::net::formdata_send::contains(std::string_view name) const MANAPIHTTP_NOEXCEPT {
    return this->data.contains(name);
}

manapi::future<manapi::status_or<std::size_t>> manapi::net::formdata_send::payload_size() const {
    std::size_t s = 0;
    for (const auto &params : this->data) {
        for (const auto &param : params.second) {
            switch (param.type) {
                case DATA_FILE: {
                    auto res = co_await manapi::fs::async_file_size(param.data);
                    if (!res.ok())
                        co_return res.err();
                    s += res.unwrap();
                    break;
                }
                case DATA_PLAIN:
                    s += (param.data.size());
                    break;
                default:
                    break;
            }
        }
    }
    co_return s;
}

manapi::status_or<std::size_t> manapi::net::formdata_send::multipart_size(std::size_t boundary_size) const MANAPIHTTP_NOEXCEPT {
    try {
        auto s = (boundary_size + (sizeof ("--\r\n") - 1));
        for (const auto &params : this->data) {
            for (const auto &param : params.second) {
                s +=(boundary_size + (sizeof ("\r\n") - 1));
                if (param.type == DATA_PLAIN) {
                    std::string const name = json{params.first}.dump();
                    std::string const val = http::stringify_header_value({{"form-data", {{"name", name}}}});
                    std::string header = http::stringify_header({http::H_CONTENT_DISPOSITION, val});
                    s += (header.size());
                    s += (sizeof ("\r\n") - 1);
                }
                else if (param.type == DATA_FILE) {
                    std::string const name = json{params.first}.dump();
                    std::string const filename = json{param.file.filename}.dump();
                    std::string val = http::stringify_header_value({{"form-data", {{"name", name}, {"filename", filename}}}});
                    std::string header = http::stringify_header({http::H_CONTENT_DISPOSITION, val});
                    s += (header.size());
                    s += (sizeof ("\r\n") - 1);

                    val = http::stringify_header_value({{param.file.filemime}});
                    header = http::stringify_header({http::H_CONTENT_TYPE, val});
                    s += (header.size());
                    s += (sizeof ("\r\n") - 1);
                }

                s += (sizeof ("\r\n") - 1);
                /* ... */
                s += (sizeof ("\r\n") - 1);
            }
        }
        return s;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "formdata: failed", e.what());
    }
    return status_internal("formdata: failed");
}

static constexpr char boundary_label[] = "--boundary";

std::string manapi::net::formdata_send::generate_boundary() const {
    std::string boundary;
    size_t constexpr boundary_label_size = sizeof (boundary_label) - 1;
    boundary.resize(boundary_label_size + boundary_payload_size);
    memcpy (boundary.data(), boundary_label, boundary_label_size);
    manapi::string::random(boundary.data() + boundary_label_size, boundary_payload_size,
        "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM0123456789");
    return std::move(boundary);
}

manapi::future<manapi::status> manapi::net::formdata_send::data2multipart(std::string boundary, ssize_t buffer_size,  std::move_only_function<manapi::future<manapi::status>(manapi::slice_view slice, bool fin)> write) {
    manapi::status status;

    slice_part_t part{};

    for (auto &params : this->data) {
        for (auto &param : params.second) {
            part.buff.base = boundary.data();
            part.buff.len = static_cast<decltype(part.buff.len)>(boundary.size());

            status = co_await write (slice_view(&part), false);
            if (!status)
                goto err;

            part.buff.base = (char*)nline;
            part.buff.len = sizeof (nline) - 1;
            status = co_await write (slice_view(&part), false);
            if (!status)
                goto err;

            if (param.type == DATA_PLAIN) {
                std::string const name = json{params.first}.dump();
                std::string header = http::stringify_header({http::H_CONTENT_DISPOSITION,
                    http::stringify_header_value({{"form-data", {{"name", name}}}})});

                part.buff.base = header.data();
                part.buff.len = static_cast<decltype(part.buff.len)>(header.size());
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;
                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;

                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;


                part.buff.base = param.data.data();
                part.buff.len = static_cast<decltype(part.buff.len)>(param.data.size());
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;
                param.data = {};

                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;
            }

            if (param.type == DATA_FILE) {
                std::string name = json{params.first}.dump();
                std::string const filename = json{std::move(param.file.filename)}.dump();
                std::string val = http::stringify_header_value({{"form-data", {{"name", name}, {"filename", filename}}}});
                std::string header = http::stringify_header({http::H_CONTENT_DISPOSITION, val});
                part.buff.base = header.data();
                part.buff.len = static_cast<decltype(part.buff.len)>(header.size());
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;
                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;

                val = http::stringify_header_value({{std::move(param.file.filemime)}});
                header = http::stringify_header({http::H_CONTENT_TYPE, val});
                part.buff.base = header.data();
                part.buff.len = static_cast<decltype(part.buff.len)>(header.size());
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;
                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;

                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), false);
                if (!status)
                    goto err;

                auto fstatus = manapi::fs::fstream::create (param.data);
                if (!fstatus)
                    co_return fstatus.err();
                auto f = fstatus.unwrap();
                status = manapi::status (co_await f->open(ev::FS_O_RDONLY));

                if (!status)
                    goto err;

                auto res = manapi::async::current()->memory_fabric().slice(65536);
                if (!res.ok())
                    co_return res.err();

                slice slices = res.unwrap();

                try {
                    auto fsize = manapi::unwrap(co_await f->size());

                    while (fsize) {
                        auto rhs = co_await f->read(slices);

                        if (rhs < 0)
                            co_return status_internal("formdata:Read data failed");

                        if (rhs == 0) {
                            continue;
                        }

                        fsize -= static_cast<std::size_t>(rhs);

                        auto slice = slices.subslice(0, static_cast<std::size_t>(rhs));
                        if (!slice)
                            co_return slice.err();

                        status = co_await write (slice.unwrap(), false);
                        if (!status)
                            goto err;
                    }
                }
                catch (std::exception const &e) {
                    manapi_log_error("%s due to %s", "data2multipart:Failed", e.what());
                    status = manapi::status_internal("data2multipart:Failed");
                }

                f->close();

                param.data = {};

                part.buff.base = (char*)nline;
                part.buff.len = sizeof (nline) - 1;
                status = co_await write (slice_view(&part), true);
                if (!status)
                    goto err;
            }
        }
    }

    part.buff.base = boundary.data();
    part.buff.len = static_cast<decltype(part.buff.len)>(boundary.size());
    status = co_await write (slice_view(&part), false);
    if (!status)
        goto err;
    part.buff.base = (char*)boundary_end_symbols;
    part.buff.len = sizeof (boundary_end_symbols) - 1;
    status = co_await write (slice_view(&part), false);
    if (!status)
        goto err;
    part.buff.base = (char*)nline;
    part.buff.len = sizeof (nline) - 1;
    status = co_await write (slice_view(&part), true);
    if (!status)
        goto err;

err:
    co_return std::move(status);
}






