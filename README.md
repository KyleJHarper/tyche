Tyche - Example Project for an Adaptive Compressed-Cache Replacement Strategy (ACCRS)
=====

The tyche project is actually just an implementation of the currently-theoretical Adaptive Compressed-Cache Replacement Strategy (still in private development).

This will be open-source (as will the ACCRS theory) and free but I am still going to enforce the Apache 2.0 software license to it for now, as well as retain copyright on the theory.

====
Latest Changelog Entries

**[2016-03-06]**
*After significant effort there were 3 major accomplishments: list management improvements (mostly in sweeping), baseline profiling (valgrind, gprof, etc), and compression parallelization.*

*First, I managed to get list management improvements made without adding excessive complexity.  The majority of the optimization was found in list__sweep() and list__restore().  Buffers are no longer copied; they are federated, processed, and reassigned.  Furthermore, operating in batches allowed for increased performance due to (I'm guessing here) better cache-locality in the CPU.  In a 5 minute test with a fixed ratio of 1% (virtually everything in the offload list) we went from 4,700 acquisitions/sec to 56,200 per sec.*

*Second, I did the first round of profiling with valgrind and gprof.  There were several leaks in the io scanning which didn't surprise me.  Those and others were fixed.  Call analysis helped fixed list__sweep() and others.  SkiplistNodes aren't leaked, but they aren't tracked in BUFFER_OVERHEAD either... still torn on whether to append it.*

*Finally, compression was so fast that parallelizing it was tough.  The mere management of a list for a queue to process was adding significant overhead compared to the actual LZ4 compress operations.  Batching with an array and indexes solved this.  A more intense compressor would have actually made this easier and aided in the apparent parallelization (using more CPUs at once).*

*Several tests were also updated to make them accurate and functional again.*

*This marks version 0.0.8*

**[2015-12-11]**
*Tyche now does the job I originally wanted it to do... read buffers and use a compressed offload-list.  Hooray.*

*There isn't any ACCRS logic because there are too many other issues preventing good results.  Namely, list management itself is too expensive, particularly list adding/removing.  This will be fixed when I convert to a linked-list with a skip list indexing system later.*

*The following table shows that hit ratio is indeed being affected as expected.  This test used 888 MB of data in 8KB pages.  Tyche was given 500 MB of RAM to use.  There were 112991 pages to choose from in the tests.*

| Ratio | Raw MB | Raw Buffers | Comp MB | Comp Buffers | Acquisitions | Test 1 | Test 2 | Test 3 |
| ----: | -----: | ----------: | ------: | -----------: | -----------: | -----: | -----: | -----: |
|   90% |    450 |      54,931 |      50 |      ~16,062 |    2,138,648 | 56.46% | 56.47% | 56.48% |
|   60% |    300 |      36,621 |     200 |      ~64,248 |    1,439,809 | 79.40% | 79.40% | 79.40% |
|   30% |    150 |      18,310 |     350 |     ~112,433 |    1,070,778 | 89.46% | 89.46% | 89.43% |

*We are clearly increasing our hit ratio.  The tests only ran for 5 minutes each (3 rounds, averaged) and the performance of my list management is still poor, so the hit ratios for all tests weren't close to their theorectical averages (because the first hit to a page counts as a miss, so it'll never be 100%).*

**[2015-11-19]**
*Tyche now supports options processing in a more sane (consistent) manner.  This options struct serves as a nice way to share options with functions.*

*Added a new test option (-t name).  It will run the built-in test with the associated name.*

*Added a new option (-l) to control lock sharing ratios.  No more large array declarations at compile time.*


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

<sub>(rl == rotational latency)</sub>

<sub>Sources: basic math and wikipedia.org articles.</sub>

<sub>Note: Transfer time is from platter to sector buffer and doesn't account for interface transfer time (e.g.: SATA speed/latency).  This can become more significant as storage fabrics come into play; e.g.: iSCSI, Fiber Channel, and so forth.</sub>

*The above table is far from a perfect analysis, but that's what tyche is all about: real world testing when it's done.  So far, our total compression/decompression time is competitive with the fastest SSD on a local bus.  When we compare decompression time for respone time we're drastically faster than even the fastest SSD.  For now, I'm satisfied that the theory is still plausible.*

*(Additional logs found in changelog)*
