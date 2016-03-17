\echo ---------------- Question 6: is-c;

SET enable_seqscan = OFF;
SET enable_bitmapscan = OFF;
set enable_indexscan = ON;
begin;
drop index cb_idx;
drop index b_idx;
explain (analyze, buffers) select * from r where b > 9 and c=10;
rollback;
