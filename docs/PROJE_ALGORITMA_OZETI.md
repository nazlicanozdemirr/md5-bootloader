# Proje Algoritması Özeti

## 1. Projenin Genel Algoritması

Bu proje **STM32F446RE** için **UART tabanlı, MD5 doğrulamalı bootloader** ve **Python host script** ile firmware yükleme sistemidir.

### 1.1 Bellek haritası
- **0x08000000** – Bootloader (Sector 0, 16 KB)
- **0x08004000** – Metadata (Sector 1, 16 KB): yüklü firmware’in MD5’i, boyutu, geçerlilik bayrağı
- **0x08008000** – Uygulama (Sector 2–7, ~480 KB)

### 1.2 Genel akış

1. **Cihaz açılır / resetlenir**  
   Bootloader çalışır; UART’ta **0xAA** (sync) bekler. Yaklaşık 150 ms’lik pencerelerle dinler; ~30 saniye boyunca geçerli paket gelmezse uygulama varsa oraya atlar.

2. **Host script (.hex/.bin) çalıştırılır**  
   - Seri port açılır (COM10, 115200 baud).  
   - **CMD_CHECK_MD5** paketi gönderilir: **20 byte** = 16 byte MD5 + 4 byte firmware boyutu (little-endian).  
   - Bootloader bu boyutu kullanarak flash’ın ilgili kısmının MD5’ini hesaplar; host’un gönderdiği MD5 ile karşılaştırır.

3. **İki sonuç**  
   - **MD5 eşleşirse (ACK_SAME):** “Firmware zaten yüklü” denir, flash yazılmaz, bootloader doğrudan uygulamaya atlar.  
   - **MD5 farklıysa (ACK_DIFF):** Bootloader 20 byte (kendi hesapladığı MD5 + boyut) gönderir; host flash yükleme moduna geçer.

4. **Flash yükleme (ACK_DIFF sonrası)**  
   - Host **CMD_START_FLASH** gönderir.  
   - Bootloader uygulama sektörlerini siler, ACK döner.  
   - Host 256 byte’lık chunk’lar halinde **CMD_SEND_DATA** (veri + CRC16) gönderir; her chunk için bootloader CRC doğrular, flash’a yazar, ACK döner.  
   - Host **CMD_END_FLASH** (4 byte boyut) gönderir.  
   - Bootloader metadata sektörünü siler, metadata’yı (MD5, boyut, magic) yazar, doğrular, uygulamaya atlar.

5. **Paket formatı (tüm komutlar)**  
   `[0xAA][CMD:1B][LEN:2B LE][DATA][CRC16:2B LE]`  
   CRC16-CCITT: CMD + LEN(2) + DATA üzerinden (init 0xFFFF).

---

## 2. Dosya Bazında Algoritmalar

### 2.1 `bootloader.h`
- **Amaç:** Sabitler, paket komutları, yanıt kodları, metadata yapısı ve bootloader API prototiplerini tanımlar.
- **İçerik:**
  - Flash adresleri: `BOOTLOADER_APP_START_ADDR`, `BOOTLOADER_METADATA_ADDR`, sektör numaraları.
  - Paket: `BOOTLOADER_PKT_SYNC` (0xAA), `CMD_*`, `BOOTLOADER_ACK`/`NACK`/`ACK_SAME`/`ACK_DIFF`.
  - `FirmwareMetadata_t`: magic[4], md5[16], fw_size, valid, reserved.
  - Jump reason adresi ve değerleri (timeout / flash).
  - API: `Bootloader_IsApplicationPresent`, `Bootloader_JumpToApplication`, `Bootloader_CheckAndRun`.

---

### 2.2 `bootloader.c`
- **Amaç:** UART’tan paket almak, komutlara göre MD5 kontrolü / flash yazma / link testi yapmak, gerekirse uygulamaya atlamak.

**CRC16-CCITT:** Tablo ile CMD+LEN+DATA üzerinden hesaplanır; paket doğrulama ve chunk doğrulama için kullanılır.

**Metadata:** Sector 1’den okunur; magic + valid kontrolü ile geçerlilik belirlenir. Boyut bilgisi len==16 iken metadata’dan, len==20 iken sadece payload’dan (16B MD5 + 4B size) alınır.

