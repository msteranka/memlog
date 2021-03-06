#if !defined(__BACKTRACE_HPP)
# define __BACKTRACE_HPP

#include <iostream>
#include "pin.H"

using namespace std;

// TODO: Why not put all parameters in one file?
//
namespace BacktraceParams {
    INT32 maxDepth;
};

// Nothing within Backtrace is thread-safe since all of its
// methods are only ever executed by one thread
//
class Backtrace {
public:
    // A Backtrace is initialized with the maximum number of stack frames
    // that it will go down
    //
    Backtrace() {
        trace = new pair<string,INT32>[BacktraceParams::maxDepth];
        for (INT32 i = 0; i < BacktraceParams::maxDepth; i++) {
            trace[i].first = "";
            trace[i].second = 0;
        }
    }

    VOID SetTrace(const CONTEXT *ctxt) {
        // buf contains maxDepth + 1 addresses because PIN_Backtrace also returns
        // the stack frame for malloc/free
        //
        VOID *buf[BacktraceParams::maxDepth + 1];
        INT32 depth;

        if (ctxt == nullptr) {
            return;
        }
    
        // Pin requires us to call Pin_LockClient() before calling PIN_Backtrace
        // and PIN_GetSourceLocation
        //
        PIN_LockClient();
        depth = PIN_Backtrace(ctxt, buf, BacktraceParams::maxDepth + 1) - 1;

        // We set i = 1 because we don't want to include the stack frame 
        // for malloc/free
        //
        for (INT32 i = 1; i < depth + 1; i++) {
            // NOTE: executable must be compiled with -g -gdwarf-2 -rdynamic
            // to locate the invocation of malloc/free
            // NOTE: PIN_GetSourceLocation does not necessarily get the exact
            // invocation point, but it's pretty close
            //
            PIN_GetSourceLocation((ADDRINT) buf[i], nullptr, 
                                    &(trace[i - 1].second),
                                    &(trace[i - 1].first));
        }
        PIN_UnlockClient();
    }

    pair<string,INT32> *GetTrace() { return trace; }

    Backtrace &operator=(const Backtrace &b) {
        for (INT32 i = 0; i < BacktraceParams::maxDepth; i++) {
            trace[i].first = b.trace[i].first;
            trace[i].second = b.trace[i].second;
        }
        return *this;
    }

    ~Backtrace() {
        // TODO: potential memory leak -- safe to delete in destructor?
        // delete[] trace;
    }

private:
    // trace consists of all invocation points of malloc/free, 
    // represented as a pairing of a file name and a line number
    //
    pair<string,INT32> *trace;
};

ostream& operator<<(ostream& os, Backtrace& bt) {
    pair<string,INT32> *t;
    t = bt.GetTrace();

    os << "[";
    for (int i = 0; i < BacktraceParams::maxDepth; i++) {
        if (t[i].first == "") { // If PIN_GetSourceLocation failed
            os << "{\"path\":\"\",\"line\":0}";
        }
        else {
            os << "{\"path\":\"" << t[i].first << "\",\"line\":" 
                << t[i].second << "}";
        }
        if (i < BacktraceParams::maxDepth - 1) { // If there's another frame after this one
            os << ",";
        }
    }
    os << "]";
    return os;
}

#endif
