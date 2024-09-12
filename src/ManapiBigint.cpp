#include <cmath>
#include <algorithm>
#include <memory.h>
#include <codecvt>
#include "ManapiBigint.hpp"

manapi::utils::bigint zero (0LL);

manapi::utils::bigint::bigint() {
    mpf_init2 (x, 128);
    this->parse(0LL);
}

manapi::utils::bigint::bigint(const long long int &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::bigint::bigint(const ssize_t &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::bigint::bigint(const double &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::bigint::bigint(const unsigned long &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::bigint::bigint(const std::string &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::bigint::bigint(const std::wstring &num, const size_t &precision) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    mpf_init2 (x, precision);
    this->parse(converter.to_bytes(num));
}

manapi::utils::bigint::bigint(const mpf_t &num, const size_t &precision) {
    mpf_set (x, num);
    set_precision(precision);
}

manapi::utils::bigint::bigint(const manapi::utils::bigint &other) {
    mpf_init (x);

    *this = other;
}

manapi::utils::bigint::~bigint() {
    cleanup();
}

// parse

void manapi::utils::bigint::parse(const std::string &num) {
    mpf_set_str (x, num.data(), 10);
}

void manapi::utils::bigint::parse(const size_t &num) {
    mpf_set_ui (x, num);
}

void manapi::utils::bigint::parse(const long long int &num) {
    mpf_set_si (x, num);
}

void manapi::utils::bigint::parse(const ssize_t &num) {
    mpf_set_si (x, num);
}

void manapi::utils::bigint::parse(const double &num) {
    mpf_set_d (x, num);
}

std::string manapi::utils::bigint::stringify() const {
    mp_exp_t exponent;
    char *ptr       = mpf_get_str (nullptr, &exponent, 10, 0, x);
    std::string ret = ptr;

    if (!ret.empty()) {
        if (ret[0] == '-')
            exponent++;

        if (ret.size() > exponent)
            ret.insert(exponent, exponent == 0 ? "0." : ".");

        else {
            for (size_t i = ret.size(); i < exponent; i++)
                ret += '0';
        }

        return ret;
    }

    return "0";
}

manapi::utils::bigint manapi::utils::bigint::operator+(const manapi::utils::bigint &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_add (n.x, x, oth.x);

    return n;
}

manapi::utils::bigint manapi::utils::bigint::operator+(const unsigned long &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_add_ui (n.x, x, oth);

    return n;
}

manapi::utils::bigint manapi::utils::bigint::operator/(const manapi::utils::bigint &oth) const {
    if (oth == zero)
    {
        THROW_MANAPI_EXCEPTION("{}", "Divided by zero");
    }

    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_div (n.x, x, oth.x);

    return n;
}

manapi::utils::bigint manapi::utils::bigint::operator-(const manapi::utils::bigint &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub (n.x, x, oth.x);

    return n;
}

manapi::utils::bigint manapi::utils::bigint::operator-(const unsigned long &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub_ui (n.x, x, oth);

    return n;
}

manapi::utils::bigint manapi::utils::bigint::operator*(const manapi::utils::bigint &oth) {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_mul (n.x, x, oth.x);

    return n;
}

void manapi::utils::bigint::operator-=(const manapi::utils::bigint &oth) {
    mpf_sub (x, x, oth.x);
}

void manapi::utils::bigint::operator+=(const manapi::utils::bigint &oth) {
    mpf_add (x, x, oth.x);
}

void manapi::utils::bigint::operator*=(const manapi::utils::bigint &oth) {
    mpf_mul (x, x, oth.x);
}

void manapi::utils::bigint::operator/=(const manapi::utils::bigint &oth) {
    mpf_div (x, x, oth.x);
}

bool manapi::utils::bigint::operator==(const manapi::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) == 0;
}

bool manapi::utils::bigint::operator!=(const manapi::utils::bigint &oth) const {
    return *this == oth;
}

bool manapi::utils::bigint::operator>(const manapi::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) < 0;
}

bool manapi::utils::bigint::operator<(const manapi::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) > 0;
}

bool manapi::utils::bigint::operator>=(const manapi::utils::bigint &oth) const {
    return *this == oth || *this > oth;
}

bool manapi::utils::bigint::operator<=(const manapi::utils::bigint &oth) const {
    return *this == oth || *this < oth;
}

manapi::utils::bigint &manapi::utils::bigint::operator--() {
    mpf_sub_ui (x, x, 1);
    return *this;
}

manapi::utils::bigint &manapi::utils::bigint::operator++() {
    mpf_add_ui (x, x, 1);
    return *this;
}

manapi::utils::bigint manapi::utils::bigint::operator!() const {
    bigint copy (x);

    mpf_neg (copy.x, copy.x);

    return copy;
}

manapi::utils::bigint manapi::utils::bigint::operator-() const {
    bigint copy (x);

    mpf_neg (copy.x, copy.x);

    return copy;
}

manapi::utils::bigint& manapi::utils::bigint::operator=(const manapi::utils::bigint &oth) {
    cleanup();

    mpf_init(x);
    mpf_set(x, oth.x);

    return *this;
}

void manapi::utils::bigint::cleanup() {
    mpf_clear (x);
}

void manapi::utils::bigint::set_precision(const size_t &precision) {
    mpf_set_prec (x, precision);
}
// other

std::ostream &operator<<(std::ostream &os, const manapi::utils::bigint &m) {
    return os << m.stringify();
}

std::istream &operator>>(std::istream &is, manapi::utils::bigint &m) {
    std::string result;
    is >> result;

    m.parse(result);

    return is;
}