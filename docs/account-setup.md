# Hesap Kurulumu ve İlk Kimlik Doğrulama

Robot, web sitelerine `WebBrowserTool` aracılığıyla erişir. Her hesap için oturum bilgileri
yerel bir Chromium profil dizinine kaydedilir. **İlk girişin bir kez manuel yapılması** gerekir;
bundan sonra robot headless (görünmez) modda aynı oturumu kullanır.

Bu akış **LinkedIn'e özel değildir**. `account.json` içine eklediğiniz herhangi bir hesap
(`google_main`, `linkedin_main`, `twitter_main`, `github_work`, `notion_personal` gibi)
aynı yöntemle ilk kez bağlanabilir.

---

## 1. Hesap Kaydı (`account.json`)

`account.json` dosyası proje kök dizinindedir ve **gitignore'da** olduğundan commit'lenmez.

```json
{
  "_comment": "Gercek hesap verisi. .gitignore'da; commit'lenmez.",
  "accountsRootDir": "",
  "accounts": {
    "google_main": {
      "displayName": "Google - Kisisel",
      "provider": "google",
      "email": "kullanici@gmail.com",
      "password": "",
      "profileDir": ""
    },
    "linkedin_main": {
      "displayName": "LinkedIn - Kisisel",
      "provider": "linkedin",
      "email": "kullanici@hotmail.com",
      "password": "",
      "profileDir": ""
    }
  }
}
```

### Alan açıklamaları

| Alan | Açıklama |
|---|---|
| `email` / `password` | Yalnızca yerel referans; modele asla iletilmez; boş bırakılabilir |
| `profileDir` | Boş bırakılırsa `~/.voice_agent_browser/profiles/<accountId>` kullanılır |
| `accountsRootDir` | Profil dizinleri için kök; boş bırakılırsa `~/.voice_agent_browser/profiles/` |

`loginUrl`, `loggedInUrl` ve `loginCheckSelector` artık ilgili skill dosyasinin (`skills/google_account.md`, `skills/linkedin_account.md`, vb.) JSON frontmatter'inda `account` blogu altinda tutulur.

### Yeni hesap eklemek

`accounts` bloğuna yeni bir nesne ekleyin; `accountId` (anahtar) benzersiz olmalıdır.
Ardından ilgili bir skill dosyası (`skills/<hesap_adi>.md`) oluşturun.

Bir hesabın bu akışla çalışabilmesi için pratikte şu 3 alan yeterlidir:

| Alan | Neden gerekli |
|---|---|
| Skill frontmatter `account.loginUrl` | İlk görünür giriş sayfasını açmak için |
| Skill frontmatter `account.loggedInUrl` | Giriş sonrası doğru yere yönlenip yönlenmediğini anlamak için |
| Skill frontmatter `account.loginCheckSelector` | Oturum gerçekten açık mı diye DOM üzerinden kontrol etmek için |

Örnek: yarın X / Twitter eklemek isterseniz şu tip bir kayıt yeterlidir:

```json
"twitter_main": {
  "displayName": "X / Twitter - Kisisel",
  "provider": "twitter",
  "email": "",
  "password": "",
  "profileDir": ""
}
```

Ve ilgili skill frontmatter'i su tipte olmalidir:

```json
"account": {
  "id": "twitter_main",
  "loginUrl": "https://x.com/i/flow/login",
  "loggedInUrl": "https://x.com/home",
  "loginCheckSelector": "a[data-testid='AppTabBar_Home_Link'], button[data-testid='SideNav_AccountSwitcher_Button']"
}
```

---

## 2. Hızlı Yol: Tek Komutla İlk Giriş

Artık ilk manuel giriş için önerilen akış tek komuttur:

```bash
cd /home/kufi/workspace/voiceAgent
bash scripts/account-login.sh <accountId>
```

Bu komut şunları otomatik yapar:

- Gerekliyse `noVNC` sanal masaüstünü başlatır.
- `DISPLAY=:99` benzeri görünür tarayıcı ortamını hazırlar.
- Hesabın kalıcı Chromium profil klasörünü açar.
- Login sayfasına gider.
- Siz giriş + 2FA tamamladıktan sonra oturumu profile kaydeder.
- Script bittiğinde başlattığı `noVNC` oturumunu kapatır.

### Hızlı akış

1. Robot üzerinde script'i çalıştırın:

```bash
cd /home/kufi/workspace/voiceAgent
bash scripts/account-login.sh linkedin_main
bash scripts/account-login.sh twitter_main
```

2. Script `DISPLAY` yoksa size SSH tunnel komutunu ve `noVNC` adresini yazdırır.
3. Laptop'ta `http://localhost:6080/vnc.html` sayfasını açın.
4. noVNC içindeki Chromium penceresinde giriş yapın.
5. Terminale geri dönüp Enter'a basın.
6. Script oturumun doğrulandığını söyleyip çıkar.

