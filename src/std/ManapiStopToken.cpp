#include "std/ManapiStopToken.hpp"
#include "ManapiDebug.hpp"
#include "ManapiErrors.hpp"
#include "ManapiThreadPool.hpp"

#include "std/ManapiContext.hpp"

#include <cassert>
#include <utility>

struct manapi::stoken::data_t {
    std::move_only_function<void()> fin_cb;
    manapi::stoken next;
    std::size_t deps;
    int flags;
};

static int manapi__stoken_ref (manapi::stoken::data_t *a) MANAPIHTTP_NOEXCEPT {
    if (a) {
        a->deps++;
        return manapi::ERR_OK;
    }
    return manapi::ERR_NOT_FOUND;
}

static void manapi__stoken_unref (manapi::stoken::data_t *&a) MANAPIHTTP_NOEXCEPT {
    if (a && !--a->deps) {
        if (!!a->fin_cb) {
            if (a->flags & manapi::STOKEN_FLAG_IMMEDIATELY_CALL) {
                a->fin_cb ();
            }
            else {
                auto &pool = manapi::async::etaskpool();
                pool->release_tasks(manapi::TASK_TYPE_FUNC, 1);
                pool->append_task(std::move(a->fin_cb));
            }
        }

        delete std::exchange(a, nullptr);
    }
}

static void manapi__stoken_reset (manapi::stoken::data_t *&a, manapi::stoken::data_t *b) MANAPIHTTP_NOEXCEPT {
    manapi__stoken_ref(b);
    manapi__stoken_unref (a);

    a = b;
}

manapi::stoken::stoken() {
    this->m_data = nullptr;
}

manapi::stoken::stoken(std::move_only_function<void()> fin_cb, int flags) {
    std::unique_ptr<manapi::stoken::data_t> z (new manapi::stoken::data_t ());
    z->deps = 1;
    z->fin_cb = std::forward<decltype(fin_cb)>(fin_cb);
    z->flags = flags;

    if (!(z->flags & STOKEN_FLAG_IMMEDIATELY_CALL)) {
        manapi::async::etaskpool()->reserve_tasks(manapi::TASK_TYPE_FUNC, 1);
    }

    this->m_data = z.release();
}

manapi::stoken::~stoken() {
    ::manapi__stoken_reset (this->m_data, nullptr);
}

manapi::stoken::stoken(manapi::stoken &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = n.m_data;
    n.m_data = nullptr;
}

manapi::stoken &manapi::stoken::operator=(manapi::stoken &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        ::manapi__stoken_reset (this->m_data, n.m_data);
        n.reset();
    }
    return *this;
}

manapi::stoken::stoken(const manapi::stoken &n) MANAPIHTTP_NOEXCEPT {
    this->m_data = n.m_data;
    manapi__stoken_ref (this->m_data);
}

manapi::stoken &manapi::stoken::operator=(const manapi::stoken &n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        ::manapi__stoken_reset (this->m_data, n.m_data);
    }
    return *this;
}

int manapi::stoken::ref() MANAPIHTTP_NOEXCEPT {
    return manapi__stoken_ref (this->m_data);
}

void manapi::stoken::unref() MANAPIHTTP_NOEXCEPT {
    manapi__stoken_unref(this->m_data);
}

void manapi::stoken::reset() MANAPIHTTP_NOEXCEPT {
    ::manapi__stoken_reset (this->m_data, nullptr);
}

std::size_t manapi::stoken::count() const MANAPIHTTP_NOEXCEPT {
    if (this->m_data)
        return this->m_data->deps;

    return 0;
}

manapi::stoken *manapi::stoken::next(const manapi::stoken &token) MANAPIHTTP_NOEXCEPT {
    if (this->m_data) {
        if (token.m_data) {
            assert(!this->m_data->next);
            this->m_data->next = token;
            return &this->m_data->next;
        }

        return this;
    }
    return nullptr;
}

void manapi::stoken::clear() MANAPIHTTP_NOEXCEPT {
    if (this->m_data)
        this->m_data->fin_cb = nullptr;
}

manapi::stoken::operator bool() const MANAPIHTTP_NOEXCEPT {
    return !!this->m_data;
}


