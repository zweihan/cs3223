/*-------------------------------------------------------------------------
 *
 * freelist.c
 *	  routines for managing the buffer pool's replacement strategy.
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/buffer/freelist.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "storage/buf_internals.h"
#include "storage/bufmgr.h"

#define CS3223DEBUG 1
#define MRU_NOT_IN_LIST -1
#define VICTIM_NOT_IN_LIST -1

/*
 * The shared freelist control information.
 */
typedef struct
{
	/* Clock sweep hand: index of next buffer to consider grabbing */
	int			nextVictimBuffer;

	int			firstFreeBuffer;	/* Head of list of unused buffers */
	int			lastFreeBuffer; /* Tail of list of unused buffers */

	/* cs3223 */
	int *bufprev;	// List of pointers to previous victim
	int *bufnext;	// List of pointers to next victim
	int mrubuf;	// Most recently used buffer
	int numVictims;	// Number of evictions done. Used to update completePasses
	bool *isUsed;	// For internal checking/assertion
	/* End of cs3223 */

	/*
	 * NOTE: lastFreeBuffer is undefined when firstFreeBuffer is -1 (that is,
	 * when the list is empty)
	 */

	/*
	 * Statistics.  These counters should be wide enough that they can't
	 * overflow during a single bgwriter cycle.
	 */
	uint32		completePasses; /* Complete cycles of the clock sweep */
	uint32		numBufferAllocs;	/* Buffers allocated since last reset */

	/*
	 * Notification latch, or NULL if none.  See StrategyNotifyBgWriter.
	 */
	Latch	   *bgwriterLatch;
} BufferStrategyControl;

/* Pointers to shared state */
static BufferStrategyControl *StrategyControl = NULL;

/*
 * Private (non-shared) state for managing a ring of shared buffers to re-use.
 * This is currently the only kind of BufferAccessStrategy object, but someday
 * we might have more kinds.
 */
typedef struct BufferAccessStrategyData
{
	/* Overall strategy type */
	BufferAccessStrategyType btype;
	/* Number of elements in buffers[] array */
	int			ring_size;

	/*
	 * Index of the "current" slot in the ring, ie, the one most recently
	 * returned by GetBufferFromRing.
	 */
	int			current;

	/*
	 * True if the buffer just returned by StrategyGetBuffer had been in the
	 * ring already.
	 */
	bool		current_was_in_ring;

	/*
	 * Array of buffer numbers.  InvalidBuffer (that is, zero) indicates we
	 * have not yet selected a buffer for this ring slot.  For allocation
	 * simplicity this is palloc'd together with the fixed fields of the
	 * struct.
	 */
	Buffer		buffers[1];		/* VARIABLE SIZE ARRAY */
}	BufferAccessStrategyData;


/* Prototypes for internal functions */
static volatile BufferDesc *GetBufferFromRing(BufferAccessStrategy strategy);
static void AddBufferToRing(BufferAccessStrategy strategy,
				volatile BufferDesc *buf);

// cs3223
// AddVictim
// Adds buffer with given buf_id to victim list as MRU
static void AddVictim(int buf_id) {
    char str[50];
    sprintf(str, "add victim buf_id:  %d", buf_id);
    elog(INFO, str);
	Assert(buf_id >= 0 && buf_id < NBuffers);

	// Make it MRU buffer
	StrategyControl->bufprev[buf_id] = StrategyControl->mrubuf;
	if(StrategyControl->mrubuf >= 0)
		StrategyControl->bufnext[StrategyControl->mrubuf] = buf_id;
	StrategyControl->mrubuf = buf_id;

	// Special case: victim list is empty.
	// This buffer is also the next victim buffer
	if(StrategyControl->nextVictimBuffer < 0)
		StrategyControl->nextVictimBuffer = buf_id;

	StrategyControl->isUsed[buf_id] = true;
	Assert(StrategyControl->isUsed[buf_id]);
}

