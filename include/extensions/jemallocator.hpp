/*
 * Copyright (C) 2015 Christopher Gilbert <christopher.john.gilbert@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

/*
 * Include the main jemalloc header exposing the public jemalloc interface
 */
#include "jemalloc/jemalloc.h"

/* The jemallocator namespace */
namespace jemallocator {

namespace __detail {
	template<class I, template<class> class... policies>
	class policy_compositor;

	template<class I, template<class> class A, template<class> class... policies>
	class policy_compositor<I, A, policies...> {
		typedef policy_compositor<I, policies...> tmp;
	public:
		typedef A<typename tmp::value> value;
	};

	template<class I>
	class policy_compositor<I> {
	public:
		typedef I value;
	};
}

template<typename T, template<class> class... policies> class jemallocator;

namespace __detail {
	template<class I>
	class base_impl {};

	template<typename T, template<class> class... policies>
	class base_impl<jemallocator<T, policies...>> {
	protected:
		typedef jemallocator<T, policies...> impl_type;

		// Returns the most derived jemallocator implementation type
		impl_type *_this() { return static_cast<impl_type*>(this); }
		const impl_type *_this() const { return static_cast<const impl_type*>(this); }

		// Specify the flags to use. Defaults to zero.
		int policy_flags(size_t bytes) const {
			return 0;
		}

		// Specify what to do when the allocation fails. Defaults to throw std::bad_alloc.
		void* policy_throwbadalloc(size_t bytes) const {
			throw std::bad_alloc();
		}

		// Callback when allocate is called, provides type information, number of items allocated
		// the total number of bytes allocated and the flags used.
		void policy_audit_allocate(const std::type_info& type, size_t n, size_t bytes, int flags) {}

		// Callback when deallocate is called, provides type information, number of items deallocated
		// and the total number of bytes deallocated.
		void policy_audit_deallocate(const std::type_info& type, size_t n, size_t bytes) {}

	public:
		using size_type       = size_t;
		using difference_type = ptrdiff_t;
		using pointer         = T*;
		using const_pointer   = const T*;
		using reference       = T&;
		using const_reference = const T&;
		using value_type      = T;

		template<typename U>
			struct rebind { using other = jemallocator<U, policies...>; };

		base_impl() noexcept {}
		base_impl(const base_impl &) noexcept {}
		template<typename U>
			base_impl(const jemallocator<U, policies...>&) {}
		~base_impl() {}

		pointer address(reference x) const noexcept { return &x; }
		const_pointer address(const_reference x) const noexcept { return &x; }

		pointer allocate(size_type n, void* hint = 0) {
			size_type size = n * sizeof(T);
			int flags = _this()->policy_flags(size);
			pointer ptr = static_cast<pointer>(mallocx(size, flags));
			if(!ptr)
				return static_cast<pointer>(_this()->policy_throwbadalloc(size));
			_this()->policy_audit_allocate(typeid(*ptr), n, size, flags);
			return static_cast<pointer>(ptr);
		}
		void deallocate(pointer p, size_type n) {
			free(p);
			_this()->policy_audit_deallocate(typeid(*p), n, n * sizeof(T));
		}

		size_type max_size() const noexcept { return (size_type(0) - size_type(1)) / sizeof(value_type); }

		template<typename U, typename... Args>
		void construct(U* p, Args&&... args) {
			new (p) U(std::forward<Args>(args)...);
		}
		template<typename U>
			void destroy(U* p) { p->~U(); }
	};
}

namespace jepolicy {
	/*
	 * An empty policy, does nothing.
	 */
	struct empty {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		};
	};
	/*
	 * A policy setting the arena to use to allocate the memory.
	 */
	template<size_t num> struct arena {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		protected:
			size_t policy_flags(size_t bytes) const {
				return B::policy_flags(bytes) | MALLOCX_ARENA(num);
			}
		};
	};
	/*
	 * A policy setting the alignment of the allocated memory .
	 */
	template<size_t alignment> struct align {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		protected:
			size_t policy_flags(size_t bytes) const {
				return B::policy_flags(bytes) | MALLOCX_ALIGN(alignment);
			}
		};
	};
	/*
	 * A policy causing the zeroing of the allocated memory.
	 */
	template<bool do_zero=true> struct zero {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		protected:
			size_t policy_flags(size_t bytes) const {
				return B::policy_flags(bytes) | (do_zero) ? MALLOCX_ZERO : 0;
			}
		};
	};
	/*
	 * A policy specifying whether to enable / disable thread caching.
	 */
	template<bool do_tcache=true> struct tcache {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		protected:
			size_t policy_flags(size_t bytes) const {
				return B::policy_flags(bytes) | (do_tcache) ? MALLOCX_TCACHE(0) : MALLOCX_TCACHE_NONE;
			}
		};
	};
	/*
	 * A policy specifying what to throw when an allocation failure occurs.
	 */
	template<typename T> struct badalloc {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		protected:
			void* policy_throwbadalloc(size_t bytes) const {
				throw T();
			}
		};
	};
	template<> struct badalloc<void> {
		template<class B> class policy : public B {
			template<class I> friend class __detail::base_impl;
		protected:
			void* policy_throwbadalloc(size_t bytes) const {
				return (void*)(-1);
			}
		};
	};
	/*
	 * A policy providing allocator auditing. Takes advantage of the fact that member function definitions
	 * are instantiated after their declarations (aka CRTP).
	 */
	template<typename T> struct audit {
		template<class B> class policy : public B, public T {
			template<class I> friend class __detail::base_impl;
		protected:
			void policy_audit_allocate(const std::type_info& type, size_t n, size_t bytes, int flags) {
				B::policy_audit_allocate(type, n, bytes, flags);
				static_cast<T*>(this)->audit_allocate(type, n, bytes, flags);
			}
			void policy_audit_deallocate(const std::type_info& type, size_t n, size_t bytes) {
				B::policy_audit_deallocate(type, n, bytes);
				static_cast<T*>(this)->audit_deallocate(type, n, bytes);
			}
		};
	};
}

/*
 * A policy driven STL allocator that uses jemalloc
 */
template<typename T, template<class> class... policies>
class jemallocator : public __detail::policy_compositor<__detail::base_impl<jemallocator<T, policies...>>, policies...>::value {
	typedef typename __detail::policy_compositor<__detail::base_impl<jemallocator<T, policies...>>, policies...>::value base;
public:
	jemallocator() noexcept {}
	jemallocator(const jemallocator &o) noexcept
		: base(o) {}
	template<typename U>
		jemallocator(const jemallocator<U, policies...> &o) noexcept {}
	~jemallocator() {}
};

// All specializations of jemallocator are interchangeable
template <typename T1, template<class> class... policies1, typename T2, template<class> class... policies2>
bool operator== (const jemallocator<T1, policies1...>&,
                 const jemallocator<T2, policies2...>&) noexcept {
    return true;
}
template <typename T1, template<class> class... policies1, typename T2, template<class> class... policies2>
bool operator!= (const jemallocator<T1, policies1...>&,
                 const jemallocator<T2, policies2...>&) noexcept {
    return false;
}

}