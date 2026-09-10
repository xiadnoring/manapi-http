/**
 * @file hash/ManapiSHA256.hpp
 * @brief SHA256
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"
#include "./ManapiHash.hpp"

namespace manapi::hash {
    class sha256 : public hash_base {
        protected:
            typedef unsigned char uint8;
            typedef unsigned int uint32;
            typedef unsigned long long uint64;

            const static uint32 sha256_k[];
            static const unsigned int SHA224_256_BLOCK_SIZE = (512/8);
        public:
            sha256();
            void update(const unsigned char *message, std::size_t len) override;
            void final(unsigned char *digest) override;
            MANAPIHTTP_NODISCARD std::size_t final_size () const override;
            static const unsigned int DIGEST_SIZE = ( 256 / 8);

        protected:
            void transform(const unsigned char *message, std::size_t block_nb);
            std::size_t m_tot_len;
            std::size_t m_len;
            unsigned char m_block[2*SHA224_256_BLOCK_SIZE];
            uint32 m_h[8];
    };

    /**
     * sha256 function to work using std strings
     *
     * @param input the source string
     * @return a 32 byte sha256 string if completed successfuly, otherwise it returns OutOfRange, InternalError, ResourceExhausted
     */
    manapi::status_or<std::string> sha256str(std::string_view input);

    /**
     *
     * @param input the source string
     * @param output the output
     * @param size the output size
     * @return Ok if completed successfuly, otherwise it returns OutOfRange
     */
    manapi::status sha256str(std::string_view input, char *output, std::size_t size);
}