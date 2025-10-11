#include "./include/ManapiUtils.hpp"
#include "ManapiBigint.hpp"

#if MANAPIHTTP_GMP_DEPENDENCY

#include <cmath>
#include <algorithm>
#include <memory.h>
#include <codecvt>
#include <gmp.h>

#include "ManapiErrors.hpp"
#include "json/ManapiJson.hpp"
#include "ManapiTime.hpp"
#include "ManapiDebug.hpp"

struct manapi::bigint::data_t {
    mpf_t m;
};

void manapi::bigint::data_t_deleter::operator()(data_t *n) noexcept(true) {
    mpf_clear(n->m);
}

manapi::bigint::bigint() {
    this->init_(128);
    this->parse(static_cast<ssize_t>(0));
}

manapi::bigint::bigint(ssize_t num, std::size_t precision) {
    this->init_(precision);
    this->parse(num);
}

manapi::bigint::bigint(int num, std::size_t precision) {
    this->init_(precision);
    this->parse(static_cast<ssize_t> (num));
}

manapi::bigint::bigint(double num, std::size_t precision) {
    this->init_(precision);
    this->parse(num);
}

manapi::bigint::bigint(long double num, std::size_t precision) {
    this->init_(precision);
    this->parse(num);
}

manapi::bigint::bigint(std::string_view num, std::size_t precision) {
    this->init_(precision);
    this->parse(num);
}

// manapi::bigint::bigint(mpf_ptr num, std::size_t precision) {
//     mpf_set (*x, num);
//     precision(precision);
// }

manapi::bigint::bigint(bigint &&other) MANAPIHTTP_NOEXCEPT {
    this->x = std::move(other.x);
}

manapi::bigint &manapi::bigint::operator=(bigint &&other) MANAPIHTTP_NOEXCEPT {
    this->x = std::move(other.x);
    return *this;
}

manapi::bigint::bigint(const manapi::bigint &other) {
    this->init_(128);
    *this = other;
}

manapi::bigint::~bigint() = default;

// parse

int manapi::bigint::parse(std::string_view num) {
    if (mpf_set_str (this->x->m, num.data(), 10))
        return ERR_INVALID_ARGUMENT;
    return ERR_OK;
}

void manapi::bigint::parse(ssize_t num) {
    mpf_set_si (this->x->m, num);
}

void manapi::bigint::parse(double num) {
    mpf_set_d (this->x->m, num);
}

void manapi::bigint::parse(long double num) {
    mpf_set_d (this->x->m, static_cast <double> (num));
}