// RemoveVictim
// Removes buffer with given buf_id from victim list
static void RemoveVictim(int buf_id) {
	Assert(buf_id >= 0 && buf_id < NBuffers);

	// Link up both neighbours
	if(StrategyControl->bufprev[buf_id] >= 0)
		StrategyControl->bufnext[StrategyControl->bufprev[buf_id]] = StrategyControl->bufnext[buf_id];
	if(StrategyControl->bufnext[buf_id] >= 0)
		StrategyControl->bufprev[StrategyControl->bufnext[buf_id]] = StrategyControl->bufprev[buf_id];

	// If removing MRU
	if(buf_id == StrategyControl->mrubuf)
		StrategyControl->mrubuf = StrategyControl->bufprev[buf_id];

	// If is next victim buffer
	if(buf_id == StrategyControl->nextVictimBuffer)
		StrategyControl->nextVictimBuffer = StrategyControl->bufnext[buf_id];

	// Clear pointers
	StrategyControl->bufprev[buf_id] = StrategyControl->bufnext[buf_id] = VICTIM_NOT_IN_LIST;
}

// AddToFreelist
// Adds buffer to head of free list
static void AddToFreelist(volatile BufferDesc *buf) {
	int buf_id = buf->buf_id;
	Assert(buf_id >= 0 && buf_id < NBuffers);
	Assert(buf->refcount == 0);

	// Add to head of free list
	buf->freeNext = StrategyControl->firstFreeBuffer;
	if(StrategyControl->firstFreeBuffer < 0)
		StrategyControl->lastFreeBuffer = buf_id;
	StrategyControl->firstFreeBuffer = buf_id;

	buf->usage_count = 0;

	StrategyControl->isUsed[buf_id] = false;
	Assert(!StrategyControl->isUsed[buf_id]);
}

// RemoveFromFreelist
// Removes the first free buffer and adds to victim list as MRU
static void RemoveFromFreelist(volatile BufferDesc *buf) {
	int buf_id = buf->buf_id;
	Assert(buf_id == StrategyControl->firstFreeBuffer);
	Assert(buf_id >= 0 && buf_id < NBuffers);
	Assert(!StrategyControl->isUsed[buf_id]);

	// Updates first free buffer
	StrategyControl->firstFreeBuffer = buf->freeNext;
}

// StrategyUpdateAccessedBuffer 
// Called by bufmgr when a buffer page is accessed.
// Adjusts the position of buffer (identified by buf_id) in the LRU stack if delete is false;
// otherwise, delete buffer buf_id from the LRU stack.
void
StrategyUpdateAccessedBuffer(int buf_id, bool delete)
{
	if(CS3223DEBUG)
		elog(INFO, "StrategyUpdateAccessedBuffer");

	// Invalid buffer. Do nothing.
	if(buf_id < 0 || buf_id >= NBuffers)
		return;

	// Assert(StrategyControl->isUsed[buf_id]);
	
	RemoveVictim(buf_id);

	// If delete, add to free list
	if(delete){
		BufferDesc *buf = &BufferDescriptors[buf_id];
		LockBufHdr(buf);
		AddToFreelist(buf);
		UnlockBufHdr(buf);
	} else // Else, make it MRU buffer
		AddVictim(buf_id);

	if(CS3223DEBUG)
		elog(INFO, "Exiting StrategyUpdateAccessedBuffer");
}

/*
 * StrategyGetBuffer
 *
 *	Called by the bufmgr to get the next candidate buffer to use in
 *	BufferAlloc(). The only hard requirement BufferAlloc() has is that
 *	the selected buffer must not currently be pinned by anyone.
 *
 *	strategy is a BufferAccessStrategy object, or NULL for default strategy.
 *
 *	To ensure that no one else can pin the buffer before we do, we must
 *	return the buffer with the buffer header spinlock still held.  If
 *	*lock_held is set on exit, we have returned with the BufFreelistLock
 *	still held, as well; the caller must release that lock once the spinlock
 *	is dropped.  We do it that way because releasing the BufFreelistLock
 *	might awaken other processes, and it would be bad to do the associated
 *	kernel calls while holding the buffer header spinlock.
 */
