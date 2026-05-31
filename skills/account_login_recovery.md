---
{
  "name": "account_login_recovery",
  "description": "Hesap oturumu yoksa kullaniciyi manuel girise yonlendirir, sonra ayni islemi tekrar dener.",
  "priority": 100,
  "alwaysOn": true
}
---
Hesap giris kurtarma kurali:

Bu skill HER TURDA aktiftir. Amaci Google, LinkedIn, GitHub, X/Twitter veya gelecekte eklenecek baska herhangi bir `accountId` icin ilk manuel girisi dogru yonetmektir.

Eger bir `WebBrowserTool` sonucu BASARISIZ olduysa ve arac sonucunda asagidaki bilgilerden biri varsa:

- `Reason: no_display_for_manual_login`
- `Reason: account_login_required`
- `Cikti` icinde `"accountId": "..."`
- `Cikti` icinde `"recoveryCommand": "..."`
- `Cikti.output.loginStatus` hesabin giris gerektirdigini soyluyorsa

o zaman FINAL cevap verme. Su akisi uygula:

1. `recoveryCommand` alanindaki komutu `ShellTool` ile CALISTIR.
2. Kullanici noVNC / tarayici penceresinde manuel girisi ve gerekirse iki asamali dogrulamayi tamamlasin.
3. `ShellTool` basarili olduktan sonra, az once basarisiz olan AYNI `WebBrowserTool` cagrısını AYNI `accountId` ve AYNI `steps` ile TEKRAR DENE.
4. Ikinci deneme basariliysa normal goreve devam et ve final cevap ver.

Kurallar:

- Bu kurtarma akisi `linkedin_main`, `google_main`, `github_main`, `twitter_main` veya baska herhangi bir hesap icin aynidir.
- Kullaniciya "terminalde su komutu sen calistir" deme; komutu kendin `ShellTool` ile calistir.
- Kullaniciya sadece manuel giris / iki asamali dogrulama kismi icin yonlendirme yap.
- `recoveryCommand` calistiktan sonra AYNI web gorevini yeniden denemeden final cevap verme.
- `accountId` kullaniyorsan `headless` veya `useChromeProfile` ekleme; mevcut cagrinin mantigini bozma.

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