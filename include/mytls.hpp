#if !defined(__MY_TLS_HPP)
# define __MY_TLS_HPP

#include <list>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "backtrace.hpp"

struct MyTLS {
    MyTLS() {
        int fd = open("/dev/urandom", O_RDONLY);
        assert(fd != -1);
        ssize_t err = read(fd, &_seed, sizeof(_seed));
        assert(err != -1);
        close(fd);
    }

    std::list<Event*> _eventsList;
    size_t _cachedSize;
    Backtrace _cachedBacktrace;
    // It's very important that _geom is signed, since when decrementing
    // it, it's possible for its value to become negative
    //
    ssize_t _geom;
    unsigned int _seed;
};

#endif // __MY_TLS_HPP
