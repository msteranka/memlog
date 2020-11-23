#if !defined(__EVENT_HPP)
# define __EVENT_HPP

#include "backtrace.hpp"

enum EventTypes {
    E_MALLOC,
    E_FREE,
    E_READ,
    E_WRITE
};

class Event {
public:
    Event() { }

    Event(char action, void *addr, unsigned int size, unsigned int threadId, unsigned int timestamp) : 
        _action(action), 
        _addr(addr),
        _size(size),
        _threadId(threadId),
        _timestamp(timestamp) { }

    char _action;
    void *_addr;
    unsigned int _size, _threadId, _timestamp;
};

class AllocationEvent : public Event {
public:
    AllocationEvent(char action, void *addr, unsigned int size, unsigned int threadId, unsigned int timestamp, Backtrace& backtrace) : 
        Event(action, addr, size, threadId, timestamp),
        _backtrace(backtrace) { }

    Backtrace _backtrace;
};

class AccessEvent : public Event {
public:
    AccessEvent(char action, void *addr, unsigned int size, unsigned int threadId, unsigned int timestamp) : 
        Event(action, addr, size, threadId, timestamp) { }
};

std::ostream& operator<<(std::ostream& os, Event& e) {
    os << "{\"type\":" << (int) e._action << "," <<
           "\"addr\":" << (size_t) e._addr << "," <<
           "\"size\":" << e._size << "," <<
           "\"tid\":" << e._threadId << "," <<
           "\"time\":" << e._timestamp; // TODO: no point in storing timestamps in output file
    if (e._action == E_MALLOC || e._action == E_FREE) {
        os << ",\"backtrace\":" << ((AllocationEvent&) e)._backtrace;
    }
    os << "}";
    return os;
}

#endif // __EVENT_HPP
