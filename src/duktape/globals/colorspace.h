//
//  colorspace.h
//  imageLab
//
//  Created by Dr Cube on 7/8/17.
//  Copyright © 2017 P. B. Richards.
//
//  Additions for rampart by Aaron Flin
//  Copyright © 2025.
//  MIT licensed


#ifndef colorspace_h
#define colorspace_h

#ifndef byte 
#define byte unsigned char
#endif

#define ITSCOLOR 0
#define ITSGRAYSCALE  1
#define ITSMONOCHROME 2


// These are the centers of the LAB color space. It is NOT 0.5
#define LABLCenter 0.518176
#define LABaCenter 0.523337
#define LABbCenter 0.523262

// These are the boundaries of gray within LAB space. I derived them visually from PhotoShop
#define LABaGrayMin 0.49588602
#define LABaGrayMax 0.546866412
#define LABbGrayMin 0.487967882
#define LABbGrayMax 0.562477686


#define PIXEL struct onePixel
PIXEL
{
   byte r;
   byte g;
   byte b;
};


#define LAB struct CIELab_struct
LAB
{
   float L;
   float a;
   float b;
};

#define HSL struct hueSaturationLightness
HSL
{
   float h;
   float s;
   float l;
   float hx; // these are the rectangular components of hue and are only created by rgbToHSL
   float hy;
};

PIXEL hslToRGB(HSL hsl);

HSL rgbToHSL(PIXEL rgb); // convert RGB to HSL where all members of HSL are >=0.0 and <=1.0

float rgbToCIEL(PIXEL p); // return Luminance of a pixel (rgb)

LAB rgbToLab(PIXEL p); // convert RGB to Lab space where members of Lab are >=0.0 and <=1.0

PIXEL LabToRGB(LAB color); // Convert Lab back to RGB

// this checks to see if the Lab pixel's color is close to gray by "amount" and forces it to gray if so

PIXEL LabToRGBForceGray(LAB color,float amount);

// forces a color to gray if its too close
LAB
rgbToLabForceGray(PIXEL p);


int isGrayPixel(PIXEL rgb); // is this pixel in the grayscale?

byte scale255(float x); // scale and clamp a number from 0.0-1.0 to 0-255 (if negative or over 1.0 it clamps)

float DeltaE2000(LAB Lab1,LAB Lab2);

/* additions for rampart */

#define CENTRY struct color_entry

CENTRY { const char *name; uint8_t r,g,b; int closest_index; float distance; };

#define CCOLOR  struct name_and_rgb 
CCOLOR {
    int      color_index;
    uint8_t  rgb[3];
    float    distance;
};

#define CCODES struct color_codes
CCODES {
    const char *lookup_names;       // a name like "SlateGray, rgb(222,222,222), blink"
    char       *blink_or_class;     // the third param in lookup_names
    CCOLOR      fg;
    CCOLOR      bg;
    uint16_t    flags;
    char        term_start[64];
    char        term_end[8];
    char       *html_start;
    char        html_end[8];
};

int rp_color_convert(CCODES *c);
CCODES *new_color_codes();
CCODES *free_color_codes(CCODES *c);

#define CCODE_FLAG_HAVE_RGB               (1<<0)   // we filled in the rgb value, use that
#define CCODE_FLAG_HAVE_NAME              (1<<1)   // no rgb, look it up in colornames below
// will always get rgb back in any conversion regardless of flags above.
#define CCODE_FLAG_WANT_TERM              (1<<2)   // term codes to change color in terminal
#define CCODE_FLAG_WANT_ANSICOLOR         (1<<3)   // get the closest ansi color
#define CCODE_FLAG_WANT_CSSCOLOR          (1<<4)   // get the closest named html color of the 140 named colors
#define CCODE_FLAG_WANT_HTMLTEXT          (1<<5)   // make the span beginning and end
#define CCODE_FLAG_WANT_BKGND             (1<<6)   // we want to do to the bg what we are doing to the fg
#define CCODE_FLAG_FORCE_TERM             (1<<7)   // force output of term control characters even if not autodetected
#define CCODE_FLAG_FORCE_TERM_TRUECOLOR   (1<<8)   // if forcing, force truecolor
#define CCODE_FLAG_FORCE_TERM_256         (1<<9)   // if forcing, and not forcing truecolor, force 256 color
//#define CCODE_FLAG_FLASHING               (1<<10)  // used internally to keep track of flashing

