#!/usr/bin/env python3
"""Build the GH MIDI user guide: writes guide.html next to this script and
prints it to ../release/GH MIDI Guide.pdf with headless Chrome (macOS).

    python3 guide/build_guide.py            # html + pdf
    python3 guide/build_guide.py --html     # html only
"""
import os, subprocess, sys, time, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VERSION = "1.1"
SITE = "michaelloya.studio/gh-midi"
REPO = "github.com/michaelloyastudio/gh-midi"

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
def note_name(n):
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"

FRET_COLOURS = {"G": "#33cc3d", "R": "#e63232", "Y": "#f2d02a", "B": "#3378e6", "O": "#f2921f"}
ORDER = "GRYBO"

# ---------------------------------------------------------------- glyphs
def glyph(pattern, size=13):
    """Five fret dots. pattern like 'G-Y--' (letters pressed, dashes not)."""
    r = size / 2 - 1
    step = size + 4
    w = step * 5
    out = [f'<svg class="fg" width="{w}" height="{size + 2}" viewBox="0 0 {w} {size + 2}" aria-label="{pattern}">']
    for i, letter in enumerate(ORDER):
        cx = i * step + size / 2 + 2
        cy = size / 2 + 1
        if pattern[i] == letter:
            out.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{FRET_COLOURS[letter]}" stroke="#0a0a0a" stroke-opacity="0.35" stroke-width="1"/>')
        else:
            out.append(f'<circle cx="{cx}" cy="{cy}" r="{r - 0.5}" fill="none" stroke="#3d2b1f" stroke-opacity="0.35" stroke-width="1.2"/>')
    out.append("</svg>")
    return "".join(out)

def pattern_from_mask(mask):
    return "".join(letter if mask & (1 << i) else "-" for i, letter in enumerate(ORDER))

def pat(pattern):
    """glyph + bracket text, used inline in tables and prose"""
    return f'<span class="pat">{glyph(pattern)}<code>[{pattern}]</code></span>'

# ---------------------------------------------------------------- guitar diagram
# Single-cutaway body, long neck, headstock on the left. Labels sit outside the
# silhouette with short vertical leaders.
GUITAR_SVG = """
<svg viewBox="0 0 640 430" width="100%" xmlns="http://www.w3.org/2000/svg" font-family="Inter, sans-serif">
  <defs><style>
    .body { fill:#f4efe7; stroke:#3d2b1f; stroke-width:6; }
    .dark { fill:#2b2320; }
    .mid  { fill:#6b5a4c; }
    .tun  { fill:#E8E0D4; stroke:#2b2320; stroke-width:4; }
    .fret { stroke:#E8E0D4; stroke-opacity:0.32; stroke-width:4; }
    .dot  { fill:#E8E0D4; fill-opacity:0.5; }
    .lead { stroke:#de0000; stroke-width:1; }
    .lab  { font-size:10.5px; font-weight:600; fill:#0a0a0a; letter-spacing:0.08em; }
    .sub  { font-size:9px; fill:#8a8580; }
  </style></defs>
  <!-- drawn in the photo's own pixel grid (574 x 1738), then scaled -->
  <g transform="translate(150,0) scale(0.245)">
    <!-- body: Les Paul, cutaway horn at the upper left -->
    <path class="body" d="M382 1040 C425 1040 478 1080 486 1152 C492 1210 480 1250 474 1282
      C472 1320 514 1380 525 1480 C522 1590 440 1712 318 1712 C196 1712 114 1590 110 1480
      C112 1380 158 1320 162 1282 C156 1250 144 1210 150 1152 C156 1090 172 1030 205 990
      C226 976 250 986 258 1012 C268 1044 278 1082 288 1096 Z"/>
    <circle class="dark" cx="222" cy="996" r="11"/>
    <!-- headstock (open-book top) and tuners -->
    <path class="dark" d="M262 300 L240 60 C240 45 250 38 262 38 L300 38 C314 38 320 50 320 50
      C320 50 326 38 340 38 L378 38 C390 38 400 45 400 60 L378 300 Z"/>
    <g class="tun"><circle cx="270" cy="100" r="14"/><circle cx="270" cy="155" r="14"/><circle cx="270" cy="210" r="14"/>
      <circle cx="362" cy="100" r="14"/><circle cx="362" cy="155" r="14"/><circle cx="362" cy="210" r="14"/></g>
    <!-- neck runs onto the body -->
    <rect class="dark" x="288" y="298" width="94" height="862"/>
    <g class="fret"><path d="M288 640h94M288 695h94M288 750h94M288 805h94M288 860h94M288 915h94M288 970h94M288 1025h94M288 1080h94M288 1135h94"/></g>
    <g class="dot"><circle cx="335" cy="722" r="6"/><circle cx="335" cy="832" r="6"/><circle cx="335" cy="942" r="6"/><circle cx="335" cy="1052" r="6"/></g>
    <!-- fret buttons -->
    <g stroke="#0a0a0a" stroke-opacity="0.35" stroke-width="3">
      <rect x="305" y="345" width="60" height="44" rx="10" fill="#33cc3d"/>
      <rect x="305" y="403" width="60" height="44" rx="10" fill="#e63232"/>
      <rect x="305" y="461" width="60" height="44" rx="10" fill="#f2d02a"/>
      <rect x="305" y="519" width="60" height="44" rx="10" fill="#3378e6"/>
      <rect x="305" y="577" width="60" height="44" rx="10" fill="#f2921f"/>
    </g>
    <!-- joystick, up by the neck -->
    <circle class="dark" cx="215" cy="1105" r="26"/><circle class="mid" cx="215" cy="1105" r="9"/>
    <!-- strum bar -->
    <rect class="dark" x="310" y="1245" width="50" height="185" rx="25"/>
    <rect class="mid" x="326" y="1270" width="18" height="135" rx="9"/>
    <!-- Wii Remote window -->
    <rect class="dark" x="188" y="1300" width="60" height="220" rx="4"/>
    <rect class="mid" x="196" y="1312" width="44" height="196" rx="2" fill-opacity="0.5"/>
    <!-- plus and minus -->
    <circle class="dark" cx="355" cy="1550" r="28"/><circle class="mid" cx="355" cy="1550" r="9"/>
    <circle class="dark" cx="385" cy="1597" r="22"/><circle class="mid" cx="385" cy="1597" r="7"/>
    <!-- whammy -->
    <circle class="dark" cx="442" cy="1392" r="12"/>
    <path d="M442 1392 L455 1486" stroke="#2b2320" stroke-width="9" stroke-linecap="round"/>
    <rect class="dark" x="438" y="1480" width="36" height="28" rx="9"/>
  </g>

  <!-- right labels -->
  <path class="lead" d="M245 119H372"/>
  <text class="lab" x="380" y="123">FRETS</text>
  <text class="sub" x="380" y="136">green · red · yellow · blue · orange</text>
  <path class="lead" d="M244 328H372"/>
  <text class="lab" x="380" y="332">STRUM BAR</text>
  <text class="sub" x="380" y="345">down and up</text>
  <path class="lead" d="M268 366H372"/>
  <text class="lab" x="380" y="370">WHAMMY</text>
  <text class="sub" x="380" y="383">pitch bend</text>
  <path class="lead" d="M251 395H372"/>
  <text class="lab" x="380" y="399">PLUS / MINUS</text>
  <text class="sub" x="380" y="412">strum spread · mode</text>

  <!-- left labels -->
  <path class="lead" d="M196 271H168"/>
  <text class="lab" x="160" y="275" text-anchor="end">JOYSTICK</text>
  <text class="sub" x="160" y="288" text-anchor="end">key · octave</text>
  <path class="lead" d="M195 345H168"/>
  <text class="lab" x="160" y="349" text-anchor="end">WII REMOTE</text>
  <text class="sub" x="160" y="362" text-anchor="end">slides in here</text>
</svg>
"""

