# REA OS - Adim 1

REA OS, x86_64 icin sifirdan yazilan egitim amacli bir cekirdek projesidir. Bu
ilk adimda Limine ile BIOS/UEFI hibrit ISO acilir, framebuffer'a metin cizilir,
PS/2 klavye polling ile okunur ve yalnizca `print` komutu calisir.

> Bu proje deneyseldir. Sanal makinede ve sanal disksiz kullanin. Cekirdek
> disk, ag ve USB surucusu icermez; diske yazma veya cihaz ayari degistirmez.

## Proje agaci

```text
.
|-- .gitignore
|-- Makefile
|-- README.md
|-- limine.conf
|-- linker.ld
|-- include/
|   |-- boot.h
|   |-- console.h
|   |-- font.h
|   |-- keyboard.h
|   |-- serial.h
|   |-- shell.h
|   `-- util.h
|-- src/
|   |-- boot.c
|   |-- console.c
|   |-- font.c
|   |-- kernel.c
|   |-- keyboard.c
|   |-- keyboard_layout.c
|   |-- serial.c
|   |-- shell.c
|   `-- util.c
`-- tests/
    |-- font_test.c
    `-- shell_test.c
```

Derleme sirasinda `build/`, `limine/` ve `rea-os.iso` olusturulur; bunlar kaynak
dosyalar degil, derleme ciktilaridir.

## Dosyalar ne yapiyor?

| Dosya | Aciklama |
|---|---|
| `src/kernel.c` | Cekirdegin baslangicini yapar ve klavyeyi surekli kontrol eder. |
| `src/boot.c` | Limine protokolunu bildirir ve framebuffer bilgisini alir. |
| `src/console.c` | Siyah ekran, 8x16 yazi hucreleri ve ekran kaydirmayi yonetir. |
| `src/font.c` | ASCII 32-126 icin projede tanimlanan 5x7 bitmap harfleri tutar. |
| `src/keyboard.c` | PS/2 portlarindan kesmesiz, polling klavye okumasini yapar. |
| `src/keyboard_layout.c` | Set-1 tarama kodlarini Turkce Q tus duzenine cevirir. |
| `src/shell.c` | Satiri toplar, Enter/Backspace islemlerini yapar, komutu bulur. |
| `src/serial.c` | COM1 hazirsa hata ayiklama metnini gonderir; portu yapilandirmaz. |
| `src/util.c` | C kutuphanesi olmadan gereken temel bellek/yazi fonksiyonlarini saglar. |
| `include/` | C dosyalarinin ortak fonksiyon bildirimlerini icerir. |
| `limine.conf` | Limine'a ISO'dan hangi cekirdek dosyasini acacagini soyler. |
| `linker.ld` | Cekirdek ve Limine isteklerinin bellek yerlesimini tanimlar. |
| `Makefile` | C dosyalarini derler ve BIOS/UEFI hibrit ISO olusturur. |

### Onemli kod akisi

1. `kmain()` framebuffer'i hazirlar, `/ >` degil tam olarak `/> ` istemini basar.
2. `keyboard_poll_char()` PS/2 durum portunu okuyup hazir tus tarama kodunu alir.
3. Tus haritasi tarama kodunu ASCII karaktere cevirir; Shift ve Caps Lock uygulanir.
4. Enter satiri `shell.c` icindeki komut tablosuna verir. Tabloda yalnizca `print` vardir.
5. `print` yalnizca tirnak icindeki ASCII metni ekrana basar. Tirnak eksikse
   `Error: expected quoted text` yazar; bilinmeyen komut tam olarak
   `Unknown command` yazar.

Bitmap yazilar yatayda 8 piksel, dikeyde 16 piksel kullanir. Harflerin 5x7
desenleri `font.c` dosyasinda tanimlidir ve her bitmap satiri iki piksel
yuksekligine cizilir. Ekran alti dolunca onceki pikseller bir satir yukari
kaydirilir.

Turkce Q duzenindeki `Shift+2` duz tirnak (`"`) uretir. Turkceye ozel
`g, u, s, i, c` tuslari ASCII fontla sinirli oldugu icin ASCII karsiliklarini
yazar; `i` kucuk harf, `I` buyuk harf olarak kullanilir. ASCII disi karakterler
konsola alinmaz. Shift+3 ve benzeri isaretler yalnizca tablodaki ASCII
karsiliklar kadar desteklenir.

## Gereken araclar

Bu ilk adim Linux, macOS veya Windows ana sistem suruculeriyle ilgili degildir.
Windows'ta yalnizca WSL2 Ubuntu terminalinden; Linux'ta dogrudan terminalden
derleyin. x86_64 Linux GCC, ciktiyi freestanding olarak derler.

