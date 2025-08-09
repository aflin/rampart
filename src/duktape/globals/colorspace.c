//
//  colorspace.c
//  imageLab
//
//  Created by Dr Cube on 7/8/17.
//  Copyright © 2017 P. B. Richards. All rights reserved.
//
//
//  Additions for rampart by Aaron Flin
//  Copyright © 2025.
//  MIT licensed

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "colorspace.h"

// scale and clamp a number from 0-1 to 0-255

byte
scale255(float x)
{
   x*=255.0;
   x=x<0.0f?0.0f:x; // limit clamp
   x=x>255.0f?255.0f:x;
   return((byte)x);
}

/*  Helper that converts hue to rgb */

static float
hueToRgb(float p, float q, float t)
{
   if (t < 0.0f)
      t += 1.0f;
   if (t > 1.0f)
      t -= 1.0f;
   if (t < 0.16667f)
      return p + (q - p) * 6.0f * t;
   if (t < 0.5f)
      return q;
   if (t < 0.66667f)
      return p + (q - p) * (0.66667f - t) * 6.0f;
   return p;
}


// please note that these rgb hsl coversions result in some precision error.
// It's usually not more than 1 part in 255

PIXEL
hslToRGB(HSL hsl)
{
   float h,s,l,r, g, b;
   PIXEL rgb;
   h=hsl.h;//255.0f;
   s=hsl.s;//255.0f;
   l=hsl.l;//255.0f;
   
   if (s == 0.0f)
   {
      r = g = b = l; // achromatic
   }
   else
   {
      float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
      float p = 2.0f * l - q;
      r = hueToRgb(p, q, h + 0.333333f);
      g = hueToRgb(p, q, h);
      b = hueToRgb(p, q, h - 0.333333f);
   }
   rgb.r=scale255(r);
   rgb.g=scale255(g);
   rgb.b=scale255(b);
   
   return(rgb);
}

// Given a Color(PIXEL Struct) in range of 0-1.0 return in HSL
HSL
rgbToHSL(PIXEL rgb)
{
   float r = rgb.r / 255.0f;
   float g = rgb.g / 255.0f;
   float b = rgb.b / 255.0f;
   float h,s,l;
   
   HSL hsl;
   
   float max = (r > g && r > b) ? r : (g > b) ? g : b;
   float min = (r < g && r < b) ? r : (g < b) ? g : b;
   
   
   l = (max + min) / 2.0f;
   
   if (max == min)
   {
      h = s = 0.0f;
   } else
   {
      float d = max - min;
      s = (l > 0.5f) ? d / (2.0f - max - min) : d / (max + min);
      
      if (r > g && r > b)
         h = (g - b) / d + (g < b ? 6.0f : 0.0f);
      
      else if (g > b)
         h = (b - r) / d + 2.0f;
      
      else
         h = (r - g) / d + 4.0f;
      
      h /= 6.0f;
   }
   
   hsl.h=h;
   hsl.s=s;
   hsl.l=l;
   hsl.hx=(sin(hsl.h*2.0*M_PI)+1.0)*0.5; // convert circle to x,y  2rads in circle, remove negative, scale back to 0-1.0
   hsl.hy=(cos(hsl.h*2.0*M_PI)+1.0)*0.5; // convert circle to x,y  2rads in circle, remove negative, scale back to 0-1.0
   return(hsl);
}

// is this pixel in the grayscale?
int
isGrayPixel(PIXEL rgb)
{
  HSL    hsl;

  if((rgb.r>>2)==(rgb.b>>2) && (rgb.b>>2)==(rgb.g>>2)) // all vales within four is gray
      return(ITSGRAYSCALE);
  
  hsl=rgbToHSL(rgb);
  
  
  
  if(hsl.l>0.75 && hsl.s>0.9)
      return(ITSCOLOR);
 
  if(hsl.s<=0.05 || hsl.l<0.10) // <5 % saturation is gray, <12% is black according to photoshop
      return(ITSGRAYSCALE);
 
  
  if(hsl.l<=0.14 && hsl.s<0.17) // dark colors with little saturation
      return(ITSGRAYSCALE);
      
 return(ITSCOLOR);
}



// Convert R G B into CIELab space
// TAKE NOTE!!!! I produce 0<=L<=1  0<=a<=1  0<=b<=1

// reverses the rgb gamma
#define inverseGamma(t)    (((t) <= 0.0404482362771076) ? ((t)/12.92) : pow(((t) + 0.055)/1.055, 2.4))
// imposes rgb Gamma
#define gammaCorrect(t)     (((t) <= 0.0031306684425005883) ? (12.92*(t)) : (1.055*pow((t), 0.416666666666666667) - 0.055))



/*  XYZ color of the D65 white point */
#define WHITEPOINT_X    0.950456
#define WHITEPOINT_Y    1.0
#define WHITEPOINT_Z    1.088754

//CIE L*a*b* f function (used to convert XYZ to L*a*b*)  http://en.wikipedia.org/wiki/Lab_color_space
#define LABF(t) ((t >= 8.85645167903563082e-3) ? powf(t,0.333333333333333) : (841.0/108.0)*(t) + (4.0/29.0))
// Inverse of above for Lab to XYZ
#define LABINVF(t) ((t >= 0.206896551724137931) ? ((t)*(t)*(t)) : (108.0/841.0)*((t) - (4.0/29.0)))
/**  Min of A, B, and C */
#define MIN(A,B)    (((A) <= (B)) ? (A) : (B))
#define MIN3(A,B,C)    (((A) <= (B)) ? MIN(A,C) : MIN(B,C))

