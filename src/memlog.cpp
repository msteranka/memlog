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
#include <algorithm>
#include <stdatomic.h>
#include <sys/mman.h>
#include <fstream>

#if defined(_MSC_VER)
# define LIKELY(x) (x)
# define UNLIKELY(x) (x)
#else
# define LIKELY(x) __builtin_expect(!!(x), 1)
# define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif // _MSC_VER

#if defined(TARGET_MAC)
# define MALLOC "_malloc"
# define FREE "_free"
#else
# define MALLOC "malloc"
# define FREE "free"
#endif // TARGET_MAC

using namespace std;

static const char *const outputPath = "memlog.json";
// static const char *const outputPath = "/nfs/cm/scratch1/emery/msteranka/memlog.bin";
static ofstream traceFile;
static KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", outputPath, "specify profiling file name");
static PIN_LOCK outputLock;
static TLS_KEY tls_key = INVALID_TLS_KEY;
static const double P = 0.001; // ADJUSTABLE
static AFUNPTR mallocUsableSize;
static unsigned int curTime;

inline size_t GetNext(unsigned int *seedp, double p) {
    int r = rand_r(seedp); // TODO: use better RNG
    float u = (float) r / (float) RAND_MAX;
    size_t geom = (size_t) ceil(log(u) / log(1.0 - p));
    return geom;
}

void WriteEvents(PIN_LOCK *outputLock, list<Event*>* eventsList) {
    while (!eventsList->empty()) {
        auto it = eventsList->begin();
        PIN_GetLock(outputLock, -1);
        if (eventsList->size() > 1) {
            traceFile << **it << ",";
        } else {
            traceFile << **it;
        }
        PIN_ReleaseLock(outputLock);
        delete *it;
        eventsList->pop_front();
    }
}

VOID ThreadStart(THREADID threadId, CONTEXT *ctxt, INT32 flags, VOID *v) {
    MyTLS *tls = new MyTLS;
    assert(PIN_SetThreadData(tls_key, tls, threadId));
    tls->_geom = (ssize_t) GetNext(&(tls->_seed), P);
}

VOID ThreadFini(THREADID threadId, const CONTEXT *ctxt, INT32 code, VOID *v) { 
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    WriteEvents(&outputLock, &(tls->_eventsList));
    delete tls;
}

VOID MallocBefore(THREADID threadId, const CONTEXT* ctxt, ADDRINT size) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    tls->_cachedSize = size;
    tls->_cachedBacktrace.SetTrace(ctxt);
}

VOID MallocAfter(THREADID threadId, ADDRINT retVal) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    // No need for atomicity with timestamps. We just need some loose ordering
    // of events.
    //
    tls->_eventsList.push_back(new Event(E_MALLOC, (void *) retVal, tls->_cachedSize, threadId, curTime, tls->_cachedBacktrace));
    curTime++;
}

VOID FreeHook(THREADID threadId, const CONTEXT* ctxt, ADDRINT ptr) {
    if ((void *) ptr == nullptr) {
        // We don't need to track frees to null pointers.
        return;
    }

    size_t size = 0;
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    Backtrace b;
    b.SetTrace(ctxt);
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
    tls->_eventsList.push_back(new Event(E_FREE, (void *) ptr, size, threadId, curTime, b));
}

VOID ReadsMem(THREADID threadId, ADDRINT addrRead, UINT32 readSize) {
    // static const size_t MAX_SIZE = 1048576; // ADJUSTABLE
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    if (LIKELY(tls->_geom > 0)) {
        tls->_geom -= readSize;
        return;
    }
    // tls->_eventsList.push_back(new Event(E_READ, (void *) addrRead, readSize, threadId, curTime));
    // if (UNLIKELY(tls->_eventsList.size() >= MAX_SIZE)) {
    //     WriteEvents(fd, &outputLock, &(tls->_eventsList));
    // }
    tls->_geom = (ssize_t) GetNext(&(tls->_seed), P);
}

VOID WritesMem(THREADID threadId, ADDRINT addrWritten, UINT32 writeSize) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(tls_key, threadId));
    if (LIKELY(tls->_geom > 0)) {
        tls->_geom -= writeSize;
        return;
    }
    // tls->_eventsList.push_back(new Event(E_WRITE, (void *) addrWritten, writeSize, threadId, curTime));
    tls->_geom = (ssize_t) GetNext(&(tls->_seed), P);
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

int eventCompare(const void *ptr1, const void *ptr2) {
    Event *e1 = (Event *) ptr1, *e2 = (Event *) ptr2;

    // Order by timestamps foremost
    //
    if (e1->_timestamp < e2->_timestamp) {
        return -1;
    }
    if (e1->_timestamp > e2->_timestamp) {
        return 1;
    }

    // If timestamps are the same, then make sure that
    // malloc events are put first
    //
    if (e1->_action == E_MALLOC) {
        return -1;
    }
    if (e2->_action == E_MALLOC) {
        return 1;
    }

    // Make sure that free events are put last
    //
    if (e1->_action == E_FREE) {
        return 1;
    }
    if (e2->_action == E_FREE) {
        return -1;
    }
    
    // Otherwise, ordering doesn't matter
    //
    return 0;
}

VOID Fini(INT32 code, VOID* v) {
    traceFile << "]}";
}

INT32 Usage() {
	std::cerr << "Log allocations, deallocations, reads, and writes" << std::endl;
	std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
	return EXIT_FAILURE;
}

int main(int argc, char* argv[]) {
	PIN_InitSymbols();
	if (PIN_Init(argc, argv)) {
		return Usage();
	}

    curTime = 0;
    traceFile.open(knobOutputFile.Value().c_str());
    traceFile.setf(ios::showbase);
    traceFile << "{\"events\":["; // Begin JSON
    PIN_InitLock(&outputLock);
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
