---
{
  "name": "google_account",
  "description": "Gmail / Google Drive / Takvim / YouTube kisisel hesap erisimi (WebBrowserTool accountId=google_main).",
  "triggers": ["gmail", "mail", "e-posta", "eposta", "epostam", "mailim", "takvim", "calendar", "drive", "youtube hesab", "google hesab", "kendi hesab", "hesabım", "hesabim"],
  "priority": 8
}
---
Hesap (Account) Modu — Google:
Kullanici Gmail/Google Drive/Google Takvim/YouTube hesabi gibi kisisel Google hesabi gerektiren bir istekte bulundugunda (orn. "mailime bak", "son mailim ne", "takvimimde ne var", "kendi hesabimla") DOGRUDAN WebBrowserTool'u `accountId: "google_main"` parametresiyle cagir.

- Google tarafinda urune dogrudan git: Gmail icin `mail.google.com`, Drive icin `drive.google.com`, Takvim icin `calendar.google.com`. Ara landing page'lerde gereksiz gezinme yapma.
- Google hesap secici veya gecis sayfasi gordugunde bunu runner'dan bekleme; gerekirse `steps` icinde dogrudan hedef URL'ye tekrar `goto` yap ve ancak kullanicinin istedigi urunun selector'u geldikten sonra okumaya basla.

- ASLA kullaniciya "giris yapmami ister misin?", "onayliyor musun?" gibi izin sorma. Tool zaten gerekirse kendisi sorar, oturum varsa otomatik kullanir.
- accountId verildiginde tool arka planda kalici bir tarayici profili (oturum cookie'leri saklanan) acar. Ilk kullanim sirasinda gorunur bir Chromium penceresi acilir ve kullaniciya manuel olarak oturum acmasi icin sorulur. Sonraki cagrilarda oturum otomatik aciktir.
- Sifre, e-posta gibi bilgileri ASLA `steps` icine yazma; bunlar sadece kullanicinin kendi tarayici penceresinde girilir.
- accountId kullaniyorsan `useChromeProfile` ve `headless` alanlarini set etme; arac dogru ayarlari kendisi uygular.

Ornek istek: "gmail hesabimda son mail nedir?" → DOGRUDAN su JSON'u dondur:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "google_main",
    "steps": [
      { "action": "goto", "url": "https://mail.google.com/mail/u/0/#inbox" },
      { "action": "wait_for_selector", "selector": "table[role='grid'] tr", "timeoutMs": 30000 },
      { "action": "extract_text", "selector": "table[role='grid'] tr:nth-child(1)", "maxLength": 500 }
    ]
  }
}

Genel Google hesap erisimi:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "google_main",
    "steps": [
      { "action": "goto", "url": "https://myaccount.google.com/" },
      { "action": "wait_for_timeout", "timeoutMs": 1000 },
      { "action": "click_first", "selectors": [
        { "textSelector": "Continue" },
        { "textSelector": "Devam" }
      ], "requireSuccess": false },
      { "action": "snapshot", "maxLength": 2000 }
    ]
  }
}
