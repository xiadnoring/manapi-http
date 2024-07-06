#include <cmath>
#include <algorithm>
#include <memory.h>
#include "ManapiDecimal.h"

manapi::utils::decimal zero (0LL);

manapi::utils::decimal::decimal() {
    mpf_init2 (x, 128);
    this->parse(0LL);
}

manapi::utils::decimal::decimal(const long long int &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::decimal::decimal(const ssize_t &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::decimal::decimal(const double &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::decimal::decimal(const unsigned long &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::decimal::decimal(const std::string &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::decimal::decimal(const mpf_t &num, const size_t &precision) {
    mpf_set (x, num);
    set_precision(precision);
}

manapi::utils::decimal::decimal(const manapi::utils::decimal &other) {
    mpf_init (x);

    *this = other;
}

manapi::utils::decimal::~decimal() {
    cleanup();
}

// parse

void manapi::utils::decimal::parse(const std::string &num) {
    mpf_set_str (x, num.data(), 10);
}

void manapi::utils::decimal::parse(const size_t &num) {
    mpf_set_ui (x, num);
}

void manapi::utils::decimal::parse(const long long int &num) {
    mpf_set_si (x, num);
}

void manapi::utils::decimal::parse(const ssize_t &num) {
    mpf_set_si (x, num);
}

void manapi::utils::decimal::parse(const double &num) {
    mpf_set_d (x, num);
}

std::string manapi::utils::decimal::stringify() const {
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

manapi::utils::decimal manapi::utils::decimal::operator+(const manapi::utils::decimal &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_add (n.x, x, oth.x);

    return n;
}

manapi::utils::decimal manapi::utils::decimal::operator+(const unsigned long &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_add_ui (n.x, x, oth);

    return n;
}

manapi::utils::decimal manapi::utils::decimal::operator/(const manapi::utils::decimal &oth) const {
    if (oth == zero)
        throw manapi::utils::manapi_exception ("Divided by zero");

    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_div (n.x, x, oth.x);

    return n;
}

manapi::utils::decimal manapi::utils::decimal::operator-(const manapi::utils::decimal &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub (n.x, x, oth.x);

    return n;
}

manapi::utils::decimal manapi::utils::decimal::operator-(const unsigned long &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub_ui (n.x, x, oth);

    return n;
}

manapi::utils::decimal manapi::utils::decimal::operator*(const manapi::utils::decimal &oth) {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_mul (n.x, x, oth.x);

    return n;
}

void manapi::utils::decimal::operator-=(const manapi::utils::decimal &oth) {
    mpf_sub (x, x, oth.x);
}

void manapi::utils::decimal::operator+=(const manapi::utils::decimal &oth) {
    mpf_add (x, x, oth.x);
}

void manapi::utils::decimal::operator*=(const manapi::utils::decimal &oth) {
    mpf_mul (x, x, oth.x);
}

void manapi::utils::decimal::operator/=(const manapi::utils::decimal &oth) {
    mpf_div (x, x, oth.x);
}

bool manapi::utils::decimal::operator==(const manapi::utils::decimal &oth) const {
    return mpf_cmp (x, oth.x) == 0;
}

bool manapi::utils::decimal::operator!=(const manapi::utils::decimal &oth) const {
    return *this == oth;
}

bool manapi::utils::decimal::operator>(const manapi::utils::decimal &oth) const {
    return mpf_cmp (x, oth.x) < 0;
}

bool manapi::utils::decimal::operator<(const manapi::utils::decimal &oth) const {
    return mpf_cmp (x, oth.x) > 0;
}

bool manapi::utils::decimal::operator>=(const manapi::utils::decimal &oth) const {
    return *this == oth || *this > oth;
}

bool manapi::utils::decimal::operator<=(const manapi::utils::decimal &oth) const {
    return *this == oth || *this < oth;
}

manapi::utils::decimal &manapi::utils::decimal::operator--() {
    mpf_sub_ui (x, x, 1);
    return *this;
}

manapi::utils::decimal &manapi::utils::decimal::operator++() {
    mpf_add_ui (x, x, 1);
    return *this;
}

manapi::utils::decimal manapi::utils::decimal::operator!() const {
    decimal copy (x);

    mpf_neg (copy.x, copy.x);

    return copy;
}

manapi::utils::decimal manapi::utils::decimal::operator-() const {
    decimal copy (x);

    mpf_neg (copy.x, copy.x);

    return copy;
}

manapi::utils::decimal& manapi::utils::decimal::operator=(const manapi::utils::decimal &oth) {
    cleanup();

    mpf_init(x);
    mpf_set(x, oth.x);

    return *this;
}

void manapi::utils::decimal::cleanup() {
    mpf_clear (x);
}

void manapi::utils::decimal::set_precision(const size_t &precision) {
    mpf_set_prec (x, precision);
}
// other

std::ostream &operator<<(std::ostream &os, const manapi::utils::decimal &m) {
    return os << m.stringify();
}

std::istream &operator>>(std::istream &is, manapi::utils::decimal &m) {
    std::string result;
    is >> result;

    m.parse(result);

    return is;
}