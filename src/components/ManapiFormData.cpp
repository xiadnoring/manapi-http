#include <memory.h>

#include "components/ManapiFormData.hpp"

#include "ManapiFilesystem.hpp"
#include "http/ManapiHttpUtils.hpp"
#include "ManapiHttpMime.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "encoding/ManapiURL.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiString.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "http/ManapiHttp1.hpp"

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

struct formdata_recv_headers_t {
    std::string s1, s2;
};

struct manapi::net::formdata_recv::formdata_recv_ctx_t {
    int current;
    int next;

    int n1;
    int n2;

    std::string boundary;

    std::unique_ptr<formdata_recv_headers_t> hctx;
    std::unique_ptr<std::map<std::string, std::string>> headers;
};

manapi::net::formdata_recv::formdata_recv(onrecv_cb_t onrecv_cb, manapi::net::worker::base *worker, worker::shared_conn *conn, http::request_data_t *req) {
    this->worker_ = worker;
    this->onrecv_cb_ = onrecv_cb;
    this->conn_ = conn;
    this->req_ = req;
    this->ctx_ = std::make_unique<formdata_recv_ctx_t>(formdata_recv_ctx_t{});
}

manapi::net::formdata_recv::~formdata_recv() = default;

manapi::net::formdata_recv::formdata_recv(formdata_recv &&n) noexcept = default;

manapi::net::formdata_recv & manapi::net::formdata_recv::operator=(formdata_recv &&n) noexcept = default;

manapi::future<void> manapi::net::formdata_recv::get(onparam_cb_t cb) {
    assert(this->onparam_cb_ == nullptr);
    try {
        this->onparam_cb_ = std::move(cb);
        int type = CONTENT_TYPE_NONE;

        {
            auto const hit = this->req_->headers.find(http::HEADER.CONTENT_TYPE);

            if (hit == this->req_->headers.end()) {
                THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_INVALID_ARGUMENT, "Content-Type header is missing");
            }

            auto hparams = http::parse_header_value(hit->second);
            if (hparams.size() != 1) {
                THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_INVALID_ARGUMENT, "Content-Type header is invalid");
            }

            if (manapi::string::equals(hparams[0].value, mime::types.MULTIPART_FORM_DATA, 0b10)) {
                auto pit = hparams[0].params.find("boundary");
                if (pit == hparams[0].params.end()) {
                    THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_INVALID_ARGUMENT, "boundary is missing");
                }

                this->ctx_->boundary = "\r\n--" + pit->second;

                type = CONTENT_TYPE_MULTIPART_FORM_DATA;
            }
            else if (manapi::string::equals(hparams[0].value, mime::types.APPLICATION_X_WWW_FORM_URLENCODED, 0b10)) {
                type = CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED;
            }
            else {
                THROW_MANAPIHTTP_EXCEPTION (manapi::ERR_INVALID_ARGUMENT, "FormData is not supported for the following Content-Type: {}", hparams[0].value);
            }
        }

        switch (type) {
            case CONTENT_TYPE_MULTIPART_FORM_DATA: {
                co_await this->onrecv_cb_ (this->worker_, this->conn_, this->req_,
                    [this] (slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                        co_return co_await this->onrecv_multipart_(buffs);
                });
                break;
            }
            case CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED: {
                co_await this->onrecv_cb_ (this->worker_, this->conn_, this->req_,
                    [this] (slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                        return this->onrecv_urlencoded_(buffs);
                });

                if (this->ctx_->current != FORMDATA_URLEN_VALUE
                    && this->ctx_->current != FORMDATA_URLEN_INIT) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT,
                        "x-www-form-urlencoded data is not complete");
                }

                if (this->ctx_->current == FORMDATA_URLEN_VALUE) {
                    auto ucb = this->onparam_cb_ (std::move(this->ctx_->hctx->s1));
                    if (ucb) {
                        auto const size = static_cast<ssize_t> (this->ctx_->hctx->s2.size());

                        slice b;

                        b.push_back(this->ctx_->hctx->s2.data(), size);

                        if (size != co_await ucb (slice_view(b), true)) {
                            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT,
                                "user callback returned an invalid result");
                        }

                        this->ctx_->hctx->s2.resize(0);
                    }
                }

                break;
            }
        }

        this->onparam_cb_ = nullptr;
        this->ondata_cb_ = nullptr;
    }
    catch (...) {
        this->onparam_cb_ = nullptr;
        this->ondata_cb_ = nullptr;
        std::rethrow_exception(std::current_exception());
    }
}

