Tyche - Example Project for the Harper Cache Replacement Strategy (HCRS)
=====

The tyche project (prefer to keep it lower case) is actually just an implementation of the currently-theoretical Harper Cache Replacement Strategy (still in private development).

This will be open source (as will the HCRS theory) but I am still going to enforce the Apache 2.0 software license to it for now.

**NOTE:  All updates below will be converted into a changelog file soon.**

**[2015-08-25]**
*The new lock subsystem is in effect.  Concurrent read/write is working and our MPMC tests are good.  Performance is non-linear (scales with CPU) by means of reference counters protected by locks rather than blocking readers with the locks alone. The following table demonstrates this to an extent; but the short-lived nature of the readers in this test prevented better scaling (in other words, the time 'using' the buffer was so short we were spending a large percentage of time handling buffer locks and the list lock anyway.)*

| CPUs |      Reads | Avg Time | Reads/sec | Reads/sec/CPU | Contention |
| ---: | ---------: | -------: | --------: | ------------: | ---------- |
|    1 | 25,000,000 |    43.73 |   571,648 |       571,648 | High(1)    |
|    2 | 25,000,000 |    33.41 |   748,196 |       374,098 | High       |
|    4 | 25,000,000 |    28.36 |   881,461 |       220,365 | High       |
|   ^8 | 25,000,000 |    42.39 |   589,709 |        73,713 | High       |
|    1 | 25,000,000 |    42.92 |   582,368 |       582,368 | Minimal(2) |
|    2 | 25,000,000 |    24.89 | 1,004,238 |       502,118 | Minimal    |
|    4 | 25,000,000 |    21.25 | 1,176,664 |       294,166 | Minimal    |
|   ^8 | 25,000,000 |    30.41 |   822,052 |       102,756 | Minimal    |

^ The test platform was a VM.  8 vCPU started running into pCPU scheduling overhead and did more harm than good.  Even 4 vCPU was starting to show signs of inefficiency.
(1) High contention means I purpose created a high worker-to-buffer pool size.  In other words I made thousands of worker threads and they could only select from 100 buffers.  Draining the list this low also resulted in huge (99%+) cache "misses".  This helped demonstrate ref count improvements over global locking.
(2) Minimal contention does the opposite of high.  Lots of buffers to choose from compared to workers, and hit rates closer to 95%+ successful.

**[2015-08-07]**
*The fundamentals of locking work but use a shared lock so performance is linear.  Boo.*

**[2015-06-18]**
*If you're here reading/using this, I don't know why...*
