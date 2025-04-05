#include <memory.h>
#include <fstream>

#ifdef _WIN32
#else
#   include <arpa/inet.h>
#endif

#include "http/base_http.hpp"

#include "services/ManapiFetch.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiHttpMime.hpp"
#include "ManapiString.hpp"
#include "ManapiTime.hpp"
#include "async/ManapiAsyncFileStream.hpp"
#include "async/ManapiAsyncParallelRun.hpp"

std::set<std::string> manapi::net::http::base::methods = {"POST", "GET", "HEAD", "OPTIONS", "TRACE", "PUT", "DELETE", "PATCH", "CONNECT"};

manapi::net::http::base::base(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site): site(site), worker(std::move(worker)), config(std::move(config)) {}

manapi::net::http::base::~base() = default;

void manapi::net::http::base::prepare() {}

manapi::future<bool> manapi::net::http::base::parse_request(ssize_t j, ssize_t size) { co_return true; }

manapi::future<void> manapi::net::http::base::send_response(manapi::net::http::response &res) {
    std::string response;
    std::string compressed;

    std::function<future<bool>(const std::string &src, const std::string &dest)> compressor = nullptr;

    auto &compress = res.compress();

    if (!compress.empty()) {
        if (!res.is_file() ||
            !res.partial_enabled() ||
            manapi::filesystem::get_size(res.file()) < config->get_partial_data_min_size()
        ) {
            compressor = site.get_compressor(compress);

            if (compressor) {
                res.header(HEADER.CONTENT_ENCODING, compress);
            }
        }
    }

    response_features_t features = {
        .compress = compress,
        .compressor = compressor,
        .replacers = res.replacers()
    };

    // set time
    res.header(HEADER.DATE, std::format("{:%a, %d %b %Y %H:%M:%S} GMT", manapi::time::current_time(false)));
    if (this->config->get_http_version() < versions::HTTP_v2) { res.header(HEADER.CONNECTION, "close"); }

    if (res.is_file()) {
        co_await send_response_file(res, features);
    } else if (res.is_text()) {
        co_await send_response_text(res, features);
    } else if (res.is_proxy()) {
        co_await send_response_proxy(res, features);
    } else if (res.is_formdata()) {
        co_await send_response_formdata(res, features);
    } else if (res.is_async_cb()) {
        co_await send_response_async_cb(res, features);
    } else if (res.is_sync_cb()) {
        co_await send_response_sync_cb(res, features);
    }else {
        co_await mask_response(res, true);
    }

    co_return;
}

manapi::future<void> manapi::net::http::base::execute_handler() {
    co_return;
}

manapi::future<void> manapi::net::http::base::send_response_file(manapi::net::http::response &res, response_features_t &features) {
    std::string filepath;

    if (features.compressor) {
        if (features.replacers) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_SETTINGS_INCOMPATIBILITY, "replacers can not be using during compress");
        }

        filepath = co_await this->compress_file(res.file(), this->site.config_cache_dir(), features.compress, features.compressor);
    }
    else {
        filepath = res.file();
    }

    filesystem::async::fstream f (this->site.async_context(), filepath);
    co_await f.open(filesystem::async::fstream::FILE_READ);

    if (!f.is_open()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to open the file: {}", filepath);
    }

    std::exception_ptr err{nullptr};

    try {
        // set headers
        {
            std::string mimetype = mime::mime_by_file_path(res.file());
            if (mimetype.size() > sizeof ("text")) {
                if (strncmp("text", mimetype.data(), sizeof ("text") - 1) == 0) {
                    mimetype = stringify_header_value({{mimetype, {{"charset", "UTF-8"}}}});
                }
            }

            res.header(HEADER.CONTENT_TYPE, mimetype);
        }
        std::vector<replace_founded_item> replacers;

        // get file size
        ssize_t fileSize = manapi::filesystem::get_size(filepath);
        ssize_t dynamicFileSize = fileSize;

        // replacers
        if (features.replacers) {
            replacers = co_await found_replacers_in_file(this->site.async_context(), filepath, 0, fileSize, features.replacers.value());

            for (const auto &replacer: replacers) {
                dynamicFileSize = static_cast<ssize_t> (
                    dynamicFileSize - (replacer.pos.second - replacer.pos.first + 1) + replacer.value->size());
            }
        }

        // partial enabled
        if (res.partial_enabled() && config->get_partial_data_min_size() <= fileSize) {
            if (features.compressor) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_SETTINGS_INCOMPATIBILITY,
                                       "the compress '{}' with the partial content is not supported.",
                                       res.compress());
            }


            if (features.replacers) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_SETTINGS_INCOMPATIBILITY, "replacers can not be use with partial");
            }

            res.status(206);
            res.header(HEADER.ACCEPT_RANGES, "bytes");

            ssize_t start = 0,
                    back = fileSize - 1,
                    size;

            switch (res.ranges_.size()) {
                case 1:
                    if (res.ranges_[0].first != -1) {
                        start = res.ranges_[0].first;
                    }

                if (res.ranges_[0].second != -1) {
                    back = res.ranges_[0].second;
                } else {
                    back = fileSize - 1;
                }
                case 0:
                    size = back - start + 1;

                res.header(HEADER.CONTENT_LENGTH, std::to_string(size));
                res.header(HEADER.CONTENT_RANGE, "bytes " + std::to_string(start) + '-' + std::to_string(back) + '/' + std::to_string(fileSize));

                if (co_await mask_response(res, false) >= 0) {
                    // set start position
                    f.seekg(start);
                    // set size and send
                    co_await send_file(res, f, size);
                }


                break;

                default:
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_UNSUPPORTED, "multi bytes unsupported");
            }
        }
        else {
            res.header(HEADER.CONTENT_LENGTH, std::to_string(dynamicFileSize));

            if (co_await mask_response(res, false) >= 0) {
                if (replacers.empty()) {
                    // without replacers
                    co_await send_file(res, f, fileSize);
                }
                else {
                    // with replacers
                    co_await send_file(res, f, fileSize, replacers);
                }
            }
        }
    }
    catch (...) {
        err = std::current_exception();
    }

    co_await f.close();

    if (err) {
        std::rethrow_exception(err);
    }
}

