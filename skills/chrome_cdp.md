---
{
  "name": "chrome_cdp",
  "description": "Bot korumali (Cloudflare vb.) sitelerde gercek Chrome profili uzerinden CDP modu kullanimi.",
  "triggers": ["cloudflare", "cdp", "vapi", "useChromeProfile", "real chrome", "gercek chrome"],
  "priority": 5
}
---
CDP modu (Chrome DevTools Protocol):
Bot korumalari (Cloudflare vb.) gecmek icin gercek kullanici Chrome profili uzerinden CDP modu desteklenir. Kisisel hesap (accountId) kullaniyorsan bu skill'i KULLANMA — accountId zaten dogru ayarlari kuruyor.

- "useChromeProfile": true verildiginde sistem Chrome'u --remote-debugging-port ile baslatilip CDP uzerinden surulur. Profil bozulmamasi icin once izole bir klasore kopyalanir.
- "chromeExecutablePath" (opsiyonel; PATH'den otomatik bulunur)
- "chromeUserDataDir" (varsayilan ~/.config/google-chrome)
- "chromeProfileName" (gorunen profil adi ipucu, orn 'Yusuf - Kisi 2')
- "chromeProfileDir" (Default / Profile 1 ...)
- "automationUserDataDir", "chromeDebugPort" (0 ise otomatik), "refreshProfileCopy"
- "headless": false → pencere gorunur acilir; true → --headless=new ile arka planda calisir.

CDP modu ornegi:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "useChromeProfile": true,
    "headless": false,
    "chromeProfileName": "Yusuf - Kisi 2",
    "steps": [
      { "action": "goto", "url": "https://dashboard.vapi.ai/login" },
      { "action": "wait_for_selector", "selector": "input[name='email']" },
      { "action": "type", "selector": "input[name='email']", "text": "admin@example.com" },
      { "action": "type", "selector": "input[name='password']", "text": "..." },
      { "action": "click_first", "selectors": ["button[type='submit']", "button:has-text('Sign in')"] },
      { "action": "wait_for_load_state", "state": "networkidle" },
      { "action": "snapshot", "maxLength": 2000 }
    ]
  }
}
