---
{
  "name": "google_account",
  "description": "Gmail / Google Drive / Takvim / YouTube icin ortak Chrome profilli explicit script akisi.",
  "triggers": ["gmail", "mail", "e-posta", "eposta", "epostam", "mailim", "takvim", "calendar", "drive", "youtube hesab", "google hesab", "kendi hesab", "hesabım", "hesabim"],
  "account": {
    "id": "google_main",
    "loginUrl": "https://accounts.google.com/",
    "loggedInUrl": "https://myaccount.google.com/",
    "loginCheckSelector": "a[href*='SignOutOptions'], a[aria-label*='Google Hesab'], a[aria-label*='Google Account']"
  },
  "priority": 8,
  "toolReferences": [
    {
      "tool": "PythonTool",
      "path": "shared_profile_web_runner.py",
      "description": "account.json icindeki ortak Chrome profilini kullanarak Google sayfalarini acar.",
      "usage": "runner=shared_profile_web_runner.py + args=[--inline-config,<json>]"
    },
    {
      "tool": "ShellTool",
      "path": "account-login.sh",
      "description": "Ortak profilin ilk manuel Google girisi icin noVNC yardimcisi.",
      "usage": "command=cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/account-login.sh google_main"
    }
  ]
}
---
Hesap Modu — Google:
Kullanici Gmail, Google Drive, Takvim veya benzeri kisisel Google hesabi gerektiren bir istekte bulundugunda eski `accountId` ve `runner=webbrowser` akisini kullanma. Ilk tercih, ortak Chrome profilini otomatik inject eden `shared_profile_web_runner.py` olmalidir.

- Google tarafinda urune dogrudan git: Gmail icin `mail.google.com`, Drive icin `drive.google.com`, Takvim icin `calendar.google.com`.
- Ortak profil zaten VNC uzerinden sifresiz Google oturumu aciyorsa, ayni oturumu tekrar kullanmak icin `shared_profile_web_runner.py` sec.
- `shared_profile_web_runner.py`, `account.json` icindeki `defaultSessionBrowserProfileId` profilini yukler. Boylece agent bos Playwright profiliyle degil, ortak Chromium profiliyle acilir.
- Gerekirse config icinde `account` alanini da ver; boylece runner login durumunu dogrulayabilir ve kullanicidan manuel giris isteyebilir.
- Sifre veya e-posta bilgisini ASLA `steps` icine yazma.
- Yalnizca ortak profilde giris yoksa veya ilk kurulum gerekiyorsa `ShellTool` ile `scripts/account-login.sh google_main` calistir; kullanici noVNC penceresinde girisi tamamlasin; sonra ayni PythonTool cagrısını tekrar dene.

Ornek istek: "gmail hesabimda ilk mail nedir?"

1. Ilk deneme ortak profille yapilir:
{
  "tool": "PythonTool",
  "arguments": {
    "runner": "shared_profile_web_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"artifactsDir\":\".voice_agent_browser/artifacts/google\",\"headless\":false,\"account\":{\"id\":\"google_main\",\"displayName\":\"Google Main\",\"loginUrl\":\"https://accounts.google.com/\",\"loggedInUrl\":\"https://myaccount.google.com/\",\"loginCheckSelector\":\"a[href*='SignOutOptions'], a[aria-label*='Google Hesab'], a[aria-label*='Google Account']\",\"manualLoginTimeoutSeconds\":180},\"steps\":[{\"action\":\"goto\",\"url\":\"https://mail.google.com/mail/u/0/#inbox\"},{\"action\":\"wait_for_selector\",\"selector\":\"table[role='grid'] tr\",\"timeoutMs\":30000},{\"action\":\"extract_text\",\"selector\":\"table[role='grid'] tr:nth-child(1)\",\"maxLength\":500}]}"
    ]
  }
}

2. Eger login yoksa, once su yardimciyi calistir ve sonra ayni PythonTool cagrısını tekrar et:
{
  "tool": "ShellTool",
  "arguments": {
    "command": "cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/account-login.sh google_main"
  }
}

Not: Google icin hesapli gezinmede ilk tercih `shared_profile_web_runner.py` olmalidir. Daha ozel ve tekrar kullanilabilir bir Google otomasyonu gerekiyorsa `ProjectFilesTool` ile `scripts/` altinda yeni bir yardimci script olustur ve yine ortak profili kullan.