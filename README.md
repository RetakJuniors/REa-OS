# REA OS - v0.0.8a

REA OS v0.0.8a, x86_64 icin sifirdan yazilan egitim amacli bir cekirdek projesidir.
Limine BIOS/UEFI hibrit ISO'yu acar; cekirdek framebuffer'a metin cizer, PS/2
klavye ve fareyi kesmelerle okur, RAMFS komutlarini, `print`/`stat` ve bellek/
gorev izleme komutlarini calistirir.

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
|-- fonts/
|   |-- Inter_18pt-Regular.ttf
|   `-- OFL.txt
|-- assets/
|   `-- cursors/
|       `-- *.png
|-- include/
|   |-- boot.h
|   |-- console.h
|   |-- cursor.h
|   |-- gdt.h
|   |-- heap.h
|   |-- idt.h
|   |-- keyboard.h
|   |-- memory.h
|   |-- pmm.h
|   |-- mouse.h
|   |-- paging.h
|   |-- ramfs.h
|   |-- pic.h
|   |-- pit.h
|   |-- rtc.h
|   |-- serial.h
|   |-- shell.h
|   |-- stat.h
|   |-- system.h
|   |-- task.h
|   |-- timer.h
|   `-- util.h
|-- src/
|   |-- boot.c
|   |-- console.c
|   |-- cursor.c
|   |-- cursor_data.c
|   |-- cursor_data.h
|   |-- font.c
|   |-- font.h
|   |-- gdt.c
|   |-- heap.c
|   |-- idt.c
|   |-- isr.S
|   |-- kernel.c
|   |-- keyboard.c
|   |-- keyboard_layout.c
|   |-- memory.c
|   |-- pmm.c
|   |-- mouse.c
|   |-- paging.c
|   |-- ramfs.c
|   |-- pic.c
|   |-- pit.c
|   |-- rtc.c
|   |-- scheduler.c
|   |-- serial.c
|   |-- shell.c
|   |-- stat.c
|   |-- system.c
|   |-- task.c
|   |-- timer.c
|   |-- context.S
|   `-- util.c
`-- tests/
|   |-- console_test.c
|   |-- font_test.c
|   |-- keyboard_layout_test.c
|   |-- memory_test.c
|   |-- vmm_test.c
|   |-- scheduler_test.c
|   |-- stat_test.c
|   `-- shell_test.c
`-- tools/
    |-- png2c.py
    `-- ttf2c.py
```

Derleme sirasinda `build/`, `limine/` ve ISO dosyalari olusturulur; bunlar
kaynak dosyalar degil, derleme ciktilaridir. Klavye duzeni `KEYBOARD_LAYOUT`
ile secilir; secenekler ayri derleme klasorlerinde tutulur.

## Dosyalar ne yapiyor?

