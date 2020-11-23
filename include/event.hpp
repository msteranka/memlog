#if !defined(__EVENT_HPP)
# define __EVENT_HPP

enum EventTypes {
    E_MALLOC,
    E_FREE,
    E_READ,
    E_WRITE
};

struct Event {
    Event() { }

    Event(char action, void *addr, unsigned int size, unsigned int threadId, unsigned int timestamp) : 
        _action(action), 
        _addr(addr),
        _size(size),
        _threadId(threadId),
        _timestamp(timestamp) { }

    char _action;
    void *_addr;
    unsigned int _size, _threadId, _timestamp; // TODO: no point in storing timestamps in output file
};

std::ostream& operator<<(std::ostream& os, Event& e) {
    os << "{\"type\":" << (int) e._action << "," <<
           "\"addr\":" << (size_t) e._addr << "," <<
           "\"size\":" << e._size << "," <<
           "\"tid\":" << e._threadId << "," <<
           "\"time\":" << e._timestamp <<
           "}";
    return os;
}

#endif // __EVENT_HPP
