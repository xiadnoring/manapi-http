#pragma once

#include <stdexcept>
#include <string_view>
#include <array>
#include <typeindex>
#include <memory.h>

#include "../../ManapiUtils.hpp"
#include "../../ManapiDebug.hpp"

namespace manapi::ext::pq {
    enum value_types {
        BOOLOID = 16,
        BYTEAOID = 17,
        CHAROID = 18,
        NAMEOID = 19,
        INT8OID = 20,
        INT2OID = 21,
        INT2VECTOROID = 22,
        INT4OID = 23,
        REGPROCOID = 24,
        TEXTOID = 25,
        OIDOID = 26,
        TIDOID = 27,
        XIDOID = 28,
        CIDOID = 29,
        OIDVECTOROID = 30,
        JSONOID = 114,
        XMLOID = 142,
        PG_NODE_TREEOID = 194,
        PG_NDISTINCTOID = 3361,
        PG_DEPENDENCIESOID = 3402,
        PG_MCV_LISTOID = 5017,
        PG_DDL_COMMANDOID = 32,
        XID8OID = 5069,
        POINTOID = 600,
        LSEGOID = 601,
        PATHOID = 602,
        BOXOID = 603,
        POLYGONOID = 604,
        LINEOID = 628,
        FLOAT4OID = 700,
        FLOAT8OID = 701,
        UNKNOWNOID = 705,
        CIRCLEOID = 718,
        MONEYOID = 790,
        MACADDROID = 829,
        INETOID = 869,
        CIDROID = 650,
        MACADDR8OID = 774,
        ACLITEMOID = 1033,
        BPCHAROID = 1042,
        VARCHAROID = 1043,
        DATEOID = 1082,
        TIMEOID = 1083,
        TIMESTAMPOID = 1114,
        TIMESTAMPTZOID = 1184,
        INTERVALOID = 1186,
        TIMETZOID = 1266,
        BITOID = 1560,
        VARBITOID = 1562,
        NUMERICOID = 1700,
        REFCURSOROID = 1790,
        REGPROCEDUREOID = 2202,
        REGOPEROID = 2203,
        REGOPERATOROID = 2204,
        REGCLASSOID = 2205,
        REGCOLLATIONOID = 4191,
        REGTYPEOID = 2206,
        REGROLEOID = 4096,
        REGNAMESPACEOID = 4089,
        UUIDOID = 2950,
        PG_LSNOID = 3220,
        TSVECTOROID = 3614,
        GTSVECTOROID = 3642,
        TSQUERYOID = 3615,
        REGCONFIGOID = 3734,
        REGDICTIONARYOID = 3769,
        JSONBOID = 3802,
        JSONPATHOID = 4072,
        TXID_SNAPSHOTOID = 2970,
        PG_SNAPSHOTOID = 5038,
        INT4RANGEOID = 3904,
        NUMRANGEOID = 3906,
        TSRANGEOID = 3908,
        TSTZRANGEOID = 3910,
        DATERANGEOID = 3912,
        INT8RANGEOID = 3926,
        INT4MULTIRANGEOID = 4451,
        NUMMULTIRANGEOID = 4532,
        TSMULTIRANGEOID = 4533,
        TSTZMULTIRANGEOID = 4534,
        DATEMULTIRANGEOID = 4535,
        INT8MULTIRANGEOID = 4536,
        RECORDOID = 2249,
        RECORDARRAYOID = 2287,
        CSTRINGOID = 2275,
        ANYOID = 2276,
        ANYARRAYOID = 2277,
        VOIDOID = 2278,
        TRIGGEROID = 2279,
        EVENT_TRIGGEROID = 3838,
        LANGUAGE_HANDLEROID = 2280,
        INTERNALOID = 2281,
        ANYELEMENTOID = 2283,
        ANYNONARRAYOID = 2776,
        ANYENUMOID = 3500,
        FDW_HANDLEROID = 3115,
        INDEX_AM_HANDLEROID = 325,
        TSM_HANDLEROID = 3310,
        TABLE_AM_HANDLEROID = 269,
        ANYRANGEOID = 3831,
        ANYCOMPATIBLEOID = 5077,
        ANYCOMPATIBLEARRAYOID = 5078,
        ANYCOMPATIBLENONARRAYOID = 5079,
        ANYCOMPATIBLERANGEOID = 5080,
        ANYMULTIRANGEOID = 4537,
        ANYCOMPATIBLEMULTIRANGEOID = 4538,
        PG_BRIN_BLOOM_SUMMARYOID = 4600,
        PG_BRIN_MINMAX_MULTI_SUMMARYOID = 4601,
        BOOLARRAYOID = 1000,
        BYTEAARRAYOID = 1001,
        CHARARRAYOID = 1002,
        NAMEARRAYOID = 1003,
        INT8ARRAYOID = 1016,
        INT2ARRAYOID = 1005,
        INT2VECTORARRAYOID = 1006,
        INT4ARRAYOID = 1007,
        REGPROCARRAYOID = 1008,
        TEXTARRAYOID = 1009,
        OIDARRAYOID = 1028,
        TIDARRAYOID = 1010,
        XIDARRAYOID = 1011,
        CIDARRAYOID = 1012,
        OIDVECTORARRAYOID = 1013,
        PG_TYPEARRAYOID = 210,
        PG_ATTRIBUTEARRAYOID = 270,
        PG_PROCARRAYOID = 272,
        PG_CLASSARRAYOID = 273,
        JSONARRAYOID = 199,
        XMLARRAYOID = 143,
        XID8ARRAYOID = 271,
        POINTARRAYOID = 1017,
        LSEGARRAYOID = 1018,
        PATHARRAYOID = 1019,
        BOXARRAYOID = 1020,
        POLYGONARRAYOID = 1027,
        LINEARRAYOID = 629,
        FLOAT4ARRAYOID = 1021,
        FLOAT8ARRAYOID = 1022,
        CIRCLEARRAYOID = 719,
        MONEYARRAYOID = 791,
        MACADDRARRAYOID = 1040,
        INETARRAYOID = 1041,
        CIDRARRAYOID = 651,
        MACADDR8ARRAYOID = 775,
        ACLITEMARRAYOID = 1034,
        BPCHARARRAYOID = 1014,
        VARCHARARRAYOID = 1015,
        DATEARRAYOID = 1182,
        TIMEARRAYOID = 1183,
        TIMESTAMPARRAYOID = 1115,
        TIMESTAMPTZARRAYOID = 1185,
        INTERVALARRAYOID = 1187,
        TIMETZARRAYOID = 1270,
        BITARRAYOID = 1561,
        VARBITARRAYOID = 1563,
        NUMERICARRAYOID = 1231,
        REFCURSORARRAYOID = 2201,
        REGPROCEDUREARRAYOID = 2207,
        REGOPERARRAYOID = 2208,
        REGOPERATORARRAYOID = 2209,
        REGCLASSARRAYOID = 2210,
        REGCOLLATIONARRAYOID = 4192,
        REGTYPEARRAYOID = 2211,
        REGROLEARRAYOID = 4097,
        REGNAMESPACEARRAYOID = 4090,
        UUIDARRAYOID = 2951,
        PG_LSNARRAYOID = 3221,
        TSVECTORARRAYOID = 3643,
        GTSVECTORARRAYOID = 3644,
        TSQUERYARRAYOID = 3645,
        REGCONFIGARRAYOID = 3735,
        REGDICTIONARYARRAYOID = 3770,
        JSONBARRAYOID = 3807,
        JSONPATHARRAYOID = 4073,
        TXID_SNAPSHOTARRAYOID = 2949,
        PG_SNAPSHOTARRAYOID = 5039,
        INT4RANGEARRAYOID = 3905,
        NUMRANGEARRAYOID = 3907,
        TSRANGEARRAYOID = 3909,
        TSTZRANGEARRAYOID = 3911,
        DATERANGEARRAYOID = 3913,
        INT8RANGEARRAYOID = 3927,
        INT4MULTIRANGEARRAYOID = 6150,
        NUMMULTIRANGEARRAYOID = 6151,
        TSMULTIRANGEARRAYOID = 6152,
        TSTZMULTIRANGEARRAYOID = 6153,
        DATEMULTIRANGEARRAYOID = 6155,
        INT8MULTIRANGEARRAYOID = 6157,
        CSTRINGARRAYOID = 1263
    };
    class text : public std::string_view {
    public:
        text (const char *a, const size_t &s) : std::string_view(a, s) {}
        text (const char *a) : std::string_view(a) {}
        text (const std::string &a) : std::string_view(a) {}
        text (const std::string_view &a) : std::string_view(a) {}
        text (std::string::iterator first, std::string::iterator last): std::string_view(first, last) {}
        text (std::string::const_iterator first, std::string::const_iterator last): std::string_view(first, last) {}
    };

