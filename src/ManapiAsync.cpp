#include "ManapiAsync.hpp"

std::size_t manapi::async::internal::current_stack_cnt_crt () MANAPIHTTP_NOEXPECT {
    return manapi::async::internal::current_stack_cnt;
}

void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXPECT {
    manapi::async::internal::current_stack_cnt = cnt;
}

size_t manapi::async::max_stack_depth = 300;