AC-FTE
======

AC-FTE stands for Adaptive Collective Fault Tolerance Engine. Its main goal is to serve as an experimental 
testbed for novel fault tolerance techniques that specifically target parallel and distributed applications 
running at large scale. 

Currently, the main focus of AC-FTE is optimization of checkpoint-restart approaches. To this end, it
implements a checkpointing library that offers specialized memory allocation primitives used to
designate critical memory regions that are necessary to restart the computation in case of failures.
At any moment during the execution, a special checkpoint primitive can be invoked to dump the contents
of the designated memory regions into a file. It is the responsability of the user to implement a restart
mechanism based on the contents of the file.

AC-FTE implements two techniques to minimize the overhead of checkpointing during application runtime
(both in terms of performance penalty and storage space required for the checkpoints):

1. An asynchronous checkpointing mechanism that dumps the content of the designated memory
regions to a file in the background without blocking. This is achieved by trapping all first time writes 
to designated memory regions and forcing a flush to the checkpoint before the memory contents can be overwritten 
with new values, effectively guaranteeing consistency. Several optimizations such as copy-on-write buffering and 
prioritized flushing based on adaptation to the first time write access pattern are used to minimize the 
interference of checkpointing with the application runtime. Please consult this paper for more details:

	"AI-Ckpt: Leveraging Memory Access Patterns for Adaptive Asynchronous Incremental Checkpointing" Bogdan Nicolae, 
  Franck Cappello. In HPDC '13: 22th International ACM Symposium on High-Performance Parallel and Distributed 
  Computing, 2013

2. An inline collective deduplication scheme that detects and eliminates redundancies in checkpointing data
both in the memory space of individual processes, as well as globally.  This technique is targeted at MPI 
applications, assuming a set of processes that need to checkpoint simultaneously and can collaborate during 
the checkpointing phase to achieve such a collective deduplication. It is the first of its kind to
propose the idea of looking beyond the memory space of a single process in order to improve deduplication
rates for scenarios where the same memory contents appears in multiple processes a single time. Furthermore, 
the deduplication is done before saving the memory contents into files (hence inline), thus lowering I/O 
bandwidth necessary to store the checkpoints. Please consult this paper for more details:

  "Towards Scalable Checkpoint Restart: A Collective Inline Memory Contents Deduplication Proposal" Bogdan Nicolae. 
  In IPDPS '13: The 27th IEEE International Parallel and Distributed Processing Symposium, 2013

AC-FTE was written by Bogdan Nicolae while working for IBM Research, Ireland and is released under the Apache
License, version 2 (included with the source code).

For more details, please contact:<br/>
Bogdan Nicolae<br/>
E-mail: bogdan.nicolae@ie.ibm.com<br/>
Web: http://researcher.ibm.com/person/ie-bogdan.nicolae
