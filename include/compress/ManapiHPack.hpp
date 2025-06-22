#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <utility>
#include <vector>
#include <array>
#include <deque>
#include <map>
#include <limits>
#include <string>
#include <set>

#include "../ManapiMath.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"

namespace manapi::compress::hpack
{
	typedef std::pair< std::string, std::string > header_t;


	class huffman_node_t
	{
		private:
		protected:
			huffman_node_t*	m_left;
			huffman_node_t* m_right;
			int16_t		m_code;
		public:
			huffman_node_t(huffman_node_t* l = nullptr, huffman_node_t* r = nullptr, int16_t c = -1);
			virtual ~huffman_node_t();
			int16_t code() const;
			void code(int16_t c);
			huffman_node_t* left();
			void left(huffman_node_t* l);
			huffman_node_t* right();
			void right(huffman_node_t* r);
	};

	class huffman_tree_t
	{
		private:
		protected:
			huffman_node_t* m_root;

			void delete_node( huffman_node_t* n);

		public:
			huffman_tree_t();

			virtual ~huffman_tree_t();

			error::status_or<std::string> decode(std::string_view src, uint32_t maxlen);
	};

	class ringtable_t
	{
		private:
		protected:
			uint64_t m_max;
			std::deque< header_t > m_queue;

		public:
			ringtable_t();
			ringtable_t(uint64_t m);
			virtual ~ringtable_t();

			void max(uint64_t m);

			uint64_t max() const;

			uint64_t entries_count() const;

			uint64_t length() const;

			void add(const header_t&  h);

			void add(const std::string& n, const std::string& v);

			void add(const char* n, const char* v);

			const header_t& at(uint64_t idx);

			bool find(const header_t& h, int64_t& index) const;


			[[nodiscard]] manapi::error::status_or<const header_t *> get_header(const std::size_t index) const;

	};
	
	class huffman_encoder_t
	{
		private:
			uint8_t m_byte;
			uint8_t m_count;

		protected:
			bool write_bit(uint8_t bit);

		public:
			huffman_encoder_t();

			virtual ~huffman_encoder_t();

			std::vector< uint8_t > encode(std::vector< uint8_t >& src);

			std::vector< uint8_t > encode(const std::string& src);

			std::vector< uint8_t > encode(const char* ptr);
	};

	/*! \Class The HPACK decoder class.
	 *  \Brief A wrapper class that ties together the static, dynamic tables and huffman
	 *  encoding such that one can pass in a HTTPv2 header block and retrieve a map of strings
	 *  that container the headers sent.
	 *
	 * \Warning Never Indexed code paths under tested.
	 */
	class decoder_t
	{
		private:
		protected:
			std::map< std::string, std::string > m_headers;
			manapi::compress::hpack::ringtable_t m_dynamic;
			huffman_tree_t m_huffman;

			uint8_t state;
			uint8_t next;
			int n1;
			uint32_t n2;
			std::string buff1;
			std::string buff2;
			uint32_t headers_size;
			uint16_t key_size;
			uint16_t label_size;

			typedef std::string_view::iterator dec_vec_itr_t;

			void decode_integer(dec_vec_itr_t& beg, const dec_vec_itr_t& end, uint32_t& dst, uint8_t N);

			error::status_or<std::string> parse_string(dec_vec_itr_t& itr, const dec_vec_itr_t& end);

		public:
			/*!
				\fn encoder_t(uint64_t max = 4096)
				\Brief Constructs the encoder

				\param max the maximum size of the dynamic table; unbounded and allowed to exceed RFC sizes
			*/
			decoder_t(int64_t max, uint32_t headers_size, uint16_t key_size, uint16_t label_size);

			virtual ~decoder_t();

			void m_dynamic_max (int64_t max);

