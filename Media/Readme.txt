This folder is for custom, user-supplied media files.  You can place
the files listed below here to customize various events and effects in
PinballY.


== Button Sound Effects ==

You can customize the sound effects played for button presses by
providing your own .WAV files.

Disabling an effect: If you want to disable a particular button sound
entirely, you can simply use a .WAV file recording of a brief interval
of silence.  For your convenience, you'll find a file called
"Silence.wav" in the PinballY\Assets folder that you can use for
this purpose.  Simply copy and rename this file to match the name
for the effect you want to disable (see the list below).


Subfolder:  "Button Sounds"

File names:

    "AddCredit.wav" - coin in added full credit
    "CoinIn.wav"    - coin in added partial credit
    "Deselect.wav"  - closing a menu, popup, etc
    "Next.wav"      - flipper button right, next menu item, etc
    "Prev.wav"      - flipper button left, previous menu item, etc
    "Select.wav"    - open menu, select menu item, open popup

Extension:  .wav


== Default Background Images ==

You can provide a custom default background image for each window.
The default background is shown whenever the currently selected game
doesn't have its own image or video for that window.  (If the current
game does have its own image or video, that takes precedence over the
default background.)

If both a default video and default image file are provided, the
default video takes precedence, unless you've disabled all video
playback globally in the option settings.

Subfolder:  "Images"

File names:

   "Default Playfield.png"   (or other extensions below)
   "Default Backglass.png"
   "Default DMD.png"
   "Default Real DMD.png"
   "Default Real DMD (color).png"
   "Default Instruction Card.png"
   "Default Topper.png"

Extensions: .png, .jpg, .jpeg

The "(color)" version of the Real DMD image is used if you have a
color-capable DMD device; otherwise the regular version is used, and
is displayed in 16-shade monochrome.


== Default Background Videos ==

You can provide your own custom default background video for each
window.  If a default background video is present, it will be shown
whenever the currently selected game doesn't have its own video or
image file.

If both a default video and default image file are provided, the
default video takes precedence, unless you've disabled all video
playback globally in the option settings.

Subfolder:  "Videos"

File names:

   "Default Playfield.mp4"    (or other extensions below)
   "Default Backglass.mp4"
   "Default DMD.mp4"
   "Default Real DMD.mp4"
   "Default Real DMD (color).mp4"
   "Default Instruction Card.mp4"
   "Default Topper.mp4"

Extensions: .mp4, .mpg, .f4v, mkv, .wmv, or .avi

The "(color)" version of the Real DMD video is used if you have a
color-capable DMD device; otherwise the regular version is used, and
is displayed in 16-shade monochrome.


== Startup Videos ==

These are videos that will be played each time PinballY starts up.
You can specify videos for each window separately.  

Subfolder:  "Startup Videos"

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

Subfolder:  "Startup Sounds"

File name:  "Startup Audio.mp3"

Extensions:  .mp3, .wav, .ogg

If you supply both a startup audio track and one or more startup
videos, the audio and video files will play concurrently.


== Game Launch Backgrounds ==

When a game is being launched, the system normally fills the playfield
window with a dark gray background, with text messages giving the status
of the launch superimposed over the background.  You can replace the
plain gray background with your own custom media as follows:

Subfolder: "Images" (for static images)
           "Videos" (for videos)

File name: "Game Launch Background.png"  (or other extensions as appropriate)

Extensions: .mpg, .png, .gif for images
            .mp4, .mpg, .f4v, .mkv, .wmv, .avi for videos

Much more extensive customization of the launch screens can be accomplished
via Javascript.  See the help under Javascript scripting > Worked Examples >
"Seamless" Game Launch for examples.