| Dosya | Aciklama |
|---|---|
| `src/kernel.c` | GDT, IDT, PIC, PIT, RAM yoneticisi ve fareyi baslatir; ana dongude klavye/fare verisini isler. |
| `src/gdt.c` | Kernel kod/veri bolumlerini, TSS'yi ve double-fault yiginini kurar. |
| `src/paging.c` | Limine'in etkin 4/5 seviyeli sayfa tablolarinda 4 KiB eslemeleri olusturur, kaldirir ve cevirir. |
| `src/heap.c` | Sayfa ayiricidan heap sayfalari alir; `kmalloc`/`kfree` ile bloklari boler ve birlestirir. |
| `src/ramfs.c` | Bellek ici dosya/dizin agacini, yollarini, dosya veri depolamasini ve 8 MiB toplam sinirini yonetir. |
| `src/rtc.c` | CMOS RTC registerlarini yalnizca okuyarak BCD/binary ve 12/24 saat formatini cevirir. |
| `src/system.c` | CPUID CPU/hypervisor tespiti, uptime/ekran/RAM bilgisi ve sanal makine reboot/poweroff islerini saglar. |
| `src/idt.c` | 256 IDT girisini kurar, istisnalari raporlar ve IRQ'lari ilgili surucuye yollar. |
| `src/isr.S` | Istisna/IRQ girislerini, yazmac kaydetme-kurtarmayi ve GDT'yi yukleme kodunu saglar. |
| `src/pic.c` | 8259 PIC'i 32-47 vektorlerine tasir, IRQ maskelerini ve EOI sinyallerini yonetir. |
| `src/pit.c` | PIT'i saniyede 100 kesmeye ayarlar ve zamanlayici tikini bildirir. |
| `src/boot.c` | Limine framebuffer, memory map ve HHDM yanitlarini alir. |
| `tools/ttf2c.py` | Pillow ile TTF gliflerini build-time alpha piksel tablosuna cevirir. |
| `src/font.c` | Betigin olusturdugu salt okunur Inter glifleri ve ikili aramayi tutar. |
| `src/font.h` | Glif boyutlari, bearing, advance ve font metriklerini bildirir. |
| `src/console.c` | UTF-8 metni alpha karistirmayla cizer; orantili satir ve imleci yonetir. |
| `src/cursor.c` | Alpha ARGB fare okunu cizer; fare hareketinde eski pikselleri geri yukler. |
| `src/cursor_data.c` | PNG betiginin olusturdugu salt okunur fare imleci piksel tablosu. |
| `tools/png2c.py` | PNG fare imleclerini derleme oncesi ARGB C tablosuna cevirir. |
| `src/mouse.c` | IRQ12 verisini halka tamponuna alir; fare paketini ana dongude cozer. |
| `src/keyboard.c` | IRQ1 tarama kodlarini halka tamponuna alir; klavye girdisini ana dongude cozer. |
| `src/keyboard_layout.c` | Set-1 tarama kodlarini secilen Turkce Q veya US QWERTY duzenine cevirir. |
| `src/memory.c` | Limine usable RAM bolgelerini bitmap tabanli fiziksel sayfa yoneticisine aktarir. |
| `src/pmm.c` / `include/pmm.h` | Bitmap sayfa ayiricisini `pmm_alloc_page`/`pmm_free_page` arayuzuyle sunar; yalnizca HHDM adreslerini kabul eder. |
| `src/scheduler.c` / `include/task.h` | Kernel ana gorevi ve ayri yiginli kernel thread'ler icin cooperative round-robin zamanlamayi saglar. |
| `src/task.c` | Gorev durumlarini (`READY`, `RUNNING`, `BLOCKED`, `EXITED`) metne cevirir. |
| `src/context.S` | Gorevler arasinda korunmasi gereken yazmaclari ve yigin gostergesini degistirir. |
| `src/timer.c` | PIT tiklerini ve `hlt` beklemesini sayarak son 5 saniyelik CPU tahmini yapar. |
| `src/shell.c` | Satiri toplar, Enter/Backspace islemlerini yapar, komutu bulur. |
| `src/stat.c` | `stat -cpu`, `stat -ram`, `stat -task` ve `stat all` ciktilarini uretir. |
| `src/serial.c` | COM1 hazirsa hata ayiklama metnini gonderir; portu yapilandirmaz. |
| `src/util.c` | C kutuphanesi olmadan gereken temel bellek/yazi fonksiyonlarini saglar. |
| `include/` | C dosyalarinin ortak fonksiyon bildirimlerini icerir. |
| `limine.conf` | Limine'a ISO'dan hangi cekirdek dosyasini acacagini soyler. |
| `linker.ld` | Cekirdek ve Limine isteklerinin bellek yerlesimini tanimlar. |
| `Makefile` | C ve assembly dosyalarini derler, BIOS/UEFI hibrit ISO olusturur. |

### Onemli kod akisi

1. `kmain()` framebuffer'i, memory map tabanli PMM'i, Limine'in etkin paging
   tablolarini, kernel heap'ini, GDT/TSS, IDT, PIC, PIT ve cooperative
   scheduler'i baslatir; PS/2 fareyi i8042 yardimci portundan hazirlar.
2. IRQ1 ve IRQ12 isleyicileri yalnizca gelen byte'i halka tamponuna koyup EOI
   gonderir. Tus/fare paketi cozme, komut ve cizim ana dongude yapilir.
