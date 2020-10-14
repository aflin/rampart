
var Sql=require("rampart-sql");
var sql=new Sql.init('./testdb'); // relative to the location of the main script


module.exports = function simple_callback(req){
    var arr=sql.exec(
        'select * from quicktest',
        {max:1}
    );
    /* default mime type is text/plain, if just given a string */
    return(JSON.stringify(arr));
}

