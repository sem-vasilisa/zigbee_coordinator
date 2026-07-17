# Jak zbudować własny Zigbee Coordinator na nRF52840 Dongle

Sprzęt i software:

| Element     | Wartość                                                     |
| ----------- | ------------------------------------------------------------- |
| Komputer    | Laptop z Windows                                              |
| Płytka     | nRF52840 Dongle (PCA10059) — mały USB-stick od Nordica      |
| IDE         | VS Code + rozszerzenie**nRF Connect for VS Code**       |
| SDK         | nRF Connect SDK (NCS)**v2.9.2**, Zephyr 3.7             |
| Stos Zigbee | **ZBOSS** (dodatek `ncs-zigbee` od Nordica)           |
| Flashowanie | **nrfutil** przez USB DFU (dongle nie ma programatora!) |

**Ta instrukcja budujesz aplikację etapami.** Zamiast wkleić od razu cały
`main.c`, dokładasz po jednej zdolności i po każdym etapie **flashujesz
i sprawdzasz, że działa**. Dzięki temu, gdy coś się zepsuje, wiesz dokładnie
który krok to spowodował. Etapy:

| Etap | Co dodajesz                       | Jak sprawdzasz, że działa                           |
| ---- | --------------------------------- | ----------------------------------------------------- |
| 0    | Pliki stałe + cykl build/flash   | Projekt się kompiluje                                |
| 1    | Odpalenie ZBOSS                   | Dioda miga = firmware żyje, ZBOSS nie crashuje       |
| 2    | Utworzenie sieci + dioda          | Dioda przechodzi z migania na światło ciągłe      |
| 3    | Logi przez USB                    | Widzisz PAN ID i kanał w nRF Connect Serial Terminal |
| 4    | Wykrycie dołączenia urządzenia | Log "New device joined" po joinie żarówki           |
| 5    | Sterowanie przyciskiem (Toggle)   | Wciśnięcie SW1 przełącza diodę żarówki         |
| 6    | Sterowanie komendą shell         | `toggle` w terminalu przełącza żarówkę         |
| 7    | Nadawanie nazw urządzeniom       | `name lampa`, potem `toggle lampa`                |

Druga część (urządzenie sterowane — Light Bulb) jest w
`../zigbee_light_bulb/INSTRUKCJA_LIGHT_BULB.md`. Środowisko z rozdziałów 2–4
konfigurujesz tylko raz — obie aplikacje budujesz w tym samym workspace.

---

## 1. Słownik pojęć — przeczytaj zanim zaczniesz

### 1.1 Zigbee — sieć

**Zigbee** to protokół radiowy (2.4 GHz, oparty o IEEE 802.15.4) do
automatyki domowej: mało danych, mało prądu, sieć kratowa (mesh). To NIE jest
WiFi ani Bluetooth — to osobne radio, choć nRF52840 potrafi obsłużyć wszystkie
trzy.

Każde urządzenie w sieci pełni jedną z trzech **ról**:

- **Coordinator** — dokładnie jeden w sieci. Tworzy sieć (wybiera kanał
  i PAN ID), decyduje kto może dołączyć, pełni funkcję **Trust Center**
  (zarządza kluczami szyfrowania). To budujemy w tej instrukcji.
- **Router** — dołącza do istniejącej sieci, przekazuje pakiety dalej
  (tworzy mesh), ma stale włączone radio. Zwykle urządzenia zasilane z sieci:
  żarówki, gniazdka.
- **End Device** — dołącza do sieci przez rodzica (Coordinatora lub Routera),
  może spać żeby oszczędzać baterię. Czujniki, piloty.

Ważne: **rola w sieci** i **funkcja urządzenia** to dwa niezależne wymiary.
Nasz Coordinator jest jednocześnie "pilotem" (wysyła komendy), a nasz Router
jest "żarówką" (odbiera komendy). Można to łączyć dowolnie.

Pojęcia adresowe:

- **PAN ID** — 16-bitowy identyfikator sieci (np. `0x3459`). Coordinator losuje
  go przy tworzeniu sieci.
- **Kanał** — Zigbee używa kanałów 11–26 w paśmie 2.4 GHz.
- **IEEE address (long address)** — 64-bitowy, unikalny, fabryczny adres MAC
  urządzenia (jak MAC w Ethernecie). Nigdy się nie zmienia.
- **Short address (network address)** — 16-bitowy adres nadawany przy
  dołączeniu do sieci. Coordinator ma zawsze `0x0000`. Po ponownym dołączeniu
  (rejoin) może się zmienić — dlatego urządzenia identyfikuje się po IEEE.

### 1.2 Zigbee — model aplikacji (najważniejszy rozdział!)

To jest część, która na początku myli najbardziej. Zigbee opisuje urządzenie
trzema warstwami pojęć:

- **Endpoint (EP)** — "gniazdo" logiczne na urządzeniu, numer 1–240. Jedno
  fizyczne urządzenie może mieć wiele endpointów, np. listwa z dwoma
  przekaźnikami ma dwa endpointy — każdy to osobna "logiczna żarówka".
  Endpoint 0 jest zarezerwowany dla warstwy zarządzania (ZDO).
- **Cluster** — zestaw atrybutów i komend realizujący jedną funkcję,
  identyfikowany 16-bitowym ID. Przykłady: `Basic` (0x0000, metadane
  urządzenia), `Identify` (0x0003, "mrugnij żebym cię znalazł"),
  **`On/Off` (0x0006)** — włącz/wyłącz. Katalog standardowych clusterów to
  **ZCL — Zigbee Cluster Library**.
- **Atrybut** — zmienna wewnątrz clustera, np. cluster On/Off ma atrybut
  `OnOff` (bool: świeci / nie świeci).
- **Komenda** — rozkaz wysyłany do clustera, np. On/Off ma komendy `On`,
  `Off`, `Toggle`.

Cluster występuje w dwóch rolach:

- **Server** — trzyma atrybuty i wykonuje komendy. Żarówka ma On/Off
  **server** (u niej jest stan "świeci").
- **Client** — wysyła komendy do serwera. Pilot/przycisk ma On/Off **client**.

W deklaracjach ZBOSS zobaczysz "IN clusters" (= serwery na tym endpointcie)
i "OUT clusters" (= klienci).

Do tego dwie warstwy protokołu, które zobaczysz w kodzie:

- **ZCL (Zigbee Cluster Library)** — warstwa aplikacyjna: komendy typu Toggle,
  odczyt/zapis atrybutów.
- **ZDO (Zigbee Device Object)** — warstwa zarządzania siecią i odkrywania
  urządzeń. Kluczowe zapytania ZDO, których użyjemy:
  - `Active_EP_req` — "jakie masz endpointy?"
  - `Simple_Desc_req` — "co jest na endpointcie X?" (lista clusterów IN/OUT,
    profil, device ID)
  - `Bind_req` — "utwórz binding"

**Binding** — wpis w tablicy bindingów urządzenia mówiący: "ruch z mojego
endpointu X, cluster Y, kieruj do urządzenia Z, endpoint W". Dzięki temu można
wysyłać komendy bez podawania adresu za każdym razem. W naszym kodzie binding
tworzymy (bo trwale zapisuje relację w NVRAM), ale komendę Toggle i tak
adresujemy jawnie — szczegóły w Etapie 5.