# ---------------------------------------------------------------- page chrome
def page(body, section, number, dark=False, plain=False):
    cls = "page dark" if dark else "page"
    head = "" if plain else f'<div class="pagehead"><span>GH MIDI · User Guide</span><span>{section}</span></div>'
    foot = "" if plain else (
        '<div class="pagefoot"><span class="brand"><img src="assets/logo-red.svg" alt=""> michaelloya.studio</span>'
        f'<span>{number}</span></div>')
    return f'<section class="{cls}">{head}{body}{foot}</section>\n'

def h2(num, title):
    return f'<div class="num">{num}</div><h2>{title}</h2><hr class="rule">'

# ---------------------------------------------------------------- content
def cover():
    return f"""
<div class="cover">
  <div class="cover-top"><img src="assets/logo-red.svg" alt="Michael Loya Studio" class="cover-logo"></div>
  <div class="cover-mid">
    <div class="wordmark">GH MIDI</div>
    <div class="cover-kicker">USER GUIDE</div>
    <p class="cover-lead">Turn a Guitar Hero controller<br>into a real MIDI instrument.</p>
  </div>
  <img src="assets/ui-chords.jpg" alt="GH MIDI main screen" class="cover-shot">
  <div class="cover-bottom"><span class="cover-site">michaelloya.studio</span><span class="cover-meta">Version {VERSION} · macOS · standalone app + VST3</span></div>
</div>"""

def p_welcome():
    return h2("01", "Welcome") + f"""
<p class="lead">GH MIDI turns a Guitar Hero controller into a MIDI instrument. Strum, and chords or notes come out as MIDI that plays any synth, sampler or drum machine.</p>

<h3>What's in the download</h3>
<table>
<tr><th style="width:34%">File</th><th>What it is</th></tr>
<tr><td><b>GH MIDI.app</b></td><td>Standalone app. No DAW needed. While it runs, every music app on your Mac sees a MIDI input called <b>GH MIDI</b>.</td></tr>
<tr><td><b>GH MIDI.vst3</b></td><td>Plugin for VST3 hosts: FL Studio, Ableton Live, Reaper, Bitwig, Cubase.</td></tr>
<tr><td><b>README.txt</b></td><td>The short version of this guide.</td></tr>
</table>
<div class="note">Run <b>one</b> at a time. Whichever opens first gets the guitar.</div>

<h3>Contents</h3>
<table class="toc">
<tr><td>02</td><td>Install</td><td>3</td></tr>
<tr><td>03</td><td>Connect to your DAW</td><td>4</td></tr>
<tr><td>04</td><td>Your controller</td><td>5</td></tr>
<tr><td>05</td><td>The screen</td><td>6</td></tr>
<tr><td>06</td><td>How it plays</td><td>7</td></tr>
<tr><td>07</td><td>CHORDS mode</td><td>8</td></tr>
<tr><td>08</td><td>NOTES mode</td><td>10</td></tr>
<tr><td>09</td><td>SOLO mode</td><td>11</td></tr>
<tr><td>10</td><td>CHART mode</td><td>12</td></tr>
<tr><td>11</td><td>Settings &amp; other controllers</td><td>13</td></tr>
<tr><td>12</td><td>Troubleshooting</td><td>14</td></tr>
</table>"""

