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

#define FEATURE_EXISTS(x) x != nullptr

std::set<std::string> manapi::net::http::base::methods = {"POST", "GET", "HEAD", "OPTIONS", "TRACE", "PUT", "DELETE", "PATCH", "CONNECT"};

manapi::net::http::base::base(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site): site(site), worker(std::move(worker)), config(std::move(config)) {}

manapi::net::http::base::~base() = default;

void manapi::net::http::base::prepare() {}

manapi::future<void> manapi::net::http::base::parse_request(ssize_t j, ssize_t size) { co_return; }

manapi::future<void> manapi::net::http::base::send_response(manapi::net::http_response &res) {
    std::string response;
    std::string compressed;

    std::function<future<bool>(const std::string &src, const std::string &dest)> compressor = nullptr;

    auto &compress = res.get_compress();

    if (!compress.empty()) {
        if (!res.is_file() ||
            !res.get_partial_enabled() ||
            manapi::filesystem::get_size(res.get_file()) < config->get_partial_data_min_size()
        ) {
            compressor = site.get_compressor(compress);

            if (compressor) {
                res.set_header(HTTP_HEADER.CONTENT_ENCODING, compress);
            }
        }
    }

    response_features_t features = {
        .compress = compress,
        .compressor = compressor,
        .replacers = res.get_replacers()
    };

    // set time
    res.set_header(HTTP_HEADER.DATE, manapi::time::fmt_current("%a, %d %b %Y %H:%M:%S GMT", false));
    if (config->get_http_version() < versions::HTTP_v2) { res.set_header(HTTP_HEADER.CONNECTION, "close"); }

    if (res.is_file()) {
        co_await send_response_file(res, features);
    } else if (res.is_text()) {
        co_await send_response_text(res, features);
    } else if (res.is_proxy()) {
        co_await send_response_proxy(res, features);
    } else {
        co_await mask_response(res, true);
    }

    co_return;
}

manapi::future<void> manapi::net::http::base::execute_handler() {
    co_return;
}