**Commissioning (BDB — Base Device Behavior)** — ustandaryzowana procedura
zakładania/dołączania do sieci:

- **Network Formation** — Coordinator tworzy nową sieć.
- **Network Steering** — dla Coordinatora: "otwórz sieć na dołączanie nowych
  urządzeń na 180 s" (tzw. permit join); dla Routera/End Device: "szukaj
  sieci i dołącz".

**Trust Center** — funkcja bezpieczeństwa działająca na Coordinatorze:
wymienia klucze z nowym urządzeniem (TCLK exchange) i autoryzuje je w sieci.

### 1.3 Narzędzia i framework

- **Zephyr RTOS** — system operacyjny czasu rzeczywistego, na którym działa
  aplikacja: wątki, kolejki pracy (`k_work`), semafory (`k_sem`), sterowniki
  GPIO/UART/USB, logging, shell. Twój `main()` to jeden z wątków Zephyra.
- **nRF Connect SDK (NCS)** — SDK Nordica = Zephyr + sterowniki i biblioteki
  Nordica (`nrf`, `nrfxlib`) + narzędzia. My używamy wersji **v2.9.2**.
- **ZBOSS** — komercyjny stos Zigbee firmy DSR, dostarczany przez Nordica jako
  prekompilowana biblioteka. Od NCS 2.8 Zigbee **nie jest częścią głównego
  SDK** — mieszka w osobnym repozytorium-dodatku **`ncs-zigbee`**. To ważne:
  stare tutoriale z `CONFIG_ZIGBEE=y` nie zadziałają, dodatek używa
  `CONFIG_ZIGBEE_ADD_ON=y`.
- **west** — narzędzie Zephyra do zarządzania workspace'em: klonuje kilkanaście
  repozytoriów (zephyr, nrf, nrfxlib, ncs-zigbee, ...) w wersjach opisanych
  w pliku-manifeście `west.yml` i buduje projekty.
- **Kconfig / `prj.conf`** — system konfiguracji (ten sam co w jądrze Linuksa).
  W `prj.conf` włączasz opcjami `CONFIG_...=y` całe podsystemy: logging, USB,
  shell, Zigbee. Kconfig decyduje co w ogóle zostanie wkompilowane.
- **Devicetree / overlay** — deklaratywny opis sprzętu (jakie peryferia, na
  jakich pinach). Definicja płytki dostarcza bazowy devicetree; plik
  `boards/<płytka>.overlay` w projekcie go modyfikuje. W kodzie C czytasz go
  makrami `DT_...`.
- **Partition Manager / `pm_static.yml`** — narzędzie NCS dzielące flash na
  partycje (aplikacja, NVRAM ZBOSS, bootloader...). Plik `pm_static.yml`
  przybija partycje do stałych adresów.
- **Bootloader / DFU** (Device Firmware Update) — dongle **nie ma programatora
  J-Link**. Flashuje się go przez fabryczny bootloader USB: wciskasz przycisk
  RESET, dongle zgłasza się jako urządzenie DFU (pulsująca czerwona dioda),
  a `nrfutil` wysyła firmware po USB.
- **USB CDC ACM** — profil USB "wirtualny port szeregowy". Nasz firmware
  zgłasza się jako COM-port, przez który lecą logi i działa shell.
- **Shell Zephyra** — interaktywna linia komend na porcie szeregowym.
  Zarejestrujemy w niej własne komendy (`name`, `toggle`, ...).

### 1.4 Jak aplikacja rozmawia z ZBOSS — model programowania

ZBOSS działa we **własnym wątku** i ma trzy punkty styku z twoim kodem:

1. **`zboss_signal_handler(zb_bufid_t bufid)`** — funkcja, którą MUSISZ
   zdefiniować. ZBOSS woła ją przy każdym zdarzeniu stosu ("sygnale"):
   sieć utworzona, urządzenie dołączyło, steering zakończony itd. To jest
   serce aplikacji Zigbee — będziemy ją rozbudowywać w kolejnych etapach.
2. **Bufory (`zb_bufid_t`)** — ZBOSS ma własną pulę buforów na
   pakiety/parametry. Prawie każde API bierze `bufid`. Bufor **zawsze trzeba
   zwolnić** (`zb_buf_free`) — wyciek = po kilku zdarzeniach stos staje.
   Nowy bufor pobiera się asynchronicznie:
   `zb_buf_get_out_delayed(moj_callback)` → ZBOSS odda ci bufor wywołując
   `moj_callback(bufid)` we własnym wątku.
3. **Scheduler ZBOSS** — API ZBOSS wolno wywoływać tylko z wątku ZBOSS.
   Z innego wątku (shell, GPIO ISR, work queue) zlecasz wykonanie przez
   `ZB_SCHEDULE_APP_CALLBACK(fn, arg)` albo właśnie
   `zb_buf_get_out_delayed(fn)`.

---

## 2. Instalacja środowiska

1. **VS Code** — https://code.visualstudio.com
2. **nRF Connect for Desktop** — https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop
   Z niego zainstaluj aplikację **Serial Terminal** (będzie potrzebna do logów
   i shella).
