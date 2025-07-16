#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiFetch.hpp"
#include "../ManapiJsonBuilder.hpp"
#include "../components/ManapiFileTransferInfo.hpp"
#include "../async/ManapiAsyncTimer.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

namespace manapi::net {
    class fetch2 {
        struct fetch_data;
    public:
        ~fetch2();

        fetch2 (std::string url, async::cancellation_action cancellation = nullptr);

        fetch2 (const fetch2 &n);

        fetch2 &operator=(const fetch2 &n);

        static manapi::future<fetch2> fetch (std::string url, manapi::json params = manapi::json::object(), async::cancellation_action cancellation = nullptr);

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<fetch_formdata> body, async::cancellation_action cancellation = nullptr);

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<std::string> body, async::cancellation_action cancellation = nullptr);

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body, async::cancellation_action cancellation = nullptr);

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)>> body, async::cancellation_action cancellation = nullptr);

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<file_transfer_info> body, async::cancellation_action cancellation = nullptr);

        [[nodiscard]] bool ok () const;

        [[nodiscard]] size_t status () const;

        std::map<std::string, std::string, std::less<>> headers ();

        manapi::future<> callback_async (std::function<manapi::future<ssize_t>(slice_view buffs, bool fin)> cb);

        manapi::future<> callback_sync (std::function<ssize_t(char *buffer, ssize_t size)> cb);

        manapi::future<std::string> text ();

        manapi::future<manapi::json> json ();
    private:
        template<typename T>
        static manapi::future<fetch2> fetch_ (std::string url, manapi::json params, T body, async::cancellation_action cancellation = nullptr);

        void setup_send_body (std::string &&data);

        static manapi::future<> continue_receiving (std::shared_ptr<fetch2::fetch_data> fetchdata);

        void setup_fetch (manapi::json params);

        manapi::future<> response ();

        std::shared_ptr<fetch_data> fetchdata;
    };
}

#endif