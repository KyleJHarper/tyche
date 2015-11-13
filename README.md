Tyche - Example Project for an Adaptive Compressed-Cache Replacement Strategy (ACCRS)
=====

The tyche project is actually just an implementation of the currently-theoretical Adaptive Compressed-Cache Replacement Strategy (still in private development).

This will be open-source (as will the ACCRS theory) and free but I am still going to enforce the Apache 2.0 software license to it for now, as well as retain copyright on the theory.

====
Latest Changelog Entries

**[2015-11-12]**
*The lists now support transferring buffers between them.  If a buffer is too big for the raw list, the raw list will be swept to make room.  Victimized buffers will be compressed and sent to the compressed list.  If the compressed list cannot contain the victimized buffer, it'll be purged one generation at a time until they all fit.*

**[2015-11-03]**
*A buffer can now compress/decompress its data element.  A compression test function was created to help with regression testing.  The compression method selected is lz4, largely due to it's extremely fast decompression speed.  When compression occurs, the comp_length member (new) stores this for future use (lz4 doesn't technically need it, but we use it for safety).  In addition, the comp_time member is updated to reflect the time (in ns) required.*

*Decompression is similar in that comp_time is updated to include decompression time.  It also updates the comp_hits member so that the (future) evaluation engine can make determinations on compression efficacy for the life of the buffer.*

*The next step is to write functionality to manage the lists so that buffers can be moved between them.  This is cost that should be accounted for but it's not specific to a single buffer; so we'll probably end up tracking this globally or at the list-level.*

*Overall, preliminary tests are hopeful.  A full comp/decomp cycle with a 4KB (4096 byte) data set (Lorem Ipsum text) takes approximately 100,000 ns on my test VM with a Core i7 4790 (single core) on DD3-1600 RAM (9-9-9-24).  This equates to 100 microseconds (us) or 0.1 millisecond (ms).  Of this, 85% was spent compressing while only 15% was decompressing; so when we need the buffer it can be ready in 0.15 * 100,000 ns, or 0.015 ms.  This favors the theory we're investigating because compression can be theoretically "offline", doing work while it happens; while decompression is always giong to be "on demand", meaning someone needs it now.*

*Comparing this to storage retrieval options:*

| Medium                 | Time (ms) | Times per Operation (ms)            |
| ---------------------- | --------: | ----------------------------------- |
|  7200 RPM HDD 120 MB/s |    13.203 | 9.0 seek + 4.17 rl + 0.033 transfer |
| 15000 RPM HDD 200 MB/s |     6.020 | 4.0 seek + 2.00 rl + 0.020 transfer |
|           SSD 150 MB/s |     0.126 | 0.1 seek + 0.00 rl + 0.026 transfer |
|           SSD 300 MB/s |     0.113 | 0.1 seek + 0.00 rl + 0.013 transfer |
|           SSD 600 MB/s |     0.107 | 0.1 seek + 0.00 rl + 0.007 transfer |

(rl == rotational latency)

Sources: basic math and wikipedia.org articles.

Note: Transfer time is from platter to sector buffer and doesn't account for interface transfer time (e.g.: SATA speed/latency).  This can become more significant as storage fabrics come into play; e.g.: iSCSI, Fiber Channel, and so forth.

*The above table is far from a perfect analysis, but that's what tyche is all about: real world testing when it's done.  So far, our total compression/decompression time is competitive with the fastest SSD on a local bus.  When we compare decompression time for respone time we're drastically faster than even the fastest SSD.  For now, I'm satisfied that the theory is still plausible.*

**[2015-10-01]**
*The buffer initialization now accepts a char pointer to use as the file-spec for reading from the disk.  IO cost is stored in the buffer as a result of this.  (Passing NULL allows you to skip this, and as a result IO time is still 0, obviously.)*


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

(1) High contention means I purposely created a high worker-to-buffer pool size.  In other words I made thousands of worker threads and they could only select from 100 buffers.  Draining the list this low also resulted in huge (99%+) cache "misses".  This helped demonstrate ref count improvements over global locking.

(2) Minimal contention does the opposite of high.  Lots of buffers to choose from compared to workers, and hit rates closer to 95%+ successful.

*(Additional logs found in changelog)*
