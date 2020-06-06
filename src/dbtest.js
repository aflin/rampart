
//print(readFile('./test.js'));
/* 
    "create table typetb (
    Chr char(16), Byt byte(8),      Lng long, 
    In int,       Sin smallint,     Flt float, 
    Dbl double,   Uin unsigned int, Usn unsigned smallint, 
    Dat date,     Vch varchar(16),  ind indirect, ctr counter, 
    slst strlst)"
*/
var arr;
var sql=new Sql('/usr/db/test/');

arr=sql.exec("select * from SYSTABLES where TYPE = '1 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '2 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '3 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '4 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '5 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '6 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '7 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '8 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '9 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '10 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '11 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '12 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '13 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '14 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '15 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '16 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '17 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '18 query'")
arr=sql.exec("select * from SYSTABLES where TYPE = '10 query'")

arr=sql.exec(
    ['%v%',1],
    'select * from typetb where Vch matches ? and Dbl > ?;'
);
print("sql exec done");

console.log(arr.length);

print("doing same again\n");

arr=sql.exec(
    ['%v%',10],
    'select * from typetb where Vch matches ? and Dbl > ?'
);
print("sql exec done");

console.log(arr.length);

print("different query");
arr=sql.exec("select * from SYSTABLES;");
print("sql exec done");
console.log(arr.length);

print("And again");
arr=sql.exec("select * from SYSTABLES where TYPE = ?;",["T"]);
print("sql exec done");
console.log(arr.length);


var cols;
print("starting with callback");
nrows=sql.exec('select * from SYSTABLES',{max:10,skip:1}, function(res,i){
    sql.exec("select * from SYSTABLES where TYPE = '10 query'");
    console.log(i,JSON.stringify(res));
});

print(nrows);
arr=sql.exec("select * from SYSTABLES where TYPE = '9 query'");
//printtable(arr);
//sql.close();
sql.exec("drop table testtb");
sql.exec("drop table testtb2");
sql.exec("create table testtb (Text varchar(16));");
sql.exec("insert into testtb values('Row x');");
sql.exec("create table testtb2 (Text varchar(16));");

for (var i=0;i<20;i++) {
    sql.exec("insert into testtb2 select * from testtb;");
    var stmt="update testtb2 set Text='Row "+i+"' where Text='Row x'";
print(stmt);
    sql.exec(stmt);
}


nrows=sql.exec('select * from testtb2', function(res,i){
    console.log(i,JSON.stringify(res));
    sql.exec("delete from testtb2");
});

sql.close();