manapi::future<void> manapi::net::http::base::send_response_text(manapi::net::http::response &res, response_features_t &features) {
    // may contains decoded / encoded body
    std::string plaintext = std::move(res.body());

    if (false || features.compressor) {
        // encode content !
        //plaintext = co_await features.compressor(res.get_body(), {});
    }

    res.header(HEADER.CONTENT_LENGTH, std::to_string(plaintext.size()));

    if (!res.ref_headers().contains(HEADER.CONTENT_TYPE)) {
        res.header(HEADER.CONTENT_TYPE, "text/html; charset=UTF-8");
    }

    if (co_await mask_response(res, false) >= 0) {
        co_await send_text(plaintext, plaintext.size());
    } else {
        MANAPIHTTP_LOG("{}", "mask_response(...) < 0");
    }
    co_return;
}

manapi::future<void> manapi::net::http::base::send_response_proxy(manapi::net::http::response &res, response_features_t &features) {
#ifdef MANAPIHTTP_FETCH_SUPPORT
    auto proxy = std::make_unique<fetch>(this->site.async_context(), res.data());

    {
        const auto proxy_setup = std::move(res.proxy_setup_cb());
        proxy_setup->operator()(*proxy);
    }

    proxy->headers({{"ranges", "0-"}});
    size_t content_length = 0;
    proxy->handle_async_headers ([this, &content_length, &proxy, &res](std::map<std::string, std::string> headers) -> manapi::future<bool> {
        res.status_code(proxy->status_code());

        if (headers.contains(HEADER.CONTENT_LENGTH)) {
            std::string &value = headers[HEADER.CONTENT_LENGTH];
            content_length = std::stoull(value);
            res.header(HEADER.CONTENT_LENGTH, std::move(value));
        }

        const auto rhs1 = co_await this->mask_response(res, content_length == 0);
        if (rhs1 < 0) {
            co_return false;
        }

        co_return true;
    });

    proxy->handle_async_body([&](char *buffer, ssize_t size) -> manapi::future<ssize_t> {
        auto rhs = co_await this->worker->fwrite (*this->connection, buffer,
            size, content_length <= size);

        if (rhs < 0) {
            co_return -1;
        }

        content_length -= rhs;
        co_return rhs;
    });

    co_await proxy->async_doit();

    co_return;
#else
    THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Fetch is required");
#endif
}