manapi::net::formdata_recv::ondata_cb_t manapi::net::formdata_recv::save_file(std::string file, int mode, ssize_t maxlen, manapi::async::cancellation_action cancellation) {
    manapi::filesystem::fstream stream (std::move(file), std::move(cancellation));
    return [maxlen, mode, stream = std::move(stream)] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
        if (maxlen >= 0) {
            maxlen -= buffs.size();

            if (maxlen < 0)
                co_return -1;
        }

        while (true) {
            if (stream.is_open())
                co_return co_await stream.fwrite(buffs);


            auto res = co_await stream.open(ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_NONBLOCK, mode);
            if (!res.ok())
                co_return -1;
        }
    };
}

manapi::net::formdata_recv::ondata_cb_t manapi::net::formdata_recv::save_string(std::string *str, ssize_t maxlen) {
    return [maxlen, str] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
        if (maxlen >= 0) {
            maxlen -= buffs.size();

            if (maxlen < 0)
                co_return -1;
        }

        auto const cursor = str->size();
        str->resize(buffs.size() + cursor);
        buffs.copy_to(str->data() + cursor, 0, buffs.size());

        co_return buffs.size();
    };
}

manapi::net::formdata_recv::ondata_cb_t manapi::net::formdata_recv::skip(ssize_t maxlen) {
    return [maxlen] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
        if (maxlen >= 0) {
            maxlen -= buffs.size();

            if (maxlen < 0)
                co_return -1;
        }
        co_return buffs.size();
    };
}