3. **nRF Util** — pobierz `nrfutil.exe` ze strony Nordica
   (https://www.nordicsemi.com/Products/Development-tools/nRF-Util), wrzuć do
   katalogu w `PATH`, potem w bashu (Git Bash):

   ```bash
   nrfutil install device          # wykrywanie urządzeń po USB
   nrfutil install nrf5sdk-tools   # pakiet z komendami DFU dla dongla
   ```
4. **Rozszerzenie VS Code**: zainstaluj **"nRF Connect for VS Code Extension
   Pack"** (instaluje też CMake, devicetree itp.).
5. **Toolchain NCS**: w VS Code otwórz panel nRF Connect (ikona z boku) →
   **Manage toolchains** → **Install Toolchain** → wybierz **v2.9.2**.
   Toolchain (kompilator ARM GCC, CMake, west, Python) ląduje w `c:\ncs\toolchains\`.

> **Czym różni się toolchain od SDK?** Toolchain to narzędzia do budowania.
> SDK (źródła Zephyra, Nordica, ZBOSS) pobierzemy sami westem w następnym
> kroku — dzięki temu mamy nad nim pełną kontrolę.

---

## 3. Utworzenie workspace'u west z dodatkiem Zigbee

Ponieważ Zigbee jest dodatkiem (`ncs-zigbee`), workspace inicjalizujemy
**z manifestu tego dodatku** — on sam zaciągnie odpowiednią wersję NCS
(jego `west.yml` wskazuje `nrf` w rewizji **v2.9.2**).

W VS Code: panel nRF Connect → kliknij ikonę terminala → **nRF Connect
Terminal** (terminal z westem i toolchainem w PATH — wybierz w nim profil
**Git Bash**). W nim:

```bash
mkdir -p /c/dev/zigbee        # w Git Bash dysk C: to /c
cd /c/dev/zigbee

# Zainicjalizuj workspace z manifestu dodatku Zigbee.
# --mr = rewizja manifestu; wybierz tag dodatku zgodny z NCS v2.9.2
# (sprawdź tagi na https://github.com/nrfconnect/ncs-zigbee/tags)
west init -m https://github.com/nrfconnect/ncs-zigbee

# Pobierz wszystkie repozytoria z manifestu (~kilka GB, kilkanaście minut!)
west update
```

Po zakończeniu struktura wygląda tak (tak samo jak w tym repozytorium):

```
zigbee/                      ← workspace (tzw. "topdir")
├── .west/                   ← konfiguracja westa (wskazuje manifest)
├── ncs-zigbee/              ← dodatek Zigbee: manifest + ZBOSS + przykłady
├── nrf/                     ← nRF Connect SDK
├── nrfxlib/                 ← binarne biblioteki Nordica
├── zephyr/                  ← Zephyr RTOS
├── modules/, bootloader/... ← zależności
├── zigbee_coordinator/      ← ⬅ tu utworzymy nasz projekt
└── zigbee_light_bulb/       ← ⬅ druga instrukcja
```

> **Skąd brać przykłady?** Oficjalne sample Zigbee są w
> `ncs-zigbee/samples/` (m.in. `light_switch`, `light_bulb`,
> `network_coordinator`). Nasz projekt powstał na bazie tych sampli —
> warto mieć je otwarte obok jako ściągę.

> **Uwaga o `ZEPHYR_BASE`.** W `CMakeLists.txt` zobaczysz
> `find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})`. Tej zmiennej **nie
> ustawiasz ręcznie** — robi to west na podstawie `.west/config` (sekcja
> `[zephyr] base = zephyr`) przy każdym budowaniu. Dlatego projekt **musi leżeć
> wewnątrz workspace'u** i być budowany przez rozszerzenie VS Code (które pod
> spodem woła `west`). Skopiowany poza workspace nie zbuduje się —
> `find_package(Zephyr) not found`.

---

## 4. Poznaj swój dongle (PCA10059)

- **Dwa przyciski**: SW1 (duży, na wierzchu — do dyspozycji aplikacji, w
  devicetree alias `sw0`) i **RESET** (mały, z boku, wciskany "w dół" —
  wejście w bootloader DFU).
- **Zielona dioda LED0 + RGB LED1** — do dyspozycji aplikacji (w devicetree
  mają aliasy `led0`, `led1`...).
- **Bootloader** siedzi na końcu flasha i uruchamia aplikację spod adresu
  `0x1000` (pod `0x0` jest MBR). Wciśnięcie RESET **nie restartuje aplikacji**
  — wchodzi w tryb DFU (czerwona dioda pulsuje). Żeby zrestartować firmware,
  wyciągnij i włóż dongle.
- **Nigdy nie flashuj dongla J-Linkiem** — programator kasuje stronę UICR
  z konfiguracją regulatora napięcia i dongle przestaje działać. Tylko DFU.
- **Konsola i shell idą po USB.** Definicja tej płytki kieruje `zephyr,console`
  i `zephyr,shell-uart` na `cdc_acm_uart` (wirtualny COM po USB) — dlatego nie
  musisz nic robić w overlay'u, wystarczy włączyć USB w `prj.conf`.

---

## Etap 0 — pliki stałe projektu i cykl build/flash

W tym etapie tworzysz szkielet projektu. Cztery pliki (`CMakeLists.txt`,
`pm_static.yml`, overlay, `zb_range_extender.h`) **nie zmieniają się już do
końca** — w kolejnych etapach dotykasz tylko `prj.conf` i `src/main.c`.

Docelowa struktura:

```
zigbee_coordinator/
├── CMakeLists.txt
├── prj.conf                 ← rośnie z etapami
├── pm_static.yml
├── boards/
│   └── nrf52840dongle_nrf52840.overlay
└── src/
    ├── main.c               ← rośnie z etapami
    └── zb_range_extender.h
```

### 0.1 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20.0)

# Dodatek ncs-zigbee nie jest częścią Zephyra — mówimy CMake'owi
# żeby doładował go jako dodatkowy moduł (Kconfigi + kod ZBOSS).
list(APPEND EXTRA_ZEPHYR_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/../ncs-zigbee
)

# Standardowy nagłówek każdej aplikacji Zephyr: znajdź SDK i wciągnij
# cały jego system budowania.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zigbee_coordinator)

target_sources(app PRIVATE src/main.c)
```

### 0.2 `boards/nrf52840dongle_nrf52840.overlay`

Plik nazywa się dokładnie jak target płytki (z `/` zamienionym na `_`)
i jest dobierany automatycznie przy budowaniu na tę płytkę.

```dts
/ {
    chosen {
        ncs,zigbee-timer = &timer2;
    };
};

&timer2 {
    status = "okay";
};
```

ZBOSS potrzebuje dedykowanego sprzętowego timera. Węzeł `chosen` to "globalne
wskazania" devicetree — mówimy "timerem Zigbee jest TIMER2" i włączamy TIMER2
(`status = "okay"`).

### 0.3 `pm_static.yml` — mapa flasha

```yaml
app:
  address: 0x1000
  end_address: 0xD7000
  region: flash_primary
  size: 0xD6000

zboss_nvram:
  address: 0xD7000
  end_address: 0xDF000
  region: flash_primary
  size: 0x8000

zboss_product_config:
  address: 0xDF000
  end_address: 0xE0000
  region: flash_primary
  size: 0x1000

bootloader:
  address: 0xE0000
  end_address: 0xFE000
  region: flash_primary
  size: 0x1E000

mbr_params_page:
  address: 0xFE000
  end_address: 0xFF000
  region: flash_primary
  size: 0x1000

bootloader_settings_page:
  address: 0xFF000
  end_address: 0x100000
  region: flash_primary
  size: 0x1000
```

- **`app`** — aplikacja, od `0x1000` (pod `0x0` jest MBR).
- **`zboss_nvram`** — 32 KB, tu ZBOSS trwale zapisuje stan sieci (PAN ID,
  klucze, bindingi). Dzięki temu sieć przeżywa restart. **Bez tej partycji
  `zigbee_enable()` crashuje.**
- **`zboss_product_config`** — strona na konfigurację produkcyjną ZBOSS.
- **`bootloader` + strony MBR** — obszar bootloadera; deklarujemy go, żeby
  Partition Manager niczego tam nie położył.

> **Uwaga:** w tym projekcie bootloader siedzi pod `0xE0000`. Świeży dongle
> ze sklepu ma oryginalny Open Bootloader pod **`0xF8000`** — sprawdź swój
> egzemplarz i w razie czego przesuń granice partycji.

### 0.4 `src/zb_range_extender.h` — definicja "urządzenia ZCL" Coordinatora