volatile BufferDesc *
StrategyGetBuffer(BufferAccessStrategy strategy, bool *lock_held)
{
	volatile BufferDesc *buf;
	Latch	   *bgwriterLatch;
	// int			trycounter;

	if(CS3223DEBUG)
		elog(INFO, "StrategyGetBuffer");

	/*
	 * If given a strategy object, see whether it can select a buffer. We
	 * assume strategy objects don't need the BufFreelistLock.
	 */
	if (strategy != NULL)
	{
		buf = GetBufferFromRing(strategy);
		if (buf != NULL)
		{
			*lock_held = false;
			// Assert(StrategyControl->isUsed[buf->buf_id]);
			if(CS3223DEBUG)
				elog(INFO, "Buffer in pool. Exiting StrategyGetBuffer");
			return buf;
		}
	}

	/* Nope, so lock the freelist */
	*lock_held = true;
	LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);

	/*
	 * We count buffer allocation requests so that the bgwriter can estimate
	 * the rate of buffer consumption.  Note that buffers recycled by a
	 * strategy object are intentionally not counted here.
	 */
	StrategyControl->numBufferAllocs++;

	/*
	 * If bgwriterLatch is set, we need to waken the bgwriter, but we should
	 * not do so while holding BufFreelistLock; so release and re-grab.  This
	 * is annoyingly tedious, but it happens at most once per bgwriter cycle,
	 * so the performance hit is minimal.
	 */
	bgwriterLatch = StrategyControl->bgwriterLatch;
	if (bgwriterLatch)
	{
		StrategyControl->bgwriterLatch = NULL;
		LWLockRelease(BufFreelistLock);
		SetLatch(bgwriterLatch);
		LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);
	}

	/*
	 * Try to get a buffer from the freelist.  Note that the freeNext fields
	 * are considered to be protected by the BufFreelistLock not the
	 * individual buffer spinlocks, so it's OK to manipulate them without
	 * holding the spinlock.
	 */
	while (StrategyControl->firstFreeBuffer >= 0)
	{
		buf = &BufferDescriptors[StrategyControl->firstFreeBuffer];

		/*
		 * If the buffer is pinned or has a nonzero usage_count, we cannot use
		 * it; discard it and retry.  (This can only happen if VACUUM put a
		 * valid buffer in the freelist and then someone else used it before
		 * we got to it.  It's probably impossible altogether as of 8.3, but
		 * we'd better check anyway.)
		 */
		LockBufHdr(buf);
		Assert(buf->freeNext != FREENEXT_NOT_IN_LIST);
		Assert(!StrategyControl->isUsed[buf->buf_id]);

		/* Unconditionally remove buffer from freelist */
		RemoveFromFreelist(buf);
		buf->freeNext = FREENEXT_NOT_IN_LIST;

		// Add to victim list
		AddVictim(buf->buf_id);

		if (buf->refcount == 0 /* && buf->usage_count == 0 */ )
		{
			if (strategy != NULL)
				AddBufferToRing(strategy, buf);
			if(CS3223DEBUG)
				elog(INFO, "Buffer get from freelist. Exiting StrategyGetBuffer");
			return buf;
		}
		UnlockBufHdr(buf);
	}

	/* Nothing on the freelist, so run the LRU algorithm */
	// trycounter = NBuffers;
	for (;;)
	{
		if(StrategyControl->nextVictimBuffer < 0)
			elog(ERROR, "no unpinned buffers available");
			
		buf = &BufferDescriptors[StrategyControl->nextVictimBuffer];
		// Assert(StrategyControl->isUsed[buf->buf_id]);

		/* cs3223 */
		RemoveVictim(buf->buf_id);
		AddVictim(buf->buf_id);

		// Update completePasses as closely to clock sweep as possible
		// So as not to break bgwriter
		StrategyControl->numVictims++;
		if(StrategyControl->numVictims >= NBuffers){
			StrategyControl->completePasses+=StrategyControl->numVictims/NBuffers;
			StrategyControl->numVictims%=NBuffers;
		}

		// if (++StrategyControl->nextVictimBuffer >= NBuffers)
		// {
		//	StrategyControl->nextVictimBuffer = 0;
		//	StrategyControl->completePasses++;
		// }
		/* End of cs3223 */

		/*
		 * If the buffer is pinned or has a nonzero usage_count, we cannot use
		 * it; decrement the usage_count (unless pinned) and keep scanning.
		 */
		LockBufHdr(buf);
		// trycounter = NBuffers;
		if (buf->refcount == 0)
		{
			/* cs3223 */
			// if (buf->usage_count > 0)
			// {
			//	buf->usage_count--;
			//	trycounter = NBuffers;
			// }
			// else
			// {
			/* Found a usable buffer */
			buf->usage_count = 0;
			if (strategy != NULL)
				AddBufferToRing(strategy, buf);
			if(CS3223DEBUG)
				elog(INFO, "Buffer get from victim. Exiting StrategyGetBuffer");
			return buf;
			// }
			/* End of cs3223 */
		}
		// else if (--trycounter == 0)
		// {
			/*
			 * We've scanned all the buffers without making any state changes,
			 * so all the buffers are pinned (or were when we looked at them).
			 * We could hope that someone will free one eventually, but it's
			 * probably better to fail than to risk getting stuck in an
			 * infinite loop.
			 */
		//	UnlockBufHdr(buf);
		//	elog(ERROR, "no unpinned buffers available");
		// }
		UnlockBufHdr(buf);
	}
}

