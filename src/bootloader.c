/**
  ******************************************************************************
  * @file    bootloader.c
  * @brief   UART tabanlÄ±, MD5 doÄŸrulamalÄ± bootloader â€” paket, flash, jump
  *          Paket: [0xAA][CMD][LEN:2B LE][DATA][CRC16:2B LE]. CRC16-CCITT.
  ******************************************************************************
  */

#include "bootloader.h"
#include "md5.h"
#include "usart.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"
#include <string.h>

/* UART handle (usart.c) */
extern UART_HandleTypeDef huart2;

#define PKT_SYNC          BOOTLOADER_PKT_SYNC
#define APP_START         BOOTLOADER_APP_START_ADDR
#define METADATA_ADDR     BOOTLOADER_METADATA_ADDR
#define APP_MAX_SIZE      BOOTLOADER_APP_MAX_SIZE
#define CHUNK_SIZE        BOOTLOADER_CHUNK_SIZE
#define METADATA_MAGIC_0  BOOTLOADER_METADATA_MAGIC_0
#define METADATA_MAGIC_1  BOOTLOADER_METADATA_MAGIC_1
#define METADATA_MAGIC_2  BOOTLOADER_METADATA_MAGIC_2
#define METADATA_MAGIC_3  BOOTLOADER_METADATA_MAGIC_3
#define METADATA_VALID    BOOTLOADER_METADATA_VALID
#define TIMEOUT_MS        BOOTLOADER_TIMEOUT_MS
#define RX_TIMEOUT_MS     BOOTLOADER_UART_RX_TIMEOUT_MS

#define APP_FIRST_SECTOR  BOOTLOADER_APP_FIRST_SECTOR
#define APP_SECTOR_COUNT  BOOTLOADER_APP_SECTOR_COUNT
#define SECTOR_METADATA   BOOTLOADER_SECTOR_METADATA

#define EMPTY_FLASH_WORD  (0xFFFFFFFFUL)
#define MAX_PAYLOAD       (260u)   /* 256 + 2 CRC veya 16 MD5 vb. */
#define PACKET_HEADER_LEN (4u)     /* 0xAA + CMD + LEN(2) */
#define PACKET_TAIL_LEN   (2u)     /* CRC16 */

/* CRC16-CCITT tablosuz (FLASH tasarrufu ~512 byte): polinom 0x1021, init 0xFFFF */
static uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *data, uint32_t len)
{
  while (len--) {
    crc ^= (uint16_t)*data++ << 8;
    for (uint8_t i = 8u; i != 0u; i--) {
      if (crc & (uint16_t)0x8000u)
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      else
        crc = (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
  return crc16_ccitt_update(0xFFFFu, data, len);
}

/* Metadata'yÄ± Sector 1'den oku */
static void read_metadata(FirmwareMetadata_t *meta)
{
  const FirmwareMetadata_t *flash_meta = (const FirmwareMetadata_t *)METADATA_ADDR;
  memcpy(meta, flash_meta, sizeof(FirmwareMetadata_t));
}

/* Metadata geÃ§erli mi (magic + valid bayraÄŸÄ±) */
static bool is_metadata_valid(const FirmwareMetadata_t *meta)
{
  if (meta->magic[0] != METADATA_MAGIC_0 || meta->magic[1] != METADATA_MAGIC_1 ||
      meta->magic[2] != METADATA_MAGIC_2 || meta->magic[3] != METADATA_MAGIC_3)
    return false;
  if (meta->valid != METADATA_VALID)
    return false;
  return true;
}

bool Bootloader_IsApplicationPresent(void)
{
  const uint32_t *vt = (const uint32_t *)APP_START;
  if (vt[0] == EMPTY_FLASH_WORD || vt[1] == EMPTY_FLASH_WORD)
    return false;
  /* Ä°lk kelime stack pointer (RAM 0x2000xxxx) olmalÄ± */
  if ((vt[0] & 0xFF000000u) != 0x20000000u)
    return false;
  return true;
}

void Bootloader_JumpToApplication(void)
{
  const uint32_t *vt = (const uint32_t *)APP_START;
  uint32_t app_msp = vt[0];
  uint32_t app_reset = vt[1];

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  SCB->VTOR = APP_START;
  __set_MSP(app_msp);
  ((void (*)(void))app_reset)();
  for (;;) { }
}

/* Tek byte gÃ¶nder (yanÄ±t: ACK, NACK, ACK_SAME, ACK_DIFF) */
static void send_response(uint8_t byte)
{
  HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY);
}

/* Sync (0xAA) bekle; timeout'ta false dÃ¶ner */
static bool wait_sync(uint32_t timeout_ms)
{
  uint8_t b;
  if (HAL_UART_Receive(&huart2, &b, 1, timeout_ms) != HAL_OK)
    return false;
  return (b == PKT_SYNC);
}

/* CMD + LEN(2) + DATA + CRC16 ile tam paket al. payload_buf en az max_len. *out_len = alÄ±nan payload uzunluÄŸu. */
static bool receive_packet(uint8_t *out_cmd, uint8_t *payload_buf, uint32_t max_len, uint32_t *out_len, uint32_t timeout_ms)
{
  uint8_t cmd;
  uint8_t len_buf[2];
  uint16_t recv_crc;
  uint16_t calc_crc;
  uint32_t len;

  if (HAL_UART_Receive(&huart2, &cmd, 1, timeout_ms) != HAL_OK)
    return false;
  if (HAL_UART_Receive(&huart2, len_buf, 2, timeout_ms) != HAL_OK)
    return false;
  len = (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8);
  if (len > max_len)
    return false;
  if (len > 0 && HAL_UART_Receive(&huart2, payload_buf, len, timeout_ms) != HAL_OK)
    return false;
  {
    uint8_t crc_rx[2];
    if (HAL_UART_Receive(&huart2, crc_rx, 2, timeout_ms) != HAL_OK)
      return false;
    recv_crc = (uint16_t)crc_rx[0] | ((uint16_t)crc_rx[1] << 8);
  }
  calc_crc = crc16_ccitt(&cmd, 1);
  calc_crc = crc16_ccitt_update(calc_crc, len_buf, 2);
  if (len > 0)
    calc_crc = crc16_ccitt_update(calc_crc, payload_buf, len);
  if (calc_crc != recv_crc)
    return false;
  *out_cmd = cmd;
  *out_len = len;
  return true;
}

/* SektÃ¶r 2â€“7 sil (uygulama alanÄ±) */
static HAL_StatusTypeDef erase_app_sectors(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sectorError = 0;

  if (HAL_FLASH_Unlock() != HAL_OK)
    return HAL_ERROR;
  erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
  erase.Banks        = FLASH_BANK_1;
  erase.Sector      = APP_FIRST_SECTOR;
  erase.NbSectors   = APP_SECTOR_COUNT;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&erase, &sectorError);
  HAL_FLASH_Lock();
  return s;
}

/* Sector 1 (metadata) sil */
static HAL_StatusTypeDef erase_metadata_sector(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sectorError = 0;

  if (HAL_FLASH_Unlock() != HAL_OK)
    return HAL_ERROR;
  erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
  erase.Banks        = FLASH_BANK_1;
  erase.Sector      = SECTOR_METADATA;
  erase.NbSectors   = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&erase, &sectorError);
  HAL_FLASH_Lock();
  return s;
}