			/*!
				\fn bool decode(const std::string&)
				\Brief Decodes the HTTPv2 Header Block contained within the parameter

				\param ptr the HTTPv2 Header Block
				\return True if decoding was successful, false if an error such as a protocol decoding error was encountered.

				\Warning Never indexed code paths were under tested.
			*/
			manapi::error::status decode(const char* ptr);


			/*!
				\fn bool decode(const std::string&)
				\Brief Decodes the HTTPv2 Header Block contained within the parameter

				\param data the HTTPv2 Header Block
				\return True if decoding was successful, false if an error such as a protocol decoding error was encountered.

				\Warning Never indexed code paths were under tested.
			*/

			manapi::error::status decode(std::string_view data);


			/*!
				\fn const std::map< std::string, std::string >& headers(void) const
				\Brief Retrieves the interally managed header map of decoded headers

				\Return The map of the decoded headers
			 */
			manapi::error::status_or<std::map< std::string, std::string >> headers(uint32_t headers_size);
	};


	/*! \Class The HPACK encoder class. 
     *  \Brief A wrapper class that ties together the ringtable_t dynamic table implementation
	 * with the prior static table such that one can simply add(name, value) into an
	 * internally managed buffer (a std::vector< uint8_t >) which can be retrieved at
	 * the end of operations. Huffman encoding and dynamic table references are handled 
	 * automatically; 
	 * 
	 * \Warning Never Indexed code paths untested.
	 */
	class encoder_t
	{
		private:
		protected:
			std::string m_buf;
			manapi::compress::hpack::ringtable_t m_dynamic;
			manapi::compress::hpack::huffman_encoder_t m_huffman;

			void huff_encode(const std::string& str);

			bool find(const header_t& h, int64_t& index);

			uint64_t encode_integer(std::vector< uint8_t >& dst, uint32_t I, uint8_t N);

		public:
			/*!
			\fn encoder_t(uint64_t max = 4096)
			\Brief Constructs the encoder

			\param max the maximum size of the dynamic table; unbounded and allowed to exceed RFC sizes
			*/
			encoder_t(uint64_t max = 4096);

			virtual ~encoder_t();

			/*!
			\fn void max_table_size(uint64_t max)
			\Brief Resizes the dynamic table

			\param max the maximum size of the dynamic table; unbounded and allowed to exceed RFC sizes
			*/
			void 
			max_table_size(uint64_t max);

			/*!
			\fn inline uint64_t max_table_size(void) const
			\Brief Retrieve the size of dynamic table

			\return max the maximum size of the dynamic table; unbounded and allowed to exceed RFC sizes
			*/
			uint64_t max_table_size() const ;

			/*!
			\fn void add(const std::string& n, const std::string& v, bool huffman = true, bool never_indexed = false)
			\Brief Add a header name-value pair to the header list
			
			\param n the name-value pair name member
			\param v the name-value pair value member
			\param huffman a boolean value that indicates whether to huffman encode any related string literals
			\param never_indexed whether to set the never indexed flag for the name-value pair
			*/
			void add(const std::string& n, const std::string& v, bool huffman = true, bool never_indexed = false);

			/*!
			\fn void add(const char* n, const char* v, bool huffman = true, bool never_indexed = false)
			\Brief Add a header name-value pair to the header list
			\Throws std::invalid_argument() when n or v is null

			\param n the name-value pair name member
			\param v the name-value pair value member
			\param huffman a boolean value that indicates whether to huffman encode any related string literals
			\param never_indexed whether to set the never indexed flag for the name-value pair
			*/
			void add(const char* n, const char* v, bool huffman = true, bool never_indexed = false);
			/*!
			\fn void add(const header_t& h, bool huffman = true, bool never_indexed = false)
			\Brief Add a header name-value pair to the header block

			\param h the name-value header_t pair to be added to the header block
			\param huffman a boolean value that indicates whether to huffman encode any related string literals
			\param never_indexed whether to set the never indexed flag for the name-value pair
			*/
			void add(const header_t& h, bool huffman = true, bool never_indexed = false);

			std::string data();
		};
}