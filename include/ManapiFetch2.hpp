#pragma once

#include "./ManapiUtils.hpp"
#include "./ManapiFetch.hpp"
#include "./json/ManapiJsonBuilder.hpp"
#include "./http/ManapiFileTransferInfo.hpp"
#include "./std/ManapiAsyncTimer.hpp"
#include "./std/ManapiCancellation.hpp"
#include "./http/ManapiFileTransferInfo.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

namespace manapi::net {
    class fetch2 {
        struct fetch_data;

        fetch2 ();
    public:
        ~fetch2();

        fetch2 (const fetch2 &n);

        fetch2 &operator=(const fetch2 &n);

        static manapi::future<manapi::status_or<fetch2>> fetch (std::string url, manapi::json params = manapi::json::object(), ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<fetch2>> fetch (std::string url, manapi::json params, std::optional<fetch_formdata> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<fetch2>> fetch (std::string url, manapi::json params, std::optional<std::string> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<fetch2>> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<fetch2>> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)>> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<fetch2>> fetch (std::string url, manapi::json params, std::optional<http::file_transfer_info> body, ctoken cancellation = nullptr);

        MANAPIHTTP_NODISCARD bool ok () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD uint16_t status () const MANAPIHTTP_NOEXCEPT;

        std::map<std::string, std::string, std::less<>> headers () MANAPIHTTP_NOEXCEPT;

        manapi::future<manapi::status> callback_async (std::function<manapi::future<ssize_t>(slice_view buffs, bool fin)> cb);

        manapi::future<manapi::status> callback_sync (std::function<ssize_t(char *buffer, ssize_t size)> cb);

        manapi::future<manapi::status_or<std::string>> text ();

        manapi::future<manapi::json_error::status_or<manapi::json>> json ();
    private:
        template<typename T>
        static manapi::future<manapi::status_or<fetch2>> fetch_ (std::string url, manapi::json params, T body, ctoken cancellation = nullptr);

        manapi::status setup_send_body (std::string &&data) MANAPIHTTP_NOEXCEPT;

        static manapi::future<manapi::status> continue_receiving (std::shared_ptr<fetch2::fetch_data> fetchdata);

        manapi::status setup_fetch (manapi::json params) MANAPIHTTP_NOEXCEPT;

        manapi::future<manapi::status> response ();

        std::shared_ptr<fetch_data> fetchdata;
    };
}

#endif