manapi::future<> manapi::net::http::base::send_response_formdata(manapi::net::http::response &res, response_features_t &features) {
    auto formdata = res.formdata();

    if (features.compressor) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_UNSUPPORTED, "formdata: Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_UNSUPPORTED, "formdata: Replacers isn't supported");
    }

    auto size = formdata.payload_size();
    auto boundary = formdata.generate_boundary();
    size += formdata.multipart_size(static_cast<ssize_t>(boundary.size()));

    res.header(HEADER.CONTENT_LENGTH, std::to_string(size));
    res.header(HEADER.CONTENT_TYPE, stringify_header_value({{"multipart/form-data", {{"boundary", boundary.substr(2)}}}}));

    if (co_await mask_response(res, false) >= 0) {
        auto &conn_data = *this->connection;
        co_await formdata.data2multipart(std::move(boundary), this->config->buffer_size(), [this, &conn_data] (const void *buffer, ssize_t size)
            -> future<> { if (co_await this->worker->fwrite(conn_data, buffer, size, false) < 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_CONNECTION_WAS_CLOSED, "failed to write"); } });
    }
    else {
        MANAPIHTTP_LOG2 ("mask_response < 0");
    }

    co_return;
}

manapi::future<> manapi::net::http::base::send_response_sync_cb(manapi::net::http::response &res, response_features_t &features) {
    if (features.compressor) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_UNSUPPORTED, "Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_UNSUPPORTED, "Replacers isn't supported");
    }

    if (co_await mask_response(res, false) >= 0) {
        std::string buffer;
        const ssize_t reserved = this->config->buffer_size();
        buffer.reserve(reserved);

        auto cb = std::move(res.callback_sync());

        bool finish = false;

        while (!finish) {
            auto rhs = cb->operator()(buffer.data(), reserved, finish);
            if (rhs < 0) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "The callback returned an invalid length");
            }
            co_await this->worker->fwrite(*this->connection, buffer.data(), rhs, finish);
        }
    } else {
        MANAPIHTTP_LOG("{}", "mask_response(...) < 0");
    }
}

manapi::future<> manapi::net::http::base::send_response_async_cb(manapi::net::http::response &res,response_features_t &features) {
    if (features.compressor) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_UNSUPPORTED, "Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_UNSUPPORTED, "Replacers isn't supported");
    }

    if (co_await mask_response(res, false) >= 0) {
        std::string buffer;
        const ssize_t reserved = this->config->buffer_size();
        buffer.reserve(reserved);

        auto cb = std::move(res.callback_async());

        bool finish = false;

        while (!finish) {
            auto rhs = co_await cb->operator()(buffer.data(), reserved, finish);
            if (rhs < 0) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "The callback returned an invalid length");
            }
            co_await this->worker->fwrite(*this->connection, buffer.data(), rhs, finish);
        }
    } else {
        MANAPIHTTP_LOG("{}", "mask_response(...) < 0");
    }
}

manapi::future<ssize_t> manapi::net::http::base::mask_response(manapi::net::http::response &resp, bool finish) {
    const auto rhs = co_await this->worker->response(*connection, resp, finish);
    co_return rhs;
}

manapi::future<void> manapi::net::http::base::handle_request(const http_handler_page *data, http::request_data_t &request_data, const size_t &status) {
    manapi_socket_information socket_information = {
        .ip = inet_ntoa(reinterpret_cast<struct sockaddr_in *>(&connection->client)->sin_addr),
        .port = htons(reinterpret_cast<struct sockaddr_in *>(&connection->client)->sin_port)
    };
    http::request req (socket_information, request_data, this, config, data);
    http::response res(request_data, status, *config);
    try {
        // handle layers
        for (const auto &layer: data->layer) {
            co_await layer->handler(req, res);

            if (!req.propagation()) {
                // skip other layers and handlers
                goto finish;
            }
        }

        // handler function not be found
        if (data->handler == nullptr) {
            // check exists static folder/file
            if (data->statics != nullptr) {
                // if statics exists
                std::string path;

                for (size_t i = data->statics_parts_len; i < request_data.path.size(); i++) {
                    path += manapi::filesystem::delimiter + request_data.path[i];
                }

                path = manapi::filesystem::join(*data->statics, path);

                if (manapi::filesystem::exists(path) && manapi::filesystem::is_file(path)) {
                    const auto ext = manapi::filesystem::extension(path);
                    auto mime = mime::mime_by_extension.find(ext);
                    bool binary = false;
                    if (mime != mime::mime_by_extension.end()) {
                        binary = mime::mime_partitial_data(mime->second);
                    }
                    res.compress_enabled(!binary);
                    res.partial_enabled(binary);
                    res.file(path);

                    try {
                        co_await send_response(res);
                        co_return;
                    } catch (const std::exception &e) {
                        MANAPIHTTP_LOG("Unexpected error: {}", e.what());
                    }

                    co_await send_error_response(503, request_data, data->error.get());
                    co_return;
                }
            }

            co_await send_error_response(404, request_data, data->error.get());
            co_return;
        }
        co_await data->handler->handler (req, res);

    finish:
        co_await send_response(res);
        co_return;
    } catch (const manapi::exception &e) {
        switch (e.get_err_num()) {
            case ERR_HTTP_CONNECTION_WAS_CLOSED:
                co_return;
            default:
                MANAPIHTTP_LOG("Unexpected error: {}", e.what());
        }
    }
    catch (const std::exception &e) {
        MANAPIHTTP_LOG("Unexpected error: {}", e.what());
    }
    co_await send_error_response(503, request_data, data->error.get());
    co_return;
}

