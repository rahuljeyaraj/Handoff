# Handoff demo video: voice-over script

## Scene 1: the problem

An expo hall. A few thousand people. And every one of them came here to meet somebody.

Someone stops at your stand, and asks a question. A real one. The kind only a person who has hit the same problem would ask.

Ten minutes later, you are both leaning over the same board. Half an hour in, you know how this person thinks. And that is worth keeping.

Then they have a flight to catch.

And out come the business cards, if either of you brought any. Or a phone tilted at a badge, hunting for the QR code the organiser printed too small. Or... what is your name on LinkedIn? How do you spell that?

Sometimes, in the rush, it just does not happen. You remember on the way home that you never got their number.

But there is one thing neither of you forgot to do.

## Scene 2: the idea

You shake hands.

That is the whole idea.

Handoff is a band you wear on your wrist. When two people wearing one shake hands, their contact details cross between them and land in each other's phones. They travel through the handshake itself. Through the two of you.

No cards. No app to open. No code to scan. You do the one thing you were always going to do.

Here are two of them, working.

## Scene 3: pairing the first band

Two phones. Two bands. And the app.

A band belongs to one phone, and only that phone. So the first thing you do is pair them.

Switch the band on, and point the phone at the Q R code on its label. This one is nine three D one.

The phone asks whether to pair with it. Tap Allow.

Paired.

## Scene 4: pairing the second band

The second band, the same way.

And if the camera will not settle on the code, the same four characters are printed right under it. Type them in instead.

Why a code, and not a list of nearby bands? Because in a hall like this there are hundreds of them, all with similar names, all a few metres away. Picking yours out of that list is guesswork. With the code, the phone looks for that one band and connects to it.

## Scene 5: your contact card

Now each person fills in their own contact card, once.

Name. Number. Email. Where they work, and what they do there.

The card goes onto their own band and stays there. The band flashes green twice and buzzes once to say it has it.

## Scene 6: a last look

Back on the home page. Open the band, and then your card. This is exactly what the band is holding.

Back out. The green card icon says the card saved and the battery status is good.

Bands on. The back of the band rests against the skin of the wrist.

From here on, the phones can stay in a pocket.

## Scene 7: the handshake

Now watch.

They shake hands.

Both bands go green.

Each card crossed two bodies, reached the other band, and went on to the phone.

## Scene 8: what lands on the phone

And there they are.

Who they are, where they work, and when you met.

Edit the name, and add a note so you remember them later.

And if you want, save them straight into the phone's contacts.

## Scene 9: your card can change

Your card is yours. Change it whenever you like, and the band takes the new one straight away.

The next handshake carries that.

## Scene 10: the list

And it fills up as the day goes on. Everybody whose hand you shook, in order, with the time.

No cards. No typing. No hunting for a QR code.

## Scene 11: the band, up close

Now let us see the bands up close.

A printed case on a watch strap. A switch and RGB LED on one side. One button on the other.

Turn it over.

The QR code. And the insulated metal plate. It sits against the skin of your wrist.

Open it up.

In the lid, you have the ground plate.

Under it, a Raspberry Pi Pico 2 W, in a socket, so it lifts straight out.

And under that, the band's own board. An amplifier, a small vibrating motor, and the connector that charges the cell.

The cell sits under the P C B, and the wire from the metal plate runs up to it.

That is the entire machine.

## Scene 12: the boards

Those boards were made by PCBWay, who sponsored this project.

I sent the files on a Saturday. Their engineers came back with two questions before a single board was made, instead of guessing, and both were closed by Monday. The boards were shipped the very next day.

And the boards took a beating from me. Every one of these joints was soldered and desoldered again and again, with a vacuum pump and solder wick over the same pads. Not one pad lifted.

Their twenty four hour lead time and their fast shipping are why this project finished in time. Thank you, PCBWay.

## Scene 13: end note

Everything is written up. The idea, the signal, the mistakes, the bench runs that went wrong before any of it went right. The board files, the parts list and the build are all in there too, so you can make a pair yourself.

A hundred and eighty years of radio has been about reaching further away. This one only has to reach across a handshake.

You shake hands. And the handshake keeps itself.

---

## For the edit

| Scene | Footage | Time in that clip |
| --- | --- | --- |
| 1 | the expo hall photo | hold, about 45 s |
| 2 | the same photo | hold, about 20 s |
| 3 | `final.mp4` | 0:00 to 0:30 |
| 4 | `final.mp4` | 0:30 to 1:02 |
| 5 | `final.mp4` | 1:02 to 1:34 |
| 6 | `final.mp4` | 1:34 to 2:12 |
| 7 | `final.mp4` | 2:12 to 2:20 |
| 8 | `final.mp4` | 2:20 to 2:38 |
| 9 | `final.mp4` | 2:38 to 3:16 |
| 10 | `final.mp4` | 3:16 to 3:44 |
| 11 | `opening.mp4` | 0:00 to 1:14, the whole clip |
| 12 | the PCBWay photos | `docs/element14-blog/13-boards.jpg`, then `29-bands-on-box.jpg` |
| 13 | the band on a wrist, or the two bands on the box | hold, about 25 s |

Where the lines in scene 11 land in `opening.mp4`:

* "Now let us see the bands up close." at 0:00, the closed band on the strap
* "Turn it over." at 0:04, as it comes off the table
* "The QR code." at 0:12
* "And the insulated metal plate" at 0:14, the gold tape over the plate
* "Open it up." at 0:18
* "In the lid, you have the ground plate." at 0:22
* "so it lifts straight out" at 0:34, as the Pico comes out
* "And under that, the band's own board" at 0:50
* "That is the entire machine." at 1:06, both bands open side by side

Notes:

* Scene 7 is the whole point and the footage only gives it eight seconds. Hold the frame on the two green bands, or slow that shot down, so the line lands.
* Scenes 4, 5 and 9 are the slow parts of the recording. Cut them down hard. The words above are written to survive the cut.
* The typing in scene 5 is worth speeding up on screen, with the voice at normal speed over it.
* Scene 9 is the same phone changing its own card. Do not name the people, or it reads as if somebody swapped bodies.
* Scene 13 needs somewhere to send people. Put the blog post link and the repository link on screen while it is read, because the voice does not spell out a URL.

---

## YouTube title and description

### Title

Handoff: shake hands, share contacts

### Description

You meet someone worth keeping. Then out come the business cards, or the badge
QR code the organiser printed too small, or "how do you spell that on LinkedIn?"

Handoff is a band you wear on your wrist. When two people wearing one shake
hands, their contact details cross between them and land in each other's phones.
The data travels through the handshake itself, through the two of you. No cards,
no app to open, no code to scan.

This video pairs two bands to two phones, fills in a contact card on each, shakes
hands, and shows both cards land. Then it opens a band up: a printed case on a
watch strap, a Raspberry Pi Pico 2 W in a socket, the band's own board with the
amplifier and the motor, the cell, and the insulated metal plate that rests
against the skin.

Source, board files and parts list
https://github.com/rahuljeyaraj/Handoff

The PCBs for this project were sponsored by PCBWay: https://www.pcbway.com/

Built for the element14 Community design challenge: Make A Connection

#electronics #diy #raspberrypipico #pcb #wearables #maker