    class blob : public std::string_view {
    public:
        blob (const char *a, const size_t &s) : std::string_view(a, s) {}
        blob (const char *a) : std::string_view(a) {}
        blob (const std::string &a) : std::string_view(a) {}
        blob (const std::string_view &a) : std::string_view(a) {}
        blob (std::string::iterator first, std::string::iterator last): std::string_view(first, last) {}
        blob (std::string::const_iterator first, std::string::const_iterator last): std::string_view(first, last) {}
    };

    template<typename T>
    struct string_traits {
        static inline T from_string (std::string_view text_) { return std::string{text_}; }
        static inline void to_string (std::string_view text_, T const &value) {
            assert(text_.size() >= value.size()); memcpy((void*)text_.data(), value.data(), value.size());
        }
        MANAPIHTTP_NODISCARD static inline size_t size (T const &value) { return value.size(); }
    };

    template<typename T>
    MANAPIHTTP_NODISCARD T from_string (std::string_view text_) {
        return string_traits<T>::from_string(text_);
    }

    template<typename T>
    MANAPIHTTP_NODISCARD size_t size_of (const T *v) {
        throw std::runtime_error("template");
    }

    template<typename T>
    MANAPIHTTP_NODISCARD size_t size_of (const T &v) {
        throw std::runtime_error("template");
    }

