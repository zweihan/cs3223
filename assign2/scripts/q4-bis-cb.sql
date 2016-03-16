\echo ---------------- Question 4: BIS-cb;

SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;
begin;
drop index c_idx, b_idx;
EXPLAIN (ANALYZE,  BUFFERS) SELECT * FROM r WHERE b = 9 AND c = 10;
rollback;
