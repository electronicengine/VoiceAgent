---
{
  "name": "linkedin_account",
  "description": "LinkedIn icin ortak Chrome profilli explicit script akisi.",
}
---

Kullanici LinkedIn baglanti istekleri, mesajlar, profil veya is ilanlari gibi hesapli bir is istediginde eski `accountId` ve `runner=webbrowser` akisini kullanma. Ilk tercih, ortak Chrome profilini otomatik kullanan `wb_runner.py` olmalidir.

* Sayfa ilk acildiginda reklam, restore veya interstitial sayfasi cikabilir. Bu, oturumun kapali oldugu anlamina gelmez. Boyle durumda `steps` icine ilk is olarak `click_first` ekleyip "Back to LinkedIn", "Restore", "Continue to LinkedIn" benzeri butonlarla ana uygulamaya don.
* LinkedIn'de kesif yapma. Kullanici ne istiyorsa dogrudan ilgili URL'ye git.
* Bildirim istiyorsa dogrudan `https://www.linkedin.com/notifications/` sayfasina git.
* Mesaj istiyorsa dogrudan `https://www.linkedin.com/messaging/` sayfasina git.

- Ortak profil zaten VNC uzerinden sifresiz LinkedIn oturumu aciyorsa, ayni oturumu tekrar kullanmak icin `wb_runner.py` sec.
- `wb_runner.py`, `account.json` icindeki `defaultSessionBrowserProfileId` profilini varsayilan olarak yukler. Boylece agent bos Playwright profiliyle degil, ortak Chromium profiliyle acilir.
- Gerekirse config icinde `account` alanini da ver; boylece runner login durumunu dogrulayabilir ve kullanicidan manuel giris isteyebilir.
- Sifre veya e-posta bilgisini ASLA `steps` icine yazma.
- Yalnizca ortak profilde giris yoksa veya ilk kurulum gerekiyorsa `ShellTool` ile `scripts/account-login.sh linkedin_main` calistir; kullanici noVNC penceresinde girisi tamamlasin; sonra ayni PythonTool cagrisini tekrar dene.

LinkedIn feed / ana sayfa:
{
  "tool": "PythonTool",
  "arguments": {
    "script": "wb_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"artifactsDir\":\".voice_agent_browser/artifacts/linkedin-feed\",\"headless\":false,\"account\":{\"id\":\"linkedin_main\",\"displayName\":\"LinkedIn Main\",\"loginUrl\":\"https://www.linkedin.com/login\",\"loggedInUrl\":\"https://www.linkedin.com/feed/\",\"loginCheckSelector\":\".global-nav__me-photo, .global-nav__primary-link[href*='/feed'], a[href='/feed/'], nav[aria-label*='Primary']\",\"manualLoginTimeoutSeconds\":180},\"steps\":[{\"action\":\"goto\",\"url\":\"https://www.linkedin.com/feed/\"},{\"action\":\"click_first\",\"selectors\":[{\"textSelector\":\"Back to LinkedIn\"},{\"textSelector\":\"Restore\"},{\"textSelector\":\"Continue to LinkedIn\"},{\"textSelector\":\"Geri dön\"},{\"textSelector\":\"Geri don\"}],\"requireSuccess\":false},{\"action\":\"dismiss_popups\",\"maxPasses\":2},{\"action\":\"wait_for_selector\",\"selector\":\".feed-container-theme, main\",\"timeoutMs\":30000},{\"action\":\"snapshot\",\"maxLength\":3000}]}"
    ]
  }
}

LinkedIn bildirimleri:
{
  "tool": "PythonTool",
  "arguments": {
    "script": "wb_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"artifactsDir\":\".voice_agent_browser/artifacts/linkedin-notifications\",\"headless\":false,\"account\":{\"id\":\"linkedin_main\",\"displayName\":\"LinkedIn Main\",\"loginUrl\":\"https://www.linkedin.com/login\",\"loggedInUrl\":\"https://www.linkedin.com/feed/\",\"loginCheckSelector\":\".global-nav__me-photo, .global-nav__primary-link[href*='/feed'], a[href='/feed/'], nav[aria-label*='Primary']\",\"manualLoginTimeoutSeconds\":180},\"steps\":[{\"action\":\"goto\",\"url\":\"https://www.linkedin.com/notifications/\"},{\"action\":\"click_first\",\"selectors\":[{\"textSelector\":\"Back to LinkedIn\"},{\"textSelector\":\"Restore\"},{\"textSelector\":\"Continue to LinkedIn\"},{\"textSelector\":\"Geri dön\"},{\"textSelector\":\"Geri don\"}],\"requireSuccess\":false},{\"action\":\"dismiss_popups\",\"maxPasses\":2},{\"action\":\"wait_for_selector\",\"selector\":\".nt-card-list, .notification-card, main\",\"timeoutMs\":30000},{\"action\":\"extract_text\",\"selector\":\".nt-card-list, main\",\"maxLength\":2500}]}"
    ]
  }
}

LinkedIn mesajlari:
{
  "tool": "PythonTool",
  "arguments": {
    "script": "wb_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"artifactsDir\":\".voice_agent_browser/artifacts/linkedin-messages\",\"headless\":false,\"account\":{\"id\":\"linkedin_main\",\"displayName\":\"LinkedIn Main\",\"loginUrl\":\"https://www.linkedin.com/login\",\"loggedInUrl\":\"https://www.linkedin.com/feed/\",\"loginCheckSelector\":\".global-nav__me-photo, .global-nav__primary-link[href*='/feed'], a[href='/feed/'], nav[aria-label*='Primary']\",\"manualLoginTimeoutSeconds\":180},\"steps\":[{\"action\":\"goto\",\"url\":\"https://www.linkedin.com/messaging/\"},{\"action\":\"click_first\",\"selectors\":[{\"textSelector\":\"Back to LinkedIn\"},{\"textSelector\":\"Restore\"},{\"textSelector\":\"Continue to LinkedIn\"},{\"textSelector\":\"Geri dön\"},{\"textSelector\":\"Geri don\"}],\"requireSuccess\":false},{\"action\":\"dismiss_popups\",\"maxPasses\":2},{\"action\":\"wait_for_selector\",\"selector\":\".msg-conversations-container, .msg-overlay-list-bubble, main\",\"timeoutMs\":30000},{\"action\":\"extract_text\",\"selector\":\".msg-conversations-container, main\",\"maxLength\":3000}]"
    ]
  }
}

Not: LinkedIn mesaj akışında eski `.msg-s-message-list__event.clearfix` benzeri selector'lar yerine önce inbox kapsayıcısını bekle, sonra görünen metni `extract_text` ile al. Thread görünümü açılmışsa da `main` üzerinden okuma daha stabildir.

Eger ortak profilde login yoksa, once su yardimciyi calistir ve sonra ayni PythonTool cagrisini tekrar et:
{
  "tool": "ShellTool",
  "arguments": {
    "command": "cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/account-login.sh linkedin_main"
  }
}

Not: LinkedIn icin hesapli gezinmede ilk tercih `wb_runner.py` olmalidir. Daha ozel ve tekrar kullanilabilir bir LinkedIn otomasyonu gerekiyorsa `ProjectFilesTool` ile `scripts/` altinda yeni bir yardimci script olustur ve yine ortak profili kullan. "

