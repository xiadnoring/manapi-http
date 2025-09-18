#include "compress/ManapiHPack.hpp"

#include <cassert>

#include "ManapiDebug.hpp"
#include "ManapiErrors.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "../include/ManapiUtils.hpp"
#include "http/ManapiHttpUtils.hpp"

#define T true
#define F false

namespace manapi::compress::hpack {
	typedef std::vector< bool > bits_t;
    const static std::set <std::string> multiheaders = {"set-cookie","www-authenticate","proxy-authenticate", "cookie"};

	enum decompress_ctx_flags {
		HPACK_DECOMPRESS_REGULAR_HEADERS = 1
	};

	static const std::array< header_t, 62 > predefined_headers = {
		{
			header_t("INVALIDINDEX", "INVALIDINDEX"), header_t(":authority", ""), header_t(":method", "GET"),
			header_t(":method", "POST"), header_t(":path", "/"), header_t(":path", "/index.html"),
			header_t(":scheme", "http"), header_t(":scheme", "https"), header_t(":status", "200"),
			header_t(":status", "204"), header_t(":status", "206"), header_t(":status", "304"),
			header_t(":status", "400"), header_t(":status", "404"), header_t(":status", "500"),
			header_t("accept-charset", ""), header_t("accept-encoding", "gzip, deflate"), header_t("accept-language", ""),
			header_t("accept-ranges", ""), header_t("accept", ""), header_t("access-control-allow-origin", ""),
			header_t("age", ""), header_t("allow", ""), header_t("authorization", ""),
			header_t("cache-control", ""), 	header_t("content-disposition", ""), header_t("content-encoding", ""),
			header_t("content-language", ""), header_t("content-length", ""), header_t("content-location", ""),
			header_t("content-range", ""), header_t("content-type", ""), header_t("cookie", ""),
			header_t("date", ""), header_t("etag", ""), header_t("expect", ""),
			header_t("expires", ""), header_t("from", ""), header_t("host", ""),
			header_t("if-match", ""), header_t("if-modified-since", ""), header_t("if-none-match", ""),
			header_t("if-range", ""), header_t("if-unmodified-since", ""), header_t("last-modified", ""),
			header_t("link", ""), header_t("location", ""), header_t("max-forwards", ""),
			header_t("proxy-authenticate", ""), header_t("proxy-authorization", ""), header_t("range", ""),
			header_t("referer", ""), header_t("refresh", ""), header_t("retry-after", ""),
			header_t("server", ""), header_t("set-cookie", ""), header_t("strict-transport-security", ""),
			header_t("transfer-encoding", ""), header_t("user-agent", ""), header_t("vary", ""),
			header_t("via", ""), header_t("www-authenticate", "")
		}
	};

