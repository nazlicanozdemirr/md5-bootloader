/**
 * @file entegre_bootloader.c
 * @brief Mevcut bootloader projene MD5 tabanlı
 *        "bu firmware zaten yüklü mü?" kontrolünü eklemek için örnek kod.
 *
 * Ayrıca RDP Level 1 altında güvenli güncelleme için:
 *  - Sektör 2 ve sonrasını silme (Sector Erase, Mass Erase DEĞİL)
 *  - Yeni yazılımı 0x08008000'e flash'a yazma
 *  - Uygulamaya atlama (Jump to Application)
 *
 * Bu dosya, kendi bootloader.c dosyana ekleyeceğin fonksiyonların
 * TAMAMEN çalışır bir örneğidir. Sen sadece:
 *
 *  - Buradaki fonksiyonları / sabitleri kendi projenin uygun
 *    yerine kopyalayacaksın.
 *  - MD5 hesaplaması için md5.c + md5.h dosyalarını projene
 *    dahil edeceksin.
 *  - Paket/protokol kısmını kendi kullandığın UART paket yapısına
 *    göre uyarlayacaksın.
 *  - Sektör silme / flash yazma / jump için projende:
 *    stm32f4xx_hal_flash.h, stm32f4xx_hal_flash_ex.h ve CMSIS olmalı.
 *
 * Aşağıdaki açıklamalarda "SATIR SATIR" ne yaptığımızı anlattım.
 */

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* 1) SENİN BOOTLOADER'INA GÖRE DOLDURMAN GEREKEN PARAMETRELER               */
/* ------------------------------------------------------------------------- */

/**
 * @brief Uygulama başlangıç adresi.
 *
 *  - STM32F4 örneğinde bizim projede 0x08008000 kullanıyoruz.
 *  - Sen kendi bootloader'ında uygulama hangi adresten başlıyorsa
 *    bu değeri ona göre AYARLAMALISIN.
 */
#ifndef APP_START_ADDR
#define APP_START_ADDR   (0x08008000UL)
#endif

/**
 * @brief Uygulama için ayrılmış maksimum flash boyutu (byte).
 *
 *  - Bizim projede 480 * 1024 kullandık (Sector 2–7).
 *  - Sen kendi haritana göre ayarlamalısın.
 */
#ifndef APP_MAX_SIZE
#define APP_MAX_SIZE     (480UL * 1024UL)
#endif

/**
 * @brief MD5 uzunluğu (16 byte, RFC 1321).
 */
#define MD5_LEN          (16u)

/**
 * @brief Bu entegre modül için önerilen komut kodu.
 *
 *  - Mevcut bootloader komut tablonu bozmayacak, boşta bir ID seç.
 *  - Biz örnek olarak 0x31 kullandık. İstersen değiştir.
 */
#ifndef CMD_CHECK_MD5_INTEGRATED
#define CMD_CHECK_MD5_INTEGRATED   (0x31u)
#endif

/**
 * @brief Cevap kodları (istersen kendi sabitlerinle değiştir).
 *
 *  - ACK_SAME  : Flash'taki firmware ile gönderilen .hex aynı → yazma.
 *  - ACK_DIFF  : Farklı → host normal flash yükleme akışına geçsin.
 */
#ifndef RESP_ACK_SAME
#define RESP_ACK_SAME    (0x80u)
#endif

#ifndef RESP_ACK_DIFF
#define RESP_ACK_DIFF    (0x81u)
#endif

#ifndef RESP_NACK_GENERIC
#define RESP_NACK_GENERIC (0x1Fu)
#endif

/* ------------------------------------------------------------------------- */
/* 2) DIŞARIDAN SAĞLANMASI GEREKEN FONKSİYONLAR (SENİN BOOTLOADER'INDAN)     */
/* ------------------------------------------------------------------------- */

/**
 * @brief UART üzerinden tek byte'lık cevap gönderme fonksiyonu.
 *
 *  - Kendi bootloader'ında zaten bir ACK/NACK gönderme fonksiyonun
 *    varsa onu bu imza ile "sarmalayabilirsin".
 *
 * ÖRNEK:
 *   void BL_SendByte(uint8_t b) { HAL_UART_Transmit(&huartX, &b, 1, TIMEOUT); }
 */
extern void BL_SendByte(uint8_t b);

/**
 * @brief UART üzerinden ham veri gönderme fonksiyonu (opsiyonel).
 *
 *  - Eğer ACK_DIFF sonrasında 20 byte (MD5 + size) göndermek
 *    istiyorsan kullanılır.
 *
 * ÖRNEK:
 *   void BL_SendBuffer(const uint8_t *buf, uint32_t len)
 *   { HAL_UART_Transmit(&huartX, (uint8_t*)buf, len, TIMEOUT); }
 */