Coordinator też musi się przedstawiać w sieci jako urządzenie ZCL — mieć
endpoint(y) i clustery. Bierzemy najprostszy szablon z ZBOSS ("Range
Extender": Basic + Identify) i **dodajemy On/Off w roli CLIENT**, żeby móc
wysyłać Toggle (wykorzystamy to od Etapu 5, ale cluster deklarujemy od razu).
Skopiuj plik z tego projektu: [src/zb_range_extender.h](src/zb_range_extender.h).
Kluczowe fragmenty:

```c
#define ZB_RANGE_EXTENDER_IN_CLUSTER_NUM  2   /* Basic + Identify (server) */
#define ZB_RANGE_EXTENDER_OUT_CLUSTER_NUM 1   /* On/Off (client!) */

/* ... w liście clusterów: */
ZB_ZCL_CLUSTER_DESC(
        ZB_ZCL_CLUSTER_ID_ON_OFF,
        0,                              /* client nie ma atrybutów */
        NULL,
        ZB_ZCL_CLUSTER_CLIENT_ROLE,     /* ← my WYSYŁAMY komendy */
        ZB_ZCL_MANUF_CODE_INVALID
)
```

Ważna zmiana względem oryginału z ZBOSS: makro
`ZB_ZCL_DECLARE_RANGE_EXTENDER_SIMPLE_DESC` **nie wywołuje**
`ZB_DECLARE_SIMPLE_DESC`. Dzięki temu w Etapie 7 możemy zadeklarować kilka
endpointów o tym samym kształcie, a typedef deskryptora utworzyć samodzielnie
tylko raz (patrz pułapka nr 7).

### 0.5 Cykl "zbuduj i wgraj" — powtarzasz go po każdym etapie

**Zbuduj (raz skonfiguruj, potem tylko Build):**

1. VS Code → **File → Open Folder** → `zigbee_coordinator`.
2. Panel **nRF Connect** → *Applications* → **Add build configuration** →
   Board target `nrf52840dongle/nrf52840` → **Build Configuration**.
3. Wynik: `build/zigbee_coordinator/zephyr/zephyr.hex`.

**Wgraj przez DFU:**

1. Wciśnij boczny **RESET** — czerwona dioda pulsuje (tryb DFU).
2. Sprawdź port: `nrfutil device list`.
3. Zbuduj pakiet i wgraj (`COM5` → twój port):

   ```bash
   nrfutil nrf5sdk-tools pkg generate \
       --hw-version 52 --sd-req 0x00 \
       --application build/zigbee_coordinator/zephyr/zephyr.hex \
       --application-version 1 \
       coordinator.zip

   nrfutil nrf5sdk-tools dfu usb-serial -pkg coordinator.zip -p COM5
   ```
4. Po DFU dongle restartuje się do aplikacji.

> **KRYTYCZNA pułapka:** `--application-version` musi **rosnąć przy każdym
> flashu**. Bootloader odrzuca pakiet z tą samą/niższą wersją **bez żadnego
> komunikatu** — DFU "przechodzi", a na donglu zostaje stary firmware. Przy
> pracy etapami zwiększaj wersję co etap (1, 2, 3, ...) albo skacz po 10.

---

## Etap 1 — odpalenie ZBOSS

**Cel:** firmware startuje, ZBOSS się uruchamia i nie crashuje. Jeszcze nie
tworzymy sieci ani nie logujemy — jedynym sygnałem życia jest migająca dioda.

**`prj.conf` (minimalny):**

```conf
CONFIG_NCS_SAMPLES_DEFAULTS=y

# UART wymagany przez libzboss.a (nawet bez logów!)
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_GPIO=y

# Zigbee
CONFIG_ZIGBEE_ADD_ON=y
CONFIG_ZIGBEE_APP_UTILS=y
CONFIG_ZIGBEE_ROLE_COORDINATOR=y

CONFIG_HEAP_MEM_POOL_SIZE=2048

# Wyłącz stos IP Zephyra — niepotrzebny
CONFIG_NET_IPV6=n
CONFIG_NET_IP_ADDR_CHECK=n
CONFIG_NET_UDP=n
```

**`src/main.c`:**

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zboss_api.h>
#include <zb_mem_config_max.h>          /* konfiguracja pamięci ZBOSS — WYMAGANE */
#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>    /* zigbee_enable(), default handler */
#include <zb_nrf_platform.h>
#include "zb_range_extender.h"

#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Minimalny kontekst urządzenia ZCL: 1 endpoint (EP 10), Basic + Identify.
 * ZBOSS wymaga zarejestrowanego kontekstu PRZED zigbee_enable(), inaczej crash. */
struct zb_device_ctx {
    zb_zcl_basic_attrs_t    basic_attr;
    zb_zcl_identify_attrs_t identify_attr;
};
static struct zb_device_ctx dev_ctx;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list,
    &dev_ctx.identify_attr.identify_time);
ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(basic_attr_list,
    &dev_ctx.basic_attr.zcl_version, &dev_ctx.basic_attr.power_source);

ZB_DECLARE_SIMPLE_DESC(2, 1);   /* raz, z literałami: 2 clustery IN + 1 OUT */
ZB_DECLARE_RANGE_EXTENDER_CLUSTER_LIST(coord_ep1_clusters,
    basic_attr_list, identify_attr_list);
ZB_DECLARE_RANGE_EXTENDER_EP(coord_ep1, 10, coord_ep1_clusters);
ZBOSS_DECLARE_DEVICE_CTX_1_EP(coordinator_ctx, coord_ep1);

static void app_clusters_attr_init(void)
{
    dev_ctx.basic_attr.zcl_version  = ZB_ZCL_VERSION;
    dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE;
    dev_ctx.identify_attr.identify_time =
        ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

/* ZBOSS woła to przy każdym zdarzeniu. Na razie tylko domyślna obsługa. */
void zboss_signal_handler(zb_bufid_t bufid)
{
    ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
    if (bufid) zb_buf_free(bufid);      /* ZAWSZE zwolnij bufor */
}

int main(void)
{
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    ZB_AF_REGISTER_DEVICE_CTX(&coordinator_ctx);   /* przed zigbee_enable()! */
    app_clusters_attr_init();

    zigbee_enable();                               /* start wątku ZBOSS */

    while (1) {
        gpio_pin_toggle_dt(&led);                  /* miganie = "żyję" */
        k_sleep(K_MSEC(500));
    }
    return 0;
}
```

**Sprawdź:** po flashu zielona dioda miga co pół sekundy. Jeśli **nie miga**
(dioda zgasła/świeci na stałe od startu), ZBOSS się wywalił — sprawdź cztery
najczęstsze przyczyny z pułapki nr 1 (brak NVRAM w `pm_static.yml`, brak
`CONFIG_SERIAL`, brak `#include <zb_mem_config_max.h>`, rejestracja kontekstu
po `zigbee_enable()`).

---

## Etap 2 — utworzenie sieci sygnalizowane diodą

**Cel:** Coordinator faktycznie tworzy sieć Zigbee, a dioda to potwierdza —
**bez podłączania do komputera**. Miganie = "czekam / tworzę sieć", światło
ciągłe = "sieć gotowa".

Teraz obsłużymy sygnały BDB. Przy pierwszym starcie ZBOSS zgłasza
`DEVICE_FIRST_START` (świeży NVRAM); my w odpowiedzi uruchamiamy **formation**
(utwórz sieć), po nim **steering** (otwórz na dołączanie). Przy kolejnych
startach ZBOSS odtwarza sieć z NVRAM i dostajemy od razu `DEVICE_REBOOT`.

