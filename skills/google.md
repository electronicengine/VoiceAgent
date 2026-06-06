---
{
  "name": "google_account",
  "description": "Gmail / Google Drive / Takvim / YouTube gibi google hesaplarına erişim sağlamak ve işlem yapmak için kullanılacak yetenek",
  
}
---

Kullanici Gmail, Google Drive, Takvim veya benzeri kisisel Google hesabi gerektiren bir istekte bulundugunda PythonTool vasıtası ile wb_runner.py scriptini kullan.

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

- Google tarafinda urune dogrudan git: 
Gmail icin `mail.google.com`, 
Drive icin `drive.google.com`, 
Takvim icin `calendar.google.com`.
Tablo ve dökümanlar için 'https://docs.google.com/'

Kullanıcı bir adrese mail göndermeni istediğinde:

1-) web tarayıcı ile mail.google.com adresine git.
2-) Compose ("Oluştur") butonunu bekle ve tıkla, 
3-) Alıcı (To), Konu (Subject), İleti Gövdesi (Body)   alanlarını doldur.
4-) Her doldurmada sonra diğerine geçmek için `{"action":"press","key":"Tab"}` ekleyebilirsin.
5-) Gönder butonuna bas. buton, çalışmazsa Ctrl+Enter klavye kısayolu kullan.