extern void BL_SendBuffer(const uint8_t *buf, uint32_t len);

/**
 * @brief md5.c / md5.h içinden alacağın fonksiyon.
 *
 *  - Bunun imzası bizim projedekiyle bire bir aynı:
 *    start_addr : MD5 alınacak flash başlangıç adresi
 *    size       : Bu adresten itibaren kaç byte okunacağı
 *    out_digest : 16 byte'lık MD5 sonucu (dizi)
 *
 *  - Bu fonksiyonu md5.c içinde kendin yazabilirsin (bizim
 *    projedeki compute_md5_from_flash ile aynı).
 */
extern void MD5_ComputeFromFlash(uint32_t start_addr,
                                 uint32_t size,
                                 uint8_t  out_digest[MD5_LEN]);

/* ------------------------------------------------------------------------- */
/* 3) MD5 KONTROL KOMUTUNU İŞLEYEN FONKSİYON                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief Host'tan gelen MD5 kontrol komutunu işler.
 *
 * PROTOKOL (önerilen):
 *
 *  - Host bu komutu gönderirken payload'ta 20 byte yollasın:
 *      [0..15] : Gönderilecek .hex/.bin datasının MD5'i
 *      [16..19]: Gönderilecek toplam boyut (little-endian, uint32_t)
 *
 *  - Bu fonksiyon:
 *      1) Boyutun mantıklı olup olmadığını kontrol eder.
 *      2) APP_START_ADDR'dan itibaren 'size' kadar flash'ı MD5'ler.
 *      3) MD5'ler eşitse RESP_ACK_SAME gönderir (ve İSTERSEN uygulamaya atlar).
 *      4) Eşit değilse RESP_ACK_DIFF gönderir ve opsiyonel olarak
 *         flash tarafındaki MD5 + size değerini host'a yollar.
 *
 * KULLANIM:
 *
 *  - Mevcut bootloader komut switch'ine şöyle bir satır eklersin:
 *
 *      case CMD_CHECK_MD5_INTEGRATED:
 *          BL_HandleCheckMd5Cmd(payload, payload_len);
 *          break;
 *
 *  - 'payload' ve 'payload_len' senin paket çözme fonksiyonundan
 *    gelen veri olmalı.
 */
void BL_HandleCheckMd5Cmd(const uint8_t *payload, uint32_t payload_len)
{
    uint8_t  host_md5[MD5_LEN];
    uint8_t  flash_md5[MD5_LEN];
    uint32_t fw_size = 0u;

    /* 1) Payload uzunluğunu kontrol et (20 byte olmalı). */
    if (payload_len != (MD5_LEN + 4u)) {
        BL_SendByte(RESP_NACK_GENERIC);
        return;
    }

    /* 2) Host MD5'ini ve boyutu oku. */
    memcpy(host_md5, &payload[0], MD5_LEN);
    fw_size  = (uint32_t)payload[16];
    fw_size |= ((uint32_t)payload[17] << 8);
    fw_size |= ((uint32_t)payload[18] << 16);
    fw_size |= ((uint32_t)payload[19] << 24);

    /* 3) Boyut sınırlarını kontrol et. */
    if (fw_size == 0u || fw_size > APP_MAX_SIZE) {
        BL_SendByte(RESP_NACK_GENERIC);
        return;
    }

    /* 4) Flash'tan MD5 hesapla. */
    MD5_ComputeFromFlash(APP_START_ADDR, fw_size, flash_md5);

    /* 5) Karşılaştır ve cevap gönder. */
    if (memcmp(flash_md5, host_md5, MD5_LEN) == 0) {
        /* Aynı firmware zaten yüklü. */
        BL_SendByte(RESP_ACK_SAME);

        /* İSTERSEN: burada doğrudan uygulamaya atlayabilirsin.
         *  örn.:
         *      JumpToApplication();
         */
    } else {
        /* Farklı; host yeni firmware yazabilir. */
        uint8_t info[MD5_LEN + 4u];

        BL_SendByte(RESP_ACK_DIFF);

        /* Opsiyonel: Flash tarafındaki MD5 + size bilgisini de yolla. */
        memcpy(&info[0], flash_md5, MD5_LEN);
        info[16] = (uint8_t)(fw_size & 0xFFu);
        info[17] = (uint8_t)((fw_size >> 8) & 0xFFu);
        info[18] = (uint8_t)((fw_size >> 16) & 0xFFu);
        info[19] = (uint8_t)((fw_size >> 24) & 0xFFu);
        BL_SendBuffer(info, sizeof(info));
    }
}

