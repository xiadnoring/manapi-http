#include <exception>

#include "ManapiHttp.hpp"
#include "services/ManapiFetch.hpp"

#include "async/ManapiAsyncPromise.hpp"

// Utils

std::mutex manapi::net::fetch::_global_init_mx;
size_t manapi::net::fetch::_global_init_value = 0;

struct curl_data_t {
    std::function <size_t(char *, const size_t&)> handler_body;
    std::function <void(const std::map <std::string, std::string>&)> handler_headers;
    std::map <std::string, std::string> *headers;
    bool first_chunk_body;
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

    auto header = manapi::net::http::parse_header(str);
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
    this->mdata.insert({name, {.data = std::move(udata), .type = PARAM_DEFAULT}});
}

void manapi::net::curlformdata::setfile(const std::string &filename, std::string filepath) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new std::string(std::move(filepath))), [] (void *ptr) -> void {
        delete static_cast <std::string *> (ptr);
    });
    this->mdata.insert({filename, {.data = std::move(udata), .type = PARAM_FILE}});
}

void manapi::net::curlformdata::setcallback(const std::string &name, const long long &size, const std::function<size_t(void *buff, size_t buff_size)> &cb) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new multipart_param_value_file({cb, size})), [] (void *ptr) -> void {
        delete static_cast <multipart_param_value_file *> (ptr);
    });
    this->mdata.insert({name, {.data = std::move(udata), .type = PARAM_CALLBACK}});
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::begin() {
    return this->mdata.begin();
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::end() {
    return this->mdata.end();
}


manapi::net::fetch::fetch(const std::string &url, net::site &site) : site(site) {
    fetch::_global_init();

    this->url = url;
    this->method = "GET";
    this->curl = curl_easy_init();
    this->curl_multi = curl_multi_init();
}

manapi::net::fetch::fetch(fetch &&n) noexcept : site(n.site) {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::fetch::~fetch() {
    if (this->curl)
    {
        curl_easy_cleanup(std::exchange(this->curl, nullptr));
    }

    if (this->curl_multi) {
        curl_multi_cleanup(std::exchange(this->curl_multi, nullptr));
    }

    //fetch::_global_deinit();
}

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept {
    curl_multi_cleanup(this->curl_multi);
    curl_free(this->curl);
    curl_slist_free_all(this->curl_headers);

    this->status_code = std::exchange(n.status_code, 200);
    this->headers_list = std::move(n.headers_list);
    this->url = std::move(n.url);
    this->handle_custom_setup = std::move(n.handle_custom_setup);
    this->handler_body = std::move(n.handler_body);
    this->handler_headers = std::move(n.handler_headers);
    this->body = std::exchange(n.body, BODY_NONE);
    this->body_default = std::move(n.body_default);
    this->method = std::move(n.method);
    this->body_formdata = std::move(n.body_formdata);
    this->curl = n.curl;
    this->curl_headers = std::exchange(n.curl_headers, nullptr);
    this->curl_multi = std::exchange(n.curl_multi, nullptr);

    return *this;
}

manapi::future<void> manapi::net::fetch::async_doit() {
    curl_mime *form = nullptr;

    if (this->curl == nullptr)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", url);
    }

    before_delete clean_up ([&] () {
        if (form != nullptr) {
            curl_mime_free(form);
        }

        if (this->curl_headers != nullptr)
        {
            curl_slist_free_all(this->curl_headers);
        }
    });

    curl_data_t data {
        .handler_body = handler_body,
        .handler_headers = handler_headers,
        .headers = &headers_list,
        .first_chunk_body = true
    };

    CURLcode resp;

    curl_easy_setopt(this->curl, CURLOPT_URL, url.data());
    curl_easy_setopt(this->curl, CURLOPT_HEADERFUNCTION, curl_header_handler);
    curl_easy_setopt(this->curl, CURLOPT_HEADERDATA, &data);

    // if handler has been set
    if (this->handler_body != nullptr)
    {
        curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, curl_write_handler);
        curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, &data);
    }

    if (this->curl_headers != nullptr)
    {
        curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, this->curl_headers);
    }

    curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, this->method.data());

    if (data.first_chunk_body && data.handler_headers != nullptr)
    {
        data.first_chunk_body = false;

        data.handler_headers (headers_list);
    }

    if (this->handle_custom_setup != nullptr)
    {
        this->handle_custom_setup (this->curl);
    }

    // body of the request
    switch (body) {
        case BODY_PLAIN: {
            curl_easy_setopt(this->curl, CURLOPT_POSTFIELDS, this->body_default.data());
            curl_easy_setopt(this->curl, CURLOPT_POSTFIELDSIZE_LARGE, this->body_default.size());
            break;
        }
        case BODY_MULTIPART: {
            form = curl_mime_init(this->curl);
            curl_mimepart *field = nullptr;
            for (auto &param : this->body_formdata) {
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
            curl_easy_setopt(this->curl, CURLOPT_MIMEPOST, form);
            break;
        }
        default:
            break;
    }

    try {
        resp = co_await async_curl_perform();
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("Exception: {}", e.what());
        resp = CURLE_AGAIN;
    }

    if (resp != CURLE_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "Connection failed: {}. Error code: {}. Error msg: {}", this->url, static_cast<int> (resp), curl_easy_strerror(resp));
    }

    curl_easy_getinfo(this->curl, CURLINFO_HTTP_CODE, &this->status_code);
}