float
rgbToCIEL(PIXEL p)
{
   double y;
   double r=p.r/255.0;
   double g=p.g/255.0;
   double b=p.b/255.0;
   
   r=inverseGamma(r);
   g=inverseGamma(g);
   b=inverseGamma(b);
   
   y = 0.2125862307855955516*r + 0.7151703037034108499*g + 0.07220049864333622685*b;
   
   // At this point we've done RGBtoXYZ now do XYZ to Lab
   
   // y /= WHITEPOINT_Y; The white point for y in D65 is 1.0
    
    y = LABF(y);
    
   /* This is the "normal conversion which produces values scaled to 100
    Lab.L = 116.0*y - 16.0;
    */
   return(1.16*y - 0.16); // return values for 0.0 >=L <=1.0
}



LAB
rgbToLab(PIXEL p)
{
   double x,y,z;
   double r=p.r/255.0;
   double g=p.g/255.0;
   double b=p.b/255.0;
   LAB Lab;
   
   r=inverseGamma(r);
   g=inverseGamma(g);
   b=inverseGamma(b);
   
   x = 0.4123955889674142161*r + 0.3575834307637148171*g + 0.1804926473817015735*b;
   y = 0.2125862307855955516*r + 0.7151703037034108499*g + 0.07220049864333622685*b;
   z = 0.01929721549174694484*r + 0.1191838645808485318*g + 0.9504971251315797660*b;
   
   // At this point we've done RGBtoXYZ now do XYZ to Lab
   
   x /= WHITEPOINT_X;
    y /= WHITEPOINT_Y;
    z /= WHITEPOINT_Z;
    
   x = LABF(x);
    y = LABF(y);
    z = LABF(z);
    
   /* This is the "normal conversion which produces values scaled to 100
    
    Lab.L = 116.0*y - 16.0;
    Lab.a = 500.0*(x - y);
    Lab.b = 200.0*(y - z);
    
    which produces this for the RGB Gamut:
    <table> <!-- L       a          b -->
    <tr><td>-0.000000  0.000000  0.000000</td><td style='background-color:#000000'>  0   0   0</td></tr>
    <tr><td> 32.302696  79.193695 -107.867744</td><td style='background-color:#0000ff'>  0   0  255</td></tr>
    <tr><td> 87.735596 -86.182236  83.180779</td><td style='background-color:#00FF00'>  0  255   0</td></tr>
    <tr><td> 91.115196 -48.077999 -14.143872</td><td style='background-color:#00FFff'>  0  255  255</td></tr>
    <tr><td> 53.231384  80.116272  67.218781</td><td style='background-color:#FF0000'> 255   0   0</td></tr>
    <tr><td> 60.318748  98.258614 -60.849129</td><td style='background-color:#FF00ff'> 255   0  255</td></tr>
    <tr><td> 97.136482 -21.550238  94.481682</td><td style='background-color:#FFFF00'> 255  255   0</td></tr>
    <tr><td> 99.998337  0.009894 -0.016594</td><td style='background-color:#FFFFff'> 255  255  255</td></tr>
    </table>
    */
   // I want 0 to 1 for all values but also to preserve their relationship for distance
   // max a|b == 98.258614, min a|b== -107.867744, max-min== 206.126358
   
   Lab.L = 1.16*y - 0.16;
    Lab.a = (500.0*(x - y)+107.867744)/206.126358;
    Lab.b = (200.0*(y - z)+107.867744)/206.126358;
   
   // maxa 98.258614 mina -86.182236 maxb 83.180779 minb -107.867744
   /*
    Lab.L = 1.16*y - 0.16;
    Lab.a = (500.0*(x - y)+86.182236)/184.44085;
    Lab.b = (200.0*(y - z)+107.867744)/191.048523;
    */
   
   return(Lab);
}


// forces a color to gray if its too close
LAB
rgbToLabForceGray(PIXEL p)
{
  LAB lab;
  if(isGrayPixel(p))
  {
    lab.L=rgbToCIEL(p);
    lab.a=LABaCenter;
    lab.b=LABbCenter;
    return(lab);
  }
  return(rgbToLab(p));
  
}


/**
 * Convert CIE L*a*b* (CIELAB) to CIE XYZ with the D65 white point
 *
 *  X, Y, Z pointers to hold the result
 *  L, a, b the input L*a*b* values
 *
 * Wikipedia: http://en.wikipedia.org/wiki/Lab_color_space
 */
