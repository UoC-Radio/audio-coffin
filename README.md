![alt tag](/images/dracula.png)
# audio-coffin
A simple audio recorder/logger on top of Jack, libsndfile and libsoxr

For [UoC Radio 96.7](https://radio.uoc.gr) (the radio station operated by University of Crete's students), we are required by the law to keep audio logs 24/7 for a specified ammount of time so we wanted an audio logger. We also wanted a recorder for recording live shows. I didn't like any of the currently available stuff out there so I decided to write one from scratch. Jack is used for audio I/O, libsndfile is used for compressing and encoding the audio to FLAC/Ogg Vorbis and libsoxr is used for resampling the audio to the specified sampling rate. For the GUI I used GTK+ 3.0 but it can also work without GUI (headless), however since we always use the GUI, that feature is still WiP and does not get much testing.

Initialy this project had a very boring name "GJackRcd" and it also needed some Icons. Since I suck at drawing stuff, I asked some friends for help. Elena came up with the idea of the Dracula and the coffin and so I named the project Audio Coffin. You can see more of Elena's work on [her blog](https://relativetheoryofgenerality.wordpress.com/). Also many thanks to Antigone for turning Elena's drawings to icons and Christopher for drinking beers with me the whole time and helping in various ways.
