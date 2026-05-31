---
{
  "name": "github_account",
  "description": "GitHub kisisel hesap erisimi (WebBrowserTool accountId=github_main).",
  "triggers": ["github", "repo", "repolarım", "repolarim", "pull request", "issue", "commit"],
  "priority": 7
}
---
Hesap (Account) Modu — GitHub:
Kullanici GitHub repolarim/issue'larim/PR'larim gibi kisisel GitHub hesabi gerektiren bir istekte bulundugunda DOGRUDAN WebBrowserTool'u `accountId: "github_main"` parametresiyle cagir.
Sayfaya girdiğinde "Manage cookie preferences" modal'i veya cookie tercih kutusu çıkarsa bunu runner'dan bekleme. Gerekiyorsa `steps` içine modal kapatma / kabul adimlarini acikca ekle: modal'i bekle, gerekirse scroll yap, tum ilgili secenekleri accept et ve sonra "Save changes" veya benzeri butona bas.


- Izin sorma; tool oturum yoksa kendisi soracaktir.
- Kimlik bilgilerini steps icine ASLA yazma.
- `useChromeProfile`/`headless` alanlarini set etme.

Ornek:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "github_main",
    "steps": [
      { "action": "goto", "url": "https://github.com/issues" },
      { "action": "click_first", "selectors": [
        { "textSelector": "Accept all cookies" },
        { "textSelector": "Accept all" },
        { "textSelector": "Save changes" },
        { "textSelector": "Save preferences" }
      ], "requireSuccess": false },
      { "action": "snapshot", "maxLength": 2000 }
    ]
  }
}

GitHub cookie modal'i belirginse daha acik bir plan kur:
{
  "tool": "WebBrowserTool",
  "arguments": {
    "accountId": "github_main",
    "steps": [
      { "action": "goto", "url": "https://github.com/issues" },
      { "action": "wait_for_timeout", "timeoutMs": 1200 },
      { "action": "click_first", "selectors": [
        { "textSelector": "Manage cookie preferences" },
        { "textSelector": "Cookie preferences" }
      ], "requireSuccess": false },
      { "action": "scroll", "x": 0, "y": 1200 },
      { "action": "click_first", "selectors": [
        { "textSelector": "Accept all cookies" },
        { "textSelector": "Accept all" },
        { "textSelector": "Save changes" },
        { "textSelector": "Save preferences" }
      ], "requireSuccess": false },
      { "action": "snapshot", "maxLength": 2000 }
    ]
  }
}
