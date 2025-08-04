/**
 * @file components/ManapiSlice.hpp
 * @brief provides slice structures and utils
 *
 * @author Timur Zajnullin
 */

#pragma once

#include "./ManapiBuffer.hpp"
#include "./ManapiChain.hpp"
#include "../components/ManapiEventStructures.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"

namespace manapi {
    class slice_base;

    struct slice_part_t {
        manapi::ev::buff_t buff;
        slice_part_t *next;
    };

    class slice_iterator {
    public:
        slice_iterator (slice_part_t *part, slice_base *base);

        slice_iterator (const slice_iterator &n);

        slice_iterator &operator=(const slice_iterator &n);

        ~slice_iterator();

        slice_iterator &operator++();

        slice_iterator &operator++(int);

        bool operator==(const slice_iterator &n) const;

        /**
         * access the storage buffer via a pointer
         * @return the storage buffer pointer
         */
        void *buffer ();

        /**
         * get the storage buffer size
         * @return the storage buffer size
         */
        MANAPIHTTP_NODISCARD std::size_t size () const;

        /**
         * is it the last in chain
         * @return true if it's the last in the chain; otherwise, returns false
         */
        MANAPIHTTP_NODISCARD bool is_last () const;
    private:
        slice_part_t *part;
        slice_base *base_;
    };

    class slice_base {
    public:
        struct slice_part_deleter {
            void operator () (slice_part_t *ptr);
        };

        slice_base (std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff);

        slice_base (std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff, uint32_t rshift);

        slice_base (slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size);

        virtual ~slice_base();

        slice_base (slice_base &&n) MANAPIHTTP_NOEXCEPT;

        slice_base &operator=(slice_base &&n) MANAPIHTTP_NOEXCEPT;

        slice_base (const slice_base &n);

        slice_base &operator=(const slice_base &n);

        /**
         * Adjusts the left shift by |shift| and clears buffer storages if required
         *
         * WARNING: This operation is irreversible
         *
         * @param shift left shift to add
         * @return Ok if success; otherwise it returns OutOfRange
         */
        manapi::error::status shift_add (std::size_t shift) MANAPIHTTP_NOEXCEPT;

        manapi::error::status copy_from (const void *buffer, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT;

        manapi::error::status copy_to (void *buffer, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT;

        manapi::error::status copy_from (slice_base &n, std::size_t shift, std::size_t shift_n, std::size_t size) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD manapi::error::status_or<manapi::slice_base> subslice (std::size_t pos, std::size_t size = 0) const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t shift () const;

        MANAPIHTTP_NODISCARD std::size_t rshift () const;

        MANAPIHTTP_NODISCARD bool empty () const;

        MANAPIHTTP_NODISCARD const slice_part_t *slices_begin () const;

        MANAPIHTTP_NODISCARD const slice_part_t *slices_end () const;

        slice_iterator begin ();

        slice_iterator end ();

        MANAPIHTTP_NODISCARD int cmp (const manapi::slice_base &n) const;

        void slices_buffs (ev::buff_t *buffs) const;

        MANAPIHTTP_NODISCARD std::unique_ptr<ev::buff_t, ev::buffer_deleter> slices_buffs () const;

        MANAPIHTTP_NODISCARD std::size_t slices_size () const;

        MANAPIHTTP_NODISCARD std::size_t size () const;
    protected:
        manapi::error::status rshift_add_ (std::size_t s) MANAPIHTTP_NOEXCEPT;

        std::size_t size_;

        uint32_t shift_;
        /**
         * !!! rshift only for slice_view !!!
         *
         * bcz using class slice we will
         * split the slice and will return the cut part
         * to the memory fabric
         */
        uint32_t rshift_;
        slice_part_t *first;
        slice_part_t *last;
        uint32_t count;
    };

    class slice final : public slice_base {
    public:
        slice ();

        static manapi::error::status_or<slice> create (std::size_t n) MANAPIHTTP_NOEXCEPT;

        slice (std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff);

        slice (std::unique_ptr<slice_part_t, slice_part_deleter> buffs,  uint32_t nbuff, uint32_t rshift);

        slice (slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size);

        //slice (slice_base n);

        slice (slice &&n) MANAPIHTTP_NOEXCEPT;

        slice &operator=(slice &&n) MANAPIHTTP_NOEXCEPT;

        ~slice() override;

        /**
         * Resize the slices chain capacity
         * @param size capacity
         * @return Ok if success; otherwise, ResourceExhausted
         */
        manapi::error::status resize (std::size_t size) MANAPIHTTP_NOEXCEPT;

        /**
         * Adds |buffer| to the slices chain
         * @param buffer bytebuffer
         * @return Ok if success; otherwise, ResourceExhausted
         */
        manapi::error::status push_back (bytebuffer buffer) MANAPIHTTP_NOEXCEPT;

        /**
         * Adds |s| to the slices chain
         * @param s slices
         * @return Ok if success; otherwise, InternalError
         */
        manapi::error::status push_back (slice s) MANAPIHTTP_NOEXCEPT;

        /**
         * Adds |buffer| to the slices chain
         * @param buffer buffer pointer
         * @param size buffer size
         * @return Ok if success, otherwise, ResourceExhausted
         */
        manapi::error::status push_back (const void *buffer, ssize_t size) MANAPIHTTP_NOEXCEPT;

        void clear () MANAPIHTTP_NOEXCEPT;
    };

    class slice_view final : public slice_base {
    public:
        slice_view ();

        slice_view (slice_part_t *part);

        slice_view (slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size);

        slice_view (const slice_base &n);

        slice_view (const slice &n);

        slice_view (const slice_view &n);

        slice_view &operator=(const slice_view &n);

        ~slice_view() override;
    private:
    };

    class slice_ref final : public slice_base {
    public:
        slice_ref ();

        slice_ref (const slice_ref &n);

        slice_ref &operator=(const slice_ref &n);

        slice_ref (slice_ref &&n) MANAPIHTTP_NOEXCEPT;

        slice_ref &operator=(slice_ref &&n) MANAPIHTTP_NOEXCEPT;

        ~slice_ref() override;

        manapi::error::status push_back (const void *buffer, std::size_t size);

        void clear () MANAPIHTTP_NOEXCEPT;
    private:
    };
}
