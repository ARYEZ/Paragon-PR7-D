# Makes tests/data/midiDB: a small groove database laid out like the ones some drum software keeps its grooves in
# (the same tables and columns), with grooves invented here (nothing from any real pack). Small 512-byte pages, so the
# reader meets tables several pages deep and a groove spread over overflow pages. Run from this folder:
#   python3 make_groove_db.py
import os, sqlite3, struct

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "midiDB")
if os.path.exists(path):
    os.remove(path)
c = sqlite3.connect(path)
c.execute("PRAGMA page_size = 512")
c.executescript("""
CREATE TABLE LIBRARY(ID integer primary key,Name text,FormattedName text,Version text,Path text);
CREATE TABLE PATHS(ID integer primary key,Path text);
CREATE TABLE FILENAMES(ID integer primary key,Name text);
CREATE TABLE HEADER(ID integer primary key,Name text);
CREATE TABLE TIMESIGS(ID integer primary key,Name text);
CREATE TABLE MIDIENTITY(ID integer,Name text,FilenameID integer,PathID integer,LibraryID integer,HeaderID integer,
    PowerHandID integer,Tempo integer,Intensity integer,Intensity2 integer,TimeSigID integer,Length integer,ExtraInfo text,
    Favorite integer DEFAULT 0,Prio integer DEFAULT 0,ResolutionID integer,GenreID integer,
    CONSTRAINT primary_entityID PRIMARY KEY(ID) ON CONFLICT IGNORE);
CREATE TABLE ACCENTS(EntID integer,AccentBar0 integer,AccentBar1 integer);
CREATE TABLE MIDI_INFO(EntID integer,MIDI blob);
CREATE TABLE TAGGROUP(ID integer primary key,Name text,IsFactory integer);
CREATE TABLE TAGS(ID integer primary key,Name text,GroupID integer,IsFactory integer);
CREATE TABLE REL_ENT_TAGS(EntID integer,TagID integer,IsFactory integer);
CREATE TABLE GENRE(ID integer primary key,Name text);
CREATE TABLE KITPIECES(ID integer primary key,Name text);
""")
# Labels as such databases give them (invented here): a genre, tags in two groups, the power hand's kit piece.
c.executemany("INSERT INTO TAGGROUP VALUES (?,?,1)", [(1, "Type"), (2, "Play Style")])
c.executemany("INSERT INTO TAGS VALUES (?,?,?,1)", [(1, "Beat", 1), (2, "Fill", 1), (3, "Straight", 1), (4, "Swing", 1),
                                                    (5, "Medium Hits", 1), (6, "Hard Hits", 1), (7, "Standard", 2), (8, "Double Kick", 2)])
c.executemany("INSERT INTO GENRE VALUES (?,?)", [(1, "Rock"), (2, "Jazz")])
c.executemany("INSERT INTO KITPIECES VALUES (?,?)", [(1, "Hi-Hat Closed"), (2, "Ride"), (3, "Snare")])
c.execute("INSERT INTO LIBRARY VALUES (1,'1@DEMO_GROOVES','Demo Grooves','1.0.0','1@DEMO_GROOVES')")
c.executemany("INSERT INTO HEADER VALUES (?,?)", [(1, "Rock Song 100 BPM"), (2, "Waltz 90 BPM")])
c.executemany("INSERT INTO TIMESIGS VALUES (?,?)", [(1, "4/4"), (2, "3/4")])
c.executemany("INSERT INTO PATHS VALUES (?,?)", [
    (1, "_EZD2MIDI_1@DEMO_GROOVES/101@STRAIGHT_4#4/100-S1@GROOVES/"),
    (2, "_EZD2MIDI_1@DEMO_GROOVES/101@STRAIGHT_4#4/100-S2@FILLS/"),
    (3, "_EZD2MIDI_1@DEMO_GROOVES/102@WALTZ_3#4/090-S3@GROOVES/")])

def blob(notes):
    return b"".join(struct.pack("<fB", beat, note) for beat, note in notes)

entity = 0
def add(name, path, header, tempo, sig, length, notes, accents=None, genre=1, hand=1, tags=(1, 3, 5, 7), prio=0):
    global entity
    entity += 1
    c.execute("INSERT INTO FILENAMES VALUES (?,?)", (entity, "Variation_%02d.mid" % entity))
    c.execute("INSERT INTO MIDIENTITY (ID,Name,FilenameID,PathID,LibraryID,HeaderID,Tempo,TimeSigID,Length,GenreID,PowerHandID,Prio) "
              "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", (entity, name, entity, path, 1, header, tempo, sig, length, genre, hand, prio))
    c.executemany("INSERT INTO REL_ENT_TAGS VALUES (?,?,1)", [(entity, t) for t in tags])
    c.execute("INSERT INTO MIDI_INFO VALUES (?,?)", (entity, blob(notes)))
    if accents is not None:
        c.execute("INSERT INTO ACCENTS VALUES (?,?,?)", (entity, accents[0], accents[1]))

# 40 rock grooves, 2 bars: kick on 1 and 3, snare on 2 and 4, eighth hats; groove k (1-40) has an extra kick on the
# "and" of 3 when k is odd. The accents mark the kicks and snares (steps 0, 8, 16, 24 of 32 a bar).
for k in range(1, 41):
    notes = []
    for bar in range(2):
        b = bar * 4.0
        notes += [(b, 36), (b + 2, 36), (b + 1, 38), (b + 3, 38)]
        if k % 2 == 1:
            notes.append((b + 2.5, 36))
        notes += [(b + i * 0.5, 42) for i in range(8)]
    notes.sort()
    # Grooves 39 and 40: double kick, hard hits, pushed to the back of the pack's order.
    add("Grooves %02d" % k, 1, 1, 100, 1, 8, notes, (0x01010101, 0x01010101),
        tags=(1, 3, 6, 8) if k >= 39 else (1, 3, 5, 7), prio=9 if k >= 39 else 0)

# A fill: snare 16ths, a closing crash on the next bar line; no accents (one level).
add("Fills 01", 2, 1, 100, 1, 4, [(i * 0.25, 38) for i in range(16)] + [(4.0, 49)], hand=3, tags=(2, 3, 6, 7))

# A long groove: 64 bars of eighth hats and a kick on each bar (576 notes, 2880 bytes: over many overflow pages).
long_notes = []
for bar in range(64):
    long_notes.append((bar * 4.0, 36))
    long_notes += [(bar * 4.0 + i * 0.5, 42) for i in range(8)]
add("Long Groove", 1, 1, 100, 1, 256, long_notes)

# A waltz in 3/4 at 90: kick on 1, hats on 2 and 3, two bars; the first beat of each bar marked.
add("Grooves 01", 3, 2, 90, 2, 6, [(0.0, 36), (1.0, 42), (2.0, 42), (3.0, 36), (4.0, 42), (5.0, 42)], (0x00000001, 0x00000001),
    genre=2, hand=2, tags=(1, 4, 5, 7))

c.commit()
c.close()
print(path, os.path.getsize(path), "bytes,", entity, "grooves")
