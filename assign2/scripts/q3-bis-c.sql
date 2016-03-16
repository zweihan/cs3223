\echo ---------------- Question 3 bis-c

SET enable_seqscan = ON;
SET enable_bitmapscan = ON;

begin;
drop index cb_idx;
\echo --------------bis-c
EXPLAIN (ANALYZE,  BUFFERS) SELECT b from r where c>15;
rollback;
