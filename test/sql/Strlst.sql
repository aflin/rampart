-- Test String lists

-- Defaults that changed in version 7:
set varchartostrlstsep = 'lastchar';

-- test016
-- Basic test

create table test (x varchar(10), y strlst);

insert into test values ('1,2,3,','1,2,3,');
insert into test values ('4|5|6|','4|5|6|');

select * from test;

drop table test;

-- test208
-- test with a single character strlst, which has '2' as delimiter.
-- Make sure search doesn't return it.

create table test (x varchar(10), y strlst);

insert into test values ('2,','2');
insert into test values ('1,2,3,','1,2,3,');
insert into test values ('4|5|6|','4|5|6|');

select * from test;

select * from test where '2' in y;
select * from test where '4' in y;
select * from test where '3' in y;
drop table test;

-- test243
-- multivaluetomultirow

create table test(x strlst);
insert into test values('a,b,c,d,');
insert into test values('e,f,g,h,');
insert into test values('b,');
insert into test values('a,c,');
create index ix1 on test(x);
select x from test group by x;          -- multivaluetomultirow default off v7+
set multivaluetomultirow=1;
select x from test group by x;          -- KNG 20090424 Bug 2598
select x from test group by x GroupByRenamed;  -- Bug 5079
set verbose=2;          -- ensure we see it use the index
set bubble=0;           -- ensure it says it's opening index
select * from test where 'b' in x;
select * from test where x in ('b');
select * from test where 'b' = x;
select * from test where x = 'b';
select * from test where x != 'b';  -- KNG 20081107 Bug 2398
select * from test where 'b%' matches x;
select * from test where x matches 'b%';
set verbose=0;
select x from test group by x;          -- Bug 2598 w/bubble=0
drop table test;

set varchartostrlstsep = 'create';
