# nap
Nano Audio Player

Prepare the 8-bit audio file using sox with the following command:

sox infile.wav -b 8 -r 8000 audio1.wav

put the file(s) audio[1-N].wav onto the SD card and the Nano will play the file when the corresponding input pin is brought to ground.

audio1 => pin 2
audio2 => pin 3

Pin 4 is CS for SD Card

audio3 => pin 5
audio4 => pin 6
audio5 => pin 7
audio6 => pin 8
audio6 => pin 9

Pin 10 is audio Output
Pin 11 is MOSI
Pin 12 is MISO

