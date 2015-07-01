/* future.hpp
Non-allocating constexpr future-promise
(C) 2015 Niall Douglas http://www.nedprod.com/
File Created: May 2015


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifndef BOOST_SPINLOCK_FUTURE_HPP
#define BOOST_SPINLOCK_FUTURE_HPP

#include "monad.hpp"
#include <future>

/*! \file future.hpp
\brief Provides a lightweight next generation future with N4399 Concurrency TS extensions

\headerfile include/boost/spinlock/future.hpp ""
*/

/*! \defgroup future_promise Lightweight next generation STL compatible futures with N4399 C++ 1z Concurrency TS extensions

C++ 1z Concurrency TS extensions N-paper used: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4399.html

Promise-Futures supplied here:
<dl>
  <dt>`promise<T>`, `future<T>` and `shared_future<T>`</dt>
    <dd>Based on `monad<T>`, these provide a STL dropin.</dd>
  <dt>`promise_result<T>`, `future_result<T>` and `shared_future_result<T>`.</dt>
    <dd>Based on `result<T>`, these provide T and error_code transport, no exception_ptr.</dd>
  <dt>`promise_option<T>`, `future_option<T>` and `shared_future_option<T>`.</dt>
    <dd>Based on `option<T>`, these provide T and nothing more, no error transport at all.</dd>
</dl>

All the above have make ready functions of the form `make_ready_NAME`, `make_errored_NAME` and `make_exceptional_NAME`
as per N4399.

In exchange for some minor limitations, this lightweight promise-future is 2x-3x faster than
`std::promise` and `std::future` in the non-blocking case. You also get deep integration with basic_monad and
lots of cool functional programming stuff.

Known deviations from the ISO C++ standard specification:

- No memory allocation is done, so if your code overrides the STL allocator for promise-future it will be ignored.
- T must implement either or both the copy or move constructor, else it will static_assert.
- T cannot be error_type nor exception_type, else it will static_assert.
- set_value_at_thread_exit() and set_exception_at_thread_exit() are not implemented, nor probably ever will be.
- promise's and future's move constructor and move assignment are guaranteed noexcept in the standard. This promise's
and future's move constructor and assignment is noexcept only if type T's move constructor is noexcept.
- Only the APIs marked "SYNC POINT" in both promise and future synchronise memory. Calling APIs not marked "SYNC POINT"
can return stale information, so don't write code which has a problem with that (specifically, do NOT have multiple threads examining
a future for state concurrently unless they are exclusively using SYNC POINT APIs to synchronise memory between them).

 When might this be a problem in real world code? For example, valid() which is not a SYNC POINT API may return true when it is in
fact false. If your code uses a synchronisation mechanism which is not a SYNC POINT API - most usually, this is "synchronised
by time/sleep" - and then executes code which depends on valid() being correct as it would always be with STL future promise
as valid() there synchronises memory, your code will be racy. The simplest solution is to call any SYNC POINT API before
examining valid(), or issue a memory fence (std::atomic_thread_fence), or best of all refactor your code to not use synchronised
by time/sleep in the first place. The thread sanitiser tsan reports any use of time to synchronise as a failure which is the
correct thing to do - just don't do it in your code.

Other things to consider:

- As both promise and future must have sizeof greater than sizeof(T), don't use multi-Kb sized T's
as they'll get copied and moved around.
- Don't use any of the `monad_errc` nor `future_errc` error codes for the errored return, else expect misoperation.

## Supplying your own implementations of `basic_future<T>` ##

Just as with basic_monad, basic_promise and basic_future are highly customisable with any kind of semantics or error
types you like.

To do this, simply supply a policy type of the following form. Note that this is identical to basic_monad's policy,
except for the added members which are commented:
\snippet future_policy.ipp future_policy
*/

// Used by constexpr testing to make sure I haven't borked any constexpr fold paths
//#define BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING

#if BOOST_SPINLOCK_IN_THREAD_SANITIZER
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE std::mutex
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR mutex
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_LOAD(v) ((std::atomic<decltype(v)> *)(&v))->load(std::memory_order::memory_order_relaxed)
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_STORE(v, x) ((std::atomic<decltype(v)> *)(&v))->store((x), std::memory_order::memory_order_relaxed)
#else
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE spinlock<bool>
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR spinlock<bool>
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_LOAD(v) (v)
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_STORE(v, x) ((v)=(x))
#endif

BOOST_SPINLOCK_V1_NAMESPACE_BEGIN
namespace lightweight_futures {
  
  template<typename R> class basic_promise;
  template<typename R> class basic_future;

  namespace detail
  {
    template<class promise_type, class future_type> struct lock_guard
    {
      promise_type *_p;
      future_type  *_f;
      lock_guard(const lock_guard &)=delete;
      lock_guard(lock_guard &&)=delete;
      BOOST_SPINLOCK_FUTURE_MSVC_HELP lock_guard(promise_type *p) : _p(nullptr), _f(nullptr)
      {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
        // constexpr fold
        if(!p->_need_locks)
        {
          _p=p;
          if(p->_storage.type==promise_type::value_storage_type::storage_type::pointer)
            _f=p->_storage.pointer_;
          return;
        }
        else
#endif
        for(;;)
        {
          p->_lock.lock();
          if(p->_storage.type==promise_type::value_storage_type::storage_type::pointer)
          {
            if(p->_storage.pointer_->_lock.try_lock())
            {
              _p=p;
              _f=p->_storage.pointer_;
              break;
            }
          }
          else
          {
            _p=p;
            break;
          }
          p->_lock.unlock();
        }
      }
      BOOST_SPINLOCK_FUTURE_MSVC_HELP lock_guard(future_type *f) : _p(nullptr), _f(nullptr)
      {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
        // constexpr fold
        if(!f->_need_locks)
        {
          _p=f->_promise;
          _f=f;
          return;
        }
        else
#endif
        for(;;)
        {
          f->_lock.lock();
          if(f->_promise)
          {
            if(f->_promise->_lock.try_lock())
            {
              _p=f->_promise;
              _f=f;
              break;
            }
          }
          else
          {
            _f=f;
            break;
          }
          f->_lock.unlock();
        }
      }
      BOOST_SPINLOCK_FUTURE_MSVC_HELP ~lock_guard()
      {
        unlock();
      }
      void unlock()
      {
        if(_p)
        {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
          if(_p->_need_locks)
#endif
            _p->_lock.unlock();
          _p=nullptr;
        }
        if(_f)
        {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
          if(_f->_need_locks)
#endif
            _f->_lock.unlock();
          _f=nullptr;
        }
      }
    };
  }

