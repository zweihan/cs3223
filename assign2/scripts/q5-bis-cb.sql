\echo ---------------- Question 5: BIS-cb;

SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;
begin;
drop index c_idx, b_idx;
EXPLAIN (ANALYZE,  BUFFERS) select a from r where b<1 and c>1;
rollback;