PIXEL
LabToRGB(LAB color)
{
   double X,Y,Z;
   double L=color.L;
   double a=color.a;
   double b=color.b;
   PIXEL p;
/*
    Lab.L = 1.16*y - 0.16;
    Lab.a = (500.0*(x - y)+107.867744)/206.126358;
    Lab.b = (200.0*(y - z)+107.867744)/206.126358;
*/
   
   if(L==0.0) // it's black
   {
      p.r=p.g=p.b=0;
      return(p);
   }
   
   L*=100.0;
   a=(a*206.126358)-107.867744;
   b=(b*206.126358)-107.867744;
   
   /* This was the errored code
   L*=100.0;
   a=(a*200.0)-100.0;
   b=(b*200.0)-100.0;
   */
   
   L = (L + 16)/116;
   a = L + a/500;
   b = L - b/200;
   
   
    X = WHITEPOINT_X*LABINVF(a);
    Y = WHITEPOINT_Y*LABINVF(L);
    Z = WHITEPOINT_Z*LABINVF(b);
   
   double R1, B1, G1, Min;
    
    
    R1 =  3.2406*X - 1.5372*Y - 0.4986*Z;
    G1 =  -0.9689*X + 1.8758*Y + 0.0415*Z;
    B1 =  0.0557*X - 0.2040*Y + 1.0570*Z;
    
    Min = MIN3(R1, G1, B1);
    
    /* Force nonnegative values so that gamma correction is well-defined. */
    if(Min < 0)
    {
        R1 -= Min;
        G1 -= Min;
        B1 -= Min;
    }

   /* Transform from RGB to R'G'B' */
   p.r = scale255((float)gammaCorrect(R1));
   p.g = scale255((float)gammaCorrect(G1));
   p.b = scale255((float)gammaCorrect(B1));
   
   return(p);
}


// this checks to see if the Lab pixel's color is close to gray by "amount" and forces it to gray if so

PIXEL
LabToRGBForceGray(LAB color,float amount)
{
  if(color.a>(LABaCenter-amount) && color.a<(LABaCenter+amount) && color.b>(LABbCenter-amount) && color.b<(LABaCenter+amount)) // forces gray
   {
      PIXEL p;
      p.g = scale255(color.L);
      p.r=p.g;
      p.b=p.g;
      return(p);
   }

  return(LabToRGB(color));
}






float
DeltaE2000a(LAB Lab1,LAB Lab2)
{
   Lab1.L*=100.0;Lab2.L*=100.0;
   Lab1.a*=200.0;Lab2.a*=200.0;
   Lab1.b*=200.0;Lab2.b*=200.0;
   Lab1.a-=100.0;Lab2.a-=100.0;
   Lab1.b-=100.0;Lab2.b-=100.0;
   
   
   /*
   float kL = 1.0;
   float kC = 1.0;
   float kH = 1.0;
   */
   float kL = 1.0;
   float kC = 1.0;
   float kH = 1.0;

   
   float lBarPrime = 0.5 * (Lab1.L + Lab2.L);
   float c1 = sqrtf(Lab1.a * Lab1.a + Lab1.b * Lab1.b);
   float c2 = sqrtf(Lab2.a * Lab2.a + Lab2.b * Lab2.b);
   float cBar = 0.5 * (c1 + c2);
   float cBar7 = cBar * cBar * cBar * cBar * cBar * cBar * cBar;
   float g = 0.5 * (1.0 - sqrtf(cBar7 / (cBar7 + 6103515625.0)));    /* 6103515625 = 25^7 */
   float a1Prime = Lab1.a * (1.0 + g);
   float a2Prime = Lab2.a * (1.0 + g);
   float c1Prime = sqrtf(a1Prime * a1Prime + Lab1.b * Lab1.b);
   float c2Prime = sqrtf(a2Prime * a2Prime + Lab2.b * Lab2.b);
   float cBarPrime = 0.5 * (c1Prime + c2Prime);
   float h1Prime = (atan2f(Lab1.b, a1Prime) * 180.0) / M_PI;
   float dhPrime; // not initialized on purpose
   
   if (h1Prime < 0.0)
      h1Prime += 360.0;
   float h2Prime = (atan2f(Lab2.b, a2Prime) * 180.0) / M_PI;
   if (h2Prime < 0.0)
      h2Prime += 360.0;
   float hBarPrime = (fabsf(h1Prime - h2Prime) > 180.0) ? (0.5 * (h1Prime + h2Prime + 360.0)) : (0.5 * (h1Prime + h2Prime));
   float t = 1.0 -
   0.17 * cosf(M_PI * (      hBarPrime - 30.0) / 180.0) +
   0.24 * cosf(M_PI * (2.0 * hBarPrime       ) / 180.0) +
   0.32 * cosf(M_PI * (3.0 * hBarPrime +  6.0) / 180.0) -
   0.20 * cosf(M_PI * (4.0 * hBarPrime - 63.0) / 180.0);
   if (fabsf(h2Prime - h1Prime) <= 180.0)
      dhPrime = h2Prime - h1Prime;
   else
      dhPrime = (h2Prime <= h1Prime) ? (h2Prime - h1Prime + 360.0) : (h2Prime - h1Prime - 360.0);
   float dLPrime = Lab2.L - Lab1.L;
   float dCPrime = c2Prime - c1Prime;
   float dHPrime = 2.0 * sqrtf(c1Prime * c2Prime) * sinf(M_PI * (0.5 * dhPrime) / 180.0);
   float sL = 1.0 + ((0.015 * (lBarPrime - 50.0) * (lBarPrime - 50.0)) / sqrtf(20.0 + (lBarPrime - 50.0) * (lBarPrime - 50.0)));
   float sC = 1.0 + 0.045 * cBarPrime;
   float sH = 1.0 + 0.015 * cBarPrime * t;
   float dTheta = 30.0 * expf(-((hBarPrime - 275.0) / 25.0) * ((hBarPrime - 275.0) / 25.0));
   float cBarPrime7 = cBarPrime * cBarPrime * cBarPrime * cBarPrime * cBarPrime * cBarPrime * cBarPrime;
   float rC = sqrtf(cBarPrime7 / (cBarPrime7 + 6103515625.0));
   float rT = -2.0 * rC * sinf(M_PI * (2.0 * dTheta) / 180.0);
   return(sqrtf(
                      (dLPrime / (kL * sL)) * (dLPrime / (kL * sL)) +
                      (dCPrime / (kC * sC)) * (dCPrime / (kC * sC)) +
                      (dHPrime / (kH * sH)) * (dHPrime / (kH * sH)) +
                      (dCPrime / (kC * sC)) * (dHPrime / (kH * sH)) * rT
                 )
    );
}


