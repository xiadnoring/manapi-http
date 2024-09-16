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

manapi::utils::bigint::bigint(const int &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(static_cast<ssize_t> (num));
}

manapi::utils::bigint::bigint(const double &num, const size_t &precision) {
    mpf_init2 (x, precision);
    this->parse(num);
}

manapi::utils::bigint::bigint(const long double &num, const size_t &precision) {
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

manapi::utils::bigint::bigint(bigint &&other) noexcept {
    memcpy (x, other.x, sizeof (x));
    memset (other.x, 0, sizeof (other.x));
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

void manapi::utils::bigint::parse(const long double &num) {
    mpf_set_d (x, static_cast <double> (num));
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

ssize_t manapi::utils::bigint::numberify() const {
    return mpf_get_si(x);
}

double manapi::utils::bigint::decimalify() const {
    return mpf_get_d(x);
}

manapi::utils::bigint manapi::utils::bigint::operator+(const manapi::utils::bigint &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_add (n.x, x, oth.x);

    return std::move(n);
}

manapi::utils::bigint manapi::utils::bigint::operator+(const size_t &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_add_ui (n.x, x, oth);

    return std::move(n);
}

manapi::utils::bigint manapi::utils::bigint::operator+(const int &oth) const {
    return std::move(this->operator+(static_cast<size_t> (oth)));
}

manapi::utils::bigint manapi::utils::bigint::operator/(const manapi::utils::bigint &oth) const {
    if (oth == zero)
    {
        THROW_MANAPI_EXCEPTION("{}", "Divided by zero");
    }

    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_div (n.x, x, oth.x);

    return std::move(n);
}

manapi::utils::bigint manapi::utils::bigint::operator/(const int &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator/(const ssize_t &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator/(const size_t &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator/(const double &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator/(const long double &oth) const {
    return std::move(*this / bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator+(const ssize_t &oth) const {
    return std::move(operator+(static_cast<size_t> (oth)));
}

manapi::utils::bigint manapi::utils::bigint::operator-(const manapi::utils::bigint &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub (n.x, x, oth.x);

    return std::move(n);
}

manapi::utils::bigint manapi::utils::bigint::operator-(const int &oth) const {
    return std::move(operator+(-oth));
}

manapi::utils::bigint manapi::utils::bigint::operator-(const ssize_t &oth) const {
    return std::move(operator+(-oth));
}

manapi::utils::bigint manapi::utils::bigint::operator-(const size_t &oth) const {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_sub_ui (n.x, x, oth);

    return std::move(n);
}

manapi::utils::bigint manapi::utils::bigint::operator-(const double &oth) const {
    return std::move(*this - bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator+(const double &oth) const {
    return std::move(*this + bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator-(const long double &oth) const {
    return std::move(*this + bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator+(const long double &oth) const {
    return std::move(*this + bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator*(const manapi::utils::bigint &oth) {
    bigint n;

    n.set_precision(mpf_get_prec (x));

    mpf_mul (n.x, x, oth.x);

    return std::move(n);
}

manapi::utils::bigint manapi::utils::bigint::operator*(const ssize_t &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator*(const size_t &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator*(const int &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator*(const long double &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

manapi::utils::bigint manapi::utils::bigint::operator*(const double &oth) {
    return std::move(*this * bigint (oth, mpf_get_prec(x)));
}

void manapi::utils::bigint::operator-=(const manapi::utils::bigint &oth) {
    mpf_sub (x, x, oth.x);
}

void manapi::utils::bigint::operator+=(const manapi::utils::bigint &oth) {
    mpf_add (x, x, oth.x);
}

void manapi::utils::bigint::operator-=(const ssize_t &oth) {
    operator+=(-oth);
}

void manapi::utils::bigint::operator+=(const ssize_t &oth) {
    *this = operator+(oth);
}

void manapi::utils::bigint::operator-=(const int &oth) {
    operator+=(-oth);
}

void manapi::utils::bigint::operator+=(const int &oth) {
    operator+=(static_cast<ssize_t> (oth));
}

void manapi::utils::bigint::operator-=(const double &oth) {
    operator+=(-oth);
}

void manapi::utils::bigint::operator+=(const double &oth) {
    operator+=(static_cast<long double> (oth));
}

void manapi::utils::bigint::operator-=(const long double &oth) {
    operator+=(-oth);
}

void manapi::utils::bigint::operator+=(const long double &oth) {
    *this = *this + oth;
}

void manapi::utils::bigint::operator*=(const manapi::utils::bigint &oth) {
    mpf_mul (x, x, oth.x);
}

void manapi::utils::bigint::operator*=(const size_t &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator*=(const ssize_t &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator*=(const int &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator*=(const double &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator*=(const long double &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator/=(const size_t &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator/=(const ssize_t &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator/=(const int &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator/=(const double &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator/=(const long double &oth) {
    *this = *this / oth;
}

void manapi::utils::bigint::operator/=(const manapi::utils::bigint &oth) {
    mpf_div (x, x, oth.x);
}

bool manapi::utils::bigint::operator==(const manapi::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) == 0;
}

bool manapi::utils::bigint::operator==(const size_t &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator==(const ssize_t &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator==(const double &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator==(const int &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator==(const std::string &oth) const {
    return *this == bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator!=(const manapi::utils::bigint &oth) const {
    return *this == oth;
}

bool manapi::utils::bigint::operator!=(const size_t &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator!=(const ssize_t &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator!=(const double &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator!=(const int &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator!=(const std::string &oth) const {
    return *this != bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>(const manapi::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) < 0;
}

bool manapi::utils::bigint::operator>(const size_t &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>(const ssize_t &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>(const double &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>(const int &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>(const std::string &oth) const {
    return *this > bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<(const manapi::utils::bigint &oth) const {
    return mpf_cmp (x, oth.x) > 0;
}

bool manapi::utils::bigint::operator<(const size_t &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<(const ssize_t &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<(const double &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<(const int &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<(const std::string &oth) const {
    return *this < bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>=(const manapi::utils::bigint &oth) const {
    return *this == oth || *this > oth;
}

bool manapi::utils::bigint::operator>=(const size_t &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>=(const ssize_t &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>=(const double &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>=(const int &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator>=(const std::string &oth) const {
    return *this >= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<=(const manapi::utils::bigint &oth) const {
    return *this == oth || *this < oth;
}

bool manapi::utils::bigint::operator<=(const size_t &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<=(const ssize_t &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<=(const double &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<=(const int &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
}

bool manapi::utils::bigint::operator<=(const std::string &oth) const {
    return *this <= bigint (oth, mpf_get_prec(x));
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

    return std::move(copy);
}

manapi::utils::bigint manapi::utils::bigint::operator-() const {
    bigint copy (x);

    mpf_neg (copy.x, copy.x);

    return std::move(copy);
}

manapi::utils::bigint & manapi::utils::bigint::operator=(const std::string &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::utils::bigint& manapi::utils::bigint::operator=(const manapi::utils::bigint &oth) {
    cleanup();

    mpf_init(x);
    mpf_set(x, oth.x);

    return *this;
}

manapi::utils::bigint & manapi::utils::bigint::operator=(const ssize_t &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::utils::bigint & manapi::utils::bigint::operator=(const size_t &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::utils::bigint & manapi::utils::bigint::operator=(const int &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
}

manapi::utils::bigint & manapi::utils::bigint::operator=(const double &oth) {
    return *this = bigint (oth, mpf_get_prec(x));
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