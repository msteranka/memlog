#if !defined(__PARSE_HPP)
# define __PARSE_HPP

#include <vector>
#include <string>
#include <cassert>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "event.hpp"

inline bool isLog(Event *e) {
    return e->_action == E_MALLOC || 
        e->_action == E_FREE ||
        e->_action == E_READ ||
        e->_action == E_WRITE;
}

std::vector<Event> *parseEvents(std::string pathname) {
    std::vector<Event> *events;
    int fd;
    struct stat buf;
    unsigned long numEvents;
    Event curEvent;

    fd = open(pathname.c_str(), O_RDONLY);
    assert(fd != -1);
    fstat(fd, &buf);
    numEvents = buf.st_size / sizeof(Event);
    events = new std::vector<Event>(numEvents);

    for (unsigned long i = 0; i < numEvents; i++) {
        read(fd, &curEvent, sizeof(curEvent));
        assert(isLog(&curEvent));
        events->push_back(curEvent);
    }

    close(fd);
    return events;
}

void parseEventsAsArray(std::string pathname, Event **ptr, size_t *length) {
    int fd;
    struct stat statbuf;

    fd = open(pathname.c_str(), O_RDONLY);
    fstat(fd, &statbuf); // Fetch file size
    *ptr = (Event *) mmap(nullptr, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // mmap file into memory
    *length = statbuf.st_size / sizeof(Event);
    close(fd);
}

#endif // __PARSE_HPP
