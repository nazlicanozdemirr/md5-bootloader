

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define BOOTLOADER_FLASH_BASE       (0x08000000UL)
#define BOOTLOADER_METADATA_ADDR   (0x08004000UL)
#define BOOTLOADER_APP_START_ADDR  (0x08008000UL)
#define BOOTLOADER_APP_MAX_SIZE    (480UL * 1024UL)

#define BOOTLOADER_SECTOR_METADATA   (1u)
#define BOOTLOADER_APP_FIRST_SECTOR  (2u)
#define BOOTLOADER_APP_SECTOR_COUNT  (6u)

#define BOOTLOADER_PKT_SYNC         (0xAAu)

#define CMD_CHECK_MD5              (0x01u)
#define CMD_START_FLASH            (0x02u)
#define CMD_SEND_DATA              (0x03u)
#define CMD_END_FLASH              (0x04u)
#define CMD_LINK_TEST              (0x05u)
#define BOOTLOADER_LINK_TEST_SIZE  (10u)

#define BOOTLOADER_ACK             (0x79u)
#define BOOTLOADER_NACK            (0x1Fu)
#define ACK_SAME                   (0x80u)
#define ACK_DIFF                   (0x81u)

#define BOOTLOADER_CHUNK_SIZE      (256u)
#define BOOTLOADER_MD5_LEN         (16u)
#define BOOTLOADER_TIMEOUT_MS      (30000u)
#define BOOTLOADER_UART_RX_TIMEOUT_MS (2000u)

#define BOOTLOADER_JUMP_REASON_ADDR   (0x2001FFFCUL)
#define BOOTLOADER_JUMP_REASON_TIMEOUT (0u)
#define BOOTLOADER_JUMP_REASON_FLASH  (1u)

#define BOOTLOADER_METADATA_MAGIC_0 ('S')
#define BOOTLOADER_METADATA_MAGIC_1 ('T')
#define BOOTLOADER_METADATA_MAGIC_2 ('M')
#define BOOTLOADER_METADATA_MAGIC_3 ('B')
#define BOOTLOADER_METADATA_VALID   (0xAAu)

typedef struct {
  uint8_t  magic[4];
  uint8_t  md5[16];
  uint32_t fw_size;
  uint8_t  valid;
  uint8_t  reserved[7];
} FirmwareMetadata_t;

bool Bootloader_IsApplicationPresent(void);

void Bootloader_JumpToApplication(void);

void Bootloader_CheckAndRun(void);

#ifdef __cplusplus
}
#endif

#endif
