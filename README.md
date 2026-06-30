# sebnemozeski_c_git
# mygit — C ile Yazılmış Basit Git İstemcisi

Bu proje, Git'in temel iç işleyişini (object model, sıkıştırma, hashleme, tree yapısı) sıfırdan C ile yeniden inşa etmek için yazıldı. Amaç Git'i kullanmak değil, Git'in *nasıl çalıştığını* anlamaktı — bu yüzden gerçek `.git` formatına %100 uyumlu, kendi objelerini kendisi oluşturup okuyabilen bir CLI ortaya çıktı.

## Neden bu proje?

Git'i her gün kullanıyoruz ama çoğu zaman bir kara kutu gibi davranıyor: `commit` dediğimizde arka planda ne oluyor? Bir dosya nasıl "blob"a dönüşüyor? Tree'ler nasıl iç içe geçiyor? Bu sorulara cevap aramak için CodeCrafters'ın "Build Your Own Git" zorluğunu temel alarak, Git'in obje deposunu (object store), SHA-1 tabanlı içerik adresleme sistemini ve zlib sıkıştırmasını adım adım kendi ellerimle yazdım.

## Desteklenen Komutlar

| Komut | Ne işe yarar |
|---|---|
| `init` | `.git`, `.git/objects`, `.git/refs` dizinlerini oluşturur ve `HEAD` dosyasını `refs/heads/main`'e işaret edecek şekilde ayarlar |
| `cat-file -p <sha>` | Verilen SHA-1'e ait objeyi diskten okur, zlib ile açar ve içeriğini ekrana basar |
| `hash-object -w <dosya>` | Bir dosyayı blob objesine çevirir, SHA-1'ini hesaplar, sıkıştırıp diske yazar |
| `ls-tree --name-only <sha>` | Bir tree objesini açıp içindeki dosya/dizin isimlerini listeler |
| `write-tree` | Mevcut dizini (alt dizinler dahil) recursive olarak tarar, her dosya için blob, her dizin için tree objesi üretir |
| `commit-tree <tree> -p <parent> -m <mesaj>` | Verilen tree'den, parent commit'ten ve mesajdan bir commit objesi oluşturur |

## Nasıl Çalışıyor?

### Obje Deposu (Object Store)

Git'teki her şey — dosya içeriği, dizin yapısı, commit geçmişi — aslında `.git/objects/` altında saklanan, SHA-1 hash'iyle adreslenen küçük dosyalardır. `write_object_to_disk` fonksiyonu bu mantığın kalbinde:

1. Veriye `<tip> <boyut>\0<içerik>` formatında bir header ekler
2. SHA-1 hash'ini hesaplar (`openssl/sha.h`)
3. zlib ile sıkıştırır (`compress`)
4. Hash'in ilk iki karakterini klasör adı, kalan 38 karakterini dosya adı olarak kullanıp diske yazar (`.git/objects/ab/cdef...`)

Bu üç obje tipi (blob, tree, commit) hepsi aynı bu fonksiyonu kullanıyor — sadece header'daki tip ve içerik farklı.

### Blob: Dosya İçeriği

`hash_blob_file` bir dosyayı okuyup `blob <boyut>\0` header'ı ile birleştirir ve `write_object_to_disk`'e gönderir. Git'te bir dosyanın adı veya konumu önemli değildir — sadece içeriği hash'lenir, bu yüzden aynı içerikli iki dosya aynı blob'u paylaşır.

### Tree: Dizin Yapısı

`write_tree_recursive`, bulunduğu dizindeki her girdiyi tarar:
- Dizinse, kendi içine recursive olarak iner ve bir alt-tree SHA'sı üretir
- Dosyaysa, `hash_blob_file` ile blob'a çevirir
- Çalıştırılabilir dosyalar `100755`, normal dosyalar `100644`, dizinler `40000` mode koduyla işaretlenir

Girdiler isimlerine göre `qsort` ile sıralanır (Git'in tree formatı sıralı girdi bekler), ardından her girdi `<mode> <isim>\0<20 byte ham SHA-1>` formatında birleştirilip tree objesi olarak yazılır.

### Commit: Geçmişin Bir Anı

`commit-tree`, bir tree SHA'sını, parent commit SHA'sını ve mesajı alıp Git'in commit formatına uygun metni (`tree`, `parent`, `author`, `committer`, boş satır, mesaj) oluşturur ve yine `write_object_to_disk` ile objeye çevirir.

### Okuma Tarafı: cat-file ve ls-tree

`cat-file`, diskteki sıkıştırılmış objeyi `uncompress` ile açar, header'ın bitişini (`\0`) bulup içeriği yazdırır. `ls-tree` de benzer şekilde tree objesini açar, ama içeriği `mode/isim/sha` üçlüleri halinde parse ederek sadece isimleri listeler — her girdiden sonra null karakteri + 20 byte'lık ham SHA-1'i atlayarak bir sonraki girdiye geçer.

## Kullanılan Kütüphaneler

- **`zlib`** — obje sıkıştırma/açma (`compress`, `uncompress`)
- **`openssl/sha.h`** — SHA-1 hash hesaplama
- **`dirent.h`** — dizin tarama (`opendir`, `readdir`)
- **`sys/stat.h`** — dosya/dizin türü ve izin kontrolü
- **`libcurl`** — ileri seviye komutlar için (ör. clone) ağ üzerinden veri çekme

## Derleme

```bash
gcc -o mygit main.c -lz -lssl -lcrypto -lcurl
```

## Örnek Kullanım

```bash
./mygit init
echo "merhaba dünya" > test.txt
./mygit hash-object -w test.txt
./mygit write-tree
./mygit commit-tree <tree_sha> -p <parent_sha> -m "ilk commit"
```

## Öğrenilen Şeyler / Bilinen Sınırlamalar

- Tree objelerinin boyutu şu an sabit bir buffer'a (`1024*1024`) yazılıyor — çok büyük dizinlerde taşma riski var, dinamik büyüyen bir buffer'a geçmek bir sonraki adım.
- `cat-file` ve `ls-tree` da benzer şekilde sabit boyutlu (`1024*1024`) açma buffer'ı kullanıyor.
- `commit-tree`'deki format string'inde küçük bir typo var (`\nauthor` yerine `nauthor` yazılmış) — bu satır gözden geçirilmeli.
- Şu an sadece tek parent destekleniyor (merge commit'ler için çoklu parent yok).

---

Bu proje [CodeCrafters - Build Your Own Git](https://codecrafters.io) zorluğu temel alınarak, Git'in iç mimarisini öğrenmek amacıyla geliştirilmiştir.
(son task olan 'clone' yapılmamıştır.)
