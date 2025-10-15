#include "./handlers.hpp"

#include "ManapiProcess.hpp"
#include "hash/ManapiSHA256.hpp"
#include "std/ManapiRef.hpp"

void init_http_server(manapi::net::http::server &router, std::string const &folder) {
    using http = manapi::net::http::server;

    router.GET("/", folder, [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        resp.compress_enabled(true);
        resp.compress("zstd");
        co_return;
    });

    router.GET("/http", [] (http::req &req, http::uresp resp) -> void {
        std::string http = "";
        auto version = req.http();
        switch (version) {
            case manapi::net::http::versions::HTTP_v0_9: http = "0.9"; break;
            case manapi::net::http::versions::HTTP_v1_0: http = "1.0"; break;
            case manapi::net::http::versions::HTTP_v1_1: http = "1.1"; break;
            case manapi::net::http::versions::HTTP_v2: http = "2"; break;
            case manapi::net::http::versions::HTTP_v3: http = "3"; break;
            default: http = "uknown";
        }
        resp->replacers({
            {"version", std::move(http)},
        });

        resp->file("/home/Timur/Desktop/WorkSpace/ManapiHTTP/examples/http.html");
        resp.finish();
    });


    router.GET("/json", [] (http::req &req, http::uresp resp) -> void {
        return resp->json({{"message", "Hello, World!"}}).unwrap();
    });

    router.POST("/json", [] (http::req &req, http::resp &resp) -> manapi::future<> {
        manapi::json_mask mask = {
            {"hello", R"({string(>=5 <10)})"}
        };

        auto data = co_await req.json(&mask);

        if (!data) {
            co_return resp.json({{"error", true}, {"msg", data.message()},
                {"pos", data.pos()}, {"path", data.path()}, {"data", data.additional_data()}}).unwrap();
        }

        co_return resp.json(data.unwrap()).unwrap();
    });

    router.GET("/", [&folder] (http::req &req, http::resp &resp) -> manapi::future<> {
        co_return resp.file(manapi::filesystem::path::join(folder, "index.html")).unwrap();
    });


    router.GET("/noise", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t len = 10737418240 / 2;

        resp.header(std::string{manapi::net::http::header::CONTENT_LENGTH}, std::to_string(len));
            co_return resp.callback_sync([current = (ssize_t)0, len] (char *buffer, ssize_t size, bool &flg) mutable
                    -> ssize_t {
                size = std::min(size, len - current);
                memset(buffer, '\0', size);
                len -= size;
                if (!len)
                    flg = true;
                return size;
            }).unwrap();
    });

    router.GET("/noise/[size]", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        char *end;
        ssize_t len = std::strtoll(req.param("size").unwrap().data(), &end, 10);

        resp.header(std::string{manapi::net::http::header::CONTENT_LENGTH}, std::to_string(len));
        co_return resp.callback_sync([current = (ssize_t)0, len] (char *buffer, ssize_t size, bool &flg) mutable
                    -> ssize_t {
                size = std::min(size, len - current);
                memset(buffer, '\0', size);
                len -= size;
                if (!len)
                    flg = true;
                return size;
            }).unwrap();
    });

    router.GET("/+error", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        resp.replacers({
            {"status_code", std::to_string(resp.status_code())},
            {"status_message", std::string{resp.status_message()}}
        }).unwrap();

        co_return resp.file("/home/Timur/Desktop/WorkSpace/ManapiHTTP/examples/error.html").unwrap();
    });

    router.POST ("/uploadtest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            ssize_t result = 0;
            auto c = std::chrono::steady_clock::now();
            try {
                (co_await req.callback_sync([&c, &result, &cb] (const char *buffer, ssize_t size, bool fin)
                    -> ssize_t {
                    result += size;
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << "\n";
                    }
                    return size;
                })).unwrap();
            }
            catch (std::exception const &e) {
                std::cout << e.what() << "\n";
            }
            auto bb = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c);
            std::string err = std::format("{}\n", ((double)result / 1024 / 1024) / ((double)bb.count()/1000));
            auto b = manapi::slice::create(err.size()).unwrap();
            b.copy_from(err.data(), 0, err.size());
            co_await cb(b, true);
        }).unwrap();
    });


    router.GET("/download", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO").unwrap();
    });

    router.POST ("/echo", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        if (req.contains_header(std::string{manapi::net::http::header::CONTENT_LENGTH}))
            resp.header(std::string{manapi::net::http::header::CONTENT_LENGTH},
                std::string{req.header(manapi::net::http::header::CONTENT_LENGTH).unwrap()}).unwrap();

        std::size_t sss = 0;
        co_return resp.callback_stream([&sss, &resp, &req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            auto fs = manapi::filesystem::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4").unwrap();
            auto rhs = co_await fs.open(manapi::ev::FS_O_RDONLY);
            rhs.unwrap();
            (co_await req.callback_async([&sss, cb = std::move(cb), fs] (manapi::slice_view buffs, bool fin) mutable
                -> manapi::future<ssize_t> {
                auto buffs2 = manapi::async::current()->memory_fabric().slice(buffs.size()).unwrap();
                buffs2.resize(buffs.size());
                assert(buffs.size() == buffs2.size());
                auto res = co_await fs.fread(buffs2);
                assert(res == buffs.size());
                auto cmp = buffs.cmp(buffs2);
                assert(!cmp);

                //sum += size;
                //std::cout << sum << " " << size << " " << fin << "\n";
                auto result = co_await cb (buffs, fin);
                sss+=result;
                fs.seekg(fs.tellg() - buffs2.size() + result);
                co_return result;
            })).unwrap();
            std::cout << sss << "\n";
        }).unwrap();
    });

    router.POST ("/uploadasynctest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            ssize_t result = 0;
            auto c = std::chrono::steady_clock::now();
            try {
                (co_await req.callback_async([&c, &result, &cb] (manapi::slice_view buffs, bool fin)
                    -> manapi::future<ssize_t> {
                    result += buffs.size();
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << " " << buffs.size() << "\n";
                    }
                    co_return buffs.size();
                })).unwrap();
            }
            catch (std::exception const &e) {
                std::cout << e.what() << "\n";
            }
            auto bb = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c);
            std::string err = std::format("{}\n", ((double)result / 1024 / 1024) / ((double)bb.count()/1000));
            auto b =manapi::slice::create(err.size()).unwrap();
            b.copy_from(err.data(), 0, err.size());
            co_await cb(b, true);
        }).unwrap();
    });

    router.POST ("/upload", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        manapi::net::hash::sha256 hash;
        try {
            (co_await req.callback_sync([&result, &hash] (const char *buffer, ssize_t size, bool fin)
                -> ssize_t {
                if (fin) {
                    std::cout << "FINSH\n";
                }
                hash.update(reinterpret_cast<const uint8_t *>(buffer), size);
                result += size;
                return size;
            })).unwrap();
        }
        catch (std::exception const &e) {
            std::cout << e.what() << "\n";
        }

        std::string b;
        b.resize(36);
        hash.final(reinterpret_cast<uint8_t *>(b.data()));

        b = manapi::crypto::strdec2strhex(b).unwrap();

        std::cout << result << " " << b << "\n";

        co_return resp.text(std::format("{} : {}", result, b)).unwrap();
    });

    router.GET("/mem", "/home/Timur/Downloads/VideoDownloader");

    router.POST ("/formdata", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        manapi::net::hash::sha256 hash{};
        try {
            (co_await req.form([&result, &hash] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                return [&hash, &result] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                    for (auto it = buffs.begin(); it != buffs.end(); it++)
                        hash.update((uint8_t*)it.buffer(), it.size());
                    result += buffs.size();
                    co_return buffs.size();
                };
            })).unwrap();
        }
        catch (std::exception const &e) {
            std::cout << e.what() << "\n";
        }

        std::string b;
        b.resize(36);
        hash.final(reinterpret_cast<uint8_t *>(b.data()));

        b = manapi::crypto::strdec2strhex(b);

        std::cout << result << " " << b << "\n";

        co_return resp.text(std::format("{} : {}", result, b)).unwrap();
    });

    router.POST ("/formdatatest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        auto c = std::chrono::steady_clock::now();
        try {
            (co_await req.form([&result, &c] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                return [&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                    result += buffs.size();
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << "\n";
                    }
                    co_return buffs.size();
                };
            })).unwrap();
        }
        catch (std::exception const &e) {
            std::cout << e.what() << "\n";
        }



        auto bb = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c);
        std::string err = std::format("{}\n", ((double)result / 1024 / 1024) / ((double)bb.count()/1000));

        co_return resp.text(err).unwrap();
    });

    router.GET ("/chunked", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        try {
            auto cancellation = manapi::async::cancellation_action::unit(req.cancellation());
            cancellation.timeout(5000);
            cancellation.ask_cancel_callback();
            auto file = manapi::filesystem::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4",
                cancellation).unwrap();
            co_await file.open (manapi::ev::FS_O_RDONLY|manapi::ev::FS_O_NONBLOCK);
            if (!file.is_open()) {
                co_return resp.text("failed to open the file").unwrap();
            }

            auto fetch = (co_await manapi::net::fetch2::fetch("https://localhost:8885/upload", {
                {"http", "2"},
                {"verify_peer", false},
                {"verbose", false},
                {"alpn", false},
                {"method", "POST"},
                {"timeout", 5},
                {"headers", {
                    //{"transfer-encoding", "chunked"}
                    {"content-length", "298512394"}
                }}
            }, [file] (manapi::slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
                auto const res = co_await file.fread(buffs);
                fin = file.eof();
                co_return res;
            }, manapi::async::cancellation_action::unit(cancellation))).unwrap();

            co_await file.close();

            if (!fetch.ok()) {
                co_return resp.text(std::format("status : {}", fetch.status())).unwrap();
            }
            co_return resp.text((co_await fetch.text()).unwrap()).unwrap();
        }
        catch (std::exception const &e) {
            co_return resp.text(e.what()).unwrap();
        }
    });

    router.GET("/fetch_sync_sha256", [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        auto f = co_await manapi::net::fetch2::fetch("https://127.0.0.1:8885/mem/ufa.mp4", {
            {"method", "GET"},
            {"http", "2"},
            {"verify_peer", false},
            {"verify_host", false}
        }, req.cancellation().sub());
        if (!f.ok()) {
            std::string s = "error: ";
            s += f.message();
            co_return resp.text(s).unwrap();
        }
        manapi::net::hash::sha256 sha256{};
        ssize_t res = 0;
        auto response = f.unwrap();

        (co_await response.callback_sync([&] (char *buff, ssize_t size) -> ssize_t {
            res += size;
            sha256.update((uint8_t *)buff, size);
            return size;
        })).unwrap();

        std::string b;
        b.resize(36);
        sha256.final(reinterpret_cast<uint8_t *>(b.data()));

        b = manapi::crypto::strdec2strhex(b).unwrap();
        co_return resp.text(std::format("{} {}", res, b)).unwrap();
    });

    router.GET("/fetch_async_sha256", [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        auto f = co_await manapi::net::fetch2::fetch("https://127.0.0.1:8885/mem/ufa.mp4", {
            {"method", "GET"},
            {"http", "2"},
            {"verify_peer", false},
            {"verify_host", false}
        }, req.cancellation().sub());
        if (!f.ok()) {
            std::string s = "error: ";
            s += f.message();
            co_return resp.text(s).unwrap();
        }
        manapi::net::hash::sha256 sha256{};
        ssize_t res = 0;
        auto response = f.unwrap();

        (co_await response.callback_async([&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
            res += buffs.size();
            for (auto it = buffs.begin(); it != buffs.end(); it++)
                sha256.update((uint8_t *)it.buffer(), it.size());
            co_return buffs.size();
        })).unwrap();

        std::string b;
        b.resize(36);
        sha256.final(reinterpret_cast<uint8_t *>(b.data()));

        b = manapi::crypto::strdec2strhex(b).unwrap();
        co_return resp.text(std::format("{} {}", res, b)).unwrap();
    });

    router.GET("/fetch_async_test", [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        auto f = co_await manapi::net::fetch2::fetch("http://127.0.0.1:8889/noise", {
            {"method", "GET"}
        },
            manapi::async::cancellation_action::unit(req.cancellation()));
        if (!f.ok()) {
            std::string s = "error: ";
            s += f.message();
            co_return resp.text(s).unwrap();
        }
        auto response = f.unwrap();
        ssize_t res = 0;
        co_await response.callback_async([&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
            res += buffs.size();
            co_return buffs.size();
        });
        co_return resp.text(std::to_string(res)).unwrap();
    });

    router.GET ("/ai", [] (http::req &req, http::resp &resp)
            -> manapi::future<> {
            if (!req.contains_get_param("text")) {
                co_return resp.text("GET param 'text' doesn't exist").unwrap();
            }

            std::string ip = "https://openrouter.ai/api/v1/chat/completions";
            int timeout = 64000;
            if (req.contains_get_param("timeout")) {
                try {
                    auto s = req.get_extract("timeout").unwrap();
                    timeout = std::stoi(s.second);
                }
                catch (...) {

                }
            }
            if (req.contains_get_param("ip"))
                ip = req.get("ip").unwrap();

            auto text = req.get("text").unwrap();

            auto token = manapi::process::get_env("MANAPIHTTP_AI").unwrap();

            auto cancellation = req.cancellation().sub();
        cancellation.timeout(timeout);

            auto response = (co_await manapi::net::fetch2::fetch(ip, {
                {"method", "POST"},
                {"verify_peer", false},
                {"alpn", true},
                {"verbose", true},
                {"headers", {
                    {"Content-Type", "application/json"},
                    {"Authorization", std::format("Bearer {}", token)}
                }}
            }, manapi::json({
                {"model", "openai/gpt-oss-20b:free"},
                {"messages", manapi::json::array({
                    {
                        {"role", "user"},
                        {"content", std::move(text)}
                    }
                })}
            }).dump(), cancellation)).unwrap();

            if (!response.ok()) {
                co_return resp.text(std::format("fetch failed. Http:", response.status())).unwrap();
            }

            auto ans = (co_await response.json()).unwrap();
            co_return resp.text(ans.dump(4)).unwrap();
        });
}


