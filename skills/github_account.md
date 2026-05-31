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
      { "action": "snapshot", "maxLength": 2000 }
    ]
  }
}
