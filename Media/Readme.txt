This folder is for custom, user-supplied media files.  You can place
the files listed below here to customize various events and effects in
PinballY.


== Startup Video ==

These are videos that will be played each time PinballY starts up.
You can specify videos for each window separately.  

Subfolder:  all of these go in the "Startup Videos" subfolder

File names:  You can use any combination of the following files:

  - "Startup Video.mp4" - plays in the main playfield window

  - "Startup Video (bg).mp4" - plays in the backglass window

  - "Startup Video (dmd).mp4" - plays in the DMD video window

  - "Startup Video (realdmd color).mp4" - plays on the real DMD
    device, using full color if supported by the device

  - "Startup Video (realdmd).mp4" - plays on the real DMD device,
    using 16-shade grayscale playback

  - "Startup Video (topper).mp4" - plays in the topper window

  - "Startup Video (instcard).mp4" - plays in the instruction 
    card window

Extensions: .mp4, .mpg, .f4v, mkv, .wmv, or .avi

You can use any subset of the files listed above.  Windows that don't
have corresponding videos will be left blank while the videos are
playing in the other windows.  All of the startup videos are queued up
and started at the same time.  This means that you synchronize events
in the different videos by editing the videos so that the events occur
at the same time offset in each video.


== Startup Audio == 

This is an audio file played when PinballY starts up.

Subfolder:  place this is the "Startup Sounds" folder

File name:  "Startup Audio.mp3"

Extensions:  .mp3, .wav, .ogg

If you supply both a startup audio track and one or more startup
videos, the audio and video files will play concurrently.