**`prj.conf`:** bez zmian.

**`src/main.c` — zmiana tylko w signal handlerze i pętli `main`:**

```c
static volatile bool network_up = false;

void zboss_signal_handler(zb_bufid_t bufid)
{
    zb_zdo_app_signal_hdr_t  *sg_p = NULL;
    zb_zdo_app_signal_type_t  sig  = zb_get_app_signal(bufid, &sg_p);
    zb_ret_t                  status = ZB_GET_APP_SIGNAL_STATUS(bufid);

    switch (sig) {
    case ZB_BDB_SIGNAL_DEVICE_FIRST_START:      /* świeży NVRAM → utwórz sieć */
        if (status == RET_OK)
            bdb_start_top_level_commissioning(ZB_BDB_NETWORK_FORMATION);
        break;

    case ZB_BDB_SIGNAL_FORMATION:               /* sieć utworzona → otwórz na join */
        if (status == RET_OK)
            bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
        break;

    case ZB_BDB_SIGNAL_DEVICE_REBOOT:           /* sieć odtworzona z NVRAM */
        if (status == RET_OK)
            bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
        break;

    case ZB_BDB_SIGNAL_STEERING:                /* permit join aktywny (180 s) */
        if (status == RET_OK)
            network_up = true;                  /* ← dioda to pokaże */
        break;

    default:
        ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
        break;
    }

    if (bufid) zb_buf_free(bufid);
}

int main(void)
{
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    ZB_AF_REGISTER_DEVICE_CTX(&coordinator_ctx);
    app_clusters_attr_init();
    zigbee_enable();

    while (1) {
        if (network_up) {
            gpio_pin_set_dt(&led, 1);           /* sieć gotowa → światło ciągłe */
            k_sleep(K_MSEC(1000));
        } else {
            gpio_pin_toggle_dt(&led);           /* czekamy → szybkie miganie */
            k_sleep(K_MSEC(150));
        }
    }
    return 0;
}
```

**Sprawdź:** po flashu dioda przez chwilę szybko miga (tworzenie sieci),
potem przechodzi w **światło ciągłe** — to znaczy, że sieć została utworzona
i jest otwarta na dołączanie. Nadal bez terminala na komputerze.

> **Pojęcie:** `bdb_start_top_level_commissioning()` to "wykonaj procedurę BDB".
> Dla Coordinatora sekwencja to zawsze `FIRST_START → FORMATION → STEERING`.
> ZBOSS informuje o zakończeniu każdego kroku osobnym sygnałem — dlatego
> steering odpalamy dopiero w odpowiedzi na sygnał `FORMATION`, a nie od razu.

---

## Etap 3 — logi przez USB (nRF Connect Serial Terminal)

**Cel:** zobaczyć, co robi Coordinator, na ekranie komputera: PAN ID, kanał,
kolejne sygnały. Do tego włączamy USB CDC ACM (wirtualny COM) i logging.
Płytka domyślnie kieruje konsolę na ten port, więc nic w overlay'u nie trzeba.

**`prj.conf` — dodaj:**

```conf
# Logi przez wirtualny COM po USB
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_CDC_ACM=y

CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
```

**`src/main.c` — dodaj nagłówek loggera, rejestrację modułu i wywołania `LOG_INF`:**

```c
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);   /* obok pozostałych #include */
```

W `main()` na początku:

```c
LOG_INF("Starting Zigbee Coordinator");
```

W signal handlerze uzupełnij logi:

```c
case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    if (status == RET_OK) {
        LOG_INF("First start — forming new network");
        bdb_start_top_level_commissioning(ZB_BDB_NETWORK_FORMATION);
    }
    break;

case ZB_BDB_SIGNAL_FORMATION:
    if (status == RET_OK) {
        LOG_INF("Network formed — starting steering");
        bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
    }
    break;

case ZB_BDB_SIGNAL_STEERING:
    if (status == RET_OK) {
        network_up = true;
        LOG_INF("Network steering started");
        LOG_INF("PAN ID: 0x%04x, channel: %d",
            zb_get_pan_id(), zb_get_current_channel());
    }
    break;
```

**Sprawdź:**

1. Otwórz **nRF Connect for Desktop → Serial Terminal**, wybierz port COM
   dongla (pojawia się nowy po starcie aplikacji), 115200 8N1.
2. Zobaczysz:

   ```
   I: Starting Zigbee Coordinator
   I: First start — forming new network
   I: Network formed — starting steering
   I: Network steering started
   I: PAN ID: 0x3459, channel: 16
   ```

Jeśli port się pojawia, ale logów brak — sprawdź, czy `LOG_MODULE_REGISTER`
jest dokładnie raz i czy `CONFIG_LOG=y`.

---

## Etap 4 — wykrycie dołączenia urządzenia

**Cel:** gdy do sieci dołączy inne urządzenie (Twój drugi dongle z firmware
Light Bulb — patrz jego instrukcja), Coordinator to zauważa i loguje jego
adresy IEEE + short.

W Zigbee 3.0 nowe urządzenie ogłasza się sygnałem `DEVICE_ANNCE`, ale bywa on
niedostarczany do aplikacji — dlatego obsługujemy też `DEVICE_AUTHORIZED`
(przychodzi po wymianie kluczy z Trust Center). Dodajemy również
`zb_bdb_set_legacy_device_support(1)`, żeby dopuścić starsze urządzenia.

**`prj.conf` — dodaj:**

```conf
CONFIG_ZIGBEE_TC_REJOIN_ENABLED=y
```

**`src/main.c` — funkcja pomocnicza + dwa nowe `case` w handlerze:**

```c
static void handle_device_joined(zb_uint16_t short_addr, const zb_ieee_addr_t ieee)
{
    LOG_INF("=====================================================");
    LOG_INF("New device joined: 0x%04x", short_addr);
    LOG_INF("  IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
        ieee[7], ieee[6], ieee[5], ieee[4],
        ieee[3], ieee[2], ieee[1], ieee[0]);
    LOG_INF("=====================================================");
}
```

W `ZB_BDB_SIGNAL_STEERING` dodaj przed logami:

```c
zb_bdb_set_legacy_device_support(1);   /* dopuść starsze urządzenia */
```

Nowe `case` (przed `default:`):

```c
case ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
    zb_zdo_signal_device_annce_params_t *a =
        ZB_ZDO_SIGNAL_GET_PARAMS(sg_p, zb_zdo_signal_device_annce_params_t);
    handle_device_joined(a->device_short_addr, a->ieee_addr);
} break;

case ZB_ZDO_SIGNAL_DEVICE_AUTHORIZED: {
    zb_zdo_signal_device_authorized_params_t *auth =
        ZB_ZDO_SIGNAL_GET_PARAMS(sg_p, zb_zdo_signal_device_authorized_params_t);
    if (auth->authorization_status == ZB_ZDO_TCLK_AUTHORIZATION_SUCCESS ||
        auth->authorization_status == ZB_ZDO_LEGACY_DEVICE_AUTHORIZATION_SUCCESS) {
        handle_device_joined(auth->short_addr, auth->long_addr);
    }
} break;
```

