/*
 * speaker_pcm
 *
 * Plays 8-bit PCM audio on pin 10 using pulse-width modulation (PWM).
 * For Arduino Mega 2560
 *
 * Uses two timers. The first changes the sample value 8000 times a second.
 * The second holds pin 11 high for 0-255 ticks out of a 256-tick cycle,
 * depending on sample value. The second timer repeats 62500 times per second
 * (16000000 / 256), much faster than the playback rate (8000 Hz), so
 * it almost sounds halfway decent, just really quiet on a PC speaker.
 *
 * Takes over Timer 1 (16-bit) for the 8000 Hz timer. This breaks PWM
 * (analogWrite()) for Arduino pins 9 and 10. Takes Timer 2 (8-bit)
 * for the pulse width modulation, breaking PWM for pins 11 & 3.
 *
 * References:
 *     http://www.uchobby.com/index.php/2007/11/11/arduino-sound-part-1/
 *     http://www.atmel.com/dyn/resources/prod_documents/doc2542.pdf
 *     http://www.evilmadscientist.com/article.php/avrdac
 *     http://gonium.net/md/2006/12/27/i-will-think-before-i-code/
 *     http://fly.cc.fer.hr/GDM/articles/sndmus/speaker2.html
 *     http://www.gamedev.net/reference/articles/article442.asp
 *
 * Michael Smith <michael@hurts.ca>
 * Incremental encoding <peterr@ncmls.org)
 * 0 nybble means change direction +-
 */

#include <stdint.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>


#define DEBUG 1
// Can be either 3 or 11, two PWM outputs connected to Timer 2
#define speakerPin 10 

#if (speakerPin==10)
#define COMON1 COM2A1
#define COMON0 COM2A0
#define COMOFF1 COM2B1
#define COMOFF0 COM2B0
#define _OCR_ OCR2A
#else
#define COMON1 COM2B1
#define COMON0 COM2B0
#define COMOFF1 COM2A1
#define COMOFF0 COM2A0
#define _OCR_ OCR2B
#endif

#define SAMPLE_RATE 8000

/*
 * The audio data needs to be unsigned, 8-bit, 8000 Hz, and small enough
 * to fit in flash. 10000-13000 samples is about the limit.
 *
 * sounddata.h should look like this:
 *     const int sounddata_length=10000;
 *     const unsigned char sounddata_data[] PROGMEM = { ..... };
 *
 * You can use wav2c from GBA CSS:
 *     http://thieumsweb.free.fr/english/gbacss.html
 * Then add "PROGMEM" in the right place. I hacked it up to dump the samples
 * as unsigned rather than signed, but it shouldn't matter.
 *
 * http://musicthing.blogspot.com/2005/05/tiny-music-makers-pt-4-mac-startup.html
 * mplayer -ao pcm macstartup.mp3
 * sox audiodump.wav -v 1.32 -c 1 -r 8000 -u -1 macstartup-8000.wav
 * sox macstartup-8000.wav macstartup-cut.wav trim 0 10000s
 * wav2c macstartup-cut.wav sounddata.h sounddata
 *
 * (starfox) nb. under sox 12.18 (distributed in CentOS 5), i needed to run
 * the following command to convert my wav file to the appropriate format:
 * sox audiodump.wav -c 1 -r 8000 -u -b macstartup-8000.wav
 */

int ledPin = 13;

#include <SD.h> /* SD card MOSI=11, MISO=12, CLK=13, CS=pin 4 */

File myFile;
boolean playing;
byte lastSample;
int dataCount;

unsigned char nextByte() {
	if (myFile.available()) {
	    lastSample = myFile.read();
	    dataCount++;
	    return lastSample;
	 } else {
	   playing = false;
	 }
 }

void findData()
{
int count = 0;
int state;
char c;
  if (myFile) {
    state = 0;
    while (myFile.available() and state < 8 ) {
    	count++;
  	c = myFile.read();
	if (state > 3 )                  state++;
	else if (state == 0 && c == 'd') state = 1;
	else if (state == 1 && c == 'a') state = 2;
	else if (state == 2 && c == 't') state = 3;
	else if (state == 3 && c == 'a') state = 4;
    }
    if (state == 8) {
       Serial.print(count);
       Serial.println(" bytes: found start of audio data");
    }
  } else
  Serial.println("error opening audio1.wav");
}

void stopPlayback()
{
    TIMSK1 &= ~_BV(OCIE1A);    // Disable playback per-sample interrupt.
    TCCR1B &= ~_BV(CS10);      // Disable the per-sample timer completely.
    TCCR2B &= ~_BV(CS10);      // Disable the PWM timer.
    digitalWrite(speakerPin, LOW);
}


ISR(TIMER1_COMPA_vect) {
    if (playing)
       _OCR_ = nextByte();    // Playing
    else if (lastSample > 0)
       _OCR_ = lastSample--;  // Fade out
    else 
    	stopPlayback();       // Stopped
}

void startPlayback()
{
    pinMode(speakerPin, OUTPUT);
				      // Timer 2 to do PWM
    ASSR &= ~(_BV(EXCLK) | _BV(AS2)); // Use internal clock (datasheet p.160)
    TCCR2A |= _BV(WGM21) | _BV(WGM20);// Set fast PWM mode  (p.157)
    TCCR2B &= ~_BV(WGM22);

    // Macros change the following code to setup the right pin (10 or 3)
    TCCR2A = (TCCR2A | _BV(COMON1)) & ~_BV(COMON0);
    TCCR2A &= ~(_BV(COMOFF1) | _BV(COMOFF0));
    TCCR2B = (TCCR2B & ~(_BV(CS12) | _BV(CS11))) | _BV(CS10);
    _OCR_ = nextByte();

    cli(); // Set Timer 1 to send a sample every interrupt.

    // Set CTC mode (Clear Timer on Compare Match) (p.133)
    // Have to set OCR1A *after*, otherwise it gets reset to 0!
    TCCR1B = (TCCR1B & ~_BV(WGM13)) | _BV(WGM12);
    TCCR1A = TCCR1A & ~(_BV(WGM11) | _BV(WGM10));

    // No prescaler (p.134)
    TCCR1B = (TCCR1B & ~(_BV(CS12) | _BV(CS11))) | _BV(CS10);

    // Set the compare register (OCR1A).
    // OCR1A is a 16-bit register, so we have to do this with
    // interrupts disabled to be safe.
    OCR1A = F_CPU / SAMPLE_RATE;    // 16e6 / 8000 = 2000

    // Enable interrupt when TCNT1 == OCR1A (p.136)
    TIMSK1 |= _BV(OCIE1A);
    playing = true;
    sei();
}

boolean once;

void setup()
{
#ifdef DEBUG
  Serial.begin(9600);
   while (!Serial) {;} // wait for serial port connection(Leonardo only)
#endif
  once = true;
  dataCount = 0;
  // CS=4 output(default) Even if not used as the CS pin, the hardware SS pin
  // (usually 10, Mega 53) must be output or SD lib functions will not work. 
  pinMode(10, OUTPUT);
   
  if (!SD.begin(4))
    Serial.println("SD Card initialization failed!");
  
  myFile = SD.open("audio1.wav");
  findData();  // Scan to audio data, and set global size

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  startPlayback();
}

void loop()
{
    if (once) {
       Serial.print("Starting audio at ");
       Serial.print(millis());
       Serial.println("ms");
       while (playing)
       	     delay(100);
       Serial.print("finished audio at ");
       Serial.print(millis());
       Serial.println("ms");
       once = false;
    }
}