**Uygulama varlığı:** `APP_START`’taki vektör tablosunun ilk iki kelimesi (MSP, Reset_Handler) boş değilse ve MSP 0x2000xxxx ise uygulama var kabul edilir.

**Jump:** Interrupt’lar kapatılır, SysTick sıfırlanır, VTOR=APP_START, MSP yüklenir, Reset_Handler çağrılır.

**Ana döngü (Bootloader_EnterUpdateMode):**
1. 150 ms timeout ile **sync (0xAA)** bekle; gelmezse sayacı artır; ~200 pencerede (≈30 sn) hiç geçerli paket alınmadıysa uygulama varsa oraya atla.
2. **receive_packet:** CMD, LEN(2), payload, CRC16 al; CRC doğrula.
3. **CMD_CHECK_MD5:**  
   - Payload 16 veya 20 byte olmalı. 20 ise boyut son 4 byte’tan (LE). 16 ise metadata’dan boyut al.  
   - `compute_md5_from_flash(APP_START, check_size, md5_computed)` ile flash’tan MD5 hesapla.  
   - Host MD5 (payload’ın ilk 16 byte’ı) ile aynıysa ACK_SAME gönder, debug satırı yaz, uygulamaya atla.  
   - Değilse ACK_DIFF gönder, 25 ms bekle, 20 byte (hesaplanan MD5 + boyut) gönder.
4. **CMD_START_FLASH:**  
   - Uygulama sektörlerini sil, ACK gönder.  
   - Döngüde: sync bekle → paket al. CMD_END_FLASH gelirse payload’taki 4 byte’ı total_written yap, çık. CMD_SEND_DATA ve len==258 ise CRC doğrula, 256 byte’ı flash’a yaz, ACK gönder.  
   - Metadata sektörünü sil, flash’tan MD5 hesapla, metadata yapısını doldur, Sector 1’e yaz, tekrar MD5 ile doğrula, ACK gönder, uygulamaya atla.
5. **CMD_LINK_TEST:** Payload’tan 10 byte kopyala, ACK gönder, aynı 10 byte’ı echo olarak gönder.
6. Bilinmeyen CMD → NACK.

**compute_md5_from_flash:** 64 byte bloklar halinde flash’tan okuyup `MD5_Update` ile besler; sonunda `MD5_Final`. Büyük RAM buffer kullanmaz.

---

### 2.3 `md5.h`
- **Amaç:** MD5 API’si ve context yapısı.
- **MD5_Context_t:** state[4], count[2] (bit sayacı), buffer[64].
- **Fonksiyonlar:** `MD5_Init`, `MD5_Update`, `MD5_Final` (16 byte digest).

---

### 2.4 `md5.c`
- **Amaç:** RFC 1321 MD5; stdlib’e bağımlılık yok (C99, STM32).

**Sabitler:** F,G,H,I yardımcı makroları; ROTLEFT; FF, GG, HH, II round makroları (a güncellenir, (b,c,d) kullanılır, ROTATE_STATE ile (a,b,c,d)→(d,a,b,c)). S11–S44 shift değerleri; K[64] sabitleri.

**md5_shift(r, i):** Round r, adım i için shift değerini döndürür. Makro yerine inline fonksiyon kullanılır (virgül genişleme hatasını önlemek için); 16 elemanlı tablo stack’te tutulur (FLASH tasarrufu).

**md5_transform(state, block):**  
- 64 byte’lık blok 16 adet uint32_t’e (little-endian) parse edilir.  
- 4 round: Round 1 FF + x[i], K[i]; Round 2 GG + x[(5*i+1)%16], K[16+i]; Round 3 HH + x[(3*i+5)%16], K[32+i]; Round 4 II + x[(7*i)%16], K[48+i]. Her adımda ilgili shift `md5_shift(round, i)` ile alınır; her adım sonunda ROTATE_STATE.  
- state[0..3] güncellenir (a,b,c,d eklenir).

**MD5_Init:** RFC 1321 başlangıç state ve count=0.

**MD5_Update:** Gelen veriyi 64 byte’lık bloklara tamamlar; her tam blok için `md5_transform`. Kalan byte’lar context buffer’da tutulur.

