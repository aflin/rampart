-- Tests Bug 7660: JSON Library



create table test (id int, Json varchar(40), Patch varchar(20));

-- ======================================================================
-- Test Cases from https://tools.ietf.org/html/rfc7386

insert into test values (seq(1), '{"a":"b"}','{"a":"c"}');
insert into test values (seq(1), '{"a":"b"}','{"b":"c"}');
insert into test values (seq(1), '{"a":"b"}','{"a":null}');
insert into test values (seq(1), '{"a":"b","b":"c"}','{"a":null}');
insert into test values (seq(1), '{"a":["b"]}','{"a":"c"}');
insert into test values (seq(1), '{"a":"c"}','{"a":["b"]}');
insert into test values (seq(1), '{"a": {"b":"c"}}','{"a": {"b":"d","c":null}}');
insert into test values (seq(1), '{"a": [{"b":"c"}]}','{"a": [1]}');
insert into test values (seq(1), '["a","b"]','["c","d"]');
insert into test values (seq(1), '{"a":"b"}','["c"]');
insert into test values (seq(1), '{"a":"foo"}','null');
insert into test values (seq(1), '{"a":"foo"}','"bar"');
insert into test values (seq(1), '{"e":null}','{"a":1}');
insert into test values (seq(1), '[1,2]','{"a":"b", "c":null}');
insert into test values (seq(1), '{}','{"a": {"bb":{"ccc":null}}}');

select id, Json, Patch, json_merge_patch (Json, Patch) as Merge_Patch, json_merge_preserve (Json, Patch) as Merge_Preserve from test;
-- select id, json_merge_patch (Json.$.a, Patch.$.a) as Patch from test;
-- select id, json_merge_preserve (Json.$.a, Patch.$.a) as Preserve from test;

-- ======================================================================
-- Try examples from:
-- https://dev.mysql.com/doc/refman/5.7/en/json-modification-functions.html
SELECT JSON_MERGE_PRESERVE('[1, 2]', '[true, false]');
SELECT JSON_MERGE_PRESERVE('{"name": "x"}', '{"id": 47}');
SELECT JSON_MERGE_PRESERVE('1', 'true');
SELECT JSON_MERGE_PRESERVE('[1, 2]', '{"id": 47}');
SELECT JSON_MERGE_PRESERVE('{ "a": 1, "b": 2 }', '{ "a": 3, "c": 4 }');
SELECT JSON_MERGE_PRESERVE(JSON_MERGE_PRESERVE('{ "a": 1, "b": 2 }','{ "a": 3, "c": 4 }'), '{ "a": 5, "d": 6 }');


-- ======================================================================
-- Test Types;

select json_type('');
select json_type('{}');
select json_type('[]');
select json_type('word');
select json_type('true');
select json_type('false');
select json_type('null');
select json_type('"word"');
select json_type('"true"');
select json_type('"false"');
select json_type('"null"');
select json_type('0');
select json_type('0.0');

select id, json_type(Json), Json, json_type(Patch), Patch from test;

drop table test;
