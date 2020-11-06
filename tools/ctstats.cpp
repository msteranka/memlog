#include "event.hpp"
#include <cassert>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>

bool isLog(Event *e) {
    return e->_action == E_MALLOC || 
        e->_action == E_FREE ||
        e->_action == E_READ ||
        e->_action == E_WRITE;
}

int main() {
    struct stat buf;
    size_t numEvents;
    bool hasMultipleThreads = false;
    unsigned int initThread;
    size_t stats[4];
    Event e;
    int fd = open("../src/memlog.bin", O_RDONLY);
    assert(fd != -1);
    memset(stats, 0, sizeof(stats));
    fstat(fd, &buf);
    numEvents = buf.st_size / sizeof(Event);
    read(fd, &e, sizeof(e));
    assert(isLog(&e));
    initThread = e._threadId;

    for (int i = 1; i < numEvents; i++) { // TODO: reads events in bulk instead of one at a time
        read(fd, &e, sizeof(e));
        assert(isLog(&e));
        stats[e._action]++;
        if (e._threadId != initThread) {
            hasMultipleThreads = true;
        }
    }

    close(fd);
    printf("numMallocs: %lu\n", stats[0]);
    printf("numFrees: %lu\n", stats[1]);
    printf("numReads: %lu\n", stats[2]);
    printf("numWrites: %lu\n", stats[3]);
    if (hasMultipleThreads) {
        printf("Multithreaded: Yes\n");
    } else {
        printf("Multithreaded: No\n");
    }
    return 0;
}
