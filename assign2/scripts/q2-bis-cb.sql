\echo ---------------- Question 2: bis-cb
begin;
drop index c_idx;
SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;

EXPLAIN (ANALYZE, BUFFERS) SELECT * from r where c=10;
rollback;