#define deg2Rad(x) ((x)*(M_PI/180.0))

float
DeltaE2000(LAB lab1,LAB lab2)
{
    /* 
     * "For these and all other numerical/graphical delta E00 values
     * reported in this article, we set the parametric weighting factors
     * to unity(i.e., k_L = k_C = k_H = 1.0)." (Page 27).
     */
    const double k_L = 1.0, k_C = 1.0, k_H = 1.0;
    const double deg360InRad = deg2Rad(360.0);
    const double deg180InRad = deg2Rad(180.0);
    const double pow25To7 = 6103515625.0; /* pow(25, 7) */
   
   lab1.L*=100.0;lab2.L*=100.0;
   lab1.a*=200.0;lab2.a*=200.0;
   lab1.b*=200.0;lab2.b*=200.0;
   lab1.a-=100.0;lab2.a-=100.0;
   lab1.b-=100.0;lab2.b-=100.0;

    
    /*
     * Step 1 
     */
    /* Equation 2 */
    double C1 = sqrt((lab1.a * lab1.a) + (lab1.b * lab1.b));
    double C2 = sqrt((lab2.a * lab2.a) + (lab2.b * lab2.b));
    /* Equation 3 */
    double barC = (C1 + C2) / 2.0;
    /* Equation 4 */
    double G = 0.5 * (1 - sqrt(pow(barC, 7) / (pow(barC, 7) + pow25To7)));
    /* Equation 5 */
    double a1Prime = (1.0 + G) * lab1.a;
    double a2Prime = (1.0 + G) * lab2.a;
    /* Equation 6 */
    double CPrime1 = sqrt((a1Prime * a1Prime) + (lab1.b * lab1.b));
    double CPrime2 = sqrt((a2Prime * a2Prime) + (lab2.b * lab2.b));
    /* Equation 7 */
    double hPrime1;
    if (lab1.b == 0 && a1Prime == 0)
        hPrime1 = 0.0;
    else {
        hPrime1 = atan2(lab1.b, a1Prime);
        /* 
         * This must be converted to a hue angle in degrees between 0 
         * and 360 by addition of 2 to negative hue angles.
         */
        if (hPrime1 < 0)
            hPrime1 += deg360InRad;
    }
    double hPrime2;
    if (lab2.b == 0 && a2Prime == 0)
        hPrime2 = 0.0;
    else {
        hPrime2 = atan2(lab2.b, a2Prime);
        /* 
         * This must be converted to a hue angle in degrees between 0 
         * and 360 by addition of 2 to negative hue angles.
         */
        if (hPrime2 < 0)
            hPrime2 += deg360InRad;
    }
    
    /*
     * Step 2
     */
    /* Equation 8 */
    double deltaLPrime = lab2.L - lab1.L;
    /* Equation 9 */
    double deltaCPrime = CPrime2 - CPrime1;
    /* Equation 10 */
    double deltahPrime;
    double CPrimeProduct = CPrime1 * CPrime2;
    if (CPrimeProduct == 0)
        deltahPrime = 0;
    else {
        /* Avoid the fabs() call */
        deltahPrime = hPrime2 - hPrime1;
        if (deltahPrime < -deg180InRad)
            deltahPrime += deg360InRad;
        else if (deltahPrime > deg180InRad)
            deltahPrime -= deg360InRad;
    }
    /* Equation 11 */
    double deltaHPrime = 2.0 * sqrt(CPrimeProduct) *
        sin(deltahPrime / 2.0);
    
    /*
     * Step 3
     */
    /* Equation 12 */
    double barLPrime = (lab1.L + lab2.L) / 2.0;
    /* Equation 13 */
    double barCPrime = (CPrime1 + CPrime2) / 2.0;
    /* Equation 14 */
    double barhPrime, hPrimeSum = hPrime1 + hPrime2;
    if (CPrime1 * CPrime2 == 0) {
        barhPrime = hPrimeSum;
    } else {
        if (fabs(hPrime1 - hPrime2) <= deg180InRad)
            barhPrime = hPrimeSum / 2.0;
        else {
            if (hPrimeSum < deg360InRad)
                barhPrime = (hPrimeSum + deg360InRad) / 2.0;
            else
                barhPrime = (hPrimeSum - deg360InRad) / 2.0;
        }
    }
    /* Equation 15 */
    double T = 1.0 - (0.17 * cos(barhPrime - deg2Rad(30.0))) +
        (0.24 * cos(2.0 * barhPrime)) +
        (0.32 * cos((3.0 * barhPrime) + deg2Rad(6.0))) -
        (0.20 * cos((4.0 * barhPrime) - deg2Rad(63.0)));
    /* Equation 16 */
    double deltaTheta = deg2Rad(30.0) *
        exp(-pow((barhPrime - deg2Rad(275.0)) / deg2Rad(25.0), 2.0));
    /* Equation 17 */
    double R_C = 2.0 * sqrt(pow(barCPrime, 7.0) /
        (pow(barCPrime, 7.0) + pow25To7));
    /* Equation 18 */
    double S_L = 1 + ((0.015 * pow(barLPrime - 50.0, 2.0)) /
        sqrt(20 + pow(barLPrime - 50.0, 2.0)));
    /* Equation 19 */
    double S_C = 1 + (0.045 * barCPrime);
    /* Equation 20 */
    double S_H = 1 + (0.015 * barCPrime * T);
    /* Equation 21 */
    double R_T = (-sin(2.0 * deltaTheta)) * R_C;
    
    /* Equation 22 */
    double deltaE = sqrt(
        pow(deltaLPrime / (k_L * S_L), 2.0) +
        pow(deltaCPrime / (k_C * S_C), 2.0) +
        pow(deltaHPrime / (k_H * S_H), 2.0) + 
        (R_T * (deltaCPrime / (k_C * S_C)) * (deltaHPrime / (k_H * S_H))));
    
    return ((float)deltaE);
}

