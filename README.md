# VocalNite

A concatenative vocal-synthesis DAW built with C++ and the JUCE framework.
Write notes on a piano roll, attach lyrics, and the engine sings them back
using a phoneme-by-phoneme voice bank.

---

## Project Description

VocalNite is a desktop DAW (Digital Audio Workstation) that synthesises
singing voice from typed lyrics. The user places notes on a piano roll,
types a word into each note, and the engine:

- Looks up the word in the CMU Pronouncing Dictionary to get its ARPAbet
  phoneme sequence
- Matches each phoneme (and diphone transition) to a pre-recorded WAV sample
  from the active voice bank
- Applies crossfading, vibrato, timing jitter, and pitch-shifting to produce
  natural-sounding audio timed to the project BPM

Patterns can be arranged across multiple tracks on a timeline, exported to a 
16-bit stereo WAV, and saved to a Supabase PostgreSQL database so projects 
persist across sessions. A fighting-game-style voice bank selector allows 
hot-swapping between voices mid-session without audio dropout.

An optional Educational Mode, exclusive to verified `.edu` accounts, adds
cyan-pulse highlights on controls, the Milk-en Female Voicebank, and the 
SynthesisInspector — a step-by-step panel that shows exactly how each word is broken
down and rendered.

---

## Team Members

| Name | Primary Contributions |
|------|-----------------------|
| [Luke Gomes] | [Database​​, Back-end(Authentication & DAW Skeleton, Piano Roll)​​, UI] |
| [Nathaniel] | [Synthesis Engine, Back-end(DAW, Project Manager Page, UI)] |
| [Aaron] | [Educational Mode, Aaron Voicebank] |

---

## Technologies Used

| Technology | Version | Purpose |
|------------|---------|---------|
| C++ | 19.5 | Application language |
| JUCE | 8.x | Audio, UI, and cross-platform framework |
| PostgreSQL (Supabase) | 18 | Cloud project/user persistence |
| libpq | 18.x | Native PostgreSQL wire protocol client |
| libcurl | 8.x | SMTPS email verification |
| Visual Studio | 2022 | Build toolchain (Windows) |
| [CMU Pronouncing Dictionary](https://github.com/cmusphinx/cmudict) | 0.7b | Word-to-ARPAbet phoneme lookup (~130 k entries) |

---

## Installation Instructions

### Prerequisites

Before downloading, make sure you have all of the following installed or available:

| Requirement | Notes |
|-------------|-------|
| Windows 10 or 11 | Only supported platform |
| Gmail account + App Password | Required only for Educational Mode email verification

---

### Step 1 — Download the Release.zip file

### Step 2 — Unzip the Release.zip file

---

## Running the Application

1. Launch `VocalNite.exe` in Release/App/
2. **Create an account** — click **Sign Up**, enter a username, email, and password. Using a `.edu` email creates an Educational account and triggers a verification email.
3. **Log in** — click **Login** and enter your credentials. Educational accounts must verify their email first (use the **Verify Account** button and paste the token from your inbox).
4. **Create a project** — click **+ New Project** on the dashboard, enter a name, and click Create.
5. **Open the DAW** — double-click a project from the list.
6. **Add a pattern** — click **+ Add Pattern** in the left panel. Double-click the pattern to open the piano roll.
7. **Place notes and lyrics** — click an empty cell to place a note, type a word into the inline editor, and press Enter to save.
8. **Arrange** — drag patterns from the browser onto a track row to place clips on the timeline.
9. **Play** — press the Play button in the transport bar. The voice bank loads in the background — transport controls un-dim once loading finishes (a few seconds on a typical SSD).
10. **Export** — File → Export As to render the full timeline to a 16-bit stereo WAV file.

---

## Screenshots

Login screen — animated star-field backdrop, Sign Up / Login
<img width="638" height="427" alt="image" src="https://github.com/user-attachments/assets/2bc89ba6-489f-4452-97b7-d104c7ddf1ab" />

Project Manager Page - Recent Projects, New Project & Open Project, Logout
<img width="610" height="409" alt="image" src="https://github.com/user-attachments/assets/c860fa81-1b34-4a13-87f5-0fa9a449a6d6" />

DAW — Timeline view — pattern browser, track lanes, placed clips, playhead, transport bar
<img width="1278" height="720" alt="image" src="https://github.com/user-attachments/assets/a012fe6b-58f1-4306-99b4-b7bacb3e6bac" />

Piano roll — 15-row note grid with inline lyric editor
<img width="1247" height="582" alt="image" src="https://github.com/user-attachments/assets/bc4b96e9-12cb-48aa-ab3c-9b67913d47b4" />

## Acknowledgements

- **[CMU Pronouncing Dictionary](https://github.com/cmusphinx/cmudict)** — developed and maintained by Carnegie Mellon University. Used for word-to-ARPAbet phoneme lookup.
- **[milk-en](https://github.com/oxygen-dioxide/milk-en)** by [oxygen-dioxide](https://github.com/oxygen-dioxide) — the female English UTAU voice bank included as the second selectable voice. Used under its repository license.
- **[JUCE](https://juce.com)** — cross-platform C++ framework for audio and UI.

---

## License

See `LICENSE` for details.