def p_install():
    return h2("02", "Install") + """
<p>Apple Silicon and Intel. The download is not notarized, so macOS blocks it once. Two ways past that: the Settings route, or one Terminal command that clears it for good.</p>

<h3>Standalone app</h3>
<ol class="steps">
<li>Drag <b>GH MIDI.app</b> into <b>Applications</b>.</li>
<li>Open it. macOS says it can't verify the app. Click <b>Done</b>.</li>
<li><b>System Settings → Privacy &amp; Security</b>, scroll down, click <b>Open Anyway</b>. Open the app again.</li>
</ol>
<p class="muted">Or skip 2 and 3 with Terminal:</p>
<code class="cmd">xattr -dr com.apple.quarantine /Applications/"GH MIDI.app"</code>

<h3>VST3 plugin</h3>
<ol class="steps">
<li>Copy <b>GH MIDI.vst3</b> to <code>~/Library/Audio/Plug-Ins/VST3</code>
<span class="muted">(Finder: Go → Go to Folder…, paste the path).</span></li>
<li>Run this in Terminal, or the DAW may report the plugin as damaged:
<code class="cmd">xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"GH MIDI.vst3"</code></li>
<li>Rescan plugins in your DAW. Add <b>GH MIDI</b> as an instrument.</li>
</ol>

<h3>Permissions</h3>
<p>If the guitar never shows up: <b>System Settings → Privacy &amp; Security → Input Monitoring</b>, allow your DAW or GH MIDI.app.</p>

<h3>Uninstall</h3>
<p>Delete the app and the .vst3. Settings live in <code>~/Library/Application Support/GH MIDI/</code>.</p>"""

def p_daw():
    return h2("03", "Connect to your DAW") + """
<p>GH MIDI makes no sound. It sends MIDI; an instrument in your DAW makes the sound. Two ways to connect:</p>

<div class="cols">
<div>
<h3>A · The GH MIDI input <span class="muted">(app or plugin)</span></h3>
<p>A MIDI input called <b>GH MIDI</b> appears system-wide. Point an instrument track at it. Your playing records as normal, editable notes.</p>
<table>
<tr><th>DAW</th><th>Enable it</th></tr>
<tr><td>FL Studio</td><td>Options → MIDI Settings → Input → <b>GH MIDI</b> → Enable</td></tr>
<tr><td>Logic / GarageBand</td><td>Nothing to enable. Make a software instrument track.</td></tr>
<tr><td>Ableton Live</td><td>Settings → Link, Tempo &amp; MIDI → Input <b>GH MIDI</b> → Track On. Arm the track.</td></tr>
<tr><td>Reaper</td><td>Preferences → MIDI Devices → <b>GH MIDI</b> → Enable input</td></tr>
</table>
</div>
<div>
<h3>B · Route inside the DAW <span class="muted">(plugin only)</span></h3>
<p>Load the plugin as an instrument and wire its MIDI output into another instrument. Everything stays in the project.</p>
<p><b>FL Studio:</b> native plugins like FLEX have no MIDI-in port in the wrapper, so use <b>Patcher</b>: add GH MIDI and the instrument, connect GH MIDI's green MIDI-out to the instrument's green MIDI-in, instrument audio to <i>To FL Studio</i>.</p>
<p><b>Ableton Live:</b> on the instrument track, <i>MIDI From</i> = the GH MIDI track, <i>Monitor</i> = In.</p>
<p><b>Reaper / Bitwig / Cubase:</b> the plugin's MIDI output feeds another instrument track.</p>
</div>
</div>
<div class="note">Whammy = pitch bend on channel 1, plus CC 20 for linking to any knob.</div>"""

def p_controller():
    return h2("04", "Your controller") + f"""
<p>Pre-mapped for a <b>Wii Guitar Hero guitar</b> on a <b>raphnet WUSBMote</b> adapter. Other USB controllers: section 11.</p>
<div class="figure">{GUITAR_SVG}</div>
<table>
<tr><th style="width:24%">Control</th><th>Does</th></tr>
<tr><td><b>Frets</b></td><td>Pick the chord or note. Patterns are written {pat("G-Y--")} in this guide.</td></tr>
<tr><td><b>Strum bar</b></td><td>Makes the sound. Down and up are separate. In CHORDS they are two voicings; up is softer.</td></tr>
<tr><td><b>Whammy</b></td><td>Bends the pitch down.</td></tr>
<tr><td><b>Minus</b></td><td>Next mode: CHORDS → NOTES → SOLO → CHART.</td></tr>
<tr><td><b>Plus</b></td><td>Strum spread, 0 to 50 ms, then back to 0.</td></tr>
<tr><td><b>Joystick</b></td><td>Left / right = key. Up / down = octave.</td></tr>
</table>
<h3>WUSBMote adapter</h3>
<p>The WUSBMote only finds the guitar <b>when it gets power</b>. If the screen says <i>adapter can't see the guitar</i>: unplug USB, seat the guitar plug, plug USB back in.</p>"""