/* Flash'a kelime kelime yaz (adres 4 byte hizalÄ±) */
static HAL_StatusTypeDef program_flash(uint32_t addr, const uint8_t *data, uint32_t len)
{
  uint32_t offset = 0;

  if (HAL_FLASH_Unlock() != HAL_OK)
    return HAL_ERROR;
  while (offset + 4 <= len) {
    uint32_t word = (uint32_t)data[offset]
                    | ((uint32_t)data[offset + 1] << 8)
                    | ((uint32_t)data[offset + 2] << 16)
                    | ((uint32_t)data[offset + 3] << 24);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + offset, word) != HAL_OK) {
      HAL_FLASH_Lock();
      return HAL_ERROR;
    }
    offset += 4;
  }
  HAL_FLASH_Lock();
  return HAL_OK;
}

/* Flash'tan fw_size byte Ã¼zerinde MD5 hesapla (64 byte bloklar, RAM'e toplu kopya yok) */
static void compute_md5_from_flash(uint32_t start_addr, uint32_t fw_size, uint8_t out[16])
{
  MD5_Context_t ctx;
  uint8_t block[64];
  uint32_t offset = 0;

  MD5_Init(&ctx);
  while (offset < fw_size) {
    uint32_t to_read = fw_size - offset;
    if (to_read > 64u) to_read = 64u;
    memcpy(block, (const void *)(start_addr + offset), to_read);
    MD5_Update(&ctx, block, to_read);
    offset += to_read;
  }
  MD5_Final(&ctx, out);
}

/* Sector 1'e metadata yaz (Ã¶nce sector silinmiÅŸ olmalÄ±) */
static HAL_StatusTypeDef write_metadata(const FirmwareMetadata_t *meta)
{
  const uint8_t *p = (const uint8_t *)meta;
  uint32_t addr = METADATA_ADDR;
  uint32_t len = sizeof(FirmwareMetadata_t);
  uint32_t offset = 0;

  if (HAL_FLASH_Unlock() != HAL_OK)
    return HAL_ERROR;
  while (offset + 4 <= len) {
    uint32_t word = (uint32_t)p[offset]
                    | ((uint32_t)p[offset + 1] << 8)
                    | ((uint32_t)p[offset + 2] << 16)
                    | ((uint32_t)p[offset + 3] << 24);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + offset, word) != HAL_OK) {
      HAL_FLASH_Lock();
      return HAL_ERROR;
    }
    offset += 4;
  }
  HAL_FLASH_Lock();
  return HAL_OK;
}

