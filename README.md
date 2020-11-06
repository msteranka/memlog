# MemLog

MemLog logs calls to malloc and free as well as reads and writes

## Usage

Build MemLog:

    $ make PIN_ROOT=/path/to/Pin obj-intel64/memlog.so

Run MemLog:

    $ /path/to/Pin/pin -t obj-intel64/memlog.so -- /path/to/executable