	// 256 chars plus end of string
	static const std::array< bits_t, 257 > huffman_table = {{
			{ T,T,T,T,T,T,T,T,T,T,F,F,F },											// 0
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F,F },						// 1
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,F },			// 2
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,T },			// 3
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,F },			// 4
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,T },			// 5
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },			// 6
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },			// 7
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },			// 8
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F },					// 9
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F },		// 10
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T },			// 11
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F },			// 12
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T },		// 13
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T },			// 14
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },			// 15
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },			// 16
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F },			// 17
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T },			// 18
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F },			// 19
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T },			// 20
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F },			// 21
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F },		// 22
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T },			// 23
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F },			// 24
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T },			// 25
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F },			// 26
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T },			// 27
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F },			// 28
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T },			// 29
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F },			// 30
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T },			// 31
			{ F,T,F,T,F,F },														// 32 ' '
			{ T,T,T,T,T,T,T,F,F,F },												// 33 '!'
			{ T,T,T,T,T,T,T,F,F,T },												// 34 '"'
			{ T,T,T,T,T,T,T,T,T,F,T,F },											// 35 '#'
			{ T,T,T,T,T,T,T,T,T,T,F,F,T },											// 36 '$'
			{ F,T,F,T,F,T },														// 37 '%'
			{ T,T,T,T,T,F,F,F },													// 38 '&'
			{ T,T,T,T,T,T,T,T,F,T,F },												// 39 '''
			{ T,T,T,T,T,T,T,F,T,F },												// 40 '('
			{ T,T,T,T,T,T,T,F,T,T },												// 41 ')'
			{ T,T,T,T,T,F,F,T },													// 42 '*'
			{ T,T,T,T,T,T,T,T,F,T,T },												// 43 '+'
			{ T,T,T,T,T,F,T,F },													// 44 ','
			{ F,T,F,T,T,F },														// 45 '-'
			{ F,T,F,T,T,T },														// 46 '.'
			{ F,T,T,F,F,F },														// 47 '/'
			{ F,F,F,F,F },															// 48 '0'
			{ F,F,F,F,T },															// 49 '1'
			{ F,F,F,T,F },															// 50 '2'
			{ F,T,T,F,F,T },														// 51 '3'
			{ F,T,T,F,T,F },														// 52 '4'
			{ F,T,T,F,T,T },														// 53 '5'
			{ F,T,T,T,F,F },														// 54 '6'
			{ F,T,T,T,F,T },														// 55 '7'
			{ F,T,T,T,T,F },														// 56 '8'
			{ F,T,T,T,T,T },														// 57 '9'
			{ T,F,T,T,T,F,F },														// 58 ':'
			{ T,T,T,T,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,F,F },
			{ T,F,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,F,T,T },
			{ T,T,T,T,T,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,F,T,F },
			{ T,F,F,F,F,T },
			{ T,F,T,T,T,F,T },
			{ T,F,T,T,T,T,F },
			{ T,F,T,T,T,T,T },
			{ T,T,F,F,F,F,F },
			{ T,T,F,F,F,F,T },
			{ T,T,F,F,F,T,F },
			{ T,T,F,F,F,T,T },
			{ T,T,F,F,T,F,F },
			{ T,T,F,F,T,F,T },
			{ T,T,F,F,T,T,F },
			{ T,T,F,F,T,T,T },
			{ T,T,F,T,F,F,F },
			{ T,T,F,T,F,F,T },
			{ T,T,F,T,F,T,F },
			{ T,T,F,T,F,T,T },
			{ T,T,F,T,T,F,F },
			{ T,T,F,T,T,F,T },
			{ T,T,F,T,T,T,F },
			{ T,T,F,T,T,T,T },
			{ T,T,T,F,F,F,F },
			{ T,T,T,F,F,F,T },
			{ T,T,T,F,F,T,F },
			{ T,T,T,T,T,T,F,F },
			{ T,T,T,F,F,T,T },
			{ T,T,T,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,F,F },
			{ T,F,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,F,T },
			{ F,F,F,T,T },
			{ T,F,F,F,T,T },
			{ F,F,T,F,F },
			{ T,F,F,T,F,F },
			{ F,F,T,F,T },
			{ T,F,F,T,F,T },
			{ T,F,F,T,T,F },
			{ T,F,F,T,T,T },
			{ F,F,T,T,F },
			{ T,T,T,F,T,F,F },
			{ T,T,T,F,T,F,T },
			{ T,F,T,F,F,F },
			{ T,F,T,F,F,T },
			{ T,F,T,F,T,F },
			{ F,F,T,T,T },
			{ T,F,T,F,T,T },
			{ T,T,T,F,T,T,F },
			{ T,F,T,T,F,F },
			{ F,T,F,F,F },
			{ F,T,F,F,T },
			{ T,F,T,T,F,T },
			{ T,T,T,F,T,T,T },
			{ T,T,T,T,F,F,F },
			{ T,T,T,T,F,F,T },
			{ T,T,T,T,F,T,F },
			{ T,T,T,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,F,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,F,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,T },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,F,F,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,F,T,T,T,F },
			{ T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T }
		}
	};


	huffman_node_t::huffman_node_t(huffman_node_t *l, huffman_node_t *r, int16_t c) : m_left(l), m_right(r), m_code(c) {

	}

	huffman_node_t::~huffman_node_t() {
		this->m_left = nullptr;
		this->m_right = nullptr;
		this->m_code = 0;
	}

	int16_t huffman_node_t::code() const {
		return this->m_code;
	}

	void huffman_node_t::code(int16_t c) {
		this->m_code = c;
	}

	huffman_node_t * huffman_node_t::left() {
		return this->m_left;
	}

	void huffman_node_t::left(huffman_node_t *l) {
		this->m_left = l;
	}

	huffman_node_t * huffman_node_t::right() {
		return this->m_right;
	}

	void huffman_node_t::right(huffman_node_t *r) {
		this->m_right = r;
	}

	void huffman_tree_t::delete_node(huffman_node_t *n) {
		if ( nullptr != n->right() )
			delete_node(n->right());
		if ( nullptr != n->left() )
			delete_node(n->left());

		// just delete
		delete n;

		return;
	}

	huffman_tree_t::huffman_tree_t() : m_root(new huffman_node_t) {
		for ( std::size_t idx = 0; idx < huffman_table.size(); idx++ ) {
			const bits_t&		bits = huffman_table.at(idx);
			huffman_node_t*		current = this->m_root;

			for ( const auto& bit : bits ) {
				if ( true == bit ) {
					if ( nullptr == current->right() )
						current->right(new huffman_node_t);

					current = current->right();
				} else {
					if ( nullptr == current->left() )
						current->left(new huffman_node_t);

					current = current->left();
				}
			}

			current->code(static_cast< int16_t >( idx ));
		}
	}

	huffman_tree_t::~huffman_tree_t() {
		delete_node(this->m_root);
	}

	error::status_or<std::string> huffman_tree_t::decode(std::string_view src, uint32_t maxlen) {
		std::string			dst;
		huffman_node_t*		current(this->m_root);

		dst.reserve(src.size());

		if ( src.length() > std::numeric_limits< unsigned int >::max() )
			return error::status_invalid_argument("hpack:Overly long input string");

		for ( unsigned int idx = 0; idx < src.length(); idx++ ) {
			for ( int8_t j = 7; j >= 0; j-- ) {
				if ( ( src[ idx ] & ( 1 << j ) ) > 0 ) {
					if ( nullptr == current->right() )
						return error::status_invalid_argument("hpack:Internal state error (right == nullptr)");
					current = current->right();
				} else {
					if ( nullptr == current->left() )
						return error::status_invalid_argument("hpack:Internal state error (left == nullptr)");

					current = current->left();
				}

				if ( current->code() >= 0 ) {
					uint16_t code = current->code();

					if ( 257 == code ) {
						if (maxlen < 2)
							goto err_zero;

						dst += static_cast< uint8_t >( ( ( code & 0xFF00 ) >> 8 ) & 0xFF );
						maxlen -= 2;
					}
					else if (!maxlen)
						goto err_zero;
					else
						maxlen -= 1;

					dst += static_cast< uint8_t >( code & 0xFF );
					current = this->m_root;
				}
			}
		}

		return dst;
err_zero:
		return error::status_resource_exhausted("hpack:Len is limited");
	}

	// 4096 is the default table size per the HTTPv2 RFC
	ringtable_t::ringtable_t() : m_max(4096) { }

	ringtable_t::ringtable_t(uint64_t m) : m_max(m) { }

	ringtable_t::~ringtable_t() = default;

	void ringtable_t::max(uint64_t m) {
		this->m_max = m;

		// the RFC dictates that we do this here,
		// so we do.
		while ( length() > this->m_max ) {
			this->m_queue.pop_back();
		}
	}

	uint64_t ringtable_t::max() const {
		return this->m_max;
	}

	uint64_t ringtable_t::entries_count() const{
		return m_queue.size();
	}

	uint64_t ringtable_t::length() const {
		uint64_t size(0);

		for ( auto& h : m_queue ) {
			uint64_t nl(h.first.length());
			uint64_t vl(h.second.length());
			uint64_t tl(0);

			// In practice it should basically never occur
			// that either of these exceptions are thrown and
			// its probably safe to remove the checks in most instances
			if ( vl > std::numeric_limits< uint64_t >::max() ||
				nl > std::numeric_limits< uint64_t >::max() - vl )
				throw std::runtime_error("HPACK::ringtable_t::length() Additive integer overflow encountered");

			tl = nl + vl;

			if ( tl > std::numeric_limits< uint64_t >::max() - size )
				throw std::runtime_error("HPACK::ringtable_t::length() Additive integer overflow encountered");

			size += tl;

		}

		return size;
	}

	void ringtable_t::add(const header_t &h) {
		uint64_t size(h.first.length() + h.second.length());

		// In practice it should be basically implausible to trip these exceptions because
		// you would need 2^(sizeof(uint64_t)*8) bytes of memory to be in use, which in itself
		// will likely fail long before then. In other words its probably safe to remove
		// these checks for the forseeable future, but I left them in because technically I
		// should check even if its an absurd condition.
		if ( h.first.length() > std::numeric_limits< uint64_t >::max() - h.second.length() )
			throw std::runtime_error("HPACK::ringtable_t::add(): Additive integer overflow encountered.");

		// Again the RFC dictates when we resize the queue.
		while ( length() >= m_max ) {
			m_queue.pop_back();
		}

		m_queue.push_front(h);
	}

	void ringtable_t::add(const std::string &n, const std::string &v) {
		header_t h(n, v);

		add(h);
	}

	void ringtable_t::add(const char *n, const char *v) {
		std::string name(n), value(v);

		if ( nullptr == n || nullptr == v )
			throw std::runtime_error("HPACK::ringtable_t::add(): Invalid nullptr parameter(s)");

		add(name, value);
	}

	const header_t & ringtable_t::at(uint64_t idx) {
		if ( idx > m_queue.size() ) {
			// It's not clear these checks even entirely make sense
			// the concern was that someone passes in an out-of-bounds
			// index, but thats because the static and dynamic
			// tables have their indices flattened into one, so we
			// attempt to address that by subtracting the static
			// headers index size if we are out of bounds, which
			// might yield a totally bogus index due to implementation
			// bug asking for an invalid index that just happens to line
			// up.
			if ( idx < predefined_headers.size() )
				throw std::invalid_argument("HPACK::ringtable_t::at(): Invalid/out-of-bounds index specified");

			idx -= predefined_headers.size();

			if ( idx > m_queue.size() )
				throw std::invalid_argument("HPACK::ringtable_t::at(): Invalid/out-of-bounds index specified");

		}

		return m_queue.at(static_cast< std::size_t >( idx ));
	}

	bool ringtable_t::find(const header_t &h, int64_t &index) const {
		index = -1;

		if ( index > std::numeric_limits< std::size_t >::max() )
			throw std::invalid_argument("HPACK::ringtable_t::find(): Invalid/overlarge index which results in truncation");

		for ( std::size_t idx = 0; idx < m_queue.size(); idx++ ) {
			if ( !h.first.compare(m_queue.at(idx).first) && !h.second.compare(m_queue.at(idx).second) ) {
				index = predefined_headers.size() + idx;
				return true;
			} else if ( !h.first.compare(m_queue.at(idx).first) ) {
				index = predefined_headers.size() + idx;
				return false;
			}
		}

		return false;
	}

	manapi::error::status_or<const header_t *> ringtable_t::get_header(std::size_t index) const {
		if ( index < predefined_headers.size() ) {
			return &predefined_headers.at(index);
		}
		if ( index < predefined_headers.size() + m_queue.size() )
			return &m_queue.at(index - predefined_headers.size());

		return manapi::error::status_out_of_range("HPACK::ringtable_t::get_header(): Invalid index/header not found");
	}

	bool huffman_encoder_t::write_bit(uint8_t bit) {
		m_byte |= bit;
		m_count--;

		if (0 == m_count ) {
			m_count = 8;
			return true;
		} else
			m_byte <<= 1;

		return false;
	}

	huffman_encoder_t::huffman_encoder_t() : m_byte(0), m_count(8) { }

	huffman_encoder_t::~huffman_encoder_t() = default;

	std::vector<uint8_t> huffman_encoder_t::encode(std::vector<uint8_t> &src) {
		std::vector< uint8_t > ret(0);

		for ( auto& byte : src ) {
			bits_t bits = huffman_table.at(byte);

			for ( decltype(auto) bit : bits ) {
				if ( true == write_bit(bit) ) {
					ret.push_back(m_byte);
					m_byte = 0;
					m_count = 8;
				}

			}
		}

		// Apparently the remainder unused bits
		// are to be set to T, some sources refer
		// to this as the EOS bit, but the code
		// for EOS is like 30-bits of 1's so its
		// clearly not the EOS code.
		if ( 8 != m_count && 0 != m_count) {
			m_byte = ( m_byte << ( m_count - 1 ) );
			m_byte |= ( 0xFF >> ( 8 - m_count ) );
			ret.push_back(m_byte);
			m_byte = 0;
			m_count = 8;
		}

		return ret;
	}

	std::vector<uint8_t> huffman_encoder_t::encode(const std::string &src) {
		std::vector< uint8_t > s(src.begin(), src.end());
		return encode(s);
	}

	std::vector<uint8_t> huffman_encoder_t::encode(const char *ptr) {
		std::string str(ptr);

		if ( nullptr == ptr )
			throw std::invalid_argument("HPACK::huffman_encoder_t::encode(): Invalid nullptr parameter");

		return encode(str);
	}

	void decoder_t::decode_integer(dec_vec_itr_t&beg, const dec_vec_itr_t &end, uint32_t &dst, uint8_t N) {
		const uint16_t two_N = static_cast< uint16_t >( math::binpow(2, N) - 1 );
		dec_vec_itr_t&  current(beg);

		if ( current == end )
			throw std::invalid_argument("HPACK::decoder_t::decode_integer(): Attempted to decode integer when at end of input");

		dst = ( (static_cast<unsigned char> (*current)) & two_N );


		if ( dst == two_N ) {
			uint64_t M = 0;

			for (current++; current != end; current++ ) {
				dst += ( ( static_cast<unsigned char> (*current) & 0x7F ) << M );
				M += 7;

				if ( !( static_cast<unsigned char> (*current) & 0x80 ) ) {
					current++;
					break;
				}
			}

			beg = current;
		}
		else
			beg = current+1;

	}

	error::status_or<std::string> decoder_t::parse_string(dec_vec_itr_t&itr, const dec_vec_itr_t &end) {
		std::string		dst;
		bool			huff(( static_cast<unsigned char>(*itr) & 0x80 ) == 0x80 ? true : false);

		unsigned int	len = 0;
		decode_integer(itr, end, len, 7);

		if (std::distance(itr, end) < len)
			throw std::invalid_argument("HPACK::decoder_t::parse_string(): string length exceeds length of input");

		std::copy(itr, itr + len, std::back_inserter(dst));

		itr += len;

		if ( true == huff ) {
			auto res = m_huffman.decode(dst, 4096);;
			if (!res.ok())
				return res.err();

			dst = res.unwrap();
		}

		return dst;
	}

	enum hpack_decode_state {
		HPACK_DECODE_HBYTE = 0,
		HPACK_DECODE_STR,
		HPACK_DECODE_INT,
		HPACK_DECODE_INT_ADDIT,
		HPACK_DECODE_STRLEN_ADDIT,
		HPACK_DECODE_STRLEN_ADDIT_HUFF,
		HPACK_DECODE_STR_FIN,
		HPACK_DECODE_STR_FIN_HUFF,
		HPACK_DECODE_HEADER_INDX,
		HPACK_DECODE_TABLE_RESIZE,
		HPACK_DECODE_HEADER_LITERAL_INDX,
		HPACK_DECODE_HEADER_LITERAL_NOINDX,
		HPACK_DECODE_HEADER_LITERAL_VALUE_INDX,
		HPACK_DECODE_HEADER_LITERAL_VALUE_NOINDX,
		HPACK_DECODE_HEADER_LITERAL_FIN_INDX,
		HPACK_DECODE_HEADER_LITERAL_FIN_NOINDX,
		HPACK_DECODE_RST,
		HPACK_DECODE_BUG
	};

	decoder_t::decoder_t(int64_t max, uint32_t headers_size, uint16_t key_size, uint16_t label_size)  : m_dynamic(max) {
		this->n1 = 0;
		this->key_size = key_size;
		this->headers_size = headers_size;
		this->label_size = label_size;
		this->n2 = 0;
		this->state = HPACK_DECODE_HBYTE;
		this->next = HPACK_DECODE_BUG;
		this->flags = 0;
	}

	decoder_t::~decoder_t() = default;

	void decoder_t::m_dynamic_max(int64_t max) {
		m_dynamic.max(max);
	}

	manapi::error::status decoder_t::decode(const char *ptr) {
		if ( nullptr == ptr )
			return manapi::error::status_invalid_argument("hpack:Invalid nullptr parameter");
		return decode(std::string_view(ptr));
	}

