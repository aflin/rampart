Compiling:

> sudo apt install libgraphicsmagick1-dev
# or
> brew install graphicsmagick

> make
> cp rampart-gm.so /usr/local/rampart/modules/ 
# or wherever your modules live, like ~/.rampart/modules

Basic usage:

var gm = require("rampart-gm");

// ---- OPEN ----
// get an image object by opening image
var images = gm.open("/path/to/my/image.jpg");
var image2 = gm.open("/path/to/my/image2.jpg");
// or multiple images in a single image object
var images = gm.open(["/path/to/my/image.jpg", "/path/to/my/image2.jpg"]);

// ---- ADD ----
// image.add([ imageObject | String(path) | buffer(i.e. img.toBuffer() ])
// add some image
images.add("/path/to/my/image2.jpg");   //or
images.add(image2);                     //or
images.add(image2.toBuffer('JPG'));     //or
images.add([image2, "/path/to/my/image3.jpg"]);

// ---- MOGRIFY ---
//mogrify (see GraphicsMagick for all options)
//if object used, single options must be paired with true
images.mogrify("-blur 20x30");      //or
images.mogrify("-blur", "20x30");   //or
images.mogrify("blur", "20x30");    //or
images.mogrify({"-blur": "20x30"});   //or
images.mogrify({"blur": "20x30"});

//example 2:
images.mogrify("-blur 20x30 -auto-orient +contrast");      //or
images.mogrify({
    blur: "20x30",
    "auto-orient": true,
    "+contrast": true
});
// note setting single options to false skips the option and does nothing

// ---- SAVE ----
images.save("/path/to/my/new_image.jpg");

// ---- TOBUFFER ----
// save file to a js buffer in the specified format
var buf = images.toBuffer(["jpg" | "PNG" | "GIF" | ... ]);

// So this now accomplishes the same as save above
rampart.utils.fprintf("/path/to/my/new_image.jpg", "%s", buf);

// Or using rampart-server, return it to client:
return {jpg: buf}

// ---- SELECT ----
//select an image to save in a multi-image document when 
//not saving to a format that supports it
images.select(1); //or
images.select(-1); //last image, negative indexes work      
image.save("/path/to/my/new_image.jpg");

// ---- LIST ----
//get a list of images in the image object
 rampart.utils.printf("%3J\n", images.list());

// ---- GETCOUNT ----
//get a count of images in the image object
 rampart.utils.printf("we have %d images\n", images.getCount());

// ---- IDENTIFY ----
// get simple description Object of current img like "gm identify img.jpg" 
 rampart.utils.printf( "%s\n", images.select(0).identify() );
// get more detail
 rampart.utils.printf( "%3J\n", images.select(0).identify(true) )

// ---- CLOSE ----
// optionally close and free resources
images.close();
// otherwise it is automatically closed
// when var images goes out of scope

// ----- Make an animaged gif -----
var images = gm.open('image1.jpg')
    .mogrify({'auto-orient': true}) //chainable
    .mogrify({ 'delay' : 20}); //this frame gets 200ms
var image2 = gm.open('image2.jpg')
    .mogrify({'auto-orient': true, //or all at once
              'delay' : 60});//this frame gets 600ms
images.add(image2);
images.mogrify({"loop":5});//stop after 5 loops
image.save("animated.gif");