  /*! \class basic_promise
  \brief Implements the state setting side of basic_monad
  \tparam implementation_policy An implementation policy type
  \ingroup future_promise
  
  Read the docs at basic_future for this class.
  */
  template<class implementation_policy> class basic_promise
  {
    friend class basic_future<implementation_policy>;
    friend implementation_policy;
  protected:
    typedef value_storage<implementation_policy> value_storage_type;
    value_storage_type _storage;      // Offset +0
  private:
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    bool _need_locks;                 // Used to inhibit unnecessary atomic use, thus enabling constexpr collapse
#endif
    bool _detached;                   // Offset +5/+9 Future has already been set and promise is now detached
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    union { BOOST_SPINLOCK_FUTURE_MUTEX_TYPE _lock; };  // Delay construction
#else
    BOOST_SPINLOCK_FUTURE_MUTEX_TYPE _lock;  // Offset +6/+10
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  public:
    //! \brief The policy used to implement this basic_future
    typedef implementation_policy policy;
    //! \brief This promise has a value_type
    BOOST_STATIC_CONSTEXPR bool has_value_type = value_storage_type::has_value_type;
    //! \brief This promise has an error_type
    BOOST_STATIC_CONSTEXPR bool has_error_type = value_storage_type::has_error_type;
    //! \brief This promise has an exception_type
    BOOST_STATIC_CONSTEXPR bool has_exception_type = value_storage_type::has_exception_type;
    //! \brief The final implementation type
    typedef typename value_storage_type::implementation_type implementation_type;
    //! \brief The type potentially held by the promise
    typedef typename value_storage_type::value_type value_type;
    //! \brief The error code potentially held by the promise
    typedef typename value_storage_type::error_type error_type;
    //! \brief The exception ptr potentially held by the promise
    typedef typename value_storage_type::exception_type exception_type;

    //! \brief This promise will never throw exceptions during move construction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_constructible = value_storage_type::is_nothrow_move_constructible;
    //! \brief This promise will never throw exceptions during move assignment
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_assignable = value_storage_type::is_nothrow_destructible && value_storage_type::is_nothrow_move_constructible;
    //! \brief This promise will never throw exceptions during destruction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_destructible = value_storage_type::is_nothrow_destructible;

    //! \brief This promise type
    typedef basic_promise promise_type;
    //! \brief The future type associated with this promise type
    typedef basic_future<implementation_policy> future_type;
    //! \brief The future_errc type we use
    typedef typename implementation_policy::future_errc future_errc;
    //! \brief The future_error type we use
    typedef typename implementation_policy::future_error future_error;

    friend struct detail::lock_guard<basic_promise, future_type>;
    static_assert(std::is_move_constructible<value_type>::value || std::is_copy_constructible<value_type>::value, "Type must be move or copy constructible to be used in a lightweight basic_promise");    

    //! \brief EXTENSION: constexpr capable constructor
    BOOST_SPINLOCK_FUTURE_CONSTEXPR basic_promise() noexcept : 
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    _need_locks(false),
#endif
    _detached(false)
    {
    }
//// template<class Allocator> basic_promise(allocator_arg_t, Allocator a); // cannot support
    //! \brief SYNC POINT Move constructor
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR basic_promise(basic_promise &&o) noexcept(is_nothrow_move_constructible) : 
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    _need_locks(o._need_locks),
#endif
    _detached(o._detached)
    {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      if(_need_locks) new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
#endif
      detail::lock_guard<promise_type, future_type> h(&o);
      _storage=std::move(o._storage);
      if(h._f)
        h._f->_promise=this;
    }
    //! \brief SYNC POINT Move assignment. If throws during move, destination promise is left as if default constructed i.e. any previous promise contents are destroyed.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_promise &operator=(basic_promise &&o) noexcept(is_nothrow_move_constructible)
    {
      //! \todo Race exists in basic_promise::operator= between destructor and move constructor
      this->~basic_promise();
      new (this) basic_promise(std::move(o));
      return *this;
    }
    basic_promise(const basic_promise &)=delete;
    basic_promise &operator=(const basic_promise &)=delete;
    //! \brief SYNC POINT Destroys the promise.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP ~basic_promise() noexcept(is_nothrow_destructible)
    {
      if(!_detached)
      {
        detail::lock_guard<promise_type, future_type> h(this);
        if(h._f)
        {
          if(!h._f->is_ready())
            h._f->_broken_promise=true;
          h._f->_promise=nullptr;
        }
        // Destroy myself before locks exit
        _storage.clear();
      }
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      if(_need_locks) _lock.~BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR();
#endif
    }
    
    //! \brief SYNC POINT Swap this promise for another
    BOOST_SPINLOCK_FUTURE_MSVC_HELP void swap(basic_promise &o) noexcept(is_nothrow_move_constructible)
    {
      detail::lock_guard<promise_type, future_type> h1(this), h2(&o);
      _storage.swap(o._storage);
      if(h1._f)
        h1._f->_promise=&o;
      if(h2._f)
        h2._f->_promise=this;
    }
    
    //! \brief SYNC POINT Create a future to be associated with this promise. Can be called exactly once, else throws a `future_already_retrieved`.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP future_type get_future()
    {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      // If no value stored yet, I need locks on from now on
      if(!_need_locks && _storage.type==value_storage_type::storage_type::empty)
      {
        _need_locks=true;
        new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
      }
#endif
      detail::lock_guard<promise_type, future_type> h(this);
      if(h._f || _detached)
        throw future_error(future_errc::future_already_retrieved);
      future_type ret(this);
      h.unlock();
      return ret;
    }
    //! \brief EXTENSION: Does this basic_promise have a future?
    BOOST_SPINLOCK_FUTURE_MSVC_HELP bool has_future() const noexcept
    {
      //detail::lock_guard<value_type> h(this);
      return _storage.type==value_storage_type::storage_type::future || _detached;
    }
    
#define BOOST_SPINLOCK_FUTURE_IMPL(name, function) \
    name \
    { \
      detail::lock_guard<promise_type, future_type> h(this); \
      if(_detached) \
        implementation_policy::_throw_error(monad_errc::already_set); \
      if(h._f) \
      { \
        if(!h._f->empty()) \
          implementation_policy::_throw_error(monad_errc::already_set); \
        h._f->function; \
        h._f->_promise=nullptr; \
        _storage.clear(); \
        _detached=true; \
      } \
      else \
      { \
        if(_storage.type!=value_storage_type::storage_type::empty) \
          implementation_policy::_throw_error(monad_errc::already_set); \
        _storage.function; \
      } \
    }
    /*! \brief SYNC POINT Sets the value to be returned by the associated future, releasing any waits occuring in other threads.
    */
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(const value_type &v), set_value(v))
    /*! \brief SYNC POINT Sets the value to be returned by the associated future, releasing any waits occuring in other threads.
    */
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(value_type &&v), set_value(std::move(v)))
    /*! \brief SYNC POINT EXTENSION: Sets the value by emplacement to be returned by the associated future, releasing any waits occuring in other threads.
    */
    BOOST_SPINLOCK_FUTURE_IMPL(template<class... Args> BOOST_SPINLOCK_FUTURE_MSVC_HELP void emplace_value(Args &&... args), emplace_value(std::forward<Args>(args)...))
    //! \brief SYNC POINT EXTENSION: Set an error code outcome (doesn't allocate)
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_error(error_type e), set_error(std::move(e)))
    //! \brief SYNC POINT Sets an exception outcome
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_exception(exception_type e), set_exception(std::move(e)))
#undef BOOST_SPINLOCK_FUTURE_IMPL
    //! \brief SYNC POINT EXTENSION: Equal to set_exception(make_exception_ptr(forward<E>(e)))
    template<typename E> void set_exception(E &&e)
    {
      set_exception(make_exception_ptr(std::forward<E>(e)));
    }
    
    // Not supported right now
