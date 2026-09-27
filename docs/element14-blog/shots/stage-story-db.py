"""Stage the blog story's people into a COPY of the app's contacts database.

    python stage-story-db.py <backup dir> <out dir> <mode> [YYYY-MM-DD]

<backup dir> holds handoff.db (+ -wal, -shm) pulled from the phone. The copy in
<out dir> is a single handoff.db (WAL folded in) ready to push back. The backup
is never touched; push it back to restore the wearer's own contacts. Do not
open the backup itself with sqlite: closing it folds the WAL in and deletes
-wal and -shm (harmless, but the restore is then the one file).

Fictional people and organisations, +91 98765 xxxxx numbers, example.* mail.
Times are 19 Sep 2026, IST, as chapters 3 and 4 tell them. Pass a date as the
fourth argument to put the same clock times on another day: the screenshots
want the story on the day they are taken, or the app files everyone under
"Earlier" with a date instead of "Today" and a time. Modes:
  empty    - no handshakes (end of 2.2, "No handshakes yet")
  first    - Savithri alone, met 09:14 (3.1)
  before11 - the busy morning up to 10:58; Savithri already renamed
             "(front desk)", still at 09:14, at the bottom (3.2, before)
  eleven   - the same, Savithri met again at 11:04 and back on top (3.2, after)
  lunch    - everyone by the coffee at two; Vikram just as received, no
             rename or note, for the rename to be done live in the app (3.2)
  day      - as lunch, with Vikram renamed "(Vivado License)" and his note (4.x)
"""
import os, re, shutil, sqlite3, sys
from datetime import datetime, timedelta, timezone

IST = timezone(timedelta(hours=5, minutes=30))
DAY = (2026, 9, 19)   # the story's date; override with the fourth argument
US = "\x1f"   # Phones.encode: LABEL US custom-label US number

# (time, first, last, number, email, org, title, note)
PEOPLE = [
    # the morning: the front desk, the keynote hall, the speaker afterwards
    ("09:14", "Savithri", "Raghavan", "+91 98765 00112", "savithri@example.com",
     "Expo registration", "Front desk lead", None),
    ("09:32", "Deepak", "Kulkarni", "+91 98765 00187", "deepak.k@example.com",
     "Voltaic Power", "Applications engineer", "Buck-boost parts. Wants our current budget."),
    ("09:41", "Nisha", "Thomas", "+91 98765 00203", "nisha.thomas@example.com",
     "Coral Microsystems", "Product manager", None),
    ("09:48", "Aditya", "Joshi", "+91 98765 00229", "aditya@example.net",
     None, "Embedded consultant", "Freelance. Did BLE on a hearing aid."),
    ("10:31", "Meera", "Pillai", "+91 98765 00245", "meera.pillai@example.com",
     "Northgate Labs", "CTO", "Keynote speaker. Low-power radios."),
    ("10:38", "Zoya", "Siddiqui", "+91 98765 00261", "zoya@example.org",
     "Sensewell", "Hardware designer", None),
    ("10:44", "Harish", "Gowda", "+91 98765 00276", "harish.gowda@example.com",
     "Brightpath Automotive", "Test engineer", None),
    ("10:52", "Lakshmi", "Narayanan", "+91 98765 00290", "lakshmi.n@example.edu",
     "Deccan Institute of Technology", "Research scholar", "Body-coupled sensing. Send her the plate drawing."),
    ("10:58", "Tanvi", "Kapoor", "+91 98765 00304", "tanvi@example.com",
     "Quanta Wearables", "Firmware lead", None),
    # the workshop and the stalls, up to the coffee at two
    ("11:36", "Kiran", "Hegde", "+91 98765 00311", "kiran.hegde@example.com",
     "Monsoon Robotics", "Controls engineer", None),
    ("12:10", "Pooja", "Agarwal", "+91 98765 00327", "pooja@example.net",
     "Ferrite Labs", "PCB designer", "Four-layer stackup tips. Ask for her checklist."),
    ("12:48", "Arjun", "Nair", "+91 98765 00318", "arjun@example.org",
     "Tessellate Robotics", "Hardware lead", "Same workshop bench. Shared his scope."),
    ("13:05", "Vikram", "Sharma", "+91 98765 00471", "vikram.sharma@example.com",
     "Kestrel Silicon", "FPGA engineer",
     "Fighting the same floating licence server.\nSend him my licence script. Call on Thursday."),
    ("13:14", "Sneha", "Reddy", "+91 98765 00482", "sneha.reddy@example.com",
     "Helix Sensors", "Sales lead", None),
    ("13:22", "Priya", "Desai", "+91 98765 00536", "priya.desai@example.com",
     "Lumen Semiconductors", "Engineering manager", "Hiring for the Bengaluru team."),
    ("13:40", "Karthik", "Rao", "+91 98765 00627", "karthik@example.net",
     "Brightline Components", "Sales engineer", "Quote for 500 connectors by Friday."),
    ("13:52", "Ananya", "Iyer", "+91 98765 00784", "ananya.iyer@example.edu",
     "College of Engineering", "Student", "Final-year project on gait sensing. Follow it."),
]

