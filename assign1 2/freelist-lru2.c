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


/*
 * The shared freelist control information.
 */
typedef struct
{
	/* Clock sweep hand: index of next buffer to consider grabbing */
	int			nextVictimBuffer;

	int			firstFreeBuffer;	/* Head of list of unused buffers */
	// int			lastFreeBuffer; /* Tail of list of unused buffers */

	int *s2prev;
	int *s2next;
	int *s1prev;
	int *s1next;
	int *numAccess;
	int s2mrubuf;
	int s1mrubuf;
	int s2lrubuf;
	int s1lrubuf;

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
// Adds buffer with buf_id to front of list s2
// Note: buf_id>=NBuffers is the 2nd last access of buf_id-NBuffers.
static void AddToS2(int buf_id){
	StrategyControl->s2next[buf_id] = StrategyControl->s2mrubuf;
	if(StrategyControl->s2mrubuf >= 0)
		StrategyControl->s2prev[StrategyControl->s2mrubuf] = buf_id;
	StrategyControl->s2mrubuf = buf_id;

	// If s2 was empty
	if(StrategyControl->s2lrubuf < 0)
		StrategyControl->s2lrubuf = buf_id;
};

// Removes buffer from s2
// Note: buf_id scheme same as in AddToS2
static void RemoveFromS2(int buf_id){

	// Link up both neighbours
	if(StrategyControl->s2prev[buf_id] >= 0)
		StrategyControl->s2next[StrategyControl->s2prev[buf_id]] = StrategyControl->s2next[buf_id];
	if(StrategyControl->s2next[buf_id] >= 0)
		StrategyControl->s2prev[StrategyControl->s2next[buf_id]] = StrategyControl->s2prev[buf_id];

	// If is lrubuf, update to its previous neighbour
	if(buf_id == StrategyControl->s2lrubuf)
		StrategyControl->s2lrubuf = StrategyControl->s2prev[buf_id];

	// If is mrubuf, update its next neighbour
	if(buf_id == StrategyControl->s2mrubuf)
		StrategyControl->s2mrubuf = StrategyControl->s2next[buf_id];

	// Clear pointers
	StrategyControl->s2prev[buf_id] = StrategyControl->s2next[buf_id] = -1;

};

// Add buffer to S1 as mrubuf
static void AddToS1(int buf_id){
	StrategyControl->s1next[buf_id] = StrategyControl->s1mrubuf;
	if(StrategyControl->s1mrubuf >= 0)
		StrategyControl->s1prev[StrategyControl->s1mrubuf] = buf_id;
	StrategyControl->s1mrubuf = buf_id;

	// If S1 was empty, make this buffer the LRU buffer
	if(StrategyControl->s1lrubuf < 0)
		StrategyControl->s1lrubuf = buf_id;
};

// Remove buffer from S1
static void RemoveFromS1(int buf_id){

	// Link up both neighbours
	if(StrategyControl->s1prev[buf_id] >= 0)
		StrategyControl->s1next[StrategyControl->s1prev[buf_id]] = StrategyControl->s1next[buf_id];
	if(StrategyControl->s1next[buf_id] >= 0)
		StrategyControl->s1prev[StrategyControl->s1next[buf_id]] = StrategyControl->s1prev[buf_id];

	// If is mrubuf, update to next neighbour
	if(buf_id == StrategyControl->s1mrubuf)
		StrategyControl->s1mrubuf = StrategyControl->s1next[buf_id];

	// If is lrubuf, update to previous neighbour
	if(buf_id == StrategyControl->s1lrubuf)
		StrategyControl->s1lrubuf = StrategyControl->s1prev[buf_id];

	// Clear pointers
	StrategyControl->s1prev[buf_id] = StrategyControl->s1next[buf_id] = -1;

};

// Adds buffer to head of freelist
// Pre-condition: Lock on freelist must be acquired
static void AddToFreelist(volatile BufferDesc *buf){
	// LockBufHdr(buf);
	// elog(INFO, "Head of freelist: %d", StrategyControl->firstFreeBuffer);
	buf->freeNext = StrategyControl->firstFreeBuffer;
	StrategyControl->firstFreeBuffer = buf->buf_id;
	buf->usage_count = 0;
	// elog(INFO, "New Head of freelist: %d", StrategyControl->firstFreeBuffer);
	// UnlockBufHdr(buf);
};

// Removes buffer from head of freelist
// Pre-condition: Lock to freelist must be acquired
static void RemoveFromFreelist(volatile BufferDesc *buf){
	// LockBufHdr(buf);
	StrategyControl->firstFreeBuffer = buf->freeNext;
	buf->freeNext	= FREENEXT_NOT_IN_LIST;;
	// UnlockBufHdr(buf);
};

// StrategyUpdateAccessedBuffer
// Called by bufmgr when a buffer page is accessed.
// Adjusts the position of buffer (identified by buf_id) in the LRU stack if delete is false;
// otherwise, delete buffer buf_id from the LRU stack.
void
StrategyUpdateAccessedBuffer(int buf_id, bool delete)
{
	// Check for invalid buf_id
	if(buf_id < 0 || buf_id >= NBuffers)
		return;

	// elog(LOG, "StrategyUpdateAccessedBuffer %d %d", buf_id, (int)delete);

	Assert(StrategyControl->numAccess[buf_id] > 0);

	if(delete){
		RemoveFromS1(buf_id);
		RemoveFromS2(buf_id);
		RemoveFromS2(buf_id+NBuffers);
		StrategyControl->numAccess[buf_id] = 0;
		LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);
		volatile BufferDesc *buf = &BufferDescriptors[buf_id];
		LockBufHdr(buf);
		AddToFreelist(buf);
		UnlockBufHdr(buf);
		LWLockRelease(BufFreelistLock);
	} else{
		Assert(StrategyControl->numAccess[buf_id] > 0);

		// Make last use the 2nd last use
		int buf_id2 = buf_id+NBuffers;
		RemoveFromS1(buf_id);
		RemoveFromS2(buf_id2);
		StrategyControl->s2prev[buf_id2] = StrategyControl->s2prev[buf_id];
		StrategyControl->s2next[buf_id2] = StrategyControl->s2next[buf_id];
		if(StrategyControl->s2prev[buf_id] >= 0)
			StrategyControl->s2next[StrategyControl->s2prev[buf_id]] = buf_id2;
		if(StrategyControl->s2next[buf_id] >= 0)
			StrategyControl->s2prev[StrategyControl->s2next[buf_id]] = buf_id2;
		if(buf_id == StrategyControl->s2lrubuf)
			StrategyControl->s2lrubuf = buf_id2;
		if(buf_id == StrategyControl->s2mrubuf)
			StrategyControl->s2mrubuf = buf_id2;

		// Clear pointers
		StrategyControl->s2prev[buf_id] = StrategyControl->s2next[buf_id] = -1;

		AddToS2(buf_id);

		if(StrategyControl->numAccess[buf_id] < 2)
			StrategyControl->numAccess[buf_id]++;
	}
	// elog(LOG, "End StrategyUpdateAccessedBuffer");
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

	// elog(LOG, "StrategyGetBuffer");

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
			// elog(LOG, "Buffer exists");
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
		// Assert(buf->freeNext != FREENEXT_NOT_IN_LIST);

		/*
		 * If the buffer is pinned or has a nonzero usage_count, we cannot use
		 * it; discard it and retry.  (This can only happen if VACUUM put a
		 * valid buffer in the freelist and then someone else used it before
		 * we got to it.  It's probably impossible altogether as of 8.3, but
		 * we'd better check anyway.)
		 */
		LockBufHdr(buf);

		/* Unconditionally remove buffer from freelist */
		RemoveFromFreelist(buf);
		AddToS1(buf->buf_id);
		AddToS2(buf->buf_id);
		StrategyControl->numAccess[buf->buf_id] = 1;

		if (buf->refcount == 0)
		{
			if (strategy != NULL)
				AddBufferToRing(strategy, buf);
			// elog(LOG, "Using free buffer %d", buf->buf_id);
			return buf;
		}
		UnlockBufHdr(buf);
	}

	/* Nothing on the freelist, so run the LRU-2 algorithm */
	int victim;

	// Try to find victim in S1 first
	for(victim=StrategyControl->s1lrubuf; victim>=0; victim=StrategyControl->s1prev[victim]){
		Assert(victim >= 0 && victim < NBuffers);
		buf = &BufferDescriptors[victim];

		LockBufHdr(buf);
		if(buf->refcount == 0){	/* Found a usable buffer */
			// elog(LOG, "Using victim %d from S1", victim);
			RemoveFromS1(buf->buf_id);
			RemoveFromS2(buf->buf_id);
			RemoveFromS2(buf->buf_id+NBuffers);
			AddToS1(buf->buf_id);
			AddToS2(buf->buf_id);
			StrategyControl->numAccess[buf->buf_id] = 1;

			if (strategy != NULL)
				AddBufferToRing(strategy, buf);
			return buf;
		}
		// elog(LOG, "Skipping buffer %d from S1", victim);
		UnlockBufHdr(buf);
	}

	// Try to look for victim in S2
	for(victim=StrategyControl->s2lrubuf; victim>=0; victim=StrategyControl->s2prev[victim]){
		if(victim < NBuffers)	// We don't want the last accessed instance
			continue;

		// elog(LOG, "S2 victim: %d", victim);
		buf = &BufferDescriptors[victim-NBuffers];

		LockBufHdr(buf);
		if(buf->refcount == 0){	/* Found a usable buffer */
			// elog(LOG, "Using victim buffer %d from S2", buf->buf_id);
			RemoveFromS1(buf->buf_id);
			RemoveFromS2(buf->buf_id);
			RemoveFromS2(victim);
			AddToS1(buf->buf_id);
			AddToS2(buf->buf_id);
			StrategyControl->numAccess[buf->buf_id] = 1;

			if (strategy != NULL)
				AddBufferToRing(strategy, buf);
			return buf;
		}
		// elog(LOG, "Skipping bufffer %d from S2", buf->buf_id);
		UnlockBufHdr(buf);
	}

	elog(ERROR, "no unpinned buffers available");
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
		// elog(LOG, "StrategyFreeBuffer");
		RemoveFromS1(buf->buf_id);
		RemoveFromS2(buf->buf_id);
		RemoveFromS2(NBuffers+buf->buf_id);
		LockBufHdr(buf);
		AddToFreelist(buf);
		UnlockBufHdr(buf);
		StrategyControl->numAccess[buf->buf_id] = 0;
		// elog(LOG, "End StrategyFreeBuffer");
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
	int			result;

	LWLockAcquire(BufFreelistLock, LW_EXCLUSIVE);
	result = StrategyControl->nextVictimBuffer;
	if (complete_passes)
		*complete_passes = StrategyControl->completePasses;
	if (num_buf_alloc)
	{
		*num_buf_alloc = StrategyControl->numBufferAllocs;
		StrategyControl->numBufferAllocs = 0;
	}
	LWLockRelease(BufFreelistLock);
	return result;
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

	size = add_size(size, mul_size(sizeof(int), 7*NBuffers));

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
	bool		numAccessFound;
	bool 		s2prevFound;
	bool		s2nextFound;
	bool		s1prevFound;
	bool		s1nextFound;

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

	int *s2prev = (int *)ShmemInitStruct(" S2 prev pointers",
		sizeof(int)*NBuffers<<1, &s2prevFound);
	int *s2next = (int *)ShmemInitStruct("S2 next pointers",
		sizeof(int)*NBuffers<<1, &s2nextFound);
	int *s1prev = (int *)ShmemInitStruct("S1 prev pointers",
		sizeof(int)*NBuffers, &s1prevFound);
	int *s1next = (int *)ShmemInitStruct("S1 next pointers",
		sizeof(int)*NBuffers, &s1nextFound);
	int *numAccess = (int *)ShmemInitStruct("numAccess",
		sizeof(int)*NBuffers, &numAccessFound);

	if (!found && !s2prevFound && !s2nextFound && !s1prevFound && !s1nextFound && !numAccessFound)
	{
		/*
		 * Only done once, usually in postmaster
		 */
		Assert(init);

		/*
		 * Grab the whole linked list of free buffers for our strategy. We
		 * assume it was previously set up by InitBufferPool().
		 */
		StrategyControl->firstFreeBuffer = 0;

		/* Initialize the clock sweep pointer */
		StrategyControl->nextVictimBuffer = 0;

		/* Clear statistics */
		StrategyControl->completePasses = 0;
		StrategyControl->numBufferAllocs = 0;

		/* No pending notification */
		StrategyControl->bgwriterLatch = NULL;

		int i;
		StrategyControl->numAccess = numAccess;
		for(i=0; i<NBuffers; i++)
			StrategyControl->numAccess[i] = 0;

		StrategyControl->s2prev = s2prev;
		StrategyControl->s2next = s2next;
		for(i=0; i<(NBuffers<<1); i++)
			StrategyControl->s2prev[i] = StrategyControl->s2next[i] = -1;

		StrategyControl->s1prev = s1prev;
		StrategyControl->s1next = s1next;
		for(i=0; i<NBuffers; i++)
			StrategyControl->s1prev[i] = StrategyControl->s1next[i] = -1;

		StrategyControl->s1mrubuf = StrategyControl->s1lrubuf = -1;
		StrategyControl->s2mrubuf = StrategyControl->s2lrubuf = -1;
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
