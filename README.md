Here is the codes for my Breath of the Wild Remote Bomb prop. I have a phone in my sheikah slate, so I wanted to connect a bomb with Bluetooth to control it.
I had to upload the HTML file to make the APK, so I might as well share it here for anyone who wants to try and make one.

The project was coded with Claude AI, so feel free to modify it as you want. You might want to change the LED count, its currently set for 36 as I test it.

_________________________________________
Parts list:
- ESP-WROOM-32 dev board,
- MAX98357A I2S amp,
- Small 4Ω or 8Ω speaker (3W is plenty),
- A few WS2812B addressable LEDs



_________________________________________
Wiring:

MAX98357A (I2S amp) → ESP32

- VIN	          →   5V

- GND	          →   GND

- BCLK	        →   GPIO26

- LRC (WS)	    →   GPIO25

- DIN	          →   GPIO22

- GAIN	        →   100kohm resistor to GND for loudest sound

- SD	          →   tie to VIN (keeps the amp always enabled)

Then wire the speaker's two leads to the MAX98357A's + and - output terminals.
_________________________________________

WS2812B LEDs → ESP32


- VCC	          →   5V

- GND	          →   GND

- DIN (data in)	→   GPIO27, through a ~330Ω resistor in series