#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QThread>
#include <QTimerEvent>
#include <QVariant>

#include "uv.h"

    iz::Eventing::LibUvEventDispatcher::LibUvEventDispatcher() : LibUvEventDispatcher(nullptr) {
    }

iz::Eventing::LibUvEventDispatcher::LibUvEventDispatcher(QObject* parent)
    : QAbstractEventDispatcher(parent)
{
    this->m_wakeupHandle = manapi::async::current()->eventloop()->create_watcher_async(nullptr).unwrap();
    this->flags.fetch_or(0b1);
}

iz::Eventing::LibUvEventDispatcher::~LibUvEventDispatcher()
{
    manapi_log_debug("~LibUvEventDispatcher");
}

void iz::Eventing::LibUvEventDispatcher::unsubscribe() MANAPIHTTP_NOEXCEPT {
    if (this->flags & 0b1) {
        this->flags.fetch_xor(0b1);
    }
    manapi::async::current()->eventloop()->stop_watcher(std::move(this->m_wakeupHandle));
    while (!this->m_pollers.empty()) {
        auto it = this->m_pollers.begin();
        manapi::reference<poller_data_t> data (*it);
        manapi::async::current()->eventloop()->stop_watcher(std::move(data->watcher));
        this->m_pollers.erase(it);
    }
}

