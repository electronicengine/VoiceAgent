---
{
  "name": "music",
  "description": "Müzik ve şarkı çalmak ve durdurmak için kullanılan yetenektir. Örneğin Tarkan'dan Dudu Dudu Dilleri çal.",
}
---
Müzik / Şarkı Çalma:
Kullanıcı "müzik çal", "şarkı çal", "X şarkısını aç", "YT Music'ten Y çal" gibi bir istekte bulunduğunda browser otomasyonu kullanma — headless tarayıcının ses çıkışı yok ve YT Music DRM gerektiriyor. Bunun yerine ShellTool ile mpv + yt-dlp kombinasyonunu kullan. mpv yt-dlp aracılığıyla YouTube'dan ses akışı çekip robotun PipeWire/Pulse hoparlör sink'ine basar.

Müzik çalmak için ilgili shell komutunu ShellTool ile ilgili komutları çalıştırarak kullan.

Çalma kuralları:
- Yeni bir parça çalmadan önce çalan başka bir mpv varsa kapat: `pkill -x mpv 2>/dev/null; sleep 0.3`.
- Komutu MUTLAKA `setsid -f` ile arka planda detach ederek çalıştır (popen/sh shell'i ile uyumlu, nohup/disown gerekmiyor).
- Çıkışı `/tmp/mpv.log`'a yönlendir; sorun olursa kullanıcıya `tail /tmp/mpv.log` ile bakabilirsin.
- Ses çıkışını `--ao=pulse` ile zorla (PipeWire'in pulse uyumluluğu üzerinden default sink'e gider).
- yt-dlp format seçici olarak DAİMA `--ytdl-format=bestaudio/best` kullan (yalnız `bestaudio` bazı videolarda yok ve "Requested format is not available" hatası verir; `/best` fallback bunu çözer).
- Sistem yt-dlp'si eski; mpv DAİMA venv'deki yenisini kullanmalı: `--script-opts=ytdl_hook-ytdl_path=/home/kufi/workspace/voiceAgent/.venv/bin/yt-dlp`. Bu flag olmadan "Only images are available" hatası alırsın.
- mpv'yi DAİMA `--volume=55` ile başlat. Yüksek seste mikrofon kullanıcıyı duymaz; %55 ses, kullanıcının "durdur" gibi komutlarını söylemesine olanak verir. Kullanıcı "sesini aç" derse `wpctl` ile sistem sesini artır, mpv volume'unu sabit bırak.
- Arama metnini tek tırnak içine al; Türkçe karakterler sorun değil.

Şarkı çalma örneği — "Tarkan'dan Dudu çal":
{
  "tool": "ShellTool",
  "arguments": {
    "command": "pkill -x mpv 2>/dev/null; sleep 0.3; setsid -f mpv --no-video --ao=pulse --volume=55 --script-opts=ytdl_hook-ytdl_path=/home/kufi/workspace/voiceAgent/.venv/bin/yt-dlp --msg-level=all=warn --ytdl-format=bestaudio/best 'ytdl://ytsearch1:Tarkan Dudu' >/tmp/mpv.log 2>&1; sleep 2; pgrep -af mpv >/dev/null && echo started || (echo failed; tail -n 20 /tmp/mpv.log)"
  }
}
Komut çıktısı "started" değilse mpv başlamamıştır; logu kullanıcıya özetle. Çıktı "started" ise şarkı çalıyor demektir.

Müziği durdurma — Kullanıcı şarkı/müzik çalarken aşağıdaki ifadelerden BİRİNİ söylediğinde MUTLAKA mpv'yi öldür. Tereddüt etme, izin isteme, doğrula deme — direkt çalıştır:
  - "müziği/şarkıyı durdur", "durdur şarkıyı", "kapat şarkıyı/müziği", "sustur", "kes şu şarkıyı", "şarkı/müzik dursun", "yeter artık", "kapat şunu", "stop", "kapat", "sus".
  - Hatta sadece "durdur" veya "kapat" gibi tek kelimelik komutta bile mpv çalıyorsa BU ARAÇ ÇAĞRISINI yap.
{
  "tool": "ShellTool",
  "arguments": { "command": "pkill -x mpv 2>/dev/null; pkill -f 'mpv ' 2>/dev/null; sleep 0.3; pkill -9 -x mpv 2>/dev/null; pkill -9 -f 'mpv ' 2>/dev/null; sleep 0.2; pgrep -x mpv >/dev/null && echo still_running || echo stopped" }
}

Ses seviyesi ayarlama (PipeWire default sink):
- Aç: `wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+`
- Kıs: `wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-`
- Belirli yüzde (ör. %60): `wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.6`
- Sustur/aç: `wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle`
