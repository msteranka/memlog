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

namespace HeapSharkParams {
    static std::ofstream traceFile;
    static double samplingRate;
    static unsigned int maxDepth;
};

namespace TLSData {
    TLS_KEY tlsKey;
    std::list<MyTLS*> tlsList;
    PIN_LOCK tlsListLock;
};

static AFUNPTR mallocUsableSize;
static unsigned int curTime;

inline size_t GetNext(unsigned int *seedp, double p) {
    int r = rand_r(seedp); // TODO: use better RNG
    float u = (float) r / (float) RAND_MAX;
    size_t geom = (size_t) ceil(log(u) / log(1.0 - p));
    return geom;
}

VOID ThreadStart(THREADID threadId, CONTEXT *ctxt, INT32 flags, VOID *v) {
    MyTLS *tls = new MyTLS;
    assert(PIN_SetThreadData(TLSData::tlsKey, tls, threadId));
    PIN_GetLock(&TLSData::tlsListLock, -1);
    TLSData::tlsList.push_back(tls);
    PIN_ReleaseLock(&TLSData::tlsListLock);
    tls->_geom = (ssize_t) GetNext(&(tls->_seed), HeapSharkParams::samplingRate);
}

VOID ThreadFini(THREADID threadId, const CONTEXT *ctxt, INT32 code, VOID *v) { }

VOID MallocBefore(THREADID threadId, const CONTEXT* ctxt, ADDRINT size) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(TLSData::tlsKey, threadId));
    tls->_cachedSize = size;
    tls->_cachedBacktrace.SetTrace(ctxt);
}

VOID MallocAfter(THREADID threadId, ADDRINT retVal) {
    if ((void *) retVal == nullptr) { 
        return;
    }
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(TLSData::tlsKey, threadId));
    // No need for atomicity with timestamps. We just need some loose ordering
    // of events.
    //
    tls->_eventsList.push_back(new AllocationEvent(E_MALLOC, (void *) retVal, tls->_cachedSize, threadId, curTime, tls->_cachedBacktrace));
    curTime++;
}

VOID FreeHook(THREADID threadId, const CONTEXT* ctxt, ADDRINT ptr) {
    if ((void *) ptr == nullptr) {
        // We don't need to track frees to null pointers.
        return;
    }

    size_t size = 0;
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(TLSData::tlsKey, threadId));
    Backtrace backtrace;
    backtrace.SetTrace(ctxt);
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
    tls->_eventsList.push_back(new AllocationEvent(E_FREE, (void *) ptr, size, threadId, curTime, backtrace));
}

VOID ReadsMem(THREADID threadId, ADDRINT addrRead, UINT32 readSize) {
    // static const size_t MAX_SIZE = 1048576; // ADJUSTABLE
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(TLSData::tlsKey, threadId));
    if (LIKELY(tls->_geom > 0)) {
        tls->_geom -= readSize;
        return;
    }
    tls->_eventsList.push_back(new AccessEvent(E_READ, (void *) addrRead, readSize, threadId, curTime));
    // if (UNLIKELY(tls->_eventsList.size() >= MAX_SIZE)) {
    //     WriteEvents(fd, &outputLock, &(tls->_eventsList));
    // }
    tls->_geom = (ssize_t) GetNext(&(tls->_seed), HeapSharkParams::samplingRate);
}

VOID WritesMem(THREADID threadId, ADDRINT addrWritten, UINT32 writeSize) {
    MyTLS *tls = static_cast<MyTLS*>(PIN_GetThreadData(TLSData::tlsKey, threadId));
    if (LIKELY(tls->_geom > 0)) {
        tls->_geom -= writeSize;
        return;
    }
    tls->_eventsList.push_back(new AccessEvent(E_WRITE, (void *) addrWritten, writeSize, threadId, curTime));
    tls->_geom = (ssize_t) GetNext(&(tls->_seed), HeapSharkParams::samplingRate);
}

VOID Instruction(INS ins, VOID* v) {
    // Intercept non-stack reads with ReadsMem
    //
	if (INS_IsMemoryRead(ins) && !INS_IsStackRead(ins)) {
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem,
					   IARG_THREAD_ID,
					   IARG_MEMORYREAD_EA,
					   IARG_MEMORYREAD_SIZE,
					   IARG_END);
	}

    // Intercept non-stack writes with WritesMem
    //
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
    const char *mallocUsableSizeFunctionName = "malloc_usable_size";

    // Intercept calls to malloc with MallocBefore + MallocAfter
    //
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

    // Intercept calls to free with FreeBefore
    //
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

    // Store the function pointer to malloc_usable_size
    //
    rtn = RTN_FindByName(img, mallocUsableSizeFunctionName);
    if (RTN_Valid(rtn)) {
        mallocUsableSize = RTN_Funptr(rtn);
    } else {
        mallocUsableSize = nullptr;
    }
}