> `KEEP_NOVNC=1 bash scripts/account-login.sh twitter_main`
> derseniz script bitince `noVNC` kapatılmaz.

---

## 3. Manuel Akışın Detayları — noVNC ile

Robot headless bir Linux ortamında çalıştığında Chromium penceresini doğrudan göremezsiniz.
İlk girişi yapabilmek için **sanal masaüstünü** noVNC üzerinden laptop tarayıcınıza yansıtmanız gerekir.

### Adım 1 — Sanal masaüstünü başlatın (robot üzerinde)

```bash
cd /home/kufi/workspace/voiceAgent
bash scripts/novnc-up.sh
```

Başarılı çıktı örneği:

```
[novnc-up] xvfb started (pid 12345)
[novnc-up] fluxbox started (pid 12346)
[novnc-up] x11vnc started (pid 12347)
[novnc-up] websockify started (pid 12348)

[novnc-up] Sanal masaustu hazir:
  DISPLAY=:99  (Xvfb, 1366x900x24)
  noVNC: http://localhost:6080/vnc.html  (SSH tunnel uzerinden)
```

### Adım 2 — SSH tüneli açın (laptop üzerinde, yeni bir terminal)

```bash
ssh -L 6080:localhost:6080 kufi@<robot-ip>
```

`<robot-ip>` yerine robotun IP adresini yazın (ör. `192.168.1.42`).  
Tünel bağlı kaldığı sürece bağlantı aktif olur; kapatmayın.

### Adım 3 — noVNC sayfasını açın (laptop tarayıcısında)

```
http://localhost:6080/vnc.html
```

Sayfayı açtığınızda robot'un sanal masaüstünü görmüş olmalısınız (boş bir Fluxbox ortamı).

### Adım 4 — Agent'ı sanal DISPLAY ile başlatın (robot üzerinde)

```bash
export DISPLAY=:99
cd /home/kufi/workspace/voiceAgent/build
./cpp_voice_agent
```

> **Not:** `DISPLAY=:99` set edilmezse Chromium sanal masaüstüne değil fiziksel
> ekrana açılmaya çalışır (veya açılamaz).

### Adım 5 — Hesabı kullanan bir komut verin

Mikrofona **hesabı tetikleyen bir komut** söyleyin, örneğin:

| Hesap | Örnek komut |
|---|---|
| Google | *"Gmail'ime bak"* |
| LinkedIn | *"LinkedIn'de bildirimlerime bak"* |
| GitHub | *"GitHub'da repolarımı listele"* |
| X / Twitter | *"Twitter ana sayfama bak"* |

Agent `WebBrowserTool`'u `accountId` ile çağırdığında **oturum açık değilse** otomatik
olarak hesabın `loginUrl`'ine gider ve kullanıcı girişi bekler.

### Adım 6 — noVNC'de Chromium'da oturum açın

noVNC sayfasında Chromium'un açıldığını görürsünüz.

1. **E-posta ve şifreyi** normalde olduğu gibi girin.
2. **İki faktörlü doğrulama** (2FA) varsa telefon/SMS kodunu da tamamlayın.
3. Oturum tamamlandıktan sonra Chromium otomatik kapanır; agent işlemine devam eder.

### Adım 7 — Sonraki kullanımlar için test edin

Aynı komutu tekrar söyleyin. Bu sefer Chromium'un açılmadığını ve agent'ın doğrudan
işlem yaptığını görmeli/duymalısınız — oturum profil dizinine kaydedildi.

### Adım 8 — Sanal masaüstünü kapatın

Giriş tamamlandıktan sonra sanal masaüstüne artık ihtiyaç yoktur:

```bash
bash /home/kufi/workspace/voiceAgent/scripts/novnc-down.sh
```

Bir sonraki çalıştırmada `DISPLAY` ayarı olmadan da headless mod çalışır.

---

## 4. Profil Dizinleri

Oturum bilgileri şu dizinlere kaydedilir:

```
~/.voice_agent_browser/profiles/
  google_main/      ← Google oturum çerezleri
  linkedin_main/    ← LinkedIn oturum çerezleri
  github_main/      ← GitHub oturum çerezleri
  twitter_main/     ← X / Twitter oturum çerezleri
```

Bir hesabın oturumunu sıfırlamak için ilgili dizini silin:

```bash
rm -rf ~/.voice_agent_browser/profiles/linkedin_main
```

Bir sonraki kullanımda ilk giriş akışı tekrar çalışır.

---

## 5. Güvenlik Notları

- `account.json` `.gitignore`'dadır; **asla commit'lemeyin**.
- `account.example.json` commit'lenebilir — şifre/e-posta içermez, yalnızca şablon.
- Profil dizinleri Chromium çerezleri içerir; şifrelenmiş disk veya kişisel dizinde tutun.
- Chromium, hesap profillerini izole sandbox'ta çalıştırır; sistem Chrome profiliyle karışmaz.
