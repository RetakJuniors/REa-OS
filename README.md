# REA OS - Adim 2

REA OS, x86_64 icin sifirdan yazilan egitim amacli bir cekirdek projesidir.
Limine BIOS/UEFI hibrit ISO'yu acar; cekirdek framebuffer'a metin cizer, PS/2
klavyeyi okur, `print` komutunu calistirir ve `stat` ile CPU/RAM bilgisini verir.

> Bu proje deneyseldir. Sanal makinede ve sanal disksiz kullanin. Cekirdek
> disk, ag ve USB surucusu icermez; diske yazma kodu yoktur.

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
|   |-- memory.h
|   |-- serial.h
|   |-- shell.h
|   |-- stat.h
|   |-- timer.h
|   `-- util.h
|-- src/
|   |-- boot.c
|   |-- console.c
|   |-- font.c
|   |-- interrupts.S
|   |-- kernel.c
|   |-- keyboard.c
|   |-- keyboard_layout.c
|   |-- memory.c
|   |-- serial.c
|   |-- shell.c
|   |-- stat.c
|   |-- timer.c
|   `-- util.c
`-- tests/
    |-- font_test.c
    |-- keyboard_layout_test.c
    |-- memory_test.c
    |-- stat_test.c
    `-- shell_test.c
```

Derleme sirasinda `build/`, `limine/` ve ISO dosyalari olusturulur; bunlar
kaynak dosyalar degil, derleme ciktilaridir. Klavye duzeni `KEYBOARD_LAYOUT`
ile secilir; secenekler ayri derleme klasorlerinde tutulur.

## Dosyalar ne yapiyor?

| Dosya | Aciklama |
|---|---|
| `src/kernel.c` | Framebuffer, RAM yoneticisi ve zamanlayiciyi baslatir; klavyeyi okur. |
| `src/boot.c` | Limine framebuffer, memory map ve HHDM yanitlarini alir. |
| `src/console.c` | Siyah ekran, 8x16 yazi hucreleri ve ekran kaydirmayi yonetir. |
| `src/font.c` | ASCII 32-126 icin projede tanimlanan 5x7 bitmap harfleri tutar. |
| `src/keyboard.c` | PS/2 portlarindan kesmesiz, polling klavye okumasini yapar. |
| `src/keyboard_layout.c` | Set-1 tarama kodlarini secilen Turkce Q veya US QWERTY duzenine cevirir. |
| `src/memory.c` | Limine usable RAM bolgelerini bitmap tabanli fiziksel sayfa yoneticisine aktarir. |
| `src/timer.c` | PIT'i 100 Hz ayarlar, IDT/PIC ile kesme alir ve son 5 saniyeyi olcer. |
| `src/interrupts.S` | Timer kesmesinde yazmaclari koruyup C kesme isleyicisini cagirir. |
| `src/shell.c` | Satiri toplar, Enter/Backspace islemlerini yapar, komutu bulur. |
| `src/stat.c` | `stat -cpu`, `stat -ram` ve `stat all` ciktilarini uretir. |
| `src/serial.c` | COM1 hazirsa hata ayiklama metnini gonderir; portu yapilandirmaz. |
| `src/util.c` | C kutuphanesi olmadan gereken temel bellek/yazi fonksiyonlarini saglar. |
| `include/` | C dosyalarinin ortak fonksiyon bildirimlerini icerir. |
| `limine.conf` | Limine'a ISO'dan hangi cekirdek dosyasini acacagini soyler. |
| `linker.ld` | Cekirdek ve Limine isteklerinin bellek yerlesimini tanimlar. |
| `Makefile` | C ve assembly dosyalarini derler, BIOS/UEFI hibrit ISO olusturur. |

### Onemli kod akisi

1. `kmain()` framebuffer'i, memory map tabanli sayfa yoneticisini ve PIT'i baslatir.
2. Klavye girdisi yokken cekirdek `hlt` ile bekler; PIT kesmesi en gec 10 ms sonra uyandirip klavye polling'i surdurur.
3. Tus haritasi tarama kodunu ASCII karaktere cevirir; Shift ve Caps Lock uygulanir.
4. Enter satiri komut tablosuna gider. Tabloda yalnizca `print` ve `stat` vardir.
5. `stat -cpu` son 5 saniyedeki PIT kesmelerinde cekirdegin `hlt` beklemesinde olup olmadigina gore CPU kullanimini yaklasik hesaplar.
6. `stat -ram`, memory map'teki tam usable sayfalari ve bitmap allocator durumunu raporlar.

Bitmap yazilar yatayda 8 piksel, dikeyde 16 piksel kullanir. Harflerin 5x7
desenleri `font.c` dosyasinda tanimlidir ve her bitmap satiri iki piksel
yuksekligine cizilir. Ekran alti dolunca onceki pikseller bir satir yukari
kaydirilir.

Varsayilan Turkce Q duzenindeki `Shift+2` duz tirnak (`"`) uretir. `US
QWERTY` secenegi standart ABD tus isaretlerini kullanir; bu duzende tirnak
`Shift+'` ile yazilir. Turkce Q duzenindeki Turkceye ozel
`g, u, s, i, c` tuslari ASCII fontla sinirli oldugu icin ASCII karsiliklarini
yazar; `i` kucuk harf, `I` buyuk harf olarak kullanilir. ASCII disi karakterler
konsola alinmaz. Shift+3 ve benzeri isaretler yalnizca tablodaki ASCII
karsiliklar kadar desteklenir.

