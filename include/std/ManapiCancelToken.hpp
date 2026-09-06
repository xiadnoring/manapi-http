#pragma once

#include <memory>
#include <functional>

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"

namespace manapi {
    struct ctoken_data_t;

    class ctoken {
    public:
        ctoken (std::nullptr_t);

        ctoken ();

        static ctoken unit (ctoken cancellation);

        ctoken sub () const;

        ctoken (ctoken &&n) MANAPIHTTP_NOEXCEPT;

        ctoken &operator=(ctoken &&n) MANAPIHTTP_NOEXCEPT;

        ctoken (const ctoken &n);

        ctoken &operator=(const ctoken &n);

        ~ctoken();

        explicit operator bool () const;

        /**
         * Reset it
         *
         * @return self
         */
        ctoken &operator=(std::nullptr_t);

        /**
         * Reuse of the cancellation object
         *
         * @param ctx Async context
         */
        void reset ();

        /**
         * Set a callback that will be called while canceling
         *
         * @param callback Callback that will be called while canceling
         * @note It must be called only in @code event loop thread@endcode
         */
        void cancel_callback (std::move_only_function<void()> callback) MANAPIHTTP_NOEXCEPT;

        /**
         * Set an other cancellation that will be cancelled while canceling
         *
         * @param cancellation Other cancellation
         * @note It must be called only in @code event loop thread@endcode
         */
        void cancel_callback (ctoken cancellation);

        /**
         * Send a canellation signal
         *
         * @throw manapi::exception with error code @code ERR_CANCELLATION_FAILED@endcode in case of failure while sending a signal
         */
        void cancel () MANAPIHTTP_NOEXCEPT;

        /**
         * Request a callback to cancel your action
         */
        void ask_cancel_callback () MANAPIHTTP_NOEXCEPT;

        /**
         * Returns true if token was cancelled
         */
        MANAPIHTTP_NODISCARD bool is_cancelled () const MANAPIHTTP_NOEXCEPT;

        /**
         * Set timeout in milliseconds
         *
         * @param timeout Timeout in milliseconds
         * @return Ok on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status timeout (size_t timeout) MANAPIHTTP_NOEXCEPT;

        /**
         * Set timeout in millseconds
         * @param timeout Timeout in millseconds
         * @return this
         */
        ctoken &tm (size_t timeout);

        /**
         * It will return a message stating that it asks
         * a callback for cancellation
         *
         * @return a message stating that it asks a callback for cancellation
         */
        MANAPIHTTP_NODISCARD bool contains_cancel_callback () const;

        /**
         * Disable cancellation without calling the callback to cancel
         */
        void disable ();

        MANAPIHTTP_NODISCARD const std::shared_ptr<ctoken_data_t> &data () const MANAPIHTTP_NOEXCEPT;
    private:
        std::shared_ptr<ctoken_data_t> m_data;
    };
}
