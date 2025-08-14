#include "std/ManapiEasyCancellation.hpp"

manapi::async::cancellation_action manapi::async::timeout_cancellation(size_t milliseconds) {
    manapi::async::cancellation_action token;
    token.timeout(milliseconds);
    return std::move(token);
}