**MD5_Final:** Bit sayacını 8 byte little-endian yazar; padding (0x80 + sıfırlar) ve uzunluk ile son bloğu tamamlar; state’i 16 byte little-endian olarak digest’e yazar.

---

### 2.5 `host_flasher.py`
- **Amaç:** .hex/.bin dosyasını okuyup UART üzerinden bootloader protokolü ile yüklemek; MD5 eşleşirse yüklemeyi atlamak.

**Paket:** `build_packet(cmd, payload)` → 0xAA + CMD + LEN(2 LE) + payload + CRC16(CMD+LEN+payload) (LE).

**Dosya:**  
- `.hex` → IntelHex ile APP_START..APP_START+APP_MAX_SIZE aralığı binary’e çevrilir.  
- `.bin` → olduğu gibi okunur.

**MD5:** `hashlib.md5(data).digest()` ile host tarafında hesaplanır; boyut `len(data)`.

**Ana akış (main):**
1. `--link-test` ise sadece link testi yapılır (10 byte rastgele, ACK + echo karşılaştırması).
2. Dosya yoksa / geçersizse çıkış.
3. Seri port açılır, PORT_READY_DELAY beklenir.
4. **--force değilse veya --check ise:**  
   - CMD_CHECK_MD5 (20 byte: MD5 + size LE) gönderilir.  
   - ACK_SAME gelirse “Firmware zaten yüklü”, debug satırı okunur, çıkış.  
   - ACK_DIFF gelirse 0.5 s beklenir, 20 byte (cihaz MD5 + boyut) okunur; isteğe bağlı MD5 karşılaştırma mesajı.  
   - Başka yanıt/timeout ise hata.
5. **--check** ise sadece sonucu yazıp çıkış.
6. **Flash:** CMD_START_FLASH → ACK kontrolü. Veriyi 256 byte chunk’lara böl; her chunk + CRC16 ile CMD_SEND_DATA gönder, her seferinde ACK bekle. CMD_END_FLASH (4 byte size) gönder, son ACK’ı kontrol et.

**Link test:** CMD_LINK_TEST ile 10 byte gönder; ACK + aynı 10 byte echo gelmeli.

---

### 2.6 `main.c` (uygulama)
- **Amaç:** Bootloader’dan sonra çalışan örnek uygulama (LD3 blink).
- **Akış:** HAL_Init, SystemClock_Config (HSE+PLL), PA5 (LED) GPIO çıkış, sonsuz döngüde 500 ms’de bir toggle ve HAL_Delay. Hata durumunda Error_Handler (interrupt kapat, sonsuz döngü).

---

### 2.7 `serial_monitor.py`
- **Amaç:** Seri porttan gelen veriyi ham olarak terminale yazdırmak (bootloader debug satırları vb.).
- **Akış:** Port ve baud (varsayılan COM10, 115200) açılır; `--timeout` süresi boyunca gelen byte’lar okunur; yazdırılabilir karakterler olduğu gibi, diğerleri `\xHH` olarak yazdırılır; süre bitince kapatılır.

---

## 3. Özet Tablo

| Dosya              | Görevi |
|--------------------|--------|
| **bootloader.h**   | Adresler, komutlar, metadata yapısı, API prototipleri |
| **bootloader.c**   | UART paket alma, CMD_CHECK_MD5 / CMD_START_FLASH / CMD_LINK_TEST işleme, flash yazma, MD5 (flash’tan), uygulamaya atlama |
| **md5.h**          | MD5 context ve Init/Update/Final API |
| **md5.c**          | RFC 1321 MD5 (blok dönüşümü, 4 round, md5_shift ile shift tablosu) |
| **host_flasher.py**| .hex/.bin okuma, CMD_CHECK_MD5 ile “zaten yüklü” kontrolü, CMD_START/SEND/END ile flash yükleme, link testi |
| **main.c**         | Uygulama: saat, LED blink |
| **serial_monitor.py** | Seri port dinleme, gelen veriyi yazdırma |

Bu yapı ile “aynı .hex ikinci kez gönderildiğinde MD5 eşleşir, yükleme atlanır ve bootloader uygulamaya atlar” davranışı sağlanır.
