---
{
  "name": "account_login_recovery",
  "description": "Hesap oturumu yoksa kullaniciyi manuel girise yonlendirir, sonra ayni islemi tekrar dener.",
  "priority": 100,
  "alwaysOn": true
}
---
Hesap / site giris kurtarma kurali:

Bu skill HER TURDA aktiftir. Amaci Google, LinkedIn, GitHub, X/Twitter veya gelecekte eklenecek baska herhangi bir `accountId` icin ilk manuel girisi dogru yonetmektir. Ayrica account.json'da TANIMLI OLMAYAN siteler icin de kalici bir `sessionId` uzerinden manuel giris akisini baslatir.

Eger bir `WebBrowserTool` sonucu BASARISIZ olduysa ve arac sonucunda asagidaki bilgilerden biri varsa:

- `Reason: no_display_for_manual_login`
- `Reason: account_login_required`
- `Reason: session_login_required`
- `Cikti` icinde `"accountId": "..."`
- `Cikti` icinde `"sessionId": "..."`
- `Cikti` icinde `"recoveryCommand": "..."`
- `Cikti.output.loginStatus` hesabin giris gerektirdigini soyluyorsa

o zaman FINAL cevap verme. Su akisi uygula:

1. `recoveryCommand` alanindaki komutu `ShellTool` ile CALISTIR.
2. Kullanici noVNC / tarayici penceresinde manuel girisi ve gerekirse iki asamali dogrulamayi tamamlasin.
3. `ShellTool` basarili olduktan sonra, az once basarisiz olan AYNI `WebBrowserTool` cagrısını AYNI `accountId` ve AYNI `steps` ile TEKRAR DENE.
4. Ikinci deneme basariliysa normal goreve devam et ve final cevap ver.

Kurallar:

- Bu kurtarma akisi `linkedin_main`, `google_main`, `github_main`, `twitter_main` veya baska herhangi bir hesap icin aynidir.
- Account.json'da TANIMLI OLMAYAN sitelerde `sessionId` kullan. Ornek: `vapi_main`, `notion_main`, `openai_main`.
- Kullaniciya "terminalde su komutu sen calistir" deme; komutu kendin `ShellTool` ile calistir.
- Kullaniciya sadece manuel giris / iki asamali dogrulama kismi icin yonlendirme yap.
- `recoveryCommand` calistiktan sonra AYNI web gorevini yeniden denemeden final cevap verme.
- `accountId` kullaniyorsan `headless` veya `useChromeProfile` ekleme; mevcut cagrinin mantigini bozma.
- `sessionId` kullaniyorsan `headless` veya `useChromeProfile` ekleme; `sessionLoginUrl` ve `sessionLoggedInUrl` alanlarini koru.

Account.json'da olmayan siteler icin manuel login akisi:

Kullanici yeni bir siteye manuel giris yapmak istediginde veya bir site login sayfasina dustuyse ve `accountId` yoksa:

1. Site icin stabil bir `sessionId` sec. Ornek: Vapi.ai -> `vapi_main`.
2. Login URL'yi ve login sonrasi donulecek URL'yi belirle.
3. DOGRUDAN `ShellTool` ile su formda komut calistir:

{
  "tool": "ShellTool",
  "arguments": {
    "command": "cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/site-login.sh --session-id vapi_main --display-name 'Vapi.ai' --login-url 'https://dashboard.vapi.ai/sign-in' --logged-in-url 'https://dashboard.vapi.ai/'"
  }
}

4. ShellTool basarili olduktan sonra AYNI site icin sonraki `WebBrowserTool` cagrilarinda `sessionId`, `sessionDisplayName`, `sessionLoginUrl`, `sessionLoggedInUrl` (ve biliyorsan `sessionLoginCheckSelector`) alanlarini ekle.
5. Boylece ayni site sonraki turlarda kalici profille tekrar acilir.

Generic site ornegi:

{
  "tool": "WebBrowserTool",
  "arguments": {
    "sessionId": "vapi_main",
    "sessionDisplayName": "Vapi.ai",
    "sessionLoginUrl": "https://dashboard.vapi.ai/sign-in",
    "sessionLoggedInUrl": "https://dashboard.vapi.ai/",
    "steps": [
      { "action": "goto", "url": "https://dashboard.vapi.ai/" },
      { "action": "snapshot", "maxLength": 2000 }
    ]
  }
}

Ornek kurtarma mantigi:

Ilk cagri basarisiz olduysa ve tool sonucu sunlari iceriyorsa:

{
  "reason": "no_display_for_manual_login",
  "accountId": "linkedin_main",
  "recoveryCommand": "cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/account-login.sh linkedin_main"
}

siradaki adim DOGRUDAN su olmali:

{
  "tool": "ShellTool",
  "arguments": {
    "command": "cd /home/kufi/workspace/voiceAgent && KEEP_NOVNC=1 bash scripts/account-login.sh linkedin_main"
  }
}

Bu basarili olduktan sonra, onceki WebBrowserTool cagrısını tekrar et.