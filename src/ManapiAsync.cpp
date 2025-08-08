#include "ManapiAsync.hpp"

thread_local std::size_t manapi::async::internal::current_stack_cnt = 0;
thread_local std::shared_ptr<manapi::async::cthread> manapi::async::internal::current_cthread_ = nullptr;

std::size_t manapi::async::internal::current_stack_cnt_crt () MANAPIHTTP_NOEXCEPT {
    return manapi::async::internal::current_stack_cnt;
}

void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    manapi::async::internal::current_stack_cnt = cnt;
}

size_t manapi::async::max_stack_depth = 300;