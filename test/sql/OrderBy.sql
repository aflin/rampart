-- test005
-- This tests the simple order by cases, ordering on one field only.

create table test (x char(10), y int);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
select * from test order by x;
select * from test order by y;
select * from test order by x desc;
select * from test order by y desc;
drop table test;

-- test006
-- This tests the simple order by cases, ordering on one field only.
-- This version also includes indices built on the fly.

create table test (x char(10), y int);
create index ix_1 on test(x);
create index ix_2 on test(y);
create index ix_3 on test(x desc);
create index ix_4 on test(y desc);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
select * from test order by x;
select * from test order by y;
select * from test order by x desc;
select * from test order by y desc;
drop table test;

-- test007
-- This tests the simple order by cases, ordering on one field only.
-- Some of these have indices that can be used.

create table test (x char(10), y int);
create index ix_1 on test(x);
create index ix_4 on test(y desc);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
select * from test order by x;
select * from test order by y;
select * from test order by x desc;
select * from test order by y desc;
drop table test;

-- test009
-- This tests the simple order by cases, ordering on one field only.
-- Some of these have indices that can be used.

create table test (x char(10), y unsigned int);
create inverted index ix_4 on test(y desc);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
select * from test order by x;
select * from test order by y;
select * from test order by x desc;
select * from test order by y desc;
drop table test;

-- test010
-- This tests the simple order by cases, ordering on one field only.
-- Some of these have indices that can be used.

create table test (x char(10), y unsigned int);
create index ix_3 on test(y);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
create inverted index ix_4 on test(y desc);
select * from test where y > 3 order by y desc;
drop table test;

-- test011
-- This tests the simple order by cases, ordering on one field only.
-- Some of these have indices that can be used.
-- This tests creating inverted index on the fly.

create table test (x char(10), y unsigned int);
create index ix_3 on test(y);
create inverted index ix_4 on test(y desc);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
select * from test where y > 3 order by y desc;
drop table test;

-- test017

-- This tests the simple order by cases, ordering on one field only.
-- Some of these have indices that can be used.

create table test (x char(10), y unsigned int);
create index ix_3 on test(y);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
create inverted index ix_4 on test(y desc);
select * from test where y between 2 and 4 order by y desc;
select * from test where y = 3 ;
drop table test;

-- test020
-- This tests the simple order by cases, ordering on one field only.
-- This version also includes indices built on the fly.

create table test (x char(10), y int);
create unique index ix_1 on test(x);
-- create unique index ix_2 on test(y);
insert into test values('d', 3);
insert into test values('b', 4);
insert into test values('c', 2);
insert into test values('f', 5);
insert into test values('a', 6);
insert into test values('e', 1);
insert into test values('a', 6);
insert into test values('e', 1);
select * from test order by x;
select * from test order by y;
select * from test order by x desc;
select * from test order by y desc;
drop table test;