std::map <std::string, std::string> manapi::net::fetch::get_headers() {
    return std::move(this->headers_list);
}

void manapi::net::fetch::_global_init() {
    std::lock_guard<std::mutex> lk (fetch::_global_init_mx);
    if (0 == fetch::_global_init_value++) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
}

void manapi::net::fetch::_global_deinit() {
    std::lock_guard<std::mutex> lk (fetch::_global_init_mx);
    if (0 == (--fetch::_global_init_value)) {
        curl_global_cleanup();
    }
}

manapi::future<CURLcode> manapi::net::fetch::async_curl_perform() {
    std::map <int, std::shared_ptr<ev::io> > fds {};
    std::shared_ptr<ev::async> w;
    int attempts = this->attempts;

    co_return co_await async::promise<CURLcode> (this->site.async_context(), [&attempts, &w, this, &fds] (const std::function<void(CURLcode v)> &resolve, const std::function<void(std::exception_ptr)> &reject) -> future<> {
        /* in the libev loop */
        CURLMcode mc = curl_multi_add_handle(this->curl_multi, this->curl);
        if (mc != CURLM_OK) {
            resolve(CURLE_AGAIN);
            co_return;
        }

        auto reject_cb = [this, &fds, &w, reject] () mutable -> void {
            auto _reject = std::move(reject);
            /* error */
            for (auto &w_it: fds) {
                this->site.async_context()->eventloop()->stop_watcher (w_it.second);
            }
            fds.clear();
            curl_multi_remove_handle(this->curl_multi, this->curl);
            this->site.async_context()->eventloop()->stop_watcher (w);
            _reject(std::make_exception_ptr(manapi::exception{ERR_FATAL, "Failure when receiving data from the peer"}));
        };

        w = co_await this->site.async_context()->eventloop()->watch_async([&attempts, reject_cb = std::move(reject_cb), &shared_w = w, reject, resolve, this, &fds] (ev::async &w, int revents)
                mutable  -> void {
            int running_handles = 0;
            CURLMcode mc;

            mc = curl_multi_perform(this->curl_multi, &running_handles);

            if (mc != CURLM_OK) {
                reject_cb();
                return;
            }

            if (running_handles) {
                fd_set fd_read;
                fd_set fd_write;
                fd_set fd_exc;

                FD_ZERO(&fd_read);
                FD_ZERO(&fd_write);
                FD_ZERO(&fd_exc);

                int maxfd = -1;
                mc = curl_multi_fdset(this->curl_multi, &fd_read, &fd_write, &fd_exc, &maxfd);

                if ((mc != CURLM_OK)) {
                    reject_cb();
                    return;
                }

                if (maxfd == -1) {
                    /* socket is unavailable */
                    if (--attempts) {
                        async::run(this->site.async_context(),
                            [this, shared_w] () mutable -> future<> {
                            auto id = co_await this->site.async_context()->timerpool()->async_append_timer_sync(this->attempt_delay,
                            [shared_w = std::move(shared_w)] () mutable -> void {
                                shared_w->send();
                            });
                        });
                    }
                    else {
                        reject_cb();
                        return;
                    }
                }

                for (int i = 0; i <= maxfd; ++i) {
                    if (FD_ISSET(i, &fd_read)) {
                        auto p = (i << 1) | 0x01;
                        if (!fds.contains(p)) {
                            auto wr = this->site.async_context()->eventloop()->create_watcher_fd(i, ev::READ, [this, &shared_w, &fds, p] (ev::io &wr, int revents) -> void {
                                shared_w->send();

                                fds.erase(p);
                                this->site.async_context()->eventloop()->stop_watcher(wr);
                            });
                            wr->start();
                            fds.insert({p, std::move(wr)});
                        }
                    }

                    if (FD_ISSET(i, &fd_write)) {
                        auto p = (i << 1);
                        if (!fds.contains(p)) {
                            auto ww = this->site.async_context()->eventloop()->create_watcher_fd(i, ev::WRITE, [this, &shared_w, &fds, p] (ev::io &ww, int revents) -> void {
                                shared_w->send();

                                fds.erase(p);
                                this->site.async_context()->eventloop()->stop_watcher(ww);
                            });
                            ww->start();
                            fds.insert({p, std::move(ww)});
                        }
                    }
                }
            }

            int msgs_left;
            CURLMsg* msg;
            while (true) {
                msg = curl_multi_info_read(this->curl_multi, &msgs_left);
                if (!msg) {
                    break;
                }
                if (msg->msg == CURLMSG_DONE) {
                    for (auto &w_it: fds) {
                        this->site.async_context()->eventloop()->stop_watcher(w_it.second);
                    }
                    fds.clear();
                    curl_multi_remove_handle(this->curl_multi, this->curl);
                    auto _resolve = std::move(resolve);
                    this->site.async_context()->eventloop()->stop_watcher(w);
                    _resolve(msg->data.result);
                    return;
                }

                reject_cb();
                return;
            }
        });
    });
}

