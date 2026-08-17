# STM32 MD5 Bootloader

Bu depo, STM32F446 tabanlı uygulama ve bootloader arasında firmware aktarımı,
MD5 bütünlük doğrulaması ve seri port üzerinden host yükleme çalışmalarını
bir arada tutar.

## İçerik

- `include/` ve `src/`: yeniden kullanılabilir bootloader ve MD5 kaynakları.
- `tools/`: seri port host flasher ve seri monitör araçları.
- `examples/minimal/`: uygulama/bootloader entegrasyonu için küçük örnekler.
- `examples/stm32cubeide/`: STM32CubeIDE uygulama ve bootloader projeleri.
- `examples/vscode/`: CMake ve VS Code'a aktarılmış proje örnekleri.
- `firmware-samples/`: geliştirme sırasında alınmış firmware çıktıları.
- `docs/`: algoritma, kurulum ve dönüşüm notları.

## Python araçları

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python .\tools\host_flasher.py --help
```

## STM32 projeleri

STM32CubeIDE örneklerini ilgili `.ioc` veya `.project` dosyasıyla içe
aktarabilirsiniz. VS Code örnekleri `CMakeLists.txt` ve `CMakePresets.json`
dosyalarını içerir. `Debug`, `Release` ve `build` çıktıları depoya alınmaz.

Firmware yazma işlemi donanımı çalışamaz hale getirebilir. Hedef MCU, flash
başlangıç adresleri ve linker betikleri doğrulanmadan gerçek karta yükleme
yapmayın.