static void Bootloader_EnterUpdateMode(void)
{
  uint8_t payload[MAX_PAYLOAD];
  uint8_t cmd;
  uint32_t len;
  uint32_t start_tick;
  FirmwareMetadata_t meta;
  uint32_t flash_addr;
  uint32_t total_written;
  uint8_t md5_computed[16];

  for (;;) {
    start_tick = HAL_GetTick();
    while (!wait_sync(1000u)) {
      if ((HAL_GetTick() - start_tick) >= TIMEOUT_MS) {
        if (Bootloader_IsApplicationPresent())
          Bootloader_JumpToApplication();
        start_tick = HAL_GetTick();
      }
    }
    if (!receive_packet(&cmd, payload, MAX_PAYLOAD, &len, RX_TIMEOUT_MS)) {
      continue;
    }

    switch (cmd) {
      case CMD_CHECK_MD5:
        if (len != 16u && len != 20u) {
          send_response(BOOTLOADER_NACK);
          break;
        }
        {
          uint32_t check_size = 0u;
          if (len >= 20u) {
            check_size = (uint32_t)payload[16] | ((uint32_t)payload[17] << 8)
                       | ((uint32_t)payload[18] << 16) | ((uint32_t)payload[19] << 24);
            if (check_size > APP_MAX_SIZE)
              check_size = 0u;
          } else {
            read_metadata(&meta);
            if (is_metadata_valid(&meta))
              check_size = meta.fw_size;
          }
          if (check_size > 0u) {
            compute_md5_from_flash(APP_START, check_size, md5_computed);
            if (memcmp(md5_computed, payload, 16) == 0) {
              send_response(ACK_SAME);
              Bootloader_JumpToApplication();
              break;
            }
          }
          send_response(ACK_DIFF);
          HAL_Delay(25);
          {
            uint8_t buf[20];
            if (check_size > 0u) {
              memcpy(buf, md5_computed, 16u);
              buf[16] = (uint8_t)(check_size >> 0);
              buf[17] = (uint8_t)(check_size >> 8);
              buf[18] = (uint8_t)(check_size >> 16);
              buf[19] = (uint8_t)(check_size >> 24);
            } else {
              memset(buf, 0, 20u);
            }
            HAL_UART_Transmit(&huart2, buf, 20u, HAL_MAX_DELAY);
          }
        }
        break;

      case CMD_START_FLASH: {
        bool got_end_flash = false;
        if (erase_app_sectors() != HAL_OK) {
          send_response(BOOTLOADER_NACK);
          break;
        }
        send_response(BOOTLOADER_ACK);
        flash_addr = APP_START;
        total_written = 0u;
        for (;;) {
          if (!wait_sync(5000u)) break;
          if (!receive_packet(&cmd, payload, MAX_PAYLOAD, &len, RX_TIMEOUT_MS)) continue;
          if (cmd == CMD_END_FLASH) {
            if (len >= 4)
              total_written = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8)
                            | ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
            got_end_flash = true;
            break;
          }
          if (cmd != CMD_SEND_DATA || len != 258u) {
            send_response(BOOTLOADER_NACK);
            continue;
          }
          {
            uint16_t recv_crc = (uint16_t)payload[256] | ((uint16_t)payload[257] << 8);
            uint16_t calc_crc = crc16_ccitt(payload, 256);
            if (calc_crc != recv_crc) {
              send_response(BOOTLOADER_NACK);
              continue;
            }
          }
          if (program_flash(flash_addr, payload, 256) != HAL_OK) {
            send_response(BOOTLOADER_NACK);
            continue;
          }
          flash_addr += 256;
          total_written += 256;
          send_response(BOOTLOADER_ACK);
        }
        if (!got_end_flash) {
          send_response(BOOTLOADER_NACK);
          break;
        }
        if (erase_metadata_sector() != HAL_OK) {
          send_response(BOOTLOADER_NACK);
          break;
        }
        compute_md5_from_flash(APP_START, total_written, md5_computed);
        meta.magic[0] = METADATA_MAGIC_0;
        meta.magic[1] = METADATA_MAGIC_1;
        meta.magic[2] = METADATA_MAGIC_2;
        meta.magic[3] = METADATA_MAGIC_3;
        memcpy(meta.md5, md5_computed, 16);
        meta.fw_size = total_written;
        meta.valid = METADATA_VALID;
        memset(meta.reserved, 0, sizeof(meta.reserved));
        if (write_metadata(&meta) != HAL_OK) {
          send_response(BOOTLOADER_NACK);
          break;
        }
        {
          uint8_t verify[16];
          compute_md5_from_flash(APP_START, total_written, verify);
          if (memcmp(verify, meta.md5, 16) != 0) {
            send_response(BOOTLOADER_NACK);
            break;
          }
        }
        send_response(BOOTLOADER_ACK);
        Bootloader_JumpToApplication();
        break;
      }

      default:
        send_response(BOOTLOADER_NACK);
        break;
    }
  }
}

void Bootloader_CheckAndRun(void)
{
  Bootloader_EnterUpdateMode();
}
