create table test (x int, y int(4), z strlst);
insert into test values(1, [1,2,3], ['a','b','c']);
insert into test values(1, [1,2,3,4], ['a','b','c','d']);
insert into test values(2, [1,2,3,4,5], [1,2,3]);
select * from test;
drop table test;
