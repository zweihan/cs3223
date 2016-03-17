\echo ---------------- Question 6: BIS-cb;

SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;

explain (analyze, buffers) select * from r where b > 9 and c=10;