#define INDEXED_BIT_PATTERN 0x80
#define LITERAL_INDEXED_BIT_PATTERN 0x40
#define LITERAL_WITHOUT_INDEXING_BIT_PATTERN 0x00
#define LITERAL_NEVER_INDEXED_BIT_PATTERN 0x10
#define HUFFMAN_ENCODED 0x80

	manapi::error::status decoder_t::decode(std::string_view data) {
		try {
			auto const end = data.end();
			for ( decltype(auto) itr = data.begin(); itr != end; /* itr++ */ ) {
				repeat: switch (this->state) {
					case HPACK_DECODE_HBYTE: {
						if ( ( static_cast<unsigned char>(*itr)& 0x80 ) ) {
							this->n1 = 7;
							this->state = HPACK_DECODE_INT;
							this->next = HPACK_DECODE_HEADER_INDX;
							goto repeat;
						}

						if ( 0x20 == ( static_cast<unsigned char>(*itr) & 0x60 ) ) {
							this->n1 = 5;
							this->state = HPACK_DECODE_INT;
							this->next = HPACK_DECODE_TABLE_RESIZE;
							goto repeat;
						}

						this->state = HPACK_DECODE_INT;
						if ( ( static_cast<unsigned char>(*itr) & 0xC0 ) == 0x40 ) {
							this->n1 = 6;
							this->next = HPACK_DECODE_HEADER_LITERAL_INDX;
						}
						else {
							this->n1 = 4;
							this->next = HPACK_DECODE_HEADER_LITERAL_NOINDX;
						}

						goto repeat;
					}
					case HPACK_DECODE_STR:
						this->state = (( static_cast<unsigned char>(*itr) & 0x80 ) == 0x80);
					case HPACK_DECODE_INT: {
						/**
						 * n1 - bits
						 * n2 - result
						 */

						const auto two_N = static_cast< uint16_t >( math::binpow(2, this->n1) - 1 );

						if ( itr == data.end() )
							return manapi::error::status_out_of_range("hpack:Attempted to decode integer when at end of input");

						this->n2 = ( (static_cast<unsigned char> (*itr)) & two_N );
						itr++;

						this->n1 = 0;
						if ( this->n2 == two_N ) {
							if (this->state == HPACK_DECODE_INT)
								this->state = HPACK_DECODE_INT_ADDIT;
							else
								// state can be 1 or 0
									this->state = this->state ? HPACK_DECODE_STRLEN_ADDIT_HUFF : HPACK_DECODE_STRLEN_ADDIT;
						}
						else {
							if (this->state == HPACK_DECODE_INT) {
								this->state = this->next;
								this->next = HPACK_DECODE_BUG;
								goto repeat;
							}

							// state can be 1 or 0
							this->state = this->state ? HPACK_DECODE_STR_FIN_HUFF : HPACK_DECODE_STR_FIN;
							goto repeat;
						}

						if (this->state == HPACK_DECODE_HEADER_INDX
							|| this->state == HPACK_DECODE_TABLE_RESIZE)
							goto repeat;
						break;
					}
					case HPACK_DECODE_INT_ADDIT:
					case HPACK_DECODE_STRLEN_ADDIT:
					case HPACK_DECODE_STRLEN_ADDIT_HUFF: {
						for (; itr != end; itr++ ) {
							this->n2 += ( ( static_cast<unsigned char> (*itr) & 0x7F ) << this->n1 );
							this->n1 += 7;

							if ( !( static_cast<unsigned char> (*itr) & 0x80 ) ) {
								itr++;

								/* finish */
								this->n1 = 0;
								if (this->state == HPACK_DECODE_INT_ADDIT) {
									this->state = this->next;
									this->next = HPACK_DECODE_BUG;
									goto repeat;
								}

								this->state = this->state == HPACK_DECODE_STRLEN_ADDIT_HUFF ?
									HPACK_DECODE_STR_FIN_HUFF : HPACK_DECODE_STR_FIN;

								/* may be 0 */
								goto repeat;
							}

							if (this->n1 >= 28)
								return manapi::error::status_out_of_range("hpack:Int overflow");
						}

						break;
					}
					case HPACK_DECODE_STR_FIN:
					case HPACK_DECODE_STR_FIN_HUFF: {
						/**
						 * n2 - len
						 */

						/**
						 * note: std::distance for std::string_view
						 * should run in O(1)
						 */

						auto const copy = std::min<size_t>(
							std::distance(itr, end), this->n2);

						uint32_t msize = 4096;

						if (this->next == HPACK_DECODE_HEADER_LITERAL_VALUE_INDX
							|| this->next == HPACK_DECODE_HEADER_LITERAL_VALUE_NOINDX)
							msize = this->key_size;

						if (this->next == HPACK_DECODE_HEADER_LITERAL_FIN_NOINDX
							|| this->next == HPACK_DECODE_HEADER_LITERAL_FIN_INDX)
							msize = this->label_size;

						if (this->buff1.size() + copy > msize) {
							this->state = HPACK_DECODE_RST;
							return manapi::error::status_resource_exhausted("hpack:Len is limited");
						}

						std::copy(itr, itr + copy,
							std::back_inserter(this->buff1));

						itr += copy;
						this->n2 -= copy;

						if (!this->n2) {
							if ( (this->state == HPACK_DECODE_STR_FIN_HUFF) ) {
								auto res = this->m_huffman.decode(this->buff1, msize);
								if (!res.ok())
									return res.err();

								this->buff1 = res.unwrap();
							}

							this->state = this->next;
							this->next = HPACK_DECODE_BUG;
							goto repeat;
						}

						//assert((itr != end));

						break;
					}
					case HPACK_DECODE_HEADER_INDX: {
						/**
						 * RFC7541 (6.1) Indexed Header Field Representation
						 */

						/**
						 * n2 - result
						 */

						if ( 0 == this->n2 )
							/* decoding error */
								return error::status_out_of_range("invalid index");



						auto res = m_dynamic.get_header(this->n2);
						this->n2 = 0;
						if (!res.ok())
							return res.err();

						auto val = res.unwrap();

						if (val->first.empty())
							return error::status_aborted("header name is empty");

						if (this->flags & HPACK_DECOMPRESS_REGULAR_HEADERS) {
							if (val->first[0] == ':')
								return error::status_aborted("invalid header");
						}
						else {
							if (val->first[0] != ':')
								this->flags |= HPACK_DECOMPRESS_REGULAR_HEADERS;
						}

						auto it = m_headers.find(val->first);
						if (it == m_headers.end()) {
							m_headers.insert(*val);
						}
						else {
							if (val->first[0] == ':')
								return manapi::error::status_aborted("duplicate pesudo-header");
							if (net::http::header_has_more_fields(val->first))
								it->second += "," + val->second;
						}
						this->state = HPACK_DECODE_HBYTE;

						break;
					}
					case HPACK_DECODE_TABLE_RESIZE: {
						/**
						 * n2 - size
						 */
						if ( this->n2 > this->m_dynamic.max() ) {
							// decoding error
							return error::status_out_of_range("invalid size");
						}

						this->m_dynamic.max(
							std::exchange(this->n2, 0));
						this->state = HPACK_DECODE_HBYTE;

						break;
					}
					case HPACK_DECODE_HEADER_LITERAL_INDX:
					case HPACK_DECODE_HEADER_LITERAL_NOINDX: {
						/**
						 * n2 - index
						 */

						int const next = this->state == HPACK_DECODE_HEADER_LITERAL_INDX ?
							HPACK_DECODE_HEADER_LITERAL_VALUE_INDX : HPACK_DECODE_HEADER_LITERAL_VALUE_NOINDX;

						if ( 0 != this->n2 ) {
							auto res = m_dynamic.get_header(this->n2);
							this->n2 = 0;

							if (res.ok())
								this->buff1 = res.unwrap()->first;
							else
								return res.err();

							this->state = next;
						}
						else {
							this->n1 = 7;
							this->state = HPACK_DECODE_STR;
							this->next = next;
						}

						break;
					}
					case HPACK_DECODE_HEADER_LITERAL_VALUE_INDX:
					case HPACK_DECODE_HEADER_LITERAL_VALUE_NOINDX: {
						/**
						 * buff1 - header name
						 */
						this->buff2 = std::move(this->buff1);
						this->n1 = 7;
						this->next = this->state == HPACK_DECODE_HEADER_LITERAL_VALUE_INDX ?
							HPACK_DECODE_HEADER_LITERAL_FIN_INDX : HPACK_DECODE_HEADER_LITERAL_FIN_NOINDX;
						this->state = HPACK_DECODE_STR;
						break;
					}
					case HPACK_DECODE_HEADER_LITERAL_FIN_INDX:
					case HPACK_DECODE_HEADER_LITERAL_FIN_NOINDX: {
						/**
						 * buff1 - value
						 * buff2 - key
						 */

						if (this->headers_size < this->buff1.size() + this->buff2.size()) {
							this->state = HPACK_DECODE_RST;
							return manapi::error::status_resource_exhausted("size is limited");
						}

						this->headers_size -= this->buff1.size() + this->buff2.size();

						if (buff2.empty())
							return error::status_aborted("header name is empty");

						if (this->flags & HPACK_DECOMPRESS_REGULAR_HEADERS) {
							if (this->buff2[0] == ':')
								return error::status_aborted("invalid header");
						}
						else {
							if (this->buff2[0] != ':')
								this->flags |= HPACK_DECOMPRESS_REGULAR_HEADERS;
						}

						if (this->state == HPACK_DECODE_HEADER_LITERAL_FIN_INDX)
							this->m_dynamic.add (header_t (this->buff2, this->buff1));

						auto existing = this->m_headers.find(this->buff2);
						if (existing != this->m_headers.end()) {
							/**
							 * Note: In practice, the "Set-Cookie" header field([COOKIE])
							 * often appears in a response message across multiple field
							 * lines and does not use the list syntax, violating the
							 * above requirements on multiple field lines with the same
							 * field name.Since it cannot be combined into a single field value,
							 * recipients ought to handle "Set-Cookie" as a special case while
							 * processing fields. (See Appendix A.2.3 of[Kri2001] for details.)
							 *
							 */

							if (this->buff2[0] == ':')
								return manapi::error::status_aborted("duplicate pesudo-header");
							existing->second.push_back(',');
							existing->second.append(this->buff1);
						}
						else {
							this->m_headers.insert({std::move(this->buff2), std::move(this->buff1)});
						}

						this->buff1.clear();
						this->buff2.clear();

						this->state = HPACK_DECODE_HBYTE;

						break;
					}
					case HPACK_DECODE_RST: {
						this->buff1.clear();
						this->buff2.clear();
						this->n1 = 0;
						this->n2 = 0;
						this->state = HPACK_DECODE_HBYTE;
						this->flags = 0;

						break;
					}
					default: {
						return error::status_internal("hpack: unreachable section");
					}
				}
			}

			return error::status_ok();
		}
		catch (std::exception const &e) {
			MANAPIHTTP_LOG ("unexpected hpack bug: {}", e.what());
		}

		return error::status_internal("hpack: due to exception");
	}

	manapi::error::status_or<std::map< std::string, std::string >> decoder_t::headers(uint32_t headers_size) {
		this->headers_size = headers_size;

		bool const flg = this->state == HPACK_DECODE_HBYTE;
		this->state = HPACK_DECODE_RST;

		if (flg)
			return std::move(this->m_headers);

		return manapi::error::status_aborted("hpack: unexpended end");
	}

	void encoder_t::huff_encode(const std::string &str) {
		std::vector< uint8_t >	huffbuff(0);
		std::size_t				len(0);


		huffbuff = m_huffman.encode(str);

		if ( 128 > huffbuff.size() )
			m_buf.push_back(static_cast< uint8_t >( HUFFMAN_ENCODED | huffbuff.size() ));
		else {
			std::vector< uint8_t > tmp;
			encode_integer(tmp, huffbuff.size(), 7);
			tmp.front() |= HUFFMAN_ENCODED;
			m_buf.insert(m_buf.end(), tmp.begin(), tmp.end());
		}

		m_buf.insert(m_buf.end(), huffbuff.begin(), huffbuff.end());
		return;
	}

	bool encoder_t::find(const header_t &h, int64_t &index) {
		int64_t saved_index(-1);
		index = -1;

		for ( uint64_t idx = 1; idx < predefined_headers.size(); idx++ ) {
			if ( !h.first.compare(predefined_headers.at(static_cast< std::size_t >( idx )).first) &&
				!h.second.compare(predefined_headers.at(static_cast< std::size_t >( idx )).second) ) {
				index = idx;
				return true;
				}
			if ( !h.first.compare(predefined_headers.at(static_cast< std::size_t >( idx )).first) ) {
				index = idx;
			}
		}

		saved_index = index;

		if ( true == m_dynamic.find(h, index) )
			return true;
		else if ( -1 != index )
			return false;

		if ( -1 != saved_index )
			index = saved_index;

		return false;
	}

	uint64_t encoder_t::encode_integer(std::vector<uint8_t> &dst, uint32_t I, uint8_t N) {
		const uint16_t two_N = static_cast< uint16_t >( std::pow(2, N) - 1 );

		if ( I < two_N ) {
			dst.push_back(static_cast< uint8_t >( I ));
			return 1;
		} else {
			I -= two_N;
			dst.push_back(static_cast< uint8_t >( two_N ));

			while ( I >= 128 ) {
				dst.push_back(static_cast< uint8_t >( ( I & 0x7F ) | 0x80 ));
				I >>= 7;
			}

			dst.push_back(static_cast< uint8_t >( I ));
			return dst.size();
		}

		// unreached
		throw std::runtime_error("HPACK::encoder_t::encode_integer(): Impossible code path");
	}

	encoder_t::encoder_t(uint64_t max) : m_dynamic(max) { }

	encoder_t::~encoder_t() = default;

	void encoder_t::max_table_size(uint64_t max) {
		m_dynamic.max(max);
	}

	uint64_t encoder_t::max_table_size() const {
		return m_dynamic.max();
	}

	void encoder_t::add(const std::string &n, const std::string &v, bool huffman, bool never_indexed) {
		header_t h(n, v);
		add(h, huffman, never_indexed);
	}

	void encoder_t::add(const char *n, const char *v, bool huffman, bool never_indexed) {
		header_t h(n, v);

		if ( nullptr == n || nullptr == v )
			throw std::invalid_argument("HPACK::encoder_t::add(): Invalid nullptr parameter.");

		add(h, huffman, never_indexed);
	}

	void encoder_t::add(const header_t &h, bool huffman, bool never_indexed) {
		int64_t					index(0);
		std::vector< uint8_t >	buf(0), huffbuff(0);

		if ( false == never_indexed && true == find(h, index) ) {
			encode_integer(buf, static_cast< uint32_t >(index), 7);

			buf.front() |= INDEXED_BIT_PATTERN;
			m_buf.insert(m_buf.end(), buf.begin(), buf.end());
			buf.clear();
		} else if ( false == never_indexed && -1 != index ) {
			m_dynamic.add(h.first, h.second);

			if ( false == never_indexed ) {
				encode_integer(buf, static_cast< uint32_t >(index), 6);
				buf.front() |= LITERAL_INDEXED_BIT_PATTERN;
				m_buf.insert(m_buf.end(), buf.begin(), buf.end());
				buf.clear();
			} else {
				encode_integer(buf, static_cast< uint32_t >(index), 4);
				buf.front() |= LITERAL_NEVER_INDEXED_BIT_PATTERN;
				m_buf.insert(m_buf.end(), buf.begin(), buf.end());
				buf.clear();
			}

			if ( true == huffman )
				huff_encode(h.second);
			else {
				buf.clear();
				encode_integer(buf, h.second.length(), 7);
				m_buf.insert(m_buf.end(), h.second.begin(), h.second.end());
			}
		} else {
			if ( false == never_indexed ) {
				m_dynamic.add(h.first, h.second);
				m_buf.push_back(LITERAL_INDEXED_BIT_PATTERN);
			} else
				m_buf.push_back(LITERAL_NEVER_INDEXED_BIT_PATTERN);

			if ( true == huffman && false == never_indexed )
				huff_encode(h.first);
			else {
				buf.clear();
				encode_integer(buf, h.first.length(), 7);
				m_buf.insert(m_buf.end(), buf.begin(), buf.end());
				m_buf.insert(m_buf.end(), h.first.begin(), h.first.end());
			}
			if ( true == huffman && false == never_indexed )
				huff_encode(h.second);
			else {
				buf.clear();
				encode_integer(buf, h.second.length(), 7);
				m_buf.insert(m_buf.end(), buf.begin(), buf.end());
				m_buf.insert(m_buf.end(), h.second.begin(), h.second.end());
			}
		}
	}

	std::string encoder_t::data() {
		return std::move(m_buf);
	}
}
