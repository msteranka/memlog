# memlog

memlog logs calls to malloc and free as well as reads and writes

## Usage

Build memlog:

    $ make PIN_ROOT=/path/to/Pin obj-intel64/memlog.so

Run memlog:

    $ /path/to/Pin/pin -t obj-intel64/memlog.so -- /path/to/executable

By default, memlog samples 0.001 of reads and writes. Additionally, it tracks calls to malloc and free 3 frames down. The sampling rate and maximum depth can be configured using the parameters -s and -d, respectively. To run memlog with a sampling rate of 0.1, maximum depth of 5, and output file mydata.json, run the following command:

    $ /path/to/Pin/pin -t obj-intel64/memlog.so -o mydata.json -s 0.1 -d 5 -- /path/to/executable
