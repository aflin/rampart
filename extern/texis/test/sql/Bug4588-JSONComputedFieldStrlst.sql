-- Tests Bug 4588: JSON Library



-- ======================================================================
create table test (id int, Json varchar(20), Country varchar(20));
insert into test values (1, '{"info":{"type":2,"address":{"town":"Cleveland","county":"Cuyahoga","country":"United States"},"tags":["Sport","Football"]},"type":"Basic"}', 'United States');
insert into test values (2, '{"info":{"type":1,"address":{"town":"Bristol","county":"Avon","country":"England"},"tags":["Sport","Water polo"]},"type":"Basic"}', 'England');
insert into test values (3, '{"name":"John","skills":["C#","SQL"]}', '');
--
--

select id, Json.$.skills[*] from test;
select id, Json.$.name from test where Json.$.skills[*] = 'SQL';
select id, Json.$.name from test where 'SQL' IN Json.$.skills[*];

create index ix4 on test(Json.$.skills[*]);

select * from SYSINDEX where TBNAME = 'test';

set verbose = 1;

select id, Json.$.name from test where Json.$.skills[*] = 'SQL';
select id, Json.$.name from test where 'SQL' IN Json.$.skills[*];

set indexaccess = 1;

select * from ix4;

drop table test;