std::string manapi::bigint::stringify() const {
    mp_exp_t exponent;
    char *ptr       = mpf_get_str (nullptr, &exponent, 10, 0, this->x->m);
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

ssize_t manapi::bigint::integerify() const {
    return mpf_get_si(this->x->m);
}

double manapi::bigint::decimalify() const {
    return mpf_get_d(this->x->m);
}

manapi::bigint manapi::bigint::operator+(const manapi::bigint &oth) const {
    bigint n;

    n.precision(mpf_get_prec (this->x->m));

    mpf_add (n.x->m, this->x->m, oth.x->m);

    return std::move(n);
}

manapi::bigint manapi::bigint::operator+(ssize_t oth) const {
    bigint n;

    n.precision(mpf_get_prec (this->x->m));

    mpf_add_ui (n.x->m, this->x->m, oth);

    return std::move(n);
}

manapi::bigint manapi::bigint::operator+(int oth) const {
    return std::move(this->operator+(static_cast<ssize_t> (oth)));
}

manapi::bigint manapi::bigint::operator/(const manapi::bigint &oth) const {
    if (oth == 0)
        THROW_MANAPIHTTP_EXCEPTION(ERR_DATA_LOSS, "Divided by zero");

    bigint n;

    n.precision(mpf_get_prec (this->x->m));

    mpf_div (n.x->m, this->x->m, oth.x->m);

    return std::move(n);
}

manapi::bigint manapi::bigint::operator/(int oth) const {
    return std::move(bigint(*this / bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator/(ssize_t oth) const {
    return std::move(bigint(*this / bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator/(double oth) const {
    return std::move(bigint(*this / bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator/(long double oth) const {
    return std::move(bigint(*this / bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::root(ssize_t oth) const {
    bigint n;
    n.precision(mpf_get_prec(this->x->m));
    mpf_pow_ui(n.x->m, this->x->m, oth);
    return std::move(n);
}

manapi::bigint manapi::bigint::sqrt(ssize_t oth) const {
    bigint n = *this;
    mpf_sqrt_ui(n.x->m, oth);
    return std::move(n);
}

manapi::bigint manapi::bigint::operator-(const manapi::bigint &oth) const {
    bigint n;

    n.precision(mpf_get_prec (this->x->m));

    mpf_sub (n.x->m, this->x->m, oth.x->m);

    return std::move(n);
}

manapi::bigint manapi::bigint::operator-(int oth) const {
    return std::move(operator+(-oth));
}

manapi::bigint manapi::bigint::operator-(ssize_t oth) const {
    bigint n;

    n.precision(mpf_get_prec (this->x->m));

    mpf_sub_ui (n.x->m, this->x->m, oth);

    return std::move(n);
}

manapi::bigint manapi::bigint::operator-(double oth) const {
    return std::move(bigint(*this - bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator+(double oth) const {
    return std::move(bigint(*this + bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator-(long double oth) const {
    return std::move(bigint(*this + bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator+(long double oth) const {
    return std::move(bigint(*this + bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator*(const manapi::bigint &oth) const {
    bigint n;

    n.precision(mpf_get_prec (this->x->m));

    mpf_mul (n.x->m, this->x->m, oth.x->m);

    return std::move(n);
}

manapi::bigint manapi::bigint::operator*(ssize_t oth) const {
    return std::move(bigint(*this * bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator*(int oth) const {
    return std::move(bigint(*this * bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator*(long double oth) const {
    return std::move(bigint(*this * bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint manapi::bigint::operator*(double oth) const {
    return std::move(bigint(*this * bigint (oth, mpf_get_prec(this->x->m))));
}

manapi::bigint& manapi::bigint::operator-=(const manapi::bigint &oth) {
    mpf_sub (this->x->m, this->x->m, oth.x->m);
    return *this;
}

manapi::bigint& manapi::bigint::operator+=(const manapi::bigint &oth) {
    mpf_add (this->x->m, this->x->m, oth.x->m);
    return *this;
}

manapi::bigint& manapi::bigint::operator-=(ssize_t oth) {
    operator+=(-oth);
    return *this;
}

manapi::bigint& manapi::bigint::operator+=(ssize_t oth) {
    *this = operator+(oth);
    return *this;
}

manapi::bigint& manapi::bigint::operator-=(int oth) {
    operator+=(-oth);
    return *this;
}

manapi::bigint& manapi::bigint::operator+=(int oth) {
    operator+=(static_cast<ssize_t> (oth));
    return *this;
}

manapi::bigint& manapi::bigint::operator-=(double oth) {
    operator+=(-oth);
    return *this;
}

manapi::bigint& manapi::bigint::operator+=(double oth) {
    operator+=(static_cast<long double> (oth));
    return *this;
}

manapi::bigint& manapi::bigint::operator-=(long double oth) {
    operator+=(-oth);
    return *this;
}

manapi::bigint& manapi::bigint::operator+=(long double oth) {
    *this = *this + oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator*=(const manapi::bigint &oth) {
    mpf_mul (this->x->m, this->x->m, oth.x->m);
    return *this;
}

manapi::bigint& manapi::bigint::operator*=(ssize_t oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator*=(int oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator*=(double oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator*=(long double oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator/=(ssize_t oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator/=(int oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator/=(double oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator/=(long double oth) {
    *this = *this / oth;
    return *this;
}

manapi::bigint& manapi::bigint::operator/=(const manapi::bigint &oth) {
    mpf_div (this->x->m, this->x->m, oth.x->m);
    return *this;
}

bool manapi::bigint::operator==(const manapi::bigint &oth) const {
    return mpf_cmp (this->x->m, oth.x->m) == 0;
}

bool manapi::bigint::operator==(ssize_t oth) const {
    return *this == bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator==(double oth) const {
    return *this == bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator==(int oth) const {
    return *this == bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator==(std::string_view oth) const {
    return *this == bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator!=(const manapi::bigint &oth) const {
    return *this == oth;
}

bool manapi::bigint::operator!=(ssize_t oth) const {
    return *this != bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator!=(double oth) const {
    return *this != bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator!=(int oth) const {
    return *this != bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator!=(std::string_view oth) const {
    return *this != bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>(const manapi::bigint &oth) const {
    return mpf_cmp (this->x->m, oth.x->m) < 0;
}

bool manapi::bigint::operator>(ssize_t oth) const {
    return *this > bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>(double oth) const {
    return *this > bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>(int oth) const {
    return *this > bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>(std::string_view oth) const {
    return *this > bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<(const manapi::bigint &oth) const {
    return mpf_cmp (this->x->m, oth.x->m) > 0;
}

bool manapi::bigint::operator<(ssize_t oth) const {
    return *this < bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<(double oth) const {
    return *this < bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<(int oth) const {
    return *this < bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<(std::string_view oth) const {
    return *this < bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>=(const manapi::bigint &oth) const {
    return *this == oth || *this > oth;
}

bool manapi::bigint::operator>=(ssize_t oth) const {
    return *this >= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>=(double oth) const {
    return *this >= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>=(int oth) const {
    return *this >= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator>=(std::string_view oth) const {
    return *this >= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<=(const manapi::bigint &oth) const {
    return *this == oth || *this < oth;
}

bool manapi::bigint::operator<=(ssize_t oth) const {
    return *this <= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<=(double oth) const {
    return *this <= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<=(int oth) const {
    return *this <= bigint (oth, mpf_get_prec(this->x->m));
}

bool manapi::bigint::operator<=(std::string_view oth) const {
    return *this <= bigint (oth, mpf_get_prec(this->x->m));
}

manapi::bigint &manapi::bigint::operator--() {
    mpf_sub_ui (this->x->m, this->x->m, 1);
    return *this;
}

manapi::bigint &manapi::bigint::operator++() {
    mpf_add_ui (this->x->m, this->x->m, 1);
    return *this;
}

manapi::bigint manapi::bigint::operator!() const {
    bigint copy (*this);

    mpf_neg (copy.x->m, copy.x->m);

    return std::move(copy);
}

manapi::bigint manapi::bigint::operator-() const {
    bigint copy (*this);

    mpf_neg (copy.x->m, copy.x->m);

    return std::move(copy);
}

manapi::bigint & manapi::bigint::operator=(std::string_view oth) {
    return *this = bigint (oth, mpf_get_prec(this->x->m));
}

manapi::bigint& manapi::bigint::operator=(const manapi::bigint &oth) {
    mpf_set_prec(this->x->m, oth.precision());
    mpf_set(this->x->m, oth.x->m);

    return *this;
}

manapi::bigint & manapi::bigint::operator=(ssize_t oth) {
    return *this = bigint (oth, mpf_get_prec(this->x->m));
}

manapi::bigint & manapi::bigint::operator=(int oth) {
    return *this = bigint (oth, mpf_get_prec(this->x->m));
}

manapi::bigint & manapi::bigint::operator=(double oth) {
    return *this = bigint (oth, mpf_get_prec(this->x->m));
}

void manapi::bigint::init_(std::size_t precision) {
    if (this->x) {
        mpf_clear(this->x->m);
    }
    else
        this->x.reset(new data_t{});

    mpf_init2(this->x->m, precision);
}

void manapi::bigint::precision(std::size_t precision) {
    mpf_set_prec (this->x->m, precision);
}

size_t manapi::bigint::precision() const {
    return mpf_get_prec(this->x->m);
}

// other

std::ostream &operator<<(std::ostream &os, const manapi::bigint &m) {
    return os << m.stringify();
}

std::istream &operator>>(std::istream &is, manapi::bigint &m) {
    std::string result;
    is >> result;

    m.parse(result);

    return is;
}

#endif