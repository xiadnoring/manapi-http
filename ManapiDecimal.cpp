#include <cmath>
#include <algorithm>
#include <memory.h>
#include "ManapiDecimal.h"

manapi::toolbox::decimal zero (0LL);

manapi::toolbox::decimal::decimal() {
    mpf_init2 (x, 128);
    this->parse(0LL);
}

manapi::toolbox::decimal::decimal(const long long int &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::toolbox::decimal::decimal(const ssize_t &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::toolbox::decimal::decimal(const double &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::toolbox::decimal::decimal(const unsigned long &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::toolbox::decimal::decimal(const std::string &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::toolbox::decimal::decimal(const mpf_t &num, const size_t &precision) {
    mpf_set (x, num);
    set_precision(precision);
}

manapi::toolbox::decimal::decimal(const manapi::toolbox::decimal &other) {
    mpf_init (x);

    *this = other;
}

manapi::toolbox::decimal::~decimal() {
    cleanup();
}

// parse

void manapi::toolbox::decimal::parse(const std::string &num) {
    mpf_set_str (x, num.data(), 10);
}

void manapi::toolbox::decimal::parse(const size_t &num) {
    mpf_set_ui (x, num);
}

void manapi::toolbox::decimal::parse(const long long int &num) {
    mpf_set_si (x, num);
}

void manapi::toolbox::decimal::parse(const ssize_t &num) {
    mpf_set_si (x, num);
}

void manapi::toolbox::decimal::parse(const double &num) {
    mpf_set_d (x, num);
}

std::string manapi::toolbox::decimal::stringify() const {
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

manapi::toolbox::decimal manapi::toolbox::decimal::operator+(const manapi::toolbox::decimal &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_add (n.x, x, oth.x);

    return n;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator+(const unsigned long &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_add_ui (n.x, x, oth);

    return n;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator/(const manapi::toolbox::decimal &oth) const {
    if (oth == zero)
        throw manapi::toolbox::manapi_exception ("Divided by zero");

    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_div (n.x, x, oth.x);

    return n;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator-(const manapi::toolbox::decimal &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub (n.x, x, oth.x);

    return n;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator-(const unsigned long &oth) const {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub_ui (n.x, x, oth);

    return n;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator*(const manapi::toolbox::decimal &oth) {
    decimal n;

    n.set_precision(mpf_get_prec (x));

    mpf_mul (n.x, x, oth.x);

    return n;
}

void manapi::toolbox::decimal::operator-=(const manapi::toolbox::decimal &oth) {
    mpf_sub (x, x, oth.x);
}

void manapi::toolbox::decimal::operator+=(const manapi::toolbox::decimal &oth) {
    mpf_add (x, x, oth.x);
}

void manapi::toolbox::decimal::operator*=(const manapi::toolbox::decimal &oth) {
    mpf_mul (x, x, oth.x);
}

void manapi::toolbox::decimal::operator/=(const manapi::toolbox::decimal &oth) {
    mpf_div (x, x, oth.x);
}

bool manapi::toolbox::decimal::operator==(const manapi::toolbox::decimal &oth) const {
    return mpf_cmp (x, oth.x) == 0;
}

bool manapi::toolbox::decimal::operator!=(const manapi::toolbox::decimal &oth) const {
    return *this == oth;
}

bool manapi::toolbox::decimal::operator>(const manapi::toolbox::decimal &oth) const {
    return mpf_cmp (x, oth.x) < 0;
}

bool manapi::toolbox::decimal::operator<(const manapi::toolbox::decimal &oth) const {
    return mpf_cmp (x, oth.x) > 0;
}

bool manapi::toolbox::decimal::operator>=(const manapi::toolbox::decimal &oth) const {
    return *this == oth || *this > oth;
}

bool manapi::toolbox::decimal::operator<=(const manapi::toolbox::decimal &oth) const {
    return *this == oth || *this < oth;
}

manapi::toolbox::decimal &manapi::toolbox::decimal::operator--() {
    mpf_sub_ui (x, x, 1);
    return *this;
}

manapi::toolbox::decimal &manapi::toolbox::decimal::operator++() {
    mpf_add_ui (x, x, 1);
    return *this;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator!() const {
    decimal copy (x);

    mpf_neg (copy.x, copy.x);

    return copy;
}

manapi::toolbox::decimal manapi::toolbox::decimal::operator-() const {
    decimal copy (x);

    mpf_neg (copy.x, copy.x);

    return copy;
}

manapi::toolbox::decimal& manapi::toolbox::decimal::operator=(const manapi::toolbox::decimal &oth) {
    cleanup();

    mpf_init(x);
    mpf_set(x, oth.x);

    return *this;
}

void manapi::toolbox::decimal::cleanup() {
    mpf_clear (x);
}

void manapi::toolbox::decimal::set_precision(const size_t &precision) {
    mpf_set_prec (x, precision);
}
// other

std::ostream &operator<<(std::ostream &os, const manapi::toolbox::decimal &m) {
    return os << m.stringify();
}

std::istream &operator>>(std::istream &is, manapi::toolbox::decimal &m) {
    std::string result;
    is >> result;

    m.parse(result);

    return is;
}