def p_screen():
    callouts = [(68, 5, 1), (57, 14, 2), (50, 58, 3), (12, 92, 4), (37, 92, 5), (62, 92, 6), (87, 92, 7), (69, 10.5, 8)]
    dots = "".join(f'<span class="callout" style="left:{x}%;top:{y}%">{n}</span>' for x, y, n in callouts)
    return h2("05", "The screen") + f"""
<div class="shotwrap mid"><img class="shot" src="assets/ui-chords.jpg" alt="">{dots}</div>
<table class="legend">
<tr><td><span class="cn">1</span></td><td><b>MAP, ? and SETTINGS</b> — every fret shape for this mode and key, a quick controls reminder, and the settings panel.</td></tr>
<tr><td><span class="cn">2</span></td><td><b>What you played</b> — the chord or note name, and every setting change.</td></tr>
<tr><td><span class="cn">3</span></td><td><b>The highway</b> — your notes travel away as gems. Held notes trail; the whammy wiggles them.</td></tr>
<tr><td><span class="cn">4</span></td><td><b>MODE</b> — CHORDS, NOTES, SOLO or CHART. Arrows or minus.</td></tr>
<tr><td><span class="cn">5</span></td><td><b>STRUM SPREAD</b> — milliseconds between the notes of a strum. 0 = stab, 30+ = slow sweep. Arrows or plus.</td></tr>
<tr><td><span class="cn">6</span></td><td><b>KEY</b> — the key everything is built in. In NOTES this reads <b>BASE</b>, the lowest note. Arrows or joystick left / right.</td></tr>
<tr><td><span class="cn">7</span></td><td><b>OCTAVE</b> — −3 to +3. Arrows or joystick up / down.</td></tr>
<tr><td><span class="cn">8</span></td><td><b>SUSTAIN</b> — FRET: a note lasts while its fret is held. STRUM: while the strum bar is held.</td></tr>
</table>
<p class="muted">The gold tag under each setting is the guitar control that changes it. It lights up while you touch that control.</p>"""

def p_playing():
    return h2("06", "How it plays") + f"""
<p class="lead">One rule in every mode: <b>the strum makes the sound.</b></p>
<table>
<tr><th style="width:30%">You do</th><th>It does</th></tr>
<tr><td>Hold frets, strum</td><td>The chord or note sounds.</td></tr>
<tr><td>Keep holding</td><td>It keeps ringing.</td></tr>
<tr><td>Let go</td><td>It stops. In SOLO, only that fret's note stops.</td></tr>
<tr><td>Change frets without strumming</td><td>Nothing, until the next strum.</td></tr>
<tr><td>Strum with no frets</td><td>The open note: a low root in CHORDS and SOLO, the base note in NOTES. Rings while the bar is held.</td></tr>
<tr><td>Push the whammy</td><td>The pitch bends down, and back when you release.</td></tr>
</table>

<h3>Key and octave</h3>
<p>Joystick right moves everything up a semitone, left down. Up and down move an octave, up to three either way. The key name pops on screen.</p>

<h3>Strum spread</h3>
<p>Chords and SOLO stacks are strummed: low to high on a down-strum, high to low on an up. Strum spread is the gap between the notes. 0 for stabs, 10 for a natural pick, 30+ for a slow sweep.</p>

<h3>The modes</h3>
<table>
<tr><th style="width:18%">Mode</th><th style="width:44%">A fret is…</th><th>For</th></tr>
<tr><td><b>CHORDS</b></td><td>a whole chord in your key</td><td>songs, rhythm. Every note is in key.</td></tr>
<tr><td><b>NOTES</b></td><td>a binary digit: combos pick one of 32 notes</td><td>riffs, basslines, melodies. Takes practice.</td></tr>
<tr><td><b>SOLO</b></td><td>one note of the pentatonic scale</td><td>leads over CHORDS. Every note is in key.</td></tr>
<tr><td><b>CHART</b></td><td>a Clone Hero lane</td><td>recording a rough chart to finish in a chart editor.</td></tr>
</table>"""

def p_chords():
    singles = [("G----", "I", "C", "G"), ("-R---", "V", "G", "D"), ("--Y--", "vi", "Am", "Em"),
               ("---B-", "IV", "F", "C"), ("----O", "ii", "Dm", "Am")]
    rows = "".join(f"<tr><td>{pat(p)}</td><td>{d}</td><td>{c}</td><td>{g}</td></tr>" for p, d, c, g in singles)
    neighbours = [("GR---", "Imaj7", "Cmaj7"), ("-RY--", "V7", "G7"), ("--YB-", "vi m7", "Am7"), ("---BO", "IVmaj7", "Fmaj7")]
    stretches = [("G-Y--", "iii", "Em"), ("--Y-O", "III", "E"), ("-R-B-", "bVII", "B♭"), ("G--B-", "iv", "Fm"), ("-R--O", "bVI", "A♭")]
    def crow(p, d, c):
        return f"<tr><td>{pat(p)}</td><td>{d}</td><td>{c}</td></tr>"
    crows = ("<tr class='group'><td colspan='3'>Neighbours · sevenths</td></tr>"
             + "".join(crow(*x) for x in neighbours)
             + "<tr class='group'><td colspan='3'>Stretches · the chords five frets can't make</td></tr>"
             + "".join(crow(*x) for x in stretches))
    return h2("07", "CHORDS mode") + f"""
<p class="lead">Every fret is a chord in the key. Hold one, strum.</p>
<div class="cols">
<div>
<h3>Single frets</h3>
<table>
<tr><th>Fret</th><th>Chord</th><th>in C</th><th>in G</th></tr>{rows}
<tr><td>{pat("-----")}</td><td>open</td><td>low C</td><td>low G</td></tr>
</table>
<p class="muted" style="margin-top:6pt">Down-strums voice low to high. Up-strums voice from the top, softer.</p>
<p class="muted">Open strum = the root, an octave down.</p>
</div>
<div>
<h3>Two-fret combos</h3>
<table>
<tr><th>Frets</th><th>Chord</th><th>in C</th></tr>{crows}
</table>
<p class="muted" style="margin-top:6pt">Any other combination plays the highest fret's chord.</p>
</div>
</div>
<p class="muted" style="margin-top:8pt">The <b>MAP</b> button shows this table live, in whatever key you're in, and lights the row you're holding.</p>
<img class="shot small" src="assets/ui-map.jpg" alt="">"""

