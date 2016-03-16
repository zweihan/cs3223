\echo ---------------- Question 1: BIS-b plan
SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;
EXPLAIN (ANALYZE,  BUFFERS) SELECT * FROM r WHERE b=9;