/* additions for rampart */

static int supports_ansi = -1, supports_truecolor = 0, color_count=0;

#define COLOR_PARSE_ERROR INT_MIN
#define COLOR_PARSE_RGB -1
#define COLOR_NOT_PARSED -2
#define CCODE_FLAG_FLASHING               (1<<10)  // used internally to keep track of flashing

static int term_colors_and_truecolor(int *colors, int *truecolor) {
    const char *term = getenv("TERM");
    const char *cterm = getenv("COLORTERM");
    char line[512];
    int found_any = 0;

    if (!colors || !truecolor) return -1;
    *colors = 0;
    *truecolor = 0;

    if (!term || !*term) return -1;

    /* 1) Try terminfo via `infocmp -1 $TERM` */
    {
        char cmd[128];
        FILE *fp;
        int parsed_colors = 0, parsed_true = 0;

        /* Safe command build */
        size_t n = snprintf(cmd, sizeof(cmd), "infocmp -1 %s 2>/dev/null", term);
        if (n < sizeof(cmd)) {
            fp = popen(cmd, "r");
            if (fp) {
                while (fgets(line, sizeof(line), fp)) {
                    /* Look for colors#N */
                    char *p = strstr(line, "colors#");
                    if (p) {
                        p += 7;
                        int v = 0;
                        while (*p >= '0' && *p <= '9') {
                            v = (v << 3) + (v << 1) + (*p - '0'); /* v = v*10 + digit using shifts */
                            ++p;
                        }
                        if (v > 0) { *colors = v; parsed_colors = 1; found_any = 1; }
                    }
                    /* Truecolor flags (common in terminfo dumps):
                       - "RGB" boolean cap (some ncurses)
                       - "Tc" boolean cap (tmux/others)
                       - presence of setrgbf/setrgbb strings also implies truecolor */
                    if (strstr(line, " RGB") || strstr(line, "=RGB") || strstr(line, ",RGB,"))
                        { *truecolor = 1; parsed_true = 1; found_any = 1; }
                    if (strstr(line, " Tc") || strstr(line, "=Tc") || strstr(line, ",Tc,"))
                        { *truecolor = 1; parsed_true = 1; found_any = 1; }
                    if (strstr(line, "setrgbf=") || strstr(line, "setrgbb="))
                        { *truecolor = 1; parsed_true = 1; found_any = 1; }
                }
                pclose(fp);
            }
        }

        /* If truecolor is asserted and colors not given, set a meaningful upper bound */
        if (parsed_true && !parsed_colors) { *colors = 16777216; found_any = 1; }
        if (parsed_colors || parsed_true) return 0;
    }

    /* 2) Fallback heuristics from TERM/COLORTERM (no ncurses) */
    if (cterm && (strstr(cterm, "truecolor") || strstr(cterm, "24bit"))) {
        *truecolor = 1;
        *colors = 16777216;
        return 0;
    }
    if (strstr(term, "-direct")) {
        *truecolor = 1;
        *colors = 16777216;
        return 0;
    }
    if (strstr(term, "256color")) { *colors = 256; return 0; }
    if (strstr(term, "88color"))  { *colors = 88;  return 0; }

    /* Some terms imply 16 colors */
    if (strstr(term, "screen") || strstr(term, "xterm") || strstr(term, "vt100") || strstr(term, "ansi")) {
        *colors = 16; /* conservative */
        return 0;
    }

    /* Last resort */
    *colors = 8;
    return found_any ? 0 : -1;
}

