#include <exception>

#include "ManapiHttp.hpp"
#include "services/ManapiFetch.hpp"

// Utils

struct curl_data_t {
    std::function <size_t(char *, const size_t&)>       handler_body;
    std::function <void(const std::map <std::string, std::string>&)>
                                                        handler_headers;
    std::map <std::string, std::string>                 *headers;
    bool                                                first_chunk_body;
};

size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    std::string str (buffer, size * n_items);

    // delete \r\n at the end of the header
    if (str.size() >= 2)
    {
        // \n
        str.pop_back();
        // \r
        str.pop_back();
    }

    auto header = manapi::net::utils::parse_header(str);
    reinterpret_cast <curl_data_t *> (userdata)->headers->insert(header);

    return n_items * size;
}

size_t curl_write_handler (char *buffer, size_t size, size_t n_mem_b, void *user_p)
{
    // check if it is the first chunk
    if (reinterpret_cast <curl_data_t *> (user_p)->first_chunk_body)
    {
        // we want to warn user about body
        reinterpret_cast <curl_data_t *> (user_p)->first_chunk_body = false;
        if (reinterpret_cast <curl_data_t *> (user_p)->handler_headers != nullptr)
        {
            reinterpret_cast <curl_data_t *> (user_p)->handler_headers (* reinterpret_cast <curl_data_t *> (user_p)->headers);
        }
    }
    // call user handler
    return reinterpret_cast <curl_data_t *> (user_p)->handler_body (buffer, size * n_mem_b);
}

size_t curl_send_formdata_cb_read (char *buffer, size_t size, size_t nitems, void *userp) {
    auto &func = *static_cast<decltype(manapi::net::curlformdata::multipart_param_value_file::callback) *> (userp);
    return func (buffer, size * nitems);
}

int curl_send_formdata_cb_seek (void *userp, curl_off_t offset, int origin) {
    return CURL_SEEKFUNC_OK;
}

void curl_send_formdata_cb_free (void *userp) {
    // pass
}


manapi::net::curlformdata::curlformdata() {}

manapi::net::curlformdata::~curlformdata() = default;

manapi::net::curlformdata::curlformdata(curlformdata &&fd) noexcept : mdata(std::move(fd.mdata)) {}

manapi::net::curlformdata & manapi::net::curlformdata::operator=(curlformdata &&fd) noexcept {
    this->mdata = std::move(fd.mdata);
    return *this;
}

void manapi::net::curlformdata::setdata(const std::string &name, std::string value) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new std::string(std::move(value))), [] (void *ptr) -> void {
        delete static_cast <std::string *> (ptr);
    });
    mdata.insert({name, {.data = std::move(udata), .type = PARAM_DEFAULT}});
}

void manapi::net::curlformdata::setfile(const std::string &filename, std::string filepath) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new std::string(std::move(filepath))), [] (void *ptr) -> void {
        delete static_cast <std::string *> (ptr);
    });
    mdata.insert({filename, {.data = std::move(udata), .type = PARAM_FILE}});
}

