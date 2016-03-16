\echo ---------------- Question 1: IS-b plan
SET enable_seqscan = OFF;
SET enable_bitmapscan = OFF;
EXPLAIN (ANALYZE,  BUFFERS) SELECT * FROM r WHERE b=9;