static void detect_terminal_capabilities(void) {
    const char *term = getenv("TERM");

    if (!term || !isatty(STDOUT_FILENO) || !strcmp(term, "dumb")) {
        supports_ansi = 0;
        color_count   = 0;
        supports_truecolor = 0;
        return;
    }

    supports_ansi = 1;

    term_colors_and_truecolor(&color_count, &supports_truecolor);
}



CCODES *new_color_codes()
{
    CCODES *ret = calloc(1, sizeof(CCODES) );

    if(!ret)
    {
        fprintf(stderr, "error: calloc()\n");
        abort();
    }
    ret->fg.color_index=COLOR_NOT_PARSED;
    ret->bg.color_index=COLOR_NOT_PARSED;
    return ret;
}

CCODES *free_color_codes(CCODES *c)
{
    if(c->html_start)
        free(c->html_start);
    if(c->blink_or_class)
        free(c->blink_or_class);
    free(c);
    return NULL;
}


static int16_t colornames_closest_index(uint8_t *rgb, float *retdist, int min, int max)
{
    PIXEL pix, comppix;
    const struct color_entry *compe;
    int i=0;
    LAB l1, l2;
    float dist, bestdist=FLT_MAX;
    int16_t ret=-1;

    pix.r=rgb[0];
    pix.g=rgb[1];
    pix.b=rgb[2];
    l1 = rgbToLab(pix);
    
    
    for(i=min;i<max;i++)
    {
        compe=&colornames[i];
        comppix.r=compe->r;
        comppix.g=compe->g;
        comppix.b=compe->b;
        l2=rgbToLab(comppix);

        dist = DeltaE2000(l1,l2);
        if(dist<bestdist) {
            bestdist=dist;
            ret=i;
        }
        
    }
    if(retdist)
        *retdist=bestdist;

    return ret;
}

static int parse_rgb_triplet(const char *s, int *r, int *g, int *b) {
    const char *p = s;
    char *end;
    long v;

    if (!p || strncmp(p, "rgb(", 4) != 0) return 0;
    p += 4;

    errno = 0;
    v = strtol(p, &end, 10);
    if (end == p || errno)
        return 0;
    p = end;
    while (*p == ' ') ++p;
    if (*p != ',')
        return 0;
    ++p;
    if(v<0 || v>255)
        return 0;
    //if (v < 0) v = 0; 
    //else if (v > 255) v = 255; 
    *r = (int)v;

    errno = 0;
    v = strtol(p, &end, 10);
    if (end == p || errno)
        return 0;
    p = end;
    while (*p == ' ') ++p;
    if (*p != ',')
        return 0; 
    ++p;
    if(v<0 || v>255)
        return 0;
    //if (v < 0) v = 0;
    //else if (v > 255) v = 255;
    *g = (int)v;

    errno = 0;
    v = strtol(p, &end, 10);
    if (end == p || errno)
        return 0;
    p = end;
    while (*p == ' ') ++p;
        if (*p != ')') return 0;
    if(v<0 || v>255)
        return 0;
    //if (v < 0) v = 0;
    //else if (v > 255) v = 255;
    *b = (int)v;

    return 1;
}

/* Parse a color token: name, 0-255 index, or rgb(r,g,b) */
// if rgb, return -1;
// if error return COLOR_PARSE_ERROR
static int parse_color_token(const char *token, uint8_t *r, uint8_t *g, uint8_t *b) {
    int i=0;
    *r=-1;
    *b=-1;
    *g=-1;

    // look for name
    for (i = 0; i < n_color_entries; i++) {
        if (strncasecmp(token, colornames[i].name, strlen(colornames[i].name)) == 0)
        {
            *r=colornames[i].r;
            *g=colornames[i].g;
            *b=colornames[i].b;
            return i;
        }
    }

    // look for a number, treat it as index of ansi256
    if (isdigit((unsigned char)token[0])) {
        i = atoi(token);
        if (i >= 0 && i <= 255) {
            *r=colornames[i].r;
            *g=colornames[i].g;
            *b=colornames[i].b;
            return i;
        }
    }

    if (strncmp(token, "rgb(", 4) == 0) {
        int rr, gg, bb;
        if (parse_rgb_triplet(token, &rr, &gg, &bb)) {
            *r = rr; *g = gg; *b = bb;
            return COLOR_PARSE_RGB;
        }
    }

    return COLOR_PARSE_ERROR;
}

