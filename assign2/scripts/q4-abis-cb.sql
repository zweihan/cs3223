\echo ---------------- Question 4: ABIS-cb;

SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;
begin;
drop index cb_idx;
EXPLAIN (ANALYZE,  BUFFERS) SELECT * FROM r WHERE b = 9 AND c = 10;
rollback;
