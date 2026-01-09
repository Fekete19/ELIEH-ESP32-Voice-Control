# 🎙️ ELIEH – ESP32 Hangvezérlés (AI Thinker AudioKit A1S)

## 📌 Tartalom

- [Fő jellemzők](#-fő-jellemzők)
- [Hogyan működik?](#-hogyan-működik)
- [Tanítás (LEARN mód)](#-tanítás-learn-mód)
- [Végrehajtás (RUN mód)](#-végrehajtás-run-mód)
- [Fizikai gombok](#-fizikai-gombok)
- [OpenAI API kulcs](#-openai-api-kulcs)
- [Hardver](#-hardver)
- [Következő lépések](#-következő-lépések)

Az **ELIEH** egy ESP32-alapú, **tanítható hangvezérlő rendszer**, amely  
offline hangrögzítést és **online beszédfelismerést (OpenAI STT)** használ.

A projekt célja egy **otthoni automatizálásra szánt, saját hangparancsokkal tanítható rendszer**,  
amely később **Tuya / SmartLife** eszközöket (relék, lámpák, fűtés, bojler stb.) vezérel.

---

## ✨ Fő jellemzők

- 🎙️ **Offline hangrögzítés** az ESP32-A1S beépített mikrofonjáról  
- 🧠 **Tanítható hangparancsok**: saját mondatok → saját címkék (pl. *„konyha világítás be”*)  
- 🗣️ **Online beszédfelismerés (OpenAI STT)**: a rögzített WAV fájlt szöveggé alakítja  
- 💾 **SD-kártyás tárolás**: rögzítés (`rec.wav`) + tanítási térkép (`voice_map.txt`)  
- 🎛️ **Fizikai gombvezérlés** (MENU / NEXT / OK / TEACH)  
- 🇭🇺 **Magyar nyelvű parancsok és menü**  
- 🔒 **TLS kompatibilis működés**: ha van CRT bundle → stabil, ha nincs → ideiglenes fallback (teszthez)

---

## ⚙️ Hogyan működik?

Az **ELIEH** rendszer egy több lépcsős hangfeldolgozási folyamatot valósít meg, amely ötvözi az **offline hangrögzítést** és az **online beszédfelismerést**.

---

### 1️⃣ Hangfigyelés (VAD – Voice Activity Detection)

- Az ESP32 **folyamatosan figyeli a környezeti zajszintet**
- Induláskor **automatikusan megtanulja az alap zajszintet**
- Dinamikus küszöböt számol (RMS + csúcsérték alapján)
- **Csak akkor indul el a rögzítés**, ha valódi beszédet érzékel  
- Ez megakadályozza a felesleges rögzítéseket (zaj, koppanás, szünetek)

---

### 2️⃣ Hangrögzítés

- Felvételi paraméterek:
  - **16 kHz**
  - **16 bit**
  - **mono WAV**
- A beszéd **SD kártyára kerül mentésre** (`/rec.wav`)
- Előpuffer (preroll) biztosítja, hogy a beszéd eleje se vesszen el
- Maximális felvételi idő védi a rendszert a beragadástól

---

### 3️⃣ Beszédfelismerés (STT – Speech-to-Text)

- A rögzített WAV fájl **HTTPS kapcsolaton** keresztül elküldésre kerül
- Használt végpont:
---

### 2️⃣ Hangrögzítés
- 16 kHz / 16 bit / mono WAV
- SD kártyára mentve:
- ---

## 🎓 Tanítás (LEARN mód)

A **LEARN mód** lehetővé teszi, hogy a rendszer **saját hangparancsokat tanuljon meg**, amelyeket később automatikusan felismer és végrehajt.

---

### 🧩 A tanítás lépései

1. **Címke megadása (soros porton)**
   - A felhasználó beír egy logikai parancsot (pl.):
     ```
     konyha világítás be
     ```
   - Ez lesz a **címke (label)**, amelyhez a hangparancs tartozik

2. **TEACH gomb megnyomása**
   - A rendszer „felfegyverzi” a tanítási módot
   - Aktiválódik a VAD (hangfigyelés)

3. **Hangparancs kimondása**
   - Pl.:  
     > „kapcsold fel a konyhai lámpát”
   - A beszéd WAV fájlba rögzül az SD kártyán

4. **Beszédfelismerés (STT)**
   - A rögzített hang elküldésre kerül az OpenAI STT szolgáltatásnak
   - A visszakapott szöveg normalizálásra kerül

5. **Eltárolás**
   - A rendszer eltárolja:
     ```
     felismert mondat → címke
     ```
   - Az adatok az SD kártyára kerülnek (`voice_map.txt`)
   - Újraindítás után is megmaradnak

---

### 💾 Tanítási adattérkép (voice_map.txt)

Az SD kártyán tárolt fájl formátuma:

```txt
# phrase|label
kapcsold fel a konyhai lámpát|konyha világítás be
oltsd le a fürdőt|fürdő világítás ki

---

## ▶️ Végrehajtás (RUN mód)

A **RUN mód** a rendszer normál működési állapota, ahol az ELIEH **figyeli a beszédet**, felismeri a korábban tanított parancsokat, és **végrehajtja a hozzájuk rendelt műveleteket**.

---

### 🔄 A működés folyamata

1. **Beszéd érzékelése**
   - A VAD (Voice Activity Detection) csak akkor aktiválódik, ha valódi beszédet érzékel
   - Zaj, koppanás, háttérhang nem indít műveletet

2. **Hangrögzítés**
   - A beszéd WAV fájlba kerül (`rec.wav`)
   - SD kártyára mentve, preroll védelemmel

3. **Beszédfelismerés (STT)**
   - A WAV fájl HTTPS kapcsolaton keresztül elküldésre kerül az OpenAI STT szolgáltatásnak
   - A visszakapott szöveg normalizálásra kerül (kisbetű, felesleges szóközök eltávolítása)

4. **Egyezés keresése**
   - A rendszer megkeresi a felismert mondatot a tanítási térképben (`voice_map.txt`)
   - Pontos egyezés esetén megtalálja a hozzárendelt címkét

5. **Végrehajtás**
   - A címkéhez tartozó művelet lefut
   - Jelenleg **logikai szinten történik a végrehajtás**
   - Később ez lesz összekötve Tuya / SmartLife / MQTT / relék irányába

---

### 📌 Példa

**Tanított parancs:**

---

## 🎛️ Fizikai gombok (MENU / NEXT / OK / TEACH)

Az **ELIEH** rendszer fizikai gombokkal is teljes mértékben vezérelhető, így **kijelző nélkül**, önálló eszközként is használható.

A gombok célja:
- menükezelés
- tanítás indítása
- működési módok közötti váltás
- biztonságos felhasználói interakció

---

### 🧩 Használt gombok

| Gomb | Funkció |
|-----|--------|
| **MENU** | Menü megjelenítése / frissítése |
| **NEXT** | Következő menüpont |
| **OK** | Aktuális menüpont kiválasztása |
| **TEACH** | Tanítás indítása (LEARN módban) |

> ⚠️ Megjegyzés:  
> A **KEY2 (GPIO13)** nem használható gombként, mert az **SD kártya DAT3 vonalán** van.  
> A rendszer ezt figyelembe veszi.

---

### 🔘 Gombkiosztás (AI Thinker AudioKit A1S)

```text
KEY1 → NEXT
KEY4 → MENU
KEY6 → OK
KEY5 → TEACH

---

## 🔑 OpenAI API kulcsok (Speech-to-Text)

Az **ELIEH** rendszer az online beszédfelismeréshez az **OpenAI Speech-to-Text (STT)** szolgáltatását használja.  
Ehhez **érvényes OpenAI API kulcs** szükséges.

---

### 🌐 Használt szolgáltatás

- Funkció: **Speech-to-Text (STT)**
- Protokoll: **HTTPS (multipart/form-data)**
- Küldött adat: **WAV hangfájl**
- Válasz: **felismert szöveg (text)**

---

### 📡 Használt végpont (endpoint)

---

### 🧠 Használt modell

gpt-4o-transcribe

---

Ez a modell:
- jól kezeli a **magyar nyelvet**
- rövid parancsokra optimalizált
- alacsony válaszidővel működik
- alkalmas embedded / IoT környezethez

---

### 🧾 API kulcs létrehozása

1. Nyisd meg:  
   👉 https://platform.openai.com/account/api-keys
2. Hozz létre egy új API kulcsot
3. Másold ki (⚠️ később nem látható újra)
4. Ellenőrizd, hogy **van aktív számlázás** (billing)

> ⚠️ Fontos:  
> Ingyenes kulcs **nem elegendő**, a STT szolgáltatáshoz aktív egyenleg szükséges.

---

### 🛠️ API kulcs beállítása a kódban

A kulcsot **közvetlenül a forráskódban** kell megadni:

```cpp
static const char* OPENAI_API_KEY = "sk-xxxxxxxxxxxxxxxxxxxxxxxxxxxx";

---

## 📦 Könyvtárak telepítése (ES8388 codec)

A projekt az **AI Thinker AudioKit A1S** kártya **ES8388** audió kodekjét használja.  
Ehhez szükség van az ES8388 könyvtárra, amit a repóban **ZIP formában** mellékeltem.

### 1) ArduinoDroid (Android) – ajánlott telepítés

1. Töltsd le a repót (Code → Download ZIP), vagy csak az `ES8388` könyvtár ZIP fájlját.
2. Nyisd meg a ZIP-et (pl. RAR appal), és **csomagold ki** az ES8388 könyvtárat az ArduinoDroid **felhasználói könyvtárába**.

**A cél:** az ES8388 könyvtár ebbe a mappába kerüljön:

- `Arduino/libraries/ES8388/`

A mappaszerkezet így nézzen ki: Arduino/ libraries/ ES8388/ ES8388.h ES8388.cpp

---

## 📦 ES8388 könyvtár telepítése ArduinoDroid alatt

> ⚠️ **Fontos megjegyzés**  
> Az ArduinoDroid jelenleg **nem támogatja megbízhatóan a ZIP formátumú könyvtárak közvetlen importálását**, ezért az alábbi **kézi módszer ajánlott és tesztelt**.

---

### 📱 Szükséges alkalmazás

- **RAR** (vagy bármilyen ZIP/RAR kezelő Androidra)

---

### 🔧 Telepítési lépések (ArduinoDroid – működő módszer)

1. Töltsd le a repóból az **`ES8388` könyvtárat tartalmazó ZIP fájlt**.
2. Nyisd meg a ZIP fájlt a **RAR alkalmazással**.
3. A ZIP-en belül jelöld ki az **`ES8388` mappát**, amely az alábbi fájlokat tartalmazza:
   - `ES8388.h`
   - `ES8388.cpp`
4. Válaszd a **Kibontás / Másolás ide** opciót.
5. Tallózd ki az ArduinoDroid felhasználói könyvtárát:

```text
/storage/emulated/0/Arduino/libraries/
Egyes rendszereken az alábbi útvonal is használatos:
/storage/emulated/0/Arduino/User/libraries/


Illeszd be ide az ES8388 mappát.
📁 Könyvtárstruktúra ellenőrzése
A könyvtárnak pontosan így kell kinéznie:

Arduino/
└── libraries/
    └── ES8388/
        ├── ES8388.h
        └── ES8388.cpp


külső Library Manager nélkül
belső #include "ES8388.h" hivatkozással
hibamentesen működik.
