#include "pin.H"
#include <iostream>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <list>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include "event.hpp"
#include "mytls.hpp"
#include <cmath>

#ifdef TARGET_MAC
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#endif // TARGET_MAC

using namespace std;

static int fd;
static PIN_LOCK fdLock;
static TLS_KEY tls_key = INVALID_TLS_KEY;
static const double P = 0.01; // TODO: adjust P
static AFUNPTR mallocUsableSize;

inline size_t GetNext(unsigned int *seedp, double p) {
    int r = rand_r(seedp); // TODO: use better RNG
    float u = (float) r / (float) RAND_MAX;
    size_t geom = (size_t) ceil(log(u) / log(1.0 - p));
    return geom;
}

void WriteEvents(int fd, PIN_LOCK *fdLock, list<Event*>* eventsList) {
    static const size_t BUF_LEN = 524288; // TODO: adjust BUF_LEN
    Event *buf = new Event[BUF_LEN];
    assert(buf != nullptr);
    int nextIndex = 0;
    ssize_t err;

    while (!eventsList->empty()) {
        auto it = eventsList->begin();
        buf[nextIndex] = **it;
        nextIndex = (nextIndex + 1) % BUF_LEN;
        if (nextIndex == 0) {
            PIN_GetLock(fdLock, -1);
            err = write(fd, buf, sizeof(Event) * BUF_LEN);
            PIN_ReleaseLock(fdLock);
            assert(err != -1);
        }
        delete *it;
        eventsList->pop_front();
    }

    PIN_GetLock(fdLock, -1);
    err = write(fd, buf, sizeof(Event) * nextIndex);
    PIN_ReleaseLock(fdLock);
    assert(err != -1);
    delete[] buf;
}

VOID ThreadStart(THREADID threadId, CONTEXT *ctxt, INT32 flags, VOID *v) {
    MyTLS *tls = new MyTLS;
    BOOL success = PIN_SetThreadData(tls_key, tls, threadId);
    assert(success);
}

VOID ThreadFini(THREADID threadId, const CONTEXT *ctxt, INT32 code, VOID *v) { 
    // TODO: parse logs
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    WriteEvents(fd, &fdLock, &(tls->_eventsList)); // TODO: no ordering of events between threads
    assert(tls->_eventsList.empty());
    delete tls;
}

VOID MallocBefore(THREADID threadId, const CONTEXT* ctxt, ADDRINT size) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    tls->_cachedSize = size;
}

VOID MallocAfter(THREADID threadId, ADDRINT retVal) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    tls->_eventsList.push_back(new Event(E_MALLOC, (void *) retVal, tls->_cachedSize, threadId));
}

VOID FreeHook(THREADID threadId, const CONTEXT* ctxt, ADDRINT ptr) {
    size_t size = 0;
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    // If mallocUsableSize is valid, then call malloc_usable_size within application
    // to fetch size of object
    // NOTE: malloc_usable_size does not return the same value given to malloc, but
    // rather the size of the object as recognized by the allocator
    //
    if (mallocUsableSize) {
        PIN_CallApplicationFunction(ctxt, threadId, CALLINGSTD_DEFAULT,
                                    mallocUsableSize, nullptr,
                                    PIN_PARG(size_t), &size,
                                    PIN_PARG(void *), (void *) ptr,
                                    PIN_PARG_END());
    }
    tls->_eventsList.push_back(new Event(E_FREE, (void *) ptr, size, threadId));
}

VOID ReadsMem(THREADID threadId, ADDRINT addrRead, UINT32 readSize) {
    static const size_t MAX_SIZE = 67108864; // TODO: write out events somewhere else?
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    if (tls->_geom > 0) {
        tls->_geom--;
        return;
    }
    tls->_eventsList.push_back(new Event(E_READ, (void *) addrRead, readSize, threadId));
    if (tls->_eventsList.size() >= MAX_SIZE) {
        WriteEvents(fd, &fdLock, &(tls->_eventsList));
    }
    tls->_geom = GetNext(&(tls->_seed), P);
}

VOID WritesMem(THREADID threadId, ADDRINT addrWritten, UINT32 writeSize) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    if (tls->_geom > 0) {
        tls->_geom--;
        return;
    }
    tls->_eventsList.push_back(new Event(E_WRITE, (void *) addrWritten, writeSize, threadId));
    tls->_geom = GetNext(&(tls->_seed), P);
}

VOID Instruction(INS ins, VOID* v) {
	if (INS_IsMemoryRead(ins) && !INS_IsStackRead(ins)) {
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem,
					   IARG_THREAD_ID,
					   IARG_MEMORYREAD_EA,
					   IARG_MEMORYREAD_SIZE,
					   IARG_END);
	}

	if (INS_IsMemoryWrite(ins) && !INS_IsStackWrite(ins)) {
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) WritesMem,
					   IARG_THREAD_ID,
					   IARG_MEMORYWRITE_EA,
					   IARG_MEMORYWRITE_SIZE,
					   IARG_END);
	}
}

VOID Image(IMG img, VOID* v) {
	RTN rtn;

	rtn = RTN_FindByName(img, MALLOC);
	if (RTN_Valid(rtn)) {
		RTN_Open(rtn);
		RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) MallocBefore,
					   IARG_THREAD_ID,
					   IARG_CONST_CONTEXT,
					   IARG_FUNCARG_ENTRYPOINT_VALUE,
					   0, IARG_END);
		RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) MallocAfter,
					   IARG_THREAD_ID,
					   IARG_FUNCRET_EXITPOINT_VALUE,
					   IARG_END);
		RTN_Close(rtn);
	}

	rtn = RTN_FindByName(img, FREE);
	if (RTN_Valid(rtn)) {
		RTN_Open(rtn);
		RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) FreeHook,
					   IARG_THREAD_ID,
					   IARG_CONST_CONTEXT,
					   IARG_FUNCARG_ENTRYPOINT_VALUE,
					   0, IARG_END);
		RTN_Close(rtn);
	}

    rtn = RTN_FindByName(img, "malloc_usable_size");
    if (RTN_Valid(rtn)) {
        mallocUsableSize = RTN_Funptr(rtn);
    } else {
        mallocUsableSize = nullptr;
    }
}

VOID Fini(INT32 code, VOID* v) {
    // TODO: order logs here
    close(fd);
}

INT32 Usage() {
	std::cerr << "Log allocations, deallocations, reads, and writes" << std::endl;
	std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
	return EXIT_FAILURE;
}

int main(int argc, char* argv[]) {
    static const char *path = (const char *) "memlog.bin";
    // static const char *path = (const char *) "/nfs/cm/scratch1/emery/msteranka/memlog.bin";
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
        S_IRUSR | S_IWUSR |
        S_IRGRP | S_IWGRP |
        S_IROTH
    );
    assert(fd != -1);

	PIN_InitSymbols();
	if (PIN_Init(argc, argv)) {
		return Usage();
	}

    PIN_InitLock(&fdLock);
    tls_key = PIN_CreateThreadDataKey(NULL);
    if (tls_key == INVALID_TLS_KEY) {
        cerr << "Number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << endl;
        PIN_ExitProcess(1);
    }

	IMG_AddInstrumentFunction(Image, 0);
	INS_AddInstrumentFunction(Instruction, 0);
	PIN_AddThreadStartFunction(ThreadStart, 0);
	PIN_AddThreadFiniFunction(ThreadFini, 0);
	PIN_AddFiniFunction(Fini, 0);
	PIN_StartProgram();
}