def p_songs():
    progs = [
        ("I – V – vi – IV", ["G----", "-R---", "--Y--", "---B-"], "Let It Be · With or Without You · Someone Like You"),
        ("vi – IV – I – V", ["--Y--", "---B-", "G----", "-R---"], "Numb · Grenade · Africa"),
        ("I – vi – IV – V", ["G----", "--Y--", "---B-", "-R---"], "Stand By Me · Every Breath You Take"),
        ("I – IV – V", ["G----", "---B-", "-R---"], "rock and blues. Twelve-bar: I I I I · IV IV I I · V IV I V"),
        ("ii – V – I", ["----O", "-R---", "G----"], "the jazz turnaround. {} and {} for sevenths"),
        ("I – bVII – IV", ["G----", "-R-B-", "---B-"], "Sweet Child O' Mine · Sympathy for the Devil"),
    ]
    rows = ""
    for name, pats, blurb in progs:
        blurb = blurb.format(glyph("-RY--", 11), glyph("GR---", 11)) if "{}" in blurb else blurb
        rows += f"<tr><td><b>{name}</b></td><td><div class='pats'>{''.join(glyph(p, 12) for p in pats)}</div></td><td class='muted'>{blurb}</td></tr>"
    return h2("07", "CHORDS mode · play any song") + f"""
<p>Most songs are four chords, and they are on the single frets. Set KEY to the song's key, then follow the pattern.</p>
<table class="progs">
<tr><th style="width:17%">Progression</th><th style="width:46%">Frets</th><th>Songs</th></tr>{rows}
</table>
<h3>Finding the key</h3>
<p>Search the song name plus "chords". The first chord is usually the key. Song in G: set KEY to <b>G</b>, and green is G, red D, yellow Em, blue C. The names on screen will match the chart.</p>
<h3>Too high or too low</h3>
<p>OCTAVE moves everything by twelve semitones, KEY by one. Nothing else changes.</p>"""

def p_notes():
    cells = []
    for n in range(32):
        cells.append(f"<tr><td>{glyph(pattern_from_mask(n), 12)}</td><td class='mono'>+{n}</td><td>{note_name(40 + n)}</td></tr>")
    cols = ""
    for c in range(4):
        cols += "<table class='chart'><tr><th>Frets</th><th>+</th><th>Note</th></tr>" + "".join(cells[c * 8:(c + 1) * 8]) + "</table>"
    return h2("08", "NOTES mode") + f"""
<p class="lead">The frets are a binary number. Add up the held frets: that many semitones above the BASE note.</p>
<div class="binary"><span>{glyph("G----", 15)}<b>= 1</b></span><span>{glyph("-R---", 15)}<b>= 2</b></span><span>{glyph("--Y--", 15)}<b>= 4</b></span><span>{glyph("---B-", 15)}<b>= 8</b></span><span>{glyph("----O", 15)}<b>= 16</b></span></div>
<p>32 chromatic notes from one hand position. The chart is for the default BASE of <b>E2</b>. Move BASE with the joystick and the chart moves with it.</p>
<div class="charts">{cols}</div>
<h3>Example · Smoke on the Water</h3>
<p>Set BASE to <b>G2</b> (joystick right three times). The riff is 0 · 3 · 5 &nbsp;|&nbsp; 0 · 3 · 6 · 5 &nbsp;|&nbsp; 0 · 3 · 5 &nbsp;|&nbsp; 3 · 0:</p>
<div class="riff"><span class="bar">{glyph("-----", 12)}{glyph("GR---", 12)}{glyph("G-Y--", 12)}</span><span class="bar">{glyph("-----", 12)}{glyph("GR---", 12)}{glyph("-RY--", 12)}{glyph("G-Y--", 12)}</span><span class="bar">{glyph("-----", 12)}{glyph("GR---", 12)}{glyph("G-Y--", 12)}</span><span class="bar">{glyph("GR---", 12)}{glyph("-----", 12)}</span></div>
<p class="muted">Changing frets doesn't change a ringing note. Set the next shape early, strum on the beat.</p>
<img class="shot small" src="assets/ui-notes.jpg" alt="">"""

def p_solo():
    rows = [("G----", "1", "root", "C"), ("-R---", "2", "second", "D"), ("--Y--", "3", "third", "E"),
            ("---B-", "5", "fifth", "G"), ("----O", "6", "sixth", "A"), ("-----", "open", "root, octave down", "low C")]
    trs = "".join(f"<tr><td>{pat(p)}</td><td>{d}</td><td>{n}</td><td>{c}</td></tr>" for p, d, n, c in rows)
    return h2("09", "SOLO mode") + f"""
<p class="lead">Five frets, five notes of the major pentatonic scale of your key. Every one fits over every chord in that key.</p>
<div class="cols">
<div>
<table>
<tr><th style="width:44%">Fret</th><th>Degree</th><th></th><th>in C</th></tr>{trs}
</table>
</div>
<div>
<h3>Several frets at once</h3>
<p>Hold several frets and strum: they all sound, swept low to high on a down-strum, high to low on an up. Strum spread sets the sweep.</p>
<p>Lift one finger and only that note stops. The rest keep ringing.</p>
<img class="shot" src="assets/ui-solo.jpg" alt="" style="margin-top:4pt">
</div>
</div>
<h3>With CHORDS</h3>
<p>Record a CHORDS part, press minus to switch to SOLO, play over it. Both modes share the same KEY. Use OCTAVE to put the solo above the chords.</p>"""

