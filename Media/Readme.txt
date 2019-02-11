This folder is for custom PinballY media files.  You can place the
following items here:


== Startup Video ==

Place a file called "Startup Video.mp4" here to show a video each
time PinballY first up.  You can also use any of the other supported
video filename extensions in place of .mp4: .mpg, .f4v, .mkv, .wmv, or
.avi.  If this file is present, PinballY will play it in the main
playfield window each time the program starts, then will show the
selected game as usual.

You can also place files here to play startup videos in the other
windows: "Startup Video (bg)", "Startup Video (dmd)", "Startup Video
(instcard)", and "Startup Video (topper)".  As with the main video,
these can use any of the supported video format extensions.  All of
the startup videos are queued up and started at the same time.  This
means that you synchronize events in the different videos by editing
the videos so that the events occur at the same time offset in each
video.

Windows that don't have any startup video files assigned will simply
remain blank while the startup videos play in the other windows.


== Startup Audio == 

Place a file called "Startup Audio.mp3" here to play an
audio track when PinballY starts up.  (You can make it a .wav or .ogg
file instead of .mp3, if you prefer.)  If you provide both a startup
audio track and a startup video, both files will play concurrently, so
you'll probably want to use a silent video and let the audio file
provide the soundtrack.

