/*******************************************************************************
 * c7a/net/lowlevel/select-dispatcher.hpp
 *
 * Asynchronous callback wrapper around select()
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_NET_LOWLEVEL_SELECT_DISPATCHER_HEADER
#define C7A_NET_LOWLEVEL_SELECT_DISPATCHER_HEADER

#include <c7a/net/lowlevel/socket.hpp>
#include <c7a/net/lowlevel/select.hpp>
#include <c7a/net/lowlevel/net_exception.hpp>

#include <deque>

namespace c7a {
namespace net {
namespace lowlevel {

//! \addtogroup netsock Low Level Socket API
//! \{

/**
 * SelectDispatcher is a higher level wrapper for select(). One can register
 * Socket objects for readability and writability checks, buffered reads and
 * writes with completion callbacks, and also timer functions.
 */
template <typename _Cookie>
class SelectDispatcher : protected Select
{
    static const bool debug = true;

public:
    //! cookie data structure for callback
    typedef _Cookie Cookie;

    //! cookie type for file descriptor readiness callbacks
    typedef std::function<bool (Cookie)> Callback;

    //! Register a buffered read callback and a default exception callback.
    void AddRead(int fd, const Cookie& c,
                 const Callback& read_cb,
                 const Callback& except_cb = DefaultExceptionCallback)
    {
        Select::SetRead(fd);
        Select::SetException(fd);
        watch_.emplace_back(fd, c, read_cb, nullptr, except_cb);
    }

    //! Register a buffered write callback and a default exception callback.
    void AddWrite(int fd, const Cookie& c,
                  const Callback& write_cb,
                  const Callback& except_cb = DefaultExceptionCallback)
    {
        Select::SetWrite(fd);
        Select::SetException(fd);
        watch_.emplace_back(fd, c, nullptr, write_cb, except_cb);
    }

    //! Register a buffered write callback and a default exception callback.
    void AddReadWrite(int fd, const Cookie& c,
                      const Callback& read_cb, const Callback& write_cb,
                      const Callback& except_cb = DefaultExceptionCallback)
    {
        Select::SetRead(fd);
        Select::SetWrite(fd);
        Select::SetException(fd);
        watch_.emplace_back(fd, c, read_cb, write_cb, except_cb);
    }

    void Dispatch(double timeout)
    {
        // copy select fdset
        Select fdset = *this;

        if (debug)
        {
            std::ostringstream oss;
            for (Watch& w : watch_) {
                oss << w.fd << " ";
            }
            oss << "| ";
            for (int i = 0; i < Select::max_fd_ + 1; ++i) {
                if (Select::InRead(i))
                    oss << "r" << i << " ";
                if (Select::InWrite(i))
                    oss << "w" << i << " ";
                if (Select::InException(i))
                    oss << "e" << i << " ";
            }
            LOG << "Performing select() on " << oss.str();
        }

        int r = fdset.select_timeout(timeout);

        if (r < 0) {
            throw lowlevel::NetException("OpenConnections() select() failed!", errno);
        }
        if (r == 0) return;

        // save _current_ size, as it may change.
        size_t watch_size = watch_.size();

        for (size_t i = 0; i != watch_size; ++i)
        {
            Watch& w = watch_[i];

            if (w.fd < 0) continue;

            if (fdset.InRead(w.fd))
            {
                if (w.read_cb) {
                    // have to clear the read flag since the callback may add a
                    // new (other) callback for the same fd.
                    Select::ClearRead(w.fd);
                    Select::ClearException(w.fd);

                    if (!w.read_cb(w.cookie)) {
                        // callback returned false: remove fd from set
                        w.fd = -1;
                    }
                    else {
                        Select::SetRead(w.fd);
                        Select::SetException(w.fd);
                    }
                }
                else {
                    LOG << "SelectDispatcher: got read event for fd "
                        << w.fd << " without a read handler.";

                    Select::ClearRead(w.fd);
                }
            }

            if (w.fd < 0) continue;

            if (fdset.InWrite(w.fd))
            {
                if (w.write_cb) {
                    // have to clear the read flag since the callback may add a
                    // new (other) callback for the same fd.
                    Select::ClearWrite(w.fd);
                    Select::ClearException(w.fd);

                    if (!w.write_cb(w.cookie)) {
                        // callback returned false: remove fd from set
                        w.fd = -1;
                    }
                    else {
                        Select::SetWrite(w.fd);
                        Select::SetException(w.fd);
                    }
                }
                else {
                    LOG << "SelectDispatcher: got write event for fd "
                        << w.fd << " without a write handler.";

                    Select::ClearWrite(w.fd);
                }
            }

            if (w.fd < 0) continue;

            if (fdset.InException(w.fd))
            {
                if (w.except_cb) {
                    Select::ClearException(w.fd);

                    if (!w.except_cb(w.cookie)) {
                        // callback returned false: remove fd from set
                        w.fd = -1;
                    }
                    else {
                        Select::SetException(w.fd);
                    }
                }
                else {
                    LOG << "SelectDispatcher: got exception event for fd "
                        << w.fd << " without an exception handler.";

                    Select::ClearException(w.fd);
                }
            }
        }
    }

private:
    //! struct to entries per watched file descriptor
    struct Watch
    {
        int      fd;
        Cookie   cookie;
        Callback read_cb, write_cb, except_cb;

        Watch(int _fd, const Cookie& _cookie,
              const Callback& _read_cb, const Callback& _write_cb,
              const Callback& _except_cb)
            : fd(_fd),
              cookie(_cookie),
              read_cb(_read_cb),
              write_cb(_write_cb),
              except_cb(_except_cb)
        { }
    };

    //! handlers for all registered file descriptors, we have to keep them
    //! address local.
    std::deque<Watch> watch_;

    //! Default exception handler
    static bool DefaultExceptionCallback(const Cookie& /* c */)
    {
        // exception on listen socket ?
        throw lowlevel::NetException("SelectDispatcher() exception on socket!",
                           errno);
    }
};

//! \}

} // namespace lowlevel
} // namespace net
} // namespace c7a

#endif // !C7A_NET_LOWLEVEL_SELECT_DISPATCHER_HEADER

/******************************************************************************/