def p_chart():
    lanes = [("Expert", 96), ("Hard", 84), ("Medium", 72), ("Easy", 60)]
    rows = "".join(f"<tr><td>{d}</td><td class='mono'>{b} – {b + 4}</td></tr>" for d, b in lanes)
    return h2("10", "CHART mode") + f"""
<p class="lead">Play along to a song and record a rough Clone Hero chart. Finish it in a chart editor.</p>
<p>Each fret sends the note number Clone Hero uses for that lane, on all four difficulties at once. Strum = note on, let go = note off, so holds become sustains. Key, octave and strum spread are off. The screen shows the letters of the frets you hit, in their colours.</p>
<h3>Charting a song</h3>
<ol class="steps">
<li>In your DAW, put the song on an audio track and set the project tempo to the song's BPM.</li>
<li>Add a MIDI track with input <b>GH MIDI</b> (section 03), arm it, switch GH MIDI to CHART.</li>
<li>Play through the song. Green, red, yellow, blue and orange land on their lanes as you strum.</li>
<li>Export that MIDI track as a <code>.mid</code>. Name the track <b>PART GUITAR</b> if your DAW lets you.</li>
<li>Open the .mid in <b>Moonscraper Chart Editor</b> with the audio. Snap to the grid, move, add, delete. Export for Clone Hero.</li>
</ol>
<div class="cols">
<div>
<h3>Lane notes</h3>
<table><tr><th>Difficulty</th><th>Green – orange</th></tr>{rows}</table>
<p class="muted" style="margin-top:6pt">Every difficulty gets the same notes. Thin the lower ones in the editor.</p>
</div>
<div>
<h3>Done in the editor, not here</h3>
<p>Hammer-ons and pull-offs, star power, open notes, solo sections, tempo changes. GH MIDI records what you strummed and when. The editor turns that into a chart.</p>
<p class="muted">Tempo matters. If the DAW tempo is wrong, notes land off the grid and snapping moves them.</p>
</div>
</div>
<img class="shot small" src="assets/ui-chart.jpg" alt="">"""

def p_settings():
    return h2("11", "Settings &amp; other controllers") + """
<div class="cols">
<div>
<img class="shot" src="assets/ui-settings.jpg" alt="">
</div>
<div>
<h3>Controller</h3>
<p>Which USB device GH MIDI listens to. RESCAN after plugging something in. Each controller keeps its own mapping.</p>
<h3>Mapping table</h3>
<p>One row per control. Press <b>LEARN</b> on a row, then press (or sweep) that control on the guitar. <b>X</b> clears a row. Rows light up as you touch controls.</p>
<h3>Virtual MIDI output</h3>
<p>The system-wide <b>GH MIDI</b> input. Leave it on.</p>
</div>
</div>
<h3>Other guitars and gamepads</h3>
<p>Anything that shows up as a USB HID gamepad works: wired PlayStation guitars, PC guitars, generic gamepads. Pick it under Controller, LEARN each row. Frets can be any five buttons; the strum bar can be a d-pad.</p>
<p class="muted">Xbox 360 and Xbox One guitars use XInput, not HID, and are not supported yet.</p>
<h3>Where things live</h3>
<p><code>~/Library/Application Support/GH MIDI/settings.json</code>. Delete it to start over. The app and plugin always start in CHORDS.</p>"""

def p_trouble():
    return h2("12", "Troubleshooting") + f"""
<table class="trouble">
<tr><th style="width:34%">It says / it does</th><th>Try</th></tr>
<tr><td><b>CONTROLLER NOT FOUND</b></td><td>Plug the guitar in. SETTINGS → Controller → pick it → RESCAN. Still nothing: allow your DAW under System Settings → Privacy &amp; Security → Input Monitoring. Make sure the app and the plugin aren't both running.</td></tr>
<tr><td><b>ADAPTER CAN'T SEE THE GUITAR</b></td><td>Unplug the adapter's USB, seat the guitar plug, plug USB back in.</td></tr>
<tr><td>Nothing happens when I strum</td><td>If the gold name pops on screen, MIDI isn't reaching an instrument: section 03.</td></tr>
<tr><td>The plugin's own track is silent</td><td>Normal. It makes no audio. Route its MIDI to an instrument.</td></tr>
<tr><td>Whammy doesn't bend the pitch</td><td>Some presets ignore pitch bend. Link CC 20 to a pitch or filter knob instead.</td></tr>
<tr><td>A strum triggers twice, or the key changes on its own</td><td>Whammy motion leaking into other contacts. Ease off the whammy, or strum a moment after it.</td></tr>
<tr><td>The key jumped</td><td>The joystick was bumped. Flick it the other way. The screen shows the key.</td></tr>
<tr><td>It crashed my DAW</td><td>Send the report from ~/Library/Logs/DiagnosticReports and what you were doing to the address below.</td></tr>
</table>

<h3>Source code, updates, help</h3>
<p>Source code: <b>{REPO}</b> — free and open source (AGPL-3.0)<br>
Guide and updates: <b>{SITE}</b><br>
Email: <b>hello@michaelloya.studio</b></p>

<h3>Credits</h3>
<p class="muted">Built with JUCE (AGPLv3) and hidapi. Metal Mania typeface by Open Window, SIL Open Font License. Guitar Hero is a trademark of Activision; this project is not affiliated with Activision or Nintendo.</p>
<p class="muted">GH MIDI and this guide were made with Claude Code. Some details may be inaccurate. If something here doesn't match what you see, trust the plugin, and send a note to the address above.</p>
<div class="backmark"><img src="assets/logo-red.svg" alt="" class="backmark-logo"><div class="backmark-site">michaelloya.studio</div></div>"""