void manapi::net::curlformdata::setcallback(const std::string &name, const long long &size, const std::function<size_t(void *buff, size_t buff_size)> &cb) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new multipart_param_value_file({cb, size})), [] (void *ptr) -> void {
        delete static_cast <multipart_param_value_file *> (ptr);
    });
    mdata.insert({name, {.data = std::move(udata), .type = PARAM_CALLBACK}});
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::begin() {
    return mdata.begin();
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::end() {
    return mdata.end();
}

manapi::net::fetch::fetch(const std::string &url) {
    this->url     = url;
    this->method  = "GET";
    this->curl    = curl_easy_init();
}

manapi::net::fetch::fetch(fetch &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::fetch::~fetch() {
    if (curl != nullptr)
    {
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
}

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept {
    curl_free(this->curl);
    curl_slist_free_all(this->curl_headers);

    this->status_code = n.status_code;
    this->headers_list = std::move(n.headers_list);
    this->url = std::move(n.url);
    this->handle_custom_setup = std::move(n.handle_custom_setup);
    this->handler_body = std::move(n.handler_body);
    this->handler_headers = std::move(n.handler_headers);
    this->body = n.body;
    this->body_default = std::move(n.body_default);
    this->method = std::move(n.method);
    this->body_formdata = std::move(n.body_formdata);
    this->curl = n.curl;
    this->curl_headers = n.curl_headers;

    n.curl = nullptr;
    n.curl_headers = nullptr;
    n.status_code = 200;
    n.body = BODY_NONE;
    return *this;
}

void manapi::net::fetch::doit() {
    curl_mime *form = nullptr;

    if (curl == nullptr)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", url);
    }

    before_delete clean_up ([&] () {
        if (form != nullptr) {
            curl_mime_free(form);
        }

        if (curl_headers != nullptr)
        {
            curl_slist_free_all(curl_headers);
        }
    });

    curl_data_t data {
        .handler_body = handler_body,
        .handler_headers = handler_headers,
        .headers = &headers_list,
        .first_chunk_body = true
    };

    CURLcode resp;

    curl_easy_setopt(curl, CURLOPT_URL, url.data());
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_handler);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &data);

    // if handler has been set
    if (handler_body != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_handler);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    }

    if (curl_headers != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.data());

    if (data.first_chunk_body && data.handler_headers != nullptr)
    {
        data.first_chunk_body = false;

        data.handler_headers (headers_list);
    }

    if (handle_custom_setup != nullptr)
    {
        handle_custom_setup (curl);
    }

    // body of the request
    switch (body) {
        case BODY_PLAIN: {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_default.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, body_default.size());
            break;
        }
        case BODY_MULTIPART: {
            form = curl_mime_init(curl);
            curl_mimepart *field = nullptr;
            for (auto &param : body_formdata) {
                field = curl_mime_addpart(form);
                curl_mime_name(field, param.first.data());
                switch (param.second.type) {
                    case curlformdata::PARAM_DEFAULT: {
                        auto &strdata = *static_cast <std::string *> (param.second.data.get());
                        curl_mime_data(field, strdata.data(), strdata.size());
                        break;
                    }
                    case curlformdata::PARAM_FILE: {
                        auto &strdata = *static_cast <std::string *> (param.second.data.get());
                        curl_mime_filedata(field, strdata.data());
                        break;
                    }
                    case curlformdata::PARAM_CALLBACK: {
                        auto &cbdata = *static_cast <curlformdata::multipart_param_value_file *> (param.second.data.get());
                        curl_mime_data_cb(field, cbdata.filesize, curl_send_formdata_cb_read, curl_send_formdata_cb_seek, curl_send_formdata_cb_free, &cbdata.callback);
                        break;
                    }
                }
            }
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
            break;
        }
        default:
            break;
    }

    resp = curl_easy_perform(curl);

    if (resp != CURLE_OK)
    {
        MANAPIHTTP_LOG ("Connection failed: {}. Error code: {}. Error msg: {}", url, static_cast<int> (resp), curl_easy_strerror(resp));
    }

    curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &status_code);
}

std::map <std::string, std::string> manapi::net::fetch::get_headers() {
    return std::move(this->headers_list);
}

void manapi::net::fetch::handle_body(const std::function<size_t(char *, const size_t&)> &_handler) {
    handler_body = _handler;
}

std::string manapi::net::fetch::text() {
    std::string content;

    handle_body ([&content](char *buffer, const size_t &size) {
        content.append(buffer, size);

        return size;
    });

    doit();

    return content;
}

manapi::json manapi::net::fetch::json() {
    return std::move(manapi::json (text(), true));
}


void manapi::net::fetch::handle_headers(const std::function<void(const std::map <std::string, std::string> &)> &_handler) {
    handler_headers = _handler;
}

void manapi::net::fetch::set_body(curlformdata params) {
    body = BODY_MULTIPART;
    body_formdata = std::move(params);
}


void manapi::net::fetch::set_method(std::string method) {
    this->method = std::move(method);
}

void manapi::net::fetch::set_body(std::string data) {
    body = BODY_PLAIN;
    body_default = std::move(data);
}

void manapi::net::fetch::set_headers(const std::map<std::string, std::string> &headers) {
    // headers
    for (const auto &header: headers)
    {
        curl_headers = curl_slist_append(curl_headers, manapi::net::utils::stringify_header(header).data());
    }
}

void manapi::net::fetch::set_custom_setup(const std::function<void(CURL *curl)> &func) {
    handle_custom_setup = func;
}

void manapi::net::fetch::enable_ssl_verify(const bool &status) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, static_cast<ssize_t> (status));
}

size_t manapi::net::fetch::get_status_code() const {
    return status_code;
}