3. Ana dongu guvenli noktada hazir kernel thread'lere `task_yield` ile sira
   verir. Girdi tamponlari ve calisabilir gorevler yoksa cekirdek kesmeleri
   acip `hlt` ile uyur; PIT'in IRQ0 tikleri en gec 10 ms icinde uyandirir.
4. Tus haritasi tarama kodunu ASCII karaktere cevirir; Shift ve Caps Lock uygulanir.
5. Enter satiri ortak komut tablosuna gider; RAMFS, sistem ve bellek/gorev komutlari burada kayitlidir.
6. `stat -cpu` son 5 saniyedeki PIT tiklerinde cekirdegin `hlt` beklemesinde
   olup olmadigina gore CPU kullanimini yaklasik hesaplar.
7. `stat -ram`, memory map'teki tam usable sayfalari ve bitmap allocator durumunu raporlar.

Komut isteminin sonunda yaklasik her 500 ms'de bir gorunup kaybolan dikey blok
vardir. PIT'in 100 Hz tikleri kullanilir; baska bir zamanlayici veya komut
eklenmez. 64x64 fare oku ekranda yarim boyutta (32x32) cizilir, hotspot'a gore
yerlestirilir ve hareket ettikce altindaki pikselleri saklayip geri yukler. Sol,
sag ve orta dugme durumu bellekte tutulur; tiklama davranisi yoktur.

Inter TTF dosyasi yalnizca derleme oncesinde Python betigiyle okunur. Cekirdek
TTF ayrıştırma yapmaz: gliflerin alpha piksellerini, olculerini ve taban cizgisi
bilgilerini salt okunur C dizilerinden alir. Ekran ciktilari UTF-8 olarak
cozulur; bozuk UTF-8 dizisi `?` olarak cizilir. Glifler orantili yerlestirilir
ve ekran fontun satir yuksekligi kadar kaydirilir. Komut istemi imleci yanip
soner; Backspace son girilen glifin advance degeri kadar geri gider.

Varsayilan Turkce Q duzenindeki `Shift+2` duz tirnak (`"`) uretir. `US
QWERTY` secenegi standart ABD tus isaretlerini kullanir; bu duzende tirnak
`Shift+'` ile yazilir. Turkce Q duzenindeki Turkceye ozel
`g, u, s, i, c` tuslari ASCII fontla sinirli oldugu icin ASCII karsiliklarini
yazar; `i` kucuk harf, `I` buyuk harf olarak kullanilir. ASCII disi karakterler
konsola alinmaz. Shift+3 ve benzeri isaretler yalnizca tablodaki ASCII
karsiliklar kadar desteklenir.

CPU istisnalari (0-31) istisna adini, hata kodunu ve RIP degerini ekrana ve
COM1'e yazar, sonra cekirdegi durdurur. Double fault ayri bir TSS/IST yigininda
islenir. PIC, IRQ0-15'i 32-47 vektorlerine tasir; bu adimda PIT, klavye ve fare
kesmeleri kullanilir.