manapi::future<void> manapi::net::http::base::send_error_response(const size_t &status, http::request_data_t &request_data, const http_handler_page *error) {
    if (!error) {
        // TODO: Default error page
        co_return;
    }

    co_await handle_request(error, request_data, status);
}

manapi::future<void> manapi::net::http::base::send_file(manapi::net::http::response &res, filesystem::async::fstream &f, ssize_t size) const {
    auto block_size = static_cast<ssize_t>(this->config->buffer_size().load());

    std::string write_block, read_block;

    write_block.resize(block_size);
    read_block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    async::parallel_run<ssize_t> parallel (this->site.async_context());

    ssize_t rhs;
    std::exception_ptr error{nullptr};

    if ((rhs = co_await f.read(write_block.data(), std::min(block_size, size - current))) <= 0) {
        co_return;
    }

    try {
        while (size > current) {
            /* add count of the chars which will be sent at this iterration */
            current += rhs;
            bool readsome = size > current;
            if (readsome) {
                parallel.run(f.read(read_block.data(), std::min(block_size, size - current)));
            }

            if ((rhs = co_await this->worker->fwrite (*this->connection, write_block.data(), rhs, !readsome)) <= 0) {
                /* failed to send */
                rhs = co_await parallel.get_or(0);
                break;
            }

            if ((rhs = co_await parallel.get_or(0)) <= 0) {
                break;
            }

            std::swap(write_block, read_block);

            //printf("%s STEP: %zi LEFT: %zi NEED: %zi CURRENT: %zi\n", res.get_file().data(), sent, left, size, current);
        }

        co_return;
    }
    catch (...) {
        error = std::current_exception();
    }

    co_await parallel.get_or(0);

    if (error) {
        std::rethrow_exception(error);
    }
}

manapi::future<void> manapi::net::http::base::send_file(manapi::net::http::response &res, filesystem::async::fstream &f, ssize_t size, std::vector<replace_founded_item> &replacers) const {
    std::string block;
    auto block_size = static_cast<ssize_t>(this->config->buffer_size());

    block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    ssize_t index;
    ssize_t replacer_index = 0;
    ssize_t current_key_index = 0;

    while (size > current) {
        const ssize_t left = size - current;

        block_size = static_cast<ssize_t>(block.size());

        if (left < block_size) {
            block_size = left;
        }

        auto rhs = co_await f.read(block.data(), block_size);
        block_size = rhs;

        index = f.tellg();

        ssize_t shift = 0;

        while (replacer_index != replacers.size() && index > replacers[replacer_index].pos.first + shift) {
            const ssize_t key_size = replacers[replacer_index].pos.second - replacers[replacer_index].pos.first + 1;
            const ssize_t start_index_in_block =
                    block_size - (index - (replacers[replacer_index].pos.first + shift + current_key_index));

            ssize_t index_in_block = start_index_in_block;

            for (; index_in_block < block_size && current_key_index < key_size; index_in_block++, current_key_index++) {
                if (current_key_index < replacers[replacer_index].value->size()) {
                    block[index_in_block] = replacers[replacer_index].value->at(current_key_index);
                    continue;
                }

                break;
            }

            // if KEY_SIZE >= VALUE_SIZE
            if (current_key_index == replacers[replacer_index].value->size()) {
                // shift <<
                // key_size <- the shift
                const ssize_t shifted_index_in_block = start_index_in_block + key_size;
                const ssize_t key_left_size = key_size - current_key_index;

                // shift chars
                ssize_t i = shifted_index_in_block;
                for (; i < block_size; i++, index_in_block++) {
                    block[index_in_block] = block[i];
                }

                if (index_in_block < block_size) {
                    const auto needed = block_size - index_in_block;

                    // we want to align block to block_size if it possible
                    if (index < size) {
                        // + 1 bcz we dont want to grab } special symbol at the end of the special key {{KEY}}
                        current = (index - block_size) + i;

                        f.seekg(current);
                        co_await f.read(block.data() + index_in_block, needed);
                    }
                    else {
                        // no data left
                        //shift -= key_left_size;
                        // decrease block_size
                        block_size -= key_left_size;
                    }

                    // update current
                    current = f.tellg() - block_size;
                } else {
                    current += key_left_size;
                    f.seekg(current);
                }

                index = f.tellg();

                replacer_index++;

                current_key_index = 0;

                continue;
            }
            // if KEY_SIZE < VALUE_SIZE
            if (current_key_index == key_size && current_key_index < replacers[replacer_index].value->size()) {
            next:
                // shift >>
                bool repeat = false;

                const auto value_left_size = static_cast<ssize_t>(replacers[replacer_index].value->size()) -
                                             current_key_index;

                // replace
                ssize_t i = index_in_block;
                ssize_t free_space = block_size - i;

                if (free_space < value_left_size) {
                    block_size += value_left_size - free_space;

                    if (block_size >= block.size()) {
                        block_size = static_cast<ssize_t>(block.size());
                        repeat = true;
                    }
                }

                for (; i < block_size && current_key_index < replacers[replacer_index].value->size();
                       i++, current_key_index++) {
                    char a = replacers[replacer_index].value->at(current_key_index);
                    block[i] = a;
                }

                if (repeat) {
                    ssize_t sent = co_await this->worker->fwrite(*this->connection, block.data(), block_size, (current + block_size) >= size);

                    if (sent < 0) {
                        // cannot to send
                        co_return;
                    }

                    current += sent;

                    shift += block_size - index_in_block;

                    // resolve
                    index_in_block = 0;

                    goto next;
                }

                if (i > block_size) {
                    // -printf("OK\n");
                }
                else {
                    free_space = block_size - i;
                    const ssize_t can_read = static_cast<ssize_t> (block.size()) - i;

                    // we want to read chars by size can_read
                    f.seekg(current + index_in_block - shift);
                    const ssize_t read = co_await f.read (block.data() + i, can_read);

                    block_size += read - free_space;

                    current = f.tellg() - block_size;

                    shift = 0;
                }

                index = f.tellg();

                replacer_index++;

                current_key_index = 0;

                continue;
            }

            break;
        }


        ssize_t sent = co_await this->worker->fwrite(*this->connection, block.data(), block_size, (current + block_size) >= size);

        if (sent < 0) {
            // cannot to send
            break;
        }

        current += sent;

        f.seekg(current);
    }
}

