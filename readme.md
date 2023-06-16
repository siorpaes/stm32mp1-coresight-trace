## Using MP1 STM to emit trace packets upon stimulus ports writes or hardware events
 
The CoreSight elements in STM32MP1 as connected as follows:

STM => FUNNEL => ETF

The STM can emit trace packets both from AXI stimulus ports or external hardware events such as intrrupts, DMA requests, and so on.

The units are described in the reference manual at these pages
Diagram            p 3652
IP base addresses  p 3669
STM                p 3770
DBCMCU             p 3807
Funnel             p 3724

* Build and run the trace application. This application writes on STM first stimuls port and configures the hardare events to trace ttySTM0 (console) and ttySTM1 interrupts
* Download OpenCSD tools from https://github.com/Linaro/OpenCSD, build it and install it
* Build trace application
* Generate some traffic on ttySTM0 and/or ttySTM1
* Start the trace application with superuser permissions
* Once completed (takes about two seconds), a binary file will be created in '/dev/shm/cstraceitm.bin'
* Copy such file in the `opencsd-decode` directory and run the decoding tool in it: `trc_pkt_lister -ss_dir .`
* The trace file will be decoded and results printed to standard output as in the below example snapshot:

```
[...]
Idx:542; ID:20; D8M:8 bit data + marker; Data=0x34
Idx:545; ID:20; D8M:8 bit data + marker; Data=0x27
Idx:547; ID:20; D8M:8 bit data + marker; Data=0x27
Idx:549; ID:20; D8M:8 bit data + marker; Data=0x27
Idx:551; ID:20; M8:Set current master; Master=0x40
Idx:553; ID:20; D32M:32 bit data + marker; Data=0xabcd0009
Idx:558; ID:20; NULL:Null packet
Idx:558; ID:20; M8:Set current master; Master=0x80
Idx:561; ID:20; D8M:8 bit data + marker; Data=0x34
Idx:563; ID:20; NULL:Null packet
Idx:563; ID:20; D8M:8 bit data + marker; Data=0x27
Idx:565; ID:20; D8M:8 bit data + marker; Data=0x27
Idx:567; ID:20; D8M:8 bit data + marker; Data=0x34
[...]
```

Diferent packets can be observed: 0x80 master represent packets generated by hardware events. Data=0x34 and Data=0x27 re associated with the two UART interrupts. Master 0x40 is associated to CortexA7 #0 and the reported data correspond to the values that are written on the first AXI stimulus port. Second Cortex-A7 is associated to master port 0x41.
Application can be run on either core bu using the `taskset` application. e.g.: `sudo taskset -c 0  ./trace`.

Note that of TRACEID is changed or other parameters are enabled/disabled, this must be reflected in the `device_0.ini` file.

