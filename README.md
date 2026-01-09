Rövid leírás
Az ELIEH egy ESP32-alapú, tanítható offline hangrögzítés + online beszédfelismerés rendszer, amely képes:
hangparancsok tanulására
a tanult parancsok felismerésére
parancs → címke (label) párosítására
parancsok végrehajtására (jelenleg logikai szinten)
később Tuya / SmartLife rendszerek vezérlésére
A projekt kifejezetten AI Thinker ESP32 AudioKit A1S (ES8388) hardverhez készült.
🎯 A projekt célja
A cél egy hangvezérelt otthonautomatizálási központ, amely:
nem fix parancslistát használ
tanítható (a felhasználó saját hangjával)
SD kártyán tárolja a tanított parancsokat
később Tuya / SmartLife reléket, lámpákat, fűtést, bojlert stb. vezérel
A rendszer úgy van felépítve, hogy a tanítási adatok később újrahasznosíthatók, nem kell újratanítani az eszközöket.
🧠 Hogyan működik?
1️⃣ Hangfigyelés (VAD – Voice Activity Detection)
Az ESP32 folyamatosan figyeli a mikrofont:
automatikusan megtanulja a környezeti zajszintet
dinamikus küszöböt állít be
csak akkor rögzít, amikor valódi beszédet érzékel
2️⃣ WAV rögzítés SD kártyára
Beszéd esetén:
16 kHz / 16 bit mono WAV fájl készül
preroll bufferrel (a szó eleje sem vész el)
a fájl az SD kártyára kerül (/rec.wav)
3️⃣ Beszédfelismerés (STT – Speech-to-Text)
A rögzített WAV fájl elküldésre kerül az OpenAI Speech-to-Text API felé:
HTTPS kapcsolaton
multipart/form-data POST kérésként
válaszként szöveges átirat érkezik
4️⃣ Tanítás (LEARN mód)
Tanításkor:
Soros porton megadsz egy címkét (pl. bojler kikapcsol)
Megnyomod a TEACH gombot
Kimondod a parancsot (pl. „a bojler kikapcsol”)
A rendszer eltárolja:
Kód másolása

"a bojler kikapcsol." -> bojler kikapcsol
Ez SD kártyára mentődik, túléli az újraindítást.
5️⃣ Végrehajtás (RUN mód)
RUN módban:
kimondod a parancsot
a rendszer felismeri
megkeresi a tanult listában
ha van találat → végrehajtja a hozzárendelt címkét
Jelenleg a végrehajtás logikai szinten (Serial log) történik, de a struktúra már kész a Tuya vezérléshez.
🧩 Jelenlegi funkciók
✅ Tanítható hangparancsok
✅ SD-kártyás tárolás
✅ Menü gombokkal (MENU / NEXT / OK / TEACH)
✅ Stabil VAD
✅ OpenAI STT integráció
✅ Több száz tanítás kezelése
✅ Magyar nyelvű parancsok
🔮 Tervezett funkciók
🔜 Tuya / SmartLife vezérlés
🔜 Soros Tuya MCU protokoll
🔜 MQTT / WiFi bridge
🔜 Offline parancs-cache
🔜 TTS visszajelzés (beszélő válasz)
🔑 OpenAI API kulcs használata
A beszédfelismeréshez OpenAI API kulcs szükséges.
Használt végpont:
Kód másolása

POST https://api.openai.com/v1/audio/transcriptions
Használt modell:
Kód másolása

gpt-4o-transcribe
Beállítás a kódban:
Kód másolása
Cpp
static const char* OPENAI_API_KEY = "sk-xxxxxxxxxxxxxxxx";
⚠️ Figyelem
A kulcs NEM része a repónak
Csak lokálisan add meg
Tesztkulccsal is működik
Quota és billing szükséges
🔐 TLS megoldás
A kód automatikusan kezeli:
ESP32 core-tól függően a CRT bundle-t
fallback módban ideiglenes INSECURE TLS (teszteléshez)
Ez lehetővé teszi a stabil HTTPS kapcsolatot régebbi Arduino core esetén is.
🧰 Hardver
ESP32 AudioKit A1S
ES8388 audio codec
SD kártya
Beépített mikrofon
Fizikai gombok:
MENU
NEXT
OK
TEACH
📁 Fájlstruktúra
Kód másolása

/ELIEH-ESP32-VOICE/
 ├── ESP32A1S_API_KEY_V2_full.ino
 ├── btn_fix.h
 ├── README.md
📌 Megjegyzés
Ez a projekt aktív fejlesztés alatt áll.
A jelenlegi verzió már stabil tanításra és felismerésre, a következő lépés a SmartLife / Tuya integráció.
Ha szeretnéd, a következő lépésben:
🔹 megírom a Tuya command formátum szabványt
🔹 vagy átalakítom ezt angol README.md-re
🔹 vagy készítek hozzá diagramot (VAD → WAV → STT → ACTION)