**Sprawdź:** zbuduj i wgraj drugi dongle wg
`../zigbee_light_bulb/INSTRUKCJA_LIGHT_BULB.md`, włącz go. W terminalu
Coordinatora pojawi się:

```
I: =====================================================
I: New device joined: 0x1234
I:   IEEE: f4:ce:36:a1:20:bb:33:9b
I: =====================================================
```

> Jeśli od startu Coordinatora minęło ponad 180 s, sieć zdążyła się zamknąć
> (permit join wygasa). Na razie wystarczy zrestartować Coordinator
> (odłącz/podłącz USB). Wygodną komendę `open` do ponownego otwarcia dodamy
> w Etapie 6.

---

## Etap 5 — sterowanie urządzeniem przyciskiem (Toggle)

**Cel:** wciśnięcie przycisku **SW1** na Coordinatorze przełącza diodę na
żarówce. To najbogatszy etap: żeby wysłać Toggle, musimy najpierw **odkryć**
(ZDO discovery), co dołączone urządzenie potrafi, i utworzyć **binding**.

Rozszerzamy `handle_device_joined` z Etapu 4: zamiast tylko logować, zapamiętuje
urządzenie i uruchamia discovery. Dla przejrzystości obsługujemy **jedno**
urządzenie (rozszerzenie na wiele — w rozdziale "Pełny projekt").

**`prj.conf`:** bez zmian (GPIO już mamy).

**Przepływ discovery + bind** (łańcuch asynchroniczny — każdy krok: pobierz
bufor → wyślij zapytanie ZDO → callback z odpowiedzią):

```
Active_EP_req   → "jakie masz endpointy?"        → active_ep_cb   (np. [20])
Simple_Desc_req → "co jest na EP 20?"            → simple_desc_cb (szukamy On/Off 0x0006)
Bind_req        → "bind: mój EP 10, On/Off → EP 20" → bind_cb     (zapamiętaj remote_ep)
```

**`src/main.c` — dodaj rejestr urządzenia, discovery, bind, wysyłkę i przycisk.**
Poniżej najważniejsze fragmenty; pełną, dopracowaną wersję masz w
[src/main.c](src/main.c).

```c
#define COORD_EP  10   /* nasz endpoint On/Off client */

static struct {
    bool           used, bound;
    zb_ieee_addr_t ieee;
    zb_uint16_t    short_addr;
    zb_uint8_t     remote_ep;   /* endpoint On/Off na urządzeniu (np. 20) */
} dev;

/* --- lista endpointów odkrywanego urządzenia --- */
static struct { zb_uint8_t eps[16], count, idx; } disc;

static void send_simple_desc_req(zb_bufid_t bufid);

/* Krok 3: utwórz binding mój EP 10 → EP urządzenia */
static void bind_cb(zb_bufid_t bufid)
{
    zb_zdo_bind_resp_t *r = (zb_zdo_bind_resp_t *)zb_buf_begin(bufid);
    if (r->status == ZB_ZDP_STATUS_SUCCESS) {
        dev.bound = true;
        dev.remote_ep = disc.eps[disc.idx - 1];
        LOG_INF("Bind OK: On/Off → EP %d", dev.remote_ep);
    } else {
        LOG_ERR("Bind failed: %d", r->status);
    }
    zb_buf_free(bufid);
}

static void do_bind(zb_bufid_t bufid)
{
    zb_zdo_bind_req_param_t *req = ZB_BUF_GET_PARAM(bufid, zb_zdo_bind_req_param_t);
    zb_ieee_addr_t my_ieee;
    zb_get_long_address(my_ieee);
    ZB_MEMCPY(req->src_address, my_ieee, sizeof(zb_ieee_addr_t));
    req->src_endp      = COORD_EP;
    req->cluster_id    = ZB_ZCL_CLUSTER_ID_ON_OFF;
    req->dst_addr_mode = ZB_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    ZB_MEMCPY(&req->dst_address.addr_long, dev.ieee, sizeof(zb_ieee_addr_t));
    req->dst_endp     = disc.eps[disc.idx - 1];
    req->req_dst_addr = zb_get_short_address();   /* bind powstaje u NAS */
    zb_zdo_bind_req(bufid, bind_cb);
}

/* Krok 2: sprawdź, czy na tym EP jest On/Off server */
static void simple_desc_cb(zb_bufid_t bufid)
{
    zb_zdo_simple_desc_resp_t *r = (zb_zdo_simple_desc_resp_t *)zb_buf_begin(bufid);
    bool found = false;
    if (r->hdr.status == ZB_ZDP_STATUS_SUCCESS) {
        for (zb_uint8_t i = 0; i < r->simple_desc.app_input_cluster_count; i++)
            if (r->simple_desc.app_cluster_list[i] == ZB_ZCL_CLUSTER_ID_ON_OFF)
                found = true;
    }
    zb_buf_free(bufid);

    if (found)                       zb_buf_get_out_delayed(do_bind);
    else if (disc.idx < disc.count)  zb_buf_get_out_delayed(send_simple_desc_req);
    else                             LOG_WRN("No On/Off server found");
}

static void send_simple_desc_req(zb_bufid_t bufid)
{
    zb_zdo_simple_desc_req_t *req = (zb_zdo_simple_desc_req_t *)
        zb_buf_initial_alloc(bufid, sizeof(zb_zdo_simple_desc_req_t));
    req->nwk_addr = dev.short_addr;
    req->endpoint = disc.eps[disc.idx++];
    zb_zdo_simple_desc_req(bufid, simple_desc_cb);
}

/* Krok 1: pobierz listę endpointów */
static void active_ep_cb(zb_bufid_t bufid)
{
    zb_zdo_ep_resp_t *r = (zb_zdo_ep_resp_t *)zb_buf_begin(bufid);
    zb_uint8_t *list = (zb_uint8_t *)(r + 1);
    disc.count = MIN(r->ep_count, ARRAY_SIZE(disc.eps));
    disc.idx = 0;
    for (zb_uint8_t i = 0; i < disc.count; i++) disc.eps[i] = list[i];
    zb_buf_free(bufid);
    if (disc.count) zb_buf_get_out_delayed(send_simple_desc_req);
}

static void send_active_ep_req(zb_bufid_t bufid)
{
    zb_zdo_active_ep_req_t *req = (zb_zdo_active_ep_req_t *)
        zb_buf_initial_alloc(bufid, sizeof(zb_zdo_active_ep_req_t));
    req->nwk_addr = dev.short_addr;
    zb_zdo_active_ep_req(bufid, active_ep_cb);
}
```

`handle_device_joined` z Etapu 4 zapamiętuje urządzenie i startuje discovery:

```c
static void handle_device_joined(zb_uint16_t short_addr, const zb_ieee_addr_t ieee)
{
    if (dev.used) return;                 /* obsługujemy jedno urządzenie */
    dev.used = true;
    dev.bound = false;
    dev.short_addr = short_addr;
    ZB_MEMCPY(dev.ieee, ieee, sizeof(zb_ieee_addr_t));
    LOG_INF("New device 0x%04x — starting discovery", short_addr);
    zb_buf_get_out_delayed(send_active_ep_req);
}
```

