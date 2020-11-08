#if !defined(__MY_TLS_HPP)
# define __MY_TLS_HPP

#include <list>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

struct MyTLS {
    MyTLS() {
        int fd = open("/dev/urandom", O_RDONLY);
        assert(fd != -1);
        ssize_t err = read(fd, &_seed, sizeof(_seed));
        assert(err != -1);
        close(fd);
    }

    std::list<Event*> _eventsList;
    size_t _cachedSize, _geom;
    unsigned int _seed;
};

#endif // __MY_TLS_HPP