void iz::Eventing::LibUvEventDispatcher::interrupt()
{

    manapi_log_debug("%s:%s", "qt", "interrupt");
}

bool iz::Eventing::LibUvEventDispatcher::processEvents(QEventLoop::ProcessEventsFlags flags)
{
    // we are awake!
    emit awake();

    // zero out processed callbacks
    this->m_processedCallbacks = 0;

    // time to send posted events
    QCoreApplication::sendPostedEvents();

    // will we block on libuv run?
    const bool willWait = (flags & QEventLoop::WaitForMoreEvents);

    manapi::sys_error::status status;
    // run libuv poll depending on willWait value
    if (willWait) {
        // we will block! signalize it
        emit aboutToBlock();

        status = manapi::async::current()->eventloop()->run(manapi::ev::RUN_ONCE);
    } else {
        // run loop once, do not block on no events
        status = manapi::async::current()->eventloop()->run(manapi::ev::RUN_NOWAIT);
    }

    if (!status) {
        if (status.code() != manapi::ERR_ABORTED) {
            status.unwrap();
        }
        QCoreApplication::quit();
    }

    // return true if we processed something
    return this->m_processedCallbacks > 0;
}

void iz::Eventing::LibUvEventDispatcher::registerSocketNotifier(QSocketNotifier* notifier)
{
    // transform QSocketNotifier::Type to uv_poll_event
    int events = qtouv(notifier->type());
    if (events == -1) {
        return;
    }

    auto &ev = manapi::async::current()->eventloop();

    manapi::reference<poller_data_t> ref;

    auto const sock = static_cast<manapi::socket_t>(notifier->socket());
    auto fit = notifier->property("socketData");
    if(fit.isNull()) {
        ref.reset(new poller_data_t{});
        ref->context = this;
        manapi::ev::shared_io watcher;
        try {
            this->m_pollers.insert(ref.get());

            watcher = ev->create_watcher_socket(
                sock, [ref] (const manapi::ev::shared_io &, int status, int events) mutable -> void {
                    ref->context->m_processedCallbacks++;

                    // send required events
                    if (events & manapi::ev::READ) {
                        QEvent e(QEvent::SockAct);
                        QCoreApplication::sendEvent(ref->read_notifier, &e);
                    }

                    if (events & manapi::ev::WRITE) {
                        QEvent e(QEvent::SockAct);
                        QCoreApplication::sendEvent(ref->write_notifier, &e);
                    }
                }).unwrap();
        }
        catch (...) {
            this->m_pollers.erase(ref.get());
            std::rethrow_exception(std::current_exception());
        }

        ref->watcher = std::move(watcher);

        // attach our custom data to QSocketNotifier
        if (!notifier->setProperty("socketData", QVariant::fromValue(ref.get()))) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "notifier->setProperty with socketData returned false");
        }

    }
    else {
        ref.reset(fit.value<poller_data_t *>());
    }

    // setup read and write notifiers
    if (events & manapi::ev::READ) {
        ref->read_notifier = notifier;
        ref->flags |= manapi::ev::READ;
    }

    // set notifiers
    if (events & manapi::ev::WRITE) {
        ref->write_notifier = notifier;
        ref->flags |= manapi::ev::WRITE;
    }

    if (auto rhs = ref->watcher->start(ref->flags)) {
        manapi_log_error("%s due to %s", "qt:socket watcher failed", manapi::ev::strerror(rhs));
        throw std::runtime_error ("qt:socket watcher failed");
    }
}