**Wysyłka Toggle i przycisk SW1:**

```c
#define SW_NODE DT_ALIAS(sw0)             /* SW1 na donglu */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW_NODE, gpios);
static struct gpio_callback button_cb;

static void send_toggle_cmd(zb_bufid_t bufid)   /* już w wątku ZBOSS */
{
    if (!dev.bound) { zb_buf_free(bufid); return; }
    LOG_INF("Sending Toggle to 0x%04x EP %d", dev.short_addr, dev.remote_ep);
    ZB_ZCL_ON_OFF_SEND_TOGGLE_REQ(bufid, dev.short_addr,
        ZB_APS_ADDR_MODE_16_ENDP_PRESENT,   /* adresowanie jawne: short + EP */
        dev.remote_ep, COORD_EP,
        ZB_AF_HA_PROFILE_ID, ZB_ZCL_DISABLE_DEFAULT_RESPONSE, NULL);
}

static void button_pressed(const struct device *port,
                           struct gpio_callback *cb, uint32_t pins)
{
    /* To jest ISR — NIE wolno tu wołać API ZBOSS bezpośrednio.
     * Delegujemy do wątku ZBOSS przez pobranie bufora. */
    zb_buf_get_out_delayed(send_toggle_cmd);
}
```

W `main()` skonfiguruj przycisk (po `gpio` LED, przed pętlą):

```c
gpio_pin_configure_dt(&button, GPIO_INPUT);
gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
gpio_add_callback(button.port, &button_cb);
```

**Sprawdź:** po dołączeniu żarówki zobaczysz w logach discovery i bind:

```
I: New device 0x1234 — starting discovery
I: Bind OK: On/Off → EP 20
```

Teraz **wciśnij SW1** — dioda na żarówce przełącza się, a w logach:

```
I: Sending Toggle to 0x1234 EP 20
```

> **Dlaczego adresujemy jawnie (short + EP), skoro tworzymy binding?** Tryb
> "wyślij wg tablicy bindingów" potrafi czekać ~10 s na rozwiązanie adresu
> starych, nieosiągalnych wpisów. Jawny adres jest natychmiastowy. Binding
> i tak tworzymy — trwale zapisuje relację w NVRAM i przeżywa restart.

> **Uwaga:** prosty przycisk bez debounce może czasem wysłać dwa Toggle za
> jednym wciśnięciem. W nauce to nie przeszkadza; produkcyjnie dodałbyś
> odfiltrowanie drgań styków (np. `k_work_delayable` z krótkim oknem).

---

## Etap 6 — sterowanie komendą shell

**Cel:** przełączać żarówkę komendą wpisaną w terminalu (`toggle`), bez
sięgania po przycisk. Dorzucamy też `open` — ręczne ponowne otwarcie sieci na
dołączanie. Shell działa na tym samym porcie USB co logi (płytka kieruje tam
`zephyr,shell-uart`).

**`prj.conf` — dodaj:**

```conf
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_PROMPT_UART="zigbee:~$ "
```

**`src/main.c` — dodaj nagłówek shella i komendy:**

```c
#include <zephyr/shell/shell.h>

static int cmd_toggle(const struct shell *sh, size_t argc, char **argv)
{
    if (!dev.bound) {
        shell_error(sh, "Brak zbindowanego urządzenia");
        return -EAGAIN;
    }
    zb_buf_get_out_delayed(send_toggle_cmd);   /* przeskok do wątku ZBOSS */
    shell_print(sh, "Toggle wysłany");
    return 0;
}

/* bdb_start_top_level_commissioning musi być wołane z wątku ZBOSS */
static void do_open_network(zb_uint8_t param)
{
    ARG_UNUSED(param);
    zb_bdb_set_legacy_device_support(1);
    bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
}

static int cmd_open(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    ZB_SCHEDULE_APP_CALLBACK(do_open_network, 0);
    shell_print(sh, "Sieć otwarta na dołączanie (180 s)");
    return 0;
}

SHELL_CMD_REGISTER(toggle, NULL, "Wyślij Toggle do urządzenia", cmd_toggle);
SHELL_CMD_REGISTER(open,   NULL, "Otwórz sieć na dołączanie (180 s)", cmd_open);
```

**Sprawdź:** w Serial Terminal przełącz tryb na **Shell** (albo pisz w linii
poleceń), zobaczysz prompt `zigbee:~$`. Wpisz:

```
zigbee:~$ toggle
Toggle wysłany
I: Sending Toggle to 0x1234 EP 20
```

Dioda żarówki się przełącza. Komenda `help` pokaże wbudowaną listę komend.
Przycisk SW1 z Etapu 5 nadal działa — masz dwa sposoby sterowania.

> **Pojęcie:** `SHELL_CMD_REGISTER(nazwa, ...)` rejestruje komendę w czasie
> kompilacji, a handler dostaje `argc`/`argv` jak zwykły `main()`. `open`
> tylko zleca pracę do wątku ZBOSS przez `ZB_SCHEDULE_APP_CALLBACK` — bo
> jesteśmy w wątku shella, a `bdb_start_top_level_commissioning` wolno wołać
> tylko z wątku ZBOSS.

---

## Etap 7 — nadawanie nazw urządzeniom

**Cel:** zamiast pamiętać, że "0x1234 to lampa", nadajesz urządzeniu nazwę
i sterujesz nim po nazwie: `toggle lampa`. Nazwę wpisujesz **zaraz po
dołączeniu**, zanim ruszy discovery.

Problem techniczny: chwilę po dołączeniu urządzenia chcemy poczekać na wpisanie
nazwy, ale signal handler działa w wątku ZBOSS, gdzie **nie wolno blokować**.
Rozwiązanie to klasyka Zephyra — oddelegowanie czekania do **systemowej work
queue** i synchronizacja **semaforem** z wątkiem shella:

```
wątek ZBOSS:  urządzenie dołączyło → zapisz adresy → k_work_submit()
work queue:   wypisz prompt → k_sem_take(&name_sem, K_FOREVER)   ← blokuje się TU
wątek shell:  user wpisuje "name lampa" → cmd_name() → k_sem_give()
work queue:   ...odblokowana → zapisz nazwę → start discovery (jak w Etapie 5)
```

**`prj.conf` — dodaj (work queue potrzebuje trochę więcej stosu):**

```conf
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
```

**`src/main.c` — dodaj pole nazwy, semafor, worker i komendę `name`:**

```c
#define MAX_NAME_LEN 32
/* do struktury dev dodaj: char name[MAX_NAME_LEN]; */

static K_SEM_DEFINE(name_sem, 0, 1);
static char pending_name[MAX_NAME_LEN];

static void name_wait_work_handler(struct k_work *work);
static K_WORK_DEFINE(name_wait_work, name_wait_work_handler);

static void name_wait_work_handler(struct k_work *work)
{
    LOG_INF("New device 0x%04x — wpisz: name <nazwa>", dev.short_addr);

    k_sem_take(&name_sem, K_FOREVER);        /* czekaj na shell (osobny wątek) */

    strncpy(dev.name, pending_name, MAX_NAME_LEN - 1);
    dev.name[MAX_NAME_LEN - 1] = '\0';
    LOG_INF("Nazwa: %s → start discovery", dev.name);

    zb_buf_get_out_delayed(send_active_ep_req);   /* discovery jak w Etapie 5 */
}
```

