==Team members:==
A0110781N	Qua Zi Xian
A0097582N	Wei Han

==LRU Implementation:==
Variable-sized doubly-linked list of victim buffers bounded by NBuffers
is implemented using 2 arrays of index pointers: bufprev and bufnext.
bufprev[i] points to the buf_id of the buffer that will be considered for
eviction before the buffer with buf_id i.
bufnext[i] points to the buf_id of the buffer that will be considered for
eviction once the buffer with buf_id i is evicted.
Value of -1 is used to represent invalid pointers.
mrubuf points to the most recently used buf_id. This is used for updating
purposes.
Rationale for such representations:
Insert, shifting, and deletion of any buf_id runs in O(1).
Compare this to insertion-sort mechanism which can potentially go up to O(N).

==Summarized changes in freelist-lru.c:==
Subroutines: AddToFreelist, RemoveFromFreelist, AddVictim, RemoveVictim

StrategyUpdateAccessedBuffer:
	remove buffer(index) from victim list
	if is delete, add to free list
	else add to back of victim list(MRU)

StrategyGetBuffer:
	while freelist not empty,
		move buffer(index) from freelist to victim list
		return buffer
	for each buffer in victim list,
		if refcount is 0,
			move buffer(index) to back of victim list(MRU)
			return buffer
	throw error

StrategyFreeBuffer:
	remove buffer(index) from victim list
	add to head of free list

StrategyShmemSize:
	increase size by 2*NBuffers*sizeof(int) to account for the additional arrays used in 
	StrategyControl.

StrategyInitialize:
	allocate the required arrays from shared memory(shmem functions)

==LRU2 Implementation==
Let S1 be a doubly-linked list of buffer pages with only 1 access, ordered from MRU to LRU.
Let S2 be a doubly-linked list of the last 1 or 2 accesses of accessed buffer pages, ordered from MRU to LRU.
Let NBuffers be the total number of buffers.
Node labels in [0, NBuffers) refer to last access of buffer with id equals to the node label.
Node labels in [NBuffers, 2*NBuffers) refer to 2nd last access of buffer with id equals to (node label - NBuffers).
S1 and S2 are implemented in the same way as in LRU for O(1) linked list operations.

==Summarised changes in freelist-lru2.c==
StrategyUpdateAccessedBuffer:
	if is delete, remove buf_id from S1 and S2. Remove buf_id+NBuffers from S2. Add buffer to freelist.
	else:
		Remove buf_id from S1.
		Remove buf_id+NBuffers from S2.
		Replace buf_id with buf_id+NBuffers in S2.
		Insert buf_id to front(MRU) of S2.

StrategyGetBuffer:
	while have free buffer:
		Remove buffer from freelist.
		Add buf_id to S1 and S2.
		if refcount is 0, return this buffer.

	while S1 is not empty:
		if refcount is 0:
			Remove buf_id from S1 and S2.
			Remove buf_id+NBuffers from S2(if exists in S2).
			Insert buf_id to front of S1 and S2.
			Return buffer.

	while S2 is not empty:
		if node is 2nd last access of a buffer:
			Get the corresponding buffer.
			if refcount is 0:
				Remove buf_id from S1 and S2.
				Remove buf_id+NBuffers from S2.
				Insert buf_id to front of S1 and S2.
				Return buffer.
	throw error

StrategyFreeBuffer:
	Similar to that in LRU

StrategyShmemSize:
	Similar to that in LRU

StrategyInitialize:
	Similar to that in LRU

==Benchmarking:==
From benchmarking results, it seems that the LRU runs slower than the clock
sweep and LRU2 runs slower than LRU.

This may be because the overhead incurred from selection of the buffer to be
replaced takes up a significant portion of the total delay.
In the clock sweep, the only operations used to move on to the next victim for
consideration are arithmetic operations like increment and modulo.
However, in our LRU implementation, accessing the next victim involves pointer
indirection, which is slower than pure arithmetic operations.
In addition, for LRU, the 2 arrays used for pointing are allocated separately from 
the StrategyControl. Accessing the 2 arrays would access a (potentially) 
different memory block, which can contribute to the delay. This is even worse for LRU2 as 2 linked lists are maintained.

On the contrary, LRU has a higher hit ratio than clock sweep and LRU2 has higher hit
rate than LRU.