void iz::Eventing::LibUvEventDispatcher::unregisterSocketNotifier(QSocketNotifier* notifier)
{
    // transform QSocketNotifier::Type to uv_poll_event
    int events = qtouv(notifier->type());
    if (events == -1) {
        return;
    }

    auto fit = notifier->property("socketData");
    assert(!fit.isNull());
    // get our data from libuv handler and actualize events flags
    auto data = fit.value<poller_data_t*>();

    if (data->flags & events & manapi::ev::READ) {
        data->flags ^= manapi::ev::READ;
    }

    if (data->flags & events & manapi::ev::WRITE) {
        data->flags ^= manapi::ev::WRITE;
    }

    // no event types left? schedule deletion of libuv's poller
    if (data->flags == 0) {
        manapi::async::current()->eventloop()->stop_watcher(std::move(data->watcher));
        // we can delete this now
        notifier->setProperty("socketData", QVariant());
        this->m_pollers.erase(data);
    }
    else {
        if (auto rhs = data->watcher->start(data->flags)) {
            manapi_log_error("%s due to %s", "qt:socket watcher failed", manapi::ev::strerror(rhs));
            throw std::runtime_error ("qt:socket watcher failed");
        }
    }
}

void iz::Eventing::LibUvEventDispatcher::enableSocketNotifier(QSocketNotifier* notifier)
{
    // transform QSocketNotifier::Type to uv_poll_event
    int events = qtouv(notifier->type());
    if (events == -1) {
        return;
    }

    // get our data from libuv' handler and actualize events flags
    auto fit = notifier->property("socketData");
    assert(!fit.isNull());
    auto data = fit.value<poller_data_t*>();
    data->flags |= events;
    // start libuv's polling
    if (auto rhs = data->watcher->start(data->flags)) {
        manapi_log_error("%s due to %s", "qt:socket watcher failed", manapi::ev::strerror(rhs));
        throw std::runtime_error ("qt:socket watcher failed");
    }
}

