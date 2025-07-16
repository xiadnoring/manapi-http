#pragma once

#include "../ManapiUtils.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY
#define MANAPIHTTP_FETCH_SUPPORT

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "../ManapiUtils.hpp"
#include "ManapiTask.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiHttpRequest.hpp"
#include "../async/ManapiAsyncParallelRun.hpp"
#include "../components/ManapiFileTransferInfo.hpp"

namespace manapi::net {
    /**
     * A Fetch FormData interface to work with the Fetch and Fetch2 API.
     * Based on the curl formdata
     */
    class fetch_formdata {
    public:
        struct multipart_param_value_file {
            std::move_only_function<size_t (void *buff, size_t size)> callback;
            long long filesize;
        };

        enum multipart_param_type {
            PARAM_DEFAULT = 0,
            PARAM_FILE ,
            PARAM_CALLBACK
        };

        struct multipart_param_value {
            std::string strdata{};
            multipart_param_value_file filedata{};
            multipart_param_type type = PARAM_DEFAULT;
        };

        typedef std::map <std::string, multipart_param_value> tdata;

        /**
         * initialize fetch formdata
         */
        fetch_formdata();

        /**
         * deconstructor fetch formdata
         */
        ~fetch_formdata();

        fetch_formdata(fetch_formdata &&fd) MANAPI_EV_NOEXPECT;

        fetch_formdata &operator=(fetch_formdata &&fd) MANAPI_EV_NOEXPECT;

        /**
         * send the plain field
         *
         * @param name the plain field name
         * @param value the plain field value
         */
        void setdata (std::string name, std::string value);

        /**
         * send the file by its filepath
         *
         * @param name the file field name
         * @param filepath the file path
         */
        void setfile (std::string name, std::string filepath);

        /**
         * set the callback
         *
         * @param name the callback field name
         * @param size the data size
         * @param cb the callback
         */
        void setcallback (std::string name, ssize_t size, std::move_only_function<size_t (void *buff, size_t buff_size)> cb);

        /**
         * clear current state
         */
        void clear ();

        /**
         * get the first field
         *
         * @return the first field
         */
        tdata::iterator begin();

        /**
         * get the field boundary
         *
         * @return the field boundary
         */
        tdata::iterator end();
    private:
        /**
         * storage
         */

        tdata mdata;
    };

    /**
     * Fetch API for C++. Based on cURL
     */
    class fetch : public task {
        struct shared_data;
    public:
        struct data_t;

        /**
         * Initialize Fetch API
         *
         * @param url the site URL
         * @param cancellation the cancellation token
         */
        explicit fetch(std::string url, manapi::async::cancellation_action cancellation = nullptr);

        fetch (const fetch &n);

        fetch &operator=(const fetch &n);

        ~fetch() override;

        /**
         * Set the sync callback to recv body from the request
         *
         * @param handler the callback
         */
        void handle_body(std::move_only_function<ssize_t(char *, ssize_t)> handler);

        /**
         * Set the async callback to recv body from the request
         *
         * @param handler the callback
         */
        void handle_async_body(std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool fin)> handler);

        /**
         * Set the sync callback to recv headers from the request
         *
         * @param handler the callback
         */
        void handle_headers (std::move_only_function<bool(std::map <std::string, std::string, std::less<>>)> handler);

        /**
         * Set the async callback to recv headers from the request
         *
         * @param handler the callback
         */
        void handle_async_headers (std::move_only_function<manapi::future<bool>(std::map<std::string, std::string, std::less<>>)> handler);

        /**
         * Set the enabled status of ALPN
         *
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status enable_alpn (bool status);

        /**
         * Set the enabled status of HTTP/3
         *
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status enable_http3 ();

        /**
         * Set the enabled status of HTTP/2
         *
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status enable_http2 ();

        /**
         * Set the enabled status of HTTP/1.1
         *
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status enable_http1_1 ();

        /**
         * Set the formdata to send it with the response
         *
         * @param params the form data
         */
        void body (fetch_formdata params);

        /**
         * Set the method for the response
         *
         * @param method the HTTP method
         */
        void method (std::string_view method);

        /**
         * Set the string data to send it with the response
         *
         * @param data the string data
         */
        void body (std::string data);

        /**
         * Set the file data to send it with the response
         *
         * @param file_info the file data
         * @return the future with the status which returns OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::future<error::status> body (file_transfer_info file_info);

        /**
         * Set the async callback to send body with the response
         *
         * @param handler the async callback
         */
        void async_body (std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)> handler);

        /**
         * Set the sync callback to send body with the response
         *
         * @param handler the sync callback
         */
        void body (std::move_only_function<ssize_t(char *, ssize_t)> handler);

        /**
         * Set the headers for the response
         *
         * @param headers the headers
         */
        void headers (std::map <std::string, std::string, std::less<>> headers);

        /**
         * Set the headers in the JSON format for the response
         * @param headers the headers in the JSON format
         */
        void json_headers (manapi::json headers);

        /**
         * Get a custom cURL object for custom configuration
         *
         * @return
         */
        const std::shared_ptr<CURL> &custom ();

        /**
         * Set the enabled status of the verify peer
         *
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status enable_verify_peer (bool status);

        /**
         * Set the enabled status of the verify host
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status enable_verify_host (bool status);

        /**
         * Set the enabled status of the verify host
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status verbose (bool status);

        /**
         * Set the status of the timeout
         * @param seconds duration in seconds
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status timeout (std::size_t seconds);

        /**
         * pause the receiving the data from the response
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status break_write_loop ();

        /**
         * continue the receiving the data from the response
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::error::status continue_write_loop ();

        /**
         * Get the current HTTP status code
         * @return the HTTP status code
         */
        [[nodiscard]] size_t status_code () const;

        /**
         * Send the request to the server
         * @return OK if there's no error, otherwise it returns InvalidArgument, AlreadyExists
         */
        future<manapi::error::status> async_doit();

        /**
         * Calls async_doit() and returns the string result
         * @return the string result
         */
        future<std::string> text();

        /**
         * Calls async_doit() and returns the JSON result
         * @return the JSON result
         */
        future<manapi::json> json();

        /**
         * Get the headers from the response
         * @return
         */
        std::map <std::string, std::string, std::less<>> headers();

        /**
         * clear current state
         */
        void clear ();
    private:
        /**
         * Internal clear()
         */
        void clear_ ();

        static manapi::future<bool> handle_body_verify (std::shared_ptr<shared_data> data);

        static manapi::future<void> handle_sync_body_finish(std::shared_ptr<shared_data> data, bool finish);

        static manapi::future<void> handle_async_body_finish(std::shared_ptr<shared_data> data, bool finish);

        static std::size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata);

        static std::size_t curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p);

        static std::size_t curl_read_handler (char *buffer, std::size_t size, std::size_t nitems, void *user_p);

        void setup_parallel_task ();

        void header_ (std::string key, std::string value);

        void default_setup_curl_ ();

        future<CURLcode> async_curl_perform ();

        std::shared_ptr<data_t> data;
    };
}

#endif