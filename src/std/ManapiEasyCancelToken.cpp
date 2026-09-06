#include "std/ManapiEasyCancelToken.hpp"

manapi::ctoken manapi::ctokens::timeout(size_t milliseconds) {
    manapi::ctoken token;
    token.timeout(milliseconds);
    return std::move(token);
}