manapi::future<void> manapi::net::http::base::send_text(std::string_view text, ssize_t size) const {
    const char *current = text.data();
    ssize_t sent = size;

    while (sent != 0) {
        const ssize_t result = co_await this->worker->write(*connection, current, sent, true);

        if (result <= 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "Could not send the text: mask_write(...) = {}. Size: {}",
                                   result, sent);
        }

        if (result > sent) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "Total sent size > prepared sent size. {} > {}", result,
                                   sent);
        }

        sent -= result;

        current = current + result;
    }
}

manapi::future<bool> manapi::net::http::base::expect_header() {
    const auto expect = this->request_data.headers.find(HEADER.EXPECT);
    if (expect != this->request_data.headers.end()) {
        if (expect->second == "100-continue") {
            http::response resp (this->request_data, 100,  *this->config);
            co_await send_response(resp);
            co_return true;
        }

        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid Expect header");
    }
    co_return false;
}

std::string generate_cache_name(const std::string &file, const std::string &ext) {
    std::string name = std::format("{}-{:%Y_%m_%d_%H_%M_%S}-{}.{}", manapi::filesystem::basename(std::forward<const std::string&> (file)),
        manapi::time::current_time (true), manapi::string::random(25), ext);

    return std::move(name);
}

manapi::future<std::string> manapi::net::http::base::compress_file(const std::string &file, const std::string &folder, const std::string &compress, const std::function<future<bool>(const std::string &src, const std::string &dest)> & compressor) const {
    std::string filepath;

    // compressor
    auto lk = co_await this->site.cache_config_mx().lock_guard();
    auto cached = this->site.get_compressed_cache_file(file, compress);

    if (cached.empty()) {
        filesystem::mkdir(folder, true);
        filepath = folder + generate_cache_name(file, "deflate");

        if (!co_await compressor(file, filepath)) {
            co_return file;
        }

        this->site.set_compressed_cache_file(file, filepath, compress);
    }
    else {
        filepath = std::move(cached);
    }

    co_return filepath;
}

manapi::future<ssize_t> manapi::net::http::base::read(void *buf, ssize_t size) {
    co_return co_await this->worker->read (*this->connection, buf, size);
}

manapi::net::site & manapi::net::http::base::get_site() {
    return this->site;
}