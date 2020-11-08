#include <list>
#include <iostream>
#include <cassert>
#include "event.hpp"
#include "parse.hpp"

int main() {
    std::vector<Event> *events = parseEvents("../src/memlog.bin");
    unsigned int cur, next;

    for (int i = 0; i < events->size() - 1; i++) {
        cur = events->at(i)._timestamp;
        next = events->at(i + 1)._timestamp;
        // printf("%u\n", events->at(i)._timestamp);
        if (cur > next) {
            printf("FAILURE: %u > %u, i = %d\n", cur, next, i);
            return -1;
        }
    }

    std::cout << "Start: " << events->front()._timestamp << ", End: " << events->back()._timestamp << std::endl;
    delete events;
    return 0;
}