/*
 * StrategyFreeBuffer: put a buffer on the freelist
 */
void
StrategyFreeBuffer(volatile BufferDesc *buf)
{
	LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);

	/*
	 * It is possible that we are told to put something in the freelist that
	 * is already in it; don't screw up the list if so.
	 */
	if (buf->freeNext == FREENEXT_NOT_IN_LIST)
	{
		if(CS3223DEBUG)
			elog(INFO, "StrategyFreeBuffer");

		/* cs3223 */
		RemoveVictim(buf->buf_id);
		AddToFreelist(buf);

		if(CS3223DEBUG)
			elog(INFO, "Exiting StrategyFreeBuffer");
	}

	LWLockRelease(BufFreelistLock);
}

/*
 * StrategySyncStart -- tell BufferSync where to start syncing
 *
 * The result is the buffer index of the best buffer to sync first.
 * BufferSync() will proceed circularly around the buffer array from there.
 *
 * In addition, we return the completed-pass count (which is effectively
 * the higher-order bits of nextVictimBuffer) and the count of recent buffer
 * allocs if non-NULL pointers are passed.  The alloc count is reset after
 * being read.
 */
int
StrategySyncStart(uint32 *complete_passes, uint32 *num_buf_alloc)
{
	elog(INFO, "LOG: StrategySyncStart");
//	int			result;

//	LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);
//	result = StrategyControl->nextVictimBuffer;
//	if (complete_passes)
//		*complete_passes = StrategyControl->completePasses;
//	if (num_buf_alloc)
//	{
//		*num_buf_alloc = StrategyControl->numBufferAllocs;
//		StrategyControl->numBufferAllocs = 0;
//	}
//	LWLockRelease(BufFreelistLock);
	*num_buf_alloc = StrategyControl->numBufferAllocs;
	StrategyControl->numBufferAllocs = 0;
	*complete_passes += 1;
	return 0;
}

/*
 * StrategyNotifyBgWriter -- set or clear allocation notification latch
 *
 * If bgwriterLatch isn't NULL, the next invocation of StrategyGetBuffer will
 * set that latch.  Pass NULL to clear the pending notification before it
 * happens.  This feature is used by the bgwriter process to wake itself up
 * from hibernation, and is not meant for anybody else to use.
 */
void
StrategyNotifyBgWriter(Latch *bgwriterLatch)
{
	/*
	 * We acquire the BufFreelistLock just to ensure that the store appears
	 * atomic to StrategyGetBuffer.  The bgwriter should call this rather
	 * infrequently, so there's no performance penalty from being safe.
	 */
	LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);
	StrategyControl->bgwriterLatch = bgwriterLatch;
	LWLockRelease(BufFreelistLock);
}


/*
 * StrategyShmemSize
 *
 * estimate the size of shared memory used by the freelist-related structures.
 *
 * Note: for somewhat historical reasons, the buffer lookup hashtable size
 * is also determined here.
 */