/* ------------------------------------------------------------------------- */
/* RDP LEVEL 1: SEKTÖR SİLME (Sector 2 ve sonrası – Mass Erase DEĞİL)       */
/* ------------------------------------------------------------------------- */
/**
 * Uygulama alanını (Sektor 2'den itibaren) siler.
 * RDP Level 1'de Mass Erase yapılmaz; sadece uygulama sektörleri silinir.
 *
 * Projende stm32f4xx_hal_flash.h ve stm32f4xx_hal_flash_ex.h olmalı.
 * APP_FLASH_SECTOR_FIRST: başlangıç sektörü (STM32F4 için FLASH_SECTOR_2 = 2).
 * APP_FLASH_SECTOR_COUNT: kaç sektör silinecek (örn. 4 → Sektor 2,3,4,5).
 */
#ifndef APP_FLASH_SECTOR_FIRST
#define APP_FLASH_SECTOR_FIRST   FLASH_SECTOR_2
#endif
#ifndef APP_FLASH_SECTOR_COUNT
#define APP_FLASH_SECTOR_COUNT   (4u)
#endif

int BL_EraseAppSectors(void)
{
#ifdef FLASH_EraseInitTypeDef
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0u;

    /* DİKKAT: Mass Erase DEĞİL, Sektör Erase kullanıyoruz (RDP Level 1 için). */
    HAL_FLASH_Unlock();

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    /* Silmeye Sektör 2'den (0x08008000) başla */
    EraseInitStruct.Sector       = APP_FLASH_SECTOR_FIRST;
    /* Uygulamanın kapladığı sektör sayısı kadar sil (örn: 4 sektör) */
    EraseInitStruct.NbSectors    = APP_FLASH_SECTOR_COUNT;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
#else
    (void)0;
    return -1; /* HAL yoksa kullanma */
#endif
}

/* ------------------------------------------------------------------------- */
/* RDP LEVEL 1: FLASH'A YAZMA (0x08008000 ve sonrası)                        */
/* ------------------------------------------------------------------------- */
/**
 * Verilen buffer'ı belirtilen flash adresine yazar.
 * STM32F4'te adres 4 byte (WORD) hizalı olmalı; uzunluk 4'ün katı olmalı.
 * UART'tan gelen .bin paketlerini 256 byte bloklar halinde alıp
 * bu fonksiyonu çağırabilirsin (YMODEM vb. protokol ile).
 *
 * Projende stm32f4xx_hal_flash.h olmalı.
 */