#define n_color_entries 396

//web colors start at 256
#define css_color_start         256
#define n_ansi_color_entries    256
#define n_ansi16_color_entries  16

static const struct color_entry colornames[n_color_entries] = {
    {"black",                    0,   0,   0, 386, 0.000000},
    {"red",                    205,   0,   0, 264, 6.801960},
    {"green",                    0, 205,   0, 357, 2.147400},
    {"yellow",                 205, 205,   0, 277, 10.063954},
    {"blue",                     0,   0, 238, 326, 2.181947},
    {"magenta",                205,   0, 205, 315, 7.437380},
    {"cyan",                     0, 205, 205, 341, 1.114353},
    {"white",                  229, 229, 229, 395, 2.004560},
    {"brightblack",            127, 127, 127, 390, 0.380549},
    {"brightred",              255,   0,   0, 263, 0.000000},
    {"brightgreen",              0, 255,   0, 358, 0.000000},
    {"brightyellow",           255, 255,   0, 280, 0.000000},
    {"brightblue",              92,  92, 255, 314, 5.732552},
    {"brightmagenta",          255,   0, 255, 311, 0.000000},
    {"brightcyan",               0, 255, 255, 344, 0.000000},
    {"brightwhite",            255, 255, 255, 385, 0.000000},
    {"Grey0",                    0,   0,   0, 386, 0.000000},
    {"NavyBlue",                 0,   0,  95, 323, 5.106870},
    {"DarkBlue",                 0,   0, 135, 324, 0.573445},
    {"Blue3",                    0,   0, 175, 325, 3.948363},
    {"Blue3",                    0,   0, 215, 325, 1.296261},
    {"Blue1",                    0,   0, 255, 326, 0.000000},
    {"DarkGreen",                0,  95,   0, 349, 1.625749},
    {"DeepSkyBlue4",             0,  95,  95, 387, 10.124250},
    {"DeepSkyBlue4",             0,  95, 135, 327, 13.609214},
    {"DeepSkyBlue4",             0,  95, 175, 327, 9.662395},
    {"DodgerBlue3",              0,  95, 215, 327, 5.175660},
    {"DodgerBlue2",              0,  95, 255, 327, 4.906084},
    {"Green4",                   0, 135,   0, 350, 2.484547},
    {"SpringGreen4",             0, 135,  95, 353, 4.231882},
    {"Turquoise4",               0, 135, 135, 338, 1.456803},
    {"DeepSkyBlue3",             0, 135, 175, 328, 9.120843},
    {"DeepSkyBlue3",             0, 135, 215, 328, 5.605708},
    {"DodgerBlue1",              0, 135, 255, 329, 2.890045},
    {"Green3",                   0, 175,   0, 357, 8.337550},
    {"SpringGreen3",             0, 175,  95, 356, 3.448573},
    {"DarkCyan",                 0, 175, 135, 356, 6.695093},
    {"LightSeaGreen",            0, 175, 175, 339, 3.080317},
    {"DeepSkyBlue2",             0, 175, 215, 330, 7.709772},
    {"DeepSkyBlue1",             0, 175, 255, 330, 6.019434},
    {"Green3",                   0, 215,   0, 357, 3.317538},
    {"SpringGreen3",             0, 215,  95, 357, 5.311810},
    {"SpringGreen2",             0, 215, 135, 360, 7.854448},
    {"Cyan3",                    0, 215, 175, 362, 5.664674},
    {"DarkTurquoise",            0, 215, 215, 341, 2.400969},
    {"Turquoise2",               0, 215, 255, 332, 8.735304},
    {"Green1",                   0, 255,   0, 358, 0.000000},
    {"SpringGreen2",             0, 255,  95, 359, 3.540280},
    {"SpringGreen1",             0, 255, 135, 359, 1.021440},
    {"MediumSpringGreen",        0, 255, 175, 360, 3.016124},
    {"Cyan2",                    0, 255, 215, 346, 5.647725},
    {"Cyan1",                    0, 255, 255, 344, 0.000000},
    {"DarkRed",                 95,   0,   0, 287, 6.899854},
    {"DeepPink4",               95,   0,  95, 305, 7.247443},
    {"Purple4",                 95,   0, 135, 304, 3.427010},
    {"Purple4",                 95,   0, 175, 304, 7.014366},
    {"Purple3",                 95,   0, 215, 326, 5.808251},
    {"BlueViolet",              95,   0, 255, 326, 5.041080},
    {"Orange4",                 95,  95,   0, 351, 9.154696},
    {"Grey37",                  95,  95,  95, 388, 3.727462},
    {"MediumPurple4",           95,  95, 135, 308, 13.453906},
    {"SlateBlue3",              95,  95, 175, 313, 5.877401},
    {"SlateBlue3",              95,  95, 215, 313, 2.887044},
    {"RoyalBlue1",              95,  95, 255, 314, 5.035089},
    {"Chartreuse4",             95, 135,   0, 355, 3.435977},
    {"DarkSeaGreen4",           95, 135,  95, 353, 8.004420},
    {"PaleTurquoise4",          95, 135, 135, 340, 8.729130},
    {"SteelBlue",               95, 135, 175, 328, 4.806824},
    {"SteelBlue3",              95, 135, 215, 331, 5.095404},
    {"CornflowerBlue",          95, 135, 255, 331, 5.196389},
    {"Chartreuse3",             95, 175,   0, 357, 9.468348},
    {"DarkSeaGreen4",           95, 175,  95, 356, 5.485304},
    {"CadetBlue",               95, 175, 135, 356, 6.163280},
    {"CadetBlue",               95, 175, 175, 340, 5.566908},
    {"SkyBlue3",                95, 175, 215, 330, 7.057065},
    {"SteelBlue1",              95, 175, 255, 331, 8.707055},
    {"Chartreuse3",             95, 215,   0, 357, 5.361232},
    {"PaleGreen3",              95, 215,  95, 357, 4.821253},
    {"SeaGreen3",               95, 215, 135, 366, 7.154707},
    {"Aquamarine3",             95, 215, 175, 362, 2.746675},
    {"MediumTurquoise",         95, 215, 215, 342, 2.955171},
    {"SteelBlue1",              95, 215, 255, 332, 5.548770},
    {"Chartreuse2",             95, 255,   0, 364, 2.305959},
    {"SeaGreen2",               95, 255,  95, 358, 4.575997},
    {"SeaGreen1",               95, 255, 135, 359, 2.740257},
    {"SeaGreen1",               95, 255, 175, 360, 3.615111},
    {"Aquamarine1",             95, 255, 215, 346, 2.820712},
    {"DarkSlateGray2",          95, 255, 255, 344, 2.638128},
    {"DarkRed",                135,   0,   0, 262, 0.794907},
    {"DeepPink4",              135,   0,  95, 305, 7.895370},
    {"DarkMagenta",            135,   0, 135, 306, 0.882534},
    {"DarkMagenta",            135,   0, 175, 307, 5.443173},
    {"DarkViolet",             135,   0, 215, 307, 2.237748},
    {"Purple",                 135,   0, 255, 309, 3.797683},
    {"Orange4",                135,  95,   0, 289, 13.932416},
    {"LightPink4",             135,  95,  95, 388, 12.630528},
    {"Plum4",                  135,  95, 135, 256, 14.468545},
    {"MediumPurple3",          135,  95, 175, 313, 8.995307},
    {"MediumPurple3",          135,  95, 215, 314, 4.947395},
    {"SlateBlue1",             135,  95, 255, 314, 3.418455},
    {"Yellow4",                135, 135,   0, 354, 2.659183},
    {"Wheat4",                 135, 135,  95, 354, 12.355789},
    {"Grey53",                 135, 135, 135, 390, 2.587867},
    {"LightSlateGrey",         135, 135, 175, 391, 11.020773},
    {"MediumPurple",           135, 135, 215, 316, 7.902449},
    {"LightSlateBlue",         135, 135, 255, 316, 9.093165},
    {"Yellow4",                135, 175,   0, 363, 8.035963},
    {"DarkOliveGreen3",        135, 175,  95, 361, 8.567677},
    {"DarkSeaGreen",           135, 175, 135, 361, 3.668845},
    {"LightSkyBlue3",          135, 175, 175, 340, 8.318541},
    {"LightSkyBlue3",          135, 175, 215, 334, 9.541192},
    {"SkyBlue2",               135, 175, 255, 331, 7.981112},
    {"Chartreuse2",            135, 215,   0, 363, 4.290836},
    {"DarkOliveGreen3",        135, 215,  95, 363, 7.041806},
    {"PaleGreen3",             135, 215, 135, 366, 5.332539},
    {"DarkSeaGreen3",          135, 215, 175, 362, 4.654090},
    {"DarkSlateGray3",         135, 215, 215, 347, 6.681665},
    {"SkyBlue1",               135, 215, 255, 333, 3.247617},
    {"Chartreuse1",            135, 255,   0, 365, 0.749141},
    {"LightGreen",             135, 255,  95, 364, 4.669909},
    {"LightGreen",             135, 255, 135, 368, 2.994747},
    {"PaleGreen1",             135, 255, 175, 368, 4.535039},
    {"Aquamarine1",            135, 255, 215, 346, 0.943886},
    {"DarkSlateGray1",         135, 255, 255, 344, 5.709919},
    {"Red3",                   175,   0,   0, 264, 5.269033},
    {"DeepPink4",              175,   0,  95, 256, 8.148431},
    {"MediumVioletRed",        175,   0, 135, 256, 6.212420},
    {"Magenta3",               175,   0, 175, 310, 6.595494},
    {"DarkViolet",             175,   0, 215, 310, 3.522279},
    {"Purple",                 175,   0, 255, 309, 6.165901},
    {"DarkOrange3",            175,  95,   0, 291, 8.868510},
    {"IndianRed",              175,  95,  95, 266, 5.856902},
    {"HotPink3",               175,  95, 135, 258, 10.419193},
    {"MediumOrchid3",          175,  95, 175, 315, 6.829751},
    {"MediumOrchid",           175,  95, 215, 315, 2.516613},
    {"MediumPurple2",          175,  95, 255, 316, 7.051293},
    {"DarkGoldenrod",          175, 135,   0, 292, 3.052718},
    {"LightSalmon3",           175, 135,  95, 293, 7.812729},
    {"RosyBrown",              175, 135, 135, 294, 3.195100},
    {"Grey63",                 175, 135, 175, 319, 10.833286},
    {"MediumPurple2",          175, 135, 215, 317, 8.864204},
    {"MediumPurple1",          175, 135, 255, 316, 8.689985},
    {"Gold3",                  175, 175,   0, 276, 9.459923},
    {"DarkKhaki",              175, 175,  95, 276, 3.233357},
    {"NavajoWhite3",           175, 175, 135, 276, 7.564137},
    {"Grey69",                 175, 175, 175, 392, 1.716421},
    {"LightSteelBlue3",        175, 175, 215, 334, 9.750381},
    {"LightSteelBlue",         175, 175, 255, 334, 13.815537},
    {"Yellow3",                175, 215,   0, 363, 4.912145},
    {"DarkOliveGreen3",        175, 215,  95, 363, 4.613526},
    {"DarkSeaGreen3",          175, 215, 135, 366, 8.882568},
    {"DarkSeaGreen2",          175, 215, 175, 361, 7.500804},
    {"LightCyan3",             175, 215, 215, 336, 3.642368},
    {"LightSkyBlue1",          175, 215, 255, 333, 8.072656},
    {"GreenYellow",            175, 255,   0, 367, 1.491357},
    {"DarkOliveGreen2",        175, 255,  95, 367, 3.451450},
    {"PaleGreen1",             175, 255, 135, 368, 4.668453},
    {"DarkSeaGreen2",          175, 255, 175, 368, 3.694056},
    {"DarkSeaGreen1",          175, 255, 215, 346, 5.812687},
    {"PaleTurquoise1",         175, 255, 255, 347, 4.358597},
    {"Red3",                   215,   0,   0, 264, 8.290498},
    {"DeepPink3",              215,   0,  95, 256, 10.149370},
    {"DeepPink3",              215,   0, 135, 256, 2.937710},
    {"Magenta3",               215,   0, 175, 256, 7.455804},
    {"Magenta3",               215,   0, 215, 315, 6.601015},
    {"Magenta2",               215,   0, 255, 311, 7.238947},
    {"DarkOrange3",            215,  95,   0, 291, 2.510472},
    {"IndianRed",              215,  95,  95, 266, 2.186901},
    {"HotPink3",               215,  95, 135, 258, 4.028427},
    {"HotPink2",               215,  95, 175, 317, 7.300033},
    {"Orchid",                 215,  95, 215, 317, 3.679252},
    {"MediumOrchid1",          215,  95, 255, 311, 6.404877},
    {"Orange3",                215, 135,   0, 293, 6.516554},
    {"LightSalmon3",           215, 135,  95, 269, 6.163512},
    {"LightPink3",             215, 135, 135, 267, 4.625222},
    {"Pink3",                  215, 135, 175, 258, 6.835519},
    {"Plum3",                  215, 135, 215, 318, 4.720508},
    {"Violet",                 215, 135, 255, 318, 5.434557},
    {"Gold3",                  215, 175,   0, 295, 5.057771},
    {"LightGoldenrod3",        215, 175,  95, 295, 6.294309},
    {"Tan",                    215, 175, 135, 297, 2.863268},
    {"MistyRose3",             215, 175, 175, 260, 8.085238},
    {"Thistle3",               215, 175, 215, 319, 5.659839},
    {"Plum2",                  215, 175, 255, 319, 7.387325},
    {"Yellow3",                215, 215,   0, 280, 8.826751},
    {"Khaki3",                 215, 215,  95, 278, 6.366636},
    {"LightGoldenrod2",        215, 215, 135, 278, 4.719144},
    {"LightYellow3",           215, 215, 175, 281, 6.075500},
    {"Grey84",                 215, 215, 215, 394, 0.936123},
    {"LightSteelBlue1",        215, 215, 255, 321, 7.307885},
    {"Yellow2",                215, 255,   0, 367, 7.130093},
    {"DarkOliveGreen1",        215, 255,  95, 367, 6.163930},
    {"DarkOliveGreen1",        215, 255, 135, 367, 8.251913},
    {"DarkSeaGreen1",          215, 255, 175, 368, 9.370877},
    {"Honeydew2",              215, 255, 215, 379, 11.077922},
    {"LightCyan1",             215, 255, 255, 348, 2.974262},
    {"Red1",                   255,   0,   0, 263, 0.000000},
    {"DeepPink2",              255,   0,  95, 265, 9.606491},
    {"DeepPink1",              255,   0, 135, 257, 2.264495},
    {"DeepPink1",              255,   0, 175, 257, 5.304807},
    {"Magenta2",               255,   0, 215, 311, 5.384838},
    {"Magenta1",               255,   0, 255, 311, 0.000000},
    {"OrangeRed1",             255,  95,   0, 271, 5.317058},
    {"IndianRed1",             255,  95,  95, 268, 6.395423},
    {"IndianRed1",             255,  95, 135, 258, 6.615883},
    {"HotPink",                255,  95, 175, 259, 1.648638},
    {"HotPink",                255,  95, 215, 317, 5.538833},
    {"MediumOrchid1",          255,  95, 255, 318, 5.951448},
    {"DarkOrange",             255, 135,   0, 273, 1.581694},
    {"Salmon1",                255, 135,  95, 274, 2.298378},
    {"LightCoral",             255, 135, 135, 267, 2.936675},
    {"PaleVioletRed1",         255, 135, 175, 259, 7.242296},
    {"Orchid2",                255, 135, 215, 318, 6.544446},
    {"Orchid1",                255, 135, 255, 318, 3.189515},
    {"Orange1",                255, 175,   0, 275, 3.440850},
    {"SandyBrown",             255, 175,  95, 296, 3.702420},
    {"LightSalmon1",           255, 175, 135, 270, 3.927751},
    {"LightPink1",             255, 175, 175, 260, 4.300746},
    {"Pink1",                  255, 175, 215, 260, 8.378834},
    {"Plum1",                  255, 175, 255, 319, 6.679647},
    {"Gold1",                  255, 215,   0, 277, 0.000000},
    {"LightGoldenrod2",        255, 215,  95, 277, 5.757607},
    {"LightGoldenrod2",        255, 215, 135, 300, 6.283812},
    {"NavajoWhite1",           255, 215, 175, 279, 1.977290},
    {"MistyRose1",             255, 215, 215, 369, 4.170527},
    {"Thistle1",               255, 215, 255, 320, 8.218845},
    {"Yellow1",                255, 255,   0, 280, 0.000000},
    {"LightGoldenrod1",        255, 255,  95, 280, 4.159685},
    {"Khaki1",                 255, 255, 135, 278, 6.635644},
    {"Wheat1",                 255, 255, 175, 281, 5.732018},
    {"Cornsilk1",              255, 255, 215, 284, 1.005979},
    {"Grey100",                255, 255, 255, 385, 0.000000},
    {"Grey3",                    8,   8,   8, 386, 1.267439},
    {"Grey7",                   18,  18,  18, 386, 3.202855},
    {"Grey11",                  28,  28,  28, 386, 6.149594},
    {"Grey15",                  38,  38,  38, 386, 9.284227},
    {"Grey19",                  48,  48,  48, 386, 12.436598},
    {"Grey23",                  58,  58,  58, 386, 15.625112},
    {"Grey27",                  68,  68,  68, 388, 13.075066},
    {"Grey30",                  78,  78,  78, 388, 9.720374},
    {"Grey35",                  88,  88,  88, 388, 6.243595},
    {"Grey39",                  98,  98,  98, 388, 2.626359},
    {"Grey42",                 108, 108, 108, 388, 1.151137},
    {"Grey46",                 118, 118, 118, 390, 3.915910},
    {"Grey50",                 128, 128, 128, 390, 0.000000},
    {"Grey54",                 138, 138, 138, 390, 3.656810},
    {"Grey58",                 148, 148, 148, 392, 6.490604},
    {"Grey62",                 158, 158, 158, 392, 3.300622},
    {"Grey66",                 168, 168, 168, 392, 0.291647},
    {"Grey70",                 178, 178, 178, 392, 2.553829},
    {"Grey74",                 188, 188, 188, 393, 1.042240},
    {"Grey78",                 198, 198, 198, 393, 1.525714},
    {"Grey82",                 208, 208, 208, 394, 0.713126},
    {"Grey85",                 218, 218, 218, 395, 0.455976},
    {"Grey89",                 228, 228, 228, 395, 1.785566},
    {"Grey93",                 238, 238, 238, 373, 1.455226},
    {"MediumVioletRed",        199,  21, 133, 162, 2.937710},
    {"DeepPink",               255,  20, 147, 198, 2.264495},
    {"PaleVioletRed",          219, 112, 147, 168, 4.028427},
    {"HotPink",                255, 105, 180, 205, 1.648638},
    {"LightPink",              255, 182, 193, 217, 4.300746},
    {"Pink",                   255, 192, 203, 217, 5.934090},
    {"DarkRed",                139,   0,   0,  88, 0.794907},
    {"Red",                    255,   0,   0,   9, 0.000000},
    {"Firebrick",              178,  34,  34, 124, 5.269033},
    {"Crimson",                220,  20,  60, 197, 9.606491},
    {"IndianRed",              205,  92,  92, 167, 2.186901},
    {"LightCoral",             240, 128, 128, 210, 2.936675},
    {"Salmon",                 250, 128, 114, 210, 4.709747},
    {"DarkSalmon",             233, 150, 122, 209, 5.624024},
    {"LightSalmon",            255, 160, 122, 216, 3.927751},
    {"OrangeRed",              255,  69,   0, 202, 5.317058},
    {"Tomato",                 255,  99,  71, 203, 6.746571},
    {"DarkOrange",             255, 140,   0, 208, 1.581694},
    {"Coral",                  255, 127,  80, 209, 2.298378},
    {"Orange",                 255, 165,   0, 214, 3.440850},
    {"DarkKhaki",              189, 183, 107, 143, 3.233357},
    {"Gold",                   255, 215,   0, 220, 0.000000},
    {"Khaki",                  240, 230, 140, 186, 4.719144},
    {"PeachPuff",              255, 218, 185, 223, 1.977290},
    {"Yellow",                 255, 255,   0,  11, 0.000000},
    {"PaleGoldenrod",          238, 232, 170, 229, 5.732018},
    {"Moccasin",               255, 228, 181, 223, 5.222951},
    {"PapayaWhip",             255, 239, 213, 223, 7.453407},
    {"LightGoldenrodYellow",   250, 250, 210, 230, 1.005979},
    {"LemonChiffon",           255, 250, 205, 230, 1.933658},
    {"LightYellow",            255, 255, 224, 230, 2.494357},
    {"Maroon",                 128,   0,   0,  88, 1.400764},
    {"Brown",                  165,  42,  42, 124, 7.036841},
    {"SaddleBrown",            139,  69,  19, 130, 11.850827},
    {"Sienna",                 160,  82,  45, 130, 10.547986},
    {"Chocolate",              210, 105,  30, 166, 2.510472},
    {"DarkGoldenrod",          184, 134,  11, 136, 3.052718},
    {"Peru",                   205, 133,  63, 172, 6.516554},
    {"RosyBrown",              188, 143, 143, 138, 3.195100},
    {"Goldenrod",              218, 165,  32, 178, 5.057771},
    {"SandyBrown",             244, 164,  96, 215, 3.702420},
    {"Tan",                    210, 180, 140, 180, 2.863268},
    {"Burlywood",              222, 184, 135, 180, 3.493877},
    {"Wheat",                  245, 222, 179, 223, 5.208564},
    {"NavajoWhite",            255, 222, 173, 223, 3.893369},
    {"Bisque",                 255, 228, 196, 223, 3.939062},
    {"BlanchedAlmond",         255, 235, 205, 223, 6.072368},
    {"Cornsilk",               255, 248, 220, 230, 5.161493},
    {"Indigo",                  75,   0, 130,  54, 3.427010},
    {"Purple",                 128,   0, 128,  90, 1.539750},
    {"DarkMagenta",            139,   0, 139,  90, 0.882534},
    {"DarkViolet",             148,   0, 211,  92, 2.237748},
    {"DarkSlateBlue",           72,  61, 139,  54, 11.100263},
    {"BlueViolet",             138,  43, 226,  93, 3.797683},
    {"DarkOrchid",             153,  50, 204, 128, 3.522279},
    {"Fuchsia",                255,   0, 255,  13, 0.000000},
    {"Magenta",                255,   0, 255,  13, 0.000000},
    {"SlateBlue",              106,  90, 205,  62, 2.887044},
    {"MediumSlateBlue",        123, 104, 238,  99, 3.418455},
    {"MediumOrchid",           186,  85, 211, 134, 2.516613},
    {"MediumPurple",           147, 112, 219,  98, 5.510426},
    {"Orchid",                 218, 112, 214, 170, 3.679252},
    {"Violet",                 238, 130, 238, 213, 3.189515},
    {"Plum",                   221, 160, 221, 182, 5.659839},
    {"Thistle",                216, 191, 216, 182, 5.806319},
    {"Lavender",               230, 230, 250, 189, 7.307885},
    {"MidnightBlue",            25,  25, 112,  17, 5.856883},
    {"Navy",                     0,   0, 128,  18, 1.017607},
    {"DarkBlue",                 0,   0, 139,  18, 0.573445},
    {"MediumBlue",               0,   0, 205,  20, 1.296261},
    {"Blue",                     0,   0, 255,  21, 0.000000},
    {"RoyalBlue",               65, 105, 225,  27, 4.906084},
    {"SteelBlue",               70, 130, 180,  67, 4.806824},
    {"DodgerBlue",              30, 144, 255,  33, 2.890045},
    {"DeepSkyBlue",              0, 191, 255,  39, 6.019434},
    {"CornflowerBlue",         100, 149, 237,  68, 5.095404},
    {"SkyBlue",                135, 206, 235, 117, 3.843255},
    {"LightSkyBlue",           135, 206, 250, 117, 3.247617},
    {"LightSteelBlue",         176, 196, 222, 153, 8.184257},
    {"LightBlue",              173, 216, 230, 152, 6.472024},
    {"PowderBlue",             176, 224, 230, 152, 3.642368},
    {"Teal",                     0, 128, 128,  30, 2.587474},
    {"DarkCyan",                 0, 139, 139,  30, 1.456803},
    {"LightSeaGreen",           32, 178, 170,  37, 3.080317},
    {"CadetBlue",               95, 158, 160,  73, 5.566908},
    {"DarkTurquoise",            0, 206, 209,   6, 1.114353},
    {"MediumTurquoise",         72, 209, 204,   6, 2.907630},
    {"Turquoise",               64, 224, 208,  44, 5.796443},
    {"Aqua",                     0, 255, 255,  14, 0.000000},
    {"Cyan",                     0, 255, 255,  14, 0.000000},
    {"Aquamarine",             127, 255, 212, 122, 0.943886},
    {"PaleTurquoise",          175, 238, 238, 159, 4.358597},
    {"LightCyan",              224, 255, 255, 195, 2.974262},
    {"DarkGreen",                0, 100,   0,  22, 1.625749},
    {"Green",                    0, 128,   0,  28, 2.484547},
    {"DarkOliveGreen",          85, 107,  47,  58, 9.154696},
    {"ForestGreen",             34, 139,  34,  28, 2.741905},
    {"SeaGreen",                46, 139,  87,  29, 4.231882},
    {"Olive",                  128, 128,   0, 100, 2.659183},
    {"OliveDrab",              107, 142,  35,  64, 3.435977},
    {"MediumSeaGreen",          60, 179, 113,  35, 3.448573},
    {"LimeGreen",               50, 205,  50,   2, 2.147400},
    {"Lime",                     0, 255,   0,  10, 0.000000},
    {"SpringGreen",              0, 255, 127,  48, 1.021440},
    {"MediumSpringGreen",        0, 250, 154,  49, 3.016124},
    {"DarkSeaGreen",           143, 188, 143, 108, 3.668845},
    {"MediumAquamarine",       102, 205, 170,  79, 2.746675},
    {"YellowGreen",            154, 205,  50, 112, 4.290836},
    {"LawnGreen",              124, 252,   0, 118, 1.145663},
    {"Chartreuse",             127, 255,   0, 118, 0.749141},
    {"LightGreen",             144, 238, 144, 120, 4.613926},
    {"GreenYellow",            173, 255,  47, 154, 1.491357},
    {"PaleGreen",              152, 251, 152, 120, 2.994747},
    {"MistyRose",              255, 228, 225, 224, 4.170527},
    {"AntiqueWhite",           250, 235, 215, 223, 7.791935},
    {"Linen",                  250, 240, 230, 255, 4.567756},
    {"Beige",                  245, 245, 220, 230, 4.697834},
    {"WhiteSmoke",             245, 245, 245, 255, 1.455226},
    {"LavenderBlush",          255, 240, 245, 255, 6.030925},
    {"OldLace",                253, 245, 230, 255, 6.269560},
    {"AliceBlue",              240, 248, 255,  15, 4.169851},
    {"Seashell",               255, 245, 238,  15, 4.089858},
    {"GhostWhite",             248, 248, 255,  15, 3.549376},
    {"Honeydew",               240, 255, 240, 230, 7.985147},
    {"FloralWhite",            255, 250, 240,  15, 4.294403},
    {"Azure",                  240, 255, 255,  15, 6.359706},
    {"MintCream",              245, 255, 250,  15, 5.634595},
    {"Snow",                   255, 250, 250,  15, 1.941157},
    {"Ivory",                  255, 255, 240,  15, 6.816962},
    {"White",                  255, 255, 255,  15, 0.000000},
    {"Black",                    0,   0,   0,   0, 0.000000},
    {"DarkSlateGray",           47,  79,  79,  23, 10.124250},
    {"DimGray",                105, 105, 105, 242, 1.151137},
    {"SlateGray",              112, 128, 144,   8, 9.190985},
    {"Gray",                   128, 128, 128, 244, 0.000000},
    {"LightSlateGray",         119, 136, 153,  67, 9.569435},
    {"DarkGray",               169, 169, 169, 248, 0.291647},
    {"Silver",                 192, 192, 192, 250, 1.042240},
    {"LightGray",              211, 211, 211, 252, 0.713126},
    {"Gainsboro",              220, 220, 220, 253, 0.455976},
};

#endif /* colorspace_h */