static int make_term_codes(CCODES *c)
{
    char *p = c->term_start;
    int written = 0;
    const struct color_entry *e;
    uint8_t r  = c->fg.rgb[0];
    uint8_t g  = c->fg.rgb[1];
    uint8_t b  = c->fg.rgb[2];
    uint8_t br = c->bg.rgb[0];
    uint8_t bg = c->bg.rgb[1];
    uint8_t bb = c->bg.rgb[2];

    

    if(supports_ansi<1 && !(c->flags & CCODE_FLAG_FORCE_TERM))
        return 0;

    strcpy(c->term_end, "\033[0m");

    strcpy(p, "\033[");
    p+=2;

    if(supports_truecolor || (c->flags & CCODE_FLAG_FORCE_TERM_TRUECOLOR)) {
        if(c->fg.color_index != COLOR_NOT_PARSED)
        {
            p += sprintf(p, "38;2;%d;%d;%d", r, g, b);
            written=1;
        }
        if(c->bg.color_index != COLOR_NOT_PARSED)
        {
            if (written) *(p++) = ';';
            p += sprintf(p, "48;2;%d;%d;%d", br, bg, bb);
        }
        return 0;
    }

    if(c->fg.color_index > COLOR_NOT_PARSED) //-2
    {
        int idx=0;
        if(c->fg.color_index == COLOR_PARSE_RGB) //-1
        {
            idx = colornames_closest_index(c->fg.rgb, &(c->fg.distance), 0, n_ansi_color_entries);
        }
        else if (c->fg.color_index < n_ansi_color_entries)
        {
            idx = c->fg.color_index;
            e=&colornames[idx];
            c->fg.distance = e->distance;
        }
        else if(c->fg.color_index < n_color_entries)
        {
            idx = colornames[c->fg.color_index].closest_index;      //pre calculated closest ansi256 to css colors
            e=&colornames[idx];
            c->fg.distance = e->distance;
        }
        if(color_count >= 256 || (c->flags & CCODE_FLAG_FORCE_TERM_256))
            p += sprintf(p, "38;5;%d", idx);
        else
            p += sprintf(p, "%d", 30 + idx % 8 + (idx >= 8 ? 60 : 0));
            
        written=1;
    }
    if(c->bg.color_index != COLOR_NOT_PARSED)
    {
        int idx=0;
        if (written) *(p++) = ';';
        if(c->bg.color_index == COLOR_PARSE_RGB) //-1
        {
            idx = colornames_closest_index(c->bg.rgb, &(c->bg.distance), 0, n_ansi_color_entries);
        }
        else if (c->bg.color_index < n_ansi_color_entries)
        {
            idx = c->bg.color_index;
            e=&colornames[idx];
            c->bg.distance = e->distance;
        }
        else if(c->bg.color_index < n_color_entries)
        {
            idx = colornames[c->bg.color_index].closest_index;      //pre calculated closest ansi256 to css colors
            e=&colornames[idx];
            c->bg.distance = e->distance;
        }
        if (color_count >= 256 || (c->flags & CCODE_FLAG_FORCE_TERM_256))
            p += sprintf(p, "48;5;%d", idx);
        else
            p += sprintf(p, "%d", 40 + idx % 8 + (idx >= 8 ? 60 : 0));
    }

    if (c->flags & CCODE_FLAG_FLASHING) {
        if (written) *(p++) = ';';
        *(p++) = '5';
    }
    *(p++) = 'm';
    *p = '\0';

    return 0;
}

// parse_name, fill in rgb values and code index for fore and background colors, and blink
static int parse_name(CCODES *c)
{
    uint8_t *r  = &(c->fg.rgb[0]);
    uint8_t *g  = &(c->fg.rgb[1]);
    uint8_t *b  = &(c->fg.rgb[2]);
    uint8_t *br = &(c->bg.rgb[0]);
    uint8_t *bg = &(c->bg.rgb[1]);
    uint8_t *bb = &(c->bg.rgb[2]);
    int i=0;
    char *p = NULL;
    char *buf;
    const char *fg_tok = NULL;
    const char *bg_tok = NULL;

    buf = strdup(c->lookup_names);
    if (!buf) {
        fprintf(stderr, "could not strdup()");
        abort();
    }
    //replace rgb(d,d,d),rgb(d,d,d) with rgb(d,d,d)|rgb(d,d,d)
    char *s=buf;
    int inparan=0;
    while(*s)
    {
        switch(*s) {
            case '(':
                inparan=1;break;
            case ',':
                if(!inparan)
                    *s='|';
                break;
            case ')':
                inparan=0;break;
        }
        s++;
    }

    char *token = strtok_r(buf, "|", &p);
    while (token) {
        i++;
        while (*token == ' ') ++token;

        if (!fg_tok && strncasecmp(token, "flashing", 8) && strncasecmp(token, "blink", 5))
            fg_tok = token;
        else if (!bg_tok && strncasecmp(token, "flashing",8 ) && strncasecmp(token, "blink", 5))
            bg_tok = token;

        if (!strncasecmp(token, "flashing",8) || !strncasecmp(token, "blink",5))
        {
            c->blink_or_class=strdup(token);
            c->flags |= CCODE_FLAG_FLASHING;
        }
        if(i==3) {
            if(!c->blink_or_class)
                c->blink_or_class=strdup(token);
            break;
        }
        token = strtok_r(NULL, "|", &p);
    }

    if (!fg_tok && !bg_tok && !c->blink_or_class) {
        free(buf);
        return 0;
    }

    // trim end
    if(c->blink_or_class)
    {
        p=c->blink_or_class + strlen(c->blink_or_class)-1;
        while (p>c->blink_or_class) {
            if(isspace(*p))
                *p='\0';
            else
                break;
            p--;
        }
    }
    if (fg_tok) {
        int idx = parse_color_token(fg_tok, r, g, b);

        if(idx == COLOR_PARSE_ERROR)
            return 1;

        c->fg.color_index = idx;
    }

    if (bg_tok && c->flags & CCODE_FLAG_WANT_BKGND) {
        int idx = parse_color_token(bg_tok, br, bg, bb);

        if(idx == COLOR_PARSE_ERROR)
            return 1;

        c->bg.color_index = idx;
    }

    return 0;
}