manapi::future<void> manapi::net::http::base::send_response_file(manapi::net::http_response &res, response_features_t &features) {
    std::string filepath;

    if (FEATURE_EXISTS(features.compressor)) {
        if (FEATURE_EXISTS(features.replacers)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_SETTINGS_INCOMPATIBILITY, "replacers can not be using during compress");
        }

        filepath = co_await this->compress_file(res.get_file(), site.config_cache_dir, features.compress, features.compressor);
    }
    else {
        filepath = res.get_file();
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
            std::string mimetype = mime_by_file_path(res.get_file());
            if (mimetype.size() > sizeof ("text")) {
                if (strncmp("text", mimetype.data(), sizeof ("text") - 1) == 0) {
                    mimetype = stringify_header_value({{mimetype, {{"charset", "UTF-8"}}}});
                }
            }

            res.set_header(HTTP_HEADER.CONTENT_TYPE, mimetype);
        }
        std::vector<replace_founded_item> replacers;

        // get file size
        ssize_t fileSize = manapi::filesystem::get_size(filepath);
        ssize_t dynamicFileSize = fileSize;

        // replacers
        if (FEATURE_EXISTS(features.replacers)) {
            replacers = co_await found_replacers_in_file(this->site.async_context(), filepath, 0, fileSize, *res.get_replacers());

            for (const auto &replacer: replacers) {
                dynamicFileSize = static_cast<ssize_t> (
                    dynamicFileSize - (replacer.pos.second - replacer.pos.first + 1) + replacer.value->size());
            }
        }

        // partial enabled
        if (res.get_partial_enabled() && config->get_partial_data_min_size() <= fileSize) {
            if (FEATURE_EXISTS(features.compressor)) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_SETTINGS_INCOMPATIBILITY,
                                       "the compress '{}' with the partial content is not supported.",
                                       res.get_compress());
            }


            if (FEATURE_EXISTS(features.replacers)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_SETTINGS_INCOMPATIBILITY, "replacers can not be use with partial");
            }

            res.set_status(206, HTTP_STATUS.PARTIAL_CONTENT_206);
            res.set_header(HTTP_HEADER.ACCEPT_RANGES, "bytes");

            ssize_t start = 0,
                    back = fileSize - 1,
                    size;

            switch (res.ranges.size()) {
                case 1:
                    if (res.ranges[0].first != -1) {
                        start = res.ranges[0].first;
                    }

                if (res.ranges[0].second != -1) {
                    back = res.ranges[0].second;
                } else {
                    back = fileSize - 1;
                }
                case 0:
                    size = back - start + 1;

                res.set_header(HTTP_HEADER.CONTENT_LENGTH, std::to_string(size));
                res.set_header(HTTP_HEADER.CONTENT_RANGE, "bytes " + std::to_string(start) + '-' + std::to_string(back) + '/' + std::to_string(fileSize));

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
        } else {
            res.set_header(HTTP_HEADER.CONTENT_LENGTH, std::to_string(dynamicFileSize));

            if (co_await mask_response(res, false) >= 0) {
                if (replacers.empty()) {
                    // without replacers
                    co_await send_file(res, f, fileSize);
                } else {
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

manapi::future<void> manapi::net::http::base::send_response_text(manapi::net::http_response &res, response_features_t &features) {
    // may contains decoded / encoded body
    std::string plaintext = std::move(res.get_body());

    if (false || FEATURE_EXISTS(features.compressor)) {
        // encode content !
        //plaintext = co_await features.compressor(res.get_body(), {});
    }

    res.set_header(HTTP_HEADER.CONTENT_LENGTH, std::to_string(plaintext.size()));

    if (!res.ref_headers().contains(HTTP_HEADER.CONTENT_TYPE)) {
        res.set_header(HTTP_HEADER.CONTENT_TYPE, "text/html; charset=UTF-8");
    }

    if (co_await mask_response(res, false) >= 0) {
        co_await send_text(plaintext, plaintext.size());
    } else {
        MANAPIHTTP_LOG("{}", "mask_response(...) < 0");
    }
    co_return;
}

manapi::future<void> manapi::net::http::base::send_response_proxy(manapi::net::http_response &res, response_features_t &features) {
    //auto proxy = std::make_unique<fetch>(res.get_data());

    // proxy->handle_headers([this, &res](const std::map<std::string, std::string> &headers) -> void {
    //     res.set_status_code(200);
    //     res.set_status_message(HTTP_STATUS.OK_200);
    //
    //     if (headers.contains(HTTP_HEADER.CONTENT_LENGTH)) {
    //         res.set_header(HTTP_HEADER.CONTENT_LENGTH, headers.at(HTTP_HEADER.CONTENT_LENGTH));
    //     }
    //
    //     co_await mask_response(res, false);
    // });
    //
    // proxy->handle_body([this](char *buffer, const size_t &size) -> size_t {
    //     const ssize_t sw = worker->write(*connection, buffer, size, false);
    //
    //     if (sw < 0) {
    //         THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "Could not write pocket: {}", "mask_write() < 0");
    //     }
    //
    //     return sw;
    // });
    // // TODO: resolve
    // worker->write (*connection, nullptr, 0, true);
    // site.taskpool->append_task(std::move(proxy));
    co_return;
}

manapi::future<ssize_t> manapi::net::http::base::mask_response(manapi::net::http_response &resp, bool finish) {
    const auto rhs = co_await worker->response(*connection, resp, finish);
    co_return rhs;
}

manapi::future<void> manapi::net::http::base::handle_request(const http_handler_page *data, http::request_data_t &request_data, const size_t &status, const std::string &message) {
    manapi_socket_information socket_information = {
        .ip = inet_ntoa(reinterpret_cast<struct sockaddr_in *>(&connection->client)->sin_addr),
        .port = htons(reinterpret_cast<struct sockaddr_in *>(&connection->client)->sin_port)
    };
    http_request req (socket_information, request_data, this, config, data);
    http_response res(request_data, status, message, *config);
    try {
        // handle layers
        for (const auto &layer: data->layer) {
            co_await layer->handler(req, res);

            if (!req.get_propagation()) {
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
                    auto mime = mime_by_extension.find(ext);
                    bool binary = false;
                    if (mime != mime_by_extension.end()) {
                        binary = mime_partitial_data(mime->second);
                    }
                    res.set_compress_enabled(!binary);
                    res.set_partial_status(binary);
                    res.file(path);

                    try {
                        co_await send_response(res);
                        co_return;
                    } catch (const std::exception &e) {
                        MANAPIHTTP_LOG("Unexpected error: {}", e.what());
                    }

                    co_await send_error_response(503, request_data, HTTP_STATUS.SERVICE_UNAVAILABLE_503, data->error.get());
                    co_return;
                }
            }

            co_await send_error_response(404, request_data, HTTP_STATUS.NOT_FOUND_404, data->error.get());
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
    co_await send_error_response(503, request_data, HTTP_STATUS.SERVICE_UNAVAILABLE_503, data->error.get());
    co_return;
}

manapi::future<void> manapi::net::http::base::send_error_response(const size_t &status, http::request_data_t &request_data, const std::string &message, const http_handler_page *error) {
    if (error == nullptr) {
        // TODO: Default error page
        co_return;
    }

    co_await handle_request(error, request_data, status, message);
}

manapi::future<void> manapi::net::http::base::send_file(manapi::net::http_response &res, filesystem::async::fstream &f, ssize_t size) const {
    auto block_size = static_cast<ssize_t>(config->get_socket_block_size());
    std::string block;
    block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    while (size > current) {
        const ssize_t left = size - current;

        if (left < block_size) {
            block_size = left;
        }

        auto rhs = co_await f.read(block.data(), static_cast<ssize_t> (block_size));

        const ssize_t sent = co_await this->worker->write (*this->connection, block.data(), rhs, (current + rhs) >= size);

        if (sent <= 0) {
            // cannot to send
            break;
        }

        current += sent;

        //printf("%s STEP: %zi LEFT: %zi NEED: %zi CURRENT: %zi\n", res.get_file().data(), sent, left, size, current);
    }
}

manapi::future<void> manapi::net::http::base::send_file(manapi::net::http_response &res, filesystem::async::fstream &f, ssize_t size, std::vector<replace_founded_item> &replacers) const {
    std::string block;
    auto block_size = static_cast<ssize_t>(config->get_socket_block_size());

    block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    ssize_t index;
    ssize_t replacer_index = 0;
    ssize_t current_key_index = 0;

    while (size > current) {
        const ssize_t left = size - current;

        if (left < block_size) {
            block_size = left;
        }

        auto rhs = co_await f.read(block.data(), block_size);

        if (rhs != block_size) {
            // TODO: rhs maybe not equal block_size. And that's bad
            MANAPIHTTP_LOG2("FIXME: TODO: rhs maybe not equal block_size. And that's bad");
        }

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
                    } else {
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

                    if (block_size >= config->get_socket_block_size()) {
                        block_size = static_cast<ssize_t>(config->get_socket_block_size());
                        repeat = true;
                    }
                }

                for (; i < block_size && current_key_index < replacers[replacer_index].value->size();
                       i++, current_key_index++) {
                    char a = replacers[replacer_index].value->at(current_key_index);
                    block[i] = a;
                }

                if (repeat) {
                    ssize_t sent = co_await worker->write(*connection, block.data(), block_size, (current + block_size) >= size);

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
                } else {
                    free_space = block_size - i;
                    ssize_t can_read = static_cast<ssize_t> (config->get_socket_block_size()) - i;

                    // we want to read chars by size can_read
                    f.seekg(current + index_in_block - shift);
                    ssize_t read = co_await f.read (block.data() + i, can_read);

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


        ssize_t sent = co_await this->worker->write(*this->connection, block.data(), block_size, (current + block_size) >= size);

        if (sent < 0) {
            // cannot to send
            break;
        }

        current += sent;

        f.seekg(current);
    }
}

manapi::future<void> manapi::net::http::base::send_text(const std::string &text, const size_t &size) const {
    const char *current = text.data();
    size_t sent = size;

    while (sent != 0) {
        const ssize_t result = co_await worker->write(*connection, current, sent, true);

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

manapi::future<void> manapi::net::http::base::expect_header() {
    const auto expect = request_data.headers.find(HTTP_HEADER.EXPECT);
    if (expect != request_data.headers.end()) {
        if (expect->second == "100-continue") {
            http_response resp (request_data, 100, HTTP_STATUS.CONTINUE_100, *config);
            co_await send_response(resp);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid Expect header");
        }
    }
}

std::string generate_cache_name(const std::string &file, const std::string &ext) {
    std::string name = manapi::filesystem::basename(std::forward<const std::string&> (file));

    name += manapi::time::fmt_current ("-%Y_%m_%d_%H_%M_%S-", true) + manapi::string::random(25);
    name += '.';
    name += ext;

    return std::move(name);
}

manapi::future<std::string> manapi::net::http::base::compress_file(const std::string &file, const std::string &folder, const std::string &compress, const std::function<future<bool>(const std::string &src, const std::string &dest)> & compressor) const {
    std::string filepath;

    // compressor
    auto lk = co_await this->site.cache_config_mx.lock_guard();
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

manapi::future<ssize_t> manapi::net::http::base::read(void *buf, size_t size) {
    auto rhs = co_await this->worker->read (*this->connection, buf, size);
    co_return rhs;
}
