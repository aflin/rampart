-- Tests Bug 4588: JSON Library

set betafeatures = 'json';

-- ======================================================================
create table test (id int, Json varchar(20), Tags strlst);
insert into test (id, Json) values (1, '{"info":{"type":2,"address":{"town":"Cleveland","county":"Cuyahoga","country":"United States"},"tags":["Sport","Football"]},"type":"Basic"}');
insert into test (id, Json) values (2, '{"info":{"type":1,"address":{"town":"Bristol","county":"Avon","country":"England"},"tags":["Sport","Water polo"]},"type":"Basic"}');

set varchar2strlstmode = 'json';

update test set Tags = Json.$.info.tags;

select Tags from test;

set strlst2varcharmode = 'json';

select Tags from test;

drop table test;
