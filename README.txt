==Team members:==
A0110781N	Qua Zi Xian
A????????	Wei Han

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

StrategyInitialize:
	allocate the required arrays from shared memory(shmem functions)

==Benchmarking:==
From benchmarking results, it seems that the LRU runs slower than the clock
sweep.
This may be because the overhead incurred from selection of the buffer to be
replaced takes up a significant portion of the total delay.
In the clock sweep, the only operations used to move on to the next victim for
consideration are arithmetic operations like increment and modulo.
However, in our LRU implementation, accessing the next victim involves pointer
indirection, which is slower than pure arithmetic operations.
