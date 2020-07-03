
# About

An audio external for Windows version of Max 8 (Max/MSP). _fl_granular~_ external is a stereo granular synthesizer, it plays a lot of short duration audio samples called 'grains', all at the same time. This grains are produced by multiplying an amplitude window to an specific sector of an audio buffer. In particular the source audio can be linked with a _buffer~_ object, and the shape of the window can be made with a list in curve format from the _function~_ object (the same way [fl_fasor~](https://github.com/ruidoenambar/fl_fasor-max) works). When a new window is made, _fl_granular~_ will make a crossfade between the old and the new window. The user can set the duration of the crossfade, which goes up to 10000ms, and the type of crossfade, that can be chosen between *Linear Crossfade*, *Power Crossfade* and *No crossfade*. The source audio linked with a buffer and a backup of the same buffer, are copied in the external so the user can change the audio source in the buffer without affecting grains that are still playing the old buffer, this produces a little downside: you won't be able to load a new buffer until there are no grains using the backup buffer.

This external has an automatic mode and a manual mode, on the automatic mode, a new grain will be produced by the external for every period of time selected by the user on the second inlet, and can be activated or deactivated by sending a 1 or 0 in the first inlet. The manual mode means a new grain will be produced each time the user sends a bang on the first inlet.

You can set the start of the grain by sending the time in milliseconds on the third inlet; a maximum random interval of time around the start of the grain on the fourth inlet; the duration of the grain on the fifth inlet; a range of random pan distribution for the output from cero to one on the sixth inlet, this means 0 equals to central pan and 1 means a random pan between completely right or completely left; a transposition in semitones value as the seventh inlet; and a list in curve format that describes the shape of the amplitude window of the grain on the first inlet.

The grain will correct it's own start and size to fit completely on the buffer limits, but this isn't true for transposed grains in which case the grain limits will wrap the buffer limits, this means that you won't get an error but the result may be unexpected if the audio samples at the end of the buffer aren't similar to the start of the buffer.

This project contains, in addition, a prebuilt external fl_fasor~.mxe64 and help file fl_fasor~.maxhelp

### _function_ curve list format
To work properly, the format listing of the curve parameters needs to start on the first value of the curve you want to set, and needs to have 3 values for each point (y position, x difference, and curve). You can achieve this easily by changing a pair of attributes on the inspector of the *function* object: 
- The **Output Mode** attribute must be set to *List* (instead of *Normal*)
- The **Mode** attribute must be set to *Curve* (instead of Linear)

------------------------------------------------------

# Versions History

**v0.1**
- Clicks on the output solved.
- Can load a new buffer without the need to stop playing.