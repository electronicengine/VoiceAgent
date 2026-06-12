---
{
  "name": "linkedin_account",
  "description": "LinkedIn sosyal medya hesabı, kullanım yeteneği",
}
---
Kullanıcı linkedin ile ilgili sosyal medya hesabı kontrolü, mesajlarının okunması, bildirimlerinin kontrol edilmesi veya iş ilanları ve başvuruları ile ilgili bir istekte bulunduğunda PythonTool vasıtası ile wb_runner.py scriptini kullan.

wb_runner.py kullanım taslağı:
{
  "tool": "PythonTool",
  "arguments": {
    "script": "wb_runner.py",
    "packages": ["playwright"],
    "args": [
      "--inline-config",
      "{\"artifactsDir\":\".voice_agent_browser/artifacts/<gorev-adi>\",\"headless\":false,\"steps\":[{\"action\":\"goto\",\"url\":\"<HEDEF_URL>\"},{\"action\":\"dismiss_popups\",\"maxPasses\":2},{\"action\":\"wait_for_selector\",\"selector\":\"<CSS_SELECTOR>\",\"timeoutMs\":30000},{\"action\":\"snapshot\",\"maxLength\":3000}]}"
    ]
  }
}


Sayfalarda gezinirken ve bir sayfaya girdiğinde sayfanın tam olarak açıldığına emin olana dek bekle. belki 3,4 sn bekleyebilirsin. 

* Linkedin sayfası ilk açıldıiında reklam, restore veya interstitial sayfası veya pop up ı çıkabilir.  Bu, oturumun kapalı veya işlevsiz olduğu anlamına gelmez. Böyle bir durumda `steps` içine ilk iş olarak `click_first` ekleyip  "Back to LinkedIn", "Restore", "Continue to LinkedIn" benzeri butonlarla bu reklam sayfasını kapat. 
* Linkedin bildirimleri ile ilgili işlemler için doğrudan `https://www.linkedin.com/notifications/` sayfasına giderek ilgili işlemi yap.
* Linkedin Mesajları ile ilgili işlemler için dogrudan `https://www.linkedin.com/messaging/` sayfasina giderek ilgili işlemleri gerçekleştir.

