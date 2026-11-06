
"""UART tabanlı STM32 bootloader için MD5 kontrollü .hex/.bin yükleyici."""

import argparse
import hashlib
import os
import random
import struct
import sys
import time

try:
    import serial
except ImportError:
    serial = None

try:
    from intelhex import IntelHex
except ImportError:
    IntelHex = None

try:
    from tqdm import tqdm
except ImportError:
    tqdm = None

PKT_SYNC    = 0xAA
CMD_CHECK_MD5   = 0x01
CMD_START_FLASH = 0x02
CMD_SEND_DATA   = 0x03
CMD_END_FLASH   = 0x04
CMD_LINK_TEST   = 0x05
ACK         = 0x79
NACK        = 0x1F
ACK_SAME    = 0x80
ACK_DIFF    = 0x81

APP_START   = 0x08008000
APP_MAX_SIZE = 480 * 1024
CHUNK_SIZE  = 256
LINK_TEST_SIZE = 10

MAX_INITIAL_RETRIES = 35
INITIAL_RESPONSE_TIMEOUT = 2.5
RETRY_DELAY = 0.6
PORT_READY_DELAY = 1.2

CRC16_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
]


def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT (0x1021, init 0xFFFF) değeri hesaplar."""
    crc = 0xFFFF
    for b in data:
        crc = ((crc << 8) ^ CRC16_TABLE[(crc >> 8) ^ b]) & 0xFFFF
    return crc


def build_packet(cmd: int, payload: bytes) -> bytes:
    """Verilen komut ve veri için bootloader paketini oluşturur."""
    length = len(payload)
    header = bytes([PKT_SYNC, cmd, length & 0xFF, (length >> 8) & 0xFF])
    body = header[1:] + payload
    crc = crc16_ccitt(body)
    return header + payload + struct.pack("<H", crc)


def load_hex(path: str):
    """Intel HEX dosyasını okuyup uygulama aralığını düz binary olarak döndürür."""
    if IntelHex is None:
        raise RuntimeError("intelhex yüklü değil; .hex için: pip install intelhex")
    ih = IntelHex(path)
    try:
        end = ih.maxaddr()
    except Exception:
        return None, APP_START
    if end < APP_START:
        return None, APP_START
    start = max(ih.minaddr(), APP_START)
    end = min(end, APP_START + APP_MAX_SIZE - 1)
    data = ih.tobinstr(start=start, end=end)
    return data, APP_START


def load_bin(path: str):
    """Ham binary dosyasını okuyup başlangıç adresi ile birlikte döndürür."""
    with open(path, "rb") as f:
        data = f.read()
    return data, APP_START


def run_link_test(args):
    """UART hattını test etmek için 10 baytlık paket gönderip echo bekler."""
    try:
        ser = serial.Serial(args.port, args.baud, timeout=2.0)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
    except Exception as e:
        print(f"Port acilamadi: {e}", file=sys.stderr)
        sys.exit(1)
    test_payload = bytes(random.randint(0, 255) for _ in range(LINK_TEST_SIZE))
    pkt = build_packet(CMD_LINK_TEST, test_payload)
    rsp = None
    echo = None
    for attempt in range(MAX_INITIAL_RETRIES):
        ser.reset_input_buffer()
        ser.write(pkt)
        ser.timeout = INITIAL_RESPONSE_TIMEOUT
        rsp = ser.read(1)
        ser.timeout = 2.0
        if rsp and rsp[0] == ACK:
            echo = b""
            while len(echo) < LINK_TEST_SIZE:
                part = ser.read(LINK_TEST_SIZE - len(echo))
                if not part:
                    break
                echo += part
            break
        if attempt < MAX_INITIAL_RETRIES - 1:
            time.sleep(RETRY_DELAY)
    ser.close()
    if rsp and rsp[0] == ACK and echo == test_payload:
        print("Link test OK: 10 byte paket STM'ye ulasti ve aynen geri geldi.")
    else:
        if not rsp or rsp[0] != ACK:
            print("Link test FAILED: STM ACK vermedi (cihazi resetleyip tekrar deneyin).", file=sys.stderr)
        else:
            print("Link test FAILED: Echo farkli (gonderilen ile alinan 10 byte eslesmedi).", file=sys.stderr)
            if echo is not None:
                print("  Gonderilen (host):", test_payload.hex(), file=sys.stderr)
                print("  Gelen (STM):      ", echo.hex(), file=sys.stderr)
                print("  Uzunluklar: host", len(test_payload), "byte, STM", len(echo), "byte", file=sys.stderr)
        sys.exit(1)


def main():
    """Argümanları okuyup gerekirse MD5 kontrolü ve firmware yüklemesini başlatır."""
    parser = argparse.ArgumentParser(
        description="STM32F446RE UART bootloader: .hex/.bin yükle (paket protokolü, MD5)."
    )
    parser.add_argument("file", nargs="?", default=None, help=".hex veya .bin dosyası (--link-test ile atlanabilir)")
    parser.add_argument("-p", "--port", default="COM10", help="Seri port (varsayılan: COM10)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud (varsayılan: 115200)")
    parser.add_argument("--force", action="store_true", help="MD5 atla, her zaman yükle")
    parser.add_argument("--check", action="store_true", help="Sadece kontrol et: bu dosya cihazda var mı? (yükleme yapma)")
    parser.add_argument("--link-test", action="store_true", help="UART link testi: 10 bytelik paket gonder, STM'de ulasip ulasmadigini dogrula")
    args = parser.parse_args()
    if serial is None:
        parser.error("pyserial gerekli: python -m pip install -r requirements.txt")

    if args.link_test:
        run_link_test(args)
        return

    path = args.file
    if not path:
        print("Dosya belirtin veya --link-test kullanin.", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(path):
        print(f"Dosya bulunamadı: {path}", file=sys.stderr)
        print("Yolun doğru olduğundan emin olun (ör. stmcube, app_md5_1).", file=sys.stderr)
        sys.exit(1)
    if path.lower().endswith(".hex"):
        data, _ = load_hex(path)
    elif path.lower().endswith(".bin"):
        data, _ = load_bin(path)
    else:
        print("Dosya .hex veya .bin olmalı.", file=sys.stderr)
        sys.exit(1)

    if not data:
        print("Dosya okunamadı veya boş.", file=sys.stderr)
        sys.exit(1)

    size = len(data)
    if size > APP_MAX_SIZE:
        print(f"Firmware {APP_MAX_SIZE // 1024} KB'dan büyük olamaz.", file=sys.stderr)
        sys.exit(1)

    img_md5 = hashlib.md5(data).digest()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=2.0)
        ser.dtr = False
        ser.rts = False
        time.sleep(PORT_READY_DELAY)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        print(f"Port: {args.port}, {args.baud} baud (cihazı resetleyip bu scripti çalıştırın).")
    except Exception as e:
        print(f"Port açılamadı: {e}", file=sys.stderr)
        sys.exit(1)

    def read_response(timeout=2.0):
        ser.timeout = timeout
        r = ser.read(1)
        ser.timeout = 2.0
        return r[0] if r else None

    try:
        if not args.force or args.check:
            check_payload = img_md5 + struct.pack("<I", size)
            pkt = build_packet(CMD_CHECK_MD5, check_payload)
            rsp = None
            for attempt in range(MAX_INITIAL_RETRIES):
                ser.reset_input_buffer()
                ser.write(pkt)
                rsp = read_response(INITIAL_RESPONSE_TIMEOUT)
                if rsp in (ACK_SAME, ACK_DIFF):
                    break
                if attempt == 0:
                    print("Bootloader yanıt vermedi, yeniden deneniyor...", flush=True)
                elif attempt < MAX_INITIAL_RETRIES - 1:
                    print(f"  ({attempt + 1}/{MAX_INITIAL_RETRIES})", end=" ", flush=True)
                if attempt < MAX_INITIAL_RETRIES - 1:
                    time.sleep(RETRY_DELAY)
            if rsp == ACK_SAME:
                print("Firmware zaten yüklü (MD5 eşleşti). Yükleme atlandı.")
                print("(Bu dosya cihazda mevcut; tekrar yazılmadı.)")
                time.sleep(0.15)
                if ser.in_waiting:
                    extra = ser.read(ser.in_waiting).decode("ascii", errors="replace").strip()
                    if extra:
                        print("Bootloader:", extra)
                return
            if rsp != ACK_DIFF:
                rsp_hex = f"0x{rsp:02X}" if rsp is not None else "timeout/yanıt yok"
                print(f"Beklenmeyen yanıt: {rsp_hex} (ACK_DIFF=0x81 bekleniyordu).", file=sys.stderr)
                print("İpucu 1: Cihazı RESETLEYİN, hemen ardından bu scripti tekrar çalıştırın.", file=sys.stderr)
                print("İpucu 2: Bootloader'ı (sürekli UART dinleyen yeni kod) ST-Link/CubeProgrammer ile 0x08000000'e yüklediğinizden emin olun. Bu script sadece uygulamayı yükler.", file=sys.stderr)
                sys.exit(1)
            time.sleep(0.5)
            device_buf = b""
            deadline = time.monotonic() + 5.0
            while len(device_buf) < 20 and time.monotonic() < deadline:
                want = min(5, 20 - len(device_buf))
                part = ser.read(want)
                if part:
                    device_buf += part
                else:
                    time.sleep(0.03)
            if len(device_buf) < 20:
                print("Uyari: Cihazdan 20 byte yerine", len(device_buf), "byte alindi; MD5 karsilastirma guvenilir olmayabilir.", flush=True)
            device_md5 = device_buf[:16]
            device_size_b = device_buf[16:20] if len(device_buf) >= 20 else b"\x00\x00\x00\x00"
            if len(device_md5) == 16:
                print("MD5 karsilastirma (neden eslesmedi gorulebilir):", flush=True)
                print("  Gonderilen (host):  ", img_md5.hex(), flush=True)
                print("  Cihazdaki (flash):  ", device_md5.hex(), flush=True)
                if len(device_size_b) == 4:
                    dev_size = device_size_b[0] | (device_size_b[1] << 8) | (device_size_b[2] << 16) | (device_size_b[3] << 24)
                    print("  Boyut: host", size, "byte, cihaz", dev_size, "byte", flush=True)
            time.sleep(0.15)
            if ser.in_waiting:
                extra = ser.read(ser.in_waiting).decode("ascii", errors="replace").strip()
                if extra:
                    print("Bootloader:", extra)

        if args.check:
            if rsp == ACK_SAME:
                print("Sonuç: Bu dosya CİHAZDA YÜKLÜ. Tekrar yazılmadı.")
            else:
                print("Sonuç: Bu dosya cihazda YOK veya farklı. Yüklemek için --check olmadan çalıştırın.")
            return

        print("Flash başlatılıyor...")
        ser.write(build_packet(CMD_START_FLASH, b""))
        if read_response(5.0) != ACK:
            print("Cihaz ACK vermedi (flash başlatılamadı).", file=sys.stderr)
            sys.exit(1)
        time.sleep(0.05)

        sent = 0
        chunk_count = (size + CHUNK_SIZE - 1) // CHUNK_SIZE
        iterator = range(chunk_count)
        if tqdm is not None:
            iterator = tqdm(iterator, unit="chunk", unit_scale=False,
                          desc="Flash", total=chunk_count, ncols=80)
        for i in iterator:
            start = i * CHUNK_SIZE
            chunk = data[start:start + CHUNK_SIZE]
            if len(chunk) < CHUNK_SIZE:
                chunk = chunk + bytes(CHUNK_SIZE - len(chunk))
            crc = crc16_ccitt(chunk)
            payload = chunk + struct.pack("<H", crc)
            ser.write(build_packet(CMD_SEND_DATA, payload))
            time.sleep(0.02)
            rsp = read_response(2.0)
            if rsp != ACK:
                print(f"\nChunk {i+1}/{chunk_count} NACK.", file=sys.stderr)
                sys.exit(1)
            sent += len(chunk)
            if tqdm is None:
                pct = 100 * sent // size if size else 100
                print(f"\r  {sent}/{size} byte ({pct}%)", end="", flush=True)
        if tqdm is None:
            print()

        fw_size_payload = struct.pack("<I", size)
        ser.write(build_packet(CMD_END_FLASH, fw_size_payload))
        rsp = read_response(5.0)
        if rsp == ACK:
            print("Yükleme tamamlandı. Cihaz uygulamaya geçiyor.")
        else:
            rsp_hex = f"0x{rsp:02X}" if rsp is not None else "timeout/yanıt yok"
            print(f"Son ACK alınamadı: {rsp_hex}", file=sys.stderr)
            sys.exit(1)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
