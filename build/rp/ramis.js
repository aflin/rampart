var ra=new Ramis();
var sixteen="0123456789abcdef";
var five="01234";
var two56=	sixteen+sixteen+sixteen+sixteen+sixteen+sixteen+sixteen+sixteen+
                sixteen+sixteen+sixteen+sixteen+sixteen+sixteen+sixteen+sixteen;
var onek=two56+two56+two56+two56;
var fivek=onek+onek+onek+onek+onek;

//set which to insert here
var insertvar=five;

var len=insertvar.length;

function runme() {
    var resp;

    resp=ra.exec("DEL *");
    console.log(resp);

    print("SET (" + len + ")");

    for (var i=0;i<100000;i++) {
        resp=ra.exec("SET key%d %b",i, insertvar, len);
        //print(i+": got " + resp[0] );
    }


    print("GET");
    for (var i=0;i<100000;i++) {
        resp=ra.exec("GET key%d",i);
        //print(i+": got " + resp[0].length );
    }

    console.log(resp);

}

runme();