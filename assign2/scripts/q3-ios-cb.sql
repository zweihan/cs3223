\echo ---------------- Question 3: IOS-cb

SET enable_seqscan = ON;
SET enable_bitmapscan = ON;
EXPLAIN (ANALYZE, BUFFERS) SELECT b from r where c>15;
