-- JSON Examples from Docs

create table test(id int, Json varchar(30));
insert into test values(1, '{
  "items" : [
    {
      "Num" : 1,
      "Text" : "The Name",
      "First" : true
    },
    {
      "Num" : 2.0,
      "Text" : "The second one",
      "First" : false
    }
    ,
    null
  ]
}
');
-- isjson
select isjson('{ "type" : 1 }');
select isjson('{}');
select isjson('json this is not');

--json_type

select json_type(Json) from test where id = 1;
select json_type(Json.$.items[0]) from test where id = 1;
select json_type(Json.$.items) from test where id = 1;
select json_type(Json.$.items[0].Num) from test where id = 1;
select json_type(Json.$.items[1].Num) from test where id = 1;
select json_type(Json.$.items[0].Text) from test where id = 1;
select json_type(Json.$.items[0].First) from test where id = 1;
select json_type(Json.$.items[2]) from test where id = 1;

--json_value

select json_value(Json, '$') from test where id = 1;
select json_value(Json, '$.items[0]') from test where id = 1;
select json_value(Json, '$.items') from test where id = 1;
select json_value(Json, '$.items[0].Num') from test where id = 1;
select json_value(Json, '$.items[1].Num') from test where id = 1;
select json_value(Json, '$.items[0].Text') from test where id = 1;
select json_value(Json, '$.items[0].First') from test where id = 1;
select json_value(Json, '$.items[2]') from test where id = 1;

--json_query

select json_query(Json, '$') from test where id = 1;
select json_query(Json, '$.items[0]') from test where id = 1;
select json_query(Json, '$.items') from test where id = 1;
select json_query(Json, '$.items[0].Num') from test where id = 1;
select json_query(Json, '$.items[1].Num') from test where id = 1;
select json_query(Json, '$.items[0].Text') from test where id = 1;
select json_query(Json, '$.items[0].First') from test where id = 1;
select json_query(Json, '$.items[2]') from test where id = 1;

-- json_modify

select json_modify('{}', '$.foo', 'Some "quote"');
select json_modify('{ "foo" : { "bar": [40, 42] } }', 'append $.foo.bar', 99);
select json_modify('{ "foo" : { "bar": [40, 42] } }', '$.foo.bar', 99);

drop table test;
