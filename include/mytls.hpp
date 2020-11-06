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
        ssize_t err = read(fd, &seed, sizeof(seed));
        assert(err != -1);
        close(fd);
    }

    std::list<Event*> _eventsList;
    size_t _cachedSize;
    unsigned int seed;
};
