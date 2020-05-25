-- Tests Bug 4588: JSON Library

set betafeatures = 'json';

-- ======================================================================
create table test (id int, Json varchar(20), Country varchar(20));
insert into test values (1, '{"info":{"type":2,"address":{"town":"Cleveland","county":"Cuyahoga","country":"United States"},"tags":["Sport","Football"]},"type":"Basic"}', 'United States');
insert into test values (2, '{"info":{"type":1,"address":{"town":"Bristol","county":"Avon","country":"England"},"tags":["Sport","Water polo"]},"type":"Basic"}', 'England');
insert into test values (3, '{"name":"John","skills":["C#","SQL"]}', '');
--
-- 
select Json.$.info.type from test;
select Json.$.info.address.town from test;
select Json.$.info."address" from test;
select Json.$.info.tags from test;
select Json.$.info.type[0] from test;
select Json.$.info.none from test;
select Json.$.info.tags[1] from test;
select Json.$.info.address.town Town from test order by Json.$.info.type;
select Json.$.info.address.town Town from test order by Json.$.info.address.country;
select Json.$.info.address.town Town from test where Json.$.info.address.country = 'England' order by Json.$.info.address.country;

create index ix1 on test(Json.$.info.type);
create index ix2 on test(Json.$.info.address.country);
create index ix3 on test(Country);

select * from SYSINDEX where TBNAME = 'test' order by NAME, TYPE;

set verbose = 1;

select Json.$.info.type Type, Json.$.info.address.town Town from test order by Json.$.info.type;
select Json.$.info.address.country Country, Json.$.info.address.town Town from test where Json.$.info.address.country = 'England' order by Json.$.info.address.country;
select Country, Json.$.info.address.town Town from test where Country = 'England' order by Country;

set indexaccess = 1;

select * from ix1;
select * from ix2;
select * from ix3;

drop table test;
