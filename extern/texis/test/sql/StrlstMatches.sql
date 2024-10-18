-- Test String lists

-- Defaults that changed in version 7:
set varchartostrlstsep = 'lastchar';

-- test245 strlst and matches

create table test(x strlst);
insert into test values('a,b,c,d,');
insert into test values('e,f,g,h,');
insert into test values('b,');
insert into test values('a,c,');
create index ix1 on test(x);
select * from test where x matches 'b%';
select * from test where 'b%' matches x;
drop table test;
