\echo ---------------- Question 5: IS-b;

SET enable_seqscan = OFF;
SET enable_bitmapscan = OFF;
begin;
drop index cb_idx;
EXPLAIN (ANALYZE,  BUFFERS) select a from r where b<1 and c>1;
rollback;
