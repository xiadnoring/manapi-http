#include <cmath>
#include <algorithm>
#include <memory.h>
#include <codecvt>

#include "ManapiUtils.hpp"
#include "ManapiBigint.hpp"

manapi::net::utils::bigint zero (0LL);

manapi::net::utils::bigint::bigint() {
    mpf_init2 (x, 128);
    this->parse(0LL);
}

manapi::net::utils::bigint::bigint(const long long int &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::net::utils::bigint::bigint(const ssize_t &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::net::utils::bigint::bigint(const int &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(static_cast<ssize_t> (num));
}

manapi::net::utils::bigint::bigint(const double &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::net::utils::bigint::bigint(const long double &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::net::utils::bigint::bigint(const unsigned long &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::net::utils::bigint::bigint(const std::string &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::net::utils::bigint::bigint(const std::wstring &num, const size_t &precision) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    mpf_init2 (x, precision);
    this->parse(converter.to_bytes(num));
}

manapi::net::utils::bigint::bigint(const mpf_t &num, const size_t &precision) {
    mpf_set (x, num);
    set_precision(precision);
}

manapi::net::utils::bigint::bigint(bigint &&other) noexcept {
    memcpy (x, other.x, sizeof (x));
    memset (other.x, 0, sizeof (other.x));
}

manapi::net::utils::bigint::bigint(const manapi::net::utils::bigint &other) {
    mpf_init (x);

    *this = other;
}

manapi::net::utils::bigint::~bigint() {
    cleanup();
}

// parse

void manapi::net::utils::bigint::parse(const std::string &num) {
    mpf_set_str (x, num.data(), 10);
}

void manapi::net::utils::bigint::parse(const size_t &num) {
    mpf_set_ui (x, num);
}

void manapi::net::utils::bigint::parse(const long long int &num) {
    mpf_set_si (x, num);
}

void manapi::net::utils::bigint::parse(const ssize_t &num) {
    mpf_set_si (x, num);
}

void manapi::net::utils::bigint::parse(const double &num) {
    mpf_set_d (x, num);
}

void manapi::net::utils::bigint::parse(const long double &num) {
    mpf_set_d (x, static_cast <double> (num));
}

std::string manapi::net::utils::bigint::stringify() const {
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

ssize_t manapi::net::utils::bigint::numberify() const {
    return mpf_get_si(x);
}

double manapi::net::utils::bigint::decimalify() const {
    return mpf_get_d(x);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator+(const manapi::net::utils::bigint &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_add (n.x, x, oth.x);

    return std::move(n);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator+(const size_t &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_add_ui (n.x, x, oth);

    return std::move(n);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator+(const int &oth) const {
    return std::move(this->operator+(static_cast<size_t> (oth)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator/(const manapi::net::utils::bigint &oth) const {
    if (oth == zero)
    {
        THROW_MANAPI_EXCEPTION("{}", "Divided by zero");
    }

    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_div (n.x, x, oth.x);

    return std::move(n);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator/(const int &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator/(const ssize_t &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator/(const size_t &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator/(const double &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator/(const long double &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator+(const ssize_t &oth) const {
    return std::move(operator+(static_cast<size_t> (oth)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-(const manapi::net::utils::bigint &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub (n.x, x, oth.x);

    return std::move(n);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-(const int &oth) const {
    return std::move(operator+(-oth));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-(const ssize_t &oth) const {
    return std::move(operator+(-oth));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-(const size_t &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub_ui (n.x, x, oth);

    return std::move(n);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-(const double &oth) const {
    return std::move(*this - bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator+(const double &oth) const {
    return std::move(*this + bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-(const long double &oth) const {
    return std::move(*this + bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator+(const long double &oth) const {
    return std::move(*this + bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator*(const manapi::net::utils::bigint &oth) {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_mul (n.x, x, oth.x);

    return std::move(n);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator*(const ssize_t &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator*(const size_t &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator*(const int &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator*(const long double &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator*(const double &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

void manapi::net::utils::bigint::operator-=(const manapi::net::utils::bigint &oth) {
    mpf_sub (x, x, oth.x);
}

void manapi::net::utils::bigint::operator+=(const manapi::net::utils::bigint &oth) {
    mpf_add (x, x, oth.x);
}

void manapi::net::utils::bigint::operator-=(const ssize_t &oth) {
    operator+=(-oth);
}

void manapi::net::utils::bigint::operator+=(const ssize_t &oth) {
    *this = operator+(oth);
}

void manapi::net::utils::bigint::operator-=(const int &oth) {
    operator+=(-oth);
}

void manapi::net::utils::bigint::operator+=(const int &oth) {
    operator+=(static_cast<ssize_t> (oth));
}

void manapi::net::utils::bigint::operator-=(const double &oth) {
    operator+=(-oth);
}

void manapi::net::utils::bigint::operator+=(const double &oth) {
    operator+=(static_cast<long double> (oth));
}

void manapi::net::utils::bigint::operator-=(const long double &oth) {
    operator+=(-oth);
}

void manapi::net::utils::bigint::operator+=(const long double &oth) {
    *this = *this + oth;
}

void manapi::net::utils::bigint::operator*=(const manapi::net::utils::bigint &oth) {
    mpf_mul (x, x, oth.x);
}

void manapi::net::utils::bigint::operator*=(const size_t &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator*=(const ssize_t &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator*=(const int &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator*=(const double &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator*=(const long double &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator/=(const size_t &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator/=(const ssize_t &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator/=(const int &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator/=(const double &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator/=(const long double &oth) {
    *this = *this / oth;
}

void manapi::net::utils::bigint::operator/=(const manapi::net::utils::bigint &oth) {
    mpf_div (x, x, oth.x);
}

bool manapi::net::utils::bigint::operator==(const manapi::net::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) == 0;
}

bool manapi::net::utils::bigint::operator==(const size_t &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator==(const ssize_t &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator==(const double &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator==(const int &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator==(const std::string &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator!=(const manapi::net::utils::bigint &oth) const {
    return *this == oth;
}

bool manapi::net::utils::bigint::operator!=(const size_t &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator!=(const ssize_t &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator!=(const double &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator!=(const int &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator!=(const std::string &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>(const manapi::net::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) < 0;
}

bool manapi::net::utils::bigint::operator>(const size_t &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>(const ssize_t &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>(const double &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>(const int &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>(const std::string &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<(const manapi::net::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) > 0;
}

bool manapi::net::utils::bigint::operator<(const size_t &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<(const ssize_t &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<(const double &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<(const int &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<(const std::string &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>=(const manapi::net::utils::bigint &oth) const {
    return *this == oth || *this > oth;
}

bool manapi::net::utils::bigint::operator>=(const size_t &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>=(const ssize_t &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>=(const double &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>=(const int &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator>=(const std::string &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<=(const manapi::net::utils::bigint &oth) const {
    return *this == oth || *this < oth;
}

bool manapi::net::utils::bigint::operator<=(const size_t &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<=(const ssize_t &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<=(const double &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<=(const int &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::net::utils::bigint::operator<=(const std::string &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

manapi::net::utils::bigint &manapi::net::utils::bigint::operator--() {
    mpf_sub_ui (x, x, 1);
    return *this;
}

manapi::net::utils::bigint &manapi::net::utils::bigint::operator++() {
    mpf_add_ui (x, x, 1);
    return *this;
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator!() const {
    bigint copy (x);

    mpf_neg (copy.x, copy.x);

    return std::move(copy);
}

manapi::net::utils::bigint manapi::net::utils::bigint::operator-() const {
    bigint copy (x);

    mpf_neg (copy.x, copy.x);

    return std::move(copy);
}

manapi::net::utils::bigint & manapi::net::utils::bigint::operator=(const std::string &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::net::utils::bigint& manapi::net::utils::bigint::operator=(const manapi::net::utils::bigint &oth) {
    cleanup();

    mpf_init(x);
    mpf_set_prec(x, oth.get_precision());
    mpf_set(x, oth.x);

    return *this;
}

manapi::net::utils::bigint & manapi::net::utils::bigint::operator=(const ssize_t &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::net::utils::bigint & manapi::net::utils::bigint::operator=(const size_t &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::net::utils::bigint & manapi::net::utils::bigint::operator=(const int &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::net::utils::bigint & manapi::net::utils::bigint::operator=(const double &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

void manapi::net::utils::bigint::cleanup() {
    mpf_clear (x);
}

void manapi::net::utils::bigint::set_precision(const size_t &precision) {
    mpf_set_prec (x, precision);
}

size_t manapi::net::utils::bigint::get_precision() const {
    return mpf_get_prec(x);
}

// other

std::ostream &operator<<(std::ostream &os, const manapi::net::utils::bigint &m) {
    return os << m.stringify();
}

std::istream &operator>>(std::istream &is, manapi::net::utils::bigint &m) {
    std::string result;
    is >> result;

    m.parse(result);

    return is;
}