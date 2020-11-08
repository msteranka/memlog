#include <cstdio>
#include <sys/mman.h>
#include <cassert>
#include "event.hpp"
#include "parse.hpp"

int main() {
    Event *events;
    size_t length;
    // Fetch events from ../src/memlog.bin, store a pointer to the events
    // and the number of events
    // NOTE: Data from this array can only be read from, but that can be 
    // changed if necessary
    //
    parseEventsAsArray("../src/memlog.bin", &events, &length);
    for (int i = 0; i < length; i++) {
        Event curEvent = events[i];
        char a = curEvent._action;
        switch(a) {
            case E_MALLOC:
                printf("E_MALLOC: ");
                break;
            case E_FREE:
                printf("E_FREE: ");
                break;
            case E_READ:
                printf("E_READ: ");
                break;
            case E_WRITE:
                printf("E_WRITE: ");
                break;
            default: // Not a valid event
                fprintf(stderr, "ERROR: Invalid event\n");
                return -1;
        }
        printf("addr = %p, size = %u, tid = %u, time = %u\n",
                curEvent._addr,
                curEvent._size,
                curEvent._threadId,
                curEvent._timestamp);
    }
    munmap(events, length * sizeof(Event));
    return 0;
}