# ---------------------------------------------------------------- html
CSS = """
@import url('https://fonts.googleapis.com/css2?family=Inter:ital,wght@0,300..700;1,300..700&display=swap');
@font-face { font-family: 'Metal Mania'; src: url('assets/MetalMania-Regular.ttf') format('truetype'); }
@page { size: letter; margin: 0; }
html, body { margin: 0; padding: 0; }
body { background: #E8E0D4; color: #3d2b1f; font-family: 'Inter', system-ui, -apple-system, sans-serif;
       font-size: 10.2pt; line-height: 1.45; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
.page { width: 8.5in; height: 11in; padding: 0.8in 0.75in 0.7in; position: relative; overflow: hidden;
        background: #E8E0D4; page-break-after: always; box-sizing: border-box; }
.page:last-child { page-break-after: auto; }
.page.dark { background: #0a0a0a; color: #E8E0D4; padding: 0.7in 0.75in; }
h1, h2, h3 { font-family: 'Inter', system-ui, sans-serif; color: #0a0a0a; margin: 0; }
h2 { font-size: 24pt; font-weight: 600; letter-spacing: -0.01em; line-height: 1.1; }
h3 { font-size: 10.5pt; font-weight: 600; text-transform: uppercase; letter-spacing: 0.09em; margin: 13pt 0 5pt; }
h3 .muted { text-transform: none; letter-spacing: 0; font-weight: 500; }
.num { color: #de0000; font: 600 9pt 'Inter', sans-serif; letter-spacing: 0.16em; margin-bottom: 2pt; }
.rule { border: 0; border-top: 2px solid #de0000; width: 34pt; margin: 8pt 0 12pt; }
.pagehead { position: absolute; top: 0.42in; left: 0.75in; right: 0.75in; display: flex; justify-content: space-between;
            font: 500 7.5pt 'Inter', sans-serif; letter-spacing: 0.16em; text-transform: uppercase; color: #8a8580; }
.pagefoot { position: absolute; bottom: 0.4in; left: 0.75in; right: 0.75in; display: flex; justify-content: space-between;
            align-items: center; font: 500 7.5pt 'Inter', sans-serif; letter-spacing: 0.08em; color: #8a8580; }
.pagefoot .brand { display: inline-flex; align-items: center; gap: 6pt; }
.pagefoot img { height: 11px; width: auto; opacity: 0.85; }
p { margin: 0 0 7pt; }
p, li, td { text-wrap: pretty; }
.lead { font-size: 12pt; color: #0a0a0a; line-height: 1.4; }
.muted { color: #8a8580; }
b { font-weight: 600; color: #0a0a0a; }
code, .mono { font-family: 'SF Mono', Menlo, monospace; font-size: 8.8pt; }
.cmd { display: block; background: #0a0a0a; color: #E8E0D4; padding: 6pt 9pt; border-radius: 4px; font-size: 8.4pt;
       margin: 4pt 0 2pt; white-space: pre; }
.steps { counter-reset: step; list-style: none; padding: 0; margin: 0 0 6pt; }
.steps li { position: relative; padding-left: 26pt; margin-bottom: 6pt; }
.steps li::before { counter-increment: step; content: counter(step); position: absolute; left: 0; top: 1pt; width: 16pt; height: 16pt;
                    border-radius: 50%; background: #de0000; color: #E8E0D4; font: 600 8.5pt 'Inter', sans-serif;
                    text-align: center; line-height: 16pt; }
table { border-collapse: collapse; width: 100%; font-size: 9.4pt; }
th { text-align: left; font: 600 7.5pt 'Inter', sans-serif; letter-spacing: 0.12em; text-transform: uppercase; color: #8a8580;
     padding: 3pt 6pt 4pt; border-bottom: 1px solid rgba(61,43,31,0.4); }
td { padding: 4pt 6pt; border-bottom: 1px solid rgba(61,43,31,0.14); vertical-align: middle; }
tr:last-child td { border-bottom: 0; }
tr.group td { padding: 6pt 6pt 2pt; border-bottom: 0; font: 600 7.5pt 'Inter', sans-serif; letter-spacing: 0.1em; text-transform: uppercase; color: #de0000; }
table.toc td { padding: 3pt 6pt; }
table.toc td:first-child { color: #de0000; font: 600 8.5pt 'Inter', sans-serif; letter-spacing: 0.12em; width: 34pt; }
table.toc td:last-child { text-align: right; color: #8a8580; width: 30pt; }
.cols { display: grid; grid-template-columns: 1fr 1fr; gap: 20pt; align-items: start; }
.cols h3:first-child { margin-top: 0; }
.shotwrap { position: relative; margin: 2pt 0 10pt; }
.shotwrap.mid { width: 80%; margin-left: auto; margin-right: auto; }
.shot { width: 100%; display: block; border-radius: 6px; }
.shot.small { width: 44%; margin: 8pt auto 0; }
.callout { position: absolute; width: 17pt; height: 17pt; border-radius: 50%; background: #de0000; color: #E8E0D4;
           font: 600 9pt 'Inter', sans-serif; text-align: center; line-height: 17pt; transform: translate(-50%, -50%);
           box-shadow: 0 0 0 2px #E8E0D4; }
.cn { display: inline-block; width: 15pt; height: 15pt; border-radius: 50%; background: #de0000; color: #E8E0D4;
      font: 600 8pt 'Inter', sans-serif; text-align: center; line-height: 15pt; }
table.legend { font-size: 9pt; }
table.legend td { padding: 2.5pt 5pt; }
table.legend td:first-child { width: 18pt; }
.note { border-left: 2px solid #de0000; padding: 2pt 0 2pt 10pt; margin: 9pt 0; color: #0a0a0a; }
.figure { margin: 4pt 0 8pt; }
.pat { display: inline-flex; align-items: center; gap: 5pt; white-space: nowrap; }
.pat code { color: #8a8580; font-size: 8pt; }
.fg { vertical-align: middle; }
.pats { display: flex; gap: 7pt; align-items: center; }
table.progs td { padding-top: 5pt; padding-bottom: 5pt; }
.binary { display: flex; gap: 14pt; align-items: center; margin: 6pt 0 8pt; font: 600 10pt 'Inter', sans-serif; color: #0a0a0a; }
.binary span { display: inline-flex; align-items: center; gap: 5pt; white-space: nowrap; flex-shrink: 0; }
.charts { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10pt; margin: 2pt 0 8pt; }
table.chart { font-size: 8.8pt; }
table.chart td { padding: 2.5pt 4pt; }
table.chart th { padding: 2pt 4pt 3pt; }
.riff { display: flex; align-items: center; gap: 4pt 0; flex-wrap: wrap; margin: 2pt 0 6pt; }
.riff .bar { display: inline-flex; align-items: center; gap: 3pt; white-space: nowrap; padding-right: 8pt; margin-right: 8pt; border-right: 1.5px solid rgba(61,43,31,0.35); }
.riff .bar:last-child { border-right: 0; }
table.trouble td { padding: 5pt 6pt; vertical-align: top; }

/* cover */
.cover { height: 100%; display: flex; flex-direction: column; }
.cover-logo { height: 34px; width: auto; }
.cover-site { font: 500 11pt 'Inter', sans-serif; letter-spacing: 0.16em; text-transform: uppercase; color: #E8E0D4; }
.backmark { margin-top: 28pt; display: flex; align-items: center; gap: 10pt; }
.backmark-logo { height: 22px; width: auto; }
.backmark-site { font: 500 10pt 'Inter', sans-serif; letter-spacing: 0.16em; text-transform: uppercase; color: #3d2b1f; }
.cover-mid { margin-top: 0.75in; }
.wordmark { font-family: 'Metal Mania', 'Inter', sans-serif; font-size: 84pt; line-height: 0.95; color: #E8E0D4; letter-spacing: 0.01em; }
.cover-kicker { font: 600 11pt 'Inter', sans-serif; letter-spacing: 0.32em; color: #de0000; margin: 10pt 0 14pt 3pt; }
.cover-lead { font-size: 14pt; color: #E8E0D4; opacity: 0.85; margin-left: 3pt; line-height: 1.35; }
.cover-shot { width: 100%; border-radius: 8px; margin-top: auto; display: block; }
.cover-bottom { display: flex; justify-content: space-between; align-items: baseline; margin-top: 18pt; }
.cover-meta { font: 500 8pt 'Inter', sans-serif; letter-spacing: 0.14em; text-transform: uppercase; color: #8a8580; }
"""