VOID Fini(INT32 code, VOID* v) {
    // Move all events to a single data structure and sort them by time
    //
    list<Event*> allEvents;
    while (!TLSData::tlsList.empty()) {
        list<Event*> *curList = &(TLSData::tlsList.front()->_eventsList);
        while (!curList->empty()) {
            allEvents.push_back(curList->front());
            curList->pop_front();
        }
        delete TLSData::tlsList.front();
        TLSData::tlsList.pop_front();
    }
    allEvents.sort(eventCompare);

    // Output sorted events in JSON format
    //
    HeapSharkParams::traceFile << "\"events\":[";
    while (!allEvents.empty()) {
        auto it = allEvents.begin();
        if (allEvents.size() > 1) {
            HeapSharkParams::traceFile << **it << ",";
        } else {
            HeapSharkParams::traceFile << **it;
        }
        delete *it;
        allEvents.pop_front();
    }
    HeapSharkParams::traceFile << "]}";
}

INT32 Usage() {
	cerr << "HeapShark identifies allocations that can be replaced "
            "with custom allocation routines to improve the performance "
            "of C/C++ applications." << endl;
	cerr << KNOB_BASE::StringKnobSummary() << endl;
	return EXIT_FAILURE;
}

void Fatal(string errMsg) {
    cerr << errMsg << endl;
    PIN_ExitProcess(1);
}

int main(int argc, char* argv[]) {
    // Declare configurable HeapShark parameters
    //
    const string defaultOutputFile = "heapshark.json", 
                 defaultSamplingRate = "0.001", 
                 defaultMaxDepth = "3";
    KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", 
                                defaultOutputFile, 
                                "Output file");
    KNOB<double> knobSamplingRate(KNOB_MODE_WRITEONCE, "pintool", "s", 
                                  defaultSamplingRate, 
                                  "Percentage of reads/writes to record");
    KNOB<unsigned int> knobMaxDepth(KNOB_MODE_WRITEONCE, "pintool", "d", 
                                    defaultMaxDepth, 
                                    "Maximum number of frames to stores in backtraces");

    // Initialize Pin and parse arguments
    //
	PIN_InitSymbols();
	if (PIN_Init(argc, argv)) {
		return Usage();
	}

    // Initialize HeapShark parameters
    //
    HeapSharkParams::traceFile.open(knobOutputFile.Value().c_str());
    HeapSharkParams::traceFile.setf(ios::showbase);
    HeapSharkParams::samplingRate = knobSamplingRate.Value();
    HeapSharkParams::maxDepth = knobMaxDepth.Value();

    // Check parameters for validity
    //
    // TODO: need to check if traceFile is valid?
    if (HeapSharkParams::samplingRate < 0 || HeapSharkParams::samplingRate > 1) {
        // TODO: Sampling rate of 0 shouldn't logs reads/writes at all
        //
        Fatal("Sampling rate must be within the interval [0, 1]");
    }
    if (HeapSharkParams::maxDepth > 256) {
        Fatal("Maximum number of frames cannot exceed 256");
    }
    BacktraceParams::maxDepth = HeapSharkParams::maxDepth;

    // TODO: JSON formatting is a headache when it's not all done in one place...
    //
    HeapSharkParams::traceFile << "{\"metadata\":{\"samplingRate\":" << 
                  HeapSharkParams::samplingRate << 
                  ",\"maxDepth\":" << 
                  HeapSharkParams::maxDepth << "},";

    // Initialize TLS related data
    //
    PIN_InitLock(&TLSData::tlsListLock);
    TLSData::tlsKey = PIN_CreateThreadDataKey(NULL);
    if (TLSData::tlsKey == INVALID_TLS_KEY) {
        Fatal("Number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit");
    }

    curTime = 0;

    // Add instrumentation functions
    //
	IMG_AddInstrumentFunction(Image, 0);
	INS_AddInstrumentFunction(Instruction, 0);
	PIN_AddThreadStartFunction(ThreadStart, 0);
	PIN_AddThreadFiniFunction(ThreadFini, 0);
	PIN_AddFiniFunction(Fini, 0);

    // Begin program
    //
	PIN_StartProgram();
}