int BL_ProgramFlash(uint32_t flash_addr, const uint8_t *data, uint32_t len)
{
#if defined(FLASH_TYPEPROGRAM_WORD) && defined(HAL_FLASH_Unlock)
    uint32_t i;
    HAL_StatusTypeDef s;

    if (data == ((const uint8_t *)0) || (len & 3u) != 0u) return -1;
    if (flash_addr < APP_START_ADDR) return -1;
    if (flash_addr + len > APP_START_ADDR + APP_MAX_SIZE) return -1;

    HAL_FLASH_Unlock();

    for (i = 0u; i < len; i += 4u) {
        uint32_t word = (uint32_t)data[i]
            | ((uint32_t)data[i + 1u] << 8)
            | ((uint32_t)data[i + 2u] << 16)
            | ((uint32_t)data[i + 3u] << 24);
        s = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr + i, word);
        if (s != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
#else
    (void)flash_addr;
    (void)data;
    (void)len;
    return -1;
#endif
}

/* ------------------------------------------------------------------------- */
/* UYGULAMAYA ATLAMA (Jump to Application)                                   */
/* ------------------------------------------------------------------------- */
/**
 * Bootloader'dan ana uygulamaya (0x08008000) atlar.
 * Önce uygulama adresinde geçerli bir vektör tablosu olup olmadığı kontrol
 * edilir; sonra çevre birimleri kapatılıp MSP ve VTOR ayarlanır.
 *
 * Projende CMSIS (__disable_irq, __set_MSP) ve SCB->VTOR kullanılır.
 */
typedef void (*pFunction)(void);

void JumpToApplication(void)
{
    pFunction JumpToApplication_fn;
    uint32_t JumpAddress;
    __IO uint32_t *app_stack_ptr = (__IO uint32_t *)APP_START_ADDR;
    __IO uint32_t *app_reset_handler = (__IO uint32_t *)(APP_START_ADDR + 4u);

    /* 1. Uygulamanın olduğu adreste gerçekten bir kod var mı kontrol et
     *    (Stack pointer 0x2000xxxx aralığında olmalı). */
    if (((*app_stack_ptr & 0x2FFE0000u) != 0x20000000u)) {
        return; /* Geçerli uygulama yok, atlama. */
    }

    /* 2. Bootloader'ın kullandığı tüm çevre birimleri kapatın ve kesmeleri durdur. */
    __disable_irq();
#ifdef HAL_DeInit
    HAL_DeInit();
#endif

    /* 3. Ana uygulamanın Reset Handler adresini al (+4 bayt offset). */
    JumpAddress = *app_reset_handler;
    JumpToApplication_fn = (pFunction)JumpAddress;

    /* 4. Ana Yığın İşaretçisini (MSP) ana uygulamanın başlangıcına kur. */
    __set_MSP(*app_stack_ptr);

    /* 5. Vektör Tablosu (VTOR) ofsetini ana uygulamanın adresine kaydır. */
    SCB->VTOR = APP_START_ADDR;

    /* 6. Uygulamaya atla. */
    JumpToApplication_fn();
}

/* ------------------------------------------------------------------------- */
/* 4) ÖZET: BU DOSYAYI NASIL ENTEGRE EDECEKSİN?                              */
/* ------------------------------------------------------------------------- */
/*
 * 1) md5.c ve md5.h dosyalarını projene ekle.
 *
 *    - Bizim projedeki md5.c/md5.h'yi doğrudan kullanabilirsin.
 *    - md5.c içine, flash'tan okuma yapan aşağıdaki tarz bir fonksiyon ekle
 *      (veya mevcut compute_md5_from_flash fonksiyonunu bu imzaya göre
 *       BL_ComputeFromFlash olarak yeniden adlandır):
 *
 *      void MD5_ComputeFromFlash(uint32_t start, uint32_t size, uint8_t out[16])
 *      {
 *          MD5_Context_t ctx;
 *          uint8_t buf[64];
 *          uint32_t off = 0;
 *
 *          MD5_Init(&ctx);
 *          while (off < size) {
 *              uint32_t chunk = size - off;
 *              if (chunk > 64u) chunk = 64u;
 *              memcpy(buf, (const void *)(start + off), chunk);
 *              MD5_Update(&ctx, buf, chunk);
 *              off += chunk;
 *          }
 *          MD5_Final(&ctx, out);
 *      }
 *
 * 2) Kendi bootloader.c dosyana bu dosyadaki:
 *
 *      - APP_START_ADDR / APP_MAX_SIZE tanımları (veya seninkilerle eşle)
 *      - CMD_CHECK_MD5_INTEGRATED, RESP_ACK_SAME, RESP_ACK_DIFF sabitleri
 *      - BL_HandleCheckMd5Cmd fonksiyonunun TAMAMI
 *
 *    kopyalanmalı.
 *
 * 3) Mevcut komut switch'ine yeni case ekle.
 *
 *    Örneğin elinde şöyle bir yapı varsa:
 *
 *      switch (cmd)
 *      {
 *        case CMD_ERASE:
 *            ...
 *            break;
 *        case CMD_WRITE:
 *            ...
 *            break;
 *        // BURAYA EKLE:
 *        case CMD_CHECK_MD5_INTEGRATED:
 *            BL_HandleCheckMd5Cmd(payload, payload_len);
 *            break;
 *        default:
 *            ...
 *      }
 *
 * 4) BL_SendByte ve BL_SendBuffer fonksiyonlarını kendi UART katmanınla eşle.
 *
 *    - Eğer hâlihazırda "send_ack", "send_nack", "uart_send" gibi
 *      fonksiyonların varsa, bu entegre modülün dışarıdan beklediği
 *      imzaya göre küçük sarmalayıcı fonksiyonlar yazman yeterli.
 *
 * Böylece eski bootloader akışın bozulmadan kalır; sadece yeni bir
 * CMD_CHECK_MD5 komutun olur ve host tarafında MD5 aynıysa flash
 * yazma adımını atlayabilirsin.
 *
 * --- RDP LEVEL 1 GÜNCELLEME AKIŞI ---
 *
 * 1) Yeni firmware gelince önce BL_EraseAppSectors() çağır (Sektor 2 ve sonrası silinir).
 * 2) UART/USB ile .bin verisini al (YMODEM vb.); her paket için BL_ProgramFlash(adres, buf, len).
 * 3) Yazma bittikten sonra JumpToApplication() ile uygulamaya atla (veya host "atla" komutu gönderir).
 *
 * Ana uygulama tarafında: system_stm32f4xx.c içinde VECT_TAB_OFFSET = 0x8000 yap.
 * Üretimde: Bootloader 0x08000000, uygulama 0x08008000 yüklensin; Sektor 0-1 WRP; RDP Level 1.
 */

