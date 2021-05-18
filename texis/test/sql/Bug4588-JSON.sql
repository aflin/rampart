-- Tests Bug 4588: JSON Library

-- ======================================================================



create table test (id int, Json varchar(20));
insert into test values (1, '{"info":{"type":2,"address":{"town":"Cleveland","county":"Cuyahoga","country":"United States"},"tags":["Sport","Football"]},"type":"Basic"}');
insert into test values (2, '{"info":{"type":1,"address":{"town":"Bristol","county":"Avon","country":"England"},"tags":["Sport","Water polo"]},"type":"Basic"}');
insert into test values (3, '{"name":"John","skills":["C#","SQL"]}');
--
-- ISJSON
--     bad string
select isjson('foo');
--     good string
select isjson(Json) from test;
--
-- JSON_VALUE
select json_value(Json, '$') from test;
select json_value(Json, '$.info.type') from test;
select json_value(Json, '$.info.address.town') from test;
select json_value(Json, '$.info."address"') from test;
select json_value(Json, '$.info.tags') from test;
select json_value(Json, '$.info.type[0]') from test;
select json_value(Json, '$.info.none') from test;
select json_value(Json, '$.info.tags[1]') from test;
select json_value(Json, '$.info.address.town') Town from test order by json_value(Json, '$.info.type');
select json_value(Json, '$.info.address.town') Town from test order by json_value(Json, '$.info.address.country');
select json_value(Json, '$.info.address.town') Town from test where json_value(Json, '$.info.address.country') = 'England' order by json_value(Json, '$.info.address.country');

select json_value('{ "foo" : { "bar": [40, 42] } }', '$.foo.bar[1]');
select json_value('{ "foo" : { "bar": [40, 42] } }', '$.foo');

-- JSON_QUERY
select json_query(Json, '$') from test;
select json_query(Json, '$.info.type') from test;
select json_query(Json, '$.info.address.town') from test;
select json_query(Json, '$.info."address"') from test;
select json_query(Json, '$.info.tags') from test;
select json_query(Json, '$.info.type[0]') from test;
select json_query(Json, '$.info.none') from test;

select json_query('{ "foo" : { "bar": [40, 42] } }', '$.foo');
select json_query('{ "foo" : { "bar": [40, 42] } }', '$.foo.bar');

-- JSON_MODIFY
update test set Json = json_modify(Json, '$.name','Mike') where id = 3;
update test set Json = json_modify(Json, '$.surname','Smith') where id = 3;
update test set Json = json_modify(Json, 'append $.skills','Azure') where id = 3;
select * from test where id = 3;

select json_modify('{}', '$.foo', 'Some "quote"');
select json_modify('{ "foo" : { "bar": [40, 42] } }', 'append $.foo.bar', 99);
select json_modify('{ "foo" : { "bar": [40, 42] } }', '$.foo.bar', 99);

set jsonfmt = 'indent(1)';

select id, json_query(Json, '$') J from test;

drop table test;