manapi::future<ssize_t> manapi::net::formdata_recv::onrecv_multipart_(slice_view buffs) {
    ssize_t total = 0;
    slice_ref slice_transfer;

    for (auto buff_it = buffs.begin(); buff_it != buffs.end(); ++buff_it) {
        ssize_t pos = 0;
        ssize_t const size = buff_it.size();
        auto buffer = static_cast<const char *>(buff_it.buffer());

        while (pos != size) {
            repeat: switch (this->ctx_->current) {
                case FORMDATA_MULTI_INIT: {
                    this->ctx_->current = FORMDATA_MULTI_BOUNDARY;

                    this->ctx_->n1 = 2;
                    this->ctx_->n2 = 0;


                    break;
                }
                case FORMDATA_MULTI_BOUNDARY: {
                    auto const copy = std::min(static_cast<ssize_t>(this->ctx_->boundary.size() - this->ctx_->n1),
                        size - pos);

                    if (copy) {
                        if (0 != strncmp (this->ctx_->boundary.data() + this->ctx_->n1, buffer + pos, copy)) {
                            co_return -1;
                        }
                    }

                    this->ctx_->n1 += static_cast<int>(copy);
                    pos += copy;

                    if (this->ctx_->n1 == this->ctx_->boundary.size()) {
                        this->ctx_->n1 = 0;
                        this->ctx_->current = FORMDATA_MULTI_R_OR_FIN;
                    }

                    break;
                }
                case FORMDATA_MULTI_HEADER_KEY: {
                    while (pos < size) {
                        if (!this->ctx_->hctx->s1.empty() && this->ctx_->hctx->s1.back() == '\r') {
                            /**
                             * RFC7230 (3.2.4) Field Parsing
                             * Historically, HTTP header field values could be extended over multiple
                             * lines by preceding each extra line with at least one space or horizontal tab (obs-fold).
                             */
                            if (buffer[pos] == '\n') {
                                if (this->ctx_->hctx->s1.size() != 1) {
                                    co_return -1;
                                }

                                this->ctx_->hctx->s1.pop_back();

                                pos ++;

                                this->ctx_->n2 = static_cast<int>(pos);

                                auto hit = this->ctx_->headers->find(http::HEADER.CONTENT_DISPOSITION);
                                if (hit == this->ctx_->headers->end()) {
                                    co_return -1;
                                }

                                auto hparams = http::parse_header_value(hit->second);

                                if (hparams.size() != 1
                                    || hparams[0].value != "form-data")
                                    co_return -1;

                                auto nit = hparams[0].params.find("name");
                                if (nit == hparams[0].params.end())
                                    co_return -1;

                                this->ondata_cb_ = this->onparam_cb_ (std::move(nit->second));

                                this->ctx_->current = FORMDATA_MULTI_DATA;
                                this->ctx_->next = FORMDATA_MULTI_ERR;

                                goto repeat;
                            }

                            co_return -1;
                        }

                        if (buffer[pos] == '\r') {
                            this->ctx_->hctx->s1.push_back(buffer[pos]);
                            pos++;
                            continue;
                        }

                        if (buffer[pos] == ':') {
                            /**
                             * RFC7230 (3.2) Header Fields
                             * Each header field consists of a case-insensitive field name followed by a colon (":"),
                             * optional leading whitespace, the field value, and optional trailing whitespace.
                             */

                            if (this->ctx_->hctx->s1 != http::HEADER.CONTENT_DISPOSITION
                                && this->ctx_->hctx->s1 != http::HEADER.CONTENT_TYPE)
                                co_return -1;

                            pos++;
                            this->ctx_->current = FORMDATA_MULTI_HEADER_VALUE;

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

                        if (this->ctx_->hctx->s1.size() > 24)
                            co_return -1;

                        this->ctx_->hctx->s1.push_back(static_cast<char>(std::tolower(buffer[pos])));
                        pos++;
                    }
                    break;
                }
                case FORMDATA_MULTI_HEADER_VALUE: {
                    if (buffer[pos] == ' ' && this->ctx_->hctx->s2.empty()) {
                        /**
                         * RFC7230 (3.2) Header Fields
                         * Optitional leading whitespace
                         */
                        pos++;
                    }

                    while (pos < size) {
                        if (!this->ctx_->hctx->s2.empty() && this->ctx_->hctx->s2.back() == '\r') {
                            if (buffer[pos] == '\n') {
                                this->ctx_->hctx->s2.pop_back();
                                pos++;

                                if (!this->ctx_->hctx->s2.empty() && this->ctx_->hctx->s2.back() == ' ') {
                                    /**
                                     * RFC7230 (3.2) Header Fields
                                     * Optitional trailing whitespace
                                     */
                                    this->ctx_->hctx->s2.pop_back();
                                }

                                /* insert */
                                auto it = this->ctx_->headers->find(this->ctx_->hctx->s1);
                                if (it == this->ctx_->headers->end()) {
                                    this->ctx_->headers->insert({this->ctx_->hctx->s1, this->ctx_->hctx->s2});
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
                                    // it->second.append(this->ctx_->hctx->s2);

                                    /* but there it isn't allowed */
                                    co_return -1;
                                }

                                this->ctx_->hctx->s1.resize(0);
                                this->ctx_->hctx->s2.resize(0);

                                this->ctx_->current = FORMDATA_MULTI_HEADER_KEY;

                                break;
                            }

                            co_return -1;
                        }

                        if (buffer[pos] == '\r') {
                            this->ctx_->hctx->s2.push_back(buffer[pos]);
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

                        if (this->ctx_->hctx->s2.size() > 128)
                            co_return -1;

                        this->ctx_->hctx->s2.push_back(buffer[pos]);
                        pos++;
                    }
                    break;
                }
                case FORMDATA_MULTI_R: {
                    if (buffer[pos] != '\r') {
                        co_return -1;
                    }
                    pos++;
                    this->ctx_->current = FORMDATA_MULTI_N;
                    break;
                }
                case FORMDATA_MULTI_N: {
                    if (buffer[pos] != '\n') {
                        co_return -1;
                    }
                    pos++;
                    this->ctx_->current = this->ctx_->next;
                    this->ctx_->next = FORMDATA_MULTI_ERR;
                    break;
                }
                case FORMDATA_MULTI_R_OR_FIN: {
                    this->ctx_->n1 = 0;

                    if (!slice_transfer.empty()) {
                        if (slice_transfer.size() != co_await this->ondata_cb_ (slice_view(slice_transfer), false))
                            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT,
                                "user callback returned an invalid result");
                        slice_transfer.clear();
                    }

                    if (buffer[pos] == '\r') {
                        pos++;
                        this->ctx_->current = FORMDATA_MULTI_N;
                        this->ctx_->next = FORMDATA_MULTI_HEADER_KEY;

                        this->ctx_->headers = std::make_unique<decltype(this->ctx_->headers)::element_type>();
                        this->ctx_->hctx = std::make_unique<decltype(this->ctx_->hctx)::element_type>(
                            decltype(this->ctx_->hctx)::element_type{});

                        break;
                    }
                    if (buffer[pos] == '-') {
                        pos++;
                        this->ctx_->current = FORMDATA_MULTI_FINISH;
                        break;
                    }
                    co_return -1;
                }
                case FORMDATA_MULTI_DATA: {
                    /**
                     * n1 - boundary size
                     * n2 - payload start position
                     */

                    auto &n1 = this->ctx_->n1;
                    auto &boundary = this->ctx_->boundary;

                    while (pos != size) {
                        if (buffer[pos] == boundary[n1]) {
                            // if (!n1) {
                            //     auto const beyond = (pos - n1);
                            //     auto const copy = beyond - this->ctx_->n2;
                            //     assert((copy >= 0));
                            //     slice_transfer.push_back(buffer + this->ctx_->n2, copy);
                            //
                            //     this->ctx_->n2 = static_cast<int>(pos);
                            // }

                            n1++;
                            pos++;

                            if (n1 == boundary.size()) {
                                this->ctx_->current = FORMDATA_MULTI_R_OR_FIN;
                                break;
                            }

                            continue;
                        }

                        if (n1) {
                            if (pos < n1) {
                                auto const copy = static_cast<ssize_t> (n1);
                                slice_transfer.push_back(this->ctx_->boundary.data(), copy);

                                n1 = 0;
                                this->ctx_->n2 = static_cast<int> (pos);
                            }
                            else {
                                n1 = 0;
                            }
                        }

                        pos++;
                    }

                    auto const beyond = (pos - n1);
                    auto const copy = beyond - this->ctx_->n2;

                    slice_transfer.push_back(buffer + this->ctx_->n2, copy);

                    this->ctx_->n2 = 0;

                    break;
                }
                case FORMDATA_MULTI_FINISH: {
                    if (buffer[pos] != '-')
                        co_return -1;

                    pos++;

                    this->ctx_->headers.reset();
                    this->ctx_->hctx.reset();

                    this->ctx_->current = FORMDATA_MULTI_R;
                    this->ctx_->next = FORMDATA_MULTI_ERR;

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
        if (slice_transfer.size() != co_await this->ondata_cb_ (slice_view(slice_transfer), false))
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT,
                "user callback returned an invalid result");
        slice_transfer.clear();
    }

    co_return total;
}

manapi::future<ssize_t> manapi::net::formdata_recv::onrecv_urlencoded_(slice_view buffs) {
    ssize_t total = 0;
    manapi::slice buffs_transfered;
    for (auto buff_it = buffs.begin(); buff_it != buffs.end(); ++buff_it) {
        ssize_t pos = 0;

        ssize_t const size = buff_it.size();
        auto buffer = static_cast<const char *>(buff_it.buffer());

        while (pos != size) {
            repeat: switch (this->ctx_->current) {
                case FORMDATA_URLEN_INIT: {
                    this->ctx_->hctx = std::make_unique<decltype(this->ctx_->hctx)::element_type>(
                        decltype(this->ctx_->hctx)::element_type{});

                    this->ctx_->n1 = 0;
                    this->ctx_->n2 = 0;

                    this->ctx_->current = FORMDATA_URLEN_KEY;

                    break;
                }
                case FORMDATA_URLEN_KEY: {
                    while (pos != size) {
                        if (buffer[pos] == '=') {
                            encoding::decode_url(this->ctx_->hctx->s1,
                                std::string_view (buffer + this->ctx_->n1, pos - this->ctx_->n1));

                            pos++;

                            this->ctx_->n1 = static_cast<int>(pos);
                            this->ctx_->current = FORMDATA_URLEN_VALUE;

                            goto repeat;
                        }

                        pos++;
                    }

                    if (pos == size) {
                        if (pos != this->ctx_->n1)
                            encoding::decode_url(this->ctx_->hctx->s1,
                                std::string_view (buffer + this->ctx_->n1, pos - this->ctx_->n1));

                        this->ctx_->n1 = 0;
                    }

                    break;
                }
                case FORMDATA_URLEN_VALUE: {
                    while (pos != size) {
                        if (buffer[pos] == '&') {
                            encoding::decode_url(this->ctx_->hctx->s2,
                                std::string_view (buffer + this->ctx_->n1, pos - this->ctx_->n1));

                            pos++;

                            this->ctx_->n1 = static_cast<int>(pos);
                            this->ctx_->current = FORMDATA_URLEN_KEY;

                            auto cb = this->onparam_cb_ (std::move(this->ctx_->hctx->s1));

                            if (cb) {
                                auto const ssize = static_cast<ssize_t> (this->ctx_->hctx->s2.size());
                                buffs_transfered.push_back(this->ctx_->hctx->s2.data(), ssize);
                            }

                            this->ctx_->hctx->s2.resize(0);

                            if (!buffs_transfered.empty()) {
                                if (buffs_transfered.size() != co_await this->ondata_cb_ (slice_view(buffs_transfered), false))
                                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT,
                                        "user callback returned an invalid result");
                            }

                            goto repeat;
                        }

                        pos++;
                    }

                    if (pos == size) {
                        if (pos != this->ctx_->n1)
                            encoding::decode_url(this->ctx_->hctx->s2,
                                std::string_view (buffer + this->ctx_->n1, pos - this->ctx_->n1));

                        this->ctx_->n1 = 0;
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

    co_return total;
}

manapi::net::formdata_send::formdata_send(async::shared_ctx ctx) : ctx(std::move(ctx)) {}

manapi::net::formdata_send::~formdata_send() = default;

manapi::net::formdata_send::formdata_send(formdata_send &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::formdata_send & manapi::net::formdata_send::operator=(formdata_send &&n) noexcept {
    this->ctx = std::move(n.ctx);
    this->data = std::move(n.data);
    return *this;
}

void manapi::net::formdata_send::append_file(const std::string &name, std::string filepath) {
    auto filename = manapi::filesystem::path::basename(filepath);
    auto filemime = manapi::mime::mime_by_file_path(filename);

    this->data.insert({name,  {DATA_FILE, std::move(filepath), data_file_storage{std::move(filename), std::move(filemime)}}});
}

void manapi::net::formdata_send::append_file(const std::string &name, std::string filepath, std::string filename, std::string filemime) {
    this->data.insert({name,  {DATA_FILE, std::move(filepath), data_file_storage{std::move(filename), std::move(filemime)}}});
}

void manapi::net::formdata_send::append_text(const std::string &name, std::string data) {
    this->data.insert({name, {DATA_PLAIN, std::move(data), {}}});
}

void manapi::net::formdata_send::erase(const std::string &name) {
    this->data.erase(name);
}

bool manapi::net::formdata_send::contains(const std::string &name) const {
    return this->data.contains(name);
}

manapi::future<ssize_t> manapi::net::formdata_send::payload_size() const {
    ssize_t s = 0;
    for (const auto &param : this->data) {
        switch (param.second.type) {
            case DATA_FILE:
                s += co_await manapi::filesystem::async_file_size(param.second.data);
            break;
            case DATA_PLAIN:
                s += static_cast<ssize_t>(param.second.data.size());
            break;
            default:
                break;
        }
    }
    co_return s;
}

ssize_t manapi::net::formdata_send::multipart_size(ssize_t boundary_size) const {
    auto s = static_cast<ssize_t>(boundary_size + (sizeof ("--\r\n") - 1));
    for (const auto &param : this->data) {
        s += static_cast<ssize_t>(boundary_size + (sizeof ("\r\n") - 1));
        if (param.second.type == DATA_PLAIN) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()}}}})});
            s += static_cast<ssize_t> (header.size());
            s += (sizeof ("\r\n") - 1);
        }
        else if (param.second.type == DATA_FILE) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()},
                    {"filename", json{param.second.file.value().filename}.dump()}}}})});
            s += static_cast<ssize_t> (header.size());
            s += (sizeof ("\r\n") - 1);

            header = http::stringify_header({http::HEADER.CONTENT_TYPE,
                http::stringify_header_value({{param.second.file.value().filemime}})});
            s += static_cast<ssize_t> (header.size());
            s += (sizeof ("\r\n") - 1);
        }

        s += (sizeof ("\r\n") - 1);
        /* ... */
        s += (sizeof ("\r\n") - 1);
    }
    return s;
}

