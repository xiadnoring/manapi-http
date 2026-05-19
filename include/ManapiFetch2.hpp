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
    class fetch2 : public std::enable_shared_from_this<fetch2> {

        template<typename T>
        friend manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> fetch2_init(std::string url, manapi::json params, T body, manapi::ctoken cancellation);

        fetch2 (std::string url, ctoken cancellation = nullptr);
    public:
        struct fetch_data;

        ~fetch2();

        static manapi::future<manapi::status_or<std::shared_ptr<fetch2>>> fetch (std::string url, manapi::json params = manapi::json::object(), ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<std::shared_ptr<fetch2>>> fetch (std::string url, manapi::json params, std::optional<fetch_formdata> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<std::shared_ptr<fetch2>>> fetch (std::string url, manapi::json params, std::optional<std::string> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<std::shared_ptr<fetch2>>> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<std::shared_ptr<fetch2>>> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)>> body, ctoken cancellation = nullptr);

        static manapi::future<manapi::status_or<std::shared_ptr<fetch2>>> fetch (std::string url, manapi::json params, std::optional<http::file_transfer_info> body, ctoken cancellation = nullptr);

        MANAPIHTTP_NODISCARD bool ok () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD uint16_t status () const MANAPIHTTP_NOEXCEPT;

        std::map<std::string, std::string, std::less<>> &headers () MANAPIHTTP_NOEXCEPT;

        manapi::future<manapi::status> callback_async (std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)> cb);

        manapi::future<manapi::status> callback_sync (std::move_only_function<ssize_t(char *buffer, std::size_t size)> cb);

        manapi::future<manapi::status_or<std::string>> text ();

        manapi::future<manapi::json_error::status_or<manapi::json>> json ();

        manapi::future<manapi::status> form (formdata_recv::onparam_cb_t cb);
    private:
        std::unique_ptr <fetch_data> fetchdata;
    };
}

#endif