#!/usr/bin/env python3
"""
Reference implementation of the compact vCard codec, independent of the C.

Same discipline as tools/gen_vectors.py and for the same reason: if compact.c
and vcard.c share a misreading of architecture section 8.2, they round-trip
against each other perfectly and the phone still gets a mangled contact. This
file is written from the document, and M1 cross-checks the two in BOTH
directions -- C encodes, Python decodes, and back.

    python tools/vcf.py encode card.vcf          -> compact TLV, as hex
    python tools/vcf.py decode <hex>             -> vCard text
    python tools/vcf.py check card.vcf           -> round trip and report sizes
    python tools/vcf.py selftest                 -> the built-in cases

Cross-checking against the C is scripts/test.py's job (see --check-codec).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# the tag registry, transcribed from architecture section 8.2
# --------------------------------------------------------------------------

TAG_NOP = 0x00
TAG_FN = 0x01
TAG_N = 0x02
TAG_TEL_CELL = 0x03   # deprecated, decode only: see TAG_TEL
TAG_TEL_WORK = 0x04   # deprecated, decode only
TAG_EMAIL = 0x05
TAG_ORG = 0x06
TAG_TITLE = 0x07
TAG_URL = 0x08
TAG_ADR = 0x09
TAG_NOTE = 0x0A
TAG_TEL = 0x0B
TAG_CONT = 0xFE

# A phone is a number and what it is for. The label lives in the value, so one
# tag covers every phone a card has, including two with the same label and one
# the wearer named themselves:
#
#   label(1) | [ len(1) | UTF-8 label text ]  <- the bracket only when CUSTOM
#            | u16 country code BE | packed BCD
#
# TAG_TEL_CELL and TAG_TEL_WORK are what bands flashed before this change send.
# Both decode forever, as MOBILE and WORK; neither is ever encoded.
TEL_LABEL_NONE = 0x00
TEL_LABEL_MOBILE = 0x01
TEL_LABEL_WORK = 0x02
TEL_LABEL_HOME = 0x03
TEL_LABEL_MAIN = 0x04
TEL_LABEL_CUSTOM = 0xFF

TEL_LABEL_MAX = 32

TEL_TYPES = {
    TEL_LABEL_MOBILE: "CELL", TEL_LABEL_WORK: "WORK",
    TEL_LABEL_HOME: "HOME", TEL_LABEL_MAIN: "MAIN",
}
TAG_RAW = 0xFF

DOMAIN_LITERAL = 0xFF

# Append only. The id is on the wire, so inserting into the middle would
# silently rewrite everyone's email address.
DOMAINS = [
    "gmail.com", "outlook.com", "hotmail.com", "icloud.com", "yahoo.com",
    "protonmail.com", "live.com", "me.com", "aol.com", "gmx.com",
    "yandex.com", "qq.com", "163.com", "web.de", "mail.com", "zoho.com",
]

# Lower sorts first. Fragment 0 must be a usable contact on its own.
PRIORITY = {
    TAG_FN: 0, TAG_TEL: 1, TAG_TEL_CELL: 1, TAG_EMAIL: 2, TAG_N: 3, TAG_ORG: 4,
    TAG_TITLE: 5, TAG_TEL_WORK: 6, TAG_URL: 7, TAG_ADR: 8, TAG_NOTE: 9,
}

TAG_NAMES = {
    TAG_FN: "FN", TAG_N: "N", TAG_TEL_CELL: "TEL;TYPE=CELL",
    TAG_TEL_WORK: "TEL;TYPE=WORK", TAG_EMAIL: "EMAIL", TAG_ORG: "ORG",
    TAG_TITLE: "TITLE", TAG_URL: "URL", TAG_ADR: "ADR", TAG_NOTE: "NOTE",
}

PROPS = {
    "FN": TAG_FN, "N": TAG_N, "TEL": TAG_TEL, "EMAIL": TAG_EMAIL,
    "ORG": TAG_ORG, "TITLE": TAG_TITLE, "URL": TAG_URL, "ADR": TAG_ADR,
    "NOTE": TAG_NOTE,
}

# E.164: zones 1 and 7 are one digit, everything else two or three.
CC2 = {20, 27, 30, 31, 32, 33, 34, 36, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49,
       51, 52, 53, 54, 55, 56, 57, 58, 60, 61, 62, 63, 64, 65, 66,
       81, 82, 84, 86, 90, 91, 92, 93, 94, 95, 98}


class PhotoRejected(Exception):
    """architecture 8.2: a PHOTO is nine seconds of airtime. Never truncated."""


# --------------------------------------------------------------------------
# phone numbers
# --------------------------------------------------------------------------

def cc_digits(digits: str) -> int:
    if digits[:1] in ("1", "7"):
        return 1
    if len(digits) >= 2 and int(digits[:2]) in CC2:
        return 2
    return 3 if len(digits) >= 3 else len(digits)


def phone_pack(text: str) -> bytes:
    plus = text.lstrip().startswith("+")
    digits = "".join(c for c in text if c.isdigit())
    if not digits:
        return b""

    ccn = cc_digits(digits) if plus else 0
    if ccn >= len(digits):
        return b""
    country = int(digits[:ccn]) if ccn else 0
    rest = digits[ccn:]

    out = bytearray(country.to_bytes(2, "big"))
    for i in range(0, len(rest), 2):
        hi = int(rest[i])
        lo = int(rest[i + 1]) if i + 1 < len(rest) else 0xF
        out.append((hi << 4) | lo)
    return bytes(out)


def phone_unpack(blob: bytes) -> str:
    country = int.from_bytes(blob[:2], "big")
    out = ("+%d" % country) if country else ""
    for byte in blob[2:]:
        for nib in (byte >> 4, byte & 0x0F):
            if nib <= 9:
                out += str(nib)
    return out


# --------------------------------------------------------------------------
# email
# --------------------------------------------------------------------------

def tel_label(params: str) -> tuple[int, str]:
    """TEL parameters -> (label byte, custom text).

    A TEL with no type at all stays NONE. It is not a mobile: it is a number
    whose owner never said what it was for, and guessing prints the wrong word
    under somebody's number.
    """
    up = params.upper()
    if "CELL" in up or "MOBILE" in up:
        return TEL_LABEL_MOBILE, ""
    if "WORK" in up:
        return TEL_LABEL_WORK, ""
    if "HOME" in up:
        return TEL_LABEL_HOME, ""
    if "MAIN" in up or "PREF" in up:
        return TEL_LABEL_MAIN, ""
    m = re.search(r"[Xx]-([^;,]*)", params)
    if m and m.group(1):
        return TEL_LABEL_CUSTOM, m.group(1)[:TEL_LABEL_MAX]
    return TEL_LABEL_NONE, ""


def tel_pack(label: int, custom: str, number: str) -> bytes:
    text = (custom or "").encode("utf-8")[:TEL_LABEL_MAX]
    if label == TEL_LABEL_CUSTOM and not text:
        label = TEL_LABEL_NONE          # a custom label with nothing in it
    packed = phone_pack(number)
    if not packed:
        return b""
    out = bytes([label])
    if label == TEL_LABEL_CUSTOM:
        out += bytes([len(text)]) + text
    return out + packed


def tel_unpack(tag: int, blob: bytes) -> tuple[int, str, str]:
    if tag in (TAG_TEL_CELL, TAG_TEL_WORK):
        label = TEL_LABEL_WORK if tag == TAG_TEL_WORK else TEL_LABEL_MOBILE
        return label, "", phone_unpack(blob)
    if not blob:
        return TEL_LABEL_NONE, "", ""
    label, o, custom = blob[0], 1, ""
    if label == TEL_LABEL_CUSTOM:
        if len(blob) < 2:
            return TEL_LABEL_NONE, "", ""
        n = blob[1]
        o = 2 + n
        if o > len(blob):
            return TEL_LABEL_NONE, "", ""
        custom = blob[2:2 + n].decode("utf-8", "replace")
    return label, custom, phone_unpack(blob[o:])


def email_pack(value: str) -> bytes:
    if "@" not in value:
        return b""
    local, domain = value.split("@", 1)
    if not local or not domain:
        return b""
    if domain in DOMAINS:
        return local.encode("utf-8") + bytes([DOMAINS.index(domain)])
    return local.encode("utf-8") + bytes([DOMAIN_LITERAL]) + domain.encode("utf-8")


def email_unpack(blob: bytes) -> str:
    at = 0
    while at < len(blob) and blob[at] >= 0x20 and blob[at] != DOMAIN_LITERAL:
        at += 1
    if at >= len(blob):
        return ""
    local = blob[:at].decode("utf-8", "replace")
    if blob[at] == DOMAIN_LITERAL:
        return local + "@" + blob[at + 1:].decode("utf-8", "replace")
    if blob[at] >= len(DOMAINS):
        return ""
    return local + "@" + DOMAINS[blob[at]]


# --------------------------------------------------------------------------
# vCard <-> field list
# --------------------------------------------------------------------------

def unfold(text: str) -> list[str]:
    """RFC 2425 section 5.8.1: a leading space continues the previous line."""
    lines: list[str] = []
    for raw in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if raw[:1] in (" ", "\t") and lines:
            lines[-1] += raw[1:]
        else:
            lines.append(raw)
    return [l for l in lines if l]


def _rank(fields: list[tuple[int, bytes]], i: int) -> int:
    """Sort priority of one field, mirroring tel_rank() in compact.c.

    Every phone is TAG_TEL now, so every phone would claim priority 1 and three
    numbers would push EMAIL out of fragment 0. Only the first is worth that
    slot; the rest take the place the work number used to hold, in the order
    the card lists them.
    """
    tag = fields[i][0]
    p = PRIORITY.get(tag, 10)
    if tag != TAG_TEL:
        return p
    seen = sum(1 for j in range(i) if fields[j][0] == TAG_TEL)
    return p if seen == 0 else PRIORITY[TAG_TEL_WORK] + seen - 1


def parse(text: str) -> list[tuple[int, bytes]]:
    fields: list[tuple[int, bytes]] = []

    for line in unfold(text):
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        base = name.split(";", 1)[0].upper()
        params = name[len(base):]

        if base in ("BEGIN", "END", "VERSION"):
            continue
        if base == "PHOTO":
            raise PhotoRejected(line[:40])

        tag = PROPS.get(base, TAG_RAW)

        if tag == TAG_TEL:
            label, custom = tel_label(params)
            packed = tel_pack(label, custom, value)
            if packed:
                fields.append((tag, packed))
                continue
            tag = TAG_RAW
        elif tag == TAG_EMAIL:
            packed = email_pack(value)
            if packed:
                fields.append((tag, packed))
                continue
            tag = TAG_RAW

        if tag == TAG_RAW:
            fields.append((TAG_RAW, line.encode("utf-8")))
        else:
            fields.append((tag, value.encode("utf-8")))

    # Stable, so two raw lines keep the order they appeared in.
    ranks = [_rank(fields, i) for i in range(len(fields))]
    order = sorted(range(len(fields)), key=lambda i: (ranks[i], i))
    fields[:] = [fields[i] for i in order]
    return fields


def render(fields: list[tuple[int, bytes]]) -> str:
    out = ["BEGIN:VCARD", "VERSION:3.0"]

    have = {t for t, _ in fields}
    if TAG_N not in have and TAG_FN in have:
        fn = next(v for t, v in fields if t == TAG_FN).decode("utf-8", "replace")
        if " " in fn:
            first, last = fn.rsplit(" ", 1)
            out.append("N:%s;%s;;;" % (last, first))
        else:
            out.append("N:%s;;;;" % fn)

    for tag, value in fields:
        if tag == TAG_RAW:
            out.append(value.decode("utf-8", "replace"))
        elif tag in (TAG_TEL, TAG_TEL_CELL, TAG_TEL_WORK):
            label, custom, number = tel_unpack(tag, value)
            if not number:
                continue
            if label == TEL_LABEL_CUSTOM:
                out.append("TEL;TYPE=X-%s:%s" % (custom, number))
            elif label in TEL_TYPES:
                out.append("TEL;TYPE=%s:%s" % (TEL_TYPES[label], number))
            else:
                out.append("TEL:%s" % number)
        elif tag == TAG_EMAIL:
            addr = email_unpack(value)
            if addr:
                out.append("EMAIL;TYPE=INTERNET:%s" % addr)
        elif tag in TAG_NAMES:
            out.append("%s:%s" % (TAG_NAMES[tag], value.decode("utf-8", "replace")))

    out.append("END:VCARD")
    return "\r\n".join(out) + "\r\n"


# --------------------------------------------------------------------------
# field list <-> TLV
# --------------------------------------------------------------------------

def encode(fields: list[tuple[int, bytes]], max_value: int = 255) -> bytes:
    out = bytearray()
    for tag, value in fields:
        done = 0
        while True:
            chunk = value[done:done + max_value]
            out.append(tag if done == 0 else TAG_CONT)
            out.append(len(chunk))
            out += chunk
            done += len(chunk)
            if done >= len(value):
                break
    return bytes(out)


def decode(blob: bytes) -> list[tuple[int, bytes]]:
    fields: list[tuple[int, bytes]] = []
    i = 0
    while i < len(blob):
        tag = blob[i]
        if tag == TAG_NOP:          # padding carries no length byte
            i += 1
            continue
        if i + 1 >= len(blob):      # a truncated tail is normal, not an error
            break
        length = blob[i + 1]
        if i + 2 + length > len(blob):
            break
        value = blob[i + 2:i + 2 + length]
        if tag == TAG_CONT:
            if fields:              # a CONT whose head never arrived is dropped
                fields[-1] = (fields[-1][0], fields[-1][1] + value)
        else:
            fields.append((tag, value))
        i += 2 + length
    return fields


# --------------------------------------------------------------------------

SAMPLE = (
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "N:Lovelace;Ada;;;\r\n"
    "FN:Ada Lovelace\r\n"
    "ORG:Analytical Engines Ltd\r\n"
    "TITLE:Programmer\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "END:VCARD\r\n"
)


def selftest() -> int:
    failures = 0

    def check(name, got, want):
        nonlocal failures
        if got != want:
            failures += 1
            print("FAIL %s\n  got  %r\n  want %r" % (name, got, want))

    for text, want in [
        ("+44 7700 900123", "+447700900123"),
        ("+447700900123", "+447700900123"),
        ("+1 555 0100", "+15550100"),
        ("+91 98765 43210", "+919876543210"),
        ("+7 495 1234567", "+74951234567"),
        ("+353 86 1234567", "+353861234567"),
        ("020 7946 0958", "02079460958"),
    ]:
        check("phone %s" % text, phone_unpack(phone_pack(text)), want)

    check("phone size", len(phone_pack("+44 7700 900123")), 7)
    check("email known", email_unpack(email_pack("ada@gmail.com")), "ada@gmail.com")
    check("email unknown", email_unpack(email_pack("bo@example.org")), "bo@example.org")

    fields = parse(SAMPLE)
    blob = encode(fields)
    check("tlv round trip", decode(blob), fields)
    check("compact is smaller", len(blob) < len(SAMPLE) // 2, True)

    once = render(fields)
    check("render is a fixed point", encode(parse(once)), blob)

    # Chunking, and a fragment holding only continuations.
    long_note = [(TAG_NOTE, b"x" * 100)]
    chunked = encode(long_note, max_value=30)
    check("chunk head", chunked[0], TAG_NOTE)
    check("chunk cont", chunked[32], TAG_CONT)
    check("chunk rejoin", decode(chunked), long_note)
    check("orphan cont dropped", decode(bytes([TAG_CONT, 1, 0x41])), [])

    try:
        parse("BEGIN:VCARD\r\nPHOTO;ENCODING=b:AAAA\r\nEND:VCARD\r\n")
        check("photo rejected", False, True)
    except PhotoRejected:
        pass

    print("vcf.py selftest: %d failures" % failures)
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["encode", "decode", "check", "selftest"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--max-value", type=int, default=255,
                    help="chunk values longer than this (fragment payload - 2)")
    args = ap.parse_args()

    if args.action == "selftest":
        return selftest()

    if not args.arg:
        ap.error("%s needs an argument" % args.action)

    if args.action == "decode":
        print(render(decode(bytes.fromhex(args.arg))), end="")
        return 0

    text = Path(args.arg).read_text(encoding="utf-8")
    try:
        fields = parse(text)
    except PhotoRejected as e:
        print("rejected: PHOTO (%s...)" % e, file=sys.stderr)
        return 1

    blob = encode(fields, args.max_value)

    if args.action == "encode":
        print(blob.hex())
        return 0

    print("vCard text   %4d bytes" % len(text.encode("utf-8")))
    print("compact TLV  %4d bytes  (%.0f%%)"
          % (len(blob), 100.0 * len(blob) / len(text.encode("utf-8"))))
    print()
    print(render(decode(blob)), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
