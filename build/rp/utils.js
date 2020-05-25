
utils=require("rputils");
i=0

try {

utils.readln("./server.js",function(line){
    print(line);
    i++
    if(i>55) return false;
});

} catch(e) {
    print("caught:");
    console.log(e);
}


//utils.readcsv("test2.csv",function(){
//    print("in function");
//});
