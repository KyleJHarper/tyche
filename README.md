Tyche - Example Project for the Harper Cache Replacement Strategy (HCRS)
=====

The tyche project (prefer to keep it lower case) is actually just an implementation of the currently-theoretical Harper Cache Replacement Strategy (still in private development).

This will be open source (as will the HCRS theory) but I am still going to enforce the Apache 2.0 software license to it for now.

**[2015-08-25]**
*The new lock subsystem is in effect.  Concurrent read/write is working and our MPMC tests are good.  Performance is non-linear (scales with CPU) by means of reference counters protected by locks rather than the locks alone.*

**[2015-08-07]**
*The fundamentals of locking work but use a shared lock so performance is linear.  Boo.*

**[2015-06-18]**
*If you're here reading/using this, I don't know why...*
