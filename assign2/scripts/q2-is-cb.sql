\echo ---------------Question 2: is-cb
begin;
drop index c_idx;
SET enable_seqscan = OFF;
SET enable_bitmapscan = OFF;

EXPLAIN (ANALYZE,  BUFFERS) SELECT * FROM r WHERE c=10;
rollback;
