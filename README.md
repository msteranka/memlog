# HeapShark

HeapShark logs invocations of malloc and free as well as memory accesses
to find allocation patterns suitable for custom allocators.

## Usage

To use HeapShark, you must first install Intel's binary instrumentation
framework, Pin. Pin can be installed from 

    https://software.intel.com/content/www/us/en/develop/articles/pin-a-binary-instrumentation-tool-downloads.html

Once Pin is installed, you can build HeapShark by running the 
following command in the src directory:

    $ make PIN_ROOT=/path/to/Pin obj-intel64/heapshark.so

To run HeapShark with the default parameters on a given executable, 
use the following command:

    $ /path/to/Pin/pin -t obj-intel64/heapshark.so -- /path/to/executable executable_args

By default, HeapShark does not sample memory accessses and stores a 
maximum of three frames in backtraces. The sampling rate and maximum 
backtrace depth can be configured using the arguments -s and -d, respectively. 
To run HeapShark with a sampling rate of 0.1, maximum depth of 5, and 
output file mydata.json, run the following command:

    $ /path/to/Pin/pin -t heapshark.so -o mydata.json -s 0.1 -d 5 -- /path/to/executable executable_args

To parse the output file that HeapShark generated, go to the 
tools directory and run

    $ python3 parse.py --input ../src/mydata.json

By default, the output file will be generated in src/heapshark.json,
and the Python script will default to this path when parsing.