def build_html():
    pages = [
        page(cover(), "", 1, dark=True, plain=True),
        page(p_welcome(), "Welcome", 2),
        page(p_install(), "Install", 3),
        page(p_daw(), "Connect to your DAW", 4),
        page(p_controller(), "Your controller", 5),
        page(p_screen(), "The screen", 6),
        page(p_playing(), "How it plays", 7),
        page(p_chords(), "CHORDS mode", 8),
        page(p_songs(), "CHORDS mode", 9),
        page(p_notes(), "NOTES mode", 10),
        page(p_solo(), "SOLO mode", 11),
        page(p_chart(), "CHART mode", 12),
        page(p_settings(), "Settings", 13),
        page(p_trouble(), "Troubleshooting", 14),
    ]
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>GH MIDI User Guide</title>
<style>{CSS}</style></head>
<body>
{''.join(pages)}
</body></html>"""

def main():
    html_path = os.path.join(HERE, "guide.html")
    with open(html_path, "w") as f:
        f.write(build_html())
    print("wrote", html_path)
    if "--html" in sys.argv:
        return
    out = os.path.join(ROOT, "release", "GH MIDI Guide.pdf")
    chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    profile = os.path.join(HERE, ".chrome-profile")
    if os.path.exists(out):
        os.remove(out)
    # headless Chrome on macOS prints the file, then sometimes never exits: poll for the PDF and stop it ourselves
    proc = subprocess.Popen([chrome, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                             "--virtual-time-budget=12000", f"--print-to-pdf={out}",
                             f"--user-data-dir={profile}", "file://" + html_path],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(90):
        if os.path.exists(out) and os.path.getsize(out) > 0 and proc.poll() is not None:
            break
        if os.path.exists(out) and os.path.getsize(out) > 0:
            time.sleep(2)
            break
        time.sleep(1)
    if proc.poll() is None:
        proc.kill()
    shutil.rmtree(profile, ignore_errors=True)
    if not os.path.exists(out):
        sys.exit("PDF was not produced")
    print("wrote", out, os.path.getsize(out) // 1024, "KB")

if __name__ == "__main__":
    main()
