\echo ---------------- Question 6: seqscan;

SET enable_seqscan = ON;
SET enable_bitmapscan = OFF;
set enable_indexscan = OFF;

explain (analyze, buffers) select * from r where b > 9 and c=10;
