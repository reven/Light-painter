# Light painter

This is my take on a sketch that drives a LED strip to create persistance of vision (POV) images from 24-bit bitmap files, that can be used to paint images in long exposure photography. My version has an LCD menu to select images and adjust brightness, speed and delay, as well as save the settings to EEPROM.

This sketch is based on the the one written by [Phil Burgess / Paint Your Dragon](https://github.com/PaintYourDragon) for Adafruit, that can be found [here](https://github.com/adafruit/NeoPixel_Painter). A complete write-up with instructions and hardware design can be found on [Adafruit's website](https://learn.adafruit.com/neopixel-painter/overview).

If you want more information on the project, you can find it [here](https://en.reven.org/2018/04/04/light-painter/). STL files for the enclosure and other bits can be found on [Thingiverse](https://www.thingiverse.com/thing:2849881).

**Impotant**: Please read the known issues and understand well the current draw of your LEDs and the capabilities of your power source.

## Setup
This sketch works on an Arduino UNO. You will also need an adressable LED strip, a power source (an UBEC and an AA battery pack work well), a LCD display (preferably with I2C module), some momentary push buttons, a slider switch and an SD card reader module.

The hardware will depend of your choice of materials or what you already have available. I used wood as the structural beam and I 3D printed the enclosure and other parts (will add STL files shortly).

## Rationale
I wanted to add an LCD interface but I also wanted something a bit more flexible in terms of settings, that could be adjusted through a menu. Phils sketch ingeniously relied on a momentary switch button and a potentiometer, that had different functionality depending on when they were adjusted. I wanted to take it to the next step and have a complete menu system with on the fly conversion and EEPROM saving of config values.

Also, I wanted the sketch to handle filenames that were meaningful, in order to be able to select them in the menu. For example "redStripes.bmp" instead of "frame001.bmp" [though this comes at a hefty memory cost].

Additionally, I wanted to maximise the brightness of my strip and to make the cross-image equalization optional, because *frame painting* isn't a feature I really have in mind using.

## To do:
* ~~Finish implementing the consistency flag~~
* Clean up code
* Integrate debug flag and output functions (see known issues)
* Optimizations
* ~~Write up build instructions~~
* ~~Add STL files for housing and battery holder~~

## Main configuration options
Options are well commented in sketch. Mainly, define your LED type, their number, and the pins you are using for your inputs/outputs. Additionally:

* set `#define CONSISTENT` if you want all your images to be balanced out. This is useful for frame painting or animations, but not too much if you're not going to be doing that.
* set `#define CURRENT_MAX` to a reasonable value that your power supply can deliver, **but be sure to read and understand the limitations explained bellow in known issues and the *correction* factor introduced in the code**.

## Known Issues:

#### Memory, memory, memory...
This sketch is pretty packed. There is not a lot of free memory and there are a lot of memory hungry functions. During development I had to choose whether to have Serial or have LCD, but not both. Some variables cause stack collisions if not severly limited (like the number of files, see below). It has to be optimized and a debug mode can be more elegantly implemented with compiler flags.

#### Measured current vs calculated current drawn.
Adafruit's original sketch has a lot of controls and safeguards to not exceed the maximum capacity of your power source. *However*, I found that the measured current was well bellow the theoretical current limit set in the sketch.

Most of the diming and equalization of images takes place in the bmpProcess() function. In it, a *lineMax* variable is defined as the cumulative brightness of the brightest line in a file, and that value is used to scale down the brightness of the image. This process overshoots by a lot, at least in my measurements. With a max of 3500mA I was reading around 980mA with a really bright image.

I introduced a correction factor of 1.4 to lineMax, in order to boost the brightness and this provides a current draw still under half the theoretical max of my power supply. Increasing the factor caused a shift to red in the LEDs, the Arduino to hang altogether or huge voltage sags in my (probably crappy) UBEC. I don't have the proper equipment to measure the current precisely (possibly a osciloscope would give a better picture if it depends on the duty cycle) or there may be other fators in the correction that I don't understand.

To sumarize: **this is a dangerous change and probably not a good idea**. Set it back to 1 unless you're sure; and again, make sure you understand the capabilities of your power source and the chatacteristics of your LEDs.

#### Memory and file indexes
In order to save precious memory, the files are accessed by their *Index* on the SD card directory, and the number is stored as a uint8_t. That's one byte per file on the SD (about 30 bytes with current max). With a perfectly clean filesystem and no long names, that would give us room for about 70 files or so (not sure how indexes are assigned...) long before before the index reaches values above 255 and overflows, even taking the generated raw files into account. *However*, the index number grows rather quickly when the user OS adds hidden files and directories, causing the index number to grow past 255.

We could change the index to a uint16_t, but that would require double the memory (60 bytes for 30 files). Another option would be to somehow clean and optimize the SD directory, moving files or deleteing system files, but that's risky and beyond the scope of this sketch.

For now just heed this warning and be careful that your index values aren't overflowing, after a lot of file movements. Reformating the card and copying over the files again via terminal (without the extra OS files) would recreate the directory with lower index values. For extra margin, copy the bmp files first, as those are the only ones indexed.

In practice, the file limit is currently set at 30 because of lack of memory, though there is plenty space for optimization in the sketch.

## Original Adafruit sketch comments and info
```
ADAFRUIT NEOPIXEL LIGHT PAINTER SKETCH: Reads 24-bit BMP image from
SD card, plays back on NeoPixel strip for long-exposure photography.

Requires SdFat library for Arduino:
http://code.google.com/p/sdfatlib/

As written, uses a momentary pushbutton connected between pin A1 and
GND to trigger playback.  An analog potentiometer connected to A0 sets
the brightness at startup and the playback speed each time the trigger
is tapped.  BRIGHTNESS IS SET ONLY AT STARTUP; can't adjust this in
realtime, not fast enough.  To change brightness, set dial and tap reset.
Then set dial for playback speed.

This is a 'plain vanilla' example with no UI or anything -- it always
reads a fixed set of files at startup (frame000.bmp - frameNNN.bmp in
root folder), outputs frameNNN.tmp for each (warning: doesn't ask, just
overwrites), then plays back from the file(s) each time button is tapped
(repeating in loop if held).  More advanced applications could add a UI
(e.g. 16x2 LCD shield), but THAT IS NOT IMPLEMENTED HERE, you will need
to get clever and rip up some of this code for such.

This is well-tailored to the Arduino Uno or similar boards.  It may work
with the Arduino Leonardo *IF* your SD shield or breakout board uses the
6-pin ICSP header for SPI rather than pins 11-13.  This WILL NOT WORK
with 'soft' SPI on the Arduino Mega (too slow).  Also, even with 'hard'
SPI, this DOES NOT RUN ANY FASTER on the Mega -- a common misconception.

Adafruit invests time and resources providing this open source code,
please support Adafruit and open-source hardware by purchasing
products from Adafruit!

Written by Phil Burgess / Paint Your Dragon for Adafruit Industries.
BSD license, all text above must be included in any redistribution.
```
