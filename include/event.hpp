enum EventTypes {
    E_MALLOC,
    E_FREE,
    E_READ,
    E_WRITE
};

struct Event
{
    Event() { }

    Event(char action, void *addr, unsigned int size, unsigned int threadId) : 
        _action(action), 
        _addr(addr),
        _size(size),
        _threadId(threadId) { }

    char _action;
    void *_addr;
    unsigned int _size, _threadId;
};