Size
StrategyShmemSize(void)
{
	Size		size = 0;

	/* size of lookup hash table ... see comment in StrategyInitialize */
	size = add_size(size, BufTableShmemSize(NBuffers + NUM_BUFFER_PARTITIONS));

	/* size of the shared replacement strategy control block */
	size = add_size(size, MAXALIGN(sizeof(BufferStrategyControl)));

	size = add_size(size, mul_size(2*NBuffers, sizeof(int)));

	size = add_size(size, mul_size(NBuffers, sizeof(bool)));

	return size;
}

/*
 * StrategyInitialize -- initialize the buffer cache replacement
 *		strategy.
 *
 * Assumes: All of the buffers are already built into a linked list.
 *		Only called by postmaster and only during initialization.
 */
void
StrategyInitialize(bool init)
{
	bool		found;

	elog(INFO, "StrategyInitialize");

	/*
	 * Initialize the shared buffer lookup hashtable.
	 *
	 * Since we can't tolerate running out of lookup table entries, we must be
	 * sure to specify an adequate table size here.  The maximum steady-state
	 * usage is of course NBuffers entries, but BufferAlloc() tries to insert
	 * a new entry before deleting the old.  In principle this could be
	 * happening in each partition concurrently, so we could need as many as
	 * NBuffers + NUM_BUFFER_PARTITIONS entries.
	 */
	InitBufTable(NBuffers + NUM_BUFFER_PARTITIONS);

	/*
	 * Get or create the shared strategy control block
	 */
	StrategyControl = (BufferStrategyControl *)
		ShmemInitStruct("Buffer Strategy Status",
						sizeof(BufferStrategyControl),
						&found);
	if (!found)
	{
		elog(INFO, "Init StrategyControl");
		/*
		 * Only done once, usually in postmaster
		 */
		Assert(init);

		/*
		 * Grab the whole linked list of free buffers for our strategy. We
		 * assume it was previously set up by InitBufferPool().
		 */
		StrategyControl->firstFreeBuffer = 0;
		StrategyControl->lastFreeBuffer = NBuffers - 1;

		/* Initially no victim */
		StrategyControl->nextVictimBuffer = VICTIM_NOT_IN_LIST;

		/* Clear statistics */
		StrategyControl->completePasses = 0;
		StrategyControl->numBufferAllocs = 0;

		/* No pending notification */
		StrategyControl->bgwriterLatch = NULL;

		/* cs3223 */
		StrategyControl->numVictims = 0;
		StrategyControl->mrubuf = MRU_NOT_IN_LIST;
		StrategyControl->bufprev = (int *)malloc(NBuffers*sizeof(int));
		StrategyControl->bufnext = (int *)malloc(NBuffers*sizeof(int));
		StrategyControl->isUsed = (bool *)calloc(NBuffers, sizeof(bool));
		int i;
		for(i=0; i<NBuffers; i++)
			StrategyControl->bufprev[i] = StrategyControl->bufnext[i] = VICTIM_NOT_IN_LIST;

		for(i=0; i<NBuffers; i++)
			Assert(!StrategyControl->isUsed[i]);
		/* End of cs3223 */
	}
	else
		Assert(!init);
}


/* ----------------------------------------------------------------
 *				Backend-private buffer ring management
 * ----------------------------------------------------------------
 */


/*
 * GetAccessStrategy -- create a BufferAccessStrategy object
 *
 * The object is allocated in the current memory context.
 */
BufferAccessStrategy
GetAccessStrategy(BufferAccessStrategyType btype)
{
	BufferAccessStrategy strategy;
	int			ring_size;

	/*
	 * Select ring size to use.  See buffer/README for rationales.
	 *
	 * Note: if you change the ring size for BAS_BULKREAD, see also
	 * SYNC_SCAN_REPORT_INTERVAL in access/heap/syncscan.c.
	 */
	switch (btype)
	{
		case BAS_NORMAL:
			/* if someone asks for NORMAL, just give 'em a "default" object */
			return NULL;

		case BAS_BULKREAD:
			ring_size = 256 * 1024 / BLCKSZ;
			break;
		case BAS_BULKWRITE:
			ring_size = 16 * 1024 * 1024 / BLCKSZ;
			break;
		case BAS_VACUUM:
			ring_size = 256 * 1024 / BLCKSZ;
			break;

		default:
			elog(ERROR, "unrecognized buffer access strategy: %d",
				 (int) btype);
			return NULL;		/* keep compiler quiet */
	}

	/* Make sure ring isn't an undue fraction of shared buffers */
	ring_size = Min(NBuffers / 8, ring_size);

	/* Allocate the object and initialize all elements to zeroes */
	strategy = (BufferAccessStrategy)
		palloc0(offsetof(BufferAccessStrategyData, buffers) +
				ring_size * sizeof(Buffer));

	/* Set fields that don't start out zero */
	strategy->btype = btype;
	strategy->ring_size = ring_size;

	return strategy;
}

