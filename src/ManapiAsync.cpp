#include "ManapiAsync.hpp"

const std::shared_ptr<manapi::async::cthread> & manapi::async::internal::current_() {
    return async::internal::current_cthread_;
}

std::size_t manapi::async::internal::current_stack_cnt_crt () {
    return manapi::async::internal::current_stack_cnt;
}

void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) {
    manapi::async::internal::current_stack_cnt = cnt;
}

size_t manapi::async::max_stack_depth = 300;