\echo ---------------- Question 6: bis-b;

SET enable_seqscan = OFF;
SET enable_bitmapscan = ON;
set enable_indexscan = ON;
begin;
drop index cb_idx;
drop index c_idx;
explain (analyze, buffers) select * from r where b > 9 and c=10;
rollback;
