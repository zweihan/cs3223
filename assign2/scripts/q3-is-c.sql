\echo ---------------- Question 3: IS-c

SET enable_seqscan = OFF;
SET enable_bitmapscan = OFF;
begin;
drop index cb_idx;
EXPLAIN (ANALYZE,  BUFFERS) SELECT b from r where c>15;
rollback;
