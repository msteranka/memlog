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
static const int THRESHOLD = RAND_MAX / 10;

void WriteEvents(int fd, PIN_LOCK *fdLock, list<Event*>* eventsList) {
    const static size_t BUF_LEN = 65536; // TODO: adjust BUF_LEN
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
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    tls->_eventsList.push_back(new Event(E_FREE, (void *) ptr, 0, threadId)); // TODO: get size here
}

VOID ReadsMem(THREADID threadId, ADDRINT addrRead, UINT32 readSize) {
    const static size_t MAX_SIZE = 67108864; // TODO: write out events somewhere else?
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    if (rand_r(&(tls->seed)) < THRESHOLD) { // TODO: use better RNG
        return; 
    }
    tls->_eventsList.push_back(new Event(E_READ, (void *) addrRead, readSize, threadId));
    if (tls->_eventsList.size() >= MAX_SIZE) {
        WriteEvents(fd, &fdLock, &(tls->_eventsList));
    }
}

VOID WritesMem(THREADID threadId, ADDRINT addrWritten, UINT32 writeSize) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    if (rand_r(&(tls->seed)) < THRESHOLD) { // TODO: use better RNG
        return; 
    }
    tls->_eventsList.push_back(new Event(E_WRITE, (void *) addrWritten, writeSize, threadId));
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