### Linux

Ubuntu/Debian:

```sh
sudo apt update
sudo apt install build-essential xorriso git
```

Fedora:

```sh
sudo dnf install gcc make xorriso git
```

### Windows (WSL2)

1. Yonetici PowerShell'de `wsl --install -d Ubuntu` komutunu calistirin ve
   gerekirse bilgisayari yeniden baslatin.
2. Ubuntu'yu acip kullanici olusturun.
3. Ubuntu terminalinde araclari kurun:

```sh
sudo apt update
sudo apt install build-essential xorriso git
```

Projeyi WSL Linux dosya sisteminde tutun (ornegin `~/rea-os`). ISO'yu WSL
icerisinde olusturup VirtualBox'a Windows tarafindan gostermek icin dosyayi
`/mnt/c/Users/<kullanici>/Downloads/rea-os.iso` altina kopyalayabilirsiniz.

## Derleme ve ISO

Proje klasorunde:

```sh
make
```

Ilk derlemede Makefile resmi Limine `v8.7.0` ikili dagitimini `limine/` altina
indirip yardimci aracini derler; bu asama icin internet gerekir. Basarili olunca
`rea-os.iso` dosyasi olusur. Temiz derleme icin:

```sh
make clean
make
```

Kabuk ve ASCII bitmap yazılarını sınamak için:

```sh
make test
```

## VirtualBox'ta calistirma

1. **New / Yeni** secin, ad olarak `REA OS` yazin.
2. Type: **Other**, Version: **Other/Unknown (64-bit)** secin. Bu secenek
   gorunmuyorsa VirtualBox'in 64-bit sanallastirma destegini kontrol edin.
3. Bellek: **256 MB** veya daha fazla verin.
4. **Create a virtual hard disk now** secenegini kapatip **Do not add a virtual
   hard disk** secin.
5. VM ayarlarinda **System > Motherboard** altinda **Enable EFI** kapaliysa
   BIOS; aciksa UEFI ile deneyin. ISO ikisini de destekler.
6. **Storage** altinda optik surucuya `rea-os.iso` dosyasini takin.
7. **Display** altinda Video Memory degerini en az **16 MB** yapin. Grafik
   denetleyicisi varsayilan VMSVGA olabilir.
8. **System > Processor** altinda 1 CPU yeterlidir; **Enable I/O APIC**
   ayarini varsayilaninda birakin.
9. Baslatin. Siyah ekranda `/> ` istemi gorunur. `print "merhaba"` yazip
   Enter'a basin.
10. ISO'dan tekrar acmak icin VM'yi kapatin, optik surucudaki ISO'yu birakin
    ve yeniden baslatin. Sanal disk gerekmez.

VirtualBox'ta klavye yakalamak icin VM penceresine tiklayin. VM pencere
icerisindeyken ana makine klavye duzeni degil, konuk tus tarama kodlari
kullanilir.

## Sik gorulen sorunlar

| Sorun | Cozum |
|---|---|
| `make` komutu bulunamiyor | Linux/WSL icin `sudo apt install build-essential` calistirin. |
| `xorriso` bulunamiyor | `sudo apt install xorriso` kurun. |
| Limine indirme hatasi | Interneti kontrol edip `make` komutunu yeniden calistirin. |
| ISO acilmiyor | VM turunu 64-bit Other yapin; once EFI kapali BIOS, sonra EFI acik UEFI deneyin. |
| Siyah ekran, istem yok | VirtualBox ekran belleğini 16 MB yapin; makineyi tamamen kapatip tekrar acin. |
| Klavye yazmiyor | VM penceresine tiklayip tuslari tekrar deneyin; VirtualBox'in klavyeyi yakaladigini kontrol edin. |
| `print` bilinmiyor | Komutu ASCII kucuk harfle yazin: `print "merhaba"`. |
| `Error: expected quoted text` | Metni iki duz cift tirnak arasina alin: `print "merhaba"`. |
| Turkce harf ekranda yok/degisik | Bu adim ASCII ile sinirlidir; Turkce ozel harfler ASCII karsiliklarina cevrilir. |

## Komut defteri

| Komut | Kullanim | Davranis |
|---|---|---|
| `print` | `print "yazi"` | Tirnak icindeki metni yazar. Tirnak yoksa/kusurluysa kisa hata mesaji verir. |

Bunun disindaki her komut `Unknown command` yazar. Bu adimda `help`, `clear`,
`ls`, `cd`, `echo` veya baska bir komut bulunmaz.