MORNING = [p for p in PEOPLE if p[0] < "11:00"]


def ms(hhmm):
    h, m = map(int, hhmm.split(":"))
    y, mo, d = DAY
    return int(datetime(y, mo, d, h, m, tzinfo=IST).timestamp() * 1000)


def rows(mode):
    """(time, display name, person, note) for each row the mode holds."""
    people = {"empty": [], "first": PEOPLE[:1], "before11": MORNING,
              "eleven": MORNING, "lunch": PEOPLE, "day": PEOPLE}[mode]
    out = []
    for p in people:
        t, first, last, note = p[0], p[1], p[2], p[7]
        name = f"{first} {last}"
        if first == "Savithri" and mode != "first":
            name += " (front desk)"
            if mode != "before11":
                t = "11:04"
        if first == "Vikram":
            if mode == "day":
                name += " (Vivado License)"
            else:
                note = None
        out.append((t, name, p, note))
    return out


def main(src, dst, mode, day=None):
    if day is not None:
        global DAY
        DAY = tuple(int(x) for x in day.split("-"))
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    p = os.path.join(dst, "handoff.db")
    c = sqlite3.connect(p)
    c.execute("DELETE FROM handshakes")
    for t, name, (_, first, last, num, email, org, title, _n), note in rows(mode):
        vcard = ("BEGIN:VCARD\r\nVERSION:3.0\r\n"
                 f"N:{last};{first};;;\r\nFN:{first} {last}\r\n"
                 + (f"ORG:{org}\r\n" if org else "") +
                 f"TITLE:{title}\r\n"
                 f"TEL;TYPE=CELL:{num}\r\nEMAIL;TYPE=INTERNET:{email}\r\nEND:VCARD\r\n")
        digits = re.sub(r"\D", "", num)
        c.execute(
            "INSERT INTO handshakes (received_at, vcard, display_name, phones, email, org, title,"
            " note, phone_key, email_key, field_count, promoted, contact_uri, raw_contact_id,"
            " added_by_hand, edited_since_promote) VALUES (?,?,?,?,?,?,?,?,?,?,?,0,NULL,NULL,0,0)",
            (ms(t), vcard, name, "MOBILE" + US + US + num, email, org, title, note,
             digits[-9:], email.lower(), 6 if org else 5))
    c.commit()
    c.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    c.execute("PRAGMA journal_mode=DELETE")
    c.close()
    for f in ("handoff.db-wal", "handoff.db-shm"):
        fp = os.path.join(dst, f)
        if os.path.exists(fp):
            os.remove(fp)
    c = sqlite3.connect(p)
    for r in c.execute("SELECT received_at, display_name FROM handshakes ORDER BY received_at DESC"):
        print(datetime.fromtimestamp(r[0] / 1000, IST).strftime("%H:%M"), r[1])


if __name__ == "__main__":
    main(*sys.argv[1:5])