void iz::Eventing::LibUvEventDispatcher::disableSocketNotifier(QSocketNotifier* notifier)
{
    int events = qtouv(notifier->type());
    if (events == -1) {
        return;
    }

    // get our data from libuv's handler and actualize events flags
    auto fit = notifier->property("socketData");
    assert(!fit.isNull());
    auto data = fit.value<poller_data_t*>();
    if (data->flags & events & manapi::ev::READ) {
        data->flags ^= manapi::ev::READ;
    }
    if (data->flags & events & manapi::ev::WRITE) {
        data->flags ^= manapi::ev::WRITE;
    }
    // if no flags are set stop polling
    // some are set? start libuv's polling with remaining ones
    if (data->flags) {
        if (auto rhs = data->watcher->start(data->flags)) {
            manapi_log_error("%s due to %s", "qt:socket watcher failed", manapi::ev::strerror(rhs));
            throw std::runtime_error ("qt:socket watcher failed");
        }
    }
    else {
        if (auto rhs = data->watcher->stop()) {
            manapi_log_error("%s due to %s", "qt:socket watcher failed", manapi::ev::strerror(rhs));
            throw std::runtime_error ("qt:socket watcher failed");
        }
    }
}

int iz::Eventing::LibUvEventDispatcher::remainingTime(int timerId)
{
    auto it = this->m_timers.find(timerId);
    if (it != this->m_timers.end()) {
        auto &t = it->second.first;
        // in millseconds
        return t.remaning().count();
    }

    // non existent timer
    return -1;
}

