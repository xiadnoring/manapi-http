#pragma once

#include "./ManapiUtils.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY
# define MANAPIHTTP_FETCH_SUPPORT

# include <string>
# include <vector>
# include <map>
# include <functional>

# include "./json/ManapiJson.hpp"
# include "./http/ManapiHttpRequest.hpp"
# include "./std/ManapiAsyncParallelRun.hpp"
# include "./http/ManapiFileTransferInfo.hpp"
# include "./std/ManapiCancellation.hpp"

namespace manapi::net {
   class fetch;

   std::size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata);
   std::size_t curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p);
   std::size_t curl_read_handler (char *buffer, std::size_t size, std::size_t nitems, void *user_p);
   manapi::future<manapi::status> curl_send_async_body (std::shared_ptr<manapi::net::fetch> parent, bool finish);

    /**
     * A Fetch FormData interface to work with the Fetch and Fetch2 API.
     * Based on the curl formdata
     */
    class fetch_formdata {
    public:
        struct multipart_param_value_file {
            std::move_only_function<size_t (void *buff, size_t size)> callback;
            std::size_t filesize;
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

        fetch_formdata(fetch_formdata &&fd) MANAPIHTTP_NOEXCEPT;

        fetch_formdata &operator=(fetch_formdata &&fd) MANAPIHTTP_NOEXCEPT;

        /**
         * send the plain field
         *
         * @param name the plain field name
         * @param value the plain field value
         */
        manapi::status set_text (std::string name, std::string value) MANAPIHTTP_NOEXCEPT;

        /**
         * send the file by its filepath
         *
         * @param name the file field name
         * @param filepath the file path
         */
        manapi::status set_file (std::string name, std::string filepath) MANAPIHTTP_NOEXCEPT;

        /**
         * set the callback
         *
         * @param name the callback field name
         * @param size the data size
         * @param cb the callback
         */
        manapi::status set_callback (std::string name, std::size_t size, std::move_only_function<size_t (void *buff, size_t buff_size)> cb) MANAPIHTTP_NOEXCEPT;

        /**
         * clear current state
         */
        void clear () MANAPIHTTP_NOEXCEPT;

        /**
         * get the first field
         *
         * @return the first field
         */
        tdata::iterator begin() MANAPIHTTP_NOEXCEPT;

        /**
         * get the field boundary
         *
         * @return the field boundary
         */
        tdata::iterator end() MANAPIHTTP_NOEXCEPT;
    private:
        /**
         * storage
         */

        tdata mdata;
    };

    /**
     * Fetch API for C++. Based on cURL
     */
    class fetch : public std::enable_shared_from_this <fetch> {
        friend std::size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata);
        friend std::size_t curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p);
        friend std::size_t curl_read_handler (char *buffer, std::size_t size, std::size_t nitems, void *user_p);
        friend manapi::future<manapi::status> curl_send_async_body (std::shared_ptr<manapi::net::fetch> parent, bool finish);

        fetch(std::string url, manapi::ctoken cancellation = nullptr);
    public:
        struct data_t;


        ~fetch();

        manapi::status init (std::string url, manapi::ctoken cancellation = nullptr);

        /**
         * Initialize Fetch API
         *
         * @param url the site URL
         * @param cancellation the cancellation token
         */
        static manapi::status_or<std::shared_ptr<fetch>> create (std::string url, manapi::ctoken cancellation = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the sync callback to recv body from the request
         *
         * @param handler the callback
         */
        manapi::status handle_body(std::move_only_function<ssize_t(char *, std::size_t)> handler) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the async callback to recv body from the request
         *
         * @param handler the callback
         */
        manapi::status handle_async_body(std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool fin)> handler) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the sync callback to recv headers from the request
         *
         * @param handler the callback
         */
        manapi::status handle_headers (std::move_only_function<bool(std::map <std::string, std::string, std::less<>>)> handler) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the async callback to recv headers from the request
         *
         * @param handler the callback
         */
        manapi::status handle_async_headers (std::move_only_function<manapi::future<bool>(std::map<std::string, std::string, std::less<>>)> handler) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the enabled status of ALPN
         *
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status enable_alpn (bool status);

        /**
         * Set the enabled status of HTTP/3
         *
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status enable_http3 ();

        /**
         * Set the enabled status of HTTP/2
         *
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status enable_http2 ();

        /**
         * Set the enabled status of HTTP/1.1
         *
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status enable_http1_1 ();

        /**
         * Set the formdata to send it with the response
         *
         * @param params the form data
         */
        manapi::status body (fetch_formdata params) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the method for the response
         *
         * @param method the HTTP method
         */
        manapi::status method (std::string_view method) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the string data to send it with the response
         *
         * @param data the string data
         */
        manapi::status body (std::string data) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the file data to send it with the response
         *
         * @param file_info the file data
         * @return the future with the status which returns OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::future<status> body (http::file_transfer_info file_info);

        /**
         * Set the async callback to send body with the response
         *
         * @param handler the async callback
         */
        manapi::status async_body (std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)> handler) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the sync callback to send body with the response
         *
         * @param handler the sync callback
         */
        manapi::status body (std::move_only_function<ssize_t(char *, std::size_t)> handler) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the headers for the response
         *
         * @param headers the headers
         */
        manapi::status headers (std::map <std::string, std::string, std::less<>> headers) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the headers in the JSON format for the response
         * @param headers the headers in the JSON format
         */
        manapi::status json_headers (manapi::json headers) MANAPIHTTP_NOEXCEPT;

        /**
         * Get a custom cURL object for custom configuration
         *
         * @return
         */
        void *custom () MANAPIHTTP_NOEXCEPT;

        /**
         * Set the enabled status of the verify peer
         *
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status enable_verify_peer (bool status) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the enabled status of the verify host
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status enable_verify_host (bool status) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the enabled status of the verify host
         * @param status the enabled status
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status verbose (bool status) MANAPIHTTP_NOEXCEPT;

        /**
         * Set the status of the timeout
         * @param seconds duration in seconds
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status timeout (std::size_t seconds) MANAPIHTTP_NOEXCEPT;

        /**
         * pause the receiving the data from the response
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status break_write_loop () MANAPIHTTP_NOEXCEPT;

        /**
         * continue the receiving the data from the response
         * @return OK if there's no error, otherwise it returns InvalidArgument
         */
        manapi::status continue_write_loop () MANAPIHTTP_NOEXCEPT;

        void async_recv_nodelay (bool enabled) MANAPIHTTP_NOEXCEPT;

        /**
         * Get the current HTTP status code
         * @return the HTTP status code
         */
        MANAPIHTTP_NODISCARD uint16_t status_code () const MANAPIHTTP_NOEXCEPT;

        /**
         * Send the request to the server
         * @return OK if there's no error, otherwise it returns InvalidArgument, AlreadyExists
         */
        future<manapi::status> async_doit();

        /**
         * Calls async_doit() and returns the string result
         * @return the string result
         */
        future<manapi::status_or<std::string>> text();

        /**
         * Calls async_doit() and returns the JSON result
         * @return the JSON result
         */
        future<manapi::status_or<manapi::json>> json();

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
        // manapi::status mheader (std::string_view key, std::string_view value) MANAPIHTTP_NOEXCEPT;

        std::unique_ptr <data_t> m_data;
    };
}

#endif