    template<typename T>
    inline void to_string (std::string_view text_, const T &v) {
        return string_traits<T>::to_string(text_, v);
    }

    template<typename T>
    inline void to_string (std::string_view text_, const T *v) {
        return string_traits<T>::to_string(text_, v);
    }
}

#include "./AsyncPostgreValueTypes.hpp"

namespace manapi::ext::pq {
    template<typename T>
    inline uint32_t oid_of (const T &v) {
        fprintf(stderr, "Unresolved function: oid_of(...)");
        exit(1);
    }


    template<typename T>
    inline uint32_t oid_of (const T *v) {
        fprintf(stderr, "Unresolved function: oid_of(...)");
        exit(1);
    }

    template<> inline uint32_t oid_of (const unsigned int &v) {
        return INT4OID;
    }

    template<> inline uint32_t oid_of (const unsigned short &v) {
        return INT2OID;
    }

    template<> inline uint32_t oid_of (const unsigned long long &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const size_t &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const int &v) {
        return INT4OID;
    }

    template<> inline uint32_t oid_of (const std::string_view &v) {
        return VARCHAROID;
    }

    template<> inline uint32_t oid_of (const pq::text &v) {
        return TEXTOID;
    }

    template<> inline uint32_t oid_of (const pq::blob &v) {
        return BYTEAOID;
    }

    template<> inline uint32_t oid_of (const short &v) {
        return INT2OID;
    }

    template<> inline uint32_t oid_of (const long long &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const ssize_t &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const std::string &v) {
        return VARCHAROID;
    }

    template<> inline uint32_t oid_of (const char *v) {
        return VARCHAROID;
    }

    template<> inline uint32_t oid_of (const bool &v) {
        return BOOLOID;
    }

    template<> inline uint32_t oid_of (const char &v) {
        return CHAROID;
    }

    template<> inline uint32_t oid_of (const unsigned char &v) {
        return CHAROID;
    }

    template<> inline uint32_t oid_of (const float &v) {
        return FLOAT4OID;
    }

    template<> inline uint32_t oid_of (const double &v) {
        return FLOAT8OID;
    }

    template<typename T>
    MANAPIHTTP_NODISCARD inline const char *serialize_param (const T v, int len, std::string_view &buffer) {
        pq::to_string<T>(buffer, v);
        const char *start = buffer.data();
        buffer = buffer.substr(len);
        return start;
    }

    template<typename ...Args>
    auto serialize (std::string &buffer, const std::tuple<Args...>& params) {
        struct result_type {
            std::array<uint32_t, sizeof...(Args)> types;
            std::array<const char *, sizeof...(Args)> values;
            std::array<int, sizeof...(Args)> lengths;
            std::array<int, sizeof...(Args)> formats;
        };

        return std::apply(
            [&] (const auto &...args) -> result_type {
                std::array<int, sizeof...(args)> lengths = { static_cast<int>(size_of(args))... };
                size_t size = 0;

                for (auto &len: lengths)
                {
                    size += len;
                }

                buffer.clear();
                buffer.resize(size);

                std::string_view window {buffer.begin(), buffer.end()};
                int index = 0;

                return result_type {
                    .types = { (oid_of(args))... },
                    .values = { (serialize_param(args, lengths[index++], window))... },
                    .lengths = std::move(lengths),
                    .formats = { ((void)args, true)... },
                };
            }, params);
    }
}
