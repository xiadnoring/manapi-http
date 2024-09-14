#ifndef MANAPIJSONWEBTOKENDEFAULTS_HPP
#define MANAPIJSONWEBTOKENDEFAULTS_HPP

#ifndef JWT_DISABLE_PICOJSON
#define JWT_DISABLE_PICOJSON
#endif

#include "ManapiJsonWebTokenTraits.hpp"


namespace jwt {
	/**
	 * \brief a class to store a generic [picojson](https://github.com/kazuho/picojson) value as claim
	 *
	 * This type is the specialization of the \ref basic_claim class which
	 * uses the standard template types.
	 */
	using claim = basic_claim<traits::manapi_json>;

	/**
	 * Create a verifier using the default clock
	 * \return verifier instance
	 */
	inline verifier<default_clock, traits::manapi_json> verify() {
		return verify<default_clock, traits::manapi_json>(default_clock{});
	}

	/**
	 * Return a builder instance to create a new token
	 */
	inline builder<traits::manapi_json> create() { return builder<traits::manapi_json>(); }

#ifndef JWT_DISABLE_BASE64
	/**
	 * Decode a token
	 * \param token Token to decode
	 * \return Decoded token
	 * \throw std::invalid_argument Token is not in correct format
	 * \throw std::runtime_error Base64 decoding failed or invalid json
	 */
	inline decoded_jwt<traits::manapi_json> decode(const std::string& token) {
		return decoded_jwt<traits::manapi_json>(token);
	}
#endif

	/**
	 * Decode a token
	 * \tparam Decode is callabled, taking a string_type and returns a string_type.
	 * It should ensure the padding of the input and then base64url decode and
	 * return the results.
	 * \param token Token to decode
	 * \param decode The token to parse
	 * \return Decoded token
	 * \throw std::invalid_argument Token is not in correct format
	 * \throw std::runtime_error Base64 decoding failed or invalid json
	 */
	template<typename Decode>
	decoded_jwt<traits::manapi_json> decode(const std::string& token, Decode decode) {
		return decoded_jwt<traits::manapi_json>(token, decode);
	}

	/**
	 * Parse a jwk
	 * \param token JWK Token to parse
	 * \return Parsed JWK
	 * \throw std::runtime_error Token is not in correct format
	 */
	inline jwk<traits::manapi_json> parse_jwk(const traits::manapi_json::string_type& token) {
		return jwk<traits::manapi_json>(token);
	}

	/**
	 * Parse a jwks
	 * \param token JWKs Token to parse
	 * \return Parsed JWKs
	 * \throw std::runtime_error Token is not in correct format
	 */
	inline jwks<traits::manapi_json> parse_jwks(const traits::manapi_json::string_type& token) {
		return jwks<traits::manapi_json>(token);
	}

	/**
	 * This type is the specialization of the \ref verify_ops::verify_context class which
	 * uses the standard template types.
	 */
	using verify_context = verify_ops::verify_context<traits::manapi_json>;
} // namespace jwt

#endif //MANAPIJSONWEBTOKENDEFAULTS_HPP