RAM toplami Limine memory map'indeki tam sayfa hizasina alinmis `usable`
bolgelerden hesaplanir. Ayiricinin bitmap'i icin kullanilan sayfalar da
"kullanilan" sayilir. CPU yuzdesi, 100 Hz PIT kesmelerinde cekirdegin `hlt`
beklemesinde olup olmadigina gore bes saniyelik kayan pencerede tahmin edilir;
bu, genel amacli bir isletim sistemindeki tam CPU muhasebesi degildir.

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
`rea-os.iso` (Turkce Q) dosyasi olusur. US QWERTY ISO'su icin:

```sh
make KEYBOARD_LAYOUT=us
```

Bu komut `rea-os-us.iso` olusturur. Iki klavye duzeninin derleme dosyalari
`build/tr/` ve `build/us/` altinda ayri tutulur. Temiz derleme icin:

```sh
make clean
make
```

Kabuk, ASCII bitmap fontu, stat ciktisi ve fiziksel sayfa ayiricisini sinamak icin:

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
6. **Storage** altinda optik surucuya `rea-os.iso` (Turkce Q) veya
   `rea-os-us.iso` (US QWERTY) dosyasini takin.
7. **Display** altinda Video Memory degerini en az **16 MB** yapin. Grafik
   denetleyicisi varsayilan VMSVGA olabilir.
8. **System > Processor** altinda 1 CPU yeterlidir; **Enable I/O APIC**
   ayarini varsayilaninda birakin. **System > Acceleration** altinda
   **Enable VT-x/AMD-V** acik olmali.
9. Baslatin. Siyah ekranda `/> ` istemi gorunur. `print "merhaba"` yazip
   Enter'a basin; RAM ve CPU icin `stat all` kullanin.
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
| `This CPU does not support 64-bit mode` | Bu hata Limine'da, cekirdek calismaya baslamadan gorunur. VM kapaliyken surumunu **Other/Unknown (64-bit)** secin ve **System > Acceleration** altinda VT-x/AMD-V'i acin. Secenek yoksa ana bilgisayarin BIOS/UEFI ayarlarinda Intel VT-x veya AMD SVM'i etkinlestirip ana bilgisayari yeniden baslatin. Bu hata kendi basina diske yazildigi anlamina gelmez. |
| ISO acilmiyor | VM turunu 64-bit Other yapin; once EFI kapali BIOS, sonra EFI acik UEFI deneyin. |
| Siyah ekran, istem yok | VirtualBox ekran belleğini 16 MB yapin; makineyi tamamen kapatip tekrar acin. |
| Klavye yazmiyor | VM penceresine tiklayip tuslari tekrar deneyin; VirtualBox'in klavyeyi yakaladigini kontrol edin. |
| `print` bilinmiyor | Komutu ASCII kucuk harfle yazin: `print "merhaba"`. |
| `Error: expected quoted text` | Metni iki duz cift tirnak arasina alin: `print "merhaba"`. |
| `RAM: unavailable` | Limine memory map veya HHDM yaniti alinmamistir; seri porttaki `REA OS:` hata satirlarini kontrol edin. |
| `stat -cpu` hep `%0` gosteriyor | Bu adimda baska calisan gorev yoktur; oran son 5 saniyedeki PIT/`hlt` orneklerinden tahmindir. |
| Turkce harf ekranda yok/degisik | Bu adim ASCII ile sinirlidir; Turkce ozel harfler ASCII karsiliklarina cevrilir. |

## Komut defteri

| Komut | Kullanim | Davranis |
|---|---|---|
| `print` | `print "yazi"` | Tirnak icindeki metni yazar. Tirnak yoksa/kusurluysa kisa hata mesaji verir. |
| `stat` | `stat -cpu`, `stat -ram`, `stat all` | CPU yuzdesini, usable RAM toplam/kullanilan/bos miktarini veya ikisini birlikte gosterir. |

Bunun disindaki her komut `Unknown command` yazar. `stat` icin gecersiz secenek
kisa kullanim satiri gosterir. `help`, `clear`, `ls`, `cd`, `echo` veya baska
bir komut bulunmaz.
