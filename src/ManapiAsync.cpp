#include "ManapiAsync.hpp"

std::size_t manapi::async::internal::current_stack_cnt_crt () {
    return manapi::async::internal::current_stack_cnt;
}

void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) {
    manapi::async::internal::current_stack_cnt = cnt;
}

size_t manapi::async::max_stack_depth = 300;