---
{
  "name": "python_web_automation",
  "description": "Web islemlerini ortak Chrome profilli PythonTool runner'lari ile yapma kilavuzu.",
  "triggers": ["web", "site", "browser", "tarayici", "internet", "sayfa", "login", "oturum", "scrape", "ekran goruntusu", "haber", "başlık", "baslik", "oku", ".com", "com.tr", "gazete"],
  "priority": 20,
  "toolReferences": [
    {
      "tool": "PythonTool",
      "path": "shared_profile_web_runner.py",
      "description": "Varsayilan web runner; her zaman account.json icindeki ortak Chrome profilini kullanir.",
      "usage": "runner=shared_profile_web_runner.py, packages=[playwright], args=[--inline-config, <json-config>]"
    },
    {
      "tool": "PythonTool",
      "path": "webbrowser_runner.py",
      "description": "Sadece anonim veya profilden bagimsiz gezinme gerekirse kullanilacak dusuk oncelikli runner.",
      "usage": "runner=webbrowser_runner.py, packages=[playwright], args=[--inline-config, <json-config>]"
    }
  ]
}
---

## PythonTool-first web automation

Web sayfasina gitmek, tiklamak, form doldurmak, ekran goruntusu almak veya site metni okumak gerektiginde PythonTool kullan.

Bu repo icinde varsayilan tercih `shared_profile_web_runner.py` olmalidir. Ortak Chrome profili kullanilabiliyorsa ASLA baska bir profil secme ve ASLA bos Playwright profiliyle baslama. Sadece kullanicinin acikca anonim veya gecici profil istedigi durumlarda `webbrowser_runner.py` kullan.

`shared_profile_web_runner.py`, `account.json` icindeki `defaultSessionBrowserProfileId` profilini otomatik inject eder. Boylece VNC'de acik olan ve sifresiz erisilebilen ayni Chromium profili kullanilir.

Genel web islemi icin varsayilan cagri:

```json
{
  "tool": "PythonTool",
  "arguments": {
    "runner": "shared_profile_web_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"steps\":[{\"action\":\"goto\",\"url\":\"https://example.com\"},{\"action\":\"extract_text\",\"selector\":\"body\",\"maxLength\":2000}],\"headless\":false,\"artifactsDir\":\".voice_agent_browser/artifacts\"}"
    ]
  }
}
```

## Bilinmeyen site kesif akisi

Kullanici belirli bir site icin bilgi istiyor ama o siteye ozel bir skill yoksa once kirilgan CSS tahminleri yapma. Ilk cagriyi kesif icin kur ve varsayilan olarak ortak profili kullan:

```json
{
  "tool": "PythonTool",
  "arguments": {
    "runner": "shared_profile_web_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"headless\":false,\"artifactsDir\":\".voice_agent_browser/artifacts\",\"steps\":[{\"action\":\"goto\",\"url\":\"https://example.com\"},{\"action\":\"dismiss_popups\",\"maxPasses\":3},{\"action\":\"snapshot\",\"maxLength\":6000},{\"action\":\"screenshot\",\"path\":\"discovery.png\",\"fullPage\":true}]}"
    ]
  }
}
```

Sonuctaki `results`, `failedStep`, `rawOutput`, `bodyPreview`, `artifactsDir` ve ekran goruntusunu incele. Sonra ikinci cagrida hangi selector, link, buton veya metin adaylarini kullanacagini buna gore sec.

Kullanici senden ayni istekte hem bir sayfayi kesfetmeni hem de bundan yeni bir yetenek cikarmanı isterse bunu iki ayri is gibi dusunme. Dogru akış su olmalidir:

1. PythonTool ile sayfayi ziyaret et, snapshot ve gerekirse screenshot al.
2. Tool sonucundaki url, title, metin, ekran goruntusu ve gozlenen akislardan ogren.
3. Ayni turda `ProjectFilesTool` ile `skills/` altina yeni bir markdown yetenek dosyasi yaz veya mevcut yetenegi guncelle.
4. Ancak dosya yazildiktan sonra final cevap ver.

Bu durumda sadece sayfayi acip ozet vererek durma. Kullanici acikca yetenek istediyse, kesif sonucu kalici skill dosyasina donusmelidir.

Eger sayfa login veya register duvarina dusuyorsa ve ortak profil buna ragmen iceri almiyorsa su sirayi izle:

1. Ortak profilli `shared_profile_web_runner.py` ile kesfe devam et; gorulen login ekranini ve giris seceneklerini cikar.
2. Site kullanicinin zaten kullandigi bir urunse ve tekrar kullanilacaksa, kullanicinin istedigi anda `site-login.sh` ile ilk manuel girisi yaptir.
3. Sonra ayni site icin reusable skill veya helper script olustur.

Config cok buyurse iki yol var: ya `ProjectFilesTool` ile `scripts/` altinda kisa bir yardimci Python scripti olusturup onu calistir, ya da runner icin JSON config dosyasini yine `scripts/` altinda olusturup ilk arguman olarak ver.

Kodu iyilestirmen gerekiyorsa yalnizca `scripts/` altindaki Python dosyalarini duzenle. Yaptigin degisiklik tekrar kullanilacaksa `ProjectFilesTool` ile ayni kok altina kalici olarak kaydet.

KULLANICI senden bir yetenek olusturmani veya guncellemeni isterse, final cevap vermeden ONCE mutlaka `ProjectFilesTool` ile ilgili `skills/*.md` dosyasini olustur veya guncelle. Sadece "yetenek olusturuyorum" deyip durma; tool cagrisi yapmadan final verme.

Skill yazarken `ProjectFilesTool` icinde tercihen `frontmatter` ve `body` alanlarini ayri ver. `frontmatter` icinde en az `name`, `description`, `triggers`, `priority` olsun. `body` icinde siteye nasil gidilecegi, hangi sayfalarin onemli oldugu, hangi tool/script'in tercih edilecegi ve kesifte ogrendigin somut UI ipuclari yer alsin.

Belirli site veya gorev davranisi icin tekrar kullanilabilir bir kural ogrendiysen `ProjectFilesTool` ile `skills/` altinda yeni bir skill olustur veya mevcut skill'i guncelle. Runner davranisini degistirdikten sonra ayni PythonTool cagrisi ile yeniden dene ve tool sonucundaki `rawOutput`, JSON `output`, `artifactsDir` ve varsa ekran goruntulerine bakarak sonraki adimi sec.
