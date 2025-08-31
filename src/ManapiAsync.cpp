#include "ManapiAsync.hpp"

static size_t max_stack_depth_ = 300;
thread_local std::size_t manapi::async::internal::current_stack_cnt = 0;
thread_local std::shared_ptr<manapi::async::cthread> manapi::async::internal::current_cthread_ = nullptr;

DLLExportImport std::size_t manapi::async::internal::current_stack_cnt_crt () MANAPIHTTP_NOEXCEPT {
    return manapi::async::internal::current_stack_cnt;
}

DLLExportImport void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    manapi::async::internal::current_stack_cnt = cnt;
}

DLLExportImport std::size_t manapi::async::internal::max_stack_depth2 () MANAPIHTTP_NOEXCEPT {
    return max_stack_depth_;
}

DLLExportImport void manapi::async::internal::max_stack_depth2 (std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    max_stack_depth_ = cnt;
}