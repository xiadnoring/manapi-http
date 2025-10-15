#pragma once

#include "ManapiHttp.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "include/ManapiFetch2.hpp"
#include "std/ManapiRef.hpp"

void init_http_server (manapi::net::http::server &router, std::string const &folder);


#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <QAbstractEventDispatcher>
#include <QSocketNotifier>

struct uv_loop_s;
struct uv_async_s;
struct uv_poll_s;
struct uv_timer_s;
struct uv_handle_s;

namespace manapi {
    template<typename T>
class scope_ptr {
    private:
        T* ptr;
        bool own;

    public:
        explicit scope_ptr(T* p = nullptr, bool ownership = true)
            : ptr(p), own(ownership) {}

        ~scope_ptr() {
            if (this->own) {
                delete this->ptr;
            }
        }

        scope_ptr(const scope_ptr&) = delete;

        scope_ptr& operator=(const scope_ptr&) = delete;

        scope_ptr(scope_ptr&& other) MANAPIHTTP_NOEXCEPT
            : ptr(other.ptr), own(other.own) {
            other.ptr = nullptr;
            other.own = false;
        }

        scope_ptr& operator=(scope_ptr&& other) MANAPIHTTP_NOEXCEPT {
            if (this != &other) {
                if (this->own) {
                    delete this->ptr;
                }
                this->ptr = other.ptr;
                this->own = other.own;
                other.ptr = nullptr;
                other.own = false;
            }
            return *this;
        }

        void ownership(bool ownership) {
            this->own = ownership;
        }

        bool ownership() const {
            return this->own;
        }

        T *release () {
            this->own = false;
            return this->ptr;
        }

        T* get() const {
            return this->ptr;
        }

        T* operator->() const {
            return this->ptr;
        }

        T& operator*() const {
            return *this->ptr;
        }
    };
}

namespace iz::Eventing
{
class LibUvEventDispatcher final : public QAbstractEventDispatcher
{
    Q_DISABLE_COPY(LibUvEventDispatcher)

public:
    LibUvEventDispatcher ();
    // ctor
    LibUvEventDispatcher(QObject* parent);

    // dtor
    ~LibUvEventDispatcher() override;

    void unsubscribe () MANAPIHTTP_NOEXCEPT;

    // QAbstractEventDispatcher interface -->

    bool processEvents(QEventLoop::ProcessEventsFlags flags) override;

    void registerSocketNotifier(QSocketNotifier* notifier) override;
    void unregisterSocketNotifier(QSocketNotifier* notifier) override;

    void registerTimer(int timerId, qint64 interval, Qt::TimerType timerType, QObject* object) override;
    bool unregisterTimer(int timerId) override;
    bool unregisterTimers(QObject* object) override;
    QList<QAbstractEventDispatcher::TimerInfo> registeredTimers(QObject* object) const override;

    int remainingTime(int timerId) override;

    void wakeUp() override;
    void interrupt() override;

    // <-- QAbstractEventDispatcher interface

    // prototype - not currently used -->

    void enableSocketNotifier(QSocketNotifier* notifier);
    void disableSocketNotifier(QSocketNotifier* notifier);

    // prototype - not currently used <--

    // poller struct data
    struct PollerData {
        explicit PollerData(int socketDescriptor, uv_poll_s* uvPollData, int events, LibUvEventDispatcher* context)
            : events(events)
            , socketDescriptor(socketDescriptor)
            , uvPollData(uvPollData)
            , context(context)
        {
        }

        int events;
        int socketDescriptor;
        uv_poll_s* uvPollData;
        QSocketNotifier* readNotifier{ nullptr };
        QSocketNotifier* writeNotifier{ nullptr };
        LibUvEventDispatcher* context;
    };

    struct poller_data_t {
        uint32_t refcnt;
        LibUvEventDispatcher *context;
        QSocketNotifier *read_notifier;
        QSocketNotifier *write_notifier;
        manapi::ev::shared_io watcher;
        int flags;
    };

private:
    // morphs qt event type to libuv's
    int qtouv(QSocketNotifier::Type qtEventType) const;

    struct timer_info_t {
        Qt::TimerType type;
        int interval;
    };

    struct timer_data_t {
        uint32_t refcnt;
        LibUvEventDispatcher *context;
        std::map<int, timer_info_t> ids;
        QObject *parent;
    };

    // libuv' async handle
    manapi::ev::shared_async m_wakeupHandle;

    // amount of processed callbacks in single processEvents() call
    std::uint64_t m_processedCallbacks;

    std::map<int, std::pair<manapi::timer, manapi::reference<timer_data_t>>> m_timers;

    std::set<poller_data_t *> m_pollers;

    std::atomic<int> flags;
};
}   // namespace iz::Eventing

Q_DECLARE_METATYPE(iz::Eventing::LibUvEventDispatcher::PollerData*);