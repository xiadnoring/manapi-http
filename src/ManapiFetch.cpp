#include <exception>

#include "ManapiHttp.hpp"
#include "ManapiFetch.hpp"

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

// Class

manapi::net::fetch::fetch(const std::string &_url) {
    url     = _url;
    method  = "GET";
    curl    = curl_easy_init();
}

manapi::net::fetch::~fetch() {
    if (curl != nullptr)
    {
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
}

void manapi::net::fetch::doit() {
    if (curl == nullptr)
    {
        THROW_MANAPI_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", url);
    }

    utils::before_delete clean_up ([&] () {
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

    // body of the request
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, body.size());

    if (data.first_chunk_body && data.handler_headers != nullptr)
    {
        data.first_chunk_body = false;

        data.handler_headers (headers_list);
    }

    if (handle_custom_setup != nullptr)
    {
        handle_custom_setup (curl);
    }

    resp = curl_easy_perform(curl);

    if (resp != CURLE_OK)
    {
        MANAPI_LOG ("Connection failed: {}", url);
    }

    curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &status_code);
}

const std::map <std::string, std::string> &manapi::net::fetch::get_headers() {
    return headers_list;
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
    return manapi::json (text(), true);
}


void manapi::net::fetch::handle_headers(const std::function<void(const std::map <std::string, std::string> &)> &_handler) {
    handler_headers = _handler;
}

void manapi::net::fetch::set_method(const std::string &_method) {
    method = _method;
}

void manapi::net::fetch::set_body(const std::string &data) {
    body = data;
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

