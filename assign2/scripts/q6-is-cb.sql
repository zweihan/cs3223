\echo ---------------- Question 6: is-cb;

SET enable_seqscan = OFF;
SET enable_bitmapscan = OFF;
set enable_indexscan = ON;

explain (analyze, buffers) select * from r where b > 9 and c=10;
