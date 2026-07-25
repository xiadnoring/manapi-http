#pragma once

#include <memory>

#include "./ManapiSlice.hpp"
#include "./ManapiAsync.hpp"

namespace manapi {
    struct channel_data_t;

    std::shared_ptr<channel_data_t> create_channel (std::size_t max_size);

    class channel_send : public std::enable_shared_from_this<channel_send> {
    public:
        channel_send (const std::shared_ptr<channel_data_t> &data);

        ~channel_send();

        manapi::future<manapi::status> send (manapi::slice &&z, bool fin);

        MANAPIHTTP_NODISCARD bool is_finished () const MANAPIHTTP_NOEXCEPT;

        void finish () MANAPIHTTP_NOEXCEPT;
    private:
        std::shared_ptr<channel_data_t> m_data;
    };

    class channel_recv : public std::enable_shared_from_this<channel_recv> {
    public:
        channel_recv (const std::shared_ptr<channel_data_t>& data);

        ~channel_recv();

        manapi::future<manapi::status_or<manapi::slice>> recv (ssize_t max_size = -1);

        MANAPIHTTP_NODISCARD bool is_finished () const MANAPIHTTP_NOEXCEPT;

        void finish () MANAPIHTTP_NOEXCEPT;
    private:
        std::shared_ptr<channel_data_t> m_data;
    };
}