static int nearest_ansi_color(CCODES *c)
{
    int idx;

    //foreground
    idx = c->fg.color_index;

    // if rgb
    if( idx == COLOR_PARSE_RGB )
        c->fg.color_index = colornames_closest_index(c->fg.rgb, &(c->fg.distance), 0, n_ansi_color_entries)    ;
    // if webcolor
    else if (idx >= css_color_start && idx < n_color_entries)
    {
        c->fg.color_index = colornames[c->fg.color_index].closest_index;
        c->fg.distance = colornames[c->fg.color_index].distance;
    }
    // else not parsed or error or already an ansi color

    // background
    idx = c->bg.color_index;

    // if rgb
    if( idx == COLOR_PARSE_RGB )
        c->bg.color_index = colornames_closest_index(c->bg.rgb, &(c->bg.distance), 0, n_ansi_color_entries)    ;
    // if webcolor
    else if (idx >= css_color_start && idx < n_color_entries)
    {
        c->bg.color_index = colornames[c->bg.color_index].closest_index;
        c->bg.distance = colornames[c->bg.color_index].distance;
    }
    // else not parsed or error or already an ansi color
    return 0;
}

static int nearest_css_color(CCODES *c)
{
    int idx;

    //foreground
    idx = c->fg.color_index;

    // if rgb
    if( idx == COLOR_PARSE_RGB )
        c->fg.color_index = colornames_closest_index(c->fg.rgb, &(c->fg.distance), css_color_start, n_color_entries);    
    // if ansi color
    else if (idx >= 0 && idx < n_ansi_color_entries)
    {
        c->fg.color_index = colornames[c->fg.color_index].closest_index;
        c->fg.distance = colornames[c->fg.color_index].distance;
    }
    // else not parsed or error or already a web color

    // background
    idx = c->bg.color_index;
    // if rgb
    if( idx == COLOR_PARSE_RGB )
        c->bg.color_index = colornames_closest_index(c->bg.rgb, &(c->bg.distance), css_color_start, n_color_entries);    
    // if ansi color
    else if (idx >= 0 && idx <n_ansi_color_entries)
    {
        c->bg.color_index = colornames[c->bg.color_index].closest_index;
        c->bg.distance = colornames[c->bg.color_index].distance;
    }
    // else not parsed or error or already a web color
    return 0;
}

static void put_css_colname(char *buf, CCOLOR *cc)
{
	// if it was an rgb or ansi color and was converted to closest css color,
	// then we assume that's what you want in the span style
	if(cc->color_index >= css_color_start && cc->color_index < n_color_entries)
		strcpy(buf, colornames[cc->color_index].name);
	else
		sprintf(buf, "rgb(%d,%d,%d)", cc->rgb[0], cc->rgb[1], cc->rgb[2]);
}

static int put_html_text(CCODES *c)
{
	char fgcolor[64];
	char bgcolor[64];
	char outb[256];

	CCOLOR *fg=&(c->fg), *bg=&(c->bg);

	if(fg->color_index<COLOR_PARSE_RGB && bg->color_index<COLOR_PARSE_RGB)
		return 2;

	if(fg->color_index>=COLOR_PARSE_RGB)
		put_css_colname(fgcolor, fg);

	if(bg->color_index>=COLOR_PARSE_RGB)
		put_css_colname(bgcolor, bg);

	if(fg->color_index<COLOR_PARSE_RGB)
		sprintf(outb, "<span class=\"rp-color%s%s\" style=\"background-color:%s;\">",
			c->blink_or_class ? " ":"", c->blink_or_class ? c->blink_or_class :"",
			bgcolor);
	else if(bg->color_index<COLOR_PARSE_RGB)
		sprintf(outb, "<span class=\"rp-color%s%s\" style=\"color:%s;\">",
			c->blink_or_class ? " ":"", c->blink_or_class ? c->blink_or_class :"",
			fgcolor);
	else
		sprintf(outb, "<span class=\"rp-color%s%s\" style=\"color:%s;background-color:%s\">",
			c->blink_or_class ? " ":"", c->blink_or_class ? c->blink_or_class :"",
			fgcolor, bgcolor);

	c->html_start=strdup(outb);
	strcpy(c->html_end,"</span>");

	return 0;
}

int rp_color_convert(CCODES *c) {
    int errcode = 0;

    if(supports_ansi == -1)
       detect_terminal_capabilities(); 

    // set color_index if named color, or a string index
    // otherwise only set rgb value
    if(c->flags & CCODE_FLAG_HAVE_NAME)
        if( (errcode=parse_name(c)) )
            return errcode;
    // else we assume rgb is filled in.  if not, you get black.

    if(c->flags & CCODE_FLAG_WANT_TERM)
        if( (errcode=make_term_codes(c)) )
            return errcode;

    if(c->flags & CCODE_FLAG_WANT_ANSICOLOR)
    {
        if( (errcode=nearest_ansi_color(c)) )
            return errcode;
    }
    else if(c->flags & CCODE_FLAG_WANT_CSSCOLOR)
        if( (errcode=nearest_css_color(c)) )
            return errcode;

    if(c->flags & CCODE_FLAG_WANT_HTMLTEXT)
        if( (errcode=put_html_text(c)) )
            return errcode;

    return 0;
}