Limine cekirdegi uzun modda ve paging acik olarak baslatir. Paging modulu mevcut
CR3 sayfa tablolarini korur; 4 veya 5 seviyeli tablolar uzerinde yeni 4 KiB
eslemeleri ekler/siler ve sanal adresi fiziksel adrese cevirebilir. Kernel heap'i
`0xffffc00000000000` sanal adresinden baslar, ihtiyac oldukca PMM'den fiziksel
sayfa alir ve en fazla 32 MiB veya baslangictaki bos RAM'in %25'i kadar buyur.
Ornek API: `kmalloc(128)` ve `kfree(ptr)`. Scheduler cooperative'dir: kernel
thread'leri yalnizca `task_yield`/`task_sleep` gibi cagri noktalarinda gorev
degistirir; IRQ handler'lari yigin degistirmez. Limine'in CR3'i korunur; VMM
yalnizca mevcut 4/5 seviyeli tablolara 4 KiB eslemeler ekler veya kaldirir.

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
sudo apt install build-essential xorriso git python3 python3-pil
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
sudo apt install build-essential xorriso git python3 python3-pil
```

Projeyi WSL Linux dosya sisteminde tutun (ornegin `~/rea-os`). ISO'yu WSL
icerisinde olusturup VirtualBox'a Windows tarafindan gostermek icin dosyayi
`/mnt/c/Users/<kullanici>/Downloads/rea-os.iso` altina kopyalayabilirsiniz.

## Derleme ve ISO

Proje klasorunde:

```sh
python3 tools/ttf2c.py --font fonts/Inter_18pt-Regular.ttf --size 16
python3 tools/png2c.py
make
```

Fare gorsellerini `assets/cursors/` altina PNG olarak koyun (`assets/cursor/`
klasor adini da otomatik tanir). `hotspots.txt` varsa her satirdaki dosya adi,
x ve y degerleri de kullanilir. PNG betigi tum
gorselleri `src/cursor_data.c` ve `.h` dosyalarina cevirir; dosya adinda
`arrow`, `pointer`, `normal` veya `default` gecen ilk gorseli varsayilan ok
secer. Uygun isim yoksa alfabetik ilk dosyayi kullanir ve seri/terminal
ciktisinda bildirir. Hotspot varsayilan olarak `(0,0)`'dir; degistirmek icin:

```sh
python3 tools/png2c.py --hotspot arrow.png:3,2
```

Komut secenegini degistirdiginiz dosya adiyla kullanin. Betik sadece PNG
okur; PNG'de alpha yoksa, goruntu 64x64'ten buyukse veya baska dosya turleri
bulursa acik uyari verir. SVG, CUR, ICO ve diger formatlar desteklenmez.

Betik varsayilan olarak `fonts/Inter-Regular.ttf` dosyasini arar; bu depodaki
fontun gercek adi `Inter_18pt-Regular.ttf` oldugundan komutta `--font` verilir.
Boyut varsayilani 16 pikseldir. Betik `src/font.c` ile `src/font.h` dosyalarini
yeniden uretir. Pillow yoksa Ubuntu/WSL icin `sudo apt install python3-pil`
kurun. Mevcut dosyalar 95 ASCII, 96 Latin-1 ve 128 Latin Extended-A glifinden
olusan 319 karakteri kapsar; fontta bulunmayan glifleri betik uyarir ve `?`
resmini kullanir.

Ilk derlemede Makefile resmi Limine `v8.7.0` ikili dagitimini `limine/` altina
indirip yardimci aracini derler; bu asama icin internet gerekir. Basarili olunca
`rea-os.iso` (Turkce Q) dosyasi olusur. Fontu degistirmediysen her derlemede
betigi yeniden calistirmak gerekmez. US QWERTY ISO'su icin:

```sh
make KEYBOARD_LAYOUT=us
```

Bu komut `rea-os-us.iso` olusturur. Iki klavye duzeninin derleme dosyalari
`build/tr/` ve `build/us/` altinda ayri tutulur. Temiz derleme icin:

```sh
make clean
make
```

Kabuk, cizim, PMM, VMM, scheduler ve RAMFS testlerini calistirmak icin:

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
kullanilir. Fareyi yakalamak icin pencere icine tiklayin; serbest birakmak
icin VirtualBox Host tusunu (varsayilan olarak sag Ctrl) kullanin.

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
| Fare oku hareket etmiyor | VirtualBox penceresine tiklayip fareyi yakalayin; i8042 uyumlu PS/2 fare aygitinin etkin oldugunu kontrol edin. Seri cikti `PS/2 mouse initialization failed` ise VM'yi kapatip yeniden baslatin. |
| Klavye/fare girdisi kesildi | Seri portta `IDT loaded`, `PIC remapped` ve `PIT started` satirlarini arayin; bu altyapi baslatilmadan IRQ girdisi calismaz. |
| Istisna ekrani gorunuyor | Seri porttaki istisna adini, hata kodunu ve RIP'i kaydedin; cekirdek bilerek `hlt` durumunda durur. |
| Heap veya sanal esleme baslatilamadi | Seri porttaki `paging manager`/`kernel heap` hata satirini kontrol edin; sayfa ayiricisi ve Limine HHDM yaniti gerekli. |
| `assets/cursors` icinde PNG yok | PNG gorsellerini bu klasore koyup `python3 tools/png2c.py` komutunu yeniden calistirin. |
| PNG disi cursor formati uyarisi | Betik yalnizca PNG okur; AI, EPS, JPEG, SVG, CUR veya ICO dosyalari tabloya alinmaz. PNG surumlerini ekleyin. |
| PNG alpha/boyut uyarisi | Saydamlik kanali olmayan gorsel opak olur. 64x64'ten buyuk gorseller calismaya devam eder ancak uyarilir. |
| `print` bilinmiyor | Komutu ASCII kucuk harfle yazin: `print "merhaba"`. |
| `Error: expected quoted text` | Metni iki duz cift tirnak arasina alin: `print "merhaba"`. |
| `No module named PIL` | Ubuntu/WSL icin `sudo apt install python3-pil`; diger sistemlerde `python3 -m pip install Pillow`. |
| `font file not found` | Var olan adla `--font fonts/Inter_18pt-Regular.ttf` secenegini verin. |
| Glif uyarisi | Betik desteklenen aralikta fontta olmayan codepoint'i bildirir ve bu glifi `?` sekline cevirir. |
| `RAM: unavailable` | Limine memory map veya HHDM yaniti alinmamistir; seri porttaki `REA OS:` hata satirlarini kontrol edin. |
| `stat -cpu` hep `%0` gosteriyor | Oran son 5 saniyedeki PIT/`hlt` orneklerinden tahmindir; CPU bos beklerken sifira yaklasmasi beklenir. |
| Turkce harf ekranda yok/degisik | Bu adim ASCII ile sinirlidir; Turkce ozel harfler ASCII karsiliklarina cevrilir. |

## Komut defteri

| Komut | Kullanim | Davranis |
|---|---|---|
| `print` | `print "yazi"` | Tirnak icindeki metni yazar. Tirnak yoksa/kusurluysa kisa hata mesaji verir. |
| `stat` | `stat -cpu`, `stat -ram`, `stat -task`, `stat all` | CPU, usable RAM ve gorev durumlarini tek tek veya birlikte gosterir. |
| `ps` | `ps` | Etkin kernel gorevlerini ID, ad, durum ve yigin boyutuyla listeler. |
| `mem` | `mem` | Fiziksel RAM, sayfa sayilari ve kernel heap kullanimini gosterir. |
| `ls` | `ls [yol]` | Ada gore dosya/dizin listesini boyutlariyla gosterir. |
| `cd` | `cd [yol]` | Dizin degistirir; argumansiz komut kok dizine gider. |
| `pwd` | `pwd` | Gecerli ramfs dizinini yazdirir. |
| `mkdir` | `mkdir <yol>` | Tek seviye dizin olusturur. |
| `rm` | `rm <yol>` | Dosya veya bos dizini siler. |
| `cat` | `cat <dosya>` | Dosya icerigini yazdirir. |
| `wr` | `wr <dosya> "metin"` | Dosya olusturur veya ustune yazar. |
| `ap` | `ap <dosya> "metin"` | Dosyanin sonuna metin ekler. |
| `cp` | `cp <kaynak> <hedef>` | Dosya kopyalar. |
| `mv` | `mv <kaynak> <hedef>` | Dosya/dizin tasir veya yeniden adlandirir. |
| `help` | `help` | Komut tablosundan tum komutlari listeler. |
| `cls` | `cls` | Ekrani temizler. |
| `ver` | `ver` | REA OS surumunu yazdirir. |
| `sys` | `sys` | CPU, hypervisor, RAM, ekran, uptime ve surum bilgisi verir. |
| `up` | `up` | Calisma suresini saat/dakika/saniye olarak verir. |
| `dt` | `dt` | CMOS RTC'den tarih ve saati okur. |
| `rb` | `rb` | 0x64 reset portuna; olmazsa triple fault'a dener. |
| `off` | `off` | Desteklenen VirtualBox/QEMU hipervisorunda sanal makineyi kapatir. |

Bilinmeyen her komut `Unknown command` yazar. `stat` gecersiz kullanimi
kisa kullanim satiri gosterir.