void iz::Eventing::LibUvEventDispatcher::registerTimer(int timerId, qint64 interval, Qt::TimerType timerType, QObject* object) {
    auto fit = object->property("timerData");
    manapi::reference<timer_data_t> ref;
    if (fit.isNull()) {
        ref.reset(new timer_data_t{});
        ref->context = this;
        ref->parent = object;
    }
    else {
        ref.reset(fit.value<timer_data_t *>());
    }

    timer_info_t info{};
    info.interval = static_cast<int>(interval); // why int64 to int :(
    info.type = timerType;
    auto res = ref->ids.insert({timerId, info}).second;
    assert(res);

    manapi::timer timer;

    try {
        timer = manapi::async::current()->timerpool()->append_interval_sync(static_cast<std::size_t> (interval),
            manapi::TIMER_POOR,
            [ref, timerId] (const manapi::timer &)
            mutable -> void {
                ref->context->m_processedCallbacks++;
                QTimerEvent e (timerId);
                QCoreApplication::sendEvent(ref->parent, &e);
                // if (ref->ids.size() == 1) {
                //     ref->parent->setProperty("timerData", QVariant());
                // }
                // ref->ids.erase(timerId);
                // ref->context->m_timers.erase(timerId);
        }).unwrap();

        res = this->m_timers.emplace(timerId,
            std::make_pair(std::move(timer), ref)).second;
        assert(res);
    }
    catch (...) {
        ref->ids.erase(timerId);
        if (timer) {
            timer.stop();
        }
        std::rethrow_exception(std::current_exception());
    }
}

bool iz::Eventing::LibUvEventDispatcher::unregisterTimer(int timerId) {
    if (manapi::async::context_exists()) {
        auto it = this->m_timers.find(timerId);
        if (it != this->m_timers.end()) {
            if (it->second.second->ids.size() == 1) {
                it->second.second->parent->setProperty("timerData", QVariant());
            }
            it->second.second->ids.erase(timerId);
            it->second.first.stop();
            this->m_timers.erase(it);

            return true;
        }
    }

    return false;
}

QList<QAbstractEventDispatcher::TimerInfo> iz::Eventing::LibUvEventDispatcher::registeredTimers(QObject* object) const
{
    QList<QAbstractEventDispatcher::TimerInfo> res;

    auto fit = object->property("timerData");
    if (!fit.isNull()) {
        auto data = fit.value<timer_data_t *>();
        for (const auto &timer : data->ids) {
            res.append({ timer.first, timer.second.interval, timer.second.type });
        }
    }

    return res;
}

bool iz::Eventing::LibUvEventDispatcher::unregisterTimers(QObject* object) {
    auto fit = object->property("timerData");
    if (fit.isNull()) {
        return false;
    }
    manapi::reference<timer_data_t> data( fit.value<timer_data_t *>());

    while (!data->ids.empty()) {
        auto it = data->ids.begin();
        this->unregisterTimer(it->first);
    }

    return true;
}

void iz::Eventing::LibUvEventDispatcher::wakeUp()
{
    if (this->flags & 0b1) {
        this->m_wakeupHandle->send();
    }
}

int iz::Eventing::LibUvEventDispatcher::qtouv(QSocketNotifier::Type qtEventType) const
{
    switch (qtEventType) {
    case QSocketNotifier::Read:
        return manapi::ev::READ;
    case QSocketNotifier::Write:
        return manapi::ev::WRITE;
    default:
        qCritical() << "Unsupported QSocketNotifier type.";
        return -1;
    }
}
//
// void iz::Eventing::LibUvEventDispatcher::timerCallback(uv_timer_s* w)
// {
//     auto timerData = static_cast<TimerData*>(w->data);
//     timerData->context->m_processedCallbacks++;
//
//     // set last fired
//     timerData->lastFired = ::uv_hrtime() / 1000000;
//
//     QTimerEvent e(timerData->timerID);
//     QCoreApplication::sendEvent(timerData->qobject, &e);
// }
//
// void iz::Eventing::LibUvEventDispatcher::timerDeleteCallback(uv_handle_s* w)
// {
//     auto* timer = ( uv_timer_s* )w;
//
//     delete (( TimerData* )timer->data);
//     delete timer;
// }