`handle_device_joined` **już nie startuje discovery od razu** — deleguje
czekanie na nazwę:

```c
static void handle_device_joined(zb_uint16_t short_addr, const zb_ieee_addr_t ieee)
{
    if (dev.used) return;
    dev.used = true;
    dev.bound = false;
    dev.short_addr = short_addr;
    ZB_MEMCPY(dev.ieee, ieee, sizeof(zb_ieee_addr_t));
    k_work_submit(&name_wait_work);          /* nie blokuj wątku ZBOSS! */
}
```

Komenda `name` zwalnia semafor, a `toggle` przyjmuje teraz nazwę:

```c
static int cmd_name(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) { shell_error(sh, "Użycie: name <nazwa>"); return -EINVAL; }
    strncpy(pending_name, argv[1], MAX_NAME_LEN - 1);
    pending_name[MAX_NAME_LEN - 1] = '\0';
    k_sem_give(&name_sem);                    /* odblokuj worker */
    shell_print(sh, "Nazwa '%s' przypisana", pending_name);
    return 0;
}

static int cmd_toggle(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) { shell_error(sh, "Użycie: toggle <nazwa>"); return -EINVAL; }
    if (!dev.used || strcmp(dev.name, argv[1]) != 0) {
        shell_error(sh, "Nie znam '%s'", argv[1]); return -ENOENT;
    }
    if (!dev.bound) { shell_error(sh, "Jeszcze nie zbindowane"); return -EAGAIN; }
    zb_buf_get_out_delayed(send_toggle_cmd);
    shell_print(sh, "Toggle → %s", dev.name);
    return 0;
}

SHELL_CMD_REGISTER(name, NULL, "Nadaj nazwę dołączonemu urządzeniu", cmd_name);
```

**Sprawdź:** włącz żarówkę; w terminalu Coordinatora:

```
I: New device 0x1234 — wpisz: name <nazwa>
zigbee:~$ name lampa
Nazwa 'lampa' przypisana
I: Nazwa: lampa → start discovery
I: Bind OK: On/Off → EP 20
zigbee:~$ toggle lampa
Toggle → lampa
I: Sending Toggle to 0x1234 EP 20
```

> **Uwaga:** nazwy są tylko w RAM — po restarcie Coordinatora nadajesz je
> ponownie. Binding jest w NVRAM, więc przycisk SW1 działa nawet bez nazwy.

Masz komplet siedmiu etapów. Coordinator tworzy sieć, przyjmuje urządzenie,
pozwala je nazwać i steruje nim przyciskiem oraz komendą shell.

---

## Pełny projekt — rozszerzenie na wiele urządzeń

Wersja w [src/main.c](src/main.c) idzie dalej niż te 7 etapów:

- **Trzy endpointy On/Off client** (EP 10/11/12) zamiast jednego — po jednym
  na urządzenie, żeby każde miało własny binding. Tu właśnie
  `ZB_DECLARE_SIMPLE_DESC(2,1)` trzeba wywołać **raz z literałami** przed
  deklaracjami endpointów, inaczej typedef deskryptora powstaje wielokrotnie
  → błąd kompilacji (pułapka nr 7). Kontekst budujesz makrem
  `ZBOSS_DECLARE_DEVICE_CTX_3_EP`.
- **Rejestr `devices[MAX_DEVICES]`** zamiast pojedynczego `dev` — z mapowaniem
  "kolejność joinu → nazwa + endpoint Coordinatora".
- **Komenda `devices`** listująca urządzenia i ich status bind.
- **Obsługa rejoinu** — to samo urządzenie (po IEEE) po restarcie aktualizuje
  tylko short address zamiast zajmować nowy slot.

Zasada rozbudowy jest ta sama co w etapach: dokładasz jeden mechanizm, budujesz,
sprawdzasz.

---

## Pułapki — ucz się na naszych błędach

1. **`zigbee_enable()` crashuje** — cztery typowe przyczyny: brak partycji
   NVRAM w `pm_static.yml`, brak `CONFIG_SERIAL=y`, brak
   `#include <zb_mem_config_max.h>`, brak `ZB_AF_REGISTER_DEVICE_CTX()` przed
   `zigbee_enable()`. (Objawia się już w Etapie 1 — dioda nie miga.)
2. **J-Link niszczy dongle** — kasuje UICR (konfiguracja regulatora
   napięcia). Flashuj wyłącznie przez DFU.
3. **RESET ≠ restart** — boczny przycisk wchodzi w bootloader DFU. Restart
   aplikacji = odłącz/podłącz USB.
4. **`--application-version` musi rosnąć** — inaczej cichy brak flasha i
   debugujesz stary firmware.
5. **`zb_buf_free(bufid)` zawsze** na końcu signal handlera i callbacków —
   wyciek buforów zabija stos po kilku zdarzeniach.
6. **ZBOSS API tylko z wątku ZBOSS** — z shella/ISR/work queue używaj
   `ZB_SCHEDULE_APP_CALLBACK()` lub `zb_buf_get_out_delayed()`. Dotyczy m.in.
   przycisku (Etap 5) i komendy `open` (Etap 6).
7. **`ZB_DECLARE_SIMPLE_DESC` raz** przy wielu endpointach o tym samym
   kształcie clusterów (redefinicja typedef) — patrz "Pełny projekt".
8. **`zb_bdb_reset_via_local_action()` nie działa w `main()`** — ZBOSS
   jeszcze nie działa. Planuj przez `ZB_SCHEDULE_APP_CALLBACK` z signal
   handlera. Po resecie NVRAM dostaniesz `FIRST_START` → formation → steering.
9. **`DEVICE_ANNCE` może nie przyjść** w Zigbee 3.0 — obsługuj też
   `DEVICE_AUTHORIZED` (Etap 4).
10. **Resetujesz NVRAM Coordinatora → resetuj też urządzenia** — inaczej będą
    szukać starej sieci (stary PAN ID) i nigdy nie dołączą do nowej.
11. **`zb_bdb_set_legacy_device_support(1)` w `STEERING`**, nie w `main()`
    (przed `zigbee_enable()` nie ma efektu).
12. **Komentarze w `prj.conf` w osobnych liniach** — `CONFIG_X=y # tekst`
    psuje parsowanie wartości.
13. **Nie wymyślaj Kconfigów** — weryfikuj w plikach `Kconfig*` dodatku
    `ncs-zigbee`.

---

## Co dalej

Zbuduj drugie urządzenie — Light Bulb (Router z On/Off server), które jest
odbiorcą Toggle i którego potrzebujesz już od Etapu 4:
**`../zigbee_light_bulb/INSTRUKCJA_LIGHT_BULB.md`**.