//// void set_value_at_thread_exit(R v);
//// void set_exception_at_thread_exit(R v);

    //! \brief Call F when the future signals, consuming the future. Only one of these may be set.
    // template<class F> typename std::result_of<F(basic_future<value_type>)>::type then(F &&f);

    //! \brief Call F when the future signals, not consuming the future.
    // template<class F> typename std::result_of<F(basic_future<const value_type &>)>::type then(F &&f);
  };

  // TODO: basic_promise<void>, basic_promise<R&> specialisations
  // TODO: basic_future<void>, basic_future<R&> specialisations
  //! \todo basic_promise<R&> and basic_future<R&> specialisations

  /*! \class basic_future
  \brief Lightweight next generation future with N4399 Concurrency TS extensions
  \ingroup future_promise
  */
  template<class implementation_policy> class basic_future : protected basic_monad<implementation_policy>
  {
    friend implementation_policy;
    friend typename implementation_policy::impl;
  public:
    //! \brief The policy used to implement this basic_future
    typedef implementation_policy policy;
    //! \brief The monad type associated with this basic_future
    typedef basic_monad<implementation_policy> monad_type;

    //! \brief This future has a value_type
    BOOST_STATIC_CONSTEXPR bool has_value_type = monad_type::has_value_type;
    //! \brief This future has an error_type
    BOOST_STATIC_CONSTEXPR bool has_error_type = monad_type::has_error_type;
    //! \brief This future has an exception_type
    BOOST_STATIC_CONSTEXPR bool has_exception_type = monad_type::has_exception_type;
    //! \brief The final implementation type
    typedef typename monad_type::implementation_type implementation_type;
    //! \brief The type potentially held by the future
    typedef typename monad_type::value_type value_type;
    //! \brief The error code potentially held by the future
    typedef typename monad_type::error_type error_type;
    //! \brief The exception ptr potentially held by the future
    typedef typename monad_type::exception_type exception_type;
    //! \brief Tag type for an empty future
    struct empty_type { typedef implementation_type parent_type; };
    //! \brief Rebind this future type into a different value_type
    template<typename U> using rebind = typename implementation_policy::template rebind<U>;

    //! \brief This future will never throw exceptions during move construction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_constructible = monad_type::is_nothrow_move_constructible;
    //! \brief This future will never throw exceptions during move assignment
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_assignable = monad_type::is_nothrow_destructible && monad_type::is_nothrow_move_constructible;
    //! \brief This future will never throw exceptions during destruction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_destructible = monad_type::is_nothrow_destructible;

    //! \brief Whether fetching value/error/exception is single shot
    BOOST_STATIC_CONSTEXPR bool is_consuming=implementation_policy::is_consuming;
    //! \brief The promise type matching this future type
    typedef basic_promise<implementation_policy> promise_type;
    //! \brief This future type
    typedef basic_future future_type;
    //! \brief The future_errc type we use
    typedef typename implementation_policy::future_errc future_errc;
    //! \brief The future_error type we use
    typedef typename implementation_policy::future_error future_error;
    
    friend class basic_promise<implementation_policy>;
    friend struct detail::lock_guard<promise_type, future_type>;
    static_assert(std::is_move_constructible<value_type>::value || std::is_copy_constructible<value_type>::value, "Type must be move or copy constructible to be used in a lightweight basic_future");    
  private:
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    bool _need_locks;                 // Used to inhibit unnecessary atomic use, thus enabling constexpr collapse
#endif
    bool _broken_promise;             // Offset +5/+9 Promise was destroyed before setting a value
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    union { BOOST_SPINLOCK_FUTURE_MUTEX_TYPE _lock; };  // Delay construction
#else
    BOOST_SPINLOCK_FUTURE_MUTEX_TYPE _lock;  // Offset +6/+10
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    promise_type *_promise;                  // Offset +8/+16
  protected:
    // Called by basic_promise::get_future(), so currently thread safe
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR basic_future(promise_type *p) : monad_type(std::move(p->_storage)),
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    _need_locks(p->_need_locks),
#endif
    _broken_promise(false), _promise(p)
    {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      if(_need_locks) new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
#endif
      // Clear the promise's storage, as we now have any state
      p->_storage.clear();
      // Do I already have a value? If so, detach, else set the promise to point to us
      if(!empty())
      {
        p->_detached=true;
        _promise=nullptr;
      }
      else
        p->_storage.set_pointer(this);
    }
    typedef detail::lock_guard<promise_type, future_type> lock_guard_type;
    void _check_validity() const
    {
      if(_broken_promise)
        throw future_error(future_errc::broken_promise);
      if(!valid())
        throw future_error(future_errc::no_state);
    }
  public:
    //! \brief EXTENSION: constexpr capable constructor
    BOOST_SPINLOCK_FUTURE_CONSTEXPR basic_future() : 
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    _need_locks(false),
#endif
    _broken_promise(false), _promise(nullptr)
    {
    }
    //! \brief If available for this kind of future, constructs this future type from some other future type
    template<class U, typename=decltype(implementation_policy::_construct(std::declval<U>()))> BOOST_SPINLOCK_FUTURE_CONSTEXPR basic_future(U &&o)
      : basic_future(implementation_policy::_construct(std::forward<U>(o)))
    {
    }
    //! \brief SYNC POINT Move constructor
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR basic_future(basic_future &&o) noexcept(is_nothrow_move_constructible) :
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
    _need_locks(o._need_locks),
#endif
    _broken_promise(o._broken_promise), _promise(nullptr)
    {
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      if(_need_locks) new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
#endif
      detail::lock_guard<promise_type, future_type> h(&o);
      new(this) monad_type(std::move(o));
      if(o._promise)
      {
        _promise=o._promise;
        o._promise=nullptr;
        if(h._p)
          h._p->_storage.pointer_=this;
      }
    }
    //! \brief SYNC POINT Move assignment. If it throws during the move, the future is left as if default constructed.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_future &operator=(basic_future &&o) noexcept(is_nothrow_move_assignable)
    {
      //! \todo Race exists in basic_future::operator= between destructor and move constructor
      this->~basic_future();
      new (this) basic_future(std::move(o));
      return *this;
    }
    basic_future(const basic_future &)=delete;
    basic_future &operator=(const basic_future &)=delete;
    //! \brief SYNC POINT Destructs the future.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP ~basic_future() noexcept(is_nothrow_destructible)
    {
      if(valid())
      {
        detail::lock_guard<promise_type, future_type> h(this);
        if(h._p)
        {
          h._p->_storage.clear();
          h._p->_detached=true;
        }
        // Destroy myself before locks exit
        monad_type::clear();
      }
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      if(_need_locks) _lock.~BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR();
#endif
    }
    
#ifdef DOXYGEN_IS_IN_THE_HOUSE
    //! \brief Same as `true_(tribool(*this))`
    BOOST_SPINLOCK_FUTURE_CONSTEXPR explicit operator bool() const noexcept;
    //! \brief True if monad contains a value_type, false if monad is empty, else unknown.
    BOOST_SPINLOCK_FUTURE_CONSTEXPR operator tribool::tribool() const noexcept;
    //! \brief True if monad is not empty
    BOOST_SPINLOCK_FUTURE_CONSTEXPR bool is_ready() const noexcept;
    //! \brief True if monad is empty
    BOOST_SPINLOCK_FUTURE_CONSTEXPR bool empty() const noexcept;
    //! \brief True if monad contains a value_type
    BOOST_SPINLOCK_FUTURE_CONSTEXPR bool has_value() const noexcept;
    //! \brief True if monad contains an error_type
    BOOST_SPINLOCK_FUTURE_CONSTEXPR bool has_error() const noexcept;
    /*! \brief True if monad contains an exception_type or error_type (any error_type is returned as an exception_ptr by get_exception()).
    This needs to be true for both for compatibility with Boost.Thread's future. If you really want to test only for has exception only,
    pass true as the argument.
    */
    BOOST_SPINLOCK_FUTURE_CONSTEXPR bool has_exception(bool only_exception = false) const noexcept;
#else
    using monad_type::operator bool;
    using monad_type::operator tribool::tribool;
    using monad_type::is_ready;
    using monad_type::empty;
    using monad_type::has_value;
    using monad_type::has_error;
    using monad_type::has_exception;
#endif
    //! \brief True if the state is set or a promise is attached
    bool valid() const noexcept
    {
      return !!_promise || is_ready() || _broken_promise;
    }
    
    //! \brief SYNC POINT Swaps the future with another future
    void swap(basic_future &o) noexcept(is_nothrow_move_constructible)
    {
      detail::lock_guard<promise_type, future_type> h1(this), h2(&o);
      monad_type::swap(o._storage);
#ifdef BOOST_SPINLOCK_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING
      std::swap(_need_locks, o._need_locks);
#endif
      std::swap(_broken_promise, o._broken_promise);
      std::swap(_promise, o._promise);
      if(h1._p)
        h1._p->_storage.pointer_=&o;
      if(h2._p)
        h2._p->_storage.pointer_=this;
    }

    //! \brief If available for this kind of future, converts this simple future into some policy determined shared future type
    BOOST_SPINLOCK_FUTURE_MSVC_HELP auto share() -> decltype(implementation_policy::_share(std::move(*this)))
    {
      _check_validity();
      return implementation_policy::_share(std::move(*this));
    }
    
#ifdef DOXYGEN_IS_IN_THE_HOUSE
    //! \brief SYNC POINT Return any value held by this future, waiting if needed for a state to become available and rethrowing any error or exceptional state.
    value_type get();
    //! \brief If contains a value_type, return that value type, else return the supplied value_type
    value_type &get_or(value_type &v) & noexcept;
    //! \brief If contains a value_type, return that value type, else return the supplied value_type
    const value_type &get_or(const value_type &v) const & noexcept;
    //! \brief If contains a value_type, return that value type, else return the supplied value_type
    value_type &&get_or(value_type &&v) && noexcept;
    //! \brief If contains a value_type, return the supplied value_type else return the contained value_type
    value_type &get_and(value_type &v) & noexcept;
    //! \brief If contains a value_type, return the supplied value_type else return the contained value_type
    const value_type &get_and(const value_type &v) const & noexcept;
    //! \brief If contains a value_type, return the supplied value_type else return the contained value_type
    value_type &&get_and(value_type &&v) && noexcept;
    //! \brief SYNC POINT Return any error held by this future, waiting if needed for a state to become available.
    error_type get_error() const;
    //! \brief If contains an error_type, returns that error_type else returns the error_type supplied
    error_type get_error_or(error_type e) const noexcept;
    //! \brief If contains an error_type, return the supplied error_type else return the contained error_type
    error_type get_error_and(error_type e) const noexcept;
    //! \brief SYNC POINT Return any exception held by this future, waiting if needed for a state to become available.
    exception_type get_exception() const;
    //! \brief If contains an exception_type, returns that exception_type else returns the exception_type supplied
    exception_type get_exception_or(exception_type e) const noexcept;
    //! \brief If contains an exception_type, return the supplied exception_type else return the contained exception_type
    exception_type get_exception_and(exception_type e) const noexcept;
#else
    using monad_type::get;
    using monad_type::get_or;
    using monad_type::get_and;
    using monad_type::get_error;
    using monad_type::get_error_or;
    using monad_type::get_error_and;
    using monad_type::get_exception;
    using monad_type::get_exception_or;
    using monad_type::get_exception_and;
#endif
    //! \brief SYNC POINT Compatibility with Boost.Thread
    exception_type get_exception_ptr() { return this->get_exception(); }
    
    //! \brief SYNC POINT Wait for the future to become ready
    void wait() const
    {
      if(is_ready())
        return;
      detail::lock_guard<promise_type, future_type> h(this);
      _check_validity();
      // TODO Actually sleep
      while(!monad_type::is_ready())
      {
        h.unlock();
        std::this_thread::yield();
        h=detail::lock_guard<promise_type, future_type>(this);
      }
    }
//// template<class R, class P> future_status wait_for(const std::chrono::duration<R, P> &rel_time) const;  // TODO
//// template<class C, class D> future_status wait_until(const std::chrono::time_point<C, D> &abs_time) const;  // TODO
    
    // TODO Where F would return a basic_future<basic_future<...>>, we unwrap to a single basic_future<R>
//// template<class F> typename std::result_of<F(basic_future)>::type then(F &&f);
  };

  /*! \class shared_basic_future_ptr
  \brief A shared pointer to a basic future. Lets you wrap up basic_future with STL shared_future semantics. Quite
  literally a shared_ptr and a thin API thunk to the basic_future.
  \ingroup future_promise
  */
  template<class _future_type> class shared_basic_future_ptr
  {
  public:
    //! The type of future this references
    typedef _future_type base_future_type;
  private:
    std::shared_ptr<base_future_type> _future;
    base_future_type *_check() const
    {
      if(!_future)
      {
        if(!base_future_type::policy::_throw_error(monad_errc::no_state))
          abort();
      }
      return _future.get();
    }
  public:
    //! Default constructor
    BOOST_SPINLOCK_FUTURE_CONSTEXPR shared_basic_future_ptr() : _future(std::make_shared<base_future_type>()) { }
    //! Forwarding constructor
    template<class U> BOOST_SPINLOCK_FUTURE_CONSTEXPR shared_basic_future_ptr(U &&o) : _future(std::make_shared<base_future_type>(std::forward<U>(o))) { }
    //! Forwards to operator bool
    explicit operator bool() const { return _check()->operator bool(); }
    //! Forwards to operator tribool
    explicit operator tribool::tribool() const { return _check()->operator tribool::tribool(); }
#define BOOST_SPINLOCK_FUTURE_IMPL(name) \
    template<class... Args> BOOST_SPINLOCK_FUTURE_MSVC_HELP auto name(Args &&... args) const noexcept(noexcept(_future->name(std::forward<Args>(args)...))) -> decltype(_future->name(std::forward<Args>(args)...)) \
    { \
      return _check()->name(std::forward<Args>(args)...); \
    }
    //! Forwards to is_ready()
    BOOST_SPINLOCK_FUTURE_IMPL(is_ready)
    //! Forwards to empty()
    BOOST_SPINLOCK_FUTURE_IMPL(empty)
    //! Forwards to has_value()
    BOOST_SPINLOCK_FUTURE_IMPL(has_value)
    //! Forwards to has_error()
    BOOST_SPINLOCK_FUTURE_IMPL(has_error)
    //! Forwards to has_exception()
    BOOST_SPINLOCK_FUTURE_IMPL(has_exception)
    //! Forwards to valid()
    BOOST_SPINLOCK_FUTURE_IMPL(valid)

    //! Forwards to get()
    BOOST_SPINLOCK_FUTURE_IMPL(get)
    //! Forwards to get_or()
    BOOST_SPINLOCK_FUTURE_IMPL(get_or)
    //! Forwards to get_and()
    BOOST_SPINLOCK_FUTURE_IMPL(get_and)
    //! Forwards to get_error()
    BOOST_SPINLOCK_FUTURE_IMPL(get_error)
    //! Forwards to get_error_or()
    BOOST_SPINLOCK_FUTURE_IMPL(get_error_or)
    //! Forwards to get_error_and()
    BOOST_SPINLOCK_FUTURE_IMPL(get_error_and)
    //! Forwards to get_exception()
    BOOST_SPINLOCK_FUTURE_IMPL(get_exception)
    //! Forwards to get_exception_or()
    BOOST_SPINLOCK_FUTURE_IMPL(get_exception_or)
    //! Forwards to get_exception_and()
    BOOST_SPINLOCK_FUTURE_IMPL(get_exception_and)
    //! Forwards to get_exception_ptr()
    BOOST_SPINLOCK_FUTURE_IMPL(get_exception_ptr)

    //! Forwards to wait()
    BOOST_SPINLOCK_FUTURE_IMPL(wait)
#undef BOOST_SPINLOCK_FUTURE_IMPL
  };

  // TODO
  // template<class InputIterator> ? when_all(InputIterator first, InputIterator last);
  // template<class... Futures> ? when_all(Futures &&... futures);
  // template<class Sequence> struct when_any_result;
  // template<class InputIterator> ? when_any(InputIterator first, InputIterator last);
  // template<class... Futures> ? when_any(Futures &&... futures);

  // TODO packaged_task

#define BOOST_SPINLOCK_FUTURE_NAME_POSTFIX 
#define BOOST_SPINLOCK_FUTURE_POLICY_ERROR_TYPE std::error_code
#define BOOST_SPINLOCK_FUTURE_POLICY_EXCEPTION_TYPE std::exception_ptr
#include "detail/future_policy.ipp"
#define BOOST_SPINLOCK_FUTURE_NAME_POSTFIX _result
#define BOOST_SPINLOCK_FUTURE_POLICY_ERROR_TYPE std::error_code
#include "detail/future_policy.ipp"
#define BOOST_SPINLOCK_FUTURE_NAME_POSTFIX _option
#include "detail/future_policy.ipp"

}
BOOST_SPINLOCK_V1_NAMESPACE_END

namespace std
{
  template<typename R> inline void swap(BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_promise<R> &a, BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_promise<R> &b)
  {
    a.swap(b);
  }
  template<typename R> inline void swap(BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_future<R> &a, BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_future<R> &b)
  {
    a.swap(b);
  }
}

#endif