void manapi::net::fetch::handle_body(const std::function<size_t(char *, const size_t&)> &_handler) {
    this->handler_body = _handler;
}

manapi::future<std::string> manapi::net::fetch::text() {
    std::string content;

    handle_body ([&content](char *buffer, const size_t &size) {
        content.append(buffer, size);

        return size;
    });

    co_await async_doit();

    co_return content;
}

manapi::future<manapi::json> manapi::net::fetch::json() {
    co_return std::move(manapi::json (co_await text(), true));
}


void manapi::net::fetch::handle_headers(const std::function<void(const std::map <std::string, std::string> &)> &_handler) {
    this->handler_headers = _handler;
}

void manapi::net::fetch::set_body(curlformdata params) {
    this->body = BODY_MULTIPART;
    this->body_formdata = std::move(params);
}


void manapi::net::fetch::set_method(std::string method) {
    this->method = std::move(method);
}

void manapi::net::fetch::set_body(std::string data) {
    this->body = BODY_PLAIN;
    this->body_default = std::move(data);
}

void manapi::net::fetch::set_headers(const std::map<std::string, std::string> &headers) {
    // headers
    for (const auto &header: headers)
    {
        this->curl_headers = curl_slist_append(this->curl_headers, manapi::net::http::stringify_header(header).data());
    }
}

void manapi::net::fetch::set_custom_setup(const std::function<void(CURL *curl)> &func) {
    this->handle_custom_setup = func;
}

void manapi::net::fetch::enable_ssl_verify(const bool &status) {
    curl_easy_setopt(this->curl, CURLOPT_SSL_VERIFYPEER, static_cast<ssize_t> (status));
}

size_t manapi::net::fetch::get_status_code() const {
    return this->status_code;
}