/*
 * FreeAccessStrategy -- release a BufferAccessStrategy object
 *
 * A simple pfree would do at the moment, but we would prefer that callers
 * don't assume that much about the representation of BufferAccessStrategy.
 */
void
FreeAccessStrategy(BufferAccessStrategy strategy)
{
	/* don't crash if called on a "default" strategy */
	if (strategy != NULL)
		pfree(strategy);
}

/*
 * GetBufferFromRing -- returns a buffer from the ring, or NULL if the
 *		ring is empty.
 *
 * The bufhdr spin lock is held on the returned buffer.
 */
static volatile BufferDesc *
GetBufferFromRing(BufferAccessStrategy strategy)
{
	volatile BufferDesc *buf;
	Buffer		bufnum;

	/* Advance to next ring slot */
	if (++strategy->current >= strategy->ring_size)
		strategy->current = 0;

	/*
	 * If the slot hasn't been filled yet, tell the caller to allocate a new
	 * buffer with the normal allocation strategy.  He will then fill this
	 * slot by calling AddBufferToRing with the new buffer.
	 */
	bufnum = strategy->buffers[strategy->current];
	if (bufnum == InvalidBuffer)
	{
		strategy->current_was_in_ring = false;
		return NULL;
	}

	/*
	 * If the buffer is pinned we cannot use it under any circumstances.
	 *
	 * If usage_count is 0 or 1 then the buffer is fair game (we expect 1,
	 * since our own previous usage of the ring element would have left it
	 * there, but it might've been decremented by clock sweep since then). A
	 * higher usage_count indicates someone else has touched the buffer, so we
	 * shouldn't re-use it.
	 */
	buf = &BufferDescriptors[bufnum - 1];
	LockBufHdr(buf);
	if (buf->refcount == 0 && buf->usage_count <= 1)
	{
		strategy->current_was_in_ring = true;
		return buf;
	}
	UnlockBufHdr(buf);

	/*
	 * Tell caller to allocate a new buffer with the normal allocation
	 * strategy.  He'll then replace this ring element via AddBufferToRing.
	 */
	strategy->current_was_in_ring = false;
	return NULL;
}

/*
 * AddBufferToRing -- add a buffer to the buffer ring
 *
 * Caller must hold the buffer header spinlock on the buffer.  Since this
 * is called with the spinlock held, it had better be quite cheap.
 */
static void
AddBufferToRing(BufferAccessStrategy strategy, volatile BufferDesc *buf)
{
	strategy->buffers[strategy->current] = BufferDescriptorGetBuffer(buf);
}

/*
 * StrategyRejectBuffer -- consider rejecting a dirty buffer
 *
 * When a nondefault strategy is used, the buffer manager calls this function
 * when it turns out that the buffer selected by StrategyGetBuffer needs to
 * be written out and doing so would require flushing WAL too.  This gives us
 * a chance to choose a different victim.
 *
 * Returns true if buffer manager should ask for a new victim, and false
 * if this buffer should be written and re-used.
 */
bool
StrategyRejectBuffer(BufferAccessStrategy strategy, volatile BufferDesc *buf)
{
	/* We only do this in bulkread mode */
	if (strategy->btype != BAS_BULKREAD)
		return false;

	/* Don't muck with behavior of normal buffer-replacement strategy */
	if (!strategy->current_was_in_ring ||
	  strategy->buffers[strategy->current] != BufferDescriptorGetBuffer(buf))
		return false;

	/*
	 * Remove the dirty buffer from the ring; necessary to prevent infinite
	 * loop if all ring members are dirty.
	 */
	strategy->buffers[strategy->current] = InvalidBuffer;

	return true;
}
