/* bgr565torgb - convert 16-bit BGR565 pixels to 24-bit RGB pixels

  Written in 2016 by Glenn Randers-Pehrson <glennrp@users.sf.net>

  To the extent possible under law, the author has dedicated all copyright
  and related and neighboring rights to this software to the public domain
  worldwide. This software is distributed without any warranty.
  See <http://creativecommons.org/publicdomain/zero/1.0/>. 

  Use with ImageMagick or GraphicsMagick to convert 16-bit BGR565 pixels
  to 24-bit RGB pixels, e.g.,

      bgr565torgb < file.bgr565 > file.rgb
      magick -size WxH -depth 8 file.rgb file.png 
*/

#include <stdio.h>
int main()
{
    int rgbhi,rgblo,red,green,blue;
    while (1) {
        rgbhi=getchar(); if (rgbhi == EOF) return (0);
        rgblo=getchar(); if (rgblo == EOF) return (1);
        putchar((rgblo & 0x1F) << 3 | (rgblo & 0x14) >> 3 );
        putchar((rgbhi & 0x07) << 5 |
                (rgblo & 0xE0) >> 3 |
                (rgbhi & 0x06) >> 1);
        putchar((rgbhi & 0xE0) | (rgbhi >> 5) & 0x07);
    }
}