std::string manapi::net::formdata_send::generate_boundary() const {
    return "--boundary" + manapi::string::random(boundary_payload_size, "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM0123456789");
}

manapi::future<> manapi::net::formdata_send::data2multipart(std::string boundary, ssize_t buffer_size,  std::function<manapi::future<void>(const void *buffer, ssize_t size)> write) {
    for (auto &param : this->data) {
        co_await write (boundary.data(), static_cast<ssize_t>(boundary.size()));
        co_await write (nline, sizeof (nline) - 1);

        if (param.second.type == DATA_PLAIN) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()}}}})});

            co_await write (header.data(), static_cast<ssize_t>(header.size()));
            co_await write (nline, sizeof (nline) - 1);

            co_await write (nline, sizeof (nline) - 1);

            co_await write (param.second.data.data(), static_cast<ssize_t> (param.second.data.size()));
            param.second.data = {};

            co_await write (nline, sizeof (nline) - 1);
        }

        if (param.second.type == DATA_FILE) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()},
                    {"filename", json{std::move(param.second.file.value().filename)}.dump()}}}})});
            co_await write (header.data(), static_cast<ssize_t>(header.size()));
            co_await write (nline, sizeof (nline) - 1);

            header = http::stringify_header({http::HEADER.CONTENT_TYPE,
                http::stringify_header_value({{std::move(param.second.file.value().filemime)}})});
            co_await write (header.data(), static_cast<ssize_t>(header.size()));
            co_await write (nline, sizeof (nline) - 1);

            co_await write (nline, sizeof (nline) - 1);

            manapi::filesystem::fstream f (param.second.data);
            auto res = co_await f.open(ev::FS_O_RDONLY);

            if (!res.ok()) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_FILESYSTEM_FAILED, "Failed to read file ({}) to send it as form data parameter", param.second.data);
            }

            std::exception_ptr err{nullptr};

            std::string buffer;
            buffer.reserve(buffer_size);

            try {
                auto fsize = co_await f.size();

                while (fsize) {
                    auto rhs = co_await f.read(buffer.data(), buffer_size);
                    if (rhs < 0) {
                        THROW_MANAPIHTTP_EXCEPTION(ERR_FILESYSTEM_FAILED, "Failed to read file ({}) to send it as formdata parameter", param.second.data);
                    }
                    if (rhs == 0) {
                        continue;
                    }

                    fsize -= rhs;

                    co_await write (buffer.data(), rhs);
                }
            }
            catch (...) {
                err = std::current_exception();
            }

            co_await f.close();

            if (err) {
                std::rethrow_exception(std::move(err));
            }

            param.second.data = {};

            co_await write (nline, sizeof (nline) - 1);
        }
    }

    co_await write (boundary.data(), static_cast<ssize_t>(boundary.size()));
    co_await write (boundary_end_symbols, sizeof (boundary_end_symbols) - 1);
    co_await write (nline, sizeof (nline) - 1);
}






