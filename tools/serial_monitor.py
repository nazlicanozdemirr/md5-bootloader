"""
Seri porttan gelen veriyi dinleyip yazdırır.
Bootloader'daki MD5 self-test çıktısını görmek için:
  1. Bu scripti çalıştır:  python serial_monitor.py -p COM10
  2. Cihazı RESETLE
  3. Birkaç saniye içinde gelen tüm çıktıyı görürsün (MD5 hex satırı dahil)
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    serial = None


def main():
    p = argparse.ArgumentParser(description="Seri port dinle; cihazı resetleyince gelen veriyi yazdırır.")
    p.add_argument("-p", "--port", default="COM10", help="Seri port")
    p.add_argument("-b", "--baud", type=int, default=115200, help="Baud")
    p.add_argument("-t", "--timeout", type=float, default=15.0, help="Kaç saniye dinlenecek (varsayılan 15)")
    args = p.parse_args()
    if serial is None:
        p.error("pyserial gerekli: python -m pip install -r requirements.txt")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f"Port açılamadı: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Port: {args.port} @ {args.baud} baud.")
    print("Cihazı RESETLEYİN; gelen veri aşağıda görünecek.")
    print("-" * 40)

    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            for b in data:
                if 32 <= b < 127 or b in (9, 10, 13):
                    print(chr(b), end="", flush=True)
                else:
                    print(f"\\x{b:02X}", end="", flush=True)
        else:
            time.sleep(0.05)

    print("\n" + "-" * 40)
    print("Süre doldu.")
    ser.close()


if __name__ == "__main__":
    main()
