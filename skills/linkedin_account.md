---
{
  "name": "linkedin_account",
  "description": "LinkedIn kisisel hesap erisimi — baglanti istekleri, mesajlar, profil, is ilanlari (WebBrowserTool accountId=linkedin_main).",
  "triggers": ["linkedin", "linkin", "baglanti", "baglantı", "network", "mesaj linkedin", "iş ilanı", "is ilani", "profil linkedin", "linkedin mesaj", "linkedin profil", "linkedin hesab", "linkedin'de"],
  "account": {
    "id": "linkedin_main",
    "loginUrl": "https://www.linkedin.com/login",
    "loggedInUrl": "https://www.linkedin.com/feed/",
    "loginCheckSelector": ".global-nav__me-photo, .global-nav__primary-link[href*='/feed'], a[href='/feed/'], nav[aria-label*='Primary']"
  },
  "priority": 8
}
---
Hesap (Account) Modu — LinkedIn:
* Sayfa ilk açıldığında reklam / restore / interstitial sayfası çıkabilir. Bu, oturumun kapalı olduğu anlamına gelmez. Böyle bir durumda WebBrowserTool `steps` içine ilk iş olarak `click_first` ekleyip "Back to LinkedIn", "Restore", "Continue to LinkedIn" benzeri butonlarla ana uygulamaya dön; bu recovery mantığını runner'dan bekleme.
* LinkedIn'de ASLA "önce biraz dolaşayım, ikonlara bakayım" yaklaşımı kullanma. hangi işlemi istiyorsa doğrudan onu yap. 
* Kullanıcı bildirimlerime bak derse DOGRUDAN https://www.linkedin.com/notifications/ sayfasına git; messaging ikonuna, arama kutusuna veya başka menülere tıklayarak keşif yapma.
* Kullanıcı mesajlarıma bak derse DOGRUDAN `https://www.linkedin.com/messaging/` sayfasına git; başka ikonlara tıklayarak gezinme.

- accountId verildiginde tool arka planda kalici bir tarayici profili acar. Ilk kullanim sirasinda gorunur bir Chromium penceresi acilir ve kullaniciya manuel olarak LinkedIn'e giris yapmasi icin soylenir. Sonraki cagrilarda oturum otomatik aciktir.
- Sifre, e-posta gibi bilgileri ASLA `steps` icine yazma.
- accountId kullaniyorsan `useChromeProfile` ve `headless` alanlarini set etme.

LinkedIn feed / ana sayfa:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "linkedin_main",
    "steps": [
      { "action": "goto", "url": "https://www.linkedin.com/feed/" },
      { "action": "click_first", "selectors": [
        { "textSelector": "Back to LinkedIn" },
        { "textSelector": "Restore" },
        { "textSelector": "Continue to LinkedIn" },
        { "textSelector": "Geri dön" },
        { "textSelector": "Geri don" }
      ], "requireSuccess": false },
      { "action": "dismiss_popups", "maxPasses": 2 },
      { "action": "wait_for_selector", "selector": ".feed-container-theme, main", "timeoutMs": 30000 },
      { "action": "snapshot", "maxLength": 3000 }
    ]
  }
}

LinkedIn bildirimleri:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "linkedin_main",
    "steps": [
      { "action": "goto", "url": "https://www.linkedin.com/notifications/" },
      { "action": "click_first", "selectors": [
        { "textSelector": "Back to LinkedIn" },
        { "textSelector": "Restore" },
        { "textSelector": "Continue to LinkedIn" },
        { "textSelector": "Geri dön" },
        { "textSelector": "Geri don" }
      ], "requireSuccess": false },
      { "action": "dismiss_popups", "maxPasses": 2 },
      { "action": "wait_for_selector", "selector": ".nt-card-list, .notification-card, main", "timeoutMs": 30000 },
      { "action": "extract_text", "selector": ".nt-card-list, main", "maxLength": 2500 }
    ]
  }
}

LinkedIn mesajları:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "linkedin_main",
    "steps": [
      { "action": "goto", "url": "https://www.linkedin.com/messaging/" },
      { "action": "click_first", "selectors": [
        { "textSelector": "Back to LinkedIn" },
        { "textSelector": "Restore" },
        { "textSelector": "Continue to LinkedIn" },
        { "textSelector": "Geri dön" },
        { "textSelector": "Geri don" }
      ], "requireSuccess": false },
      { "action": "dismiss_popups", "maxPasses": 2 },
      { "action": "wait_for_selector", "selector": ".msg-conversations-container, .msg-overlay-list-bubble, main", "timeoutMs": 30000 },
      { "action": "snapshot", "maxLength": 3000 }
    ]
  }
}
